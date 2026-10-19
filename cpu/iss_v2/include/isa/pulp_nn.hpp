// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Angelo Garofalo (angelo.garofalo@unibo.it), Germain Haugou (germain.haugou@gmail.com)

/*
 * PULP-NN ISA extension (XpulpNN) on iss_v2, for the Ri5kyNn core.
 *
 * The SIMD operations on nibbles and crumbs are those of the iss (v1) one
 * (cpu/iss/include/isa/pulp_nn.hpp). The mac-load instructions and pv.qnt,
 * which load into registers of the core outside the register file (the 6
 * special-purpose registers, the quantization thresholds), go through the
 * Ri5kyNn LSU (Ri5kyNnLsu::load_ext): an instruction needing one of them
 * while its load is in flight waits for it.
 */

#pragma once

#define WSPR_UPDATE_MASK 0x10
#define WSPR_UPDATE_POS 4
#define ASPR_UPDATE_MASK 0x08
#define ASPR_UPDATE_POS 3
#define WSPR_ADDR_MASK 0x06
#define WSPR_ADDR_POS 1
#define ASPR_ADDR_MASK 0x01
#define ASPR_ADDR_POS 0

#define PV_OP_RS_EXEC_NN(insn_name, lib_name)                                                     \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                 \
    {                                                                                             \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_int4_t_to_int32_t, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                        \
    }                                                                                             \
                                                                                                  \
    static inline iss_reg_t pv_##insn_name##_sc_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                             \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_int4_t_to_int32_t, REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                        \
    }                                                                                             \
                                                                                                  \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                 \
    {                                                                                             \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_int2_t_to_int32_t, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                        \
    }                                                                                             \
                                                                                                  \
    static inline iss_reg_t pv_##insn_name##_sc_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)              \
    {                                                                                             \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_int2_t_to_int32_t, REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                        \
    }

#define PV_OP_RU_EXEC_NN(insn_name, lib_name)                                                       \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                   \
    {                                                                                               \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_uint4_t_to_uint32_t, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                          \
    }                                                                                               \
                                                                                                    \
    static inline iss_reg_t pv_##insn_name##_sc_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                \
    {                                                                                               \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_uint4_t_to_uint32_t, REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                          \
    }                                                                                               \
                                                                                                    \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                   \
    {                                                                                               \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_uint2_t_to_uint32_t, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                          \
    }                                                                                               \
                                                                                                    \
    static inline iss_reg_t pv_##insn_name##_sc_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)                \
    {                                                                                               \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_uint2_t_to_uint32_t, REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                          \
    }

PV_OP_RS_EXEC_NN(add, ADD)
PV_OP_RS_EXEC_NN(sub, SUB)
PV_OP_RS_EXEC_NN(avg, AVG)
PV_OP_RU_EXEC_NN(avgu, AVGU)
PV_OP_RS_EXEC_NN(max, MAX)
PV_OP_RU_EXEC_NN(maxu, MAXU)
PV_OP_RS_EXEC_NN(min, MIN)
PV_OP_RU_EXEC_NN(minu, MINU)

PV_OP_RU_EXEC_NN(srl, SRL)

PV_OP_RS_EXEC_NN(sra, SRA)

PV_OP_RU_EXEC_NN(sll, SLL)

PV_OP_RS_EXEC_NN(or, OR)

PV_OP_RS_EXEC_NN(xor, XOR)

PV_OP_RS_EXEC_NN(and, AND)

#define PV_OP1_RS_EXEC_NN(insn_name, lib_name)                                     \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
    {                                                                              \
        REG_SET(0, LIB_CALL1(lib_VEC_##lib_name##_int4_t_to_int32_t, REG_GET(0))); \
        return iss_insn_next(iss, insn, pc);                                                         \
    }                                                                              \
                                                                                   \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)  \
    {                                                                              \
        REG_SET(0, LIB_CALL1(lib_VEC_##lib_name##_int2_t_to_int32_t, REG_GET(0))); \
        return iss_insn_next(iss, insn, pc);                                                         \
    }

PV_OP1_RS_EXEC_NN(abs, ABS)

#define PV_OP_RS_EXEC_NN_2(insn_name, lib_name)                                      \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)    \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_4, REG_GET(0), REG_GET(1)));       \
        return iss_insn_next(iss, insn, pc);                                                           \
    }                                                                                \
                                                                                     \
    static inline iss_reg_t pv_##insn_name##_n_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_4, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                           \
    }                                                                                \
                                                                                     \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)    \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_2, REG_GET(0), REG_GET(1)));       \
        return iss_insn_next(iss, insn, pc);                                                           \
    }                                                                                \
                                                                                     \
    static inline iss_reg_t pv_##insn_name##_c_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_2, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                           \
    }

