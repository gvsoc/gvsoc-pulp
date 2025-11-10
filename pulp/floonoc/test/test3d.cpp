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

#include "test3d.hpp"



Test3D::Test3D(Testbench *top)
: TestCommon(top, "Test3D"), fsm_event(this, Test3D::entry)
{
    this->top = top;
}


void Test3D::entry(vp::Block *__this, vp::ClockEvent *event)
{
    Test3D *_this = (Test3D *)__this;

    // One generator on top/right, another one on bottom/left, at Z=0 at Z=2
    int x0=_this->top->nb_cluster_x-1, y0=0, z0=0;
    int x1=0, y1=_this->top->nb_cluster_y-1, z1=2;
    // Receivers in reverse locations
    int x2=0, y2=_this->top->nb_cluster_y-1, z2=2;
    int x3=_this->top->nb_cluster_x-1, y3=0, z3=0;
    size_t size = 1024*256;
    size_t bw = 8;

    if (_this->step == 0)
    {
        printf("Test 3D, checking two generators going to two receivers through two Z hops\n");

        uint64_t base2 = _this->top->get_cluster_base(x2, y2, z2);
        uint64_t base3 = _this->top->get_cluster_base(x3, y3, z3);

        _this->top->get_generator(x0, y0, z0)->start(base2, size, bw, &_this->fsm_event);
        _this->top->get_generator(x1, y1, z1)->start(base3, size, bw, &_this->fsm_event);
        _this->top->get_receiver(x2, y2, z2)->start(bw);
        _this->top->get_receiver(x3, y3, z3)->start(bw);

        _this->clockstamp = _this->clock.get_cycles();
        _this->step = 1;
    }
    else
    {
        bool is_finished = true;

        is_finished &= _this->top->get_generator(x0, y0, z0)->is_finished();
        is_finished &= _this->top->get_generator(x1, y1, z1)->is_finished();

        // TODO below
        if (is_finished)
        {
            int64_t cycles = _this->clock.get_cycles() - _this->clockstamp;
            // Since we have 2 generators going to same target, bandwidth is divided by 2
            int64_t expected = size / (bw / 2);

            printf("    Done (size: %lld, bw: %lld, cycles: %lld, expected: %lld)\n",
                size, bw, cycles, expected);

            int status = _this->top->check_cycles(cycles, expected);

            _this->top->test_end(status);
        }
    }
}

void Test3D::exec_test()
{
    this->step = 0;
    this->fsm_event.enqueue();
}
