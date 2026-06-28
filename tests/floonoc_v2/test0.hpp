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

class Test0 : public TestCommon
{
public:
    Test0(Testbench *top);
    void exec_test();

private:
    static void entry(vp::Block *__this, vp::ClockEvent *event);
    bool check_single_path(bool do_write, bool wide, bool narrow, int initiator_bw, int target_bw,
        int size, int expected_cycles, int x0, int y0, int x1, int y1, uint64_t offset,
        int x2, int y2);
    bool check_2_paths_through_same_node();
    bool check_2_paths_to_same_target();
    bool check_prefered_path();

    Testbench *top;
    vp::ClockEvent fsm_event;
    int step;
    int testcase;
    int64_t clockstamp;
    bool status;
    std::vector<std::function<bool()>> testcases;
};
