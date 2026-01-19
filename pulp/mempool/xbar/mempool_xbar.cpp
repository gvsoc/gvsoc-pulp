/*
 * Copyright (C) 2025 ETH Zurich and University of Bologna
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

#include <stdio.h>
#include <math.h>
#include <climits>
#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include <vp/itf/io.hpp>
#include <vp/mapping_tree.hpp>

class MempoolXbar;
class InputPort;

/**
 * @brief Port
 *
 * Common class for output and input ports, mostly provides features for logging accesses
 * for the profiler
 */
class Port
{
public:
    Port(MempoolXbar *top, std::string name);
    void log_access(uint64_t addr, uint64_t size);
    MempoolXbar *top;
    // Name of the port, used for logging accesses in the profiler
    std::string name;
    // Address used for logging accesses in the profiler
    vp::Signal<uint64_t> log_addr;
    // Size used for logging accesses in the profiler
    vp::Signal<uint64_t> log_size;
    // Last cycle where an access was logged. Used to delay a bit in the trace the requests
    // which arrives in the same cycle
    int64_t last_logged_access = -1;
    // Number of requests logged in the same cycle. Used to delay a bit in the trace the requests
    // which arrives in the same cycle
    int nb_logged_access_in_same_cycle = 0;
    // Used for profiling
    vp::Signal<bool> stalled_signal;
};

/**
 * @brief OutputPort
 *
 * This represents a possible OutputPort in the memory map.
 * It is used to implement the bandwidth limiter, and store port information like request election
 * and stall state
 */
class OutputPort : public Port
{
public:
    OutputPort(MempoolXbar *top, std::string name, int64_t bandwidth, int64_t latency);
    // True when a request has been denied. The port can not send requests anymore until the
    // denied request is granted. This also stall the elected input port.
    bool stalled = false;
    // When not NULL, indicates a request has been elected to be sent through this output port
    // and should be sent as soon as the bandwidth allows it. The input port can not send anymore
    // until this elected request is sent
    InputPort *elected_input = NULL;
    // Cycle stamp where the next request can be sent. It is updated each time a request is
    // sent, according to request and size and router bandwidth
    int64_t next_burst_cycle = 0;
    // When the output port is stalled, this indicates the input port where the request comes from
    // This allows unstalling the input port when the output port is unstalled since they are
    // stalled together
    InputPort *stalled_port;
};

/**
 * @brief InputPort
 *
 * This represents a router input port, mostly used to store information about bandwidth limiter
 * and port state like pending requests
 */
class InputPort : public Port
{
public:
    InputPort(int id, std::string name, MempoolXbar *top, int64_t bandwidth, int64_t latency);
    int id;
    // Queue of pending requests. Any incoming request is first pushed here. An arbitration event
    // is then executed to route these requests to output ports
    std::queue<vp::IoReq *> pending_reqs;
    // Queue of requests which have been denied because the input FIFO size was full. As soon as
    // the FIFO becomes ready, requests are popped from this queue and pushed to the pending queue
    std::queue<vp::IoReq *> denied_reqs;
    // Input FIFO size. Incoming requests are denied as soon as this becomes equal or greater than
    // the FIFO size
    vp::Signal<int> pending_size;
    // If not NULL, this indicates the mapping tree information about the currently elected
    // request.
    vp::Signal<bool> arbitration_lock;
    vp::Signal<int> pending_mapping;
    // True if the input port is full because an elected request from this port was sent and denied.
    // The input port can still enqueue incoming requests unless the FIFO is full, but no more
    // requests are router until the port is unstalled
    bool stalled = false;
    // Cycle stamp where the next request can be elected. It is updated each time a request is
    // elected, according to request and size and router bandwidth
    int64_t next_burst_cycle = 0;
};

/**
 * @brief Channel router
 *
 * This class is used to model requests for a channel.
 * The router can be configured with either a single channel where both read and write requests
 * shares the same bandwidth and are scheduled together, or with 2 channels where read and write
 * requests have their own bandwidth and are scheduled separatly.
 */
