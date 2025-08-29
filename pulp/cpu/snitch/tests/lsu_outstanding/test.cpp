/*
 * Copyright (C) 2025 ETH Zurich and University of Bologna
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
#include "test.hpp"

#define CYCLES_ERROR 0.01f

Testbench::Testbench(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

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
