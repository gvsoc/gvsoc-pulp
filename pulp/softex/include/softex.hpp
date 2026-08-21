/*
* Copyright (C) 2026 ETH Zurich, University of Bologna, and Fondazione Chips-IT
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
*
* Authors:  Alessandro Nadalini <alessandro.nadalini3@unibo.it>
*/

#ifndef __SOFTEX_HPP__
#define __SOFTEX_HPP__

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <vp/register.hpp>
#include <cpu/iss/flexfloat/flexfloat.h>
#include <vector>
#include <queue>
#include <cstdint>
#include "archi_softex.h"

/**************************************************************************
* Every arithmetic step below (max tracking, the exponential unit,
* accumulation, the Newton-Raphson reciprocal, the final normalization
* multiply) is carried out with flexfloat_t values in the matching format
* and correctly rounded at each step, rather than computed in plain
* double/float and only truncated once at the very end. This is the main
* numerical-accuracy improvement over the previous version of this model:
* intermediate rounding (e.g. of the max-subtraction, of each partial
* sum, of the reciprocal) now actually happens where the RTL would round
* it, instead of being hidden behind full double precision until the
* last cast.
**************************************************************************/

namespace SoftexFormat
{
    inline flexfloat_desc_t in()  { return flexfloat_desc_t{8, 7};  } // softex_pkg::FPFORMAT_IN  (FP16ALT)
    inline flexfloat_desc_t acc() { return flexfloat_desc_t{8, 23}; } // softex_pkg::FPFORMAT_ACC (FP32)
}

// ---------------------------------------------------------------------
// Cycle-approximate pipeline latencies, derived from softex_pkg.sv
// ---------------------------------------------------------------------
namespace SoftexLatency
{
    // softex_pkg::NUM_REGS_*
    constexpr int EXP_REGS       = 2;   // NUM_REGS_EXPU
    constexpr int FMA_REGS_IN    = 2;   // NUM_REGS_FMA_IN  (new/old-max subtractor)
    constexpr int FMA_REGS_ACC   = 4;   // NUM_REGS_FMA_ACC (accumulator FMA / divider FMA)
    constexpr int SUM_REGS_ACC   = 2;   // NUM_REGS_SUM_ACC (reduction-tree stage regs)
    constexpr int INV_APPR_REGS  = 1;   // NUM_REGS_INV_APPR
    constexpr int N_NEWTON_ITERS = 2;   // softex_pkg::N_NEWTON_ITERS

    // Cross-checked against softex_acc_ctrl.sv's actual FSM (COMPUTING ->
    // FINISHING -> REDUCTION -> INVERSION -> INV_FMA -> INV_MUL) and
    // calibrated against a real RTL run (LENGTH=1024, PULP cluster integration):
    // ACC_FILL=8, the WAIT_DATAPATH_EMPTY->WAIT_ACCUMULATION transition=8,
    // INV_LATENCY=14.

    // softex_ctrl.sv's WAIT_DATAPATH_EMPTY only waits for the front of the
    // pipe to drain (addmul_o_busy/exp_o_busy/sum_o_busy/add_fifo empty --
    // see softex_datapath.sv's datapath_busy). The accumulator-FMA stage
    // itself is drained separately, by REDUCTION/WAIT_ACCUMULATION below.
    // 2*SUM_REGS_ACC (not clog2(N_ROWS)*SUM_REGS_ACC): the reduction tree's
    // final combine stage doesn't add a further register beyond what's
    // already counted here (confirmed empirically).
    constexpr int ACC_FILL = FMA_REGS_IN + EXP_REGS + 2 * SUM_REGS_ACC;

    // ACC_REDUCE_LAT accounts for every other cycle of FMA_REGS_ACC partial
    // accumulations in flight (see softex_acc_ctrl.sv's REDUCTION
    // state) -- charged once, at the WAIT_DATAPATH_EMPTY -> WAIT_ACCUMULATION
    // transition.
    constexpr int ACC_REDUCE_LAT = 2 * FMA_REGS_ACC;

