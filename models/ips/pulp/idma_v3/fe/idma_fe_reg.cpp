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

#include <vp/vp.hpp>
#include "idma_fe_reg.hpp"
#include "../be/idma_be.hpp"

// Register offsets (see the class documentation)
#define REG_CONF            0x000
#define REG_STREAM_BASE     0x004
#define REG_DST_ADDR        0x0D0
#define REG_SRC_ADDR        0x0D8
#define REG_LENGTH          0x0E0
#define REG_DST_STRIDE_2    0x0E8
#define REG_SRC_STRIDE_2    0x0F0
#define REG_REPS_2          0x0F8
#define REG_DST_STRIDE_3    0x100
#define REG_SRC_STRIDE_3    0x108
#define REG_REPS_3          0x110

#define CONF_DECOUPLE_RW_BIT 0
#define CONF_DECOUPLE_AW_BIT 1
#define CONF_ENABLE_ND_BIT   10
#define CONF_SRC_PROT_BIT    12
#define CONF_DST_PROT_BIT    15

// Identifiers 0 and 1 are never handed out
#define FIRST_ID            2

// STATUS bits: 8 = midend (queue), 7:0 = backend units
#define STATUS_MIDEND_BUSY  (1 << 8)



IdmaFeReg::Stream::Stream(IdmaFeReg *top, int id)
:   Block(top, "stream" + std::to_string(id)),
    top(top),
    id(id),
    next_id(*this, "next_id", 32, true, FIRST_ID),
    done_id(*this, "done_id", 32, true, FIRST_ID - 1),
    trace_busy(*this, "busy", 1, vp::SignalCommon::ResetKind::HighZ),
    trace_id(*this, "id", 32, vp::SignalCommon::ResetKind::HighZ)
{
}



void IdmaFeReg::Stream::me_ready()
{
    this->top->stream_ready(this->id);
}



void IdmaFeReg::Stream::complete_nd(IdmaNdReq *req)
{
    this->top->stream_done(this, req);
}



IdmaFeReg::RegPort::RegPort(IdmaFeReg *top, int id)
:   Block(top, "port" + std::to_string(id)),
    id(id),
    itf(id, &IdmaFeReg::req),
    conf(*this, "conf", 32, true, 0),
    dst(*this, "dst", 32, true, 0),
    src(*this, "src", 32, true, 0),
    length(*this, "length", 32, true, 0),
    dst_stride_2(*this, "dst_stride_2", 32, true, 0),
    src_stride_2(*this, "src_stride_2", 32, true, 0),
    reps_2(*this, "reps_2", 32, true, 0),
    dst_stride_3(*this, "dst_stride_3", 32, true, 0),
    src_stride_3(*this, "src_stride_3", 32, true, 0),
    reps_3(*this, "reps_3", 32, true, 0)
{
}



IdmaFeReg::IdmaFeReg(vp::Component *top, int nb_ports, int nb_streams, int nb_events,
    int launch_bubble)
:   Block(top, "fe"),
    done_event(this, &IdmaFeReg::done_handler),
    launch_bubble(launch_bubble)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    for (int i = 0; i < nb_ports; i++)
    {
        RegPort *port = new RegPort(this, i);
        top->new_slave_port("input_" + std::to_string(i), &port->itf, this);
        this->ports.push_back(port);
    }

    for (int i = 0; i < nb_streams; i++)
    {
        this->streams.push_back(new Stream(this, i));
    }

    for (int i = 0; i < nb_events; i++)
    {
        vp::WireMaster<bool> *itf = new vp::WireMaster<bool>();
        top->new_master_port("event_" + std::to_string(i), itf, this);
        this->event_itf.push_back(itf);
    }

    top->new_master_port("fc_event", &this->fc_event_itf, this);
    top->new_master_port("busy", &this->busy_itf, this);

    this->enable_itf.set_sync_meth(&IdmaFeReg::enable_sync);
    top->new_slave_port("enable", &this->enable_itf, this);
}



void IdmaFeReg::reset(bool active)
{
    if (active)
    {
        // With the clock gate wired, the block starts gated as in the RTL
        this->enabled = !this->enable_itf.is_bound();
        this->busy = false;
        this->rr_port = 0;
        for (RegPort *port: this->ports)
        {
            port->launch_pending = false;
        }
        for (Stream *stream: this->streams)
        {
            stream->pending_done = 0;
        }
    }
}



void IdmaFeReg::set_stream(int stream, IdmaFeSink *me, IdmaBackend *be)
{
    this->streams[stream]->me = me;
    this->streams[stream]->be = be;
}



bool IdmaFeReg::is_busy()
{
    return this->busy;
}



