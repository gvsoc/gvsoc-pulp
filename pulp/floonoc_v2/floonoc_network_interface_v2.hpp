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
#include <map>
#include "floonoc_v2.hpp"
#include "floonoc_link_v2.hpp"
#include <pulp/floonoc_v2/floonoc_v2/floonoc_network_interface_v2_config.hpp>

class NetworkInterfaceV2;

/**
 * Per-network injection queue of the NI. Splits external bursts into mesh
 * requests and feeds them to the NI's link output for its network (req, rsp
 * or wide).
 *
 * The link is shared by several inputs, one per channel it carries, arbitrated
 * round-robin per packet. Each input holds at most INPUT_DEPTH flits: a full
 * input refuses the next request, so contention reaches the initiator (request
 * inputs: the request is denied and retried) or the destination target
 * (response inputs: its response is denied and retried, if the target supports
 * it).
 */
class NetworkQueueV2 : public vp::Block
{
    friend class NetworkInterfaceV2;
public:
    NetworkQueueV2(NetworkInterfaceV2 &ni, std::string name, uint64_t width, int nw);
    void reset(bool active) override;

    // Inputs of the req link
    static constexpr int IN_REQ_NARROW_W = 0;
    static constexpr int IN_REQ_NARROW_AR = 1;
    static constexpr int IN_REQ_WIDE_AR = 2;
    // Inputs of the wide link
    static constexpr int IN_WIDE_W = 0;
    static constexpr int IN_WIDE_R = 1;
    // Inputs of the rsp link
    static constexpr int IN_RSP_NARROW_B = 0;
    static constexpr int IN_RSP_NARROW_R = 1;
    static constexpr int IN_RSP_WIDE_B = 2;
    static constexpr int MAX_INPUTS = 3;
    // Flits an input holds: the two entries a link stage needs to stream one
    // flit per cycle while ready is registered.
    static constexpr size_t INPUT_DEPTH = 2;

    void check();
    // The flits go to the arbiter input given by the caller (IN_*), the one
    // it reserved.
    void handle_req(vp::IoReq *req, bool wide, int input);
    void handle_rsp(FloonocReqV2 *req, bool is_address, int input);
    // Whether the input can take a flit. If not, the input remembers that its
    // producer is owed a notification (NetworkInterfaceV2::input_room) when
    // it can.
    bool reserve(int input);

private:
    void enqueue_router_req(vp::IoReq *req, bool is_address, bool wide, bool is_req, int input);
    void enqueue_router_rsp(FloonocReqV2 *req, bool is_address, int input);
    int next_input();
    void send_router_req(int input);
    void unstall();

    NetworkInterfaceV2 &ni;
    uint64_t width;
    // Network this queue injects into (NetworkInterfaceV2::NW_*), i.e. which
    // of the NI's link output ports it drives.
    int nw;
    vp::Trace trace;
    int nb_inputs;
    std::queue<FloonocReqV2 *> inputs[MAX_INPUTS];
    // Input owning the link in the middle of a packet, -1 between packets.
    int locked_input = -1;
    // Input that has priority for the next packet.
    int rr_input = 0;
    // Inputs which refused a flit and owe a notification when they have room.
    bool owes_room[MAX_INPUTS];
    bool stalled;
};

/**
 * v2 FlooNoC network interface.
 *
 * Standalone component instantiated by the generator: entry/exit point of the
 * network, identified by its node ID. External ports speak the v2 io protocol (burst beats with is_first /
 * is_last / burst_id, plus the retry() deny handshake). Mesh traversal uses
 * FloonocReqV2 over 'floonoc_link' ports bound to the local (or, for border
 * NIs, nearest) routers of the three physical networks.
 */
class NetworkInterfaceV2 : public vp::Component
{
    friend class NetworkQueueV2;

public:
    static constexpr int NW_REQ   = 0;
    static constexpr int NW_RSP   = 1;
    static constexpr int NW_WIDE  = 2;
    static constexpr int NW_NB    = 3;

    NetworkInterfaceV2(vp::ComponentConf &config);

    void reset(bool active);

    void handle_response(FloonocReqV2 *req);
    // Recover our own FloonocReqV2 from a downstream response beat (initiator-owned
    // request convention): folds a distinct beat's payload onto our request and
    // frees the beat; passes a round-tripped write ack through unchanged.
    FloonocReqV2 *unwrap_response(vp::IoReq *req);

private:
    // Link input callback: a mesh request (or response) delivered by a router.
    // Returns true when the NI's downstream target denied it (the router must
    // then stall the corresponding output until unstalled).
    static bool link_req(vp::Block *__this, FloonocReqV2 *req, int nw);
    // Link output callback: the router accepts injections again on network nw.
    static void link_unstall(vp::Block *__this, int nw);
    static vp::IoRespAck wide_response(vp::Block *__this, vp::IoReq *req);
    static void wide_retry(vp::Block *__this, vp::IoRetryChannel);
    static vp::IoRespAck narrow_response(vp::Block *__this, vp::IoReq *req);
    static void narrow_retry(vp::Block *__this, vp::IoRetryChannel);
    static vp::IoReqStatus narrow_req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus wide_req(vp::Block *__this, vp::IoReq *req);
    vp::IoReqStatus handle_req(vp::IoReq *req, bool wide);
    // Destination side: build the object forwarded to the local target for one
    // mesh request flit. Write data flits hand the encapsulated external write
    // beat itself to the target (ownership travelled with the flit; the target
    // consumes and frees it under the write-ack contract); reads and atomics
    // forward the flit itself.
    vp::IoReq *make_target_req(FloonocReqV2 *req);
    // Destination side: send (or re-send, from a retry) a request built by
    // make_target_req to the local target and handle the inline outcomes.
    // Returns true when the target denied it (the caller keeps it in its
    // stalled slot and stalls the link it came in on).
    bool send_to_target(vp::IoReq *to_send, bool wide);
    // Destination side: forward a request flit delivered on link nw to the
    // local target, once the target has room for one more transaction.
    // Returns true when it is held (its link must stall until it is sent).
    bool forward_to_target(FloonocReqV2 *flit, int nw);
    // Whether a request flit gets a response from the target, i.e. holds an
    // entry of the target-side transaction limit until it comes back.
    static bool expects_response(FloonocReqV2 *flit);
    // Send the held flits which waited for room in the transaction limit.
    void forward_held();
    // Whether the arbiter input of a target response has room. If not, the
    // target is owed a resp_retry().
    bool accept_response(vp::IoReq *req, bool wide);
    // The target is ready again: re-send the requests it denied.
    void retry_target(bool wide);
    // Input `input` of the queue of link nw has room again.
    void input_room(int nw, int input);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    int get_req_nw(bool is_wide, bool is_write);
    int get_rsp_nw(bool is_wide, bool is_write);
    EntryV2 *get_entry(uint64_t base, uint64_t size);

