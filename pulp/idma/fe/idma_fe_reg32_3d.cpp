/*
 * Copyright (C) 2026 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include "idma_fe_reg32_3d.hpp"
#include "vp/itf/io.hpp"


IDmaFeReg32_3d::Stream::Stream(vp::Block &parent, int id)
    : next_id(parent, "stream_" + std::to_string(id) + "_next_id", 32, true, 1),
    done_id(parent, "stream_" + std::to_string(id) + "_done_id", 32, true, 0)
{
    this->pending = 0;
}



IDmaFeReg32_3dPort::IDmaFeReg32_3dPort(IDmaFeReg32_3d *fe, vp::Component *idma, std::string name,
    int id)
    : Block(fe, name),
    conf(*this, "conf", 32),
    src(*this, "src", 32),
    dst(*this, "dst", 32),
    length(*this, "length", 32),
    src_stride_2(*this, "src_stride_2", 32),
    dst_stride_2(*this, "dst_stride_2", 32),
    reps_2(*this, "reps_2", 32),
    src_stride_3(*this, "src_stride_3", 32),
    dst_stride_3(*this, "dst_stride_3", 32),
    reps_3(*this, "reps_3", 32)
{
    this->fe = fe;
    this->id = id;

    // Each port is mapped at its own address, so it gets its own slave interface. Passing this
    // port as the block of the interface is what lets the request handler know which register
    // file is being accessed.
    this->input_itf.set_req_meth(&IDmaFeReg32_3dPort::req);
    idma->new_slave_port(name, &this->input_itf, this);
}



vp::IoReqStatus IDmaFeReg32_3dPort::req(vp::Block *__this, vp::IoReq *req)
{
    IDmaFeReg32_3dPort *_this = (IDmaFeReg32_3dPort *)__this;
    IDmaFeReg32_3d *fe = _this->fe;

    if (req->get_size() != 4)
    {
        return vp::IO_REQ_INVALID;
    }

    uint64_t offset = req->get_addr();
    uint32_t value = *(uint32_t *)req->get_data();

    // Reading NEXT_ID of a stream launches the transfer currently programmed on this port. The
    // stream is derived from the offset since the identifier registers are a multireg.
    for (int stream = 0; stream < fe->nb_streams; stream++)
    {
        if (offset == (uint64_t)IDMA_REG32_3D_NEXT_ID(stream))
        {
            *(uint32_t *)req->get_data() = fe->enqueue_copy(_this, stream);
            return vp::IO_REQ_OK;
        }

        if (offset == (uint64_t)IDMA_REG32_3D_DONE_ID(stream))
        {
            *(uint32_t *)req->get_data() = fe->streams[stream]->done_id.get();
            return vp::IO_REQ_OK;
        }

        if (offset == (uint64_t)IDMA_REG32_3D_STATUS(stream))
        {
            *(uint32_t *)req->get_data() = fe->streams[stream]->pending > 0;
            return vp::IO_REQ_OK;
        }
    }

    switch (offset)
    {
        case IDMA_REG32_3D_CONF:
            _this->conf.set(value);
            break;
        case IDMA_REG32_3D_DST_ADDR:
            _this->dst.set(value);
            break;
        case IDMA_REG32_3D_SRC_ADDR:
            _this->src.set(value);
            break;
        case IDMA_REG32_3D_LENGTH:
            _this->length.set(value);
            break;
        case IDMA_REG32_3D_DST_STRIDE_2:
            _this->dst_stride_2.set(value);
            break;
        case IDMA_REG32_3D_SRC_STRIDE_2:
            _this->src_stride_2.set(value);
            break;
        case IDMA_REG32_3D_REPS_2:
            _this->reps_2.set(value);
            break;
        case IDMA_REG32_3D_DST_STRIDE_3:
            _this->dst_stride_3.set(value);
            break;
        case IDMA_REG32_3D_SRC_STRIDE_3:
            _this->src_stride_3.set(value);
            break;
        case IDMA_REG32_3D_REPS_3:
            _this->reps_3.set(value);
            break;
        default:
            fe->trace.force_warning("Access to invalid register (port: %d, offset: 0x%lx)\n",
                _this->id, offset);
            return vp::IO_REQ_INVALID;
    }

    return vp::IO_REQ_OK;
}



IDmaFeReg32_3d::IDmaFeReg32_3d(vp::Component *idma, IdmaTransferConsumer *me)
    : Block(idma, "fe"),
    trace_busy(*this, "busy", 1, vp::SignalCommon::ResetKind::HighZ),
    trace_src(*this, "src", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_dst(*this, "dst", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_length(*this, "length", 32, vp::SignalCommon::ResetKind::HighZ),
    trace_id(*this, "id", 32, vp::SignalCommon::ResetKind::HighZ)
{
    // Middle-end will be used later for interaction
    this->me = me;

    // Declare our own trace so that we can individually activate traces
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    js::Config *config = idma->get_js_config();
    this->nb_ports = config->get_int("nb_ports");
    this->nb_streams = config->get_int("nb_streams");
    this->nb_cores = config->get_int("nb_cores");
    this->global_queue_depth = config->get_int("global_queue_depth");

    if (this->nb_streams > IDMA_REG32_3D_MULTIREG_COUNT)
    {
        this->trace.force_warning("Too many streams for the generated register map "
            "(nb_streams: %d, max: %d)\n", this->nb_streams, IDMA_REG32_3D_MULTIREG_COUNT);
    }

    // One independent register file per requester
    for (int i = 0; i < this->nb_ports; i++)
    {
        this->ports.push_back(
            new IDmaFeReg32_3dPort(this, idma, "ctrl_" + std::to_string(i), i));
    }

    // Identifier counters are per stream and shared by every port
    for (int i = 0; i < this->nb_streams; i++)
    {
        this->streams.push_back(new Stream(*this, i));
    }

    // Completion is broadcast, so one event port per core and per PE port
    for (int i = 0; i < this->nb_cores; i++)
    {
        vp::WireMaster<bool> *itf = new vp::WireMaster<bool>();
        idma->new_master_port("event_" + std::to_string(i), itf, this);
        this->event_itf.push_back(itf);
    }

    for (int i = 0; i < this->nb_ports - this->nb_cores; i++)
    {
        vp::WireMaster<bool> *itf = new vp::WireMaster<bool>();
        idma->new_master_port("event_pe_" + std::to_string(i), itf, this);
        this->event_pe_itf.push_back(itf);
    }
}



uint32_t IDmaFeReg32_3d::enqueue_copy(IDmaFeReg32_3dPort *port, int stream)
{
    // The hardware returns 0 when it cannot take the transfer. Refuse here rather than dropping
    // it silently, so that software polling on the returned identifier behaves as on hardware.
    if (this->pending_queue.size() >= (size_t)this->global_queue_depth)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Rejecting transfer, queue is full (port: %d, stream: %d)\n", port->id, stream);
        return 0;
    }

    uint32_t conf = port->conf.get();
    uint32_t enable_nd =
        (conf >> IDMA_REG32_3D_CONF_ENABLE_ND_BIT) & IDMA_REG32_3D_CONF_ENABLE_ND_MASK;

    // Allocate transfer ID
    uint32_t transfer_id = this->streams[stream]->next_id.get();
    this->streams[stream]->next_id.set(transfer_id + 1);

    // Allocate a new transfer and fill it from the registers of this port
    IdmaTransfer *transfer = new IdmaTransfer();
    transfer->src = port->src.get();
    transfer->dst = port->dst.get();
    transfer->size = port->length.get();
    transfer->src_stride = port->src_stride_2.get();
    transfer->dst_stride = port->dst_stride_2.get();
    transfer->reps = port->reps_2.get();
    transfer->config = (enable_nd >= 1 ? IDMA_CONFIG_2D : 0)
        | (enable_nd >= 2 ? IDMA_CONFIG_3D : 0);

    transfer->data.resize(IDMA_ND_DATA_SIZE);
    transfer->data[IDMA_ND_SRC_STRIDE_3] = port->src_stride_3.get();
    transfer->data[IDMA_ND_DST_STRIDE_3] = port->dst_stride_3.get();
    transfer->data[IDMA_ND_REPS_3] = port->reps_3.get();
    transfer->data[IDMA_ND_STREAM] = stream;

    this->trace_src.set_and_release(transfer->src);
    this->trace_dst.set_and_release(transfer->dst);
    this->trace_length.set_and_release(transfer->size);
    this->trace_id.set_and_release(transfer_id);
    this->trace_busy = true;

    this->trace.msg(vp::Trace::LEVEL_INFO, "Enqueuing transfer (port: %d, stream: %d, id: %d, "
        "src: 0x%llx, dst: 0x%llx, size: 0x%llx, reps: 0x%llx, reps_3d: 0x%llx, config: 0x%llx)\n",
        port->id, stream, transfer_id, transfer->src, transfer->dst, transfer->size,
        transfer->reps, transfer->data[IDMA_ND_REPS_3], transfer->config);

    this->streams[stream]->pending++;

    // A transfer with an empty dimension moves no data. Terminate it right away so that its
    // identifier still shows up in DONE_ID and software waiting on it makes progress.
    bool is_empty = transfer->size == 0
        || ((transfer->config & IDMA_CONFIG_2D) && transfer->reps == 0)
        || ((transfer->config & IDMA_CONFIG_3D) && transfer->data[IDMA_ND_REPS_3] == 0);

    if (is_empty)
    {
        this->ack_transfer(transfer);
        return transfer_id;
    }

    this->pending_queue.push(transfer);
    this->check_pending_queue();

    return transfer_id;
}



void IDmaFeReg32_3d::check_pending_queue()
{
    while (this->pending_queue.size() > 0 && this->me->can_accept_transfer())
    {
        IdmaTransfer *transfer = this->pending_queue.front();
        this->pending_queue.pop();
        this->me->enqueue_transfer(transfer);
    }
}



void IDmaFeReg32_3d::raise_event()
{
    for (vp::WireMaster<bool> *itf : this->event_itf)
    {
        if (itf->is_bound())
        {
            itf->sync(true);
        }
    }

    for (vp::WireMaster<bool> *itf : this->event_pe_itf)
    {
        if (itf->is_bound())
        {
            itf->sync(true);
        }
    }
}



// Called by middle-end when a transfer is done
void IDmaFeReg32_3d::ack_transfer(IdmaTransfer *transfer)
{
    int stream = transfer->data[IDMA_ND_STREAM];

    // Transfers of a stream complete in order, so the last completed identifier is simply the
    // previous value incremented.
    this->streams[stream]->done_id.inc(1);
    this->streams[stream]->pending--;

    delete transfer;

    bool busy = false;
    for (Stream *s : this->streams)
    {
        busy |= s->pending > 0;
    }
    this->trace_busy = busy;

    // The hardware broadcasts the completion event to every core and never raises an interrupt
    this->raise_event();
}



// Called by middle-end when something has been updated to check if we must take any action
void IDmaFeReg32_3d::update()
{
    this->check_pending_queue();
}



void IDmaFeReg32_3d::reset(bool active)
{
    if (active)
    {
        while (this->pending_queue.size() > 0)
        {
            delete this->pending_queue.front();
            this->pending_queue.pop();
        }

        for (Stream *stream : this->streams)
        {
            stream->pending = 0;
        }
    }
}