    // Newton-Raphson reciprocal: N_NEWTON_ITERS iterations, each 2 dependent
    // FMA passes (INV_FMA then INV_MUL in softex_acc_ctrl.sv), but the
    // second pass's operands latch the same cycle the first pass's result
    // becomes valid (result forwarding), so each iteration costs
    // 2*FMA_REGS_ACC-1, not 2*FMA_REGS_ACC; the initial inv_appr approximation
    // (INV_APPR_REGS) overlaps the REDUCTION->INVERSION handoff and doesn't
    // add separately.
    constexpr int INV_LATENCY = N_NEWTON_ITERS * (2 * FMA_REGS_ACC - 1);

    // Handshake latency before the first streaming block is actually
    // visible on the bus (command decode / in_start /out_start
    // pulse in softex_ctrl.sv). Charged once at the start of
    // ACCUMULATION and once at the start of DIVIDING. Calibrated against
    // the same RTL run as above.
    constexpr int STREAM_START_LAT = 5;

    // Overhead for control-plane transitions (register commit, clear, etc.):
    // it matches softex_ctrl.sv's FINISHED state, which is unconditionally
    // 1 cycle (next_state = IDLE is combinational, no wait condition):
    // every *->FINISHED transition should charge exactly this, not a separate
    // drain latency (confirmed against an RTL run -- see the
    // DIVIDING->FINISHED transition in stream_advance_beat()).
    constexpr int CTRL_LAT = 1;
}

// `out` is wired straight into the cluster L1 interleaver, with no
// Router/remove_offset stage in between (see l1_subsystem.py) to strip a
// PI_L1 pointer's cluster base off for us -- so every set_addr() on `out`
// must already be a cluster-local (0-based) offset.
namespace SoftexAddr
{
    constexpr uint32_t CLUSTER_WINDOW_MASK = 0x400000 - 1; // cluster L1 window size (cluster.json)
}

// Softex's own master port toward the L1 interleaver: stream-in and
// stream-out share this single, muxed port (only one direction active at a
// time), so a beat (SOFTEX_N_ROWS elements, up to 16B) that's wider than
// this needs multiple cycles to actually cross the interconnect.
namespace SoftexBus
{
    constexpr uint32_t WIDTH_BYTES = SOFTEX_DATA_WIDTH_BITS / 8; // 96 bits -> 12B (3x32-bit)
}

/**************************************************************************
* Control FSM states
* `softex_state_t`. Stored in a vp::Register<uint32_t> (reg_fsm_state)
**************************************************************************/

enum softex_fsm_state_e : uint32_t
{
    SOFTEX_FSM_IDLE = 0,
    SOFTEX_FSM_WAIT_SLOT_VALID,
    SOFTEX_FSM_ACCUMULATION,
    SOFTEX_FSM_WAIT_DATAPATH_EMPTY,
    SOFTEX_FSM_WAIT_ACCUMULATION,
    SOFTEX_FSM_WAIT_INVERSION,
    SOFTEX_FSM_DIVIDING,
    SOFTEX_FSM_FINISHED
};

// Softex job configured through a register context
struct SoftexJob
{
    uint32_t in_addr        = 0;
    uint32_t out_addr       = 0;
    uint32_t tot_len        = 0;   // in bytes, as programmed by sw
    uint32_t commands       = 0;
    uint32_t cache_base_addr= 0;
    uint32_t cast_ctrl      = 0;

    bool acc_only()       const { return commands & SOFTEX_CMD_ACC_ONLY; }
    bool div_only()       const { return commands & SOFTEX_CMD_DIV_ONLY; }
    bool acquire_slot()   const { return commands & SOFTEX_CMD_ACQUIRE_SLOT; }
    bool last()           const { return commands & SOFTEX_CMD_LAST; }
    bool set_cache_addr() const { return commands & SOFTEX_CMD_SET_CACHE_ADDR; }
    bool no_op()          const { return commands & SOFTEX_CMD_NO_OP; }
    bool cast_input()     const { return commands & SOFTEX_CMD_INT_INPUT; }
    bool cast_output()    const { return commands & SOFTEX_CMD_INT_OUTPUT; }
    uint32_t slot_id()    const { return (commands & SOFTEX_CMD_SLOT_ID_MASK) >> SOFTEX_CMD_SLOT_ID_SHIFT; }