    FloonocNetworkInterfaceV2Config cfg;

    int ni_outstanding_reqs;
    // Max input burst size / boundary a burst may not cross (AXI 4KB rule); 0
    // disables the burst-legality checks.
    uint64_t max_burst_size;
    uint64_t narrow_width;
    uint64_t wide_width;

    // Memory map, address range -> destination node. Every NI holds the full
    // table.
    std::vector<EntryV2> entries;

    vp::IoMaster wide_output_itf;
    vp::IoMaster narrow_output_itf;
    vp::IoSlave wide_input_itf;
    vp::IoSlave narrow_input_itf;

    // Pools serving the read response beats delivered upstream to the external
    // masters (payload co-allocated), indexed by the burst's wide flag:
    // [0] = narrow_width, [1] = wide_width.
    vp::IoReqAllocator *rsp_allocator[2];

    // Size-0 (data-less) pools for the per-burst write-ack contract:
    // - ack_allocator serves the single write burst ack sent upstream to the
    //   external master once every beat of a burst has its B flit back.
    // - fwd_wr_allocator serves the destination-side wrapper beats of the
    //   SPLIT write path only (a beat spread over several W flits keeps its
    //   buffer alive at the source, so each flit is forwarded wrapped in a
    //   distinct consumable pool beat). A 1:1 beat (owns_beat) needs no
    //   wrapper: the encapsulated external beat itself is handed out.
    vp::IoReqAllocator *ack_allocator;
    vp::IoReqAllocator *fwd_wr_allocator;

    // Shared pool of FloonocReqV2 mesh flits (see FloonocReqV2Allocator),
    // replacing bare new/delete on the flit lifecycle.
    FloonocReqV2Allocator *flit_allocator;

    // Initiator side, per-burst write tracking (io_v2 write-acknowledgement):
    // incoming write beats travel the mesh inside their W flit and are
    // consumed and freed by the destination target; the B flits carry the
    // accounting back and the burst is acknowledged upstream exactly once,
    // keyed by burst_id per input port ([0] = narrow, [1] = wide). A lone
    // beat with burst_id == -1 bypasses the map (it completes on its own B).
    struct WrTrack
    {
        uint64_t issued_bytes = 0;
        uint64_t acked_bytes = 0;
        bool seen_last = false;
        bool error = false;
        void *initiator = nullptr;
        uint64_t base = 0;
    };
    std::map<int64_t, WrTrack> wr_bursts[2];

    // Mesh links, indexed by NW_*: outputs inject into the routers, inputs
    // receive what the routers deliver.
    std::array<FloonocLinkMaster, NW_NB> link_out;
    std::array<FloonocLinkSlave, NW_NB> link_in;

    vp::Trace trace;
    NetworkQueueV2 req_queue;
    NetworkQueueV2 wide_queue;
    NetworkQueueV2 rsp_queue;
    vp::ClockEvent fsm_event;

    vp::Signal<uint64_t> signal_narrow_req;
    vp::Signal<uint64_t> signal_wide_req;

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

    // Request flits held for the local targets, per target port ([0] =
    // narrow, [1] = wide) and per link they arrived on, their link stalled
    // until they are sent: one may come on each link (a wide read request on
    // the req link, a wide write beat on the wide link). `to_send` is what
    // make_target_req built, denied by the target and re-sent on its retry:
    // the flit itself for reads/atomics, the encapsulated external write beat
    // for write data (the flit then stays reachable via beat->initiator).
    // NULL while the flit waits for room for its response.
    struct HeldReq
    {
        FloonocReqV2 *flit = nullptr;
        vp::IoReq *to_send = nullptr;
    };
    HeldReq held[2][NW_NB];
    // Destination side, transactions forwarded to the local targets and not
    // answered yet, per target port and [0] = read, [1] = write (atomics with
    // the writes), at most ni_outstanding_reqs of each.
    int target_txns[2][2];
    // A transaction completed: the held flits may be forwarded.
    bool forward_held_pending;
    // Target port whose response was denied and is owed a resp_retry().
    bool owes_resp_retry[2];

    // Synchronous responses are pushed here so they fire after the latency
    // annotation expires.
    vp::Queue response_queue;

    int nb_pending_bursts[2];
};
