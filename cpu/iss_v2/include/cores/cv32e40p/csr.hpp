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
static_assert(CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS <= 29,
    "at most 29 HPM counters (mhpmcounter3..31)");

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

/* User-mode counter alias (cycle/instret/hpmcounterN and the H views):
 * reads mirror the machine counter through a registered callback, writes
 * raise illegal-instruction (0xCxx is the architecturally read-only CSR
 * range, and the RTL has no write path for it). */
class Cv32e40pCounterAlias : public CsrAbtractReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

class Cv32e40pCsr : public Csr
{
public:
    /* mcountinhibit implemented bits: CY, IR and one per HPM counter. */
    static constexpr iss_reg_t MCOUNTINHIBIT_MASK =
        0x5 | (((1u << CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS) - 1) << 3);

    Cv32e40pCsr(Iss &iss);

    void start();
    void reset(bool active);

    /* FP CSR access legality: illegal while mstatus.FS == Off (00). */
    inline bool fp_access_illegal();

    /* Promote mstatus.FS to Dirty (11) on FP state change. The RTL forces it
     * on FP regfile writes, fflags updates and FP-CSR writes when the FPU is
     * in the ISA (FPU=1, ZFINX=0). SD (bit 31) is derived at read time.
     * Out-of-line: the trapped-instruction guard needs the full Iss type. */
    void fp_state_dirty();

    /* Advance the counters for one retired instruction: events is the OR of
     * the RTL hpm_events lines it fired (see cores/cv32e40p/events.hpp).
     * Called once per retire by Cv32e40pEvents::event_retire_account. */
    inline void hpm_commit(uint32_t events);

    /* True while any implemented counter is enabled: keeps the core on the
     * full handlers, where the event lines fire (Cv32e40pExec). */
    inline bool hpm_counting();

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

    /* Architectural LPEND per loop, written by the corev.hpp setters. The
     * Hwloop module stores the loop-back point (LPEND - 4), so it cannot
     * serve the CSR read: a never-programmed loop must read back 0. */
    iss_reg_t hwloop_lpend[2] = {0, 0};

    /* mip front-end (0x344): reads mirror the wire-driven base register,
     * CSR writes are silently dropped — the RTL has no mip write path
     * (cv32e40p_cs_registers.sv reads it from the interrupt lines only).
     * Replaces the base mip in the CSR map, so the generic IrqRiscv write
     * callback (wdata & 0xAAA, which also clears the fast-line bits) can
     * never corrupt the pending state. */
    CsrAbtractReg mip_view;

    /* Debug-mode CSRs (0x7B0-0x7B3): views over the base raw fields
     * (dcsr/depc/scratch0/scratch1), which the debug-entry and dret paths
     * write directly. The base register file leaves these addresses
     * undeclared, so without the views every debug-ROM csrrw raises
     * illegal-instruction. The RTL has no debug-mode access gate
     * (cv32e40p_cs_registers.sv decodes them at any time). */
    CsrAbtractReg dcsr_view;      /* 0x7B0 */
    CsrAbtractReg dpc_view;       /* 0x7B1 */
    CsrAbtractReg dscratch0_view; /* 0x7B2 */
    CsrAbtractReg dscratch1_view; /* 0x7B3 */

    /* User counter aliases: 0xC00/0xC02/0xC03..0xC1F and the H views at
     * 0xC80/0xC82/0xC83..0xC9F. time (0xC01) is absent. */
    Cv32e40pCounterAlias cycle_alias;
    Cv32e40pCounterAlias instret_alias;
    Cv32e40pCounterAlias hpmcounter_alias[29];
#if ISS_REG_WIDTH == 32
    Cv32e40pCounterAlias cycleh_alias;
    Cv32e40pCounterAlias instreth_alias;
    Cv32e40pCounterAlias hpmcounterh_alias[29];
#endif

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
    bool mip_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dcsr_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dpc_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dscratch0_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dscratch1_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mcycle_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mcycleh_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool cycle_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool cycleh_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool instret_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool instreth_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool hpm_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool hpmh_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool mcountinhibit_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mstatus_read_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mtvec_write_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);

    /* Current 64-bit mcycle count: the frozen register pair while
     * mcountinhibit.CY is set, the offset clock otherwise. */
    uint64_t mcycle_count();
    void mcycle_set(uint64_t count);

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

inline bool Cv32e40pCsr::hpm_counting()
{
    /* CY excluded: mcycle is clock-derived and needs no full-handler
     * support, only minstret and the event counters do. */
    constexpr iss_reg_t event_bits = MCOUNTINHIBIT_MASK & ~(iss_reg_t)0x1;
    return (this->mcountinhibit.value & event_bits) != event_bits;
}

inline void Cv32e40pCsr::hpm_commit(uint32_t events)
{
    /* minstret: retired instructions, gated on mcountinhibit.IR (bit 2). */
    if (!(this->mcountinhibit.value & 0x4))
    {
        if (++this->minstret.value == 0)
        {
#if ISS_REG_WIDTH == 32
            this->minstreth.value++;
#endif
        }
    }
    /* mhpmcounterN advances at most +1 per retire when the mhpmeventN mask
     * intersects the fired lines and its mcountinhibit bit is clear. */
    for (int i = 0; i < CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS; i++)
    {
        if ((this->mhpmevent[i].value & events)
            && !(this->mcountinhibit.value & (1u << (3 + i))))
        {
            if (++this->mhpmcounter[i].value == 0)
            {
#if ISS_REG_WIDTH == 32
                this->mhpmcounterh[i].value++;
#endif
            }
        }
    }
}
