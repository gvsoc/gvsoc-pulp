/*
 * Copyright (C) 2024 ETH Zurich, University of Bologna, and Fondazione Chips-IT
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Chi Zhang,       ETH Zurich                       chizhang@iis.ee.ethz.ch
            Lorenzo Zuolo,   Chips-IT                         lorenzo.zuolo@chips.it
            Alex Marchioni,  Chips-IT                         alex.marchioni@chips.it
            Francesco Conti, University of Bologna & Chips-IT f.conti@unibo.it
            Yinrong Li,      ETH Zurich                       yinrli@student.ethz.ch
 * Note:
 *     The model currently supports:
 *       - GEMM mode:   Z = X @ W + Y
 *       - MATMUL mode: Z = X @ W
 *     Other GEMM-Ops may be supported in a future release.
 *
 * io_v2 port of light_redmule.cpp.
 *
 */

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include <list>
#include <queue>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <vp/register.hpp>
#include <climits>

#include <cpu/iss/include/offload.hpp>
#include <cpu/iss/flexfloat/flexfloat.h>

/****************************************************
*                   Type Definition                 *
****************************************************/

typedef uint8_t  fp8e4m3;
typedef uint16_t fp16;

enum redmule_state {
    IDLE,
    PRELOAD,
    ROUTINE,
    STORING,
    FINISHED,
    ACKNOWLEDGE
};

// Cycles from the job trigger to the first stream request, as in the RTL:
// trigger -> start_cfg (1) -> tiler sequential multipliers -> tiler valid (3)
// -> scheduler IDLE->PRELOAD (2) -> streamer address generators (2).
static constexpr int JOB_START_LATENCY = 8;

// W rows loaded ahead of the compute: HCI load FIFO (4, redmule_streamer.sv) +
// i_w_buffer_fifo (4, redmule_top.sv) + 1 in flight.
static constexpr uint32_t W_PREFETCH_ROWS = 9;

// The Y stream feeds the z_buffer one row every 4 cycles (redmule_streamer.sv). The
// compute starts Y_ROW_CYCLES * (ce_height - 1) + 1 cycles after the first preload access.
static constexpr int64_t Y_ROW_CYCLES = 4;

// gemm_op_e values of MCNFIG1.gemm_ops (redmule_pkg.sv). Only these two are
// modeled; with MATMUL the RTL never starts the Y stream and feeds a zero bias.
static constexpr uint32_t GEMM_OP_MATMUL = 0;
static constexpr uint32_t GEMM_OP_GEMM   = 1;

enum iter_instruction {
    INSTR_LOAD_Y,
    INSTR_LOAD_W,
    INSTR_LOAD_W_COMPUTE,
    // W row past N (see get_w_rows()): timing only, gated to zero by the RTL
    INSTR_LOAD_W_PAD,
    // Y/Z row past M in the last M tile (see get_y_z_pad_rows()): the Y load is
    // timing only, the Z store writes zeroes
    INSTR_LOAD_Y_PAD,
    INSTR_STOR_Z_PAD,
    INSTR_LOAD_X,
    INSTR_STOR_Z,
    INSTR_FORWARD_YZ
};

/********************************************************
*                   Function Definition                 *
********************************************************/

void matmul_uint16(uint16_t * z, uint16_t * y, uint16_t * x, uint16_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size);
void matmul_int16(int16_t * z, int16_t * y, int16_t * x, int16_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size);
void matmul_uint8(uint8_t * z, uint8_t * y, uint8_t * x, uint8_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size);
void matmul_int8(int8_t * z, int8_t * y, int8_t * x, int8_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size);
void matmul_fp8e4m3(fp8e4m3 * z, fp8e4m3 * y, fp8e4m3 * x, fp8e4m3 * w, uint16_t m_size, uint16_t n_size, uint16_t k_size);

/*****************************************************
*                   Class Definition                 *
*****************************************************/


class LightRedmule : public vp::Component
{

public:
    LightRedmule(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus req_v2(vp::Block *__this, vp::IoReq *req);
    // Method for offload interface, called when the core is offloading an instruction
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    //static void offload_grant(vp::Block *__this, IssOffloadInsnGrant<iss_reg_t> *result);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    uint32_t op_foramt_parser(uint32_t op_format);

    // HWPE control-register protocol (ACQUIRE / COMMIT_TRIGGER / SOFT_CLEAR)
    int32_t  acquire();
    void     commit(bool start);
    void     start_next_job();
    bool     is_gemm() const { return this->gemm_ops != GEMM_OP_MATMUL; }
    void     soft_clear(uint32_t value);
    void init_redmule_meta_data();
    uint32_t tmp_next_addr();
    uint32_t next_addr();
    uint32_t calculate_tile_base_address(uint32_t base, uint32_t stride, uint32_t tile_col, uint32_t tile_row, uint32_t i, uint32_t j);
    uint32_t inc_addr(uint32_t addr, uint32_t stride, uint32_t tile_row);
    uint32_t next_iteration();
    uint32_t get_redmule_array_runtime();
    uint32_t get_w_rows(uint32_t iter_k);
    uint32_t get_y_z_pad_rows(uint32_t rows);
    uint32_t get_routine_access_block_number();
    uint32_t get_preload_access_block_number();
    uint32_t get_storing_access_block_number();
    uint32_t get_routine_to_storing_latency();
    void     process_iter_instruction();
    void     process_iter_instruction(uint8_t *buf, uint32_t instr);
    void     process_compute();

    // TCDM callbacks: response for GRANTED requests, retry after a DENIED one.
    static vp::IoRespAck tcdm_response(vp::Block *__this, vp::IoReq *req);
    static void tcdm_retry(vp::Block *__this, vp::IoRetryChannel channel);
    int  alloc_req_slot();
    void free_req_slot(int id, int64_t available_at);
    // Iteration end-condition: all slots reusable at the current cycle.
    bool all_slots_free();
    // True if another request can be issued (see definition).
    bool issue_slot_gate_ok();
    // Issue one request; returns false only if the slot pool is exhausted.
    bool issue_request(uint32_t addr, uint32_t instr, bool is_write, uint32_t size);
    // Consume a response (DONE inline or GRANTED); the slot is reusable from free_cycle.
    void handle_response(int slot_id, int64_t free_cycle);
    // Apply the data movement using the values captured at issue time.
    void apply_response_data(uint8_t *buf, uint32_t instr, uint32_t dst_offset, uint32_t cutoff);
    // Called when the last W row lands: run the matmul and clear w_buffer.
    void run_compute_and_drain();
    void     matmul_fp16(fp16 * z, fp16 * y, fp16 * x, fp16 * w, uint16_t m_size, uint16_t n_size, uint16_t k_size);

    vp::Trace           trace;
    vp::IoSlave         input_itf{&LightRedmule::req};
    vp::IoSlave         input_itf_v2{&LightRedmule::req_v2};
    // Interface from which the instructions are received from the core
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf;
    // Interface for granting previously stalled redmule offload
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf;
    vp::WireMaster<bool> done;

    vp::IoMaster        tcdm_itf{&LightRedmule::tcdm_retry, &LightRedmule::tcdm_response};

    //redmule fsm
    vp::IoReq *         redmule_query;
    // Port the parked query came in on, to respond on.
    vp::IoSlave *       redmule_query_port;
    vp::IoReq*          tcdm_req;
    vp::ClockEvent *    fsm_event;
    vp::Register<uint32_t>  reg_fsm_state;
    vp::Register<uint32_t>  reg_busy;
    uint32_t            tcdm_block_total;
    uint32_t            fsm_counter;
    uint32_t            fsm_timestamp;
    int64_t             timer_start;
    int64_t             cycle_start;
    int64_t             total_runtime;
    int64_t             num_matmul;

    //HWPE job queue (depth 2, double-buffered)
    uint8_t             job_id_counter;   //wraps at 256
    int                 job_state;        //0 = free to acquire, -2 = acquired-not-committed
    int                 job_pending;      //0..2 committed-but-not-finished jobs
    int                 cxt_cfg_ptr;      //context slot software is configuring
    int                 cxt_use_ptr;      //context slot the FSM is executing
    int                 cxt_job_id[2];    //job id per context slot, -1 = empty
    int                 running_job_id;   //current/last running job id, -1 = none yet
    uint8_t             completed_jobs;   //RUNNING_JOB: jobs completed so far (job_running_id_q)
    uint32_t            cxt_m_size[2];
    uint32_t            cxt_n_size[2];
    uint32_t            cxt_k_size[2];
    uint32_t            cxt_x_addr[2];
    uint32_t            cxt_w_addr[2];
    uint32_t            cxt_z_addr[2];
    uint32_t            cxt_y_offs[2];
    uint32_t            cxt_gemm_ops[2];
    uint32_t            cxt_compute_able[2];

    //redmule configuration
    uint32_t            tcdm_bank_width;
    uint32_t            tcdm_bank_number;
    uint32_t            elem_size;
    uint32_t            ce_height;
    uint32_t            ce_width;
    uint32_t            ce_pipe;
    uint32_t            queue_depth;
    uint32_t            bandwidth;
    uint32_t            fold_tiles_mapping;
    uint64_t            loc_base;
    uint32_t            compute_able;
    uint32_t            LOCAL_BUFFER_H;
    uint32_t            LOCAL_BUFFER_N;
    uint32_t            LOCAL_BUFFER_W;

    //redmule registers
    uint32_t            m_size;
    uint32_t            n_size;
    uint32_t            k_size;
    uint32_t            x_addr;
    uint32_t            w_addr;
    uint32_t            y_addr;
    uint32_t            z_addr;
    uint32_t            y_offs;     //MCNFIG2: Y read at z_addr + y_offs (bytes)
    uint32_t            gemm_ops;   //MCNFIG1[22:20]: 0 = MATMUL, 1 = GEMM

    //redmule meta data
    uint32_t            x_row_tiles;
    uint32_t            x_row_lefts;
    uint32_t            x_col_tiles;
    uint32_t            x_col_lefts;
    uint32_t            w_row_tiles;
    uint32_t            w_row_lefts;
    uint32_t            w_col_tiles;
    uint32_t            w_col_lefts;
    uint32_t            z_row_tiles;
    uint32_t            z_row_lefts;
    uint32_t            z_col_tiles;
    uint32_t            z_col_lefts;
    uint32_t            iter_i;
    uint32_t            iter_j;
    uint32_t            iter_k;
    uint32_t            iter_x_addr;
    uint32_t            iter_w_addr;
    uint32_t            iter_y_addr;
    uint32_t            iter_z_addr;
    uint32_t            x_acc_block;
    uint32_t            w_acc_block;
    uint32_t            w_pad_block;      // W rows loaded for timing only (get_w_rows())
    uint32_t            w_rows_later;     // real W rows of the tile left to a later phase
    uint32_t            w_prefetch_real;  // real W rows of the first tile loaded by the preload
    uint32_t            w_prefetch_pad;   // padding W rows of the first tile loaded by the preload
    int64_t             preload_first_issue; // cycle of the first access of the preload
    uint32_t            y_pad_block;      // Y rows loaded for timing only (get_y_z_pad_rows())
    uint32_t            z_pad_block;      // Z rows stored for timing only (get_y_z_pad_rows())
    uint32_t            y_acc_block;
    uint32_t            z_acc_block;
    uint32_t            z_store_width;
    uint32_t            z_store_height;
    double              ideal_runtime;
    uint32_t            iter_instruction;
    uint32_t            iter_x_row_ptr;
    uint32_t            iter_w_row_ptr;
    uint32_t            iter_y_row_ptr;
    uint32_t            iter_z_row_ptr;

    //redmule buffer
    uint8_t *           access_buffer;
    uint8_t *           y_buffer_preload;
    uint8_t *           w_buffer;
    uint8_t *           x_buffer;       // current k-iter (read by compute)
    uint8_t *           x_buffer_next;  // next k-iter (written by LOAD_X resp)
    uint8_t *           z_buffer_compute;
    uint8_t *           z_buffer_previos;

    // Outstanding TCDM request.
    struct ReqSlot {
        vp::IoReq *     req;
        uint8_t *       buf;
        uint32_t        instr;
        // Captured at issue, so responses can be applied out of order.
        uint32_t        dst_offset;
        uint32_t        cutoff;
        // <= now: free; > now: busy until then; INT64_MAX: awaiting response or retry.
        int64_t         available_at;
    };
    std::vector<ReqSlot> req_slots;
    bool                request_denied;  // last issue was denied, await retry
    int                 denied_slot;     // slot held by that denied issue (-1 = none)
    // Compute fires when w_outstanding_count reaches 0 with compute_pending
    // set. X uses a double-buffer (x_buffer_next), swapped at iter_done.
    uint32_t            w_outstanding_count;
    bool                compute_pending;

};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new LightRedmule(config);
}

