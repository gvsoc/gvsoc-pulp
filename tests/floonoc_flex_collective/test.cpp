/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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

// Driver for floonoc_flex's collective (multicast/reduction) test matrix
// (see COLLECTIVES.md, "Test matrix"). The scenario property sets the test to run.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include <vp/vp.hpp>
#include <cpu/iss/flexfloat/flexfloat.h>
#include "interco/traffic/generator.hpp"
#include "pulp/floonoc_flex/floonoc_flex.hpp"

#define MESH_DIM 4
#define NODE(x, y) ((y) * MESH_DIM + (x))
#define PACKET_SIZE 64

static constexpr flexfloat_desc_t FP16_DESC = {5, 10};

// Mirrors of FlooNoc::process_collective_operations init_reduction_identity
typedef void (*CombineFn)(uint8_t *acc, const uint8_t *in, size_t size);
typedef void (*InitFn)(uint8_t *acc, size_t size);

static void init_zero(uint8_t *acc, size_t size) { std::memset(acc, 0, size); }

static void init_max_int16(uint8_t *acc, size_t size)
{
    int16_t *dst = (int16_t *)acc;
    for (size_t i = 0; i < size / sizeof(int16_t); i++)
    {
        dst[i] = INT16_MIN;
    }
}

static void init_max_fp16(uint8_t *acc, size_t size)
{
    flexfloat_t neg_inf;
    ff_init(&neg_inf, FP16_DESC);
    flexfloat_set_bits(&neg_inf, 0xFC00); // fp16 -inf
    uint16_t bits = flexfloat_get_bits(&neg_inf);
    uint16_t *dst = (uint16_t *)acc;
    for (size_t i = 0; i < size / sizeof(uint16_t); i++)
    {
        dst[i] = bits;
    }
}

static void combine_add_uint16(uint8_t *acc, const uint8_t *in, size_t size)
{
    uint16_t *dst = (uint16_t *)acc;
    const uint16_t *src = (const uint16_t *)in;
    for (size_t i = 0; i < size / sizeof(uint16_t); i++)
    {
        dst[i] = (uint16_t)(dst[i] + src[i]);
    }
}

static void combine_add_int16(uint8_t *acc, const uint8_t *in, size_t size)
{
    int16_t *dst = (int16_t *)acc;
    const int16_t *src = (const int16_t *)in;
    for (size_t i = 0; i < size / sizeof(int16_t); i++)
    {
        dst[i] = (int16_t)(dst[i] + src[i]);
    }
}

static void combine_add_fp16(uint8_t *acc, const uint8_t *in, size_t size)
{
    uint16_t *dst = (uint16_t *)acc;
    const uint16_t *src = (const uint16_t *)in;
    for (size_t i = 0; i < size / sizeof(uint16_t); i++)
    {
        flexfloat_t a, b, result;
        ff_init(&a, FP16_DESC);
        ff_init(&b, FP16_DESC);
        ff_init(&result, FP16_DESC);
        flexfloat_set_bits(&a, dst[i]);
        flexfloat_set_bits(&b, src[i]);
        ff_add(&result, &a, &b);
        dst[i] = (uint16_t)flexfloat_get_bits(&result);
    }
}

static void combine_max_uint16(uint8_t *acc, const uint8_t *in, size_t size)
{
    uint16_t *dst = (uint16_t *)acc;
    const uint16_t *src = (const uint16_t *)in;
    for (size_t i = 0; i < size / sizeof(uint16_t); i++)
    {
        dst[i] = dst[i] > src[i] ? dst[i] : src[i];
    }
}

static void combine_max_int16(uint8_t *acc, const uint8_t *in, size_t size)
{
    int16_t *dst = (int16_t *)acc;
    const int16_t *src = (const int16_t *)in;
    for (size_t i = 0; i < size / sizeof(int16_t); i++)
    {
        dst[i] = dst[i] > src[i] ? dst[i] : src[i];
    }
}

