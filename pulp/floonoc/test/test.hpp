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
#include <vp/itf/io.hpp>
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

    vp::IoMaster *get_noc_ni_itf(int x, int y, int z = 0);
    uint64_t get_cluster_base(int x, int y, int z = 0);
    TrafficGeneratorConfigMaster *get_generator(int x, int y, int z = 0);
    TrafficReceiverConfigMaster *get_receiver(int x, int y, int z = 0);
    void test_end(int status);
    int check_cycles(int64_t result, int64_t expected);

    int nb_cluster_x;
    int nb_cluster_y;
    int nb_cluster_z;

private:
    int get_cluster_id(int x, int y, int z = 0);
    void exec_next_test();

    vp::Trace trace;
    std::vector<TestCommon *>tests;
    uint64_t cluster_base;
    uint64_t cluster_size;
    int current_test;
    int current_test_step;
    std::vector<vp::IoMaster> noc_ni_itf;
    std::vector<TrafficGeneratorConfigMaster> generator_control_itf;
    std::vector<TrafficReceiverConfigMaster> receiver_control_itf;
};
