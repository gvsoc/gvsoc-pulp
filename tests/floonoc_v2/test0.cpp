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

bool Test0::check_single_path(bool do_write, bool wide, bool narrow, int initiator_bw,
    int target_bw, int size, int expected, int x0, int y0, int x1, int y1, uint64_t offset, int x2, int y2)
{
    if (this->step == 0)
    {
        printf("Single path (write: %d, wide: %d, narrow: %d, initiator_bw: %d, target_bw: %d, "
            "x0: %d, y0: %d, x1: %d, y1: %d)\n",
            do_write, wide, narrow, initiator_bw, target_bw, x0, y0, x1, y1);

        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);

        uint64_t base1 = this->top->get_target_base(x1, y1) + offset;
        uint64_t base2 = this->top->get_target_base(x2, y2) + offset;

        if (wide)
        {
            this->top->get_generator(x0, y0, false)->start(base1, size, initiator_bw, sync, do_write, true);

            if (!this->top->use_memory)
            {
                this->top->get_receiver(x1, y1, false)->start(target_bw);
                if (x2 != -1)
                {
                    this->top->get_receiver(x2, y2, false)->start(target_bw);
                }
            }
        }

        if (narrow)
        {
            this->top->get_generator(x0, y0, true)->start(base1 + 0x10000, size, initiator_bw, sync, do_write, true);
            if (!this->top->use_memory)
            {
                this->top->get_receiver(x1, y1, true)->start(target_bw);
                if (x2 != -1)
                {
                    this->top->get_receiver(x2, y2, false)->start(target_bw);
                }
            }
        }

        sync->start();

        this->step++;
    }
    else
    {
        int64_t cycles = 0;

        bool check_status = false;

        if (wide)
        {
            this->top->get_generator(x0, y0, false)->get_result(&check_status, &cycles);
        }

        if (narrow)
        {
            this->top->get_generator(x0, y0, true)->get_result(&check_status, &cycles);
        }

        bool status = check_status;
        status |= this->top->check_cycles(cycles, expected) != 0;

        printf("    %s (check: %d, size: %lld, cycles: %lld, expected: %lld)\n",
            status ? "Failed" : "Done", check_status, size, cycles, expected);

        this->status |= status;
        return true;
    }
    return false;
}

bool Test0::check_2_paths_through_same_node()
{
    int x0=(this->top->nb_cluster_x-1)/2, y0=0;
    int x1=(this->top->nb_cluster_x-1)/2, y1=this->top->nb_cluster_y-1;
    int x2=0, y2=(this->top->nb_cluster_y-1)/2;
    int x3=this->top->nb_cluster_x-1, y3=(this->top->nb_cluster_y-1)/2;
    size_t size = 1024*256;
    size_t bw = 8;

    if (this->step == 0)
    {
        printf("Test 1, checking 2 data streams going through same router but using different paths\n");

        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);

        uint64_t base1 = this->top->get_cluster_base(x1, y1);
        uint64_t base3 = this->top->get_cluster_base(x3, y3);

        this->top->get_generator(x0, y0)->start(base1, size, bw, sync, false, true);
        if (!this->top->use_memory)
        {
            this->top->get_receiver(x1, y1)->start(bw);
        }
        this->top->get_generator(x2, y2)->start(base3, size, bw, sync, false, true);
        if (!this->top->use_memory)
        {
            this->top->get_receiver(x3, y3)->start(bw);
        }

        sync->start();

        this->step++;
    }
    else
    {
        int64_t cycles = 0;
        bool check_status = false;

        this->top->get_generator(x0, y0)->get_result(&check_status, &cycles);
        this->top->get_generator(x2, y2)->get_result(&check_status, &cycles);

        int64_t expected = size / bw;

        bool status = this->top->check_cycles(cycles, expected) != 0;

        printf("    %s (size: %lld, bw: %lld, cycles: %lld, expected: %lld)\n",
            status ? "Failed" : "Done", size, bw, cycles, expected);

        this->status |= status;
        return true;
    }
    return false;
}

bool Test0::check_2_paths_to_same_target()
{
    int x0=this->top->nb_cluster_x-1, y0=0;
    int x1=0, y1=this->top->nb_cluster_y-1;
    int x2=this->top->nb_cluster_x-1, y2=this->top->nb_cluster_y-1;
    size_t size = 1024*256;
    size_t bw1 = 4;
    size_t bw2 = 8;

    if (this->step == 0)
    {
        printf("Test 2, checking 2 generators going to same receiver\n");

        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);

        uint64_t base = this->top->get_cluster_base(x2, y2);

        this->top->get_generator(x0, y0)->start(base, size, bw1, sync, false, true);
        this->top->get_generator(x1, y1)->start(base + 0x10000, size, bw1, sync, false, true);
        if (!this->top->use_memory)
        {
            this->top->get_receiver(x2, y2)->start(bw2);
        }

        sync->start();

        this->clockstamp = this->clock.get_cycles();
        this->step++;
    }
    else
    {
        int64_t cycles = 0;
        bool check_status = false;

        this->top->get_generator(x0, y0)->get_result(&check_status, &cycles);
        this->top->get_generator(x1, y1)->get_result(&check_status, &cycles);

        int64_t expected = size / (bw1 / 2);

        bool status = this->top->check_cycles(cycles, expected) != 0;

        printf("    %s (size: %lld, bw: %lld, cycles: %lld, expected: %lld)\n",
            status ? "Failed" : "Done", size, bw1, cycles, expected);

        this->status |= status;
        return true;
    }
    return false;
}

