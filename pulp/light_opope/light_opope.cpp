/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
 * Light OPOPE: outer-product engine, drop-in replacement of LightRedmule.
 *
 * Same software interface as LightRedmule (register file, offload instructions
 * and done irq): Z[M x K] = Y[M x K] + X[M x N] * W[N x K], with Z written in
 * place of Y. N is the reduction dimension (RedMulE naming).
 *
 * Differently from RedMulE, X must be stored TRANSPOSED in memory (Xt[N x M],
 * row-major), so that both operands of one outer product are contiguous:
 *   x = Xt[n, i*2p : i*2p+2p]     w = W[n, j*2p : j*2p+2p]
 *
 * Architecture (p is the parameter):
 *   - p x p fp16 MAC units, fp16 accumulation.
 *   - One memory port of 2p x 16 bit: one beat carries 2p elements.
 *   - One step: 2 beats (x and w, 2p elements each), then 4 cycles to compute
 *     the 2p x 2p outer product as 4 p x p quadrants (one per cycle). Each MAC
 *     unit holds 4 accumulators.
 *   - The 2p x 2p accumulators are preloaded with Y (2p beats) before the
 *     first step of an output tile and stored to Z (2p beats) after the last.
 *     MATMUL (Z = X * W) clears them instead, without using the port.
 *   - Request queue of queue_depth entries: an x/w beat holds an entry from
 *     when it is sent until the array starts its step, a Y or Z beat until
 *     its response is back. x/w beats are sent ahead of the array while an
 *     entry is free, so the memory round trip overlaps with the computation;
 *     a step waits only for its own x and w beats.
 *   - Accumulators are ping-ponged (2 banks): the Y preload of tile t+1 and
 *     the Z store of tile t-1 overlap with the computation of tile t.
 *   - mac_latency extra cycles are charged after the last step of a tile
 *     before its accumulators can be stored (pipeline drain).
 *
 * The model is cycle driven: every cycle at most one beat is sent on the port
 * (priority: x/w of the next 2 steps, Z stores, Y preloads, x/w of later
 * steps) and the array starts a step when its inputs and accumulators are
 * available. Memory latency comes
 * from the synchronous TCDM responses (bank conflicts and interconnect).
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdint.h>
#include <cstring>
#include <queue>
#include <vector>
#include <functional>
#include <algorithm>

#include <cpu/iss/include/offload.hpp>
#include <cpu/iss/flexfloat/flexfloat.h>

typedef uint16_t fp16;

enum opope_state {
    IDLE,
    BUSY,
    FINISHED
};

// Cycles taken by one outer-product step (4 p x p quadrants)
#define OPOPE_STEP_CYCLES 4

