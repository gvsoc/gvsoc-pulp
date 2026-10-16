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


#include <memory>
#include <vector>
#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include "floonoc_v2.hpp"
#include "floonoc_link_v2.hpp"
#include <pulp/floonoc_v2/floonoc_v2/floonoc_router_v2_config.hpp>

/**
 * One FlooNoC router.
 *
 * Standalone component instantiated by the generator, once per node and per
 * physical network (req, rsp and wide). Each port is a pair of
 * 'floonoc_link' ports (one input feeding an input queue, one output); the
 * number of ports and their names come from the config. As the RTL
 * floo_router with InFifoDepth = OutFifoDepth = 2: a flit spends one cycle in
 * the input FIFO, one in the output FIFO, both 2 deep, and the RTL
 * floo_wormhole_arbiter of each output moves at most one flit per cycle from
 * the input FIFOs to its output FIFO (wormhole: an output stays with an input
 * until its tail flit).
 *
 * The output port of a request is resolved from its destination node ID with
 * one of the RTL routing algorithms (floo_pkg::route_algo_e):
 * - "xy": dimension-order routing on a 2D mesh. The router has the five ports
 *   right, left, up, down, local (DIR_* indices, same order in floonoc_v2.py)
 *   and knows its own position; node IDs pack mesh positions.
 * - "id_table": the destination ID indexes a routing table giving the output
 *   port, like the RTL IdTable routing. Works on any topology.
 *
 * Ports of absent neighbours are simply left unbound by the generator.
 */
class RouterV2 : public vp::Component
{
public:
    // Direction port indices of an XY router. Must match the DIR_* constants
    // in floonoc_v2.py.
    static constexpr int DIR_RIGHT = 0;
    static constexpr int DIR_LEFT = 1;
    static constexpr int DIR_UP   = 2;
    static constexpr int DIR_DOWN = 3;
    static constexpr int DIR_LOCAL = 4;
    static constexpr int DIR_NB = 5;

    RouterV2(vp::ComponentConf &config);
    ~RouterV2();

    void reset(bool active);

private:
    // Link input callback: push the request into the input queue identified
    // by the port id, return true when the queue is full (the
    // sender must then hold off until unstalled).
    static bool link_req(vp::Block *__this, FloonocReqV2 *req, int queue_index);
    // Link output callback: the downstream node accepts requests again on the
    // identified output.
    static void link_unstall(vp::Block *__this, int output_id);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Output port a request to node dest_id must leave through.
    int get_output_port(int dest_id);
    // XY routing: next position on the way to dest_id and the port facing it.
    void get_next_router_pos(int dest_id, int &next_x, int &next_y);
    int get_req_queue(int from_x, int from_y);

    FloonocRouterV2Config cfg;
    vp::Trace trace;
    bool xy_routing;
    int nb_ports;
    // Input queue capacity per port: the configured queue size plus one slot
    // per link pipeline stage (a stage register holds one flit in flight).
    std::vector<int> queue_capacity;
    // Extra cycles a flit spends on the link feeding each input port.
    std::vector<int> input_stages;
    // ID-table routing: destination node ID -> output port, -1 if unrouted.
    std::vector<int> routing_table;
    std::vector<vp::Queue *> input_queues;
    // Per output: the RTL output FIFO (OutFifoDepth), between the arbiter and the link.
    std::vector<vp::Queue *> output_queues;
    static constexpr int OUTPUT_DEPTH = 2;
    // Per output: cycle the next node freed a slot of its full input. As the
    // registered ready of the RTL spill register, it is only usable from the
    // next cycle.
    std::vector<int64_t> unstall_cycle;
    // RTL floo_wormhole_arbiter state, per output: the request vector
    // latched at the last tail flit (valid_q, last_q) and its rr_arb_tree
    // (FairArb, LockIn): priority rr_q, locked request vector req_q.
    struct OutputArbiter
    {
        uint32_t valid_q = 0;
        bool last_q = false;
        uint32_t req_q = 0;
        bool lock_q = false;
        int rr_q = 0;
    };
    std::vector<OutputArbiter> arbiters;
    int arb_levels = 0;
    // rr_arb_tree selection of the request vector req with priority rr
    int arb_select(uint32_t req, int rr);
    int arb_next(uint32_t req, int rr);
    // Per-cycle scratch of the FSM, allocated once
    std::vector<bool> scratch_output_full;
    std::vector<int> scratch_head_output;
    std::vector<uint32_t> scratch_valid_in;
    std::vector<std::unique_ptr<FloonocLinkSlave>> input_ports;
    std::vector<std::unique_ptr<FloonocLinkMaster>> output_ports;
    vp::ClockEvent fsm_event;
    std::vector<std::unique_ptr<vp::Signal<bool>>> stalled_queues;
    vp::Signal<uint64_t> signal_req;
    vp::Signal<uint64_t> signal_req_size;
    vp::Signal<bool> signal_req_is_write;
};