static void combine_max_fp16(uint8_t *acc, const uint8_t *in, size_t size)
{
    uint16_t *dst = (uint16_t *)acc;
    const uint16_t *src = (const uint16_t *)in;
    for (size_t i = 0; i < size / sizeof(uint16_t); i++)
    {
        flexfloat_t a, b, result;
        ff_init(&a, FP16_DESC);
        ff_init(&b, FP16_DESC);
        ff_init(&result, FP16_DESC);
        flexfloat_set_bits(&a, dst[i]);
        flexfloat_set_bits(&b, src[i]);
        ff_max(&result, &a, &b);
        dst[i] = (uint16_t)flexfloat_get_bits(&result);
    }
}

static uint64_t node_addr(uint64_t base, uint64_t node_size, int x, int y)
{
    return base + (uint64_t)NODE(x, y) * node_size;
}

class CollectiveTestbench : public vp::Component
{
public:
    CollectiveTestbench(vp::ComponentConf &config);

    void reset(bool active);

private:
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    void advance();

    // Scenario builders: each fills `this->phases` with the lambdas for
    // one matrix row.
    void build_unicast_regression();
    void build_broadcast(bool full_mesh);
    void build_reduction(int collective_op, CombineFn combine, InitFn init);
    void build_mixed_traffic();

    // Small helpers shared by the builders above.
    uint64_t addr(int x, int y) { return node_addr(this->node_base, this->node_size, x, y); }
    uint64_t row_mask() { return FlooNoc::coll_mask_set_axis(0, 0, 0x3); }
    uint64_t full_mesh_mask()
    {
        return FlooNoc::coll_mask_set_axis(
            FlooNoc::coll_mask_set_axis(0, 0, 0x3), 1, 0x3);
    }
    // All four below go through this->wide/this->narrow_control_itf,
    // picked by `this->wide` (set once from the scenario name)
    std::vector<TrafficGeneratorConfigMaster> &itf()
    {
        return this->wide ? this->wide_control_itf : this->narrow_control_itf;
    }
    void start_write(TrafficGeneratorSync *sync, int x, int y, const uint8_t *data,
        int op = 0, uint64_t mask = 0)
    {
        this->itf()[NODE(x, y)].start(this->addr(x, y), PACKET_SIZE,
            PACKET_SIZE, sync, true, false, op, mask, nullptr, data);
    }
    void start_self_check_write(TrafficGeneratorSync *sync, int x, int y)
    {
        this->itf()[NODE(x, y)].start(this->addr(x, y), PACKET_SIZE,
            PACKET_SIZE, sync, true, true);
    }
    void start_check(TrafficGeneratorSync *sync, int x, int y, const uint8_t *expected,
        int op = 0, uint64_t mask = 0)
    {
        this->itf()[NODE(x, y)].start(this->addr(x, y), PACKET_SIZE,
            PACKET_SIZE, sync, false, true, op, mask, expected);
    }
    bool collect(int x, int y, const char *label)
    {
        bool check_status = false;
        this->itf()[NODE(x, y)].get_result(&check_status, nullptr);
        printf("[floonoc_flex_collective] %s: node (%d,%d) %s\n", label, x, y,
            check_status ? "FAILED" : "passed");
        this->status |= check_status;
        return check_status;
    }

    vp::Trace trace;
    vp::ClockEvent fsm_event;
    std::vector<TrafficGeneratorConfigMaster> narrow_control_itf;
    std::vector<TrafficGeneratorConfigMaster> wide_control_itf;

    std::string scenario;
    bool wide = false;
    uint64_t node_size;
    uint64_t node_base;
    std::vector<std::function<void()>> phases;
    int phase_index = 0;
    bool status = false;

    // Scratch buffers kept alive for the whole test.
    uint8_t pattern_a[PACKET_SIZE];
    uint8_t member_pattern[MESH_DIM][PACKET_SIZE];
    uint8_t fp16_member_pattern[MESH_DIM][PACKET_SIZE];
    uint8_t reduce_expected[PACKET_SIZE];
    uint8_t zero_sentinel[PACKET_SIZE] = {0};
};