class Channel : public vp::Block
{
public:
    Channel(MempoolXbar *top, std::string name);
    // Handle an incoming request
    vp::IoReqStatus handle_req(vp::IoReq *req, int port);
    // Handle the response of a request which was sent to an output port
    void response(vp::IoReq *req, int id);
    // Handle the grant of a request which was sent to an output port and denied
    void grant(vp::IoReq *req, int id);

private:
    // Arbiter event handler. See arbiter event doc for details
    static void arbiter_handler(vp::Block *__this, vp::ClockEvent *event);
    // FSM event handler. See FSM event doc for details
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Handle a request which has been granted
    void handle_req_grant(InputPort *in);
    // Handle a request which has received its response
    void handle_req_end(vp::IoReq *req, vp::IoReqStatus status);

    MempoolXbar *top;
    // This component trace
    vp::Trace trace;
    // Array of input ports
    std::vector<InputPort *> inputs;
    // Array of output ports
    std::vector<OutputPort *> entries;
    // Arbiter event. This is a special event which is enqueued with 0 delay to be executed at the
    // end of the cycle, to do the arbitration with all requests, including all the ones enqueued
    // during the las cycle. It elects input pending requests
    // to be sent. The elected requests are sent in another event, since it is not allowed to call
    // interfaces in 0 delay events.
    vp::ClockEvent arbiter_event;
    // FSM events. It takes elected requests and tries to send them.
    vp::ClockEvent fsm_event;
    // Input where election will start in the next election phase. Used to implement round robin
    int current_input = 0;
    vp::Queue ended_reqs;
};


/**
 * @brief Asynchronous router
 *
 * This models an AXI-like router that is capable of routing memory-mapped requests from an input
 * port to output ports based on the request address.
 */
class MempoolXbar : public vp::Component
{
    friend class Channel;

public:
    MempoolXbar(vp::ComponentConf &conf);

private:
    // Incoming requests are received here. The port indicates from which input port it is received.
    vp::IoReqStatus handle_req(vp::IoReq *req, int port);
    // Interface callback where incoming requests are received. Just a wrapper for handle_req
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req, int port);
    // Asynchronous responses are received here.
    static void response(vp::Block *__this, vp::IoReq *req, int id);
    // Asynchronous grants are received here.
    static void grant(vp::Block *__this, vp::IoReq *req, int id);

    // This component trace
    vp::Trace trace;
    // True if the bandwidth is shared between read and writes
    bool shared_rw_bandwidth;
    // Input FIFO size
    int max_input_pending_size;
    // Router bandwidth
    int64_t bandwidth;
    // Number of input ports
    int nb_input_port;
    int nb_output_port;
    // Router latency, not yet implemented
    int latency;
    // Router channels. It has 1 channel when bandwidth is shared for read and writes, or 1 for read
    // and 1 for write if it is not shared
    std::vector<Channel *> channels;
    // Gives the ID of the error mapping, the one returning an error when a request is matching
    // this mapping
    int error_id = -1;
    // COmponent input interfaces
    std::vector<vp::IoSlave> input_itfs;
    // COmponent output interfaces
    std::vector<vp::IoMaster *> output_itfs;
};



MempoolXbar::MempoolXbar(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->bandwidth = this->get_js_config()->get_int("bandwidth");
    this->latency = this->get_js_config()->get_int("latency");
    this->nb_input_port = this->get_js_config()->get_int("nb_input_port");
    this->nb_output_port = this->get_js_config()->get_int("nb_output_port");
    this->shared_rw_bandwidth = this->get_js_config()->get_child_bool("shared_rw_bandwidth");
    this->max_input_pending_size = this->get_js_config()->get_child_int("max_input_pending_size");
    if (this->max_input_pending_size == 0)
    {
        // If no FIFO is specified, set to max to not have any limitation
        this->max_input_pending_size = INT_MAX;
    }

    // Instantiates input ports
    this->input_itfs.resize(nb_input_port);
    for (int i=0; i<this->nb_input_port; i++)
    {
        vp::IoSlave *input = &this->input_itfs[i];
        std::string name = i == 0 ? "input" : "input_" + std::to_string(i);
        input->set_req_meth_muxed(&MempoolXbar::req, i);
        this->new_slave_port(name, input, this);
    }
    for (int i=0; i<this->nb_output_port; i++)
    {
        vp::IoMaster *itf = new vp::IoMaster();
        std::string name = i == 0 ? "output" : "output_" + std::to_string(i);
        itf->set_resp_meth_muxed(&MempoolXbar::response, i);
        itf->set_grant_meth_muxed(&MempoolXbar::grant, i);
        this->output_itfs.push_back(itf);
        this->new_master_port(name, itf);
    }

    if (this->shared_rw_bandwidth)
    {
        this->channels.push_back(new Channel(this, "rwchannel"));
    }
    else
    {
        this->channels.push_back(new Channel(this, "rchannel"));
        this->channels.push_back(new Channel(this, "wchannel"));
    }
}