    // int_bits is a signed 7-bit field (bit 6 is the sign bit); sign-extend it manually
    static int sext7(uint32_t v) { v &= 0x7F; return (v >= 64) ? (int)v - 128 : (int)v; }
    int  in_int_bits()    const { return sext7(cast_ctrl & SOFTEX_CAST_IN_INT_BITS_MASK); }
    bool in_signed()      const { return cast_ctrl & SOFTEX_CAST_IN_SIGNED_BIT; }
    int  out_int_bits()   const { return sext7((cast_ctrl & SOFTEX_CAST_OUT_INT_BITS_MASK) >> SOFTEX_CAST_OUT_INT_BITS_SHIFT); }
    bool out_signed()     const { return cast_ctrl & SOFTEX_CAST_OUT_SIGNED_BIT; }
};

// softex_pkg::slot_t: partial-softmax state slot (running max + running
// denominator), used to resume "online softmax" accumulation across calls.
struct SoftexSlot
{
    double   maximum     = -__builtin_inf();
    double   denominator = 0.0;
    bool     valid       = false;
    uint32_t id          = 0;
};

class Softex : public vp::Component
{
public:
    Softex(vp::ComponentConf &config);

    void reset(bool active);

    vp::IoSlave  in;    // config/register port
    vp::IoMaster out;   // data port (operands + slot cache spill)
    vp::WireMaster<bool> irq;
    vp::Trace trace;

    // FSM-state / busy registers
    vp::Register<uint32_t> reg_fsm_state;
    vp::Register<uint32_t> reg_busy;

private:
    static vp::IoReqStatus cfg_slave(vp::Block *__this, vp::IoReq *req);

    // Top-level FSM tick: handles the "control" states (IDLE,
    // WAIT_SLOT_VALID, WAIT_DATAPATH_EMPTY, WAIT_ACCUMULATION,
    // WAIT_INVERSION, FINISHED) directly, and drives ACCUMULATION/DIVIDING
    // via stream_tick() below -- ticking every cycle for as long as either
    // of those two states is active.
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    void fsm_step();
    void enqueue_fsm(int64_t cycles);
    vp::ClockEvent *fsm_event;

    // Logs a phase-boundary timestamp at LEVEL_INFO (cycle + phase name),
    // for lining up against an RTL waveform/testbench cycle breakdown --
    // see every reg_fsm_state.set() call site. Enable with
    // `--trace=.*softex.* --trace-level=info` on gvrun/gapy.
    void trace_phase(const char *name);

    // register file access
    void handle_cfg_read(uint32_t offset, uint32_t *value);
    void handle_cfg_write(uint32_t offset, uint32_t value);

    // ------------------------------------------------------------------
    // HWPE control-register protocol (ACQUIRE / TRIGGER / STATUS /
    // RUNNING_JOB / SOFT_CLEAR), depth-SOFTEX_N_CONTEXT job queue.
    // Modeled after pulp/light_redmule's acquire()/commit()/
    // start_next_job()/soft_clear(), adapted to softex's simpler TRIGGER
    // (no commit/trigger-split bits -- a plain write always commits+starts).
    // ------------------------------------------------------------------
    int32_t acquire();
    void    commit(bool start);
    void    start_next_job();
    void    soft_clear();

    // shared setup for ACCUMULATION/DIVIDING (beat/element bookkeeping +
    // kicking off the streaming engine at beat 0); called from
    // start_next_job() and from the WAIT_SLOT_VALID / WAIT_INVERSION
    // continuations.
    void begin_datapath_phase();

    // state-slot cache (softex_slot_regfile.sv, simplified/functional;
    // unchanged from the previous version -- this is a rare, small
    // metadata access so it stays on the simple blocking helpers below)
    bool slot_lookup(uint32_t id, SoftexSlot &out);
    void slot_update(uint32_t id, double maximum, double denominator, bool free_it);
    void slot_spill_to_mem(SoftexSlot &victim, uint32_t cache_base_addr);
    void slot_fill_from_mem(uint32_t id, uint32_t cache_base_addr, SoftexSlot &out);
    int64_t stream_read_beat(uint32_t addr, uint8_t *buf, int size);
    int64_t stream_write_beat(uint32_t addr, uint8_t *buf, int size);