class LightOpope : public vp::Component
{
public:
    LightOpope(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

private:
    uint32_t op_format_parser(uint32_t op_format);
    void start_job();
    void tick();
    bool issue_beat();
    void issue_xw();
    void issue_z();
    void issue_y();
    int64_t send_beat(uint32_t addr, uint32_t size, bool is_write, bool track);
    uint32_t queue_used();
    void compute_step(uint32_t tile, uint32_t slot);
    void report();

    // Tile geometry
    uint32_t tile_row(uint32_t tile) { return tile / this->tiles_k; }
    uint32_t tile_col(uint32_t tile) { return tile % this->tiles_k; }
    uint32_t tile_rows(uint32_t tile);
    uint32_t tile_cols(uint32_t tile);

    vp::Trace           trace;
    vp::IoSlave         input_itf;
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf;
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf;
    vp::WireMaster<bool> done;
    vp::IoMaster        tcdm_itf;
    vp::IoReq *         tcdm_req;
    vp::ClockEvent *    fsm_event;
    vp::reg_32          state;

    // Configuration
    uint32_t            tcdm_bank_width;
    uint32_t            tcdm_bank_number;
    uint32_t            elem_size;
    uint32_t            p;
    uint32_t            tile;           // 2p: output tile side, elements per beat
    uint32_t            mac_latency;
    uint32_t            buffered_steps; // x/w slots: steps that can be in the queue
    uint32_t            queue_depth;    // request queue entries (beats)
    uint64_t            loc_base;
    uint32_t            compute_able;
    bool                use_y;          // GEMM loads Y, MATMUL starts from 0

    // Registers
    uint32_t            m_size;
    uint32_t            n_size;
    uint32_t            k_size;
    uint32_t            x_addr;
    uint32_t            w_addr;
    uint32_t            y_addr;
    uint32_t            z_addr;

    // Job progress (steps are numbered globally: g = tile * n_size + n)
    int64_t             now;
    uint32_t            tiles_k;
    uint32_t            num_tiles;
    uint64_t            num_steps;
    uint64_t            load_step;      // next step whose x/w beats are sent
    uint32_t            load_beat;      // 0: x, 1: w
    uint64_t            start_step;     // next step to start on the array
    uint64_t            consumed_steps; // steps whose computation completed (unused by the queue)
    bool                op_active;
    int64_t             op_end;
    uint32_t            op_tile;
    uint32_t            op_n;
    uint32_t            preload_tile;   // next tile whose Y rows are loaded
    uint32_t            preload_row;
    uint32_t            computed_tiles;
    uint32_t            store_tile;     // next tile whose Z rows are stored
    uint32_t            store_row;
    int64_t             acc_ready[2];   // Y preload of the bank fully returned
    int64_t             acc_done[2];    // last step + drain of the bank done
    std::vector<uint64_t> slot_step;
    std::vector<int64_t>  slot_ready;
    std::priority_queue<int64_t, std::vector<int64_t>, std::greater<int64_t>> pending; // Y/Z beats in flight

    // Statistics
    int64_t             timer_start;
    int64_t             cycle_start;
    int64_t             total_runtime;
    int64_t             num_matmul;
    uint64_t            stall_cycles;

    // Data
    uint8_t *           access_buffer;
    std::vector<fp16 *> slot_x;
    std::vector<fp16 *> slot_w;
    fp16 *              acc[2];
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new LightOpope(config);
}

static fp16 fp16_fma(fp16 a, fp16 b, fp16 c)
{
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

LightOpope::LightOpope(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&LightOpope::req);
    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("tcdm", &this->tcdm_itf);
    this->offload_itf.set_sync_meth(&LightOpope::offload_sync);
    this->new_slave_port("offload", &this->offload_itf, this);
    this->new_master_port("offload_grant", &this->offload_grant_itf, this);
    this->new_master_port("done_irq", &this->done, this);

    this->tcdm_bank_width   = get_js_config()->get("tcdm_bank_width")->get_int();
    this->tcdm_bank_number  = get_js_config()->get("tcdm_bank_number")->get_int();
    this->elem_size         = get_js_config()->get("elem_size")->get_int();
    this->p                 = get_js_config()->get("p")->get_int();
    this->mac_latency       = get_js_config()->get("mac_latency")->get_int();
    this->queue_depth       = get_js_config()->get("queue_depth")->get_int();
    this->loc_base          = get_js_config()->get("loc_base")->get_double();
    this->tile              = 2 * this->p;
    this->compute_able      = 0;

    if (this->elem_size != 2)
        this->trace.fatal("[LightOpope] Only 16-bit elements are supported (elem_size=%d)\n", this->elem_size);
    if (this->tcdm_bank_width * this->tcdm_bank_number < this->tile * this->elem_size)
        this->trace.fatal("[LightOpope] Memory port (%d B) narrower than one beat of 2p elements (%d B)\n",
            this->tcdm_bank_width * this->tcdm_bank_number, this->tile * this->elem_size);
    if (this->queue_depth < 2)
        this->trace.fatal("[LightOpope] queue_depth must be at least 2 (one step is an x and a w beat)\n");
    // One step is two beats (x and w): at most queue_depth / 2 steps in the queue
    this->buffered_steps    = this->queue_depth / 2 + 1;

    this->m_size = 4;
    this->n_size = 4;
    this->k_size = 4;
    this->x_addr = 0;
    this->w_addr = 0;
    this->y_addr = 0;
    this->z_addr = 0;

    this->access_buffer = new uint8_t [this->tile * this->elem_size];
    for (uint32_t s = 0; s < this->buffered_steps; s++)
    {
        this->slot_x.push_back(new fp16 [this->tile]);
        this->slot_w.push_back(new fp16 [this->tile]);
    }
    this->slot_step.resize(this->buffered_steps);
    this->slot_ready.resize(this->buffered_steps);
    for (int b = 0; b < 2; b++)
    {
        this->acc[b] = new fp16 [this->tile * this->tile];
    }

    this->state.set(IDLE);
    this->use_y = true;
    this->tcdm_req          = this->tcdm_itf.req_new(0, 0, 0, 0);
    this->fsm_event         = this->event_new(&LightOpope::fsm_handler);
    this->timer_start       = 0;
    this->cycle_start       = 0;
    this->total_runtime     = 0;
    this->num_matmul        = 0;

    this->trace.msg("[LightOpope] Model Initialization Done! (p=%d, port=%d B, queue=%d beats)\n",
        this->p, this->tcdm_bank_width * this->tcdm_bank_number, this->queue_depth);
}

uint32_t LightOpope::tile_rows(uint32_t tile)
{
    uint32_t left = this->m_size - this->tile_row(tile) * this->tile;
    return left < this->tile ? left : this->tile;
}

uint32_t LightOpope::tile_cols(uint32_t tile)
{
    uint32_t left = this->k_size - this->tile_col(tile) * this->tile;
    return left < this->tile ? left : this->tile;
}

void LightOpope::start_job()
{
    this->tiles_k           = (this->k_size + this->tile - 1) / this->tile;
    this->num_tiles         = ((this->m_size + this->tile - 1) / this->tile) * this->tiles_k;
    this->num_steps         = (uint64_t)this->num_tiles * this->n_size;
    this->now               = 0;
    this->load_step         = 0;
    this->load_beat         = 0;
    this->start_step        = 0;
    this->consumed_steps    = 0;
    this->op_active         = false;
    this->op_end            = 0;
    this->preload_tile      = 0;
    this->preload_row       = 0;
    this->computed_tiles    = 0;
    this->store_tile        = 0;
    this->store_row         = 0;
    this->stall_cycles      = 0;
    for (int b = 0; b < 2; b++)
    {
        this->acc_ready[b]  = 0;
        this->acc_done[b]   = 0;
    }
    while (!this->pending.empty()) this->pending.pop();

    this->timer_start       = this->time.get_time();
    this->cycle_start       = this->clock.get_cycles();
    this->state.set(BUSY);
    this->event_enqueue(this->fsm_event, 1);
}

// Sends one beat, returns the cycle at which its response is back. Y/Z beats
// (track) hold their queue entry until then, x/w beats until their step starts.
int64_t LightOpope::send_beat(uint32_t addr, uint32_t size, bool is_write, bool track)
{
    this->tcdm_req->init();
    this->tcdm_req->set_addr(addr - this->loc_base);
    this->tcdm_req->set_data(this->access_buffer);
    this->tcdm_req->set_size(size);
    this->tcdm_req->set_is_write(is_write);
    if (this->tcdm_itf.req(this->tcdm_req) != vp::IO_REQ_OK)
    {
        this->trace.fatal("[LightOpope] Error while accessing 0x%08x (size %d, write %d)\n", addr, size, is_write);
    }
    int64_t ready = this->now + this->tcdm_req->get_latency();
    if (track) this->pending.push(ready);
    return ready;
}

// Queue entries in use: x/w beats of the steps not started yet, Y/Z beats in flight
uint32_t LightOpope::queue_used()
{
    return 2 * (this->load_step - this->start_step) + this->load_beat + this->pending.size();
}

// One x or w beat of step load_step
void LightOpope::issue_xw()
{
    uint32_t es = this->elem_size;
    uint32_t t      = this->load_step / this->n_size;
    uint32_t n      = this->load_step % this->n_size;
    uint32_t slot   = this->load_step % this->buffered_steps;
    uint32_t addr, len;
    if (this->load_beat == 0)
    {
        len  = this->tile_rows(t);
        addr = this->x_addr + (n * this->m_size + this->tile_row(t) * this->tile) * es;
    } else {
        len  = this->tile_cols(t);
        addr = this->w_addr + (n * this->k_size + this->tile_col(t) * this->tile) * es;
    }
    int64_t ready = this->send_beat(addr, len * es, false, false);
    fp16 *dst = this->load_beat == 0 ? this->slot_x[slot] : this->slot_w[slot];
    memset(dst, 0, this->tile * sizeof(fp16));
    memcpy(dst, this->access_buffer, len * es);
    this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] Load %c tile %d n %d [addr=0x%08x] ready @%ld\n",
        this->load_beat == 0 ? 'X' : 'W', t, n, addr, ready);

    if (this->load_beat == 0)
    {
        this->slot_step[slot]   = this->load_step;
        this->slot_ready[slot]  = ready;
        this->load_beat         = 1;
    } else {
        this->slot_ready[slot]  = std::max(this->slot_ready[slot], ready);
        this->load_beat         = 0;
        this->load_step        += 1;
    }
}

