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
 * Testbench driver for the two-stream cluster iDMA (ClusterDmaV2).
 *
 * The case is a list of transfers. Each is programmed through the register
 * port and launched by reading NEXT_ID of its stream; the returned id is
 * checked against the expected sequence (2, 3, ...) per stream, and a launch
 * flagged expect_denied must come back denied at least once. Once all are
 * launched the tester polls DONE_ID of each stream until it reaches the last
 * id, then reads every destination back against the pattern and reports
 *
 *   tester PASS idma_cycles=<last completion - first launch> bytes=<total>
 *          events=<pulses seen on event 0>
 *
 * Every transfer's byte i is (seed + i) & 0xff, in transfer order (lines,
 * then pages).
 */

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#define REG_CONF        0x000
// Per-stream registers: STATUS block, NEXT_ID block, DONE_ID block
#define REG_STATUS(s)   (0x004 + 4 * (s))
#define REG_NEXT_ID(s)  (0x004 + 4 * (this->nb_streams + (s)))
#define REG_DONE_ID(s)  (0x004 + 4 * (2 * this->nb_streams + (s)))
#define REG_DST         0x0D0
#define REG_SRC         0x0D8
#define REG_LENGTH      0x0E0
#define REG_DST_STRIDE2 0x0E8
#define REG_SRC_STRIDE2 0x0F0
#define REG_REPS2       0x0F8
#define REG_DST_STRIDE3 0x100
#define REG_SRC_STRIDE3 0x108
#define REG_REPS3       0x110

#define NB_STREAMS 2


class ClusterDmaTester : public vp::Component
{
public:
    ClusterDmaTester(vp::ComponentConf &conf);

    void reset(bool active) override;

private:
    struct Transfer
    {
        int stream;
        int src_prot;
        int dst_prot;
        uint64_t src;
        uint64_t dst;
        uint64_t length;
        uint64_t src_stride;
        uint64_t dst_stride;
        uint64_t reps;
        int nd;
        uint64_t src_stride_3;
        uint64_t dst_stride_3;
        uint64_t reps_3;
        uint32_t seed;
        bool expect_denied;
        bool decouple_rw;
        bool decouple_aw;
        uint32_t id;
        bool was_denied;
    };

    enum Phase
    {
        PHASE_GATE,
        PHASE_PROGRAM,
        PHASE_WAIT,
        PHASE_READBACK,
        PHASE_DONE,
    };

    static vp::IoRespAck mem_resp(vp::Block *__this, vp::IoReq *req);
    static void mem_retry(vp::Block *__this, vp::IoRetryChannel);
    static void event_sync(vp::Block *__this, bool value, int id);
    static void fc_event_sync(vp::Block *__this, bool value);
    static void step_handler(vp::Block *__this, vp::ClockEvent *event);
    static void timeout_handler(vp::Block *__this, vp::ClockEvent *event);

