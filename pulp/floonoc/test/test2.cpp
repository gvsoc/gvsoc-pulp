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

#include "test2.hpp"



Test2::Test2(Testbench *top)
: TestCommon(top, "Test2"), fsm_event(this, Test2::entry)
{
    this->top = top;
}


void Test2::entry(vp::Block *__this, vp::ClockEvent *event)
{
    Test2 *_this = (Test2 *)__this;

    // One generator on top/right, another one on bottom/left and the receiver on bottom/right
    int x0=_this->top->nb_cluster_x-1, y0=0;
    int x1=0, y1=_this->top->nb_cluster_y-1;
    int x2=_this->top->nb_cluster_x-1, y2=_this->top->nb_cluster_y-1;
    size_t size = 1024*256;
    size_t bw1 = 4;
    size_t bw2 = 8;

    if (_this->step == 0)
    {
        printf("Test 2, checking 2 generators going to same receiver\n");

        uint64_t base = _this->top->get_cluster_base(x2, y2);

        _this->top->get_generator(x0, y0)->start(base, size, bw1, &_this->fsm_event);
        _this->top->get_generator(x1, y1)->start(base, size, bw1, &_this->fsm_event);
        _this->top->get_receiver(x2, y2)->start(bw2);

        _this->clockstamp = _this->clock.get_cycles();
        _this->step = 1;
    }
    else
    {
        bool is_finished = true;

        is_finished &= _this->top->get_generator(x0, y0)->is_finished();
        is_finished &= _this->top->get_generator(x1, y1)->is_finished();

        if (is_finished)
        {
            int64_t cycles = _this->clock.get_cycles() - _this->clockstamp;
            // Since we have 2 generators going to same target, bandwidth is divided by 2
            int64_t expected = size / (bw1 / 2);

            printf("    Done (size: %lld, bw: %lld, cycles: %lld, expected: %lld)\n",
                size, bw1, cycles, expected);

            int status = _this->top->check_cycles(cycles, expected);

            _this->top->test_end(status);
        }
    }
}

void Test2::exec_test()
{
    this->step = 0;
    this->fsm_event.enqueue();
}
