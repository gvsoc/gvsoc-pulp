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

#pragma once

#include <array>
#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include "floonoc.hpp"

class FlooNoc;

class RouterQueue
{
public:
    RouterQueue(vp::Block *parent, std::string name, vp::ClockEvent *ready_event=NULL);
    vp::Queue queue;
    FloonocNode *stalled_node;
};

/**
 * @brief FlooNoc router
 *
 * Router are the nodes of the noc which are moving internal requests from the network interface
 * to the target.
 */
class Router : public FloonocNode
{
public:
    Router(FlooNoc *noc, std::string name, int x, int y, int queue_size);
    ~Router();

    void reset(bool active);

    // This gets called by other routers or a network interface to move a request to this router
    bool handle_request(FloonocNode *node, vp::IoReq *req, int from_x, int from_y) override;
    // Called by other routers or NI to unstall an output queue after an input queue became available
    void unstall_queue(int from_x, int from_y) override;
    // Called by NI to stall the queues in case no more request should be sent to NI
    void stall_queue(int from_x, int from_y);
    void set_neighbour(int dir, FloonocNode *node);

    // X position of this router in the grid
    int x;
    // Y position of this router in the grid
    int y;

private:
    // FSM event handler called when something happened and queues need to be checked to see
    // if a request should be handled.
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Get the position of the next router which should handle a request.
    void get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y);
    // Get the index of the queue corresponding to a source or destination position
    int get_req_queue(int from_x, int from_y);
    // Return the source or destination position which corresponds to a source or destination
    // queue index
    void get_pos_from_queue(int queue, int &pos_x, int &pos_y);

    // Pointer to top
    FlooNoc *noc;
    // This block trace
    vp::Trace trace;
    // Size of the input queues. This limits the number of requests from the same source which can
    // be pending
    int queue_size;
    // The input queues for each direction and the local one
    RouterQueue *input_queues[5];
    FloonocNode *output_nodes[5];
    // Clock event used to schedule FSM handler. This is scheduled eveytime something may need to
    // be done
    vp::ClockEvent fsm_event;
    // Current queue where next request will be taken from, used for round-robin
    int current_queue;
    // State of the output queues, true if it is stalled and nothing can be sent to it anymore
    // until it is unstalled.
    std::array<vp::Signal<bool>, 5> stalled_queues;
    // Signal used for tracing router request address
    vp::Signal<uint64_t> signal_req;
    vp::Signal<uint64_t> signal_req_size;
    vp::Signal<bool> signal_req_is_write;
};
