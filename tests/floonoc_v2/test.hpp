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
#include <vp/itf/io_v2.hpp>
#include "interco/traffic/generator.hpp"
#include "interco/traffic/receiver.hpp"

class TestCommon : public vp::Block
{
public:
    TestCommon(Block *parent, std::string name);
    virtual void exec_test() = 0;
};


class Testbench : public vp::Component
{
public:
    Testbench(vp::ComponentConf &config);

    void reset(bool active);

    vp::IoMaster *get_noc_ni_itf(int x, int y);
    uint64_t get_cluster_base(int x, int y);
    uint64_t get_target_base(int x, int y);
    TrafficGeneratorConfigMaster *get_generator(int x, int y, bool narrow=false);
    TrafficReceiverConfigMaster *get_receiver(int x, int y, bool narrow=false);
    void test_end(int status);
    int check_cycles(int64_t result, int64_t expected);

    int nb_cluster_x;
    int nb_cluster_y;
    bool use_memory;
    int mem_bw;

private:
    int get_cluster_id(int x, int y);
    void exec_next_test();

    // Stubs for the v2 IoMaster: this test harness never actually sends through
    // noc_ni_itf — those ports exist only as a structural binding. v2's
    // IoMaster requires retry and response callbacks at construction time.
    static void stub_retry(vp::Block *__this);
    static void stub_response(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    std::vector<TestCommon *>tests;
    uint64_t cluster_base;
    uint64_t cluster_size;
    int current_test;
    int current_test_step;
    std::vector<vp::IoMaster *> noc_ni_itf;
    std::vector<TrafficGeneratorConfigMaster> generator_control_itf;
    std::vector<TrafficReceiverConfigMaster> receiver_control_itf;
};
