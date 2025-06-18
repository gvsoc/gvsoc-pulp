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

#include <vp/vp.hpp>

class FlooNoc;

/**
 * @brief FlooNoc router
 *
 * Router are the nodes of the noc which are moving internal requests from the network interface
 * to the target.
 */
class Router : public vp::Block
{
public:
    Router(FlooNoc *noc, std::string name, int x, int y, int queue_size);

    void reset(bool active);

    // This gets called by other routers or a network interface to move a request to this router
    bool handle_request(vp::IoReq *req, int from_x, int from_y);
    // Called by other routers to unstall an output queue after an input queue became available
    void unstall_queue(int from_x, int from_y);
    // This gets called by the top noc to grant a a request denied by a target
    void grant(vp::IoReq *req);

    // State of the output queues, true if it is stalled and nothing can be sent to it anymore
    // until it is unstalled.
    bool stalled_queues[5];

private:
    // FSM event handler called when something happened and queues need to be checked to see
    // if a request should be handled.
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Called when a request has reached its destination position and should be sent to a target
    void send_to_target_ni(vp::IoReq *req, int pos_x, int pos_y);
    // Get the position of the next router which should handle a request.
    void get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y);
    // Get the index of the queue corresponding to a source or destination position
    int get_req_queue(int from_x, int from_y);
    // Return the source or destination position which corresponds to a source or destination
    // queue index
    void get_pos_from_queue(int queue, int &pos_x, int &pos_y);

    // Unstalls the router or network interface corresponding to the in_queue_index
    void unstall_previous(vp::IoReq *req, int in_queue_index);

    // Pointer to top
    FlooNoc *noc;
    // This block trace
    vp::Trace trace;
    // X position of this router in the grid
    int x;
    // Y position of this router in the grid
    int y;
    // Size of the input queues. This limits the number of requests from the same source which can
    // be pending
    int queue_size;
    // The input queues for each direction and the local one
    vp::Queue *input_queues[5];
    // Clock event used to schedule FSM handler. This is scheduled eveytime something may need to
    // be done
    vp::ClockEvent fsm_event;
    // Current queue where next request will be taken from, used for round-robin
    int current_queue;
};