bool Test0::check_prefered_path()
{
    size_t size = 1024*256;
    size_t bw0 = 8;
    int x0=0, y0=0, x1=1, y1=0, x2=2, y2=0;

    if (this->step == 0)
    {
        printf("Test 4, checking 3 generators to 1 border with prefered direction\n");

        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);

        this->top->get_generator(x0, y0)->start(0x90000000, size, bw0, sync, false, true);
        this->top->get_generator(x1, y1)->start(0x90010000, size, bw0, sync, false, true);
        this->top->get_generator(x2, y2)->start(0x90020000, size, bw0, sync, false, true);
        if (!this->top->use_memory)
        {
            this->top->get_receiver(x0+1, 0)->start(bw0);
            this->top->get_receiver(x0+1, 0)->start(bw0);
            this->top->get_receiver(x0+1, 0)->start(bw0);
        }

        this->clockstamp = this->clock.get_cycles();
        sync->start();
        this->step++;
    }
    else
    {
        int64_t cycles = 0;
        bool check_status = false;

        this->top->get_generator(x0, y0)->get_result(&check_status, &cycles);
        this->top->get_generator(x1, y1)->get_result(&check_status, &cycles);
        this->top->get_generator(x2, y2)->get_result(&check_status, &cycles);

        printf("    Done\n");

        return true;
    }
    return false;
}

void Test0::entry(vp::Block *__this, vp::ClockEvent *event)
{
    Test0 *_this = (Test0 *)__this;

    if (_this->testcases[_this->testcase]())
    {
        _this->testcase++;
        if (_this->testcase >= _this->testcases.size())
        {
            _this->top->test_end(_this->status);
        }
        else
        {
            _this->step = 0;
            _this->fsm_event.enqueue();
        }
    }
}

void Test0::exec_test()
{
    auto single_path_tests_add = [this](int x0, int y0, int x1, int y1, uint64_t offset, int x2=-1, int y2=-1)
    {
        if (!this->top->use_memory || this->top->mem_bw == 4)
        {
            this->testcases.insert(testcases.end(), {
                [=] { return this->check_single_path(true  , true , false  , 8      , 4     , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(true  , true , true   , 8      , 4     , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , false  , 8      , 4     , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , true   , 8      , 4     , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
            });
        }

        if (!this->top->use_memory || this->top->mem_bw == 8)
        {
            this->testcases.insert(testcases.end(), {
                [=] { return this->check_single_path(true  , true , false  , 64     , 8     , 1024*256, 32768 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(true  , true , true   , 64     , 8     , 1024*256, 36864 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , false  , 64     , 8     , 1024*256, 32768 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , true   , 64     , 8     , 1024*256, 32768 , x0, y0, x1, y1, offset, x2, y2); },
            });
        }

        if (!this->top->use_memory || this->top->mem_bw == 64)
        {
            this->testcases.insert(testcases.end(), {
                [=] { return this->check_single_path(true  , true , false  , 4      , 64    , 1024*256, 131072, x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(true  , true , true   , 4      , 64    , 1024*256, 131072, x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , false  , 4      , 64    , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , true   , 4      , 64    , 1024*256, 131072, x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(true  , true , false  , 8      , 64    , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(true  , true , true   , 8      , 64    , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , false  , 8      , 64    , 1024*256, 32768 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , true   , 8      , 64    , 1024*256, 65536 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(true  , true , false  , 64     , 64    , 1024*256, 8192 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , false  , 64     , 64    , 1024*256, 4096 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(true  , true , false  , 256    , 64    , 1024*256, 5120 , x0, y0, x1, y1, offset, x2, y2); },
                [=] { return this->check_single_path(false , true , false  , 256    , 64    , 1024*256, 4096 , x0, y0, x1, y1, offset, x2, y2); },
            });
        }
    };

    single_path_tests_add(0, 0, this->top->nb_cluster_x, this->top->nb_cluster_y, 0);
    single_path_tests_add(0, 0, this->top->nb_cluster_x + 1, this->top->nb_cluster_y, 0);
    single_path_tests_add(0, 0, this->top->nb_cluster_x, 0, 0);
    single_path_tests_add(0, 0, this->top->nb_cluster_x, this->top->nb_cluster_y + 1, 0);
    single_path_tests_add(0, 0, 0, this->top->nb_cluster_y, 0);
    single_path_tests_add(0, 0, this->top->nb_cluster_x, this->top->nb_cluster_y, 1);
    single_path_tests_add(0, 0, this->top->nb_cluster_x-1, this->top->nb_cluster_y-1, 0x100000 - 8, this->top->nb_cluster_x, this->top->nb_cluster_y - 1);

    if (!this->top->use_memory || this->top->mem_bw == 8)
    {
        this->testcases.insert(testcases.end(), {
            [&] { return this->check_2_paths_through_same_node(); },
            [&] { return this->check_2_paths_to_same_target(); },
            [&] { return this->check_prefered_path(); },
        });
    }

    this->status = false;
    this->step = 0;
    this->testcase = 0;
    this->fsm_event.enqueue();
}