CollectiveTestbench::CollectiveTestbench(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, CollectiveTestbench::fsm_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->node_size = this->get_js_config()->get_uint("node_size");
    this->node_base = this->get_js_config()->get_uint("node_base");
    this->scenario = this->get_js_config()->get_child_str("scenario");
    // "<row>_wide" runs that same test entirely over the wide link instead of the narrow one
    static const std::string wide_suffix = "_wide";
    if (this->scenario.size() > wide_suffix.size() &&
        this->scenario.compare(this->scenario.size() - wide_suffix.size(),
            wide_suffix.size(), wide_suffix) == 0)
    {
        this->wide = true;
        this->scenario.resize(this->scenario.size() - wide_suffix.size());
    }

    int nb_nodes = MESH_DIM * MESH_DIM;
    this->narrow_control_itf.resize(nb_nodes);
    this->wide_control_itf.resize(nb_nodes);
    for (int node = 0; node < nb_nodes; node++)
    {
        this->new_master_port("generator_control_" + std::to_string(node) + "_narrow",
            &this->narrow_control_itf[node]);
        this->new_master_port("generator_control_" + std::to_string(node) + "_wide",
            &this->wide_control_itf[node]);
    }

    for (int i = 0; i < PACKET_SIZE; i++)
    {
        this->pattern_a[i] = (uint8_t)(0xA0 + i);
    }
    for (int m = 0; m < MESH_DIM; m++)
    {
        uint16_t *pattern16 = (uint16_t *)this->member_pattern[m];
        for (int i = 0; i < PACKET_SIZE / 2; i++)
        {
            pattern16[i] = (uint16_t)(m * 1000 + i);
        }

        flexfloat_t f;
        ff_init_float(&f, (float)(m + 1), FP16_DESC);
        uint16_t bits = flexfloat_get_bits(&f);
        uint16_t *pattern16_fp = (uint16_t *)this->fp16_member_pattern[m];
        for (int i = 0; i < PACKET_SIZE / 2; i++)
        {
            pattern16_fp[i] = bits;
        }
    }

    if (this->scenario == "unicast_on" || this->scenario == "unicast_off")
    {
        this->build_unicast_regression();
    }
    else if (this->scenario == "broadcast_full_mesh")
    {
        this->build_broadcast(/*full_mesh=*/true);
    }
    else if (this->scenario == "broadcast_subgrid")
    {
        this->build_broadcast(/*full_mesh=*/false);
    }
    else if (this->scenario == "reduction_add_uint16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDADD_UINT16, combine_add_uint16, init_zero);
    }
    else if (this->scenario == "reduction_add_int16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDADD_INT16, combine_add_int16, init_zero);
    }
    else if (this->scenario == "reduction_add_fp16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDADD_FP16, combine_add_fp16, init_zero);
    }
    else if (this->scenario == "reduction_max_uint16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDMAX_UINT16, combine_max_uint16, init_zero);
    }
    else if (this->scenario == "reduction_max_int16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDMAX_INT16, combine_max_int16, init_max_int16);
    }
    else if (this->scenario == "reduction_max_fp16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDMAX_FP16, combine_max_fp16, init_max_fp16);
    }
    else if (this->scenario == "mixed_traffic")
    {
        this->build_mixed_traffic();
    }
    else
    {
        this->trace.fatal("Unknown scenario '%s'\n", this->scenario.c_str());
    }
}

void CollectiveTestbench::reset(bool active)
{
    if (!active)
    {
        this->phase_index = 0;
        this->status = false;
        this->fsm_event.enqueue();
    }
}

void CollectiveTestbench::advance()
{
    if (this->phase_index >= (int)this->phases.size())
    {
        this->time.get_engine()->quit(this->status);
        return;
    }
    int idx = this->phase_index++;
    this->phases[idx]();
}

void CollectiveTestbench::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ((CollectiveTestbench *)__this)->advance();
}

// unicast_on / unicast_off: a plain unicast write+readback, (0,0) -> (3,3).
void CollectiveTestbench::build_unicast_regression()
{
    this->phases.push_back([this]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_self_check_write(sync, 3, 3);
        sync->start();
    });
    this->phases.push_back([this]() {
        this->collect(3, 3, "unicast_regression");
        this->advance();
    });
}

