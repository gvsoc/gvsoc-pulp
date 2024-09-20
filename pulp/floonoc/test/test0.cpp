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

#include "test0.hpp"


Test0::Test0(Testbench *top)
: TestCommon(top, "test0"), fsm_event(this, Test0::entry)
{
    this->top = top;
}

void Test0::entry(vp::Block *__this, vp::ClockEvent *event)
{
    Test0 *_this = (Test0 *)__this;

    size_t size = 1024*256;
    size_t bw0 = 8, bw1 = 4;

    if (_this->step == 0)
    {
        printf("Test 0, checking stalls, high bandwidth source going to low bandwidth target\n");

        // Check communications from top/left cluster to bottom/right. Target cluster has half
        // the bandwidth of the initiator cluster

        int x0=0, y0=0, x1=_this->top->nb_cluster_x-1, y1=_this->top->nb_cluster_y-1;
        uint64_t base1 = _this->top->get_cluster_base(x1, y1);

        _this->top->get_generator(x0, y0)->start(base1, size, bw0, &_this->fsm_event);
        _this->top->get_receiver(x1, y1)->start(bw1);

        _this->clockstamp = _this->clock.get_cycles();
        _this->step = 1;
    }
    else
    {
        int64_t cycles = _this->clock.get_cycles() - _this->clockstamp;
        int64_t expected = size / bw1;

        printf("    Done (size: %lld, bw: %lld, cycles: %lld, expected: %lld)\n",
            size, bw1, cycles, expected);

        int status = _this->top->check_cycles(cycles, expected);

        _this->top->test_end(status);
    }
}

void Test0::exec_test()
{
    this->step = 0;
    this->fsm_event.enqueue();
}