LightRedmule::LightRedmule(vp::ComponentConf &config)
    : vp::Component(config),
    reg_fsm_state(*this, "fsm_state", 32, true, IDLE),
    reg_busy(*this, "busy", 1, true, 0)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->input_itf);
    this->new_slave_port("input_v2", &this->input_itf_v2);
    this->new_master_port("tcdm", &this->tcdm_itf);

    // Declare offload slave interface where instructions will be offloaded
    this->offload_itf.set_sync_meth(&LightRedmule::offload_sync);
    this->new_slave_port("offload", &this->offload_itf, this);

    // Declare offload master interface for granting blocked transfers
    //this->offload_grant_itf.set_sync_meth(&LightRedmule::offload_grant);
    this->new_master_port("offload_grant", &this->offload_grant_itf, this);

    this->new_master_port("done_irq", &this->done, this);
    
    
    //Initialize configuration
    this->tcdm_bank_width   = get_js_config()->get("tcdm_bank_width")->get_int();
    this->tcdm_bank_number  = get_js_config()->get("tcdm_bank_number")->get_int();
    this->elem_size         = get_js_config()->get("elem_size")->get_int();
    this->ce_height         = get_js_config()->get("ce_height")->get_int();
    this->ce_width          = get_js_config()->get("ce_width")->get_int();
    this->ce_pipe           = get_js_config()->get("ce_pipe")->get_int();
    this->queue_depth       = get_js_config()->get("queue_depth")->get_int();
    this->fold_tiles_mapping= get_js_config()->get("fold_tiles_mapping")->get_int();
    this->loc_base          = get_js_config()->get("loc_base")->get_double();
    this->compute_able      = 0;
    this->bandwidth         = this->tcdm_bank_width * this->tcdm_bank_number;
    this->LOCAL_BUFFER_H    = this->ce_height;
    this->LOCAL_BUFFER_N    = this->bandwidth / this->elem_size;
    this->LOCAL_BUFFER_W    = this->ce_width * (this->ce_pipe + 1);

    //Initialize registers
    this->m_size            = 4;
    this->n_size            = 4;
    this->k_size            = 4;
    this->x_addr            = 0;
    this->w_addr            = 0;
    this->y_addr            = 0;
    this->z_addr            = 0;
    this->y_offs            = 0;
    this->gemm_ops          = GEMM_OP_GEMM;

    //Initialize redmule meta data
    this->x_row_tiles       = 0;
    this->x_row_lefts       = 0;
    this->x_col_tiles       = 0;
    this->x_col_lefts       = 0;
    this->w_row_tiles       = 0;
    this->w_row_lefts       = 0;
    this->w_col_tiles       = 0;
    this->w_col_lefts       = 0;
    this->z_row_tiles       = 0;
    this->z_row_lefts       = 0;
    this->z_col_tiles       = 0;
    this->z_col_lefts       = 0;
    this->iter_i            = 0;
    this->iter_j            = 0;
    this->iter_k            = 0;
    this->iter_x_addr       = 0;
    this->iter_w_addr       = 0;
    this->iter_y_addr       = 0;
    this->iter_z_addr       = 0;
    this->x_acc_block       = 0;
    this->w_acc_block       = 0;
    this->w_pad_block       = 0;
    this->w_rows_later      = 0;
    this->w_prefetch_real   = 0;
    this->w_prefetch_pad    = 0;
    this->y_pad_block       = 0;
    this->z_pad_block       = 0;
    this->y_acc_block       = 0;
    this->z_acc_block       = 0;
    this->z_store_width     = 0;
    this->z_store_height    = 0;
    this->ideal_runtime     = 0;
    this->iter_instruction  = INSTR_LOAD_Y;
    this->iter_x_row_ptr    = 0;
    this->iter_w_row_ptr    = 0;
    this->iter_y_row_ptr    = 0;
    this->iter_z_row_ptr    = 0;

    // Zero-init buffers (edge-case rows must start clean).
    this->access_buffer     = new uint8_t [this->bandwidth * 2]();
    this->y_buffer_preload  = new uint8_t [this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size]();
    this->w_buffer          = new uint8_t [this->LOCAL_BUFFER_N * this->LOCAL_BUFFER_W * this->elem_size]();
    this->x_buffer          = new uint8_t [this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_N * this->elem_size]();
    this->x_buffer_next     = new uint8_t [this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_N * this->elem_size]();
    this->z_buffer_compute  = new uint8_t [this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size]();
    this->z_buffer_previos  = new uint8_t [this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size]();

    //Initialize FSM
    this->reg_fsm_state.set(IDLE);
    this->reg_busy.set(0);
    this->redmule_query     = NULL;
    this->redmule_query_port = NULL;
    this->tcdm_req          = new vp::IoReq();
    this->fsm_event         = this->event_new(&LightRedmule::fsm_handler);
    this->tcdm_block_total  = 0;
    this->fsm_counter       = 0;
    this->fsm_timestamp     = 0;
    this->timer_start       = 0;
    this->cycle_start       = 0;
    this->total_runtime     = 0;
    this->num_matmul        = 0;

    //Initialize HWPE job queue
    this->job_id_counter    = 0;
    this->job_state         = 0;
    this->job_pending       = 0;
    this->cxt_cfg_ptr       = 0;
    this->cxt_use_ptr       = 0;
    this->cxt_job_id[0]     = -1;
    this->cxt_job_id[1]     = -1;
    this->running_job_id    = -1;
    this->completed_jobs    = 0;
    for (int i = 0; i < 2; i++) {
        this->cxt_m_size[i]       = 0;
        this->cxt_n_size[i]       = 0;
        this->cxt_k_size[i]       = 0;
        this->cxt_x_addr[i]       = 0;
        this->cxt_w_addr[i]       = 0;
        this->cxt_z_addr[i]       = 0;
        this->cxt_y_offs[i]       = 0;
        this->cxt_gemm_ops[i]     = GEMM_OP_GEMM;
        this->cxt_compute_able[i] = 0;
    }

    // Slot pool: queue_depth+1 entries; each owns an IoReq + bandwidth
    // buffer. The slot id travels in IoReq::initiator so a response can be
    // matched back to its slot.
    this->request_denied      = false;
    this->denied_slot         = -1;
    this->w_outstanding_count = 0;
    this->compute_pending     = false;

    int pool_size = (int)this->queue_depth + 1;
    this->req_slots.resize(pool_size);
    for (int i = 0; i < pool_size; i++)
    {
        ReqSlot &slot = this->req_slots[i];
        slot.req          = new vp::IoReq();
        slot.buf          = new uint8_t[this->bandwidth];
        slot.instr        = 0;
        slot.dst_offset   = 0;
        slot.cutoff       = 0;
        slot.available_at = 0;
        slot.req->initiator = (void *)(intptr_t)i;
    }

    this->trace.msg("[LightRedmule] Model Initialization Done! (slot pool size=%d)\n", pool_size);
}

void LightRedmule::init_redmule_meta_data(){
    uint32_t buffer_h = this->ce_height;
    uint32_t buffer_w = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n = this->bandwidth / this->elem_size;

    this->x_row_lefts = this->n_size % buffer_n;
    this->x_row_tiles = this->n_size / buffer_n + (this->x_row_lefts > 0 ? 1 : 0);

    this->x_col_lefts = this->m_size % buffer_h;
    this->x_col_tiles = this->m_size / buffer_h + (this->x_col_lefts > 0 ? 1 : 0);

    this->w_row_lefts = this->k_size % buffer_w;
    this->w_row_tiles = this->k_size / buffer_w + (this->w_row_lefts > 0 ? 1 : 0);

    this->w_col_lefts = this->x_row_lefts;
    this->w_col_tiles = this->x_row_tiles;
    this->z_row_lefts = this->w_row_lefts;
    this->z_row_tiles = this->w_row_tiles;
    this->z_col_lefts = this->x_col_lefts;
    this->z_col_tiles = this->x_col_tiles;

    this->iter_i = 0;
    this->iter_j = 0;
    this->iter_k = 0;

    this->iter_x_addr = this->x_addr;
    this->iter_w_addr = this->w_addr;
    this->iter_y_addr = this->y_addr;
    this->iter_z_addr = this->z_addr;

    this->x_acc_block = 0;
    this->w_acc_block = 0;
    this->w_pad_block = 0;
    this->y_pad_block = 0;
    this->z_pad_block = 0;
    this->y_acc_block = 0;
    this->z_acc_block = 0;

    this->iter_x_row_ptr = 0;
    this->iter_w_row_ptr = 0;
    this->iter_y_row_ptr = 0;
    this->iter_z_row_ptr = 0;

    // Reset the slot pool for the new job.
    this->w_outstanding_count = 0;
    this->compute_pending     = false;
    int64_t now = this->clock.get_cycles();
    for (auto &slot : this->req_slots) slot.available_at = now;

    this->ideal_runtime = 1.0 * (this->m_size * this->n_size * this->k_size)/( 1.0 * this->ce_height * this->ce_width);
}

uint32_t LightRedmule::next_iteration(){
    this->iter_k += 1;
    if (this->iter_k == this->x_row_tiles)
    {
        this->iter_k = 0;
        this->iter_j += 1;
        if (this->iter_j == this->z_row_tiles)
        {
            this->iter_j = 0;
            this->iter_i += 1;
            if (this->iter_i == this->z_col_tiles)
            {
                return 1;
            }
        }
    }
    return 0;
}

uint32_t LightRedmule::calculate_tile_base_address(uint32_t base, uint32_t stride, uint32_t tile_col, uint32_t tile_row, uint32_t i, uint32_t j){
    /*
    *       ----
    *   col |  |
    *       ----
    *        row
    */
    if (this->fold_tiles_mapping)
    {
        uint32_t tiles_per_row = (stride + tile_row - 1)/tile_row;
        return base + (i * tiles_per_row + j) * tile_row * tile_col * this->elem_size;
    } else {
        return base + (i * tile_col * stride + j * tile_row) * this->elem_size;
    }
}

