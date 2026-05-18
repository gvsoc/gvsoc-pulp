/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
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
 * IDmaTesterV2 — model-level testbench driver for idma_v2.
 *
 * Drives an io_v2 register master into the iDMA's register frontend, observes
 * an IRQ wire, and uses an io_v2 memory master (bound to the same v2 router as
 * the iDMA's AXI) to:
 *   1. fill a known pattern in the source memory range,
 *   2. program the iDMA registers and trigger a transfer,
 *   3. wait for the IRQ pulse signalling completion,
 *   4. read the destination range back and compare against the pattern.
 *
 * The tester runs serially — one outstanding request at a time on each port —
 * to keep the verification simple. It calls engine->quit(0) on full match,
 * quit(1) on the first mismatch or timeout.
 *
 * All transfer parameters (src/dst/length/strides/reps/config/seed) come from
 * the JSON config built by tests/idma_v2/test.py.
 */

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <cstdio>
#include <cstring>


// iDMA register offsets (mirror gvsoc/pulp/models/pulp/ips/pulp/idma_v2/fe/idma_fe_reg.cpp).
static constexpr uint64_t IDMA_REG_TRIGGER     = 0x10;
static constexpr uint64_t IDMA_REG_DST         = 0xd0;
static constexpr uint64_t IDMA_REG_SRC         = 0xd8;
static constexpr uint64_t IDMA_REG_LENGTH      = 0xe0;
static constexpr uint64_t IDMA_REG_DST_STRIDE  = 0xe8;
static constexpr uint64_t IDMA_REG_SRC_STRIDE  = 0xf0;
static constexpr uint64_t IDMA_REG_REPS        = 0xf8;


class IDmaTesterV2 : public vp::Component
{
public:
    IDmaTesterV2(vp::ComponentConf &conf);
    void reset(bool active) override;

private:
    enum Phase
    {
        PHASE_FILL,
        PHASE_PROGRAM,
        PHASE_WAIT_IRQ,
        PHASE_READBACK,
        PHASE_DONE,
    };

    static void regs_resp(vp::Block *__this, vp::IoReq *req);
    static void regs_retry(vp::Block *__this);
    static void mem_resp(vp::Block *__this, vp::IoReq *req);
    static void mem_retry(vp::Block *__this);
    static void irq_sync(vp::Block *__this, bool value);
    static void step_handler(vp::Block *__this, vp::ClockEvent *event);
    static void timeout_handler(vp::Block *__this, vp::ClockEvent *event);

    void schedule_step(int delay = 1);
    void step();
    void start_phase(Phase p);

    // Pattern: byte at logical offset `i` is (seed + i) & 0xff. Logical offset
    // for (rep r, byte b) is r * length + b.
    uint8_t pattern_byte(uint64_t logical_offset) const
    {
        return (uint8_t)((this->pattern_seed + logical_offset) & 0xff);
    }

    // Fill 4 bytes at the requested address with pattern starting at logical_offset.
    void issue_mem_write(uint64_t addr, uint64_t logical_offset);
    // Read 4 bytes at the requested address; on resp, compare against pattern.
    void issue_mem_read(uint64_t addr, uint64_t expected_logical_offset);
    // Program one register; on resp, advance the program cursor.
    void issue_reg_write(uint64_t offset, uint32_t value);
    // Trigger transfer (write to NEXT_ID register, value irrelevant).
    void issue_trigger();

    void fail(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void pass();

    vp::IoMaster regs_master{&IDmaTesterV2::regs_retry, &IDmaTesterV2::regs_resp};
    vp::IoMaster mem_master {&IDmaTesterV2::mem_retry,  &IDmaTesterV2::mem_resp};
    vp::WireSlave<bool> irq_slave;
    vp::ClockEvent step_event;
    vp::ClockEvent timeout_event;
    vp::Trace trace;

    // Configuration (from JSON):
    uint64_t regs_addr;
    uint64_t src;
    uint64_t dst;
    uint64_t length;       // bytes per repetition
    uint64_t src_stride;   // 0 for 1D
    uint64_t dst_stride;   // 0 for 1D
    uint64_t reps;         // 1 for 1D
    uint64_t config_word;  // 0 for 1D, (1<<1) for 2D
    uint32_t pattern_seed;
    int64_t  quit_after_cycles;

    // Per-step state:
    Phase phase;
    uint64_t cur_rep;
    uint64_t cur_byte;     // byte cursor within current rep, advances by 4
    int      program_step; // index into the register-write sequence

    // Cached payload buffers (reused across requests since one is in flight at a time).
    uint8_t  mem_data[4];
    uint8_t  mem_expected[4];
    uint64_t mem_expected_offset;  // logical offset of the in-flight read
    uint8_t  reg_data[4];

    // Single in-flight request handles. Heap-allocated so they can be reused.
    vp::IoReq mem_req;
    vp::IoReq reg_req;

    bool waiting_for_resp = false;
    bool got_irq = false;

    // Latency / bandwidth measurement points. trigger_cycle is captured the
    // moment we transition into WAIT_IRQ (i.e. one cycle after the trigger
    // reg-write resp); irq_cycle is captured the moment irq_sync fires. The
    // "iDMA-only" cost is irq_cycle - trigger_cycle.
    int64_t trigger_cycle = 0;
    int64_t irq_cycle = 0;
};


IDmaTesterV2::IDmaTesterV2(vp::ComponentConf &config)
    : vp::Component(config),
      step_event(this, &IDmaTesterV2::step_handler),
      timeout_event(this, &IDmaTesterV2::timeout_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_master_port("regs", &this->regs_master);
    this->new_master_port("mem",  &this->mem_master);

    this->irq_slave.set_sync_meth(&IDmaTesterV2::irq_sync);
    this->new_slave_port("irq", &this->irq_slave);

    js::Config *cfg = this->get_js_config();
    this->regs_addr   = (uint64_t)cfg->get_child_int("regs_addr");
    this->src         = (uint64_t)cfg->get_child_int("src");
    this->dst         = (uint64_t)cfg->get_child_int("dst");
    this->length      = (uint64_t)cfg->get_child_int("length");
    this->src_stride  = (uint64_t)cfg->get_child_int("src_stride");
    this->dst_stride  = (uint64_t)cfg->get_child_int("dst_stride");
    this->reps        = (uint64_t)cfg->get_child_int("reps");
    this->config_word = (uint64_t)cfg->get_child_int("config");
    this->pattern_seed = (uint32_t)cfg->get_child_int("pattern_seed");
    this->quit_after_cycles = cfg->get_child_int("quit_after_cycles");
    if (this->quit_after_cycles <= 0)
        this->quit_after_cycles = 1000000;

    this->mem_req.set_data(this->mem_data);
    this->mem_req.set_size(4);
    this->reg_req.set_data(this->reg_data);
    this->reg_req.set_size(4);
}


void IDmaTesterV2::reset(bool active)
{
    if (!active)
    {
        // Source memory is preloaded by the target via stim_file, so we go
        // straight to PROGRAM. This keeps the iDMA's idma_cycles measurement
        // unaffected by tester-FILL traffic on the shared router.
        this->phase = PHASE_PROGRAM;
        this->cur_rep = 0;
        this->cur_byte = 0;
        this->program_step = 0;
        this->waiting_for_resp = false;
        this->got_irq = false;
        printf("[%ld] tester START reps=%lu length=%lu src=0x%lx dst=0x%lx seed=0x%x\n",
            this->clock.get_cycles(), this->reps, this->length, this->src, this->dst,
            this->pattern_seed);
        this->schedule_step(1);
        this->timeout_event.enqueue(this->quit_after_cycles);
    }
}


void IDmaTesterV2::schedule_step(int delay)
{
    if (delay < 1) delay = 1;
    if (!this->step_event.is_enqueued())
        this->step_event.enqueue(delay);
}


void IDmaTesterV2::start_phase(Phase p)
{
    this->phase = p;
    this->cur_rep = 0;
    this->cur_byte = 0;
    this->program_step = 0;
    printf("[%ld] tester PHASE %d\n", this->clock.get_cycles(), (int)p);
    this->schedule_step(1);
}


void IDmaTesterV2::issue_mem_write(uint64_t addr, uint64_t logical_offset)
{
    for (int i = 0; i < 4; i++)
        this->mem_data[i] = this->pattern_byte(logical_offset + i);

    this->mem_req.prepare();
    this->mem_req.set_addr(addr);
    this->mem_req.set_size(4);
    this->mem_req.set_is_write(true);
    this->mem_req.set_data(this->mem_data);
    this->mem_req.is_first = true;
    this->mem_req.is_last  = true;
    this->mem_req.burst_id = -1;
    this->mem_req.set_resp_status(vp::IO_RESP_OK);

    this->waiting_for_resp = true;
    vp::IoReqStatus st = this->mem_master.req(&this->mem_req);
    if (st == vp::IO_REQ_DONE)
    {
        this->waiting_for_resp = false;
        if (this->mem_req.get_resp_status() != vp::IO_RESP_OK)
        {
            this->fail("mem write at 0x%lx returned INVALID", addr);
        }
    }
    else if (st == vp::IO_REQ_DENIED)
    {
        // Wait for retry. Since we only have one request in flight we just don't
        // schedule the next step; mem_retry will call schedule_step.
    }
    // IO_REQ_GRANTED: wait for resp
}


void IDmaTesterV2::issue_mem_read(uint64_t addr, uint64_t expected_logical_offset)
{
    this->mem_expected_offset = expected_logical_offset;
    for (int i = 0; i < 4; i++)
        this->mem_expected[i] = this->pattern_byte(expected_logical_offset + i);

    this->mem_req.prepare();
    this->mem_req.set_addr(addr);
    this->mem_req.set_size(4);
    this->mem_req.set_is_write(false);
    this->mem_req.set_data(this->mem_data);
    this->mem_req.is_first = true;
    this->mem_req.is_last  = true;
    this->mem_req.burst_id = -1;
    this->mem_req.set_resp_status(vp::IO_RESP_OK);
    // Pre-poison the buffer so a missing response is visible.
    for (int i = 0; i < 4; i++) this->mem_data[i] = 0xff;

    this->waiting_for_resp = true;
    vp::IoReqStatus st = this->mem_master.req(&this->mem_req);
    if (st == vp::IO_REQ_DONE)
    {
        this->waiting_for_resp = false;
        if (this->mem_req.get_resp_status() != vp::IO_RESP_OK)
        {
            this->fail("mem read at 0x%lx returned INVALID", addr);
            return;
        }
        // Compare in line with the sync DONE.
        if (memcmp(this->mem_data, this->mem_expected, 4) != 0)
        {
            this->fail("mismatch at 0x%lx logical_off=0x%lx: got %02x%02x%02x%02x expected %02x%02x%02x%02x",
                addr, expected_logical_offset,
                this->mem_data[0], this->mem_data[1], this->mem_data[2], this->mem_data[3],
                this->mem_expected[0], this->mem_expected[1], this->mem_expected[2], this->mem_expected[3]);
        }
    }
    else if (st == vp::IO_REQ_DENIED)
    {
        // Wait for retry.
    }
    // IO_REQ_GRANTED: wait for resp
}


void IDmaTesterV2::issue_reg_write(uint64_t offset, uint32_t value)
{
    *(uint32_t *)this->reg_data = value;

    this->reg_req.prepare();
    this->reg_req.set_addr(this->regs_addr + offset);
    this->reg_req.set_size(4);
    this->reg_req.set_is_write(true);
    this->reg_req.set_data(this->reg_data);
    this->reg_req.is_first = true;
    this->reg_req.is_last  = true;
    this->reg_req.burst_id = -1;
    this->reg_req.set_resp_status(vp::IO_RESP_OK);

    this->waiting_for_resp = true;
    vp::IoReqStatus st = this->regs_master.req(&this->reg_req);
    if (st == vp::IO_REQ_DONE)
    {
        this->waiting_for_resp = false;
        if (this->reg_req.get_resp_status() != vp::IO_RESP_OK)
        {
            this->fail("reg write to 0x%lx returned INVALID", offset);
        }
    }
}


void IDmaTesterV2::issue_trigger()
{
    // Capture trigger_cycle before issuing so it's correct even when the
    // iDMA's frontend acks the transfer synchronously (zero-size or
    // zero-rep cases) and the IRQ fires inside this very req() call.
    this->trigger_cycle = this->clock.get_cycles();
    // Triggering = writing anything to TRIGGER (NEXT_ID register).
    this->issue_reg_write(IDMA_REG_TRIGGER, 0);
}


void IDmaTesterV2::step()
{
    if (this->waiting_for_resp) return;

    switch (this->phase)
    {
        case PHASE_FILL:
        {
            if (this->cur_rep >= this->reps)
            {
                this->start_phase(PHASE_PROGRAM);
                return;
            }
            uint64_t addr  = this->src + this->cur_rep * this->src_stride + this->cur_byte;
            uint64_t logoff = this->cur_rep * this->length + this->cur_byte;
            this->issue_mem_write(addr, logoff);

            this->cur_byte += 4;
            if (this->cur_byte >= this->length)
            {
                this->cur_byte = 0;
                this->cur_rep++;
            }
            break;
        }

        case PHASE_PROGRAM:
        {
            // Program registers in the order the v1 reg_dma test sequence used.
            // After all are programmed, trigger and move to WAIT_IRQ.
            switch (this->program_step)
            {
                case 0: this->issue_reg_write(IDMA_REG_DST,        (uint32_t)this->dst); break;
                case 1: this->issue_reg_write(IDMA_REG_SRC,        (uint32_t)this->src); break;
                case 2: this->issue_reg_write(IDMA_REG_LENGTH,     (uint32_t)this->length); break;
                case 3: this->issue_reg_write(IDMA_REG_DST_STRIDE, (uint32_t)this->dst_stride); break;
                case 4: this->issue_reg_write(IDMA_REG_SRC_STRIDE, (uint32_t)this->src_stride); break;
                case 5: this->issue_reg_write(IDMA_REG_REPS,       (uint32_t)this->reps); break;
                case 6: this->issue_trigger(); break;
                default: this->start_phase(PHASE_WAIT_IRQ); return;
            }
            this->program_step++;
            break;
        }

        case PHASE_WAIT_IRQ:
        {
            if (this->got_irq)
            {
                this->got_irq = false;
                this->start_phase(PHASE_READBACK);
                return;
            }
            // Idle: timeout_handler will fire if IRQ never arrives.
            break;
        }

        case PHASE_READBACK:
        {
            // Zero-size or zero-rep 2D cases: the iDMA acks the transfer
            // immediately without moving any bytes, so there is nothing to
            // verify on the destination.
            if (this->cur_rep >= this->reps || this->length == 0)
            {
                this->pass();
                return;
            }
            uint64_t addr  = this->dst + this->cur_rep * this->dst_stride + this->cur_byte;
            uint64_t logoff = this->cur_rep * this->length + this->cur_byte;
            this->issue_mem_read(addr, logoff);

            this->cur_byte += 4;
            if (this->cur_byte >= this->length)
            {
                this->cur_byte = 0;
                this->cur_rep++;
            }
            break;
        }

        case PHASE_DONE:
            break;
    }

    if (!this->waiting_for_resp && this->phase != PHASE_DONE && this->phase != PHASE_WAIT_IRQ)
        this->schedule_step(1);
}


void IDmaTesterV2::regs_resp(vp::Block *__this, vp::IoReq *req)
{
    IDmaTesterV2 *_this = (IDmaTesterV2 *)__this;
    if (req->get_resp_status() != vp::IO_RESP_OK)
    {
        _this->fail("reg op returned INVALID");
        return;
    }
    _this->waiting_for_resp = false;
    _this->schedule_step(1);
}


void IDmaTesterV2::regs_retry(vp::Block *__this)
{
    IDmaTesterV2 *_this = (IDmaTesterV2 *)__this;
    // Retry the in-flight reg request. Since regs are sync DONE in our setup we
    // shouldn't actually hit this — but if we do, just re-issue.
    if (_this->waiting_for_resp)
    {
        vp::IoReqStatus st = _this->regs_master.req(&_this->reg_req);
        if (st == vp::IO_REQ_DONE)
        {
            _this->waiting_for_resp = false;
            _this->schedule_step(1);
        }
    }
}


void IDmaTesterV2::mem_resp(vp::Block *__this, vp::IoReq *req)
{
    IDmaTesterV2 *_this = (IDmaTesterV2 *)__this;
    if (req->get_resp_status() != vp::IO_RESP_OK)
    {
        _this->fail("mem op returned INVALID");
        return;
    }
    if (_this->phase == PHASE_READBACK)
    {
        if (memcmp(_this->mem_data, _this->mem_expected, 4) != 0)
        {
            _this->fail("readback mismatch logical_off=0x%lx: got %02x%02x%02x%02x expected %02x%02x%02x%02x",
                _this->mem_expected_offset,
                _this->mem_data[0], _this->mem_data[1], _this->mem_data[2], _this->mem_data[3],
                _this->mem_expected[0], _this->mem_expected[1], _this->mem_expected[2], _this->mem_expected[3]);
            return;
        }
    }
    _this->waiting_for_resp = false;
    _this->schedule_step(1);
}


void IDmaTesterV2::mem_retry(vp::Block *__this)
{
    IDmaTesterV2 *_this = (IDmaTesterV2 *)__this;
    if (_this->waiting_for_resp)
    {
        vp::IoReqStatus st = _this->mem_master.req(&_this->mem_req);
        if (st == vp::IO_REQ_DONE)
        {
            _this->waiting_for_resp = false;
            // For a sync DONE on readback path we still need to compare:
            if (_this->phase == PHASE_READBACK
                && _this->mem_req.get_resp_status() == vp::IO_RESP_OK
                && memcmp(_this->mem_data, _this->mem_expected, 4) != 0)
            {
                _this->fail("readback mismatch on retry");
                return;
            }
            _this->schedule_step(1);
        }
    }
}


void IDmaTesterV2::irq_sync(vp::Block *__this, bool value)
{
    IDmaTesterV2 *_this = (IDmaTesterV2 *)__this;
    if (value)
    {
        _this->irq_cycle = _this->clock.get_cycles();
        printf("[%ld] tester IRQ\n", _this->irq_cycle);
        _this->got_irq = true;
        _this->schedule_step(1);
    }
}


void IDmaTesterV2::step_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaTesterV2 *_this = (IDmaTesterV2 *)__this;
    _this->step();
}


void IDmaTesterV2::timeout_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IDmaTesterV2 *_this = (IDmaTesterV2 *)__this;
    _this->fail("timeout after %ld cycles in phase %d (rep=%lu byte=%lu)",
        _this->quit_after_cycles, (int)_this->phase, _this->cur_rep, _this->cur_byte);
}


void IDmaTesterV2::fail(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[%ld] tester FAIL %s\n", this->clock.get_cycles(), buf);
    this->phase = PHASE_DONE;
    this->time.get_engine()->quit(1);
}


void IDmaTesterV2::pass()
{
    int64_t now = this->clock.get_cycles();
    int64_t idma_cycles = this->irq_cycle - this->trigger_cycle;
    uint64_t bytes = this->length * this->reps;
    // bytes_per_1000_cycles avoids float in C++; checker on the Python side
    // can divide by 1000.0 for a friendlier number.
    int64_t bw_x1000 = idma_cycles > 0
        ? (int64_t)((bytes * (uint64_t)1000) / (uint64_t)idma_cycles) : 0;
    printf("[%ld] tester PASS idma_cycles=%ld bytes=%lu bw_x1000=%ld\n",
        now, idma_cycles, bytes, bw_x1000);
    this->phase = PHASE_DONE;
    this->time.get_engine()->quit(0);
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new IDmaTesterV2(config);
}