    void step();
    void start_phase(Phase phase);
    // Issue one 4-byte access; returns true when it completed inline
    bool issue(uint64_t addr, bool is_write, uint32_t value);
    void after_resp();
    void fail(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void pass();

    // Number of bytes of a transfer, and the destination address and pattern
    // offset of its i-th byte
    uint64_t transfer_bytes(Transfer &t);
    uint64_t dst_addr(Transfer &t, uint64_t index);
    uint8_t pattern(Transfer &t, uint64_t index) { return (t.seed + index) & 0xff; }

    vp::IoMaster mem_master{&ClusterDmaTester::mem_retry, &ClusterDmaTester::mem_resp};
    std::vector<vp::WireSlave<bool> *> event_slaves;
    vp::WireSlave<bool> fc_event_slave;
    vp::WireMaster<bool> enable_master;
    vp::ClockEvent step_event;
    vp::ClockEvent timeout_event;
    vp::Trace trace;

    uint64_t regs_addr;
    std::vector<Transfer> transfers;
    int nb_events;
    int nb_streams;
    bool enable_gate;
    int64_t quit_after_cycles;

    Phase phase;
    int index;          // transfer being programmed / read back
    int sub_step;       // register within the program sequence, byte within the readback
    uint64_t rb_index;  // byte index within the transfer being read back
    bool waiting_for_resp;
    bool inline_done;
    uint8_t data[4];
    vp::IoReq req;
    bool req_denied;
    uint32_t last_id[NB_STREAMS];
    bool stream_used[NB_STREAMS];
    int events_seen;
    int fc_events_seen;
    int64_t first_launch_cycle;
    int64_t last_done_cycle;
    uint64_t total_bytes;
    bool gate_checked;
};



ClusterDmaTester::ClusterDmaTester(vp::ComponentConf &config)
:   vp::Component(config),
    step_event(this, &ClusterDmaTester::step_handler),
    timeout_event(this, &ClusterDmaTester::timeout_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_master_port("mem", &this->mem_master);
    this->new_master_port("enable", &this->enable_master);

    js::Config *cfg = this->get_js_config();
    this->regs_addr = cfg->get_child_int("regs_addr");
    this->nb_events = cfg->get_child_int("nb_events");
    this->nb_streams = cfg->get_child_int("nb_streams");
    if (this->nb_streams <= 0) this->nb_streams = 2;
    this->enable_gate = cfg->get_child_int("enable_gate") != 0;
    this->quit_after_cycles = cfg->get_child_int("quit_after_cycles");
    if (this->quit_after_cycles <= 0) this->quit_after_cycles = 1000000;

    for (js::Config *elem: cfg->get("transfers")->get_elems())
    {
        Transfer t;
        t.stream = elem->get_child_int("stream");
        t.src_prot = elem->get_child_int("src_prot");
        t.dst_prot = elem->get_child_int("dst_prot");
        t.src = elem->get_child_int("src");
        t.dst = elem->get_child_int("dst");
        t.length = elem->get_child_int("length");
        t.src_stride = elem->get_child_int("src_stride");
        t.dst_stride = elem->get_child_int("dst_stride");
        t.reps = elem->get_child_int("reps");
        t.nd = elem->get_child_int("nd");
        t.src_stride_3 = elem->get_child_int("src_stride_3");
        t.dst_stride_3 = elem->get_child_int("dst_stride_3");
        t.reps_3 = elem->get_child_int("reps_3");
        t.seed = elem->get_child_int("seed");
        t.expect_denied = elem->get_child_int("expect_denied") != 0;
        t.decouple_rw = elem->get("decouple_rw") != NULL && elem->get_child_int("decouple_rw") != 0;
        t.decouple_aw = elem->get("decouple_aw") != NULL && elem->get_child_int("decouple_aw") != 0;
        t.id = 0;
        t.was_denied = false;
        this->transfers.push_back(t);
    }

    for (int i = 0; i < this->nb_events; i++)
    {
        vp::WireSlave<bool> *slave = new vp::WireSlave<bool>();
        slave->set_sync_meth_muxed(&ClusterDmaTester::event_sync, i);
        this->new_slave_port("event_" + std::to_string(i), slave);
        this->event_slaves.push_back(slave);
    }

    this->fc_event_slave.set_sync_meth(&ClusterDmaTester::fc_event_sync);
    this->new_slave_port("fc_event", &this->fc_event_slave);

    this->req.set_data(this->data);
    this->req.set_size(4);
}



void ClusterDmaTester::reset(bool active)
{
    if (!active)
    {
        this->waiting_for_resp = false;
        this->events_seen = 0;
        this->fc_events_seen = 0;
        this->first_launch_cycle = -1;
        this->last_done_cycle = 0;
        this->total_bytes = 0;
        this->gate_checked = false;
        for (int s = 0; s < NB_STREAMS; s++)
        {
            this->last_id[s] = 0;
            this->stream_used[s] = false;
        }
        printf("[%ld] tester START transfers=%zu\n", this->clock.get_cycles(),
            this->transfers.size());
        this->start_phase(this->enable_gate ? PHASE_GATE : PHASE_PROGRAM);
        this->timeout_event.enqueue(this->quit_after_cycles);
    }
}



void ClusterDmaTester::start_phase(Phase phase)
{
    this->phase = phase;
    this->index = 0;
    this->sub_step = 0;
    this->rb_index = 0;
    printf("[%ld] tester PHASE %d\n", this->clock.get_cycles(), (int)phase);
    this->step_event.enqueue(1);
}



uint64_t ClusterDmaTester::transfer_bytes(Transfer &t)
{
    uint64_t reps = t.nd >= 1 && t.reps != 0 ? t.reps : 1;
    uint64_t reps_3 = t.nd >= 2 && t.reps_3 != 0 ? t.reps_3 : 1;
    return t.length * reps * reps_3;
}



uint64_t ClusterDmaTester::dst_addr(Transfer &t, uint64_t index)
{
    uint64_t reps = t.nd >= 1 && t.reps != 0 ? t.reps : 1;
    uint64_t line = index / t.length;
    uint64_t byte = index % t.length;
    uint64_t j = line % reps;
    uint64_t k = line / reps;
    // Incremental address generation of the ND middle-end: stride_2 per
    // line, stride_3 instead on the wrap of the second dimension
    return t.dst + k * (t.dst_stride_3 + (reps - 1) * t.dst_stride) + j * t.dst_stride + byte;
}



bool ClusterDmaTester::issue(uint64_t addr, bool is_write, uint32_t value)
{
    *(uint32_t *)this->data = value;
    this->req.prepare();
    this->req.set_addr(addr);
    this->req.set_size(4);
    this->req.set_is_write(is_write);
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



void ClusterDmaTester::step()
{
    if (this->waiting_for_resp) return;

    switch (this->phase)
    {
        case PHASE_GATE:
        {
            // First access while gated must be refused; then open the gate
            if (this->sub_step == 0)
            {
                this->sub_step++;
                this->issue(this->regs_addr + REG_LENGTH, false, 0);
                if (this->req.get_resp_status() != vp::IO_RESP_INVALID)
                {
                    this->fail("access accepted while the clock gate is off");
                    return;
                }
                printf("[%ld] tester gated access refused as expected\n", this->clock.get_cycles());
                this->enable_master.sync(true);
                this->start_phase(PHASE_PROGRAM);
                return;
            }
            break;
        }

        case PHASE_PROGRAM:
        {
            if (this->index >= (int)this->transfers.size())
            {
                this->start_phase(PHASE_WAIT);
                return;
            }

            Transfer &t = this->transfers[this->index];
            uint64_t base = this->regs_addr;
            uint32_t conf = (t.nd << 10) | (t.src_prot << 12) | (t.dst_prot << 15)
                | (t.decouple_rw ? 1 : 0) | (t.decouple_aw ? 2 : 0);
            bool done = true;

            switch (this->sub_step)
            {
                case 0: done = this->issue(base + REG_CONF, true, conf); break;
                case 1: done = this->issue(base + REG_SRC, true, t.src); break;
                case 2: done = this->issue(base + REG_DST, true, t.dst); break;
                case 3: done = this->issue(base + REG_LENGTH, true, t.length); break;
                case 4: done = this->issue(base + REG_SRC_STRIDE2, true, t.src_stride); break;
                case 5: done = this->issue(base + REG_DST_STRIDE2, true, t.dst_stride); break;
                case 6: done = this->issue(base + REG_REPS2, true, t.reps); break;
                case 7: done = this->issue(base + REG_SRC_STRIDE3, true, t.src_stride_3); break;
                case 8: done = this->issue(base + REG_DST_STRIDE3, true, t.dst_stride_3); break;
                case 9: done = this->issue(base + REG_REPS3, true, t.reps_3); break;
                case 10:
                {
                    if (this->first_launch_cycle < 0)
                    {
                        this->first_launch_cycle = this->clock.get_cycles();
                    }
                    done = this->issue(base + REG_NEXT_ID(t.stream), false, 0);
                    if (this->req_denied)
                    {
                        t.was_denied = true;
                        printf("[%ld] tester launch %d denied\n", this->clock.get_cycles(),
                            this->index);
                    }
                    break;
                }
                default: break;
            }

            if (!done)
            {
                // Completion (or retry) comes through the response path,
                // which re-schedules the step; sub_step advances there
                return;
            }

            this->after_resp();
            return;
        }

        case PHASE_WAIT:
        {
            // Poll DONE_ID of the used streams, one read per step
            int s = this->sub_step % NB_STREAMS;
            this->sub_step++;
            if (!this->stream_used[s])
            {
                this->step_event.enqueue(1);
                return;
            }
            bool done = this->issue(this->regs_addr + REG_DONE_ID(s), false, 0);
            if (!done) return;
            this->after_resp();
            return;
        }

        case PHASE_READBACK:
        {
            while (this->index < (int)this->transfers.size()
                && this->rb_index >= this->transfer_bytes(this->transfers[this->index]))
            {
                this->index++;
                this->rb_index = 0;
            }
            if (this->index >= (int)this->transfers.size())
            {
                this->pass();
                return;
            }
            Transfer &t = this->transfers[this->index];
            bool done = this->issue(this->dst_addr(t, this->rb_index), false, 0);
            if (!done) return;
            this->after_resp();
            return;
        }

        case PHASE_DONE:
            break;
    }
}



// Called once the current access has completed, inline or through resp()
void ClusterDmaTester::after_resp()
{
    switch (this->phase)
    {
        case PHASE_PROGRAM:
        {
            Transfer &t = this->transfers[this->index];
            if (this->req.get_resp_status() != vp::IO_RESP_OK)
            {
                this->fail("register access refused (transfer: %d, step: %d)", this->index,
                    this->sub_step);
                return;
            }
            if (this->sub_step == 10)
            {
                t.id = *(uint32_t *)this->data;
                uint32_t expected = this->last_id[t.stream] == 0 ? 2 : this->last_id[t.stream] + 1;
                printf("[%ld] tester launched %d on stream %d id=%u%s\n",
                    this->clock.get_cycles(), this->index, t.stream, t.id,
                    t.was_denied ? " (after deny)" : "");
                if (t.id != expected)
                {
                    this->fail("unexpected id (transfer: %d, got: %u, expected: %u)",
                        this->index, t.id, expected);
                    return;
                }
                if (t.expect_denied && !t.was_denied)
                {
                    this->fail("launch %d was expected to be denied", this->index);
                    return;
                }
                this->last_id[t.stream] = t.id;
                this->stream_used[t.stream] = true;
                this->total_bytes += this->transfer_bytes(t);
                this->index++;
                this->sub_step = 0;
            }
            else
            {
                this->sub_step++;
            }
            this->step_event.enqueue(1);
            break;
        }

        case PHASE_WAIT:
        {
            int s = (this->sub_step - 1) % NB_STREAMS;
            uint32_t done_id = *(uint32_t *)this->data;
            if ((int32_t)(done_id - this->last_id[s]) >= 0)
            {
                this->stream_used[s] = false;
                this->last_done_cycle = this->clock.get_cycles();
                printf("[%ld] tester stream %d done (done_id=%u)\n", this->clock.get_cycles(),
                    s, done_id);
            }
            bool all_done = true;
            for (int i = 0; i < NB_STREAMS; i++)
            {
                if (this->stream_used[i]) all_done = false;
            }
            if (all_done)
            {
                this->start_phase(PHASE_READBACK);
            }
            else
            {
                this->step_event.enqueue(1);
            }
            break;
        }

        case PHASE_READBACK:
        {
            Transfer &t = this->transfers[this->index];
            uint64_t index = this->rb_index;
            uint64_t bytes = this->transfer_bytes(t);
            for (int i = 0; i < 4 && index + i < bytes; i++)
            {
                uint8_t expected = this->pattern(t, index + i);
                if (this->data[i] != expected)
                {
                    this->fail("mismatch (transfer: %d, byte: %lu, addr: 0x%lx, got: 0x%02x, "
                        "expected: 0x%02x)", this->index, index + i,
                        this->dst_addr(t, index + i), this->data[i], expected);
                    return;
                }
            }
            this->rb_index += 4;
            this->step_event.enqueue(1);
            break;
        }

        default:
            break;
    }
}



vp::IoRespAck ClusterDmaTester::mem_resp(vp::Block *__this, vp::IoReq *req)
{
    ClusterDmaTester *_this = (ClusterDmaTester *)__this;
    _this->waiting_for_resp = false;
    _this->after_resp();
    return vp::IO_RESP_ACCEPTED;
}



void ClusterDmaTester::mem_retry(vp::Block *__this, vp::IoRetryChannel)
{
    ClusterDmaTester *_this = (ClusterDmaTester *)__this;
    if (!_this->waiting_for_resp || !_this->req_denied) return;

    vp::IoReqStatus status = _this->mem_master.req(&_this->req);
    if (status == vp::IO_REQ_DONE)
    {
        _this->waiting_for_resp = false;
        _this->req_denied = false;
        _this->after_resp();
    }
}



void ClusterDmaTester::event_sync(vp::Block *__this, bool value, int id)
{
    ClusterDmaTester *_this = (ClusterDmaTester *)__this;
    if (value && id == 0)
    {
        _this->events_seen++;
    }
}



void ClusterDmaTester::fc_event_sync(vp::Block *__this, bool value)
{
    ClusterDmaTester *_this = (ClusterDmaTester *)__this;
    if (value)
    {
        _this->fc_events_seen++;
    }
}



void ClusterDmaTester::step_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ((ClusterDmaTester *)__this)->step();
}



void ClusterDmaTester::timeout_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ClusterDmaTester *_this = (ClusterDmaTester *)__this;
    if (_this->phase != PHASE_DONE)
    {
        _this->fail("timeout (phase: %d, transfer: %d)", (int)_this->phase, _this->index);
    }
}



void ClusterDmaTester::fail(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[%ld] tester FAIL %s\n", this->clock.get_cycles(), buf);
    this->phase = PHASE_DONE;
    this->time.get_engine()->quit(1);
}



void ClusterDmaTester::pass()
{
    int64_t cycles = this->last_done_cycle - this->first_launch_cycle;
    printf("[%ld] tester PASS idma_cycles=%ld bytes=%lu events=%d fc_events=%d\n",
        this->clock.get_cycles(), cycles, this->total_bytes, this->events_seen,
        this->fc_events_seen);
    this->phase = PHASE_DONE;
    this->time.get_engine()->quit(0);
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterDmaTester(config);
}