uint32_t LightRedmule::inc_addr(uint32_t addr, uint32_t stride, uint32_t tile_row){
    if (this->fold_tiles_mapping)
    {
        return addr + tile_row * this->elem_size;
    } else {
        return addr + stride * this->elem_size;
    }
}

uint32_t LightRedmule::tmp_next_addr(){
    this->z_addr = (this->z_addr + this->bandwidth) % (this->ce_height * this->bandwidth);
    return this->z_addr;
}

uint32_t LightRedmule::next_addr(){
    uint32_t addr       = 0;
    uint32_t buffer_h   = this->ce_height;
    uint32_t buffer_w   = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n   = this->bandwidth / this->elem_size;

    // Y -> W -> X -> Z
    if (this->y_acc_block > 0)
    {
        addr = this->iter_y_addr;
        this->iter_y_addr = this->inc_addr(addr, this->k_size, buffer_w);
        this->y_acc_block -= 1;
        this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Address] Y tile at 0x%11x | #Y tile left %d\n", addr, this->y_acc_block);
        this->tcdm_req->set_is_write(0);
        this->tcdm_req->set_size(buffer_w * this->elem_size);
        this->iter_instruction  = INSTR_LOAD_Y;
    } else if (this->y_pad_block > 0)
    {
        // Rows past M, read after the real ones like the RTL does. Timing only.
        addr = this->iter_y_addr;
        this->iter_y_addr = this->inc_addr(addr, this->k_size, buffer_w);
        this->y_pad_block -= 1;
        this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Address] Y padding tile at 0x%11x | #Y padding tile left %d\n", addr, this->y_pad_block);
        this->tcdm_req->set_is_write(0);
        this->tcdm_req->set_size(buffer_w * this->elem_size);
        this->iter_instruction  = INSTR_LOAD_Y_PAD;
    } else if (this->w_pad_block > 0)
    {
        // Padding rows first, so that the last real W row still triggers the
        // compute. Timing only: same row address, nothing stored.
        addr = this->iter_w_addr;
        this->w_pad_block -= 1;
        this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Address] W padding tile at 0x%11x | #W padding tile left %d\n", addr, this->w_pad_block);
        this->tcdm_req->set_is_write(0);
        this->tcdm_req->set_size(buffer_w * this->elem_size);
        this->iter_instruction  = INSTR_LOAD_W_PAD;
    } else if (this->w_acc_block > 0)
    {
        addr = this->iter_w_addr;
        this->iter_w_addr = this->inc_addr(addr, this->k_size, buffer_w);
        this->w_acc_block -= 1;
        this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Address] W tile at 0x%11x | #W tile left %d\n", addr, this->w_acc_block);
        this->tcdm_req->set_is_write(0);
        this->tcdm_req->set_size(buffer_w * this->elem_size);
        if (this->w_acc_block == 0 && this->w_rows_later == 0)
        {
            this->iter_instruction  = INSTR_LOAD_W_COMPUTE;
        } else {
            this->iter_instruction  = INSTR_LOAD_W;
        }
    } else if (this->x_acc_block > 0)
    {
        addr = this->iter_x_addr;
        this->iter_x_addr = this->inc_addr(addr, this->n_size, buffer_n);
        this->x_acc_block -= 1;
        this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Address] X tile at 0x%11x | #X tile left %d\n", addr, this->x_acc_block);
        this->tcdm_req->set_is_write(0);
        this->tcdm_req->set_size(this->bandwidth);
        this->iter_instruction  = INSTR_LOAD_X;
    } else if (this->z_acc_block > 0)
    {
        addr = this->iter_z_addr;
        this->iter_z_addr = this->inc_addr(addr, this->k_size, buffer_w);
        this->z_acc_block -= 1;
        this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Address] Z tile at 0x%11x | #Z tile left %d\n", addr, this->z_acc_block);
        this->tcdm_req->set_is_write(1);
        this->tcdm_req->set_size(this->z_store_width * this->elem_size);
        this->iter_instruction  = INSTR_STOR_Z;
    } else if (this->z_pad_block > 0)
    {
        // Rows past M: the RTL writes zeroes with all byte enables set, clobbering
        // the memory after Z. Modeled as such.
        addr = this->iter_z_addr;
        this->iter_z_addr = this->inc_addr(addr, this->k_size, buffer_w);
        this->z_pad_block -= 1;
        this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Address] Z padding tile at 0x%11x | #Z padding tile left %d\n", addr, this->z_pad_block);
        this->tcdm_req->set_is_write(1);
        this->tcdm_req->set_size(this->z_store_width * this->elem_size);
        this->iter_instruction  = INSTR_STOR_Z_PAD;
    } else {
        this->trace.fatal("[LightRedmule][Address] INVALID redmule address iteration : No tiles to access\n");
    }

    return addr;
}

void LightRedmule::process_compute(){
    uint32_t buffer_h           = this->ce_height;
    uint32_t buffer_w           = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n           = this->bandwidth / this->elem_size;

    if (this->compute_able == 1)
    {
        //UINT16
        matmul_uint16(  (uint16_t *)this->z_buffer_compute,
                        (uint16_t *)this->z_buffer_compute,
                        (uint16_t *)this->x_buffer,
                        (uint16_t *)this->w_buffer,
                        (uint16_t)buffer_h,
                        (uint16_t)buffer_n,
                        (uint16_t)buffer_w);
    } else
    if (this->compute_able == 2)
    {
        //INT16
        matmul_int16(   (int16_t *)this->z_buffer_compute,
                        (int16_t *)this->z_buffer_compute,
                        (int16_t *)this->x_buffer,
                        (int16_t *)this->w_buffer,
                        (uint16_t)buffer_h,
                        (uint16_t)buffer_n,
                        (uint16_t)buffer_w);
    } else
    if (this->compute_able == 3)
    {
        //FP16
        this->matmul_fp16(    (fp16 *)this->z_buffer_compute,
                        (fp16 *)this->z_buffer_compute,
                        (fp16 *)this->x_buffer,
                        (fp16 *)this->w_buffer,
                        (uint16_t)buffer_h,
                        (uint16_t)buffer_n,
                        (uint16_t)buffer_w);
    }
    if (this->compute_able == 5)
    {
        //UINT8
        matmul_uint8(   (uint8_t *)this->z_buffer_compute,
                        (uint8_t *)this->z_buffer_compute,
                        (uint8_t *)this->x_buffer,
                        (uint8_t *)this->w_buffer,
                        (uint16_t)buffer_h,
                        (uint16_t)buffer_n,
                        (uint16_t)buffer_w);
    }
    if (this->compute_able == 6)
    {
        //UINT8
        matmul_int8(    (int8_t *)this->z_buffer_compute,
                        (int8_t *)this->z_buffer_compute,
                        (int8_t *)this->x_buffer,
                        (int8_t *)this->w_buffer,
                        (uint16_t)buffer_h,
                        (uint16_t)buffer_n,
                        (uint16_t)buffer_w);
    }
    if (this->compute_able == 7)
    {
        //UINT8
        matmul_fp8e4m3(     (fp8e4m3 *)this->z_buffer_compute,
                        (fp8e4m3 *)this->z_buffer_compute,
                        (fp8e4m3 *)this->x_buffer,
                        (fp8e4m3 *)this->w_buffer,
                        (uint16_t)buffer_h,
                        (uint16_t)buffer_n,
                        (uint16_t)buffer_w);
    }

}

void LightRedmule::process_iter_instruction(){
    // Used for FORWARD_YZ only; TCDM responses go through apply_response_data().
    this->process_iter_instruction(this->access_buffer, this->iter_instruction);
}

void LightRedmule::process_iter_instruction(uint8_t *buf, uint32_t instr){

    /*
                      buffer_w,
                      k_size,
                      iter_j
            buffer_n|------
            n_size  |  W  |
            iter_k  |     |
    buffer_h|--------------
    m_size  |  X    |  Y/Z|
    iter_i  |-------|------
    */

    uint32_t buffer_h_byte = this->ce_height * this->elem_size;
    uint32_t buffer_n_byte = this->bandwidth;
    uint32_t buffer_w_byte = this->ce_width * (this->ce_pipe + 1) * this->elem_size;

    uint32_t _k = this->iter_k + 1;
    if ((_k == this->x_row_tiles) || this->reg_fsm_state.get() == PRELOAD)
    {
        _k = 0;
    }
    uint32_t x_leftover_byte = this->n_size * this->elem_size - _k * buffer_n_byte;
    uint32_t x_cutoff = x_leftover_byte < buffer_n_byte ? x_leftover_byte : buffer_n_byte;

    uint32_t w_leftover_byte = this->k_size * this->elem_size - this->iter_j * buffer_w_byte;
    uint32_t w_cutoff = w_leftover_byte < buffer_w_byte ? w_leftover_byte : buffer_w_byte;

    uint32_t _j = this->iter_j + 1;
    if ((_j == this->z_row_tiles) || this->reg_fsm_state.get() == PRELOAD)
    {
        _j = 0;
    }
    uint32_t y_leftover_byte = this->k_size * this->elem_size - _j * buffer_w_byte;
    uint32_t y_cutoff = y_leftover_byte < buffer_w_byte ? y_leftover_byte : buffer_w_byte;

    uint32_t z_width_leftover_byte = this->k_size * this->elem_size - this->iter_j * buffer_w_byte;
    uint32_t z_width_cutoff = z_width_leftover_byte < buffer_w_byte ? z_width_leftover_byte : buffer_w_byte;
    uint32_t z_height_leftover_byte = this->m_size * this->elem_size - this->iter_i * buffer_h_byte;
    uint32_t z_height_cutoff = z_height_leftover_byte < buffer_h_byte ? z_height_leftover_byte : buffer_h_byte;

    uint32_t buffer_yz_byte = this->ce_height * this->ce_width * (this->ce_pipe + 1) * this->elem_size;
    switch(instr) {
        case INSTR_LOAD_Y:
            if (this->iter_y_row_ptr == 0) {
                std::memset(this->y_buffer_preload, 0, this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size);
            }
            std::memcpy(&(this->y_buffer_preload[this->iter_y_row_ptr]), buf, y_cutoff);
            if (this->y_acc_block == 0)
            {
                this->iter_y_row_ptr = 0;
            } else {
                this->iter_y_row_ptr += buffer_w_byte;
            }
            break;
        case INSTR_LOAD_W:
            if (this->iter_w_row_ptr == 0) {
                std::memset(this->w_buffer, 0, this->LOCAL_BUFFER_N * this->LOCAL_BUFFER_W * this->elem_size);
            }
            std::memcpy(&(this->w_buffer[this->iter_w_row_ptr]), buf, w_cutoff);
            this->iter_w_row_ptr += buffer_w_byte;
            break;
        case INSTR_LOAD_W_COMPUTE:
            if (this->iter_w_row_ptr == 0) {
                std::memset(this->w_buffer, 0, this->LOCAL_BUFFER_N * this->LOCAL_BUFFER_W * this->elem_size);
            }
            std::memcpy(&(this->w_buffer[this->iter_w_row_ptr]), buf, w_cutoff);
            this->process_compute();
            this->iter_w_row_ptr = 0;
            //Clear X and W buffer
            std::memset(this->x_buffer, 0, this->ce_height * this->bandwidth);
            std::memset(this->w_buffer, 0, this->ce_width * (this->ce_pipe + 1) * this->bandwidth);
            break;
        case INSTR_LOAD_X:
            if (this->iter_x_row_ptr == 0) {
                std::memset(this->x_buffer, 0, ce_height * this->bandwidth);
            }
            std::memcpy(&(this->x_buffer[this->iter_x_row_ptr]), buf, x_cutoff);
            if (this->x_acc_block == 0)
            {
                this->iter_x_row_ptr = 0;
            } else {
                this->iter_x_row_ptr += buffer_n_byte;
            }
            break;
        case INSTR_STOR_Z:
            std::memcpy(buf, &(this->z_buffer_previos[this->iter_z_row_ptr]), buffer_w_byte);
            if (this->z_acc_block == 0)
            {
                this->iter_z_row_ptr = 0;
            } else {
                this->iter_z_row_ptr += buffer_w_byte;
            }
            break;
        case INSTR_FORWARD_YZ:
            std::memcpy(this->z_buffer_previos, this->z_buffer_compute, buffer_yz_byte);
            if (this->is_gemm()) {
                std::memcpy(this->z_buffer_compute, this->y_buffer_preload, buffer_yz_byte);
            } else {
                // MATMUL: the z_buffer masks Y, the bias is zero
                std::memset(this->z_buffer_compute, 0, buffer_yz_byte);
            }
            std::memset(this->y_buffer_preload, 0, buffer_yz_byte);
            this->z_store_width = z_width_cutoff / this->elem_size;
            this->z_store_height = z_height_cutoff / this->elem_size;
            break;
        default:
            break;
    }
}

