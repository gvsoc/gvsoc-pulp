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

// Driver for floonoc's collective (multicast/reduction) test matrix
// (see COLLECTIVES.md, "Test matrix"). The scenario property sets the test to run.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include <vp/vp.hpp>
#include <cpu/iss/flexfloat/flexfloat.h>
#include <cpu/iss/include/offload.hpp>
#include "interco/traffic/generator.hpp"
#include "pulp/floonoc/floonoc.hpp"

#define MESH_DIM 4
#define NODE(x, y) ((y) * MESH_DIM + (x))
#define PACKET_SIZE 64
#define LARGE_PACKET_SIZE (2 * PACKET_SIZE)

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

static void init_ones(uint8_t *acc, size_t size) { std::memset(acc, 0xFF, size); }

static void combine_lsband(uint8_t *acc, const uint8_t *in, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        acc[i] = (acc[i] & 1) & (in[i] & 1);
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
    void build_reduction(int collective_op, CombineFn combine, InitFn init,
        uint8_t (*pattern)[PACKET_SIZE]);
    void build_mixed_traffic();
    void build_dma_broadcast();
    void build_dma_reduction(int collective_op, CombineFn combine, InitFn init,
        uint8_t (*pattern)[PACKET_SIZE], uint64_t mask, int nb_members, bool use_wide);
    void build_dma_reduction_large();

    // issues the xdma instruction sequence on the master port bound to a SnitchDma
    void dma_issue(uint32_t func7, uint32_t arg_a, uint32_t arg_b, uint32_t *result)
    {
        IssOffloadInsn<uint32_t> insn;
        insn.opcode = ((uint64_t)func7) << 25;
        insn.arg_a = arg_a;
        insn.arg_b = arg_b;
        insn.granted = false;
        insn.result = 0;
        this->dma_offload_itf.sync(&insn);
        if (result)
        {
            *result = insn.result;
        }
    }
    // func7 values below mirror IDmaFeXdma::offload_sync (pulp/idma/fe/idma_fe_xdma.cpp)
    void dma_set_src(uint64_t addr)
    {
        this->dma_issue(0b0000000, (uint32_t)addr, (uint32_t)(addr >> 32), nullptr);
    }
    void dma_set_dst(uint64_t addr)
    {
        this->dma_issue(0b0000001, (uint32_t)addr, (uint32_t)(addr >> 32), nullptr);
    }
    // dmmask: packs {op (low byte), mask (bits [63:8])} - see DMA_COLLECTIVES.md, 3b.
    void dma_set_collective(uint8_t op, uint64_t mask)
    {
        uint64_t cfg = (uint64_t)op | (mask << 8);
        this->dma_issue(0b0001000, (uint32_t)cfg, (uint32_t)(cfg >> 32), nullptr);
    }
    uint32_t dma_start_copy(uint32_t size)
    {
        uint32_t transfer_id;
        this->dma_issue(0b0000011, size, 0, &transfer_id);
        return transfer_id;
    }
    uint32_t dma_completed_count()
    {
        uint32_t status;
        this->dma_issue(0b0000101, 0, 0, &status);
        return status;
    }
    // Polls dmstat until `expected_completed` transfers are done
    void dma_wait_done(uint32_t expected_completed)
    {
        this->dma_expected_completed = expected_completed;
        this->dma_wait_needs_gen = false;
        this->dma_poll_event.enqueue(1);
    }
    // Like dma_wait_done(), but for a dma_reduction_* phase 
    void dma_wait_done_with_generators(uint32_t expected_completed)
    {
        this->gen_wait_done = false;
        // dma_wait_done() unconditionally resets dma_wait_needs_gen,
        // set it back to true
        this->dma_wait_done(expected_completed);
        this->dma_wait_needs_gen = true;
    }
    static void dma_poll_handler(vp::Block *__this, vp::ClockEvent *event)
    {
        CollectiveTestbench *_this = (CollectiveTestbench *)__this;
        if (_this->dma_completed_count() >= _this->dma_expected_completed &&
            (!_this->dma_wait_needs_gen || _this->gen_wait_done))
        {
            _this->advance();
        }
        else
        {
            _this->dma_poll_event.enqueue(1);
        }
    }
    // Completion target for the Generator sync in a dma_wait_done_with_generators() phase
    static void gen_wait_handler(vp::Block *__this, vp::ClockEvent *event)
    {
        CollectiveTestbench *_this = (CollectiveTestbench *)__this;
        _this->gen_wait_done = true;
        if (_this->dma_completed_count() >= _this->dma_expected_completed)
        {
            _this->advance();
        }
    }
    // Handle the IssOffloadInsnGrant from the SnitchDma's master port
    static void dma_offload_grant_sync(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *grant)
    {
    }

    // Small helpers shared by the builders above.
    uint64_t addr(int x, int y) { return node_addr(this->node_base, this->node_size, x, y); }
    uint64_t row_mask() { return FlooNoc::coll_mask_set_axis(0, 0, 0x3); }
    // dma_reduction_subgrid only: masks just 1 bit instead of row_mask()'s
    // 2, halving the box to 2 members instead of the full row of 4.
    uint64_t half_row_mask() { return FlooNoc::coll_mask_set_axis(0, 0, 0x1); }
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
        int op = 0, uint64_t mask = 0, uint64_t size = PACKET_SIZE)
    {
        this->itf()[NODE(x, y)].start(this->addr(x, y), size,
            size, sync, true, false, op, mask, nullptr, data);
    }
    // Like start_write(), but issues the write from a different node than
    // the one it targets. Needed so several distinct members can each
    // write their own contribution to one shared destination address for
    // a write-triggered reduction/barrier.
    void start_write_to(TrafficGeneratorSync *sync, int src_x, int src_y,
        int dst_x, int dst_y, const uint8_t *data, int op = 0, uint64_t mask = 0,
        uint64_t size = PACKET_SIZE)
    {
        this->itf()[NODE(src_x, src_y)].start(this->addr(dst_x, dst_y), size,
            size, sync, true, false, op, mask, nullptr, data);
    }
    void start_self_check_write(TrafficGeneratorSync *sync, int x, int y)
    {
        this->itf()[NODE(x, y)].start(this->addr(x, y), PACKET_SIZE,
            PACKET_SIZE, sync, true, true);
    }
    void start_check(TrafficGeneratorSync *sync, int x, int y, const uint8_t *expected,
        int op = 0, uint64_t mask = 0, uint64_t size = PACKET_SIZE)
    {
        this->itf()[NODE(x, y)].start(this->addr(x, y), size,
            size, sync, false, true, op, mask, expected);
    }
    // this->itf()'s plane other than the one currently selected
    std::vector<TrafficGeneratorConfigMaster> &other_plane_itf()
    {
        return this->wide ? this->narrow_control_itf : this->wide_control_itf;
    }
    void start_check_other_plane(TrafficGeneratorSync *sync, int x, int y,
        const uint8_t *expected, int op = 0, uint64_t mask = 0, uint64_t size = PACKET_SIZE)
    {
        this->other_plane_itf()[NODE(x, y)].start(this->addr(x, y), size,
            size, sync, false, true, op, mask, expected);
    }
    bool collect(int x, int y, const char *label)
    {
        bool check_status = false;
        this->itf()[NODE(x, y)].get_result(&check_status, nullptr);
        printf("[floonoc_collective] %s: node (%d,%d) %s\n", label, x, y,
            check_status ? "FAILED" : "passed");
        this->status |= check_status;
        return check_status;
    }
    bool collect_other_plane(int x, int y, const char *label)
    {
        bool check_status = false;
        this->other_plane_itf()[NODE(x, y)].get_result(&check_status, nullptr);
        printf("[floonoc_collective] %s: node (%d,%d) %s\n", label, x, y,
            check_status ? "FAILED" : "passed");
        this->status |= check_status;
        return check_status;
    }

    vp::Trace trace;
    vp::ClockEvent fsm_event;
    std::vector<TrafficGeneratorConfigMaster> narrow_control_itf;
    std::vector<TrafficGeneratorConfigMaster> wide_control_itf;

    // DMA offload driver bound to a SnitchDma's i_OFFLOAD()/o_OFFLOAD_GRANT().
    vp::WireMaster<IssOffloadInsn<uint32_t> *> dma_offload_itf;
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> dma_offload_grant_itf;
    vp::ClockEvent dma_poll_event;
    uint32_t dma_expected_completed = 0;
    // dma_wait_done_with_generators() only - see its comment.
    vp::ClockEvent gen_wait_event;
    bool gen_wait_done = false;
    bool dma_wait_needs_gen = false;

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
    // LsbAnd ("arrived" flags): every member all-1s except the last one,
    // which reports "not arrived" (bit0=0) at one lane only - proves the
    // barrier correctly stays 0 at that lane instead of trivially being
    // all-1s regardless of input.
    uint8_t barrier_pattern[MESH_DIM][PACKET_SIZE];
    uint8_t reduce_expected[PACKET_SIZE];
    uint8_t zero_sentinel[PACKET_SIZE] = {0};
    // dma_reduction_large only.
    uint8_t large_pattern[MESH_DIM][LARGE_PACKET_SIZE];
    uint8_t large_reduce_expected[LARGE_PACKET_SIZE];
    uint8_t large_zero_sentinel[LARGE_PACKET_SIZE] = {0};
};

