/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 *          Jonas Martin, ETH (martinjo@student.ethz.ch)
 */

#include "floonoc_router_flex.hpp"
#include "floonoc_flex.hpp"
#include "floonoc_network_interface_flex.hpp"
#include <vp/itf/io.hpp>
#include <vp/vp.hpp>

Router::Router(FlooNoc *noc, std::string name, int node_id, int num_queues,
               int queue_size)
    : FloonocNode(noc, name + "_" + std::to_string(node_id)),
      fsm_event(this, &Router::fsm_handler),
      signal_req(*this, "req", 64, vp::SignalCommon::ResetKind::HighZ),
      signal_req_size(*this, "req_size", 64,
                      vp::SignalCommon::ResetKind::HighZ),
      signal_req_is_write(*this, "req_is_write", 1,
                          vp::SignalCommon::ResetKind::HighZ)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->noc = noc;
    this->node_id = node_id;
    this->num_queues = num_queues;
    this->queue_size = queue_size;
    this->input_queues.resize(this->num_queues, nullptr);
    this->output_nodes.resize(this->num_queues, nullptr);
    this->stalled_queues.resize(this->num_queues, nullptr);
    this->input_latencies.resize(this->num_queues, 1);

    // Create a queue for each direction
    for (int i = 0; i < this->num_queues; i++)
    {
        this->input_queues[i] = new RouterQueue(
            this, "input_queue_" + std::to_string(i), &this->fsm_event);

        this->stalled_queues[i] = new vp::Signal<bool>(
            *this, "stalled_queue_" + std::to_string(i), 1);

        *(this->stalled_queues[i]) = false;
    }
}

Router::~Router()
{
    for (int i = 0; i < this->num_queues; i++)
    {
        delete this->input_queues[i];
        delete this->stalled_queues[i];
    }
}

void Router::set_neighbour(int dir, FloonocNode *node, int neighbor_id,
                           int latency)
{
    if (dir >= this->num_queues)
    {
        printf("\n[FATAL] Router %d tried to map neighbor "
               "%d to port %d, but num_queues is %d.\n",
               this->node_id, neighbor_id, dir, this->num_queues);
        printf("Check your Python script for duplicate add_link() calls!\n");
        exit(1);
    }
    this->output_nodes[dir] = node;
    this->neighbor_to_queue[neighbor_id] = dir;
    this->queue_to_neighbor[dir] = neighbor_id;
    this->input_latencies[dir] = latency;
}

void Router::set_routing_table(std::vector<int> table)
{
    this->routing_table = table;
}

bool Router::handle_request(FloonocNode *node, vp::IoReq *req, int from_node)
{
    this->trace.msg(
        vp::Trace::LEVEL_DEBUG,
        "Handle request (req: %p, base: 0x%x, size: 0x%x, from: %d)\n", req,
        req->get_addr(), req->get_size(), from_node);

    this->signal_req.set_and_release(req->initiator_addr);
    this->signal_req_size.set_and_release(req->get_size());
    this->signal_req_is_write.set_and_release(req->get_is_write());

    // Each direction has its own input queue to properly implement the
    // round-robin Get the one for the router or network interface which sent
    // this request
    int queue_index = this->get_req_queue(from_node);

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
                    "Pushed request to input queue (req: %p, queue: %d)\n", req,
                    queue_index);

    // And push it to the queue. The queue will automatically trigger the FSM if
    // needed
    RouterQueue *queue = this->input_queues[queue_index];
    int latency = this->input_latencies[queue_index];
    queue->queue.push_back(req,
                           latency); // Delay representing link latency

    // Update peak queue depth performance metric
    int current_depth = queue->queue.size();
    if (current_depth > queue->peak_queue_depth)
    {
        queue->peak_queue_depth = current_depth;
    }

    // We let the source enqueue one more request than what is possible to model
    // the fact the request is stalled. This will then stall the source which
    // will not send any request there anymore until we unstall it
    bool stalled = queue->queue.size() > this->queue_size;
    if (stalled)
    {
        queue->stalled_node = node;
    }
    return stalled;
}