    // Strips a PI_L1 pointer's cluster-window base off before it reaches
    // `out`'s set_addr() -- see SoftexAddr::CLUSTER_WINDOW_MASK above.
    uint32_t local_addr(uint32_t addr) const;

    // L1 interleaver routes bank_id/offset from a request's start address
    // alone and never splits a wide access across banks itself (see
    // l1_interleaver_impl.cpp's req()) so any transfer that could span
    // more than one 4-byte interleaving granule must be pre-split into
    // per-granule requests, same workaround redmule_streamer.cpp's
    // BYTES_PER_BANK loop applies for the same interconnect. Optionally
    // reports the max latency seen across the sub-chunks (several chunks 
    // land in the same cycle across parallel banks, so the group's cost
    // is their max, not their sum).
    vp::IoReqStatus req_split(vp::IoReq *req, uint32_t addr, uint8_t *buf, uint32_t size, bool is_write,
                               int64_t *out_max_latency = nullptr);

    /**********************************************************************
    * Memory streaming engine (ACCUMULATION / DIVIDING)
    *
    * Modeled directly after pulp/light_redmule's fsm_handler PRELOAD/
    * ROUTINE/STORING cases (see light_redmule.cpp), not an async
    * request-slot pool: softex has a single, muxed master port toward the
    * L1 interleaver (SoftexBus::WIDTH_BYTES wide -- stream-in and
    * stream-out share it, never concurrent), so stream_tick() below issues
    * at most one bus-width block per cycle (an `if`, not a `while`) and
    * tracks in-flight blocks' completion timestamps in pending_req_queue,
    * gated by queue_depth. The L1 target is assumed to always resolve
    * synchronously (IO_REQ_OK) -- no resp/grant callbacks.
    **********************************************************************/
    uint32_t queue_depth;

    vp::IoReq *stream_req;                  // single reused request for streaming
    std::queue<int64_t> pending_req_queue;  // completion timestamp per in-flight block

    uint32_t cur_beat_idx;                  // which beat (0..n_beats-1) is in flight
    uint32_t cur_beat_base_addr;            // local (cluster-window) address of its current direction
    uint32_t cur_beat_bytes;                // valid bytes for the current direction of this beat
    uint32_t cur_beat_bytes_sent;           // bytes already handed to req_split() so far
    bool     cur_beat_is_write;             // DIVIDING only: false=reading input, true=writing output
    uint8_t  cur_beat_buf[SOFTEX_N_ROWS * 2];    // accumulates read bytes (ACC read / DIV read)
    uint8_t  cur_beat_outbuf[SOFTEX_N_ROWS * 2]; // computed output bytes, queued for DIV write

    void stream_start_beat(uint32_t beat_idx);
    void stream_advance_beat();
    void stream_tick();

    // Accelerator datapath methods
    void complete_acc_beat(const uint8_t *buf, uint32_t valid_bytes, uint32_t beat_idx);
    // Computes the normalized output for one DIVIDING beat into `outbuf`
    // (caller-provided, SOFTEX_N_ROWS*2 bytes) and returns its valid byte
    // count; no longer issues the write itself -- see stream_advance_beat().
    uint32_t complete_div_read_beat(const uint8_t *buf, uint32_t valid_bytes, uint32_t beat_idx, uint8_t *outbuf);

    // schraudolph_exp approximates exp(x) the way softex's expu actually
    // does in hardware: not a call to libm, but the classic Schraudolph
    // (1999) bit-trick fast exponential (reinterpret round(x * 2^frac_bits
    // / ln(2)) + bias*2^frac_bits as the raw bits of the target float
    // format), implemented generically via flexfloat's bit accessors.
    // This is a deliberately *approximate* exponential -- matching the
    // real hardware's typical ~1-3% error -- not an accuracy bug.
    flexfloat_t schraudolph_exp(flexfloat_t x) const;
    
