#ifndef _FLEX_DMA_PATTERN_2D_H_
#define _FLEX_DMA_PATTERN_2D_H_

#include <stdint.h>

/***********************************
 *  2D Cluster Position Math       *
 ***********************************/

typedef struct FlexPosition2D
{
    uint32_t x;
    uint32_t y;
} FlexPosition2D;

// Extracts the 2D coordinate from the linear cluster ID.
static inline FlexPosition2D get_pos(uint32_t cluster_id) {
    FlexPosition2D pos;
    pos.x = cluster_id % ARCH_NUM_CLUSTER_X;
    pos.y = cluster_id / ARCH_NUM_CLUSTER_X;
    return pos;
}

// X-Axis shifts
static inline FlexPosition2D right_pos(FlexPosition2D pos) {
    FlexPosition2D new_pos;
    new_pos.x = (pos.x + 1) % ARCH_NUM_CLUSTER_X;
    new_pos.y = pos.y;
    return new_pos;
}

static inline FlexPosition2D left_pos(FlexPosition2D pos) {
    FlexPosition2D new_pos;
    new_pos.x = (pos.x + ARCH_NUM_CLUSTER_X - 1) % ARCH_NUM_CLUSTER_X;
    new_pos.y = pos.y;
    return new_pos;
}

// Y-Axis shifts
static inline FlexPosition2D top_pos(FlexPosition2D pos) {
    FlexPosition2D new_pos;
    new_pos.x = pos.x;
    new_pos.y = (pos.y + 1) % ARCH_NUM_CLUSTER_Y;
    return new_pos;
}

static inline FlexPosition2D bottom_pos(FlexPosition2D pos) {
    FlexPosition2D new_pos;
    new_pos.x = pos.x;
    new_pos.y = (pos.y + ARCH_NUM_CLUSTER_Y - 1) % ARCH_NUM_CLUSTER_Y;
    return new_pos;
}


/***********************************
 *  2D Address Resolution          *
 ***********************************/

#define cluster_index(x, y) \
    (((y) * ARCH_NUM_CLUSTER_X) + (x))

#define remote_xy(x, y, offset) \
    (ARCH_CLUSTER_TCDM_REMOTE + cluster_index(x, y) * ARCH_CLUSTER_TCDM_SIZE + (offset))

#define remote_pos(pos, offset) \
    (ARCH_CLUSTER_TCDM_REMOTE + cluster_index(pos.x, pos.y) * ARCH_CLUSTER_TCDM_SIZE + (offset))


/*******************************************
 *  Traffic Pattern: Asynchronous Interface *
 *******************************************/

// Pattern: Round Shift Right
static inline void flex_dma_async_pattern_round_shift_right(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    FlexPosition2D pos = get_pos(flex_get_cluster_id());
    bare_dma_start_1d(local(local_offset), remote_pos(left_pos(pos), remote_offset), transfer_size); // Start iDMA
}

// Pattern: Round Shift Up
static inline void flex_dma_async_pattern_round_shift_up(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    FlexPosition2D pos = get_pos(flex_get_cluster_id());
    bare_dma_start_1d(local(local_offset), remote_pos(bottom_pos(pos), remote_offset), transfer_size); // Start iDMA
}

// Pattern Dialog-to-Dialog: Transpose the X and Y coordinates in the 2D grid
static inline void flex_dma_async_pattern_dialog_to_dialog(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    FlexPosition2D pos = get_pos(flex_get_cluster_id());
    bare_dma_start_1d(local(local_offset), remote_xy(pos.y, pos.x, remote_offset), transfer_size); // Start iDMA
}

#endif // _FLEX_DMA_PATTERN_2D_H_