void Router::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Router *_this = (Router *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Checking pending requests\n");
    // The routers can process 1 incoming request from each direction and send 1
    // request to each of the directions in 1 cycle The round robin is used to
    // make sure we don't always process the same direction first
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Current queue: %d\n",
                     _this->current_queue);
    // Get the currently active queue and update it to implement the round-robin
    int in_queue_index = _this->current_queue;

    std::vector<bool> output_full(_this->num_queues,
                                  false); // Used to make sure we only send a
                                          // single request per cycle to each
                                          // direction
    // Then go through the num_queues input queues until we find a request which
    // can be propagated
    for (int i = 0; i < _this->num_queues; i++)
    {
        RouterQueue *queue = _this->input_queues[in_queue_index];
        _this->trace.msg(
            vp::Trace::LEVEL_TRACE,
            "Checking input queue (queue_index: %d, queue size: %d)\n",
            in_queue_index, queue->queue.size());
        if (!queue->queue.empty())
        {
            vp::IoReq *req = (vp::IoReq *)queue->queue.head();

            // Extract the destination from the request, that was filled in the
            // network interface when the request was created

            int to_node = req->get_int(FlooNoc::REQ_DEST_ID);

            // Get the next node id. This takes care of deciding
            // which path is taken to go to the destination
            int next_node;
            _this->get_next_router_pos(to_node, next_node);
            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                             "Resolved next position (req: %p, dest: (%d), "
                             "next_position: (%d))\n",
                             req, to_node, next_node);

            // Get output queue ID from next position
            int out_queue_id = _this->get_req_queue(next_node);

            if (out_queue_id < 0 || out_queue_id >= _this->num_queues)
            {
                printf("\n[FATAL] Router %d tried to route packet to NextHop "
                       "%d, but get_req_queue returned %d!\n",
                       _this->node_id, next_node, out_queue_id);
                printf("Original Destination was: %d\n", to_node);
                exit(1);
            }

            // Only send one request per cycle to the same output
            if (output_full[out_queue_id])
            {
                // Performance Counter
                _this->stat_stall_cycles++;

                _this->trace.msg(
                    vp::Trace::LEVEL_TRACE,
                    "Output queue is full, skipping (out queue: %d)\n",
                    out_queue_id);
                _this->fsm_event.enqueue(); // Check again in next cycle
                in_queue_index += 1;
                if (in_queue_index == _this->num_queues)
                {
                    in_queue_index = 0;
                }
                continue; // Skip this request and instead check another input
                          // queue
            }
            output_full[out_queue_id] = true;

            // In case the request goes to a queue which is stalled, skip it
            // we'll retry later
            if (*(_this->stalled_queues[out_queue_id]))
            {
                // Performance Counter
                _this->stat_stall_cycles++;

                _this->trace.msg(
                    vp::Trace::LEVEL_TRACE,
                    "Output queue is stalled, skipping (out queue: %d)\n",
                    out_queue_id);
                // Don't enque here because the stalled router will notifiy once
                // it is unstalled
                in_queue_index += 1;
                if (in_queue_index == _this->num_queues)
                {
                    in_queue_index = 0;
                }
                continue;
            }

            // Since we now know, that the request will be propagated, remove it
            // from the queue
            queue->queue.pop();

            // Performance Counter
            _this->stat_routed_packets++;

            if (queue->queue.size() ==
                _this->queue_size) // Remember we let the source enqueue one
                                   // more request than what is possible.
            {
                // In case the queue had one more element than possible, it
                // means the output queue of the sending router is stalled.
                // Unstall it now that we can accept one more request
                queue->stalled_node->unstall_queue(_this->node_id);
            }

            // Otherwise forward to next position
            FloonocNode *node = _this->output_nodes[out_queue_id];

            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                             "Forwarding request to next router (req: %p, "
                             "base: 0x%x, size: 0x%x, "
                             "next_node: %d, in_queue: %d)\n",
                             req, req->get_addr(), req->get_size(), next_node,
                             in_queue_index);
            // Send the request to next router, and in case it reports that its
            // input queue is full, stall the corresponding output queue to make
            // sure we stop sending there until the queue is unstalled
            if (node->handle_request(_this, req, _this->node_id))
            {
                _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                                 "Stalling queue (node: %d, queue: %d)\n",
                                 _this->node_id, out_queue_id);
                *(_this->stalled_queues[out_queue_id]) = true;
            }
            _this->current_queue =
                in_queue_index + 1; // Always start looking from the queue after
                                    // the one that has been processed last
            if (_this->current_queue == _this->num_queues)
            {
                _this->current_queue = 0;
            }

            // Since we removed a request, check in next cycle if there is
            // another one to handle
            _this->fsm_event.enqueue();
        }
        else
        {
            if (queue->queue.size())
            {
                _this->fsm_event.enqueue();
            }
        }

        // Go to next input queue
        in_queue_index += 1;
        if (in_queue_index == _this->num_queues)
        {
            in_queue_index = 0;
        }
    }
}

