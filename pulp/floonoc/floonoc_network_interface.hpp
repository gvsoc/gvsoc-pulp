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
#include "floonoc.hpp"

class FlooNoc;
class NetworkInterface;

class NetworkQueue : public FloonocNode
{
    friend class NetworkInterface;
public:
    NetworkQueue(NetworkInterface &ni, std::string name, uint64_t width, bool is_wide);
    void reset(bool active) override;

    void check();
    void handle_req(vp::IoReq *req);
    void handle_rsp(vp::IoReq *req, bool is_address);

private:
    void enqueue_router_req(vp::IoReq *req, bool is_address, bool is_req);
    void send_router_req();
    void unstall_queue(int from_x, int from_y) override;
    bool handle_request(FloonocNode *node, vp::IoReq *req, int from_x, int from_y) override;

    NetworkInterface &ni;
    Router *router;
    uint64_t width;
    bool is_wide;
    vp::Trace trace;
    std::queue<vp::IoReq *> queue;
    // True when the output queue is stalled because a router denied a request. The network
    // interface can not send any request until it gets unstalled
    bool stalled;
};

/**
 * @brief FlooNoc network interface
 *
 * Network interfaces are entry point to the noc.
 * Initiators can injects IO bursts there.
 * The network interface is then in charge of splitting the bursts into request which fit
 * the noc width and pass them to the closest router, so that they routed to the destination
 */
class NetworkInterface : public FloonocNode
{
    friend class NetworkQueue;

public:
    static constexpr int NW_REQ   = 0;
    static constexpr int NW_RSP   = 1;
    static constexpr int NW_WIDE  = 2;
    static constexpr int NW_NB    = 3;

    NetworkInterface(FlooNoc *noc, int x, int y, std::string itf_name);

    void reset(bool active);

    // This gets called by the top when an asynchronous response is received from a target.
    void handle_response(vp::IoReq *req);
    // This gets called by a router to unstall the output queue of the network interface after
    // a request was denied because the input queue of the router was full
    void unstall_queue(int from_x, int from_y) override;

    // This gets called by a router when the destination is reached and the request is sent from the router to the network interface
    bool handle_request(FloonocNode *node, vp::IoReq *req, int from_x, int from_y) override;
    // Can be used to retrieve the x coordinate of the network interface
    int get_x();
    // Can be used to retrieve the y coordinate of the network interface
    int get_y();
    // This gets called by the top noc to grant a a request denied by a target
    void grant(vp::IoReq *req);
    void set_router(int nw, Router *router);
private:
    // Callback called when a target request is asynchronously granted after a denied error was
    // reported
    static void wide_grant(vp::Block *__this, vp::IoReq *req);
    // Callback called when a target request is asynchronously replied after a pending error was
    // reported
    static void wide_response(vp::Block *__this, vp::IoReq *req);
    static void narrow_grant(vp::Block *__this, vp::IoReq *req);
    static void narrow_response(vp::Block *__this, vp::IoReq *req);
    // Input method called when a narrow burst is received from the local initiator
    static vp::IoReqStatus narrow_req(vp::Block *__this, vp::IoReq *req);
    // Input method called when a wide burst is received from the local initiator
    static vp::IoReqStatus wide_req(vp::Block *__this, vp::IoReq *req);
    vp::IoReqStatus handle_req(vp::IoReq *req);
    // FSM event handler called when something happened and queues need to be checked to see
    // if a request should be handled.
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    int get_req_nw(bool is_wide, bool is_write);
    int get_rsp_nw(bool is_wide, bool is_write);
    // Pointer to top
    FlooNoc *noc;
    int ni_outstanding_reqs;
    // X position of this network interface in the grid
    int x;
    // Y position of this network interface in the grid
    int y;
    vp::IoMaster wide_output_itf;
    vp::IoMaster narrow_output_itf;
    // Input IO interface where wide incoming bursts are injected into the network
    vp::IoSlave wide_input_itf;
    // Input IO interface where narrow incoming bursts are injected into the network
    vp::IoSlave narrow_input_itf;
    // This block trace
    vp::Trace trace;
    NetworkQueue req_queue;
    NetworkQueue wide_queue;
    NetworkQueue rsp_queue;
    // Clock event used to schedule FSM handler. This is scheduled eveytime something may need to
    // be done
    vp::ClockEvent fsm_event;

    // Signal used for tracing narrow reqs
    vp::Signal<uint64_t> signal_narrow_req;
    // Signal used for tracing wide reqs
    vp::Signal<uint64_t> signal_wide_req;
    // True when the associated target has reported a stall. No request must be sent until
    // the stalled one is granted
    bool target_stalled;
    // True when the routers has been stalled because either the target reported a stall
    // or there is not enough requests anymore
    FloonocNode *routers_stalled;
    Router *router[NW_NB];

    vp::IoReq *wide_read_pending_burst;
    vp::IoReq *wide_write_pending_burst;
    int wide_read_pending_burst_nb_req;
    int wide_write_pending_burst_nb_req;
    vp::IoReq *narrow_read_pending_burst;
    vp::IoReq *narrow_write_pending_burst;
    int narrow_read_pending_burst_nb_req;
    int narrow_write_pending_burst_nb_req;


    // When initiator is stalled because max number of input pending req has been reached,
    // this give the input request which has been stalled and must be granted.
    std::queue<vp::IoReq *>wide_denied_read_req;
    std::queue<vp::IoReq *>wide_denied_write_req;
    std::queue<vp::IoReq *>narrow_denied_read_req;
    std::queue<vp::IoReq *>narrow_denied_write_req;

    // Response received synchronously are put here to inject them into rsp only after latency
    // has passed
    vp::Queue response_queue;

int nb_pending_bursts[2];
};
