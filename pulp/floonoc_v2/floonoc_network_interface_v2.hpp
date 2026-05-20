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

#pragma once

#include <vp/vp.hpp>
#include <list>
#include "floonoc_v2.hpp"

class FlooNocV2;
class NetworkInterfaceV2;
class RouterV2;

class NetworkQueueV2 : public FloonocNodeV2
{
    friend class NetworkInterfaceV2;
public:
    NetworkQueueV2(NetworkInterfaceV2 &ni, std::string name, uint64_t width, bool is_wide);
    void reset(bool active) override;

    void check();
    void handle_req(vp::IoReq *req, bool wide);
    void handle_rsp(FloonocReqV2 *req, bool is_address);

private:
    void enqueue_router_req(vp::IoReq *req, bool is_address, bool wide, bool is_req);
    void enqueue_router_rsp(FloonocReqV2 *req, bool is_address);
    void send_router_req();
    void unstall_queue(int from_x, int from_y) override;
    bool handle_request(FloonocNodeV2 *node, FloonocReqV2 *req, int from_x, int from_y) override;

    NetworkInterfaceV2 &ni;
    RouterV2 *router;
    uint64_t width;
    bool is_wide;
    vp::Trace trace;
    std::queue<FloonocReqV2 *> queue;
    bool stalled;
};

/**
 * v2 FlooNoC network interface.
 *
 * Same role as the v1 NI: entry/exit point for the mesh. External ports speak
 * the v2 io protocol (burst beats with is_first / is_last / burst_id, plus the
 * retry() deny handshake). Internal mesh traversal uses FloonocReqV2.
 */
class NetworkInterfaceV2 : public FloonocNodeV2
{
    friend class NetworkQueueV2;

public:
    static constexpr int NW_REQ   = 0;
    static constexpr int NW_RSP   = 1;
    static constexpr int NW_WIDE  = 2;
    static constexpr int NW_NB    = 3;

    NetworkInterfaceV2(FlooNocV2 *noc, int x, int y, std::string itf_name);

    void reset(bool active);

    void handle_response(FloonocReqV2 *req);
    void unstall_queue(int from_x, int from_y) override;

    bool handle_request(FloonocNodeV2 *node, FloonocReqV2 *req, int from_x, int from_y) override;
    int get_x();
    int get_y();
    void set_router(int nw, RouterV2 *router);
private:
    static void wide_response(vp::Block *__this, vp::IoReq *req);
    static void wide_retry(vp::Block *__this);
    static void narrow_response(vp::Block *__this, vp::IoReq *req);
    static void narrow_retry(vp::Block *__this);
    static vp::IoReqStatus narrow_req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus wide_req(vp::Block *__this, vp::IoReq *req);
    vp::IoReqStatus handle_req(vp::IoReq *req, bool wide);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    int get_req_nw(bool is_wide, bool is_write);
    int get_rsp_nw(bool is_wide, bool is_write);

    FlooNocV2 *noc;
    int ni_outstanding_reqs;
    int x;
    int y;

    vp::IoMaster wide_output_itf;
    vp::IoMaster narrow_output_itf;
    vp::IoSlave wide_input_itf;
    vp::IoSlave narrow_input_itf;

    vp::Trace trace;
    NetworkQueueV2 req_queue;
    NetworkQueueV2 wide_queue;
    NetworkQueueV2 rsp_queue;
    vp::ClockEvent fsm_event;

    vp::Signal<uint64_t> signal_narrow_req;
    vp::Signal<uint64_t> signal_wide_req;

    // True when a downstream target returned DENIED to one of our internal
    // requests; cleared by the corresponding retry callback.
    bool target_stalled;
    FloonocNodeV2 *routers_stalled;
    RouterV2 *router[NW_NB];

    // Pending external bursts. Only one of each (per wide x read/write) can be
    // accepted at a time, matching v1 semantics.
    vp::IoReq *wide_read_pending_burst;
    vp::IoReq *wide_write_pending_burst;
    int wide_read_pending_burst_nb_req;
    int wide_write_pending_burst_nb_req;
    vp::IoReq *narrow_read_pending_burst;
    vp::IoReq *narrow_write_pending_burst;
    int narrow_read_pending_burst_nb_req;
    int narrow_write_pending_burst_nb_req;

    // owes_retry_*_input: true when an incoming req was DENIED and the master
    // is owed a retry() once capacity returns.
    bool owes_retry_wide_input;
    bool owes_retry_narrow_input;

    // When a downstream target returns DENIED, v2 requires the master (this
    // NI) to hold the req and re-send it on the target's retry(). One slot
    // per output port.
    FloonocReqV2 *wide_target_stalled_req;
    FloonocReqV2 *narrow_target_stalled_req;
    // Node to unstall when a target retry frees the corresponding output.
    FloonocNodeV2 *wide_routers_stalled;
    FloonocNodeV2 *narrow_routers_stalled;

    // Synchronous responses are pushed here so they fire after the latency
    // annotation expires.
    vp::Queue response_queue;

    int nb_pending_bursts[2];
};
