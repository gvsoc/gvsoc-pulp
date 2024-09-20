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

#include "test1.hpp"



Test1::Test1(Testbench *top)
: TestCommon(top, "Test1"), fsm_event(this, Test1::entry)
{
    this->top = top;
}


void Test1::entry(vp::Block *__this, vp::ClockEvent *event)
{
    Test1 *_this = (Test1 *)__this;

    // One horizontal and one vertical paths, both in the middle, so that they cross in the center
    // of the grid
    int x0=(_this->top->nb_cluster_x-1)/2, y0=0;
    int x1=(_this->top->nb_cluster_x-1)/2, y1=_this->top->nb_cluster_y-1;
    int x2=0, y2=(_this->top->nb_cluster_y-1)/2;
    int x3=_this->top->nb_cluster_x-1, y3=(_this->top->nb_cluster_y-1)/2;
    size_t size = 1024*256;
    size_t bw = 8;

    if (_this->step == 0)
    {
        printf("Test 1, checking 2 data streams going through same router but using different paths\n");

        uint64_t base1 = _this->top->get_cluster_base(x1, y1);
        uint64_t base3 = _this->top->get_cluster_base(x3, y3);

        _this->top->get_generator(x0, y0)->start(base1, size, bw, &_this->fsm_event);
        _this->top->get_receiver(x1, y1)->start(bw);
        _this->top->get_generator(x2, y2)->start(base3, size, bw, &_this->fsm_event);
        _this->top->get_receiver(x3, y3)->start(bw);

        _this->clockstamp = _this->clock.get_cycles();
        _this->step = 1;
    }
    else
    {
        bool is_finished = true;

        is_finished &= _this->top->get_generator(x0, y0)->is_finished();
        is_finished &= _this->top->get_generator(x2, y2)->is_finished();

        if (is_finished)
        {
            int64_t cycles = _this->clock.get_cycles() - _this->clockstamp;
            int64_t expected = size / bw;

            printf("    Done (size: %lld, bw: %lld, cycles: %lld, expected: %lld)\n",
                size, bw, cycles, expected);

            int status = _this->top->check_cycles(cycles, expected);

            _this->top->test_end(status);
        }
    }
}

void Test1::exec_test()
{
    this->step = 0;
    this->fsm_event.enqueue();
}