// broadcast_full_mesh / broadcast_subgrid: root (0,0) multicasts
// pattern_a to either the whole mesh or just row y=0.
void CollectiveTestbench::build_broadcast(bool full_mesh)
{
    uint64_t mask = full_mesh ? this->full_mesh_mask() : this->row_mask();

    this->phases.push_back([this, mask]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_write(sync, 0, 0, this->pattern_a, FlooNoc::COLL_OP_MULTICAST, mask);
        sync->start();
    });

    this->phases.push_back([this, full_mesh]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        for (int y = 0; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                bool in_mask = full_mesh || y == 0;
                this->start_check(sync, x, y,
                    in_mask ? this->pattern_a : this->zero_sentinel);
            }
        }
        sync->start();
    });

    this->phases.push_back([this]() {
        for (int y = 0; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->collect(x, y, "broadcast");
            }
        }
        this->advance();
    });
}

// reduction_<op>: the 4 row-y=0 members each write their own distinct
// pattern to their own tile, then root (0,0) gathers with `collective_op`
// over the row mask.
void CollectiveTestbench::build_reduction(int collective_op, CombineFn combine, InitFn init)
{
    bool is_fp16 = collective_op == FlooNoc::COLL_OP_REDADD_FP16 ||
        collective_op == FlooNoc::COLL_OP_REDMAX_FP16;
    uint8_t (*pattern)[PACKET_SIZE] = is_fp16 ? this->fp16_member_pattern : this->member_pattern;

    init(this->reduce_expected, PACKET_SIZE);
    for (int x = 0; x < MESH_DIM; x++)
    {
        combine(this->reduce_expected, pattern[x], PACKET_SIZE);
    }

    this->phases.push_back([this, pattern]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        for (int x = 0; x < MESH_DIM; x++)
        {
            this->start_write(sync, x, 0, pattern[x]);
        }
        sync->start();
    });

    this->phases.push_back([this, collective_op, pattern]() {
        uint64_t mask = this->row_mask();
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_check(sync, 0, 0, this->reduce_expected, collective_op, mask);
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->start_check(sync, x, 0, pattern[x]);
        }
        for (int y = 1; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->start_check(sync, x, y, this->zero_sentinel);
            }
        }
        sync->start();
    });

    this->phases.push_back([this]() {
        this->collect(0, 0, "reduction_root");
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->collect(x, 0, "reduction_member_untouched");
        }
        for (int y = 1; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->collect(x, y, "reduction_outside_untouched");
            }
        }
        this->advance();
    });
}

// mixed_traffic: a broadcast write/setup phase followed by a phase that
// overlaps a broadcast check-read, a reduction gather, AND ordinary
// unrelated unicast write+check traffic all in flight together.
void CollectiveTestbench::build_mixed_traffic()
{
    static uint16_t expected16[PACKET_SIZE / 2];
    std::memset(expected16, 0, sizeof(expected16));
    for (int m = 0; m < MESH_DIM; m++)
    {
        combine_add_uint16((uint8_t *)expected16, this->member_pattern[m], PACKET_SIZE);
    }

    this->phases.push_back([this]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_write(sync, 0, 0, this->pattern_a, FlooNoc::COLL_OP_MULTICAST,
            this->row_mask());
        for (int x = 0; x < MESH_DIM; x++)
        {
            this->start_write(sync, x, 1, this->member_pattern[x]);
        }
        this->start_self_check_write(sync, 1, 2);
        this->start_self_check_write(sync, 3, 3);
        sync->start();
    });

    this->phases.push_back([this]() {
        this->collect(1, 2, "mixed_unicast_setup");
        this->collect(3, 3, "mixed_unicast_setup");

        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        for (int x = 0; x < MESH_DIM; x++)
        {
            this->start_check(sync, x, 0, this->pattern_a);
        }
        this->start_check(sync, 0, 1, (const uint8_t *)expected16,
            FlooNoc::COLL_OP_REDADD_UINT16, this->row_mask());
        this->start_self_check_write(sync, 2, 2);
        this->start_self_check_write(sync, 0, 3);
        sync->start();
    });

    this->phases.push_back([this]() {
        for (int x = 0; x < MESH_DIM; x++)
        {
            this->collect(x, 0, "mixed_broadcast");
        }
        this->collect(0, 1, "mixed_reduction");
        this->collect(2, 2, "mixed_unicast");
        this->collect(0, 3, "mixed_unicast");
        this->advance();
    });
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new CollectiveTestbench(config);
}