#define PV_OP_RU_EXEC_NN_2(insn_name, lib_name)                                      \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)    \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_4, REG_GET(0), REG_GET(1)));       \
        return iss_insn_next(iss, insn, pc);                                                           \
    }                                                                                \
                                                                                     \
    static inline iss_reg_t pv_##insn_name##_n_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_4, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                           \
    }                                                                                \
                                                                                     \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)    \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_2, REG_GET(0), REG_GET(1)));       \
        return iss_insn_next(iss, insn, pc);                                                           \
    }                                                                                \
                                                                                     \
    static inline iss_reg_t pv_##insn_name##_c_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc) \
    {                                                                                \
        REG_SET(0, LIB_CALL2(lib_VEC_##lib_name##_SC_2, REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                           \
    }

PV_OP_RS_EXEC_NN_2(dotsp, DOTSP)

PV_OP_RU_EXEC_NN_2(dotup, DOTUP)

PV_OP_RS_EXEC_NN_2(dotusp, DOTUSP)

#define PV_OP_RRS_EXEC_NN_2(insn_name, lib_name)                                              \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)             \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_4, REG_GET(2), REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }                                                                                         \
                                                                                              \
    static inline iss_reg_t pv_##insn_name##_n_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_4, REG_GET(2), REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }                                                                                         \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)             \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_2, REG_GET(2), REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }                                                                                         \
                                                                                              \
    static inline iss_reg_t pv_##insn_name##_c_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_2, REG_GET(2), REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }

PV_OP_RRS_EXEC_NN_2(sdotsp, SDOTSP)
PV_OP_RRS_EXEC_NN_2(sdotusp, SDOTUSP)

#define PV_OP_RRU_EXEC_NN_2(insn_name, lib_name)                                              \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)             \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_4, REG_GET(2), REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }                                                                                         \
                                                                                              \
    static inline iss_reg_t pv_##insn_name##_n_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_4, REG_GET(2), REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }                                                                                         \
                                                                                              \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)             \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_2, REG_GET(2), REG_GET(0), REG_GET(1)));    \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }                                                                                         \
                                                                                              \
    static inline iss_reg_t pv_##insn_name##_c_sc_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)          \
    {                                                                                         \
        REG_SET(0, LIB_CALL3(lib_VEC_##lib_name##_SC_2, REG_GET(2), REG_GET(0), REG_GET(1))); \
        return iss_insn_next(iss, insn, pc);                                                                    \
    }

PV_OP_RRU_EXEC_NN_2(sdotup, SDOTUP)


// Mac-load: rd += dot(SPR[a], SPR[w]) on elements of `bits`, and, with one of
// the update bits of the immediate, the next word of rs1 is loaded into the
// activation or the weight SPR, rs1 being post-incremented.
template<typename F>
static inline iss_reg_t pulp_nn_ml_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc, F dotp,
    bool sign_a)
{
    PulpNnState &nn = iss->arch.nn;
    iss_uim_t ctl_imm = UIM_GET(0);

    int ac_addr = ((ctl_imm & ASPR_ADDR_MASK) >> ASPR_ADDR_POS);
    int wt_addr = ((ctl_imm & WSPR_ADDR_MASK) >> WSPR_ADDR_POS) + 0x2;
    bool ac_update = ((ctl_imm & ASPR_UPDATE_MASK) >> ASPR_UPDATE_POS);
    bool wt_update = ((ctl_imm & WSPR_UPDATE_MASK) >> WSPR_UPDATE_POS);

    // The SPRs read must be there
    if (nn.spr_busy & ((1 << ac_addr) | (1 << wt_addr)))
    {
        iss->exec.insn_stall();
        return pc;
    }

    iss_reg_t result = sign_a ?
        (iss_reg_t)dotp(iss, REG_GET(0), nn.spr[ac_addr], nn.spr[wt_addr]) :
        (iss_reg_t)dotp(iss, REG_GET(0), nn.spr[wt_addr], nn.spr[ac_addr]);

    if (ac_update || wt_update)
    {
        int spr = ac_update ? ac_addr : wt_addr;
        iss_reg_t addr = REG_GET(1);
        if (iss->lsu.load_ext(insn, addr, 4, spr, &nn.spr[spr], &nn.spr_busy, 1 << spr))
        {
            // Not issued, the instruction is retried
            return pc;
        }
        IN_REG_SET(1, addr + 4);
    }

    REG_SET(0, result);
    return iss_insn_next(iss, insn, pc);
}

#define PV_OP_RRRU3_EXEC_NN(insn_name, lib_name, signOpA)                                       \
    static inline iss_reg_t pv_##insn_name##_h_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)   \
    {                                                                                           \
        return pulp_nn_ml_exec(iss, insn, pc, lib_VEC_##lib_name##_16, signOpA);                \
    }                                                                                           \
    static inline iss_reg_t pv_##insn_name##_b_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)   \
    {                                                                                           \
        return pulp_nn_ml_exec(iss, insn, pc, lib_VEC_##lib_name##_8, signOpA);                 \
    }                                                                                           \
    static inline iss_reg_t pv_##insn_name##_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)   \
    {                                                                                           \
        return pulp_nn_ml_exec(iss, insn, pc, lib_VEC_##lib_name##_4, signOpA);                 \
    }                                                                                           \
    static inline iss_reg_t pv_##insn_name##_c_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)   \
    {                                                                                           \
        return pulp_nn_ml_exec(iss, insn, pc, lib_VEC_##lib_name##_2, signOpA);                 \
    }

PV_OP_RRRU3_EXEC_NN(mlsdotup, SDOTUP, 0)
PV_OP_RRRU3_EXEC_NN(mlsdotusp, SDOTUSP, 0)
PV_OP_RRRU3_EXEC_NN(mlsdotsup, SDOTUSP, 1)
PV_OP_RRRU3_EXEC_NN(mlsdotsp, SDOTSP, 1)

// pv.qnt.n: 4-bit quantization of rs1 by a binary search in the tree of 15
// 16-bit thresholds at rs2, one threshold load per step, the instruction
// being executed again for each step (steps 1 to 4, after a first cycle).
static inline iss_reg_t pv_qnt_n_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    PulpNnState &nn = iss->arch.nn;
    iss_reg_t input = REG_GET(0);

    if (nn.qnt_busy)
    {
        iss->exec.insn_stall();
        return pc;
    }

    if (nn.qnt_step == 0)
    {
        nn.qnt_addr = REG_GET(1);
        nn.qnt_out = 0;
        nn.qnt_loaded = false;
        nn.qnt_step++;
        return pc;
    }

    if (!nn.qnt_loaded)
    {
        if (iss->lsu.load_ext(insn, nn.qnt_addr, 2, RI5KYNN_LSU_EXT_QNT, &nn.qnt_threshold,
            &nn.qnt_busy, 1))
        {
            return pc;
        }
        nn.qnt_loaded = true;
        if (nn.qnt_busy)
        {
            // Compared when it is there
            return pc;
        }
    }

    nn.qnt_loaded = false;
    int16_t threshold = (int16_t)nn.qnt_threshold;
    int step = nn.qnt_step;

    if (step == 1)
    {
        // The first comparison goes the other way
        nn.qnt_out = input <= threshold ? 1 : 0;
        nn.qnt_addr = input <= threshold ? nn.qnt_addr - 4 * 2 : nn.qnt_addr + 4 * 2;
    }
    else
    {
        nn.qnt_out |= input > threshold ? 1 : 0;
        int offset = (4 - step) * 2;
        nn.qnt_addr = input > threshold ? nn.qnt_addr + offset : nn.qnt_addr - offset;
    }

    if (step < 4)
    {
        nn.qnt_out <<= 1;
        nn.qnt_step++;
        return pc;
    }

    nn.qnt_step = 0;
    iss_reg_t out = nn.qnt_out;
    out = (out & 0x08) ? (out | 0xFFFFFFF0) : (out & 0x0000000F);
    REG_SET(0, out);
    return iss_insn_next(iss, insn, pc);
}
