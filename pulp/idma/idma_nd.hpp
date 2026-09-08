/*
 * Copyright (C) 2026 Fondazione Chips-IT
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

/*
 * Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
 */

#pragma once

#include <stdint.h>

/*
 * Conventions shared between the reg32_3d front-end and the 3D middle-end.
 *
 * IdmaTransfer carries the first two dimensions in its own fields (size, src_stride, dst_stride
 * and reps). The third dimension and the stream index do not have a dedicated field, so they are
 * passed through the free-form IdmaTransfer::data vector, at the indices below. The vector is
 * sized to IDMA_ND_DATA_SIZE by the front-end.
 */
enum IdmaNdData
{
    IDMA_ND_SRC_STRIDE_3 = 0, // Bytes added to the page base to reach the next source page
    IDMA_ND_DST_STRIDE_3 = 1, // Bytes added to the page base to reach the next destination page
    IDMA_ND_REPS_3 = 2,       // Number of pages
    IDMA_ND_STREAM = 3,       // Index of the stream the transfer was launched on
    IDMA_ND_DATA_SIZE = 4,    // Number of entries, keep last
};

/*
 * IdmaTransfer::config bits.
 *
 * IDMA_CONFIG_2D keeps the encoding already used by the 2D middle-end, so a transfer produced for
 * one middle-end stays meaningful for the other.
 */
#define IDMA_CONFIG_2D (1 << 1)
#define IDMA_CONFIG_3D (1 << 2)
