#ifndef _FLEX_DMA_PATTERN_H_
#define _FLEX_DMA_PATTERN_H_

#include "softhier_arch.h"
#include "softhier_runtime.h"
#include <stddef.h>

/********************************************
 *  iDMA Trigger Fucntions (Customized ISA)  *
 ********************************************/

#define OP_CUSTOM1 0b0101011
#define XDMA_FUNCT3 0b000
#define DMSRC_FUNCT7 0b0000000
#define DMDST_FUNCT7 0b0000001
#define DMCPYI_FUNCT7 0b0000010
#define DMCPYC_FUNCT7 0b0000011
#define DMSTATI_FUNCT7 0b0000100
#define DMMASK_FUNCT7 0b0000101
#define DMSTR_FUNCT7 0b0000110
#define DMREP_FUNCT7 0b0000111

#define R_TYPE_ENCODE(funct7, rs2, rs1, funct3, rd, opcode)                    \
  ((funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) |   \
   (opcode))

inline uint32_t bare_dma_start_1d(uint64_t dst, uint64_t src, size_t size) {
  register uint32_t reg_dst_low asm("a0") = dst >> 0;   // 10
  register uint32_t reg_dst_high asm("a1") = dst >> 32; // 11
  register uint32_t reg_src_low asm("a2") = src >> 0;   // 12
  register uint32_t reg_src_high asm("a3") = src >> 32; // 13
  register uint32_t reg_size asm("a4") = size;          // 14

  // dmsrc a2, a3
  asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMSRC_FUNCT7, 13, 12,
                                                XDMA_FUNCT3, 0, OP_CUSTOM1)),
               "r"(reg_src_high), "r"(reg_src_low));

  // dmdst a0, a1
  asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMDST_FUNCT7, 11, 10,
                                                XDMA_FUNCT3, 0, OP_CUSTOM1)),
               "r"(reg_dst_high), "r"(reg_dst_low));

  // dmcpyi a0, a4, 0b00
  register uint32_t reg_txid asm("a0"); // 10
  asm volatile(".word %1\n"
               : "=r"(reg_txid)
               : "i"(R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00000, 14, XDMA_FUNCT3, 10,
                                   OP_CUSTOM1)),
                 "r"(reg_size));

  return reg_txid;
}

inline uint32_t bare_dma_start_2d(uint64_t dst, uint64_t src, size_t size,
                                  size_t dst_stride, size_t src_stride,
                                  size_t repeat) {
  register uint32_t reg_dst_low asm("a0") = dst >> 0;      // 10
  register uint32_t reg_dst_high asm("a1") = dst >> 32;    // 11
  register uint32_t reg_src_low asm("a2") = src >> 0;      // 12
  register uint32_t reg_src_high asm("a3") = src >> 32;    // 13
  register uint32_t reg_size asm("a4") = size;             // 14
  register uint32_t reg_dst_stride asm("a5") = dst_stride; // 15
  register uint32_t reg_src_stride asm("a6") = src_stride; // 16
  register uint32_t reg_repeat asm("a7") = repeat;         // 17

  // dmsrc a0, a1
  asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMSRC_FUNCT7, 13, 12,
                                                XDMA_FUNCT3, 0, OP_CUSTOM1)),
               "r"(reg_src_high), "r"(reg_src_low));

  // dmdst a0, a1
  asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMDST_FUNCT7, 11, 10,
                                                XDMA_FUNCT3, 0, OP_CUSTOM1)),
               "r"(reg_dst_high), "r"(reg_dst_low));

  // dmstr a5, a6
  asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMSTR_FUNCT7, 15, 16,
                                                XDMA_FUNCT3, 0, OP_CUSTOM1)),
               "r"(reg_src_stride), "r"(reg_dst_stride));

  // dmrep a7
  asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMREP_FUNCT7, 0, 17,
                                                XDMA_FUNCT3, 0, OP_CUSTOM1)),
               "r"(reg_repeat));

  // dmcpyi a0, a4, 0b10
  register uint32_t reg_txid asm("a0"); // 10
  asm volatile(".word %1\n"
               : "=r"(reg_txid)
               : "i"(R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00010, 14, XDMA_FUNCT3, 10,
                                   OP_CUSTOM1)),
                 "r"(reg_size));

  return reg_txid;
}

