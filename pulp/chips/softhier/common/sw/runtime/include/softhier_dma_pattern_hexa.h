#ifndef _FLEX_DMA_PATTERN_HEXA_H_
#define _FLEX_DMA_PATTERN_HEXA_H_

#include <stdint.h>
#include "softhier_printf.h"

/***********************************
 *  Hexagonal Position Math        *
 ***********************************/

typedef struct FlexPositionHexa
{
    int32_t q;
    int32_t r;
} FlexPositionHexa;

// Absolute value helper
static inline int32_t hex_abs(int32_t v) {
    return (v < 0) ? -v : v;
}

// Maximum of three helper
static inline int32_t hex_max(int32_t a, int32_t b, int32_t c) {
    int32_t m = (a > b) ? a : b;
    return (m > c) ? m : c;
}

// Distance from center (0,0) in a hex grid
static inline int32_t hex_dist(FlexPositionHexa pos) {
    return hex_max(hex_abs(pos.q), hex_abs(pos.r), hex_abs(pos.q + pos.r));
}

// Maps linear ID to (q, r) mirroring NoC instantiation
static inline FlexPositionHexa get_pos(uint32_t cluster_id) {
    FlexPositionHexa pos = {0, 0};
    if (cluster_id == 0) return pos;

    // ring_walk_dirs: (-1, 1), (-1, 0), (0, -1), (1, -1), (1, 0), (0, 1)
    int32_t dq[6] = {-1, -1, 0, 1, 1, 0};
    int32_t dr[6] = { 1,  0, -1, -1, 0, 1};
    
    uint32_t current_id = 1;
    int32_t ring = 1;

    while (current_id <= cluster_id) {
        int32_t q = ring;
        int32_t r = 0;
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < ring; j++) {
                if (current_id == cluster_id) {
                    pos.q = q;
                    pos.r = r;
                    return pos;
                }
                q += dq[i];
                r += dr[i];
                current_id++;
            }
        }
        ring++;
    }
    return pos;
}

// Maps (q, r) back to linear ID efficiently by jumping straight to the required ring
static inline int32_t get_id(FlexPositionHexa pos) {
    if (pos.q == 0 && pos.r == 0) return 0;
    
    int32_t ring = hex_dist(pos);
    if (ring > ARCH_NUM_RINGS) return -1; // Out of bounds
    
    int32_t dq[6] = {-1, -1, 0, 1, 1, 0};
    int32_t dr[6] = { 1,  0, -1, -1, 0, 1};
    
    // Calculate the starting ID of this ring (1 + 3 * ring * (ring - 1))
    uint32_t current_id = 1 + 3 * ring * (ring - 1);
    int32_t cur_q = ring;
    int32_t cur_r = 0;
    
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < ring; j++) {
            if (cur_q == pos.q && cur_r == pos.r) return current_id;
            cur_q += dq[i];
            cur_r += dr[i];
            current_id++;
        }
    }
    return -1;
}

// Identify if the current topology is a Torus
static inline int32_t is_torus() {
    // "FoldedHexaTorus" starts with 'F', "HexaMesh" starts with 'H'
    return ARCH_TOPOLOGY[0] == 'F'; 
}

/***********************************
 *  Hexagonal Directional Shifts   *
 ***********************************/

// Applies the periodic lattice translation vectors (FoldedHexaTorus wrap-around)
static inline FlexPositionHexa wrap_hexa(FlexPositionHexa pos) {
    if (hex_dist(pos) <= ARCH_NUM_RINGS) return pos; // Node is safely on-chip
    
    int32_t R = ARCH_NUM_RINGS;
    
    // C_vectors from FoldedHexaTorus Python config
    int32_t C_q[6] = {R + 1, -R, -2 * R - 1, -R - 1, R, 2 * R + 1};
    int32_t C_r[6] = {R, 2 * R + 1, R + 1, -R, -2 * R - 1, -R - 1};
    
    for (int i = 0; i < 6; i++) {
        FlexPositionHexa wrapped = {pos.q - C_q[i], pos.r - C_r[i]};
        if (hex_dist(wrapped) <= ARCH_NUM_RINGS) {
            return wrapped;
        }
    }
    
    return pos; // Fallback (should not be reached for immediate neighbors)
}

static inline FlexPositionHexa west_pos(FlexPositionHexa pos) {
    FlexPositionHexa new_pos = {pos.q - 1, pos.r};
    return is_torus() ? wrap_hexa(new_pos) : new_pos;
}

static inline FlexPositionHexa northwest_pos(FlexPositionHexa pos) {
    FlexPositionHexa new_pos = {pos.q, pos.r - 1};
    return is_torus() ? wrap_hexa(new_pos) : new_pos;
}

static inline FlexPositionHexa opposite_pos(FlexPositionHexa pos) {
    // Diametrically opposite in axial coordinates is just the negative values
    FlexPositionHexa new_pos = {-pos.q, -pos.r};
    // No wrapping needed, as the negative of a valid coordinate is always a valid coordinate
    return new_pos;
}


/***********************************
 *  Hexagonal Address Resolution   *
 ***********************************/

#define remote_hexa(pos, offset) \
    (ARCH_CLUSTER_TCDM_REMOTE + get_id(pos) * ARCH_CLUSTER_TCDM_SIZE + (offset))


/*******************************************
 *  Traffic Pattern: Asynchronous Interface *
 *******************************************/

// Pattern: Round Shift Right (Read from West Neighbor on Q Axis)
static inline void flex_dma_async_pattern_round_shift_right(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    if(flex_get_cluster_id() == 0){
        printf("flex_dma_async_pattern_round_shift_right is not implemented for Hexagonal topologies \n");
    }
}

// Pattern: Round Shift Up (Read from NorthWest Neighbor on R Axis)
static inline void flex_dma_async_pattern_round_shift_up(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    if(flex_get_cluster_id() == 0){
        printf("flex_dma_async_pattern_round_shift_up is not implemented for Hexagonal topologies \n");
    }
}

// Pattern: Dialog-to-Dialog (Target diametrically opposite node through the center)
static inline void flex_dma_async_pattern_dialog_to_dialog(uint32_t local_offset, uint32_t remote_offset, size_t transfer_size) {
    if(flex_get_cluster_id() == 0){
        printf("flex_dma_async_pattern_dialog_to_dialog is not implemented for Hexagonal topologies \n");
    }
}

#endif // _FLEX_DMA_PATTERN_HEXA_H_