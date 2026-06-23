/*
 * Compatibility handlers for old SoftHier custom RVV encodings.
 *
 * The applications encode these operations with raw .word values:
 *   vfexp.vv     0x32041857
 *   vfredmax.vx  0x1e88b057
 *   vfredsum.vx  0x0688b057
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#ifdef CONFIG_GVSOC_ISS_V2
#include <cpu/iss_v2/include/iss.hpp>
#include <cpu/iss_v2/include/isa_lib/macros.h>
#else
#include <cpu/iss/include/iss.hpp>
#include <cpu/iss/include/isa_lib/macros.h>
#endif

#include <cpu/iss/include/isa_lib/int.h>
#include <cpu/iss/include/isa/rv32v_timed.hpp>

static inline double soft_hier_double_from_bits(uint64_t bits)
{
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline uint64_t soft_hier_bits_from_double(double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline double soft_hier_vec_float_to_double(Iss *iss, uint64_t value)
{
    uint64_t bits = LIB_FF_CALL4(
        lib_flexfloat_cvt_ff_ff_round,
        value,
        iss->vector.exp,
        iss->vector.mant,
        11,
        52,
        7
    );

    return soft_hier_double_from_bits(bits);
}

static inline uint64_t soft_hier_double_to_vec_float(Iss *iss, double value)
{
    uint64_t bits = soft_hier_bits_from_double(value);

    return LIB_FF_CALL4(
        lib_flexfloat_cvt_ff_ff_round,
        bits,
        11,
        52,
        iss->vector.exp,
        iss->vector.mant,
        7
    );
}

static inline iss_reg_t soft_hier_vfexp_vv_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    unsigned int sewb = iss->vector.sewb;
    unsigned int lmul = iss->vector.lmul;

    for (unsigned int i = VSTART; i < VEND; i++)
    {
        if (velem_is_active(iss, i, UIM_GET(0)))
        {
            uint64_t in = velem_get_value(iss, REG_IN(0), i, sewb, lmul);
            uint64_t res = soft_hier_double_to_vec_float(
                iss,
                std::exp(soft_hier_vec_float_to_double(iss, in))
            );

            velem_set_value(iss, REG_OUT(0), i, sewb, res);
        }
    }

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t soft_hier_vfredmax_vx_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    unsigned int sewb = iss->vector.sewb;
    unsigned int lmul = iss->vector.lmul;
    uint64_t lane = RVV_REG_GET(0);

    if (lane >= (uint64_t)vlmax_get(iss))
    {
        return iss_insn_next(iss, insn, pc);
    }

    uint64_t res = velem_get_value(iss, REG_IN(2), lane, sewb, lmul);

    for (unsigned int i = VSTART; i < VEND; i++)
    {
        if (velem_is_active(iss, i, UIM_GET(0)))
        {
            uint64_t in = velem_get_value(iss, REG_IN(1), i, sewb, lmul);
            res = LIB_FF_CALL2(
                lib_flexfloat_max,
                res,
                in,
                iss->vector.exp,
                iss->vector.mant
            );
        }
    }

    velem_set_value(iss, REG_OUT(0), lane, sewb, res);

    return iss_insn_next(iss, insn, pc);
}

static inline iss_reg_t soft_hier_vfredsum_vx_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    unsigned int sewb = iss->vector.sewb;
    unsigned int lmul = iss->vector.lmul;
    uint64_t lane = RVV_REG_GET(0);

    if (lane >= (uint64_t)vlmax_get(iss))
    {
        return iss_insn_next(iss, insn, pc);
    }

    uint64_t res = velem_get_value(iss, REG_IN(2), lane, sewb, lmul);

    for (unsigned int i = VSTART; i < VEND; i++)
    {
        if (velem_is_active(iss, i, UIM_GET(0)))
        {
            uint64_t in = velem_get_value(iss, REG_IN(1), i, sewb, lmul);
            res = LIB_FF_CALL3(
                lib_flexfloat_add_round,
                res,
                in,
                iss->vector.exp,
                iss->vector.mant,
                7
            );
        }
    }

    velem_set_value(iss, REG_OUT(0), lane, sewb, res);

    return iss_insn_next(iss, insn, pc);
}