// One Z row of tile store_tile
void LightOpope::issue_z()
{
    uint32_t es = this->elem_size;
    uint32_t t      = this->store_tile;
    uint32_t row    = this->store_row;
    uint32_t len    = this->tile_cols(t);
    uint32_t addr   = this->z_addr + ((this->tile_row(t) * this->tile + row) * this->k_size + this->tile_col(t) * this->tile) * es;
    memcpy(this->access_buffer, &this->acc[t % 2][row * this->tile], len * es);
    this->send_beat(addr, len * es, true, true);
    this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] Store Z tile %d row %d [addr=0x%08x]\n", t, row, addr);
    if (++this->store_row == this->tile_rows(t))
    {
        this->store_row   = 0;
        this->store_tile += 1;
    }
}

// One Y row of tile preload_tile, in the accumulator bank freed by the store of tile - 2
void LightOpope::issue_y()
{
    uint32_t es = this->elem_size;
    uint32_t t      = this->preload_tile;
    uint32_t row    = this->preload_row;
    uint32_t len    = this->tile_cols(t);
    uint32_t addr   = this->y_addr + ((this->tile_row(t) * this->tile + row) * this->k_size + this->tile_col(t) * this->tile) * es;
    int64_t ready   = this->send_beat(addr, len * es, false, true);
    if (row == 0)
    {
        memset(this->acc[t % 2], 0, this->tile * this->tile * sizeof(fp16));
        this->acc_ready[t % 2] = ready;
    }
    memcpy(&this->acc[t % 2][row * this->tile], this->access_buffer, len * es);
    this->acc_ready[t % 2] = std::max(this->acc_ready[t % 2], ready);
    this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] Load Y tile %d row %d [addr=0x%08x] ready @%ld\n", t, row, addr, ready);
    if (++this->preload_row == this->tile_rows(t))
    {
        this->preload_row   = 0;
        this->preload_tile += 1;
    }
}

