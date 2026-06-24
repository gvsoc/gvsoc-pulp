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
#include <vp/itf/io_v2.hpp>
#include "interco/traffic/generator.hpp"
#include "interco/traffic/receiver.hpp"
#include "test0.hpp"

#define CYCLES_ERROR 0.01f

void Testbench::stub_retry(vp::Block *__this, vp::IoRetryChannel) {}
void Testbench::stub_response(vp::Block *__this, vp::IoReq *req) {}

Testbench::Testbench(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_cluster_x = this->get_js_config()->get_int("nb_cluster_x");
    this->nb_cluster_y = this->get_js_config()->get_int("nb_cluster_y");

    this->cluster_base = this->get_js_config()->get_uint("cluster_base");
    this->cluster_size = this->get_js_config()->get_uint("cluster_size");

    this->use_memory = this->get_js_config()->get_child_bool("use_memory");
    this->mem_bw = this->get_js_config()->get_int("mem_bw");

    int nb_cluster = this->nb_cluster_x*this->nb_cluster_y;

    this->noc_ni_itf.resize(nb_cluster);
    this->generator_control_itf.resize(nb_cluster*2);
    this->receiver_control_itf.resize((this->nb_cluster_x + 2) * (this->nb_cluster_y + 2) * 2);

    for (int x=0; x<this->nb_cluster_x; x++)
    {
        for (int y=0; y<this->nb_cluster_y; y++)
        {
            int cid = y*this->nb_cluster_x + x;

            this->new_master_port(
                "generator_control_" + std::to_string(x) + "_" + std::to_string(y) + "_w",
                &this->generator_control_itf[2*cid]);

            this->new_master_port(
                "generator_control_" + std::to_string(x) + "_" + std::to_string(y) + "_n",
                &this->generator_control_itf[2*cid + 1]);

            // v2 IoMaster requires retry+response callbacks at construction.
            // This master is never actively used to send requests; the stubs
            // are there only to satisfy the framework.
            this->noc_ni_itf[cid] = new vp::IoMaster(&Testbench::stub_retry,
                                                    &Testbench::stub_response);
            this->new_master_port(
                "noc_ni_" + std::to_string(x) + "_" + std::to_string(y),
                this->noc_ni_itf[cid]);
        }
    }

    for (int x=0; x<this->nb_cluster_x + 2; x++)
    {
        for (int y=0; y<this->nb_cluster_y + 2; y++)
        {
            this->new_master_port(
                "receiver_control_" + std::to_string(x) + "_" + std::to_string(y) + "_w",
                &this->receiver_control_itf[2* (y * (this->nb_cluster_x + 2) + x)]);

            this->new_master_port(
                "receiver_control_" + std::to_string(x) + "_" + std::to_string(y) + "_n",
                &this->receiver_control_itf[2 * (y * (this->nb_cluster_x + 2) + x) + 1]);
        }
    }

    this->tests.push_back(new Test0(this));
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

uint64_t Testbench::get_target_base(int x, int y)
{
    if (x == 0 || y == 0 | x >= this->nb_cluster_x + 1|| y >= this->nb_cluster_y + 1)
    {
        if (y == this->nb_cluster_y + 1) return 0x90000000;
        else if (y == 0) return 0xA0000000 + 0x100000 * (x-1);
        else if (x == 0) return 0xB0000000 + 0x100000 * (y-1);
        else if (x == this->nb_cluster_x + 1) return 0xC0000000 + 0x100000 * (y-1);
        return 0;
    }
    else
    {
        return this->cluster_base + this->cluster_size * this->get_cluster_id(x-1, y-1);
    }
}

vp::IoMaster *Testbench::get_noc_ni_itf(int x, int y)
{
    return this->noc_ni_itf[this->get_cluster_id(x, y)];
}

TrafficGeneratorConfigMaster *Testbench::get_generator(int x, int y, bool narrow)
{
    return &this->generator_control_itf[2*this->get_cluster_id(x, y) + narrow];
}

TrafficReceiverConfigMaster *Testbench::get_receiver(int x, int y, bool narrow)
{
    return &this->receiver_control_itf[2* (y * (this->nb_cluster_x + 2) + x) + narrow];
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
