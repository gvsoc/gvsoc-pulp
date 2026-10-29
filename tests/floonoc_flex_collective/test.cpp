/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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

// Driver for floonoc_flex's collective (multicast/reduction) test
// Three scenarios run back to back on the 4x4 mesh built by test.py: 
// a plain unicast transfer, a broadcast to the y=0 row, and an ADD/uint16
// reduction from that same row back to its root, the three using narrow link only.

#include <cstdio>
#include <cstring>
#include <vp/vp.hpp>
#include "interco/traffic/generator.hpp"
#include "pulp/floonoc_flex/floonoc_flex.hpp"

#define MESH_DIM 4
#define NODE(x, y) ((y) * MESH_DIM + (x))
#define PACKET_SIZE 64

static uint64_t node_addr(uint64_t base, uint64_t node_size, int x, int y)
{
    return base + (uint64_t)NODE(x, y) * node_size;
}

class CollectiveTestbench : public vp::Component
{
public:
    CollectiveTestbench(vp::ComponentConf &config);

    void reset(bool active);

private:
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::Trace trace;
    vp::ClockEvent fsm_event;
    std::vector<TrafficGeneratorConfigMaster> narrow_control_itf;
    std::vector<TrafficGeneratorConfigMaster> wide_control_itf;

    uint64_t node_size;
    uint64_t node_base;
    int step = 0;
    bool status = false;

    // Scratch buffers kept alive across phases.
    uint8_t broadcast_pattern[PACKET_SIZE];
    uint8_t reduction_pattern[MESH_DIM][PACKET_SIZE];
    uint8_t reduction_expected[PACKET_SIZE];
};

CollectiveTestbench::CollectiveTestbench(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, CollectiveTestbench::fsm_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->node_size = this->get_js_config()->get_uint("node_size");
    this->node_base = this->get_js_config()->get_uint("node_base");

    int nb_nodes = MESH_DIM * MESH_DIM;
    this->narrow_control_itf.resize(nb_nodes);
    this->wide_control_itf.resize(nb_nodes);
    for (int node = 0; node < nb_nodes; node++)
    {
        this->new_master_port("generator_control_" + std::to_string(node) + "_narrow",
            &this->narrow_control_itf[node]);
        this->new_master_port("generator_control_" + std::to_string(node) + "_wide",
            &this->wide_control_itf[node]);
    }

    // Known broadcast pattern.
    for (int i = 0; i < PACKET_SIZE; i++)
    {
        this->broadcast_pattern[i] = (uint8_t)(0xA0 + i);
    }

    // Four distinct per-contributor uint16 patterns (one per y=0 row
    // member), and their elementwise ADD reduction for reference
    uint16_t *expected16 = (uint16_t *)this->reduction_expected;
    for (int i = 0; i < PACKET_SIZE / 2; i++)
    {
        expected16[i] = 0;
    }
    for (int m = 0; m < MESH_DIM; m++)
    {
        uint16_t *pattern16 = (uint16_t *)this->reduction_pattern[m];
        for (int i = 0; i < PACKET_SIZE / 2; i++)
        {
            pattern16[i] = (uint16_t)(m * 1000 + i);
            expected16[i] = (uint16_t)(expected16[i] + pattern16[i]);
        }
    }
}

void CollectiveTestbench::reset(bool active)
{
    if (!active)
    {
        this->step = 0;
        this->status = false;
        this->fsm_event.enqueue();
    }
}

