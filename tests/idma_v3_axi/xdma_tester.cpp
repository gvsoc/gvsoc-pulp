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
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

/*
 * Testbench driver for the Snitch xdma-driven iDMA (SnitchDmaV3).
 *
 * Plays the Snitch core: one offload instruction per cycle to program and
 * launch each transfer (dmsrc, dmdst, dmstr, dmrep, dmcpy), replaying a
 * refused dmcpy every cycle until it is granted as the iss_v2 core does,
 * then one dmstat(busy) per cycle until the DMA is idle; irq pulses are
 * counted; finally every destination is read back (8 bytes per access) and
 * checked against the pattern (seed + i) & 0xff. Prints
 *
 *   [cycle] tester PASS idma_cycles=<launch..idle> bytes=<n> events=<irqs> fc_events=<irqs>
 */

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <cpu/iss/include/offload.hpp>

// xdma func7 encodings (opcode custom-3, func7 in bits 31:25)
#define XDMA_OPCODE     0x2b
#define FUNC7_DMSRC     0b0000000
#define FUNC7_DMDST     0b0000001
#define FUNC7_DMCPYI    0b0000010
#define FUNC7_DMCPY     0b0000011
#define FUNC7_DMSTATI   0b0000100
#define FUNC7_DMSTAT    0b0000101
#define FUNC7_DMSTR     0b0000110
#define FUNC7_DMREP     0b0000111

#define DMSTAT_BUSY     2

class XdmaTester : public vp::Component
{
public:
    XdmaTester(vp::ComponentConf &config);

    void reset(bool active) override;

private:
    struct Transfer
    {
        uint64_t src, dst, length;
        uint64_t src_stride, dst_stride, reps;
        int nd;
        int seed;
        bool expect_denied;
        bool decouple_rw;
        bool denied_seen;
        uint32_t id;
    };

    enum Phase { PHASE_PROGRAM, PHASE_WAIT, PHASE_READBACK, PHASE_DONE };

    static void step_handler(vp::Block *__this, vp::ClockEvent *event);
    static void timeout_handler(vp::Block *__this, vp::ClockEvent *event);
    static vp::IoRespAck mem_resp(vp::Block *__this, vp::IoReq *req);
    static void mem_retry(vp::Block *__this, vp::IoRetryChannel channel);
    static void grant_sync(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *grant);
    static void irq_sync(vp::Block *__this, bool value);

    uint32_t offload(int func7, uint32_t arg_a, uint32_t arg_b, bool *granted=nullptr);
    void step();
    bool issue_read(uint64_t addr);
    void after_resp();
    uint64_t dst_addr(Transfer &t, uint64_t index);
    uint64_t total_bytes(Transfer &t);
    void fail(const char *fmt, ...);
    void pass();

    vp::Trace trace;
    vp::ClockEvent step_event;
    vp::ClockEvent timeout_event;
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf;
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> grant_itf;
    vp::WireSlave<bool> irq_itf;
    vp::IoMaster mem_master{&XdmaTester::mem_retry, &XdmaTester::mem_resp};
    vp::IoReq req;
    uint8_t data[8];

    std::vector<Transfer> transfers;
    int64_t quit_after_cycles;

    Phase phase;
    int index;
    int sub_step;
    bool waiting_for_resp;
    bool req_denied;
    bool core_denied;
    uint64_t rb_index;
    int64_t start_cycle;
    int64_t end_cycle;
    int irqs_seen;
    uint64_t bytes_total;
    int errors;
};



XdmaTester::XdmaTester(vp::ComponentConf &config)
:   vp::Component(config),
    step_event(this, &XdmaTester::step_handler),
    timeout_event(this, &XdmaTester::timeout_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_master_port("offload", &this->offload_itf);
    this->grant_itf.set_sync_meth(&XdmaTester::grant_sync);
    this->new_slave_port("offload_grant", &this->grant_itf);
    this->irq_itf.set_sync_meth(&XdmaTester::irq_sync);
    this->new_slave_port("irq", &this->irq_itf);
    this->new_master_port("mem", &this->mem_master);

    js::Config *cfg = this->get_js_config();
    this->quit_after_cycles = cfg->get_child_int("quit_after_cycles");
    if (this->quit_after_cycles <= 0) this->quit_after_cycles = 1000000;

    for (js::Config *elem: cfg->get("transfers")->get_elems())
    {
        Transfer t;
        t.src = elem->get_child_int("src");
        t.dst = elem->get_child_int("dst");
        t.length = elem->get_child_int("length");
        t.src_stride = elem->get_child_int("src_stride");
        t.dst_stride = elem->get_child_int("dst_stride");
        t.reps = elem->get_child_int("reps");
        t.nd = elem->get_child_int("nd");
        t.seed = elem->get_child_int("seed");
        t.expect_denied = elem->get_child_int("expect_denied") != 0;
        t.decouple_rw = elem->get("decouple_rw") != NULL && elem->get_child_int("decouple_rw") != 0;
        t.denied_seen = false;
        t.id = 0;
        this->transfers.push_back(t);
    }
}