// This is determining the routes the requests will take in the network
void Router::get_next_router_pos(int to_node, int &next_node)
{
    if (to_node < 0 || to_node >= this->routing_table.size())
    {
        printf("\n[FATAL] Router %d asked to route to Node %d, but "
               "routing_table size is %lu!\n",
               this->node_id, to_node, this->routing_table.size());
        exit(1);
    }
    if (to_node != this->node_id)
    {
        next_node = this->routing_table[to_node];
    }
    else
    {
        next_node = this->node_id;
    }
}

void Router::unstall_queue(int from_node)
{
    // This gets called when an output queue gets unstalled because the denied
    // request gets granted. Just unstall the queue and trigger the fsm, in case
    // we can now send a new request
    int queue = this->get_req_queue(from_node);
    if (queue == -1)
        return; // Prevent negative index access

    this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "Unstalling queue (node: %d, queue: %d)\n", from_node,
                    queue);
    *(this->stalled_queues[queue]) = false;
    // And check in next cycle if another request can be sent
    this->fsm_event.enqueue();
}

void Router::stall_queue(int from_node)
{
    int queue = this->get_req_queue(from_node);
    if (queue == -1)
        return; // Prevent negative index access

    this->trace.msg(vp::Trace::LEVEL_TRACE,
                    "Stalling queue (node: %d, queue: %d)\n", from_node, queue);
    *(this->stalled_queues[queue]) = true;
}

void Router::get_node_from_queue(int queue, int &node_id)
{
    node_id = this->queue_to_neighbor[queue];
}

int Router::get_req_queue(int from_node)
{
    // Local NI and neighbor routers will be mapped here
    if (this->neighbor_to_queue.count(from_node))
    {
        return this->neighbor_to_queue[from_node];
    }
    this->trace.msg(vp::Trace::LEVEL_ERROR, "Unknown from_node %d\n",
                    from_node);
    return -1;
}

int Router::get_max_peak_queue_depth()
{
    int max_peak = 0;
    for (int i = 0; i < this->num_queues; i++)
    {
        if (this->input_queues[i]->peak_queue_depth > max_peak)
        {
            max_peak = this->input_queues[i]->peak_queue_depth;
        }
    }
    return max_peak;
}

void Router::reset(bool active)
{
    if (active)
    {
        this->current_queue = 0;
        for (int i = 0; i < this->num_queues; i++)
        {
            *(this->stalled_queues[i]) = false;
        }
    }
}

RouterQueue::RouterQueue(vp::Block *parent, std::string name,
                         vp::ClockEvent *ready_event)
    : queue(parent, name, ready_event)
{
    this->peak_queue_depth = 0;
}