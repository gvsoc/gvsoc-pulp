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

class NetworkInterface : public vp::Block
{
public:
    NetworkInterface(FlooNoc *noc, int x, int y);

    void reset(bool active);

    void handle_response(vp::IoReq *req);
    void unstall_queue(int from_x, int from_y);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    void check_state();

    FlooNoc *noc;
    int x;
    int y;
    vp::IoSlave input_itf;
    vp::Trace trace;
    std::queue<vp::IoReq *> pending_bursts;
    uint64_t pending_burst_base;
    uint64_t pending_burst_size;
    uint8_t *pending_burst_data;
    vp::ClockEvent fsm_event;
    std::queue<vp::IoReq *> free_reqs;
    bool stalled;
};