uint32_t LightRedmule::get_routine_access_block_number(){
    uint32_t total_blocks       = 0;
    uint32_t is_last_iteration  = (this->iter_i == (this->z_col_tiles - 1)) && (this->iter_j == (this->z_row_tiles - 1)) && (this->iter_k == (this->x_row_tiles - 1));
    uint32_t is_first_iteration = (this->iter_i == 0) && (this->iter_j == 0) && (this->iter_k == 0);
    uint32_t buffer_h           = this->ce_height;
    uint32_t buffer_w           = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n           = this->bandwidth / this->elem_size;
    uint32_t tcdms_bw           = buffer_n;

    this->x_acc_block = 0;
    this->w_acc_block = 0;
    this->w_pad_block = 0;
    this->y_pad_block = 0;
    this->z_pad_block = 0;
    this->y_acc_block = 0;
    this->z_acc_block = 0;
    

    // W block
    if (this->iter_k == (this->x_row_tiles - 1) && (this->x_row_lefts > 0))
    {
        this->w_acc_block = this->x_row_lefts;
    } else {
        this->w_acc_block = tcdms_bw;
    }
    total_blocks += this->w_acc_block;
    this->w_pad_block = this->get_w_rows(this->iter_k) - this->w_acc_block;
    total_blocks += this->w_pad_block;
    this->iter_w_addr = this->calculate_tile_base_address(this->w_addr, this->k_size, buffer_n, buffer_w, this->iter_k, this->iter_j);
    this->w_rows_later = 0;
    if (is_first_iteration)
    {
        // The preload already loaded the first rows of this tile
        this->w_pad_block -= this->w_prefetch_pad;
        this->w_acc_block -= this->w_prefetch_real;
        total_blocks -= this->w_prefetch_pad + this->w_prefetch_real;
        for (uint32_t i = 0; i < this->w_prefetch_real; i++)
        {
            this->iter_w_addr = this->inc_addr(this->iter_w_addr, this->k_size, buffer_w);
        }
        this->w_prefetch_pad  = 0;
        this->w_prefetch_real = 0;
    }

    // X block
    if (is_last_iteration == 0)
    {

        //update x tile base address
        uint32_t _k = this->iter_k;
        uint32_t _j = this->iter_j;
        uint32_t _i = this->iter_i;
        _k += 1;
        if (_k == this->x_row_tiles)
        {
            _k = 0;
            _j += 1;
            if (_j == this->z_row_tiles)
            {
                _j = 0;
                _i += 1;
            }
        }
        this->iter_x_addr = this->calculate_tile_base_address(this->x_addr, this->n_size, buffer_h, buffer_n, _i, _k);

        this->x_acc_block = (this->m_size - _i * this->ce_height) < this->ce_height ? (this->m_size - _i * this->ce_height) : this->ce_height;
        total_blocks += this->x_acc_block;
    }

    // Y block (none in MATMUL: the RTL never starts the Y stream)
    if (this->is_gemm() && this->iter_k == (this->x_row_tiles - 1) && (is_last_iteration == 0))
    {
        //update y tile base address
        uint32_t _i = this->iter_i;
        uint32_t _j = this->iter_j;
        _j += 1;
        if (_j == this->z_row_tiles)
        {
            _j = 0;
            _i += 1;
        }
        this->iter_y_addr = this->calculate_tile_base_address(this->y_addr, this->k_size, buffer_h, buffer_w, _i, _j);

        this->y_acc_block = (this->m_size - _i * this->ce_height) < this->ce_height ? (this->m_size - _i * this->ce_height) : this->ce_height;
        this->y_pad_block = this->get_y_z_pad_rows(this->y_acc_block);
        total_blocks += this->y_acc_block + this->y_pad_block;
    }

    // Z block
    if ((this->iter_k == 0) && (is_first_iteration == 0))
    {
        this->z_acc_block = this->z_store_height;
        this->z_pad_block = this->get_y_z_pad_rows(this->z_acc_block);
        total_blocks += this->z_acc_block + this->z_pad_block;

        //update z tile base address
        uint32_t _i = this->iter_i;
        uint32_t _j = this->iter_j;
        if (_j == 0)
        {
            _j = this->z_row_tiles - 1;
            _i = _i - 1;
        } else {
            _j = _j -1;
        }
        this->iter_z_addr = this->calculate_tile_base_address(this->z_addr, this->k_size, buffer_h, buffer_w, _i, _j);
    }

    return total_blocks;

}

// Preload accesses, one per cycle: the first X tile, the Y rows (GEMM only) and the
// first W_PREFETCH_ROWS rows of the first W tile. The rest of that W tile, and the
// second X tile the RTL also preloads, are left to the first iteration.
uint32_t LightRedmule::get_preload_access_block_number(){
    uint32_t total_blocks       = 0;
    uint32_t tcdms_bw           = this->bandwidth / this->elem_size;
    uint32_t buffer_n           = tcdms_bw;
    uint32_t buffer_w           = this->ce_width * (this->ce_pipe + 1);

    this->x_acc_block = 0;
    this->w_acc_block = 0;
    this->w_pad_block = 0;
    this->y_pad_block = 0;
    this->z_pad_block = 0;
    this->y_acc_block = 0;
    this->z_acc_block = 0;

    // X & Y block
    this->x_acc_block = this->m_size < this->ce_height ?  this->m_size : this->ce_height;
    if (this->is_gemm())
    {
        this->y_acc_block = this->m_size < this->ce_height ?  this->m_size : this->ce_height;
        this->y_pad_block = this->get_y_z_pad_rows(this->y_acc_block);
    }
    total_blocks = this->x_acc_block + this->y_acc_block + this->y_pad_block;

    // First rows of the first W tile, padding first as in the iterations
    uint32_t w_real = (this->x_row_tiles == 1 && this->x_row_lefts > 0) ? this->x_row_lefts : tcdms_bw;
    uint32_t w_pad = this->get_w_rows(0) - w_real;
    uint32_t w_rows = std::min(W_PREFETCH_ROWS, w_real + w_pad);
    this->w_pad_block = std::min(w_rows, w_pad);
    this->w_acc_block = w_rows - this->w_pad_block;
    this->w_prefetch_pad = this->w_pad_block;
    this->w_prefetch_real = this->w_acc_block;
    this->w_rows_later = w_real - this->w_acc_block;
    this->iter_w_addr = this->calculate_tile_base_address(this->w_addr, this->k_size, buffer_n, buffer_w, 0, 0);
    total_blocks += w_rows;

    return total_blocks;

}

uint32_t LightRedmule::get_storing_access_block_number(){
    uint32_t total_blocks       = 0;
    uint32_t buffer_h           = this->ce_height;
    uint32_t buffer_w           = this->ce_width * (this->ce_pipe + 1);
    uint32_t buffer_n           = this->bandwidth / this->elem_size;
    uint32_t tcdms_bw           = this->bandwidth / this->elem_size;

    this->x_acc_block = 0;
    this->w_acc_block = 0;
    this->w_pad_block = 0;
    this->y_pad_block = 0;
    this->z_pad_block = 0;
    this->y_acc_block = 0;
    this->z_acc_block = 0;

    // Z block
    this->z_acc_block = this->z_store_height;
    this->z_pad_block = this->get_y_z_pad_rows(this->z_acc_block);
    total_blocks = this->z_acc_block + this->z_pad_block;
    this->iter_z_addr = this->calculate_tile_base_address(this->z_addr, this->k_size, buffer_h, buffer_w, this->z_col_tiles - 1, this->z_row_tiles - 1);

    return total_blocks;

}

uint32_t LightRedmule::get_routine_to_storing_latency(){
    return this->ce_width * (this->ce_pipe + 1);
    // return 0;
}

uint32_t LightRedmule::get_redmule_array_runtime(){
    uint32_t tcdms_bw       = this->bandwidth / this->elem_size;
    uint32_t runtime_unit   = this->ce_width * (this->ce_pipe + 1);
    uint32_t runtime_pices  = 1;
    uint32_t runtime        = tcdms_bw * (this->ce_pipe + 1);
    if (this->iter_k == (this->x_row_tiles - 1) && (this->x_row_lefts > 0))
    {
        // The array goes through every W row the RTL loads, padding included
        runtime = this->get_w_rows(this->iter_k) * (this->ce_pipe + 1);
    }
    runtime_pices = (runtime + runtime_unit - 1)/runtime_unit;
    return runtime_pices * runtime_unit;
}

// Rows of W the RTL loads and the array processes for the N tile iter_k
// (redmule_tiler.sv): a job with N <= Height runs as if N were MinimumSizeN
// (2 * Height), and the W-row loop is rounded up to a multiple of Height. The
// rows past N are gated to zero, so they cost time but do not change the
// result. Height is ce_width here (the K tile is ce_width * (ce_pipe + 1)).
uint32_t LightRedmule::get_w_rows(uint32_t iter_k)
{
    uint32_t height  = this->ce_width;
    uint32_t chunk   = this->bandwidth / this->elem_size;
    uint32_t n_eff   = (this->n_size <= height) ? 2 * height : this->n_size;
    uint32_t total   = (n_eff + height - 1) / height * height;
    uint32_t start   = iter_k * chunk;
    if (total <= start) return 0;
    return (total - start) < chunk ? (total - start) : chunk;
}

// Rows of padding added to a Y load or Z store of `rows` real rows: the RTL
// always streams Width (ce_height) rows per M tile (redmule_tiler.sv:
// yz_tot_len = Width * x_rows_iter * w_cols_iter; only the X stream is cut to
// x_rows_lftovr, in redmule_memory_scheduler.sv).
uint32_t LightRedmule::get_y_z_pad_rows(uint32_t rows)
{
    return rows < this->ce_height ? this->ce_height - rows : 0;
}