bool LightOpope::issue_beat()
{
    // MATMUL: clear the free accumulator banks, no Y beats
    while (!this->use_y && (this->preload_tile < this->num_tiles) &&
           (this->preload_tile < this->store_tile + 2))
    {
        memset(this->acc[this->preload_tile % 2], 0, this->tile * this->tile * sizeof(fp16));
        this->acc_ready[this->preload_tile % 2] = this->now;
        this->preload_tile += 1;
    }

    if (this->queue_used() >= this->queue_depth)
    {
        return false;
    }

    bool xw_left = (this->load_step < this->num_steps) &&
                   (this->load_step < this->start_step + this->buffered_steps);

    // x/w of the next steps of the array
    if (xw_left && (this->load_step < this->start_step + 2))
    {
        this->issue_xw();
        return true;
    }

    // Z stores of a computed tile
    if ((this->store_tile < this->computed_tiles) &&
        (this->acc_done[this->store_tile % 2] <= this->now))
    {
        this->issue_z();
        return true;
    }

    // Y preloads
    if ((this->preload_tile < this->num_tiles) &&
        (this->preload_tile < this->store_tile + 2))
    {
        this->issue_y();
        return true;
    }

    // x/w of later steps
    if (xw_left)
    {
        this->issue_xw();
        return true;
    }

    return false;
}