inline void bare_dma_wait_all() {
  // dmstati t0, 2  # 2=status.busy
  asm volatile("1: \n"
               ".word %0\n"
               "bne t0, zero, 1b \n" ::"i"(R_TYPE_ENCODE(
                   DMSTATI_FUNCT7, 0b10, 0, XDMA_FUNCT3, 5, OP_CUSTOM1))
               : "t0");
}

/*************************************
 *  Basic Asynchronize DMA Interface  *
 *************************************/

// Basic DMA 1d transfter
void flex_dma_async_1d(uint64_t dst_addr, uint64_t src_addr,
                       size_t transfer_size) {
  bare_dma_start_1d(dst_addr, src_addr, transfer_size); // Start iDMA
}

// Basic DMA 2d transfter
void flex_dma_async_2d(uint64_t dst_addr, uint64_t src_addr,
                       size_t transfer_size, size_t dst_stride,
                       size_t src_stride, size_t repeat) {
  bare_dma_start_2d(dst_addr, src_addr, transfer_size, dst_stride, src_stride,
                    repeat); // Start iDMA
}

// wait for idma
void flex_dma_async_wait_all() {
  bare_dma_wait_all(); // Wait for iDMA Finishing
}

/*******************
 * Cluster Position *
 *******************/

// 1D Ring positioning functions
uint32_t right_pos(uint32_t cluster_id) {
  return (cluster_id + 1) % ARCH_NUM_CLUSTER;
}

uint32_t left_pos(uint32_t cluster_id) {
  return (cluster_id + ARCH_NUM_CLUSTER - 1) % ARCH_NUM_CLUSTER;
}

#define remote_id(id, offset)                                                  \
  (ARCH_CLUSTER_TCDM_REMOTE + (id) * ARCH_CLUSTER_TCDM_SIZE + offset)

/*******************************************
 *  Traffic Pattern: Asynchronize Interface *
 *******************************************/

// Pattern: Round Shift Right (Read from Left Neighbor)
void flex_dma_async_pattern_round_shift_right(uint32_t local_offset,
                                              uint32_t remote_offset,
                                              size_t transfer_size) {
  uint32_t my_id = flex_get_cluster_id();
  uint32_t left_id = left_pos(my_id);
  bare_dma_start_1d(local(local_offset), remote_id(left_id, remote_offset),
                    transfer_size); // Start iDMA
}

// Pattern: Round Shift Left (Read from Right Neighbor)
void flex_dma_async_pattern_round_shift_left(uint32_t local_offset,
                                             uint32_t remote_offset,
                                             size_t transfer_size) {
  uint32_t my_id = flex_get_cluster_id();
  uint32_t right_id = right_pos(my_id);
  bare_dma_start_1d(local(local_offset), remote_id(right_id, remote_offset),
                    transfer_size); // Start iDMA
}

// Pattern: Dialog-to-Dialog (Adapted for Ring: Diametrically Opposite Node)
void flex_dma_async_pattern_dialog_to_dialog(uint32_t local_offset,
                                             uint32_t remote_offset,
                                             size_t transfer_size) {
  uint32_t my_id = flex_get_cluster_id();

  // Calculate the node exactly halfway across the ring
  uint32_t opposite_id = (my_id + (ARCH_NUM_CLUSTER / 2)) % ARCH_NUM_CLUSTER;

  bare_dma_start_1d(local(local_offset), remote_id(opposite_id, remote_offset),
                    transfer_size); // Start iDMA
}

#endif