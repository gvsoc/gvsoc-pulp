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

class Router;
class NetworkInterface;


class Entry
{
public:
    uint64_t base;
    uint64_t size;
    int x;
    int y;
};



class FlooNoc : public vp::Component
{
    friend class NetworkInterface;

public:
    FlooNoc(vp::ComponentConf &config);

    void reset(bool active);

    Router *get_router(int x, int y);
    vp::IoMaster *get_target(int x, int y);
    NetworkInterface *get_network_interface(int x, int y);
    void handle_request_end(vp::IoReq *req);

    static constexpr int REQ_DEST_NI = 0;
    static constexpr int REQ_DEST_BURST = 1;
    static constexpr int REQ_DEST_BASE = 2;
    static constexpr int REQ_DEST_X = 3;
    static constexpr int REQ_DEST_Y = 4;
    static constexpr int REQ_ROUTER = 5;
    static constexpr int REQ_QUEUE = 6;
    static constexpr int REQ_NB_ARGS = 7;

    static constexpr int DIR_RIGHT = 0;
    static constexpr int DIR_LEFT = 1;
    static constexpr int DIR_UP   = 2;
    static constexpr int DIR_DOWN = 3;
    static constexpr int DIR_LOCAL = 4;

private:
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;

    uint64_t width;
    std::vector<Entry> entries;
    int dim_x;
    int dim_y;
    int router_input_queue_size;
    std::vector<Router *> routers;
    std::vector<vp::IoMaster *> targets;
    std::vector<NetworkInterface *> network_interfaces;
};
