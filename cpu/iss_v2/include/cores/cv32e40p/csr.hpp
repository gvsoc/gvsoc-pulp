// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/csr.hpp>

/* Static personality configuration, set by the Python recipe
 * (pulp/cpu/iss/cv32e40p_v2.py):
 *   CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA  F extension present (0 for ZFINX)
 *   CONFIG_GVSOC_ISS_CV32E40P_ZFINX       ZFINX variant
 *   CONFIG_GVSOC_ISS_CV32E40P_PULP        COREV_PULP (XPULP) configuration
 *   CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS  implemented HPM counters
 */
#ifndef CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
#define CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA 0
#endif
#ifndef CONFIG_GVSOC_ISS_CV32E40P_ZFINX
#define CONFIG_GVSOC_ISS_CV32E40P_ZFINX 0
#endif
#ifndef CONFIG_GVSOC_ISS_CV32E40P_PULP
#define CONFIG_GVSOC_ISS_CV32E40P_PULP 1
#endif
#ifndef CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS
#define CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS 1
#endif

/* CSR that is read-only from CSR instructions: any write attempt raises an
 * illegal-instruction exception before the access happens, so the destination
 * register is not written (matches the RTL decoder behaviour). */
class Cv32e40pRoCsr : public CsrReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

/* fflags / frm / fcsr front-end. Access legality follows the RTL fs_off
 * signal: rejected with an illegal-instruction exception while mstatus.FS
 * is Off (FPU in the ISA), always rejected without an FPU, always granted
 * for ZFINX — see fp_access_illegal(). The register content lives in the
 * base class fcsr field; the value mapping is done by the registered
 * callback. */
class Cv32e40pFpCsr : public CsrAbtractReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

/* Hardware-loop CSR front-end (lpstart/lpend/lpcount). Readable via CSR
 * instructions, but the RTL decoder raises illegal-instruction on any CSR
 * write to them (they are only programmed through the cv.* instructions). */
class Cv32e40pHwloopCsr : public CsrAbtractReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

class Cv32e40pCsr : public Csr
{
public:
    Cv32e40pCsr(Iss &iss);

    void start();
    void reset(bool active);

    /* FP CSR access legality: illegal while mstatus.FS == Off (00). */
    inline bool fp_access_illegal();

    /* Promote mstatus.FS to Dirty (11) on FP state change. The RTL forces it
     * on FP regfile writes, fflags updates and FP-CSR writes when the FPU is
     * in the ISA (FPU=1, ZFINX=0). SD (bit 31) is derived at read time. */
    inline void fp_state_dirty();

    /* CV32E40P-only CSRs, absent from the generic register file. */
    Cv32e40pRoCsr mvendorid_ro;  /* 0xF11 (replaces the base read/write reg) */
    Cv32e40pRoCsr marchid_ro;    /* 0xF12 (replaces the base read/write reg) */
    Cv32e40pRoCsr mimpid;        /* 0xF13 */
    Cv32e40pRoCsr mhartid_csr;   /* 0xF14 */
    CsrReg tinfo;                /* 0x7A4, read-only through a zero mask */
    CsrReg mcontext;             /* 0x7A8, writable only from debug mode */
    CsrReg scontext;             /* 0x7AA, writable only from debug mode */
    CsrReg minstret;             /* 0xB02 */
#if ISS_REG_WIDTH == 32
    CsrReg mcycleh;              /* 0xB80 */
    CsrReg minstreth;            /* 0xB82 */
#endif
    CsrReg mhpmevent[29];        /* 0x323..0x33F */

    /* PULP custom CSRs (COREV_PULP configurations). */
    Cv32e40pRoCsr uhartid;       /* 0xCD0 */
    Cv32e40pRoCsr privlv;        /* 0xCD1 */
    Cv32e40pRoCsr zfinx_csr;     /* 0xCD2, undeclared when FPU=1 && ZFINX=0 */

    /* Hardware-loop CSRs: 0xCC0..0xCC2 / 0xCC4..0xCC6 (gap at 0xCC3). */
    Cv32e40pHwloopCsr hwloop_csr[6];

    /* fflags / frm / fcsr (0x001..0x003). */
    Cv32e40pFpCsr fflags_csr;
    Cv32e40pFpCsr frm_csr;
    Cv32e40pFpCsr fcsr_csr;

private:
    bool fflags_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool frm_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool fcsr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool hwloop_csr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool tselect_read_zero(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mcycle_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mstatus_read_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);

    int64_t mcycle_offset = 0;
};

inline bool Cv32e40pCsr::fp_access_illegal()
{
    /* RTL (cv32e40p_cs_registers.sv:1110 + decoder): illegal when there is
     * no FPU; gated on mstatus.FS only with the FPU registers in the ISA;
     * always legal for ZFINX (no FS state, flags/rm still implemented). */
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    return this->mstatus.fs == 0;
#elif CONFIG_GVSOC_ISS_CV32E40P_ZFINX
    return false;
#else
    return true;
#endif
}

inline void Cv32e40pCsr::fp_state_dirty()
{
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    this->mstatus.fs = 3;
#endif
}