void IdmaFeReg::enable_sync(vp::Block *__this, bool value)
{
    IdmaFeReg *_this = (IdmaFeReg *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Clock gate %s\n", value ? "enabled" : "disabled");
    _this->enabled = value;
}



vp::IoReqStatus IdmaFeReg::req(vp::Block *__this, vp::IoReq *req, int port_id)
{
    IdmaFeReg *_this = (IdmaFeReg *)__this;
    RegPort *port = _this->ports[port_id];
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint32_t *data = (uint32_t *)req->get_data();
    int nb_streams = _this->streams.size();

    if (!_this->enabled)
    {
        // The RTL block is clock gated: the request is granted but its
        // response never comes and the requester hangs. Fail visibly instead.
        _this->trace.force_warning("Access to the DMA while its clock is gated, set the "
            "clock gate first (port: %d, offset: 0x%lx)\n", port_id, offset);
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    if (req->get_size() != 4)
    {
        _this->trace.force_warning("Unsupported access size (port: %d, offset: 0x%lx, "
            "size: %ld)\n", port_id, offset, req->get_size());
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    req->set_resp_status(vp::IO_RESP_OK);

    // Per-stream registers: STATUS, then NEXT_ID, then DONE_ID blocks
    if (offset >= REG_STREAM_BASE && offset < REG_STREAM_BASE + 12 * nb_streams)
    {
        int index = (offset - REG_STREAM_BASE) / 4;
        int kind = index / nb_streams;
        int stream = index % nb_streams;
        Stream *s = _this->streams[stream];

        if (kind == 0)
        {
            if (!is_write)
            {
                uint32_t status = s->be->busy_bits();
                if (s->me->is_busy()) status |= STATUS_MIDEND_BUSY;
                *data = status;
            }
            return vp::IO_REQ_DONE;
        }
        else if (kind == 1)
        {
            if (is_write)
            {
                _this->trace.force_warning("Write to NEXT_ID ignored, a read launches the "
                    "transfer (port: %d, stream: %d)\n", port_id, stream);
                return vp::IO_REQ_DONE;
            }
            return _this->launch(port, stream, data);
        }
        else
        {
            if (!is_write)
            {
                *data = s->done_id.get();
            }
            return vp::IO_REQ_DONE;
        }
    }

    vp::Register<uint32_t> *reg = NULL;
    switch (offset)
    {
        case REG_CONF:         reg = &port->conf; break;
        case REG_DST_ADDR:     reg = &port->dst; break;
        case REG_SRC_ADDR:     reg = &port->src; break;
        case REG_LENGTH:       reg = &port->length; break;
        case REG_DST_STRIDE_2: reg = &port->dst_stride_2; break;
        case REG_SRC_STRIDE_2: reg = &port->src_stride_2; break;
        case REG_REPS_2:       reg = &port->reps_2; break;
        case REG_DST_STRIDE_3: reg = &port->dst_stride_3; break;
        case REG_SRC_STRIDE_3: reg = &port->src_stride_3; break;
        case REG_REPS_3:       reg = &port->reps_3; break;
        default:
            _this->trace.force_warning("Invalid register access (port: %d, offset: 0x%lx)\n",
                port_id, offset);
            req->set_resp_status(vp::IO_RESP_INVALID);
            return vp::IO_REQ_DONE;
    }

    if (is_write)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Register write (port: %d, offset: 0x%lx, "
            "value: 0x%x)\n", port_id, offset, *data);
        reg->set(*data);
    }
    else
    {
        *data = reg->get();
    }

    return vp::IO_REQ_DONE;
}



// A NEXT_ID read: allocate the id, assemble the ND request from the port's
// registers and push it to the stream's mid-end. The request registers are
// per port (each port is a full register file in the RTL), the id counters
// and the streams are shared between the ports.
vp::IoReqStatus IdmaFeReg::launch(RegPort *port, int stream_id, uint32_t *rdata)
{
    Stream *stream = this->streams[stream_id];

    if (!stream->me->can_accept_nd())
    {
        // The RTL withholds the grant of the NEXT_ID read while the stream's
        // request FIFO is full: deny and retry once a slot is free
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stream queue full, back-pressuring launch "
            "(port: %d, stream: %d)\n", port->id, stream_id);
        port->launch_pending = true;
        port->pending_stream = stream_id;
        return vp::IO_REQ_DENIED;
    }

    uint32_t id = stream->next_id.get();
    uint32_t next = id + 1;
    if (next < FIRST_ID) next = FIRST_ID;
    stream->next_id.set(next);

    uint32_t conf = port->conf.get();

    IdmaNdReq *req = new IdmaNdReq();
    req->src = port->src.get();
    req->dst = port->dst.get();
    req->length = port->length.get();
    req->src_stride[0] = port->src_stride_2.get();
    req->dst_stride[0] = port->dst_stride_2.get();
    req->reps[0] = port->reps_2.get();
    req->src_stride[1] = port->src_stride_3.get();
    req->dst_stride[1] = port->dst_stride_3.get();
    req->reps[1] = port->reps_3.get();
    req->nd = (conf >> CONF_ENABLE_ND_BIT) & 0x3;
    req->src_prot = (conf >> CONF_SRC_PROT_BIT) & 0x7;
    req->dst_prot = (conf >> CONF_DST_PROT_BIT) & 0x7;
    req->decouple_rw = (conf >> CONF_DECOUPLE_RW_BIT) & 1;
    req->decouple_aw = (conf >> CONF_DECOUPLE_AW_BIT) & 1;
    req->id = id;
    req->stream = stream_id;

    stream->trace_id.set_and_release(id);
    stream->trace_busy = true;

    this->trace.msg(vp::Trace::LEVEL_INFO, "Launching transfer (port: %d, stream: %d, id: %d, "
        "src: 0x%lx, dst: 0x%lx, length: 0x%lx, nd: %d, src_prot: %d, dst_prot: %d, "
        "src_stride_2: 0x%lx, dst_stride_2: 0x%lx, reps_2: %ld, src_stride_3: 0x%lx, "
        "dst_stride_3: 0x%lx, reps_3: %ld)\n",
        port->id, stream_id, id, req->src, req->dst, req->length, req->nd, req->src_prot,
        req->dst_prot, req->src_stride[0], req->dst_stride[0], req->reps[0], req->src_stride[1],
        req->dst_stride[1], req->reps[1]);

    // The RTL datapath is clock gated while the DMA is idle and only wakes
    // the cycle after a launch: charge that bubble on the first transfer
    int delay = this->launch_bubble > 0 && !this->busy ? this->launch_bubble : 0;
    stream->me->push_nd(req, delay);

    this->update_busy();

    *rdata = id;
    return vp::IO_REQ_DONE;
}



void IdmaFeReg::stream_ready(int stream_id)
{
    Stream *stream = this->streams[stream_id];
    int nb_ports = this->ports.size();

    // Re-issue the launches waiting for this stream, round-robin between the
    // ports as the RTL arbiter does. retry() re-runs the held request
    // synchronously, so req() runs again and this time enqueues it.
    for (int i = 0; i < nb_ports && stream->me->can_accept_nd(); i++)
    {
        RegPort *port = this->ports[this->rr_port];
        this->rr_port = (this->rr_port + 1) % nb_ports;

        if (port->launch_pending && port->pending_stream == stream_id)
        {
            port->launch_pending = false;
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Stream got ready, retrying launch "
                "(port: %d, stream: %d)\n", port->id, stream_id);
            port->itf.retry();
        }
    }
}