// Functional model of one step: 2p x 2p outer product accumulated in fp16
void LightOpope::compute_step(uint32_t tile, uint32_t slot)
{
    if (this->compute_able == 0) return;
    fp16 *z = this->acc[tile % 2];
    fp16 *x = this->slot_x[slot];
    fp16 *w = this->slot_w[slot];
    uint32_t rows = this->tile_rows(tile);
    uint32_t cols = this->tile_cols(tile);
    for (uint32_t r = 0; r < rows; r++)
    {
        for (uint32_t c = 0; c < cols; c++)
        {
            z[r * this->tile + c] = fp16_fma(x[r], w[c], z[r * this->tile + c]);
        }
    }
}

void LightOpope::tick()
{
    // Retire returned requests (bounds the outstanding requests)
    while (!this->pending.empty() && this->pending.top() <= this->now)
    {
        this->pending.pop();
    }

    // Array: complete the running step
    if (this->op_active && (this->now >= this->op_end))
    {
        this->op_active       = false;
        this->consumed_steps += 1;
        if (this->op_n == this->n_size - 1)
        {
            this->acc_done[this->op_tile % 2] = this->op_end + this->mac_latency;
            this->computed_tiles += 1;
        }
    }

    // Array: start the next step when its inputs and accumulators are there
    if (!this->op_active && (this->start_step < this->num_steps))
    {
        uint32_t t      = this->start_step / this->n_size;
        uint32_t slot   = this->start_step % this->buffered_steps;
        bool acc_ok     = (this->preload_tile > t) && (this->acc_ready[t % 2] <= this->now);
        bool in_ok      = (this->load_step > this->start_step) && (this->slot_step[slot] == this->start_step) &&
                          (this->slot_ready[slot] <= this->now);
        if (acc_ok && in_ok)
        {
            this->compute_step(t, slot);
            this->op_active   = true;
            this->op_end      = this->now + OPOPE_STEP_CYCLES;
            this->op_tile     = t;
            this->op_n        = this->start_step % this->n_size;
            this->start_step += 1;
        } else {
            this->stall_cycles += 1;
        }
    }

    // Port: one beat per cycle
    this->issue_beat();
}

void LightOpope::report()
{
    int64_t start_time_ns   = (this->timer_start)/1000;
    int64_t end_time_ns     = (this->time.get_time())/1000;
    int64_t period_ns       = end_time_ns - start_time_ns;
    int64_t period_clk      = this->clock.get_cycles() - this->cycle_start;
    double  ideal_runtime   = 1.0 * this->m_size * this->n_size * this->k_size / (1.0 * this->p * this->p);
    double  period_uti      = ideal_runtime / (1.0 * period_clk);
    this->total_runtime    += period_ns;
    this->num_matmul       += 1;
    this->trace.msg("[LightOpope] Finished : %0d ns ---> %0d ns | period = %0d ns (%0d cyc) | uti = %0.3f | stall = %0d cyc | runtime = %0d ns | GEMM id = %0d | FMT = %0d | M-N-K = %0d-%0d-%0d | X-W-Y = 0x%5x-0x%5x-0x%5x\n",
        start_time_ns, end_time_ns, period_ns, period_clk, period_uti, this->stall_cycles, this->total_runtime, this->num_matmul,
        this->compute_able, this->m_size, this->n_size, this->k_size, this->x_addr, this->w_addr, this->y_addr);
}

void LightOpope::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    LightOpope *_this = (LightOpope *)__this;

    switch (_this->state.get()) {
        case IDLE:
            _this->done.sync(false); //clear irq
            break;

        case BUSY: {
            _this->tick();
            _this->now += 1;
            if ((_this->store_tile == _this->num_tiles) && _this->pending.empty())
            {
                _this->report();
                _this->state.set(FINISHED);
            }
            _this->event_enqueue(_this->fsm_event, 1);
            break;
        }

        case FINISHED: {
            _this->done.sync(true); //send interrupt
            _this->state.set(IDLE);
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope][FINISHED] Done!\n");
            _this->event_enqueue(_this->fsm_event, 1);
            break;
        }

        default:
            _this->trace.fatal("[LightOpope] INVALID state: %d\n", _this->state.get());
    }
}

