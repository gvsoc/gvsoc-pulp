#ifndef _FLEX_DMA_PATTERN_RING_H_
#define _FLEX_DMA_PATTERN_RING_H_

#include <stdint.h>

/***********************************
 *  1D Cluster Position Math       *
 ***********************************/

// 1D Ring positioning functions
static inline uint32_t right_pos(uint32_t cluster_id) {
    return (cluster_id + 1) % ARCH_NUM_CLUSTER;
}

static inline uint32_t left_pos(uint32_t cluster_id) {
    return (cluster_id + ARCH_NUM_CLUSTER - 1) % ARCH_NUM_CLUSTER;
}


/***********************************
 *  1D Address Resolution          *
 ***********************************/

#define remote_id(id, offset) \
    (ARCH_CLUSTER_TCDM_REMOTE + (id) * ARCH_CLUSTER_TCDM_SIZE + (offset))


/*******************************************
 *  Traffic Pattern: Asynchronous Interface *
 *******************************************/

// Pattern: Round Shift Right
static inline void flex_dma_async_pattern_round_shift_right(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    uint32_t my_id = flex_get_cluster_id();
    uint32_t left_id = left_pos(my_id);
    bare_dma_start_1d(local(local_offset), remote_id(left_id, remote_offset), transfer_size); // Start iDMA
}

// Pattern: Round Shift Left
static inline void flex_dma_async_pattern_round_shift_left(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    uint32_t my_id = flex_get_cluster_id();
    uint32_t right_id = right_pos(my_id);
    bare_dma_start_1d(local(local_offset), remote_id(right_id, remote_offset), transfer_size); // Start iDMA
}

// Pattern: Dialog-to-Dialog (Ring: Diametrically Opposite Node)
static inline void flex_dma_async_pattern_dialog_to_dialog(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    uint32_t my_id = flex_get_cluster_id();
    
    // Calculate the node exactly halfway across the ring
    uint32_t opposite_id = (my_id + (ARCH_NUM_CLUSTER / 2)) % ARCH_NUM_CLUSTER;
    
    bare_dma_start_1d(local(local_offset), remote_id(opposite_id, remote_offset), transfer_size); // Start iDMA
}

#endif // _FLEX_DMA_PATTERN_RING_H_