vp::IoReqStatus MempoolXbar::req(vp::Block *__this, vp::IoReq *req, int port)
{
    MempoolXbar *_this = (MempoolXbar *)__this;
    return _this->handle_req(req, port);
}

vp::IoReqStatus MempoolXbar::handle_req(vp::IoReq *req, int port)
{
    int channel = this->shared_rw_bandwidth ? 0 : req->get_is_write();
    return this->channels[channel]->handle_req(req, port);
}

void MempoolXbar::grant(vp::Block *__this, vp::IoReq *req, int id)
{
    MempoolXbar *_this = (MempoolXbar *)__this;
    int channel = _this->shared_rw_bandwidth ? 0 : req->get_is_write();
    _this->channels[channel]->grant(req, id);
}

void MempoolXbar::response(vp::Block *__this, vp::IoReq *req, int id)
{
    MempoolXbar *_this = (MempoolXbar *)__this;
    int channel = _this->shared_rw_bandwidth ? 0 : req->get_is_write();
    _this->channels[channel]->response(req, id);
}

void Channel::arbiter_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Channel *_this = (Channel *)__this;
    int64_t cycles = _this->clock.get_cycles();

    // Go through each input port and see if we can route a new request to an output port
    for (int i=0, current_input=_this->current_input; i<_this->inputs.size(); i++)
    {
        InputPort *in = _this->inputs[current_input];

        // Check if the bandwidth allows the request to go through the input
        if (cycles >= in->next_burst_cycle)
        {
            // Check if this port is not stalled and has any pending request
            if (!in->stalled && !in->pending_reqs.empty())
            {
                vp::IoReq *req = in->pending_reqs.front();
                // Since a request can be checked multiple times before it is routed, we only
                // get its mapping the first time when it is not yet initialized
                if (!in->arbitration_lock.get())
                {
                    uint64_t output_id = (uint64_t)req->arg_pop();
                    if (output_id >= _this->top->nb_output_port)
                    {
                        _this->trace.fatal("Invalid output ID %ld\n", output_id);
                    }
                    else
                    {
                        in->arbitration_lock = true;
                        in->pending_mapping = output_id;
                    }
                }

                OutputPort *out = _this->entries[in->pending_mapping.get()];
                // Check if the output we need is not already assigned a request
                if (out->elected_input == NULL && out->stalled == false)
                {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Elected input (req: %p, in: %d, out: %d)\n",
                        req, i, in->pending_mapping.get());

                    // If not, store the request, it will be sent in the FSM event
                    out->elected_input = in;

                    req->get_resp_port()->grant(req);

                    // Update now the input FIFO to let another request be accepted in the next
                    // cycle. This only works for requests smaller than bandwidth
                    in->pending_size -= req->get_size();

                    // Update now the bandwidth since the next request will anyway be processed
                    // only when this one has been sent
                    if (_this->top->bandwidth > 0)
                    {
                        in->next_burst_cycle = cycles +
                            (req->get_size() + _this->top->bandwidth - 1) / _this->top->bandwidth;
                    }
                }
            }
        }

        current_input++;
        if (current_input == _this->inputs.size())
        {
            current_input = 0;
        }
    }

    // Update election round-robin
    _this->current_input++;
    if (_this->current_input == _this->inputs.size())
    {
        _this->current_input = 0;
    }


    _this->fsm_event.enqueue();
}

