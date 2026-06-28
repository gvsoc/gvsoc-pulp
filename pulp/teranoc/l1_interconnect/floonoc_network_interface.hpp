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
            Jonas Martin, ETH (martinjo@student.ethz.ch)
 */

#pragma once

#include <vp/vp.hpp>

class FlooNoc;

/**
 * @brief FlooNoc network interface
 *
 * Network interfaces are entry point to the noc.
 * Initiators can injects IO bursts there.
 * The network interface is then in charge of splitting the bursts into request which fit
 * the noc width and pass them to the closest router, so that they routed to the destination
 */
class NetworkInterface : public vp::Block
{
public:
    NetworkInterface(FlooNoc *noc, int x, int y);

    void reset(bool active);

    // This gets called by a router to unstall the output queue of the network interface after
    // a request was denied because the input queue of the router was full
    void unstall_queue(int from_x, int from_y);

    // This gets called by a router when the destination is reached and the request is sent from the router to the network interface
    void req_from_router(vp::IoReq *req, int pos_x, int pos_y);
    // Can be used to retrieve the x coordinate of the network interface
    int get_x();
    // Can be used to retrieve the y coordinate of the network interface
    int get_y();
    // This gets called by the top noc to grant a a request denied by a target
    void grant(vp::IoReq *req);
private:
    // Input method called when a narrow burst is received from the local initiator
    static vp::IoReqStatus narrow_req(vp::Block *__this, vp::IoReq *req);
    // This gets called internally by the wide_req and narrow_req when a burst is received
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    void handle_req(vp::IoReq *burst);
    // Pointer to top
    FlooNoc *noc;
    // X position of this network interface in the grid
    int x;
    // Y position of this network interface in the grid
    int y;
    // Target attached to this network interface
    vp::IoMaster *target;
    // Input IO interface where narrow incoming bursts are injected into the network
    vp::IoSlave narrow_input_itf;
    // This block trace
    vp::Trace trace;

    // True when the output queue is stalled because a router denied a request. The network
    // interface can not send any request until it gets unstalled
    bool stalled;
    // Signal used for tracing narrow reqs
    vp::Signal<uint64_t> signal_narrow_req;
    // True when the associated target has reported a stall. No request must be sent until
    // the stalled one is granted
    bool target_stalled;
    // True when the routers has been stalled because either the target reported a stall
    // or there is not enough requests anymore
    bool router_stalled;
};
