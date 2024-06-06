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
 */

#pragma once

#include <vp/vp.hpp>

class FlooNoc;

class Router : public vp::Block
{
public:
    Router(FlooNoc *noc, int x, int y, int queue_size);

    void reset(bool active);

    bool handle_request(vp::IoReq *req, int from_x, int from_y);
    void grant(vp::IoReq *req);

private:
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    void send_to_target(vp::IoReq *req, int pos_x, int pos_y);
    void get_next_router_pos(int dest_x, int dest_y, int &next_x, int &next_y);
    int get_req_queue(int from_x, int from_y);
    void get_pos_from_queue(int queue, int &pos_x, int &pos_y);
    void unstall_queue(int from_x, int from_y);

    FlooNoc *noc;
    vp::Trace trace;
    int x;
    int y;
    int queue_size;
    std::queue<vp::IoReq *> input_queues[5];
    vp::ClockEvent fsm_event;
    int current_queue;
    bool stalled_queues[5];
};
