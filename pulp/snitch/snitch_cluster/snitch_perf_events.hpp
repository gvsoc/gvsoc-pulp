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

#pragma once

#include <cstdint>

/**
 * Metrics of the performance counters of the Snitch cluster (perf_metric of
 * snitch_cluster_peripheral_reg.rdl).
 */
enum SnitchPerfMetric
{
    SNITCH_PERF_CYCLE             = 0,
    SNITCH_PERF_TCDM_ACCESSED     = 1,
    SNITCH_PERF_TCDM_CONGESTED    = 2,
    SNITCH_PERF_ISSUE_FPU         = 3,
    SNITCH_PERF_ISSUE_FPU_SEQ     = 4,
    SNITCH_PERF_ISSUE_CORE_TO_FPU = 5,
    SNITCH_PERF_RETIRED_INSTR     = 6,
    SNITCH_PERF_RETIRED_LOAD      = 7,
    SNITCH_PERF_RETIRED_I         = 8,
    SNITCH_PERF_RETIRED_ACC       = 9,
    SNITCH_PERF_DMA_AW_STALL      = 10,
    SNITCH_PERF_DMA_AR_STALL      = 11,
    SNITCH_PERF_DMA_R_STALL       = 12,
    SNITCH_PERF_DMA_W_STALL       = 13,
    SNITCH_PERF_DMA_BUF_W_STALL   = 14,
    SNITCH_PERF_DMA_BUF_R_STALL   = 15,
    SNITCH_PERF_DMA_AW_DONE       = 16,
    SNITCH_PERF_DMA_AW_BW         = 17,
    SNITCH_PERF_DMA_AR_DONE       = 18,
    SNITCH_PERF_DMA_AR_BW         = 19,
    SNITCH_PERF_DMA_R_DONE        = 20,
    SNITCH_PERF_DMA_R_BW          = 21,
    SNITCH_PERF_DMA_W_DONE        = 22,
    SNITCH_PERF_DMA_W_BW          = 23,
    SNITCH_PERF_DMA_B_DONE        = 24,
    SNITCH_PERF_DMA_BUSY          = 25,
    SNITCH_PERF_ICACHE_MISS       = 26,
    SNITCH_PERF_ICACHE_HIT        = 27,
    SNITCH_PERF_ICACHE_PREFETCH   = 28,
    SNITCH_PERF_ICACHE_DOUBLE_HIT = 29,
    SNITCH_PERF_ICACHE_STALL      = 30,
    SNITCH_PERF_NB_METRICS        = 31,
};

/**
 * Totals since reset of the events of a hart or of a DMA channel, indexed by
 * metric. The cluster registers zero it and ask the source (a wire sync with
 * a pointer to it) when a counter is accessed; the source fills the entries
 * it models and leaves the others at 0.
 */
struct SnitchPerfEvents
{
    uint64_t count[SNITCH_PERF_NB_METRICS];
};