uint32_t LightRedmule::op_foramt_parser(uint32_t op_format) {
    uint32_t data_format=op_format&0x7;
    uint32_t operation=(op_format>>3)&0x7;
    uint32_t compute_able=0;
    //only GeMM is supported for now
    //expected compute_able=1 --> matmul_uint16
    //expected compute_able=2 --> matmul_int16
    //expected compute_able=3 --> matmul_fp16
    //expected compute_able=5 --> matmul_uint8
    //expected compute_able=6 --> matmul_int8
    //expected compute_able=7 --> matmul_fp8e4m3
    if ((operation==1) && (data_format==1))
        compute_able=3;
    else if ((operation==1) && (data_format==0))
        compute_able=7;
    else 
        this->trace.fatal("[LightRedmule] Selected wrong operation/format combination [op_format=0x%x-data_format=%d-operation=%d]\n",op_format,data_format,operation);
    return compute_able;
}

void LightRedmule::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    LightRedmule *_this = (LightRedmule *)__this;
    uint32_t opc = insn->opcode & 0x7F;

    switch (opc)
    {
        case 0b0001011:
        {
            if (_this->reg_fsm_state.get() == IDLE) {
                insn->granted = true; //here we always grant the core as it is just a configuration
                _this->m_size = insn->arg_a & 0xFFFF;
                _this->n_size = insn->arg_b;
                _this->k_size = (insn->arg_a) >> 16;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set MNK size: %d, %d, %d\n", _this->m_size, _this->n_size, _this->k_size);
            }
            break;
        }
        case 0b0101011:
        {
            if (_this->reg_fsm_state.get() == IDLE)
            {
                insn->granted = true; //as soon as the operations is triggered we deassert the core
                _this->x_addr = insn->arg_a;
                _this->w_addr = insn->arg_b;
                _this->y_addr = insn->arg_c;
                _this->z_addr = _this->y_addr;
                _this->compute_able = _this->op_foramt_parser(insn->arg_d);
                _this->elem_size = (_this->compute_able < 4)? 2:1;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set XWY addr: 0x%08x, 0x%08x, 0x%08x\n", _this->x_addr, _this->w_addr, _this->y_addr);

                /*************************
                *  Asynchronize Trigger  *
                *************************/
                //Sanity Check
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] redmule configuration (M-N-K): %d, %d, %d\n", _this->m_size, _this->n_size, _this->k_size);
                if ((_this->m_size == 0)||(_this->n_size == 0)||(_this->k_size == 0))
                {
                    _this->trace.fatal("[LightRedmule] INVALID redmule configuration (M-N-K): %d, %d, %d\n", _this->m_size, _this->n_size, _this->k_size);
                }

                //Initilaize redmule meta data
                _this->init_redmule_meta_data();

                //Trigger FSM
                _this->reg_fsm_state.set(PRELOAD);
                _this->reg_busy.set(1);
                _this->tcdm_block_total = _this->get_preload_access_block_number();
                _this->fsm_counter      = 0;
                _this->fsm_timestamp    = 0;
                _this->timer_start      = _this->time.get_time();
                _this->cycle_start      = _this->clock.get_cycles();
                _this->event_enqueue(_this->fsm_event, JOB_START_LATENCY);
            }
            break;
        }
    }
}

/************************************************************
*  HWPE control-register protocol (ACQUIRE / COMMIT_TRIGGER  *
*  / STATUS / RUNNING_JOB / SOFT_CLEAR), depth-2 job queue    *
************************************************************/

//ACQUIRE (0x04, read): assign a new job id and lock the controller
int32_t LightRedmule::acquire()
{
    if (this->job_state != 0) {
        //Already acquired, not yet committed: re-return the pending id
        //(checked first to match hwpe_ctrl_target.sv ACQUIRE-state priority over queue-full)
        return this->job_state;
    } else if (this->job_pending == 2) {
        //Queue full
        return -1;
    } else {
        //Free to acquire and room in the queue: hand out a new id
        int32_t id = (int32_t) this->job_id_counter++;
        this->cxt_job_id[this->cxt_cfg_ptr] = id;
        this->job_state = -2;
        return id;
    }
}

//COMMIT_TRIGGER write (ct == 0 commit+start, ct == 1 commit-only)
void LightRedmule::commit(bool start)
{
    if (this->job_state == 0) {
        //For callers that (improerly) write TRIGGER directly without ever
        //reading ACQUIRE, the real HW behavior is to not advance the job
        //id counter, but only the running job counter (which counts retired
        //jobs). We mirror that here.
        if (this->job_pending < 2) {
            this->cxt_job_id[this->cxt_cfg_ptr] = (int32_t) this->job_id_counter;
            this->job_state = -2;
        }
    }
    if (this->job_state != -2) {
        //Queue full: drop
        this->trace.msg(vp::Trace::LEVEL_WARNING,"[LightRedmule] COMMIT_TRIGGER dropped: job queue full (job_pending=%d)\n", this->job_pending);
        return;
    }

    //Commit: unlock, queue the job, flip the config pointer
    this->job_pending++;
    this->job_state = 0;
    this->cxt_cfg_ptr = 1 - this->cxt_cfg_ptr;

    if (start && (this->reg_fsm_state.get() == IDLE)) {
        this->start_next_job();
    }
}

//Load the queued job in cxt_use_ptr and kick off the FSM (IDLE -> PRELOAD)
void LightRedmule::start_next_job()
{
    //Load context
    this->m_size         = this->cxt_m_size[this->cxt_use_ptr];
    this->n_size         = this->cxt_n_size[this->cxt_use_ptr];
    this->k_size         = this->cxt_k_size[this->cxt_use_ptr];
    this->x_addr         = this->cxt_x_addr[this->cxt_use_ptr];
    this->w_addr         = this->cxt_w_addr[this->cxt_use_ptr];
    this->z_addr         = this->cxt_z_addr[this->cxt_use_ptr];
    this->y_offs         = this->cxt_y_offs[this->cxt_use_ptr];
    this->y_addr         = this->z_addr + this->y_offs;
    this->gemm_ops       = this->cxt_gemm_ops[this->cxt_use_ptr];
    this->compute_able   = this->cxt_compute_able[this->cxt_use_ptr];
    this->elem_size      = (this->compute_able < 4) ? 2 : 1;
    this->running_job_id = this->cxt_job_id[this->cxt_use_ptr];

    //Sanity Check
    this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] redmule configuration (M-N-K): %d, %d, %d. Compute able is %d. %s, Y addr 0x%x, Z addr 0x%x\n", this->m_size, this->n_size, this->k_size, this->compute_able, this->is_gemm() ? "GEMM" : "MATMUL", this->y_addr, this->z_addr);
    if ((this->m_size == 0)||(this->n_size == 0)||(this->k_size == 0))
    {
        this->trace.fatal("[LightRedmule] INVALID redmule configuration (M-N-K): %d, %d, %d\n", this->m_size, this->n_size, this->k_size);
        return;
    }

    //Initilaize redmule meta data
    this->init_redmule_meta_data();

    //Trigger FSM
    this->reg_fsm_state.set(PRELOAD);
    this->reg_busy.set(1);
    this->tcdm_block_total = this->get_preload_access_block_number();
    this->fsm_counter      = 0;
    this->fsm_timestamp    = 0;
    this->timer_start      = this->time.get_time();
    this->cycle_start      = this->clock.get_cycles();
    this->event_enqueue(this->fsm_event, JOB_START_LATENCY);
}

//SOFT_CLEAR write: 0 full clear, 1 clear engine state only, 2 clear regfile+queue only
void LightRedmule::soft_clear(uint32_t value)
{
    if ((value == 0) || (value == 2)) {
        //Clear job-offload state machine, job queue, ID counters, and buffered
        //job-dependent config (regfile)
        this->job_state      = 0;
        this->job_pending     = 0;
        this->cxt_job_id[0]   = -1;
        this->cxt_job_id[1]   = -1;
        this->running_job_id  = -1;
        this->completed_jobs  = 0;
        this->cxt_cfg_ptr     = 0;
        this->cxt_use_ptr     = 0;
        this->job_id_counter  = 0;
        for (int i = 0; i < 2; i++) {
            this->cxt_m_size[i]       = 0;
            this->cxt_n_size[i]       = 0;
            this->cxt_k_size[i]       = 0;
            this->cxt_x_addr[i]       = 0;
            this->cxt_w_addr[i]       = 0;
            this->cxt_z_addr[i]       = 0;
            this->cxt_y_offs[i]       = 0;
            this->cxt_gemm_ops[i]     = GEMM_OP_GEMM;
            this->cxt_compute_able[i] = 0;
        }
    }
    if ((value == 0) || (value == 1)) {
        //Clear engine state (compute FSM)
        this->reg_fsm_state.set(IDLE);
        this->reg_busy.set(0);
        this->done.sync(false);
    }
}

//LightRedmule register interface
vp::IoReqStatus LightRedmule::req(vp::Block *__this, vp::IoReq *req)
{
    LightRedmule *_this = (LightRedmule *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    //_this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] access (offset: 0x%x, size: 0x%x, is_write: %d, data:%x)\n", offset, size, is_write, *(uint32_t *)data);

    if (is_write == 1) {
        uint32_t value = *(uint32_t *)data;
        switch (offset) {
            case 0x00: {
                if ((_this->redmule_query == NULL) && (_this->reg_fsm_state.get() == IDLE)) {
                    /************************
                    *  Synchronize Trigger  *
                    ************************/
                    //Sanity Check
                    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] redmule configuration (M-N-K): %d, %d, %d. Compute able is %d\n", _this->m_size, _this->n_size, _this->k_size,_this->compute_able);
                    if ((_this->m_size == 0)||(_this->n_size == 0)||(_this->k_size == 0))
                    {
                        _this->trace.fatal("[LightRedmule] INVALID redmule configuration (M-N-K): %d, %d, %d\n", _this->m_size, _this->n_size, _this->k_size);
                        return vp::IO_REQ_DONE;
                    }

                    //Initialize redmule meta data
                    _this->init_redmule_meta_data();

                    

                    //Trigger FSM
                    _this->reg_fsm_state.set(PRELOAD);
                    _this->reg_busy.set(1);
                    _this->tcdm_block_total = _this->get_preload_access_block_number();
                    _this->fsm_counter      = 0;
                    _this->fsm_timestamp    = 0;
                    _this->timer_start      = _this->time.get_time();
                    _this->cycle_start      = _this->clock.get_cycles();
                    _this->event_enqueue(_this->fsm_event, JOB_START_LATENCY);

                    //Save Query
                    //_this->redmule_query = req;
                    //_this->redmule_query_port = &_this->input_itf;
                }
                break;
            }
            case 0x40: {
                _this->x_addr = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set X addr 0x%x)\n", _this->x_addr);
                break;
            }
            case 0x44: {
                _this->w_addr = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set W addr 0x%x)\n", _this->w_addr);
                break;
            }
            case 0x48: {
                _this->y_addr = value;
                _this->z_addr = _this->y_addr;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set Y addr 0x%x, Set Z addr 0x%x)\n", _this->y_addr,_this->z_addr);
                break;
            }
            case 0x4C: { //REDMULE_MCFG0_PTR
                _this->m_size=value & 0xFFFF;
                _this->k_size=value >> 16;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set M size %d, Set K size %d)\n", _this->m_size,_this->k_size);
                break;
            }
            case 0x50: { //REDMULE_MCFG1_PTR
                _this->n_size=value & 0xFFFF;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set N size %d)\n", _this->n_size);
                break;
            }
            case 0x54: {
                _this->compute_able = _this->op_foramt_parser((value == 0x480)? 9:0); //the marith mapping between reg-if and offload-if is different... WHY?
                _this->elem_size = (_this->compute_able < 4)? 2:1;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] marith 0x%x, compute_able 0x%x, elem_size %d)\n", value,_this->compute_able,_this->elem_size);
                break;
            }
            default:
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] write to INVALID address\n");
        }
    }
    else {
        switch (offset) {
            case 0x00:
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] read to INVALID address\n");
                break;
            case 0x0C: {
                int32_t done_id_r;
                if ((_this->redmule_query == NULL) && (_this->reg_fsm_state.get() == IDLE)) {
                    done_id_r =  0x00;
                    memcpy((void *)data, (void *)&done_id_r, size);
                }
                else {
                    done_id_r =  0x01;
                    memcpy((void *)data, (void *)&done_id_r, size);
                }
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] read status reg 0x%x\n",done_id_r);
                break;
            }
            default:
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] read to INVALID address\n");
        }
    }
    return vp::IO_REQ_DONE;
}

