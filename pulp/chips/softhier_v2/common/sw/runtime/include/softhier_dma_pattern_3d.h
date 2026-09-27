#ifndef _FLEX_DMA_PATTERN_3D_H_
#define _FLEX_DMA_PATTERN_3D_H_

#include <stdint.h>

/***********************************
 *  3D Cluster Position Math       *
 ***********************************/

typedef struct FlexPosition3D
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
} FlexPosition3D;

// Extracts the 3D coordinate from the linear cluster ID.
static inline FlexPosition3D get_pos(uint32_t cluster_id) {
    FlexPosition3D pos;
    pos.x = cluster_id % ARCH_NUM_CLUSTER_X;
    pos.y = (cluster_id / ARCH_NUM_CLUSTER_X) % ARCH_NUM_CLUSTER_Y;
    pos.z = cluster_id / (ARCH_NUM_CLUSTER_X * ARCH_NUM_CLUSTER_Y);
    return pos;
}

// X-Axis shifts
static inline FlexPosition3D right_pos(FlexPosition3D pos) {
    FlexPosition3D new_pos = pos;
    new_pos.x = (pos.x + 1) % ARCH_NUM_CLUSTER_X;
    return new_pos;
}

static inline FlexPosition3D left_pos(FlexPosition3D pos) {
    FlexPosition3D new_pos = pos;
    new_pos.x = (pos.x + ARCH_NUM_CLUSTER_X - 1) % ARCH_NUM_CLUSTER_X;
    return new_pos;
}

// Y-Axis shifts
static inline FlexPosition3D top_pos(FlexPosition3D pos) {
    FlexPosition3D new_pos = pos;
    new_pos.y = (pos.y + 1) % ARCH_NUM_CLUSTER_Y;
    return new_pos;
}

static inline FlexPosition3D bottom_pos(FlexPosition3D pos) {
    FlexPosition3D new_pos = pos;
    new_pos.y = (pos.y + ARCH_NUM_CLUSTER_Y - 1) % ARCH_NUM_CLUSTER_Y;
    return new_pos;
}

// Z-Axis shifts
static inline FlexPosition3D front_pos(FlexPosition3D pos) {
    FlexPosition3D new_pos = pos;
    new_pos.z = (pos.z + 1) % ARCH_NUM_CLUSTER_Z;
    return new_pos;
}

static inline FlexPosition3D back_pos(FlexPosition3D pos) {
    FlexPosition3D new_pos = pos;
    new_pos.z = (pos.z + ARCH_NUM_CLUSTER_Z - 1) % ARCH_NUM_CLUSTER_Z;
    return new_pos;
}

/***********************************
 *  3D Address Resolution          *
 ***********************************/

#define cluster_index(x, y, z) \
    (((z) * ARCH_NUM_CLUSTER_X * ARCH_NUM_CLUSTER_Y) + ((y) * ARCH_NUM_CLUSTER_X) + (x))

#define remote_xyz(x, y, z, offset) \
    (ARCH_CLUSTER_TCDM_REMOTE + cluster_index(x, y, z) * ARCH_CLUSTER_TCDM_SIZE + offset)

#define remote_pos(pos, offset) \
    (ARCH_CLUSTER_TCDM_REMOTE + cluster_index(pos.x, pos.y, pos.z) * ARCH_CLUSTER_TCDM_SIZE + offset)


/*******************************************
 *  Traffic Pattern: Asynchronous Interface *
 *******************************************/

// Pattern: Round Shift Right
static inline void flex_dma_async_pattern_round_shift_right(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    FlexPosition3D pos = get_pos(flex_get_cluster_id());
    bare_dma_start_1d(local(local_offset), remote_pos(left_pos(pos), remote_offset), transfer_size); // Start iDMA
}

// Pattern: Round Shift Up
static inline void flex_dma_async_pattern_round_shift_up(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    FlexPosition3D pos = get_pos(flex_get_cluster_id());
    bare_dma_start_1d(local(local_offset), remote_pos(bottom_pos(pos), remote_offset), transfer_size); // Start iDMA
}

// Pattern: Round Shift In
static inline void flex_dma_async_pattern_round_shift_in(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    FlexPosition3D pos = get_pos(flex_get_cluster_id());
    bare_dma_start_1d(local(local_offset), remote_pos(back_pos(pos), remote_offset), transfer_size); // Start iDMA
}

// Pattern Dialog-to-Dialog: Target the diametrically opposite node in the 3D grid
static inline void flex_dma_async_pattern_dialog_to_dialog(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    FlexPosition3D pos = get_pos(flex_get_cluster_id());
    
    uint32_t opp_x = (pos.x + (ARCH_NUM_CLUSTER_X / 2)) % ARCH_NUM_CLUSTER_X;
    uint32_t opp_y = (pos.y + (ARCH_NUM_CLUSTER_Y / 2)) % ARCH_NUM_CLUSTER_Y;
    uint32_t opp_z = (pos.z + (ARCH_NUM_CLUSTER_Z / 2)) % ARCH_NUM_CLUSTER_Z;

    bare_dma_start_1d(local(local_offset), remote_xyz(opp_x, opp_y, opp_z, remote_offset), transfer_size); // Start iDMA
}

#endif // _FLEX_DMA_PATTERN_3D_H_