CollectiveTestbench::CollectiveTestbench(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, CollectiveTestbench::fsm_handler),
    dma_poll_event(this, CollectiveTestbench::dma_poll_handler),
    gen_wait_event(this, CollectiveTestbench::gen_wait_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_master_port("dma_offload", &this->dma_offload_itf);
    this->dma_offload_grant_itf.set_sync_meth(&CollectiveTestbench::dma_offload_grant_sync);
    this->new_slave_port("dma_offload_grant", &this->dma_offload_grant_itf, this);

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

        std::memset(this->barrier_pattern[m], 0x01, PACKET_SIZE);

        uint16_t *large16 = (uint16_t *)this->large_pattern[m];
        for (int i = 0; i < LARGE_PACKET_SIZE / 2; i++)
        {
            large16[i] = (uint16_t)(m * 1000 + i);
        }
    }
    this->barrier_pattern[MESH_DIM - 1][5] = 0x00;

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
        this->build_reduction(FlooNoc::COLL_OP_REDADD_UINT16, combine_add_uint16, init_zero,
            this->member_pattern);
    }
    else if (this->scenario == "reduction_add_int16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDADD_INT16, combine_add_int16, init_zero,
            this->member_pattern);
    }
    else if (this->scenario == "reduction_add_fp16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDADD_FP16, combine_add_fp16, init_zero,
            this->fp16_member_pattern);
    }
    else if (this->scenario == "reduction_max_uint16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDMAX_UINT16, combine_max_uint16, init_zero,
            this->member_pattern);
    }
    else if (this->scenario == "reduction_max_int16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDMAX_INT16, combine_max_int16, init_max_int16,
            this->member_pattern);
    }
    else if (this->scenario == "reduction_max_fp16")
    {
        this->build_reduction(FlooNoc::COLL_OP_REDMAX_FP16, combine_max_fp16, init_max_fp16,
            this->fp16_member_pattern);
    }
    else if (this->scenario == "barrier")
    {
        this->build_reduction(FlooNoc::COLL_OP_LSBAND, combine_lsband, init_ones,
            this->barrier_pattern);
    }
    else if (this->scenario == "mixed_traffic")
    {
        this->build_mixed_traffic();
    }
    else if (this->scenario == "dma_broadcast")
    {
        this->build_dma_broadcast();
    }
    else if (this->scenario == "dma_reduction_add_uint16")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDADD_UINT16, combine_add_uint16, init_zero,
            this->member_pattern, this->row_mask(), MESH_DIM, /*use_wide=*/true);
    }
    else if (this->scenario == "dma_reduction_add_int16")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDADD_INT16, combine_add_int16, init_zero,
            this->member_pattern, this->row_mask(), MESH_DIM, /*use_wide=*/true);
    }
    else if (this->scenario == "dma_reduction_add_fp16")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDADD_FP16, combine_add_fp16, init_zero,
            this->fp16_member_pattern, this->row_mask(), MESH_DIM, /*use_wide=*/true);
    }
    else if (this->scenario == "dma_reduction_max_uint16")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDMAX_UINT16, combine_max_uint16, init_zero,
            this->member_pattern, this->row_mask(), MESH_DIM, /*use_wide=*/true);
    }
    else if (this->scenario == "dma_reduction_max_int16")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDMAX_INT16, combine_max_int16, init_max_int16,
            this->member_pattern, this->row_mask(), MESH_DIM, /*use_wide=*/true);
    }
    else if (this->scenario == "dma_reduction_max_fp16")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDMAX_FP16, combine_max_fp16, init_max_fp16,
            this->fp16_member_pattern, this->row_mask(), MESH_DIM, /*use_wide=*/true);
    }
    else if (this->scenario == "dma_reduction_subgrid")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDADD_UINT16, combine_add_uint16, init_zero,
            this->member_pattern, this->half_row_mask(), 2, /*use_wide=*/true);
    }
    else if (this->scenario == "dma_reduction_narrow")
    {
        this->build_dma_reduction(FlooNoc::COLL_OP_REDADD_UINT16, combine_add_uint16, init_zero,
            this->member_pattern, this->row_mask(), MESH_DIM, /*use_wide=*/false);
    }
    else if (this->scenario == "dma_reduction_large")
    {
        this->build_dma_reduction_large();
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
// pattern, tagged with `collective_op`/the row mask, to the SAME
// destination (0,0).
void CollectiveTestbench::build_reduction(int collective_op, CombineFn combine, InitFn init,
    uint8_t (*pattern)[PACKET_SIZE])
{
    init(this->reduce_expected, PACKET_SIZE);
    for (int x = 0; x < MESH_DIM; x++)
    {
        combine(this->reduce_expected, pattern[x], PACKET_SIZE);
    }

    uint64_t mask = this->row_mask();

    this->phases.push_back([this, pattern, collective_op, mask]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        for (int x = 0; x < MESH_DIM; x++)
        {
            this->start_write_to(sync, x, 0, 0, 0, pattern[x], collective_op, mask);
        }
        sync->start();
    });

    this->phases.push_back([this]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_check(sync, 0, 0, this->reduce_expected);
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->start_check(sync, x, 0, this->zero_sentinel);
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
            // Write-triggered reduction: every row-y=1 member writes its
            // contribution to the same destination (0,1)
            this->start_write_to(sync, x, 1, 0, 1, this->member_pattern[x],
                FlooNoc::COLL_OP_REDADD_UINT16, this->row_mask());
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
        this->start_check(sync, 0, 1, (const uint8_t *)expected16);
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

// dma_broadcast: A SnitchDma wired at node (0,0) issues the xdma instruction
// sequence to multicast pattern_a over the full mesh
void CollectiveTestbench::build_dma_broadcast()
{
    this->phases.push_back([this]() {
        // Seed the DMA's source tile (3,3), untouched by the broadcast mask.
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_write(sync, 3, 3, this->pattern_a);
        sync->start();
    });

    this->phases.push_back([this]() {
        this->collect(3, 3, "dma_broadcast_seed");

        this->dma_set_src(this->addr(3, 3));
        this->dma_set_dst(this->addr(0, 0));
        this->dma_set_collective(FlooNoc::COLL_OP_MULTICAST, this->full_mesh_mask());
        uint32_t transfer_id = this->dma_start_copy(PACKET_SIZE);
        // Mirrors the real snrt_dma_disable_multicast()/dmuser-reset
        // convention: clear dmmask right after the copy that
        // needed it so a later, unrelated transfer doesn't inherit it.
        this->dma_set_collective(FlooNoc::COLL_OP_UNICAST, 0);
        // dmstat(0) (completed_id) reaches transfer_id exactly when that transfer is done
        this->dma_wait_done(transfer_id);
    });

    this->phases.push_back([this]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        for (int y = 0; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->start_check(sync, x, y, this->pattern_a);
            }
        }
        sync->start();
    });

    this->phases.push_back([this]() {
        for (int y = 0; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->collect(x, y, "dma_broadcast");
            }
        }
        this->advance();
    });
}