vp::IoReqStatus LightRedmule::req_v2(vp::Block *__this, vp::IoReq *req)
{
    LightRedmule *_this = (LightRedmule *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (is_write == 1) {
        uint32_t value = *(uint32_t *)data;
        switch (offset) {
            case 0x00: { //REDMULE_TRIGGER / hwpe_commit_trigger
                uint32_t ct = value & 0x3;
                if (ct == 2) {
                    //Trigger-only: start whatever is already queued, no new commit
                    if ((_this->job_pending > 0) && (_this->reg_fsm_state.get() == IDLE)) {
                        _this->start_next_job();
                    }
                } else if (ct == 3) {
                    //Neither bit is 0: no-op
                } else {
                    //ct == 0: commit + start; ct == 1: commit only (deferred start)
                    _this->commit(ct == 0);
                }
                break;
            }
            case 0x20: { //REDMULE_MCFG0_PTR
                _this->cxt_m_size[_this->cxt_cfg_ptr] = value & 0xFFFF;
                _this->cxt_k_size[_this->cxt_cfg_ptr] = value >> 16;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set mcfg_reg0 -- M size %d, Set K size %d)\n", _this->cxt_m_size[_this->cxt_cfg_ptr],_this->cxt_k_size[_this->cxt_cfg_ptr]);
                break;
            }
            case 0x24: { //REDMULE_MCFG1_PTR
                _this->cxt_n_size[_this->cxt_cfg_ptr] = value & 0xFFFF;
                //gemm_ops (redmule_regif.rdl): only MATMUL and GEMM are modeled
                uint32_t gemm_ops = (value >> 20) & 0x7;
                if ((gemm_ops != GEMM_OP_MATMUL) && (gemm_ops != GEMM_OP_GEMM))
                {
                    _this->trace.fatal("[LightRedmule] Unsupported gemm_ops %d (only MATMUL=0 and GEMM=1)\n", gemm_ops);
                }
                _this->cxt_gemm_ops[_this->cxt_cfg_ptr] = gemm_ops;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set mcfg_reg1 -- N size %d, gemm_ops %d)\n", _this->cxt_n_size[_this->cxt_cfg_ptr], gemm_ops);
                break;
            }
            case 0x28: { //REDMULE_MCFG2_PTR
                _this->cxt_y_offs[_this->cxt_cfg_ptr] = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set mcfg_reg2 -- Y offset 0x%x)\n", _this->cxt_y_offs[_this->cxt_cfg_ptr]);
                break;
            }
            case 0x2C: {
                _this->cxt_x_addr[_this->cxt_cfg_ptr] = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set X addr 0x%x)\n", _this->cxt_x_addr[_this->cxt_cfg_ptr]);
                break;
            }
            case 0x30: {
                _this->cxt_w_addr[_this->cxt_cfg_ptr] = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set W addr 0x%x)\n", _this->cxt_w_addr[_this->cxt_cfg_ptr]);
                break;
            }
            case 0x34: {
                _this->cxt_z_addr[_this->cxt_cfg_ptr] = value;
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] Set Z addr 0x%x)\n", _this->cxt_z_addr[_this->cxt_cfg_ptr]);
                break;
            }
            case 0x14: { //REDMULE_SOFT_CLEAR
                _this->soft_clear(value);
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] SOFT_CLEAR 0x%x\n", value);
                break;
            }
            case 0x54: {
                _this->cxt_compute_able[_this->cxt_cfg_ptr] = _this->op_foramt_parser((value == 0x480)? 9:0); //the marith mapping between reg-if and offload-if is different... WHY?
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] marith 0x%x, compute_able 0x%x)\n", value,_this->cxt_compute_able[_this->cxt_cfg_ptr]);
                break;
            }
            default:
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] write to INVALID address\n");
        }
    }
    else {
        switch (offset) {
            case 0x04: { //REDMULE_ACQUIRE
                int32_t id = _this->acquire();
                memcpy((void *)data, (void *)&id, size);
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] ACQUIRE -> job id %d\n", id);
                break;
            }
            case 0x0C: { //REDMULE_STATUS - per-slot busy bitfield (bit0 slot0, bit8 slot1)
                uint32_t status = (_this->cxt_job_id[0] >= 0 ? 0x1 : 0) | (_this->cxt_job_id[1] >= 0 ? 0x100 : 0);
                memcpy((void *)data, (void *)&status, size);
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] read status reg 0x%x\n",status);
                break;
            }
            case 0x10: { //REDMULE_RUNNING_JOB
                // Number of jobs completed so far, as job_running_id_q in
                // hwpe_ctrl_target.sv (incremented on each job_done, cleared
                // by the soft clear)
                uint32_t running_job = (uint32_t) _this->completed_jobs;
                memcpy((void *)data, (void *)&running_job, size);
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] RUNNING_JOB -> %d\n", running_job);
                break;
            }
            default:
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule] read to INVALID address\n");
        }
    }
    return vp::IO_REQ_DONE;
}

// Reserve the free slot (available_at <= now) that freed earliest, or return -1.
// It stays reserved (INT64_MAX) until free_req_slot() sets its completion cycle.
int LightRedmule::alloc_req_slot()
{
    int64_t now = this->clock.get_cycles();
    int     best_id = -1;
    int64_t best_cycle = INT64_MAX;
    for (size_t i = 0; i < this->req_slots.size(); i++)
    {
        int64_t at = this->req_slots[i].available_at;
        if (at <= now && at < best_cycle)
        {
            best_id    = (int)i;
            best_cycle = at;
        }
    }
    if (best_id >= 0)
    {
        this->req_slots[best_id].available_at = INT64_MAX;
    }
    return best_id;
}

void LightRedmule::free_req_slot(int id, int64_t available_at)
{
    this->req_slots[id].available_at = available_at;
}

bool LightRedmule::all_slots_free()
{
    int64_t now = this->clock.get_cycles();
    for (auto &slot : this->req_slots)
    {
        if (slot.available_at > now) return false;
    }
    return true;
}

// At most queue_depth + 1 requests in flight. A slot freeing at now still counts
// here (>=), unlike in alloc_req_slot() (<=), so the alloc that follows cannot fail.
bool LightRedmule::issue_slot_gate_ok()
{
    int64_t now = this->clock.get_cycles();
    uint32_t outstanding = 0;
    for (auto &slot : this->req_slots)
    {
        if (slot.available_at >= now) outstanding++;
    }
    return outstanding <= this->queue_depth;
}

// Copy response data into the local buffers, using only what was captured at issue.
void LightRedmule::apply_response_data(uint8_t *buf, uint32_t instr, uint32_t dst_offset, uint32_t cutoff)
{
    switch (instr)
    {
        case INSTR_LOAD_Y:
            std::memcpy(&(this->y_buffer_preload[dst_offset]), buf, cutoff);
            break;
        case INSTR_LOAD_W:
        case INSTR_LOAD_W_COMPUTE:
            std::memcpy(&(this->w_buffer[dst_offset]), buf, cutoff);
            break;
        case INSTR_LOAD_X:
            // Double-buffer: write to x_buffer_next (compute reads x_buffer).
            std::memcpy(&(this->x_buffer_next[dst_offset]), buf, cutoff);
            break;
        case INSTR_STOR_Z:
            // Data was serialized into slot.buf at issue time.
            break;
        default:
            break;
    }
}

void LightRedmule::run_compute_and_drain()
{
    this->process_compute();
    std::memset(this->w_buffer, 0, this->ce_width * (this->ce_pipe + 1) * this->bandwidth);
    this->compute_pending = false;
}

void LightRedmule::handle_response(int slot_id, int64_t free_cycle)
{
    ReqSlot &slot = this->req_slots[slot_id];

    if (this->compute_able != 0)
    {
        this->apply_response_data(slot.buf, slot.instr, slot.dst_offset, slot.cutoff);
    }

    if (slot.instr == INSTR_LOAD_W || slot.instr == INSTR_LOAD_W_COMPUTE)
    {
        if (this->w_outstanding_count > 0) this->w_outstanding_count -= 1;
        if (this->compute_pending && this->w_outstanding_count == 0)
        {
            this->run_compute_and_drain();
        }
    }

    this->free_req_slot(slot_id, free_cycle);
}

vp::IoRespAck LightRedmule::tcdm_response(vp::Block *__this, vp::IoReq *req)
{
    LightRedmule *_this = (LightRedmule *)__this;
    int slot_id = (int)(intptr_t)req->initiator;
    uint32_t instr = _this->req_slots[slot_id].instr;

    _this->handle_response(slot_id, _this->clock.get_cycles());

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule][Resp] slot=%d instr=%d (w_out=%d, compute_pending=%d)\n",
        slot_id, instr, _this->w_outstanding_count, (int)_this->compute_pending);

    // This consumer never back-pressures the response channel.
    return vp::IO_RESP_ACCEPTED;
}

// Re-send the denied request from inside the callback: io_v2 retries must be
// serviced synchronously, the interconnect re-arbitrates only during this call.
void LightRedmule::tcdm_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    LightRedmule *_this = (LightRedmule *)__this;

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightRedmule][Retry] received, re-issuing\n");

    if (!_this->request_denied || _this->denied_slot < 0)
    {
        // Nothing held: the retry is for someone else (the interconnect does not
        // track who it denied), just resume issuing.
        _this->request_denied = false;
        return;
    }

    int slot_id = _this->denied_slot;
    ReqSlot &slot = _this->req_slots[slot_id];

    // prepare() resets the per-send fields and keeps initiator (the slot id).
    slot.req->prepare();
    vp::IoReqStatus err = _this->tcdm_itf.req(slot.req);

    if (err == vp::IO_REQ_DENIED)
    {
        // Still refused: keep holding it and wait for the next retry.
        return;
    }

    _this->request_denied = false;
    _this->denied_slot    = -1;

    if (err == vp::IO_REQ_DONE)
    {
        int64_t free_cycle = _this->clock.get_cycles() +
            (int64_t)slot.req->get_full_latency();
        _this->handle_response(slot_id, free_cycle);
    }
    else if (err != vp::IO_REQ_GRANTED)
    {
        _this->trace.fatal("[LightRedmule] Unexpected IoReq status %d on retry\n", (int)err);
    }
    // GRANTED: the slot stays reserved, tcdm_response will free it.
}