void Channel::grant(vp::IoReq *req, int id)
{
    // When an output request is denied, it stalls both the input and the output ports, we need
    // to unstall both
    OutputPort *out = this->entries[id];
    InputPort *in = out->stalled_port;
    out->stalled = false;
    out->stalled_port->stalled = false;
    out->stalled_signal = false;
    out->stalled_port->stalled_signal = !out->stalled_port->denied_reqs.empty();
    // Handle request grant, this will remove it from the queue and allow a new election for
    // this input port.
    this->handle_req_grant(in);
}

void Channel::response(vp::IoReq *req, int id)
{
    // Handle request response, this will reply to the initiator component
    this->handle_req_end(req, req->status);
}

void Channel::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Channel *_this = (Channel *)__this;
    int64_t cycles = _this->clock.get_cycles();

    // Some requests for which responses came back synchronously need to be handled once the
    // latency has ellapsed
    while(!_this->ended_reqs.empty())
    {
        vp::IoReq *ended_req = (vp::IoReq *)_this->ended_reqs.pop();
        _this->handle_req_end(ended_req, vp::IO_REQ_OK);
    }

    // Go through each output to see if we can send requests
    for (int i=0; i<_this->entries.size(); i++)
    {
        OutputPort *out = _this->entries[i];

        // Only send if the port is not stalled due to a denied request and the bandwidth
        // allows it
        if (!out->stalled && cycles >= out->next_burst_cycle)
        {
            InputPort *in = out->elected_input;

            // Only send if a request was elected to be sent to this port
            if (in)
            {
                // We grant now the requests which were denied due to input FIFO so that
                // the initiator can send new requests in this cycle
                while (in->denied_reqs.size() > 0 && in->pending_size < _this->top->max_input_pending_size)
                {
                    vp::IoReq *denied_req = in->denied_reqs.front();
                    in->denied_reqs.pop();
                    in->stalled_signal = !in->denied_reqs.empty();
                    in->pending_reqs.push(denied_req);
                    in->pending_size += denied_req->get_size();
                }

                vp::IoMaster *itf =_this->top->output_itfs[i];
                out->elected_input = NULL;

                vp::IoReq *req;
                uint64_t addr;
                req = in->pending_reqs.front();
                addr = req->get_addr();
                req->arg_push((void *)req->resp_port);

                if (_this->top->bandwidth > 0)
                {
                    out->next_burst_cycle = cycles +
                        (req->get_size() + _this->top->bandwidth - 1) / _this->top->bandwidth;
                }

                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Sending req (req: %p, in: %d, out: %d)\n",
                    req, in->id, i);

                out->log_access(addr, req->get_size());


                vp::IoReqStatus status = itf->req(req);

                if (status == vp::IO_REQ_DENIED)
                {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Denied req, stalling input and output (req: %p, in: %d, out: %d)\n",
                        req, in->id, i);

                    out->stalled = true;
                    out->stalled_port = in;
                    in->stalled = true;
                    out->stalled_signal = true;
                    in->stalled_signal = true;
                }
                else
                {
                    _this->handle_req_grant(in);
                }

                if (status == vp::IO_REQ_OK)
                {
                    // When the request is handle synchronously, the target uses the latency to
                    // apply timing. We need to take it into account before reusing the request
                    int64_t latency = req->get_full_latency();
                    if (latency <= 1)
                    {
                        _this->handle_req_end(req, vp::IO_REQ_OK);
                    }
                    else
                    {
                        _this->ended_reqs.push_back(req, latency - 2);
                    }
                }
            }
        }
    }

    bool resched = false;
    for (int i=0; i<_this->inputs.size(); i++)
    {
        resched |= _this->inputs[i]->pending_reqs.size() > 0;
    }

    if (resched || _this->ended_reqs.has_reqs())
    {
        _this->arbiter_event.enqueue(0);
    }
}

Channel::Channel(MempoolXbar *top, std::string name)
: vp::Block(top, name), top(top), fsm_event(this, Channel::fsm_handler),
    arbiter_event(this, Channel::arbiter_handler),
    ended_reqs(this, "ended_reqs")
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    // Instantiates input ports
    this->inputs.resize(this->top->nb_input_port);
    for (int i=0; i<this->top->nb_input_port; i++)
    {
        InputPort *input_port = new InputPort(i, name + "/input_" + std::to_string(i), this->top, this->top->bandwidth, this->top->latency);
        this->inputs[i] = input_port;
    }

    for (int i=0; i<this->top->nb_output_port; i++)
    {
        OutputPort *entry = new OutputPort(this->top, name + '/' + std::to_string(i), this->top->bandwidth, 0);

        this->entries.push_back(entry);


    }
}