void XdmaTester::reset(bool active)
{
    if (!active)
    {
        this->phase = PHASE_PROGRAM;
        this->index = 0;
        this->sub_step = 0;
        this->waiting_for_resp = false;
        this->req_denied = false;
        this->core_denied = false;
        this->rb_index = 0;
        this->start_cycle = -1;
        this->end_cycle = -1;
        this->irqs_seen = 0;
        this->bytes_total = 0;
        this->errors = 0;
        printf("[%ld] tester START transfers=%zu\n", this->clock.get_cycles(),
            this->transfers.size());
        this->step_event.enqueue(1);
        this->timeout_event.enqueue(this->quit_after_cycles);
    }
}



uint64_t XdmaTester::total_bytes(Transfer &t)
{
    uint64_t reps = t.nd >= 1 && t.reps != 0 ? t.reps : 1;
    return t.length * reps;
}



uint64_t XdmaTester::dst_addr(Transfer &t, uint64_t index)
{
    uint64_t line = index / t.length;
    uint64_t byte = index % t.length;
    return t.dst + line * t.dst_stride + byte;
}



uint32_t XdmaTester::offload(int func7, uint32_t arg_a, uint32_t arg_b, bool *granted)
{
    IssOffloadInsn<uint32_t> insn = {
        .opcode = (uint64_t)((func7 << 25) | XDMA_OPCODE),
        .arg_a = arg_a,
        .arg_b = arg_b,
    };
    this->offload_itf.sync(&insn);
    if (granted != nullptr)
    {
        // iss_v2 semantics: the instruction only completes once no grant
        // is pending and the front-end granted it
        *granted = !this->core_denied && insn.granted;
        if (!*granted)
        {
            this->core_denied = true;
        }
    }
    return insn.result;
}



void XdmaTester::grant_sync(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *grant)
{
    XdmaTester *_this = (XdmaTester *)__this;
    _this->core_denied = false;
}



void XdmaTester::irq_sync(vp::Block *__this, bool value)
{
    XdmaTester *_this = (XdmaTester *)__this;
    if (value)
    {
        _this->irqs_seen++;
    }
}



void XdmaTester::step_handler(vp::Block *__this, vp::ClockEvent *event)
{
    XdmaTester *_this = (XdmaTester *)__this;
    _this->step();
    if (_this->phase != PHASE_DONE)
    {
        _this->step_event.enqueue(1);
    }
}



void XdmaTester::timeout_handler(vp::Block *__this, vp::ClockEvent *event)
{
    XdmaTester *_this = (XdmaTester *)__this;
    if (_this->phase != PHASE_DONE)
    {
        _this->fail("timeout (phase: %d, transfer: %d)", _this->phase, _this->index);
    }
}