// Issue one request, capturing dst_offset and cutoff now since the FSM moves on
// before the response. Returns false if no slot is free.
bool LightRedmule::issue_request(uint32_t addr, uint32_t instr, bool is_write, uint32_t size)
{
    int slot_id = this->alloc_req_slot();
    if (slot_id < 0)
    {
        return false;
    }

    ReqSlot &slot = this->req_slots[slot_id];
    slot.instr = instr;

    uint32_t buffer_n_byte = this->bandwidth;
    uint32_t buffer_w_byte = this->ce_width * (this->ce_pipe + 1) * this->elem_size;

    // Cutoff (valid bytes); mirrors process_iter_instruction.
    uint32_t cutoff = 0;
    if (instr == INSTR_LOAD_X)
    {
        uint32_t _k = this->iter_k + 1;
        if ((_k == this->x_row_tiles) || this->reg_fsm_state.get() == PRELOAD) _k = 0;
        uint32_t x_leftover_byte = this->n_size * this->elem_size - _k * buffer_n_byte;
        cutoff = (x_leftover_byte < buffer_n_byte) ? x_leftover_byte : buffer_n_byte;
    }
    else if (instr == INSTR_LOAD_W || instr == INSTR_LOAD_W_COMPUTE)
    {
        uint32_t w_leftover_byte = this->k_size * this->elem_size - this->iter_j * buffer_w_byte;
        cutoff = (w_leftover_byte < buffer_w_byte) ? w_leftover_byte : buffer_w_byte;
    }
    else if (instr == INSTR_LOAD_Y)
    {
        uint32_t _j = this->iter_j + 1;
        if ((_j == this->z_row_tiles) || this->reg_fsm_state.get() == PRELOAD) _j = 0;
        uint32_t y_leftover_byte = this->k_size * this->elem_size - _j * buffer_w_byte;
        cutoff = (y_leftover_byte < buffer_w_byte) ? y_leftover_byte : buffer_w_byte;
    }
    else if (instr == INSTR_STOR_Z)
    {
        cutoff = buffer_w_byte;
    }
    slot.cutoff = cutoff;

    // Capture the row offset and advance the row pointer, as process_iter_instruction() does.
    if (instr == INSTR_LOAD_Y)
    {
        slot.dst_offset = this->iter_y_row_ptr;
        if (slot.dst_offset == 0)
        {
            // Safe to clear now: FORWARD_YZ reads it only once all slots are free.
            std::memset(this->y_buffer_preload, 0,
                this->LOCAL_BUFFER_H * this->LOCAL_BUFFER_W * this->elem_size);
        }
        if (this->y_acc_block == 0) this->iter_y_row_ptr = 0;
        else                        this->iter_y_row_ptr += buffer_w_byte;
    }
    else if (instr == INSTR_LOAD_W || instr == INSTR_LOAD_W_COMPUTE)
    {
        slot.dst_offset = this->iter_w_row_ptr;
        if (slot.dst_offset == 0)
        {
            // Safe to clear now: compute runs only once all W responses are in.
            std::memset(this->w_buffer, 0,
                this->LOCAL_BUFFER_N * this->LOCAL_BUFFER_W * this->elem_size);
        }
        this->iter_w_row_ptr += buffer_w_byte;
        this->w_outstanding_count += 1;
        if (instr == INSTR_LOAD_W_COMPUTE)
        {
            this->compute_pending  = true;
            this->iter_w_row_ptr   = 0;
        }
    }
    else if (instr == INSTR_LOAD_X)
    {
        // LOAD_X writes to x_buffer_next; iter_done's swap+memset keeps it clean.
        slot.dst_offset = this->iter_x_row_ptr;
        if (this->x_acc_block == 0) this->iter_x_row_ptr = 0;
        else                        this->iter_x_row_ptr += buffer_n_byte;
    }
    else if (instr == INSTR_STOR_Z)
    {
        // Snapshot Z row into slot.buf now — z_buffer_previos may be
        // overwritten by FORWARD_YZ before this write finishes.
        slot.dst_offset = this->iter_z_row_ptr; // src_offset for stores
        if (this->compute_able != 0)
        {
            std::memcpy(slot.buf, &(this->z_buffer_previos[slot.dst_offset]), buffer_w_byte);
        }
        if (this->z_acc_block == 0) this->iter_z_row_ptr = 0;
        else                        this->iter_z_row_ptr += buffer_w_byte;
    }
    else if (instr == INSTR_STOR_Z_PAD)
    {
        // Padding row: the RTL stores zeroes
        std::memset(slot.buf, 0, size);
    }

    // prepare() resets the per-send fields and keeps initiator (the slot id).
    slot.req->prepare();
    slot.req->set_addr(addr);
    slot.req->set_size(size);
    slot.req->set_is_write(is_write);
    slot.req->set_data(slot.buf);

    vp::IoReqStatus err = this->tcdm_itf.req(slot.req);

    if (err == vp::IO_REQ_DONE)
    {
        // Data is back; the slot stays busy for the full latency.
        int64_t free_cycle = this->clock.get_cycles() + (int64_t)slot.req->get_full_latency();
        this->handle_response(slot_id, free_cycle);
    }
    else if (err == vp::IO_REQ_GRANTED)
    {
        // Slot stays reserved (INT64_MAX); tcdm_response will free it.
    }
    else if (err == vp::IO_REQ_DENIED)
    {
        // Hold the request in its slot and stop issuing; tcdm_retry() re-sends it.
        this->request_denied = true;
        this->denied_slot    = slot_id;
    }
    else
    {
        this->trace.fatal("[LightRedmule] Unexpected IoReq status %d\n", (int)err);
        return false;
    }

    return true;
}

void LightRedmule::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    LightRedmule *_this = (LightRedmule *)__this;

    _this->fsm_timestamp += 1;

    switch (_this->reg_fsm_state.get()) {
        case IDLE:
            _this->done.sync(false); //clear irq
            //Auto-continue: start the next queued job, if any (depth-2 pipelining).
            //Happens after done is deasserted.
            if (_this->job_pending > 0) {
                _this->start_next_job();
            }
            break;

        case PRELOAD: {
            // Gate issue on slot capacity before next_addr() advances state.
            bool can_issue = (_this->fsm_counter < _this->tcdm_block_total)
                && !_this->request_denied
                && _this->issue_slot_gate_ok();

            if (can_issue)
            {
                uint32_t temp_addr = _this->next_addr() - _this->loc_base;
                bool is_w = _this->tcdm_req->get_is_write();
                uint32_t sz = (uint32_t)_this->tcdm_req->get_size();
                if (_this->issue_request(temp_addr, _this->iter_instruction, is_w, sz))
                {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Preload] Send TCDM req #%d [addr=0x%08x]\n",
                        _this->fsm_counter, temp_addr);
                    if (_this->fsm_counter == 0)
                    {
                        _this->preload_first_issue = _this->clock.get_cycles();
                    }
                    _this->fsm_counter += 1;
                }
            }

            bool iter_done = (_this->fsm_counter >= _this->tcdm_block_total)
                          && _this->all_slots_free();
            int64_t latency = 1;
            if (iter_done)
            {
                // The computation starts once Y is through the z_buffer. In
                // MATMUL there is no Y and it starts once X is loaded
                if (_this->is_gemm())
                {
                    int64_t start = _this->preload_first_issue
                        + Y_ROW_CYCLES * (_this->ce_height - 1) + 1;
                    int64_t now = _this->clock.get_cycles();
                    if (start > now + 1) latency = start - now;
                }

                _this->tcdm_block_total = _this->get_routine_access_block_number();
                _this->fsm_counter      = 0;
                _this->fsm_timestamp    = 0;
                _this->reg_fsm_state.set(ROUTINE);
                _this->reg_busy.set(1);

                // Promote PRELOAD's X(k=0) into x_buffer for ROUTINE's compute.
                std::swap(_this->x_buffer, _this->x_buffer_next);
                std::memset(_this->x_buffer_next, 0, _this->ce_height * _this->bandwidth);

                if (_this->compute_able != 0)
                {
                    _this->iter_instruction = INSTR_FORWARD_YZ;
                    _this->process_iter_instruction();
                }
            } else {
                _this->reg_fsm_state.set(PRELOAD);
                _this->reg_busy.set(1);
            }

            _this->event_enqueue(_this->fsm_event, latency);
            break;
        }
        case ROUTINE: {
            auto issue_one = [_this]() {
                bool can_issue = (_this->fsm_counter < _this->tcdm_block_total)
                    && !_this->request_denied
                    && _this->issue_slot_gate_ok();

                if (can_issue)
                {
                    uint32_t temp_addr = _this->next_addr() - _this->loc_base;
                    bool is_w = _this->tcdm_req->get_is_write();
                    uint32_t sz = (uint32_t)_this->tcdm_req->get_size();
                    if (_this->issue_request(temp_addr, _this->iter_instruction, is_w, sz))
                    {
                        _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][ROUTINE-ijk: %0d-%0d-%0d] Send TCDM req #%d [addr=0x%08x]\n",
                            _this->iter_i, _this->iter_j, _this->iter_k, _this->fsm_counter, temp_addr);
                        _this->fsm_counter += 1;
                    }
                }
            };

            issue_one();

            bool iter_done = (_this->fsm_counter >= _this->tcdm_block_total)
                          && _this->all_slots_free();
            if (iter_done)
            {
                int modeled_runtime = _this->get_redmule_array_runtime();
                int64_t latency = 1;

                // Wait for the modeled array runtime if the accesses finished earlier
                if (_this->fsm_timestamp >= modeled_runtime)
                {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][ROUTINE-ijk: %0d-%0d-%0d] TCDM Access Time(%d) > Model Time(%d)\n", _this->iter_i, _this->iter_j, _this->iter_k, _this->fsm_timestamp, modeled_runtime);
                    latency = 1;
                } else {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][ROUTINE-ijk: %0d-%0d-%0d] Model Time(%d) > TCDM Access Time(%d)\n",  _this->iter_i, _this->iter_j, _this->iter_k, modeled_runtime, _this->fsm_timestamp);
                    latency = modeled_runtime - _this->fsm_timestamp + 1;
                }

                // Next iteration
                _this->fsm_counter      = 0;
                _this->fsm_timestamp    = 0;

                // Promote next-iter's X into x_buffer (race-free because
                // all_slots_free guarantees every X response has landed).
                std::swap(_this->x_buffer, _this->x_buffer_next);
                std::memset(_this->x_buffer_next, 0, _this->ce_height * _this->bandwidth);

                // Need Forward YZ
                if ((_this->compute_able != 0) && (_this->iter_k + 1 == _this->x_row_tiles))
                {
                    _this->iter_instruction = INSTR_FORWARD_YZ;
                    _this->process_iter_instruction();
                }

                if (_this->next_iteration() == 0)
                {
                    _this->tcdm_block_total = _this->get_routine_access_block_number();
                    _this->reg_fsm_state.set(ROUTINE);
                    _this->reg_busy.set(1);
                    if (latency == 1)
                    {
                        // Bound by the accesses: the RTL streams go on one
                        // access per cycle across the iterations, so the next
                        // one issues in the cycle the last response landed
                        issue_one();
                    }
                } else {
                    _this->tcdm_block_total = _this->get_storing_access_block_number();
                    _this->reg_fsm_state.set(STORING);
                    _this->reg_busy.set(1);
                    latency += _this->get_routine_to_storing_latency();
                }

                _this->event_enqueue(_this->fsm_event, latency);
            } else {
                _this->reg_fsm_state.set(ROUTINE);
                _this->reg_busy.set(1);
                _this->event_enqueue(_this->fsm_event, 1);
            }

            break;
        }
        case STORING: {
            bool can_issue = (_this->fsm_counter < _this->tcdm_block_total)
                && !_this->request_denied
                && _this->issue_slot_gate_ok();

            if (can_issue)
            {
                uint32_t temp_addr = _this->next_addr() - _this->loc_base;
                bool is_w = _this->tcdm_req->get_is_write();
                uint32_t sz = (uint32_t)_this->tcdm_req->get_size();
                if (_this->issue_request(temp_addr, _this->iter_instruction, is_w, sz))
                {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][Storing] Send TCDM req #%d [addr=0x%08x]\n",
                        _this->fsm_counter, temp_addr);
                    _this->fsm_counter += 1;
                }
            }

            bool iter_done = (_this->fsm_counter >= _this->tcdm_block_total)
                          && _this->all_slots_free();
            if (iter_done)
            {
                _this->tcdm_block_total = 0;
                _this->fsm_counter      = 0;
                _this->fsm_timestamp    = 0;
                _this->reg_fsm_state.set(FINISHED);
                _this->reg_busy.set(1);
                _this->event_enqueue(_this->fsm_event, 1);

                //report
                int64_t start_time_ns   = (_this->timer_start)/1000;
                int64_t end_time_ns     = (_this->time.get_time())/1000;
                int64_t period_ns       = end_time_ns - start_time_ns;
                int64_t period_clk      = _this->clock.get_cycles() - _this->cycle_start;
                double  period_uti      = (1.0 * _this->ideal_runtime)/(1.0 * period_clk);
                int64_t gemm_size       = 2 * (_this->m_size * _this->n_size + _this->n_size * _this->k_size + 2 * _this->m_size * _this->k_size);
                double  redmul_eff      = period_uti * (1.0 * _this->ce_height * _this->ce_width) / (1.0 * gemm_size);
                _this->total_runtime   += period_ns;
                _this->num_matmul      += 1;
                _this->trace.msg("[LightRedmule] Finished : %0d ns ---> %0d ns | period = %0d ns (%0d cyc) | uti = %0.3f | runtime = %0d ns | GEMM id = %0d | FMT = %0d | M-N-K = %0d-%0d-%0d | X-W-Y = 0x%5x-0x%5x-0x%5x\n",
                    start_time_ns, end_time_ns, period_ns, period_clk, period_uti, _this->total_runtime, _this->num_matmul, _this->compute_able, _this->m_size, _this->n_size, _this->k_size, _this->x_addr, _this->w_addr, _this->y_addr);
            } else {
                _this->reg_fsm_state.set(STORING);
                _this->reg_busy.set(1);
                _this->event_enqueue(_this->fsm_event, 1);
            }
            break;
        }
        case FINISHED: {
            //Retire the job. Offload/legacy-path jobs have no queue entry but still
            //flip cxt_use_ptr and count in completed_jobs
            _this->cxt_job_id[_this->cxt_use_ptr] = -1;
            _this->cxt_use_ptr = 1 - _this->cxt_use_ptr;
            _this->completed_jobs++;
            if (_this->job_pending > 0) {
                _this->job_pending--;
            }
            //Back to GEMM, for the paths that never set gemm_ops
            _this->gemm_ops = GEMM_OP_GEMM;

            //Reply Stalled Query
            if (_this->redmule_query == NULL)
            {
                _this->done.sync(true); //send interrupt
                _this->reg_fsm_state.set(IDLE);
                _this->reg_busy.set(0);
                // IssOffloadInsnGrant<uint32_t> offload_grant = {
                //     .result=0x0
                // };
                // _this->offload_grant_itf.sync(&offload_grant);
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][FINISHED] GRANT core and Wait for query!\n");
            } else {
                _this->reg_fsm_state.set(ACKNOWLEDGE);
                _this->reg_busy.set(1);
            }
            _this->event_enqueue(_this->fsm_event, 1);
            break;
        }
        case ACKNOWLEDGE: {
            _this->redmule_query_port->resp(_this->redmule_query);
            _this->redmule_query = NULL;
            _this->redmule_query_port = NULL;
            _this->done.sync(true); //send interrupt
            _this->reg_fsm_state.set(IDLE);
            _this->reg_busy.set(0);
            // IssOffloadInsnGrant<uint32_t> offload_grant = {
            //     .result=0x0
            // };
            // _this->offload_grant_itf.sync(&offload_grant);
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[LightRedmule][ACKNOWLEDGE] GRANT core and Done!\n");
            _this->event_enqueue(_this->fsm_event, 1);
            break;
        }
        default:
            _this->trace.fatal("[LightRedmule] INVALID RedMule Status: %d\n", _this->reg_fsm_state.get());
    }
}



