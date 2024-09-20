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

#include "test.hpp"

class Test2 : public TestCommon
{
public:
    Test2(Testbench *top);
    void exec_test();

private:
    static void entry(vp::Block *__this, vp::ClockEvent *event);

    Testbench *top;
    vp::ClockEvent fsm_event;
    int step;
    int64_t clockstamp;
};