void XdmaTester::step()
{
    if (this->waiting_for_resp) return;

    switch (this->phase)
    {
        case PHASE_PROGRAM:
        {
            if (this->index >= (int)this->transfers.size())
            {
                this->phase = PHASE_WAIT;
                return;
            }
            Transfer &t = this->transfers[this->index];
            switch (this->sub_step)
            {
                case 0: this->offload(FUNC7_DMSRC, t.src, t.src >> 32); break;
                case 1: this->offload(FUNC7_DMDST, t.dst, t.dst >> 32); break;
                case 2: this->offload(FUNC7_DMSTR, t.src_stride, t.dst_stride); break;
                case 3: this->offload(FUNC7_DMREP, t.reps, 0); break;
                case 4:
                {
                    bool granted;
                    uint32_t config = (t.nd ? 2 : 0) | (t.decouple_rw ? 1 : 0);
                    uint32_t id = this->offload(FUNC7_DMCPY, t.length, config, &granted);
                    if (!granted)
                    {
                        // The core stalls and replays the instruction
                        t.denied_seen = true;
                        return;
                    }
                    t.id = id;
                    if (this->start_cycle < 0) this->start_cycle = this->clock.get_cycles();
                    printf("[%ld] tester launched %d id=%u\n", this->clock.get_cycles(),
                        this->index, id);
                    if (t.expect_denied && !t.denied_seen)
                    {
                        this->fail("transfer %d was expected to be denied", this->index);
                        return;
                    }
                    this->bytes_total += this->total_bytes(t);
                    this->index++;
                    this->sub_step = -1;
                    break;
                }
            }
            this->sub_step++;
            return;
        }

        case PHASE_WAIT:
        {
            if (this->offload(FUNC7_DMSTAT, 0, DMSTAT_BUSY) == 0)
            {
                this->end_cycle = this->clock.get_cycles();
                printf("[%ld] tester idle\n", this->end_cycle);
                this->phase = PHASE_READBACK;
                this->index = 0;
                this->rb_index = 0;
            }
            return;
        }

        case PHASE_READBACK:
        {
            while (this->index < (int)this->transfers.size()
                && this->rb_index >= this->total_bytes(this->transfers[this->index]))
            {
                this->index++;
                this->rb_index = 0;
            }
            if (this->index >= (int)this->transfers.size())
            {
                if (this->errors == 0 && this->irqs_seen != (int)this->transfers.size())
                {
                    this->fail("expected %zu irq pulses, got %d", this->transfers.size(),
                        this->irqs_seen);
                    return;
                }
                if (this->errors == 0)
                {
                    this->pass();
                }
                else
                {
                    this->fail("%d byte mismatches", this->errors);
                }
                return;
            }
            Transfer &t = this->transfers[this->index];
            if (this->issue_read(this->dst_addr(t, this->rb_index)))
            {
                this->after_resp();
            }
            return;
        }

        case PHASE_DONE:
            return;
    }
}



bool XdmaTester::issue_read(uint64_t addr)
{
    this->req.prepare();
    this->req.set_addr(addr);
    this->req.set_size(8);
    this->req.set_is_write(false);
    this->req.set_data(this->data);
    this->req.is_first = true;
    this->req.is_last = true;
    this->req.burst_id = -1;
    this->req.set_resp_status(vp::IO_RESP_OK);
    this->waiting_for_resp = true;
    this->req_denied = false;

    vp::IoReqStatus status = this->mem_master.req(&this->req);
    if (status == vp::IO_REQ_DONE)
    {
        this->waiting_for_resp = false;
        return true;
    }
    if (status == vp::IO_REQ_DENIED)
    {
        this->req_denied = true;
    }
    return false;
}



void XdmaTester::after_resp()
{
    Transfer &t = this->transfers[this->index];
    uint64_t total = this->total_bytes(t);
    for (int i = 0; i < 8 && this->rb_index + i < total; i++)
    {
        uint8_t expected = (t.seed + this->rb_index + i) & 0xff;
        if (this->data[i] != expected)
        {
            if (this->errors < 5)
            {
                printf("[%ld] tester MISMATCH transfer=%d addr=0x%lx got=0x%02x expected=0x%02x\n",
                    this->clock.get_cycles(), this->index,
                    this->dst_addr(t, this->rb_index + i), this->data[i], expected);
            }
            this->errors++;
        }
    }
    this->rb_index += 8;
}



vp::IoRespAck XdmaTester::mem_resp(vp::Block *__this, vp::IoReq *req)
{
    XdmaTester *_this = (XdmaTester *)__this;
    _this->waiting_for_resp = false;
    _this->after_resp();
    return vp::IO_RESP_ACCEPTED;
}



void XdmaTester::mem_retry(vp::Block *__this, vp::IoRetryChannel)
{
    XdmaTester *_this = (XdmaTester *)__this;
    if (!_this->waiting_for_resp || !_this->req_denied) return;

    vp::IoReqStatus status = _this->mem_master.req(&_this->req);
    if (status == vp::IO_REQ_DONE)
    {
        _this->waiting_for_resp = false;
        _this->req_denied = false;
        _this->after_resp();
    }
}



void XdmaTester::fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[%ld] tester FAIL ", this->clock.get_cycles());
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    this->phase = PHASE_DONE;
    this->time.get_engine()->quit(1);
}



void XdmaTester::pass()
{
    printf("[%ld] tester PASS idma_cycles=%ld bytes=%lu events=%d fc_events=%d\n",
        this->clock.get_cycles(), this->end_cycle - this->start_cycle, this->bytes_total,
        this->irqs_seen, this->irqs_seen);
    this->phase = PHASE_DONE;
    this->time.get_engine()->quit(0);
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new XdmaTester(config);
}