// dma_reduction_<op> / dma_reduction_subgrid / dma_reduction_narrow
void CollectiveTestbench::build_dma_reduction(int collective_op, CombineFn combine, InitFn init,
    uint8_t (*pattern)[PACKET_SIZE], uint64_t mask, int nb_members, bool use_wide)
{
    this->wide = use_wide;

    init(this->reduce_expected, PACKET_SIZE);
    for (int x = 0; x < nb_members; x++)
    {
        combine(this->reduce_expected, pattern[x], PACKET_SIZE);
    }

    this->phases.push_back([this, pattern]() {
        // Seed the DMA's source tile (3,3) with member 0's contribution:
        // the DMA copies from there to (0,0)
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_write(sync, 3, 3, pattern[0]);
        sync->start();
    });

    // The Generator writes (members 1..nb_members-1)
    // and the DMA's own (member 0) are issued together with a combined wait
    this->phases.push_back([this, pattern, collective_op, mask, nb_members]() {
        this->collect(3, 3, "dma_reduction_seed");

        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->gen_wait_event);
        for (int x = 1; x < nb_members; x++)
        {
            this->start_write_to(sync, x, 0, 0, 0, pattern[x], collective_op, mask);
        }
        sync->start();

        this->dma_set_src(this->addr(3, 3));
        this->dma_set_dst(this->addr(0, 0));
        this->dma_set_collective((uint8_t)collective_op, mask);
        uint32_t transfer_id = this->dma_start_copy(PACKET_SIZE);
        this->dma_set_collective(FlooNoc::COLL_OP_UNICAST, 0);
        this->dma_wait_done_with_generators(transfer_id);
    });

    this->phases.push_back([this]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        // Check the result at (0,0) through the plane not currently occupied by the DMA
        this->start_check_other_plane(sync, 0, 0, this->reduce_expected);
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->start_check(sync, x, 0, this->zero_sentinel);
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
        this->collect_other_plane(0, 0, "dma_reduction_root");
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->collect(x, 0, "dma_reduction_member_untouched");
        }
        for (int y = 1; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->collect(x, y, "dma_reduction_outside_untouched");
            }
        }
        this->advance();
    });
}

