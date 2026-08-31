#ifndef _FLEX_DMA_PATTERN_H_
#define _FLEX_DMA_PATTERN_H_

#include "softhier_runtime.h"
#include "softhier_arch.h"
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
    ((funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | \
     (opcode))

inline uint32_t bare_dma_start_1d(uint64_t dst, uint64_t src,
                                          size_t size) {
    register uint32_t reg_dst_low asm("a0") = dst >> 0;    // 10
    register uint32_t reg_dst_high asm("a1") = dst >> 32;  // 11
    register uint32_t reg_src_low asm("a2") = src >> 0;    // 12
    register uint32_t reg_src_high asm("a3") = src >> 32;  // 13
    register uint32_t reg_size asm("a4") = size;           // 14

    // dmsrc a2, a3
    asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMSRC_FUNCT7, 13, 12,
                                                  XDMA_FUNCT3, 0, OP_CUSTOM1)),
                 "r"(reg_src_high), "r"(reg_src_low));

    // dmdst a0, a1
    asm volatile(".word %0\n" ::"i"(R_TYPE_ENCODE(DMDST_FUNCT7, 11, 10,
                                                  XDMA_FUNCT3, 0, OP_CUSTOM1)),
                 "r"(reg_dst_high), "r"(reg_dst_low));

    // dmcpyi a0, a4, 0b00
    register uint32_t reg_txid asm("a0");  // 10
    asm volatile(".word %1\n"
                 : "=r"(reg_txid)
                 : "i"(R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00000, 14, XDMA_FUNCT3,
                                     10, OP_CUSTOM1)),
                   "r"(reg_size));

    return reg_txid;
}

inline uint32_t bare_dma_start_2d(uint64_t dst, uint64_t src,
                                                 size_t size, size_t dst_stride,
                                                 size_t src_stride,
                                                 size_t repeat) {
    register uint32_t reg_dst_low asm("a0") = dst >> 0;       // 10
    register uint32_t reg_dst_high asm("a1") = dst >> 32;     // 11
    register uint32_t reg_src_low asm("a2") = src >> 0;       // 12
    register uint32_t reg_src_high asm("a3") = src >> 32;     // 13
    register uint32_t reg_size asm("a4") = size;              // 14
    register uint32_t reg_dst_stride asm("a5") = dst_stride;  // 15
    register uint32_t reg_src_stride asm("a6") = src_stride;  // 16
    register uint32_t reg_repeat asm("a7") = repeat;          // 17

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
    register uint32_t reg_txid asm("a0");  // 10
    asm volatile(".word %1\n"
                 : "=r"(reg_txid)
                 : "i"(R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00010, 14, XDMA_FUNCT3,
                                     10, OP_CUSTOM1)),
                   "r"(reg_size));

    return reg_txid;
}


inline void bare_dma_wait_all() {
    // dmstati t0, 2  # 2=status.busy
    asm volatile(
        "1: \n"
        ".word %0\n"
        "bne t0, zero, 1b \n" ::"i"(
            R_TYPE_ENCODE(DMSTATI_FUNCT7, 0b10, 0, XDMA_FUNCT3, 5, OP_CUSTOM1))
        : "t0");
}


/*************************************
*  Basic Asynchronize DMA Interface  *
*************************************/

//Basic DMA 1d transfter
void flex_dma_async_1d(uint64_t dst_addr, uint64_t src_addr, size_t transfer_size){
    bare_dma_start_1d(dst_addr, src_addr, transfer_size); //Start iDMA
}

//Basic DMA 2d transfter
void flex_dma_async_2d(uint64_t dst_addr, uint64_t src_addr, size_t transfer_size, size_t dst_stride, size_t src_stride, size_t repeat){
    bare_dma_start_2d(dst_addr, src_addr, transfer_size, dst_stride, src_stride, repeat); //Start iDMA
}

//wait for idma
void flex_dma_async_wait_all(){
    bare_dma_wait_all(); // Wait for iDMA Finishing
}

// --- Topology-Specific Pattern Routing ---
// 1 = 1D (Ring / Hierarchical Ring)
// 2 = 2D (Mesh / Torus)
// 3 = 3D (Mesh / Torus)
// 4 = Hexagonal (HexaMesh / FoldedHexaTorus)

#if ARCH_SHAPE_CATEGORY == 1
    #include "softhier_dma_pattern_ring.h"
#elif ARCH_SHAPE_CATEGORY == 2
    #include "softhier_dma_pattern_2d.h"
#elif ARCH_SHAPE_CATEGORY == 3
    #include "softhier_dma_pattern_3d.h"
#elif ARCH_SHAPE_CATEGORY == 4
    #include "softhier_dma_pattern_hexa.h"
#else
    #error "ARCH_SHAPE_CATEGORY not defined or unsupported in softhier_arch.py"
#endif

#endif