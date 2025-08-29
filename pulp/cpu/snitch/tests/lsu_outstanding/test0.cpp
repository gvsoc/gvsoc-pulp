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

#include "test.hpp"


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
        printf("Test 0 entry\n");

        _this->step = 1;
    }
    else
    {
        printf("    Done\n");

        int status = _this->top->check_cycles(0, 0);

        _this->top->test_end(status);
    }
}

void Test0::exec_test()
{
    this->step = 0;
    this->fsm_event.enqueue();
}