vp::IoReqStatus Channel::handle_req(vp::IoReq *req, int port)
{
    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    bool is_write = req->get_is_write();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n",
        offset, size, is_write);

    InputPort *in = this->inputs[port];

    in->log_access(offset, size);

    if (in->denied_reqs.size() > 0 || in->pending_size >= this->top->max_input_pending_size)
    {
        in->denied_reqs.push(req);
        in->stalled_signal = true;
        return vp::IO_REQ_DENIED;
    }
    else
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Pushing req to pending (req: %p, in: %d)\n",
            req, port);
        in->pending_reqs.push(req);
        in->pending_size += req->get_size();
        this->arbiter_event.enqueue(0);
        return vp::IO_REQ_DENIED;
    }
}

void Channel::handle_req_grant(InputPort *in)
{
    vp::IoReq *req = in->pending_reqs.front();
    in->pending_reqs.pop();
    in->arbitration_lock = false;
}

void Channel::handle_req_end(vp::IoReq *req, vp::IoReqStatus status)
{
    vp::IoSlave *resp_port = (vp::IoSlave *)req->arg_pop();
    if (resp_port)
    {
        resp_port->resp(req);
    }
    else
    {
        vp::IoReq *parent_req = (vp::IoReq *)req->arg_pop();
        vp::IoReqStatus praent_status = (vp::IoReqStatus)(long)parent_req->arg_pop();
        uint64_t remaining_size = (int64_t)parent_req->arg_pop();
        remaining_size -= req->get_size();
        if (status == vp::IO_REQ_INVALID)
        {
            praent_status = vp::IO_REQ_INVALID;
        }

        this->top->output_itfs[0]->req_del(req);

        if (remaining_size > 0)
        {
            parent_req->arg_push((void *)remaining_size);
            parent_req->arg_push((void *)praent_status);
        }
        else
        {
            vp::IoSlave *resp_port = (vp::IoSlave *)parent_req->arg_pop();
            parent_req->status = praent_status;
            resp_port->resp(parent_req);
        }
    }
}

Port::Port(MempoolXbar *top, std::string name)
: top(top), name(name),
log_addr(*top, name + "/addr", 64, vp::SignalCommon::ResetKind::HighZ),
log_size(*top, name + "/size", 64, vp::SignalCommon::ResetKind::HighZ),
stalled_signal(*top, name + "/stalled", 1)
{

}

void Port::log_access(uint64_t addr, uint64_t size)
{
    int64_t cycles = this->top->clock.get_cycles();

    if (cycles > this->last_logged_access)
    {
        this->nb_logged_access_in_same_cycle = 0;
    }

    int64_t delay = 0;
    if (this->nb_logged_access_in_same_cycle > 0)
    {
        int64_t period = this->top->clock.get_period();
        delay = period - (period >> this->nb_logged_access_in_same_cycle);
    }
    this->log_addr.set_and_release(addr, 0, delay);
    this->log_size.set_and_release(size, 0, delay);
    this->nb_logged_access_in_same_cycle++;
    this->last_logged_access = cycles;
}

OutputPort::OutputPort(MempoolXbar *top, std::string name, int64_t bandwidth, int64_t latency)
    : Port(top, name)
{
}

InputPort::InputPort(int id, std::string name, MempoolXbar *top, int64_t bandwidth, int64_t latency)
: Port(top, name), id(id), pending_size(*top, name + "/pending_size", 32, vp::SignalCommon::ResetKind::Value, 0),
    pending_mapping(*top, name + "/pending_mapping", 32, vp::SignalCommon::ResetKind::Value, 0),
    arbitration_lock(*top, name + "/arbitration_lock", 1, vp::SignalCommon::ResetKind::Value, 0)
{
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new MempoolXbar(config);
}
