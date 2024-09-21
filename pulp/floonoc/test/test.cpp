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

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "interco/traffic/generator.hpp"
#include "interco/traffic/receiver.hpp"
#include "test0.hpp"
#include "test1.hpp"
#include "test2.hpp"

#define CYCLES_ERROR 0.01f

Testbench::Testbench(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_cluster_x = this->get_js_config()->get_int("nb_cluster_x");
    this->nb_cluster_y = this->get_js_config()->get_int("nb_cluster_y");

    this->cluster_base = this->get_js_config()->get_uint("cluster_base");
    this->cluster_size = this->get_js_config()->get_uint("cluster_size");

    int nb_cluster = this->nb_cluster_x*this->nb_cluster_y;

    this->noc_ni_itf.resize(nb_cluster);
    this->generator_control_itf.resize(nb_cluster);
    this->receiver_control_itf.resize(nb_cluster);

    for (int x=0; x<this->nb_cluster_x; x++)
    {
        for (int y=0; y<this->nb_cluster_y; y++)
        {
            int cid = y*this->nb_cluster_x + x;

            this->new_master_port(
                "generator_control_" + std::to_string(x) + "_" + std::to_string(y),
                &this->generator_control_itf[cid]);

            this->new_master_port(
                "receiver_control_" + std::to_string(x) + "_" + std::to_string(y),
                &this->receiver_control_itf[cid]);

            this->new_master_port(
                "noc_ni_" + std::to_string(x) + "_" + std::to_string(y),
                &this->noc_ni_itf[cid]);
        }
    }

    this->tests.push_back(new Test0(this));
    this->tests.push_back(new Test1(this));
    this->tests.push_back(new Test2(this));
}

void Testbench::reset(bool active)
{
    if (!active)
    {
        this->current_test = 0;
        this->current_test_step = 0;
        this->exec_next_test();
    }
}

void Testbench::exec_next_test()
{
    if (this->current_test == this->tests.size())
    {
        this->time.get_engine()->quit(0);
    }
    else
    {
        this->tests[this->current_test++]->exec_test();
    }
}

int Testbench::get_cluster_id(int x, int y)
{
    return this->nb_cluster_x * y + x;
}

uint64_t Testbench::get_cluster_base(int x, int y)
{
    return this->cluster_base + this->cluster_size * this->get_cluster_id(x, y);
}

vp::IoMaster *Testbench::get_noc_ni_itf(int x, int y)
{
    return &this->noc_ni_itf[this->get_cluster_id(x, y)];
}

TrafficGeneratorConfigMaster *Testbench::get_generator(int x, int y)
{
    return &this->generator_control_itf[this->get_cluster_id(x, y)];
}

TrafficReceiverConfigMaster *Testbench::get_receiver(int x, int y)
{
    return &this->receiver_control_itf[this->get_cluster_id(x, y)];
}

void Testbench::test_end(int status)
{
    if (status != 0)
    {
        this->time.get_engine()->quit(status);
    }
    else
    {
        this->exec_next_test();
    }
}

int Testbench::check_cycles(int64_t result, int64_t expected)
{
    float error = ((float)std::abs(result - expected)) / expected;

    return error > CYCLES_ERROR;
}

TestCommon::TestCommon(Block *parent, std::string name)
: Block(parent, name)
{

}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Testbench(config);
}