// dma_reduction_large: same shape as build_dma_reduction(), but with a
// LARGE_PACKET_SIZE transfer over the wide link
void CollectiveTestbench::build_dma_reduction_large()
{
    this->wide = true;

    std::memset(this->large_reduce_expected, 0, LARGE_PACKET_SIZE);
    for (int x = 0; x < MESH_DIM; x++)
    {
        combine_add_uint16(this->large_reduce_expected, this->large_pattern[x], LARGE_PACKET_SIZE);
    }

    uint64_t mask = this->row_mask();

    this->phases.push_back([this]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        this->start_write(sync, 3, 3, this->large_pattern[0], 0, 0, LARGE_PACKET_SIZE);
        sync->start();
    });

    // The Generator writes (members 1..nb_members-1)
    // and the DMA's own (member 0) are issued together with a combined wait
    this->phases.push_back([this, mask]() {
        this->collect(3, 3, "dma_reduction_large_seed");

        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->gen_wait_event);
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->start_write_to(sync, x, 0, 0, 0, this->large_pattern[x],
                FlooNoc::COLL_OP_REDADD_UINT16, mask, LARGE_PACKET_SIZE);
        }
        sync->start();

        this->dma_set_src(this->addr(3, 3));
        this->dma_set_dst(this->addr(0, 0));
        this->dma_set_collective(FlooNoc::COLL_OP_REDADD_UINT16, mask);
        uint32_t transfer_id = this->dma_start_copy(LARGE_PACKET_SIZE);
        this->dma_set_collective(FlooNoc::COLL_OP_UNICAST, 0);
        this->dma_wait_done_with_generators(transfer_id);
    });

    this->phases.push_back([this]() {
        TrafficGeneratorSync *sync = new TrafficGeneratorSync(&this->fsm_event);
        // Check the result at (0,0) through the plane not currently occupied by the DMA
        this->start_check_other_plane(sync, 0, 0, this->large_reduce_expected, 0, 0, LARGE_PACKET_SIZE);
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->start_check(sync, x, 0, this->large_zero_sentinel, 0, 0, LARGE_PACKET_SIZE);
        }
        for (int y = 1; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->start_check(sync, x, y, this->large_zero_sentinel, 0, 0, LARGE_PACKET_SIZE);
            }
        }
        sync->start();
    });

    this->phases.push_back([this]() {
        this->collect_other_plane(0, 0, "dma_reduction_large_root");
        for (int x = 1; x < MESH_DIM; x++)
        {
            this->collect(x, 0, "dma_reduction_large_member_untouched");
        }
        for (int y = 1; y < MESH_DIM; y++)
        {
            for (int x = 0; x < MESH_DIM; x++)
            {
                this->collect(x, y, "dma_reduction_large_outside_untouched");
            }
        }
        this->advance();
    });
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new CollectiveTestbench(config);
}