// ND completion (the RTL trans_complete): counted and pulsed one cycle later
// by done_event so that DONE_ID and the event are registered, and so that
// the completions of both streams in one cycle give a single pulse.
void IdmaFeReg::stream_done(Stream *stream, IdmaNdReq *req)
{
    this->trace.msg(vp::Trace::LEVEL_INFO, "Transfer done (stream: %d, id: %d)\n",
        stream->id, req->id);

    // DONE_ID and the event are registered: visible next cycle, completions
    // of one cycle merged into one event pulse
    stream->pending_done++;
    this->done_event.enqueue(1);
}



void IdmaFeReg::done_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IdmaFeReg *_this = (IdmaFeReg *)__this;

    for (Stream *stream: _this->streams)
    {
        while (stream->pending_done > 0)
        {
            stream->pending_done--;
            // DONE_ID counts completions, skipping the identifiers a launch
            // never returns; transfers of a stream complete in order so it
            // tracks the id
            uint32_t next = stream->done_id.get() + 1;
            if (next < FIRST_ID) next = FIRST_ID;
            stream->done_id.set(next);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Completion counted (stream: %d, "
                "done_id: %d)\n", stream->id, next);
        }
        if (stream->done_id.get() + 1 == stream->next_id.get())
        {
            stream->trace_busy = false;
        }
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Raising completion event\n");

    for (vp::WireMaster<bool> *itf: _this->event_itf)
    {
        if (itf->is_bound())
        {
            itf->sync(true);
        }
    }

    if (_this->fc_event_itf.is_bound())
    {
        _this->fc_event_itf.sync(true);
    }

    _this->update_busy();
}



void IdmaFeReg::update_busy()
{
    bool busy = false;
    for (Stream *stream: this->streams)
    {
        if (stream->me->is_busy() || stream->be->is_busy())
        {
            busy = true;
        }
    }

    if (busy != this->busy)
    {
        this->busy = busy;
        if (this->busy_itf.is_bound())
        {
            this->busy_itf.sync(busy);
        }
    }
}