void CollectiveTestbench::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    CollectiveTestbench *_this = (CollectiveTestbench *)__this;

    switch (_this->step)
    {
        case 0:
        {
            // Phase 0: plain unicast write+readback, (0,0) -> (3,3).
            printf("[floonoc_flex_collective] unicast_regression\n");
            TrafficGeneratorSync *sync = new TrafficGeneratorSync(&_this->fsm_event);
            uint64_t target = node_addr(_this->node_base, _this->node_size, 3, 3);
            _this->narrow_control_itf[NODE(0, 0)].start(target, PACKET_SIZE, PACKET_SIZE,
                sync, /*do_write=*/true, /*check=*/true);
            sync->start();
            _this->step = 1;
            break;
        }

        case 1:
        {
            bool check_status = false;
            _this->narrow_control_itf[NODE(0, 0)].get_result(&check_status, nullptr);
            printf("[floonoc_flex_collective] unicast_regression: %s\n",
                check_status ? "FAILED" : "passed");
            _this->status |= check_status;

            // Phase 1: broadcast write, root (0,0), mask = row y=0
            // (members (0,0)..(3,0)). 
            printf("[floonoc_flex_collective] broadcast_subrow: write\n");
            TrafficGeneratorSync *sync = new TrafficGeneratorSync(&_this->fsm_event);
            uint64_t target = node_addr(_this->node_base, _this->node_size, 0, 0);
            uint64_t mask = FlooNoc::coll_mask_set_axis(0, 0, 0x3); // axis0 (x): full row
            _this->narrow_control_itf[NODE(0, 0)].start(target, PACKET_SIZE, PACKET_SIZE,
                sync, /*do_write=*/true, /*check=*/false, FlooNoc::COLL_OP_MULTICAST,
                mask, /*expected_data=*/nullptr, _this->broadcast_pattern);
            sync->start();
            _this->step = 2;
            break;
        }

        case 2:
        {
            // Phase 2: one plain unicast check-read per row member, each
            // comparing against the same broadcast_pattern.
            printf("[floonoc_flex_collective] broadcast_subrow: check\n");
            TrafficGeneratorSync *sync = new TrafficGeneratorSync(&_this->fsm_event);
            for (int x = 0; x < MESH_DIM; x++)
            {
                uint64_t target = node_addr(_this->node_base, _this->node_size, x, 0);
                _this->narrow_control_itf[NODE(x, 0)].start(target, PACKET_SIZE,
                    PACKET_SIZE, sync, /*do_write=*/false, /*check=*/true, 0, 0,
                    _this->broadcast_pattern);
            }
            sync->start();
            _this->step = 3;
            break;
        }

        case 3:
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                bool check_status = false;
                _this->narrow_control_itf[NODE(x, 0)].get_result(&check_status, nullptr);
                printf("[floonoc_flex_collective] broadcast_subrow: node (%d,0) %s\n",
                    x, check_status ? "FAILED" : "passed");
                _this->status |= check_status;
            }

            // Phase 3: reduction setup: each row member writes its own
            // distinct pattern to its own tile.
            printf("[floonoc_flex_collective] reduction_add_uint16: setup\n");
            TrafficGeneratorSync *sync = new TrafficGeneratorSync(&_this->fsm_event);
            for (int x = 0; x < MESH_DIM; x++)
            {
                uint64_t target = node_addr(_this->node_base, _this->node_size, x, 0);
                _this->narrow_control_itf[NODE(x, 0)].start(target, PACKET_SIZE,
                    PACKET_SIZE, sync, /*do_write=*/true, /*check=*/false, 0, 0,
                    nullptr, _this->reduction_pattern[x]);
            }
            sync->start();
            _this->step = 4;
            break;
        }

        case 4:
        {
            // Phase 4: reduction gather: root (0,0) reads with
            // COLL_OP_REDADD_UINT16 over the same row mask; expected_data
            // is the host-computed elementwise sum of the 4 patterns above.
            printf("[floonoc_flex_collective] reduction_add_uint16: gather\n");
            TrafficGeneratorSync *sync = new TrafficGeneratorSync(&_this->fsm_event);
            uint64_t target = node_addr(_this->node_base, _this->node_size, 0, 0);
            uint64_t mask = FlooNoc::coll_mask_set_axis(0, 0, 0x3);
            _this->narrow_control_itf[NODE(0, 0)].start(target, PACKET_SIZE, PACKET_SIZE,
                sync, /*do_write=*/false, /*check=*/true, FlooNoc::COLL_OP_REDADD_UINT16,
                mask, _this->reduction_expected);
            sync->start();
            _this->step = 5;
            break;
        }

        case 5:
        {
            bool check_status = false;
            _this->narrow_control_itf[NODE(0, 0)].get_result(&check_status, nullptr);
            printf("[floonoc_flex_collective] reduction_add_uint16: %s\n",
                check_status ? "FAILED" : "passed");
            _this->status |= check_status;

            _this->time.get_engine()->quit(_this->status);
            break;
        }
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new CollectiveTestbench(config);
}
