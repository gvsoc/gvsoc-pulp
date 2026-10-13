// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vector>
#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/csr.hpp>

class Iss;
class SnitchSsr;

// SSR configuration registers (word index, scfgr / scfgw)
#define SNITCH_SSR_REG_STATUS  0
#define SNITCH_SSR_REG_REPEAT  1
#define SNITCH_SSR_REG_BOUNDS  2
#define SNITCH_SSR_REG_STRIDES 6
#define SNITCH_SSR_REG_RPTR    24
#define SNITCH_SSR_REG_WPTR    28

// One stream, from the write of its pointer to its last element
struct SnitchSsrJob
{
    bool is_write;
    int dims;
    iss_reg_t repeat;
    iss_reg_t bounds[4];
    iss_reg_t strides[4];
    // Address generator
    iss_reg_t ptr;
    iss_reg_t cnt[4];
    bool addr_done;
    // Cycle from which the address generator can start the stream, -1 until
    // the stream is the current one of the address generator
    int64_t start_cycle;
    // Accesses left on the FP side (pops of a read stream, including the
    // repetitions, pushes of a write stream)
    int64_t fp_remaining;
};

struct SnitchSsrReq
{
    // First field so that a raw vp::IoReq * can be cast back to its entry
    vp::IoReq req;
    uint64_t data;
    // Data FIFO slot of a read
    int slot;
    // Offset of the element in the 64-bit word read
    int offset;
    int64_t timestamp;
    SnitchSsrReq *next;
};

struct SnitchSsrSlot
{
    uint64_t data;
    // The read data is back
    bool valid;
    // Cycle from which the data can be used: FPU operand of a read, memory
    // write of a write
    int64_t ready_cycle;
};

/**
 * @brief One SSR data mover (RTL snitch_ssr, affine streams).
 *
 * The configuration registers (repeat, bounds, strides) are shadow
 * registers: writing a read or write pointer launches a stream with the
 * current configuration. A launched stream waits behind the current one,
 * at most one of them, the core stalls on a pointer write beyond that.
 *
 * The address generator walks the 4D affine loop nest (the pointer moves by
 * the stride of the outermost dimension which increments) and keeps the
 * data FIFO full: a read takes a FIFO credit when it is issued, and frees it
 * once the FP subsystem consumed the data (repeat + 1 times). A write
 * leaves the FIFO once its data is produced by the FPU. One memory request
 * per cycle on the streamer io_v2 port, with deny / retry.
 */
class SnitchSsrStreamer
{
public:
    SnitchSsrStreamer(Iss &iss, SnitchSsr &top, int id);

    void reset(bool active);

    // Configuration access. cfg_write returns false if the access must be
    // retried (pointer write while a stream is already waiting).
    bool cfg_write(int reg, iss_reg_t value);
    iss_reg_t cfg_read(int reg);

    // FP side: data available to be read at this cycle, room to write, read,
    // write
    inline bool can_read(int64_t cycle);
    inline bool can_write();
    uint64_t pop(int64_t insn_id);
    void push(uint64_t data, int64_t ready_cycle);

    // Address generator, called every cycle while active. Returns true if
    // the streamer still has work.
    bool handle(int64_t cycle);
    bool is_busy();
    // True if a stream can be launched (pointer write)
    inline bool can_launch() { return this->jobs.size() < 2; }

    vp::IoMaster itf{&SnitchSsrStreamer::retry, &SnitchSsrStreamer::response};

private:
    static void retry(vp::Block *__this, vp::IoRetryChannel);
    static vp::IoRespAck response(vp::Block *__this, vp::IoReq *req);
    void send(SnitchSsrReq *req);
    void req_end(SnitchSsrReq *req, int64_t cycle);
    bool addr_next(SnitchSsrJob &job);
    void job_check_end();
    inline SnitchSsrJob *fp_job();
    inline SnitchSsrJob *addr_job();

    Iss &iss;
    SnitchSsr &top;
    int id;
    vp::Trace trace;

    // Shadow configuration
    iss_reg_t repeat;
    iss_reg_t bounds[4];
    iss_reg_t strides[4];
    iss_reg_t ptrs[8];

    // Current stream and the one waiting behind it
    std::vector<SnitchSsrJob> jobs;

    // Data FIFO, shared by reads (slots allocated at request) and writes
    std::vector<SnitchSsrSlot> fifo;
    int fifo_head;
    int fifo_tail;
    int fifo_count;
    // Repetitions done on the FIFO head
    iss_reg_t repeat_count;
    // Last FP read, an instruction reading the same SSR twice pops once
    int64_t last_pop_id;
    uint64_t last_pop_data;

    SnitchSsrReq reqs[CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH];
    SnitchSsrReq *free_reqs;
    SnitchSsrReq *denied_req;
    std::vector<SnitchSsrReq *> sync_reqs;
    int nb_pending_reqs;

    vp::Signal<iss_reg_t> addr_signal;
};

/**
 * @brief The stream semantic registers of the Snitch FP subsystem.
 *
 * When enabled through the ssr CSR (0x7C0), the FP register reads and
 * writes of ft0..ft(NB_SSRS-1) go to the SSR data FIFOs instead of the
 * register file.
 */
class SnitchSsr
{
public:
    SnitchSsr(Iss &iss);

    void reset(bool active);

    bool cfg_write(int reg, int ssr, iss_reg_t value);
    iss_reg_t cfg_read(int reg, int ssr);

    inline bool is_enabled() { return this->enabled; }
    // True if a streamer still moves data
    bool is_busy();
    void activate();

    std::vector<SnitchSsrStreamer *> streamers;

private:
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    bool csr_update(iss_insn_t *insn, bool is_write, iss_reg_t &value);

    Iss &iss;
    vp::Trace trace;
    vp::ClockEvent fsm_event;
    CsrReg csr;
    bool enabled;
};

inline bool SnitchSsrStreamer::can_read(int64_t cycle)
{
    SnitchSsrJob *job = this->fp_job();
    if (job == NULL || job->is_write) return false;
    if (this->fifo_count == 0) return false;
    SnitchSsrSlot &slot = this->fifo[this->fifo_head];
    return slot.valid && slot.ready_cycle <= cycle;
}

inline bool SnitchSsrStreamer::can_write()
{
    SnitchSsrJob *job = this->fp_job();
    return job != NULL && job->is_write && this->fifo_count < CONFIG_GVSOC_ISS_SNITCH_SSR_FIFO_DEPTH;
}

inline SnitchSsrJob *SnitchSsrStreamer::fp_job()
{
    for (SnitchSsrJob &job: this->jobs)
    {
        if (job.fp_remaining > 0) return &job;
    }
    return NULL;
}

inline SnitchSsrJob *SnitchSsrStreamer::addr_job()
{
    for (SnitchSsrJob &job: this->jobs)
    {
        if (!job.addr_done) return &job;
    }
    return NULL;
}