uint32_t LightOpope::op_format_parser(uint32_t op_format)
{
    uint32_t data_format = op_format & 0x7;
    uint32_t operation = (op_format >> 3) & 0x7;
    // Only FP16 is supported (compute_able=3, as in LightRedmule).
    // operation=1: GEMM (Z = X * W + Y), operation=0: MATMUL (Z = X * W)
    if ((operation <= 1) && (data_format == 1))
    {
        this->use_y = (operation == 1);
        return 3;
    }
    this->trace.fatal("[LightOpope] Unsupported operation/format [op_format=0x%x-data_format=%d-operation=%d], only FP16 GEMM/MATMUL\n",
        op_format, data_format, operation);
    return 0;
}

void LightOpope::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    LightOpope *_this = (LightOpope *)__this;
    uint32_t opc = insn->opcode & 0x7F;

    switch (opc)
    {
        case 0b0001011:
        {
            if (_this->state.get() == IDLE) {
                insn->granted = true;
                _this->m_size = insn->arg_a & 0xFFFF;
                _this->n_size = insn->arg_b;
                _this->k_size = (insn->arg_a) >> 16;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] Set MNK size: %d, %d, %d\n", _this->m_size, _this->n_size, _this->k_size);
            }
            break;
        }
        case 0b0101011:
        {
            if (_this->state.get() == IDLE)
            {
                insn->granted = true;
                _this->x_addr = insn->arg_a;
                _this->w_addr = insn->arg_b;
                _this->y_addr = insn->arg_c;
                _this->z_addr = _this->y_addr;
                _this->compute_able = _this->op_format_parser(insn->arg_d);
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] Set XWY addr: 0x%08x, 0x%08x, 0x%08x\n", _this->x_addr, _this->w_addr, _this->y_addr);
                if ((_this->m_size == 0) || (_this->n_size == 0) || (_this->k_size == 0))
                {
                    _this->trace.fatal("[LightOpope] INVALID configuration (M-N-K): %d, %d, %d\n", _this->m_size, _this->n_size, _this->k_size);
                }
                _this->start_job();
            }
            break;
        }
    }
}

// Same register map as LightRedmule
vp::IoReqStatus LightOpope::req(vp::Block *__this, vp::IoReq *req)
{
    LightOpope *_this = (LightOpope *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    if (req->get_is_write()) {
        uint32_t value = *(uint32_t *)data;
        switch (offset) {
            case 0x00: {
                if (_this->state.get() == IDLE) {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] configuration (M-N-K): %d, %d, %d. Compute able is %d\n", _this->m_size, _this->n_size, _this->k_size, _this->compute_able);
                    if ((_this->m_size == 0) || (_this->n_size == 0) || (_this->k_size == 0))
                    {
                        _this->trace.fatal("[LightOpope] INVALID configuration (M-N-K): %d, %d, %d\n", _this->m_size, _this->n_size, _this->k_size);
                        return vp::IO_REQ_OK;
                    }
                    _this->start_job();
                }
                break;
            }
            case 0x40:
                _this->x_addr = value;
                break;
            case 0x44:
                _this->w_addr = value;
                break;
            case 0x48:
                _this->y_addr = value;
                _this->z_addr = _this->y_addr;
                break;
            case 0x4C: //REDMULE_MCFG0_PTR
                _this->m_size = value & 0xFFFF;
                _this->k_size = value >> 16;
                break;
            case 0x50: //REDMULE_MCFG1_PTR
                _this->n_size = value & 0xFFFF;
                break;
            case 0x54:
                // Same marith mapping as LightRedmule: op << 10 | fmt << 7
                _this->compute_able = _this->op_format_parser((((value >> 10) & 0x7) << 3) | ((value >> 7) & 0x7));
                break;
            default:
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] write to INVALID address 0x%x\n", offset);
        }
    } else {
        switch (offset) {
            case 0x0C: {
                int32_t status = (_this->state.get() == IDLE) ? 0x00 : 0x01;
                memcpy((void *)data, (void *)&status, size);
                break;
            }
            default:
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[LightOpope] read to INVALID address 0x%x\n", offset);
        }
    }
    return vp::IO_REQ_OK;
}