/************************************************************
*                   Function Implementation                 *
************************************************************/

void matmul_uint16(uint16_t * z, uint16_t * y, uint16_t * x, uint16_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size){
    for (int i = 0; i < m_size; ++i)
    {
        for (int j = 0; j < k_size; ++j)
        {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
            {
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
            }
        }
    }
}

void matmul_int16(int16_t * z, int16_t * y, int16_t * x, int16_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size){
    for (int i = 0; i < m_size; ++i)
    {
        for (int j = 0; j < k_size; ++j)
        {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
            {
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
            }
        }
    }
}

fp16 fp16_fma(fp16 a, fp16 b, fp16 c) {
    flexfloat_t ff_a, ff_b, ff_c, ff_res; 
    flexfloat_desc_t env = (flexfloat_desc_t){5, 10}; 
    ff_init(&ff_a, env); 
    ff_init(&ff_b, env); 
    ff_init(&ff_c, env); 
    ff_init(&ff_res, env); 
    flexfloat_set_bits(&ff_a, a); 
    flexfloat_set_bits(&ff_b, b); 
    flexfloat_set_bits(&ff_c, c);
    ff_fma(&ff_res, &ff_a, &ff_b, &ff_c); 
    return (fp16)flexfloat_get_bits(&ff_res);
}

void LightRedmule::matmul_fp16(fp16 * z, fp16 * y, fp16 * x, fp16 * w, uint16_t m_size, uint16_t n_size, uint16_t k_size){
    for (int i = 0; i < m_size; ++i)
    {
        for (int j = 0; j < k_size; ++j)
        {
            z[i * k_size + j] = y[i * k_size + j];
            //printf("y[%d]=0x%4x\n",i * k_size + j, y[i * k_size + j]);
            for (int k = 0; k < n_size; ++k)
            {
                // this->trace.msg(vp::Trace::LEVEL_TRACE,"[i=%d-j=%d-k=%d] x[%d]=0x%04x w[%d]=0x%04x y[%d]=0x%04x \n",i,
                //                                                                     j,
                //                                                                     k,
                //                                                                     i * n_size + k,
                //                                                                     x[i * n_size + k],
                //                                                                     k * k_size + j,
                //                                                                     w[k * k_size + j],
                //                                                                     i * k_size + j,
                //                                                                     y[i * k_size + j]);
                z[i * k_size + j] = fp16_fma(x[i * n_size + k], w[k * k_size + j], z[i * k_size + j]);
            }
            //this->trace.msg(vp::Trace::LEVEL_TRACE,"z[%d]=0x%4x\n",i * k_size + j, z[i * k_size + j]);
        }
    }
}


void matmul_uint8(uint8_t * z, uint8_t * y, uint8_t * x, uint8_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size){
    for (int i = 0; i < m_size; ++i)
    {
        for (int j = 0; j < k_size; ++j)
        {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
            {
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
            }
        }
    }
}

void matmul_int8(int8_t * z, int8_t * y, int8_t * x, int8_t * w, uint16_t m_size, uint16_t n_size, uint16_t k_size){
    for (int i = 0; i < m_size; ++i)
    {
        for (int j = 0; j < k_size; ++j)
        {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
            {
                z[i * k_size + j] += x[i * n_size + k] * w[k * k_size + j];
            }
        }
    }
}

// Constants for FP8-E4M3 format
#define FP8_EXP_MASK  0x78  // 0111 1000
#define FP8_FRAC_MASK 0x07  // 0000 0111
#define FP8_SIGN_MASK 0x80  // 1000 0000
#define FP8_BIAS      7

// Convert FP8 (E4M3) to float
float fp8e4m3_to_float(fp8e4m3 value) {
    uint8_t sign = (value & FP8_SIGN_MASK) >> 7;
    uint8_t exponent = (value & FP8_EXP_MASK) >> 3;
    uint8_t fraction = value & FP8_FRAC_MASK;

    if (exponent == 0) {
        // Subnormal number
        if (fraction == 0) return sign ? -0.0f : 0.0f;
        return (sign ? -1.0f : 1.0f) * (fraction / 8.0f) * powf(2, -6);
    } else if (exponent == 15) {
        // Infinity or NaN
        return fraction ? NAN : (sign ? -INFINITY : INFINITY);
    }

    // Normalized number
    float mantissa = 1.0f + (fraction / 8.0f);
    float result = mantissa * powf(2, exponent - FP8_BIAS);
    return sign ? -result : result;
}

// Convert float to FP8 (E4M3)
fp8e4m3 float_to_fp8e4m3(float value) {
    if (isnan(value)) return 0x7F;  // NaN representation
    if (isinf(value)) return value < 0 ? 0xF8 : 0x78;  // +/-Inf representation

    uint8_t sign = (value < 0) ? 0x80 : 0x00;
    value = fabsf(value);

    int exponent;
    float mantissa = frexpf(value, &exponent);

    if (value == 0.0f) return 0;  // Zero representation

    exponent += FP8_BIAS - 1;

    if (exponent < 1) {
        // Subnormal handling
        int frac = (int)roundf(value / powf(2, -6) * 8.0f);
        return sign | (frac & FP8_FRAC_MASK);
    } else if (exponent > 14) {
        // Clamp to infinity
        return sign | 0x78;
    }

    // Normal number
    uint8_t frac = (uint8_t)roundf((mantissa - 0.5f) * 16.0f);
    return sign | ((exponent << 3) & FP8_EXP_MASK) | (frac & FP8_FRAC_MASK);
}

// Fused Multiply-Add for FP8
fp8e4m3 fp8e4m3_fma(fp8e4m3 a, fp8e4m3 b, fp8e4m3 c) {
    float fa = fp8e4m3_to_float(a);
    float fb = fp8e4m3_to_float(b);
    float fc = fp8e4m3_to_float(c);

    float result = fa * fb + fc;
    return float_to_fp8e4m3(result);
}

void matmul_fp8e4m3(fp8e4m3 * z, fp8e4m3 * y, fp8e4m3 * x, fp8e4m3 * w, uint16_t m_size, uint16_t n_size, uint16_t k_size){
    for (int i = 0; i < m_size; ++i)
    {
        for (int j = 0; j < k_size; ++j)
        {
            z[i * k_size + j] = y[i * k_size + j];
            for (int k = 0; k < n_size; ++k)
            {
                z[i * k_size + j] = fp8e4m3_fma(x[i * n_size + k], w[k * k_size + j], z[i * k_size + j]);
            }
        }
    }
}
