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

#include <array>
#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include "floonoc_v2.hpp"

class FlooNocV2;

class RouterQueueV2
{
public:
    RouterQueueV2(vp::Block *parent, std::string name, vp::ClockEvent *ready_event=NULL);
    vp::Queue queue;
    FloonocNodeV2 *stalled_node;
};

class RouterV2 : public FloonocNodeV2
{
public:
    RouterV2(FlooNocV2 *noc, std::string name, int x, int y, int queue_size);
    ~RouterV2();

    void reset(bool active);

    bool handle_request(FloonocNodeV2 *node, FloonocReqV2 *req, int from_x, int from_y) override;
    void unstall_queue(int from_x, int from_y) override;
    void stall_queue(int from_x, int from_y);
    void set_neighbour(int dir, FloonocNodeV2 *node);

    int x;
    int y;

private:
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    void get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y);
    int get_req_queue(int from_x, int from_y);
    void get_pos_from_queue(int queue, int &pos_x, int &pos_y);

    FlooNocV2 *noc;
    vp::Trace trace;
    int queue_size;
    RouterQueueV2 *input_queues[5];
    FloonocNodeV2 *output_nodes[5];
    vp::ClockEvent fsm_event;
    int current_queue;
    std::array<vp::Signal<bool>, 5> stalled_queues;
    vp::Signal<uint64_t> signal_req;
    vp::Signal<uint64_t> signal_req_size;
    vp::Signal<bool> signal_req_is_write;
};
