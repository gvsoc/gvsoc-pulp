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
#include <list>

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

    // This gets called by the top when an asynchronous response is received from a target.
    void handle_response(vp::IoReq *req);
    // This gets called by a router to unstall the output queue of the network interface after
    // a request was denied because the input queue of the router was full
    void unstall_queue(int from_x, int from_y);

    // This gets called by a router when the destination is reached and the request is sent from the router to the network interface
    vp::IoReqStatus req_from_router(vp::IoReq *req, int pos_x, int pos_y);
    // Can be used to retrieve the x coordinate of the network interface
    int get_x();
    // Can be used to retrieve the y coordinate of the network interface
    int get_y();

private:
    // Input method called when a narrow burst is received from the local initiator
    static vp::IoReqStatus narrow_req(vp::Block *__this, vp::IoReq *req);
    // Input method called when a wide burst is received from the local initiator
    static vp::IoReqStatus wide_req(vp::Block *__this, vp::IoReq *req);
    // This gets called internally by the wide_req and narrow_req when a burst is received
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    // FSM event handler called when something happened and queues need to be checked to see
    // if a request should be handled.
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // This gets called to handle a addr request
    void handle_addr_req(void);
    // This gets called to handle a data request
    void handle_data_req(void);
    // This gets called to remove the current pending burst and also remove all related information from the other queues
    void remove_pending_burst(void);
    // This gets called to add a new pending burst to the queue
    void add_pending_burst(vp::IoReq *burst, bool isaddr, int64_t timestamp, std::tuple<int, int> origin_pos);
    // Pointer to top
    FlooNoc *noc;
    // X position of this network interface in the grid
    int x;
    // Y position of this network interface in the grid
    int y;
    // Maxinum number of pending input requests before the initiator is stalled
    int ni_outstanding_reqs;
    // Input IO interface where wide incoming bursts are injected into the network
    vp::IoSlave wide_input_itf;
    // Input IO interface where narrow incoming bursts are injected into the network
    vp::IoSlave narrow_input_itf;
    // This block trace
    vp::Trace trace;
    // Queue of pending incoming bursts. Any received burst is pushed there and they are processed
    // one by one sequentially by the network interface.
    std::queue<vp::IoReq *> pending_bursts;
    // Also store if the burst is an address burst or a data burst. This is used to know if the
    // burst must be processed by the address handler or the data handler
    std::queue<bool> pending_burst_isaddr;
    // Also a maintain a queue of timestamps at which the corresponding burst can start to take
    // into account the burst latency.
    std::queue<int64_t> pending_bursts_timestamp;
    // Also store the origin position of the burst to know where to send the response requests
    std::queue<std::tuple<int, int>> pending_bursts_origin_pos;
    // Current base address of the burst currently being processed. It is used to update the address
    // of the internal requests send to the routers to process the burst
    uint64_t pending_burst_base;
    // Remaining size of the burst currently being processed. Used to track when all requests
    // for the current burst have been sent.
    uint64_t pending_burst_size;
    // Current data of the burst currently being processed.
    uint8_t *pending_burst_data;
    // Clock event used to schedule FSM handler. This is scheduled eveytime something may need to
    // be done
    vp::ClockEvent fsm_event;

    // True when the output queue is stalled because a router denied a request. The network
    // interface can not send any request until it gets unstalled
    bool stalled;
    // When initiator is stalled because max number of input pending req has been reached,
    // this give the input request which has been stalled and must be granted.
    vp::IoReq *denied_req;
};