    // expu_correction mirrors rtl/expu/expu_correction.sv's secondary
    // piecewise-quadratic mantissa correction, which softex's real
    // expu_row.sv *always* applies on top of the raw Schraudolph result
    // (EXPU_ENABLE_MANT_CORRECTION defaults to 1 in softex_pkg.sv) to
    // pull the linear bit-trick estimate much closer to the true 2^f
    // curve. Ported as an exact integer/bit-width translation of the
    // Verilog (not a real-arithmetic shortcut like schraudolph_exp above)
    // since the branch constants (ALPHA/BETA/GAMMA_*_FIXED) are already
    // narrow fixed-point roundings of softex_pkg.sv's real-valued
    // parameters, not the literal real values themselves -- e.g.
    // EXPU_ALPHA_FIXED = round(0.24609375 * 2^4) = 4, not an encoding of
    // 0.24609375. This has been spot-checked against 2^0.25 and 2^0.5
    // (the correction lands within ~0.02% of the true curve at those
    // points, vs. the raw approximation's several-percent error) but not
    // against an actual RTL simulation run.
    flexfloat_t expu_correction(flexfloat_t sch_result) const;
    
    // expu_exp is the actual hardware exp unit as softex's datapath uses
    // it: schraudolph_exp() followed unconditionally by expu_correction(),
    // matching expu_row.sv's fixed pipeline. Callers in softex_datapath.cpp
    // should call this, not schraudolph_exp() directly.
    flexfloat_t expu_exp(flexfloat_t x) const;
    
    // Newton-Raphson reciprocal (softex_pkg::N_NEWTON_ITERS iterations),
    // seeded from an ordinary double division and refined with
    // correctly-rounded FP32 flexfloat arithmetic each step.
    flexfloat_t newton_reciprocal(flexfloat_t denom) const;
    
    // Input/output casting (when enabled)
    flexfloat_t cast_in_to_ff(const uint8_t *raw, int byte_idx, bool is_int) const;
    void cast_out_from_ff(uint8_t *raw, int byte_idx, flexfloat_t value, bool is_int) const;

    // BF16 <-> FP32 helpers, used only
    static float bf16_to_f32(uint16_t bits);
    static uint16_t f32_to_bf16(float value);

    /**********************************************************************
    * Register contexts and job queues
    **********************************************************************/
    
    SoftexJob contexts[SOFTEX_N_CONTEXT];
    uint8_t   job_id_counter;            // wraps at 256
    int       job_state;                 // 0 = free to acquire, -2 = acquired-not-committed
    int       job_pending;               // 0..SOFTEX_N_CONTEXT committed-but-not-finished jobs
    int       cxt_cfg_ptr;               // context slot software is configuring
    int       cxt_use_ptr;               // context slot the FSM is executing
    int32_t   cxt_job_id[SOFTEX_N_CONTEXT]; // job id per context slot, -1 = empty
    int32_t   running_job_id;            // current/last running job id, -1 = none yet
    uint32_t  slot_cache_base_addr = 0;
    vp::IoReq *mem_req;                  // reused by the blocking slot-cache helpers only

    /**********************************************************************
    * Active job runtime state
    **********************************************************************/
    
    SoftexJob job;                // snapshot of the active context for this run
    SoftexSlot cur_slot;

    uint32_t beat_size_in;        // bytes per beat on the input side (16, or int-cast size)
    uint32_t beat_size_out;
    uint32_t n_beats;             // number of beats for this job's tot_len
    uint32_t num_elements;        // total valid elements (last beat may be partial)
    uint32_t beats_completed;     // beats fully done (ACC: read done; DIV: write done)

    flexfloat_t running_max; // FP16ALT
    // Sum of exp(x - max) during ACCUMULATION
    flexfloat_t running_sum; // FP32
    flexfloat_t reciprocal;

    bool irq_pending = false;

    /**********************************************************************
    * Local state-slot cache
    **********************************************************************/
    int n_state_slots;
    int cache_slot_size;
    std::vector<SoftexSlot> local_slots;
};

#endif
