// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <map>
#include <set>
#include <string>
#include <cpu/iss_v2/include/csr.hpp>

/* RTL parameters, set by the core recipe (pulp/cv32e40p/cv32e40p.py):
 *   CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA        F registers (FPU and not ZFINX)
 *   CONFIG_GVSOC_ISS_CV32E40P_ZFINX             ZFINX
 *   CONFIG_GVSOC_ISS_CV32E40P_PULP              COREV_PULP
 *   CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS  NUM_MHPMCOUNTERS
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

/* Read-only CSR. As in the RTL decoder, a write raises illegal-instruction
 * before the access, so the destination register is not written. */
class Cv32e40pRoCsr : public CsrReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

/* fflags, frm and fcsr, legal as the RTL fs_off signal allows
 * (fp_access_illegal). The value is the fcsr field of the base class. */
class Cv32e40pFpCsr : public CsrAbtractReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

/* lpstart, lpend and lpcount. The RTL decoder raises illegal-instruction
 * on a CSR write to them, and only the cv.* instructions program them. */
class Cv32e40pHwloopCsr : public CsrAbtractReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

/* User counters (cycle, instret, hpmcounterN and their high halves). They
 * read the machine counter, and a write raises illegal-instruction. */
class Cv32e40pCounterAlias : public CsrAbtractReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

/* dcsr, dpc, dscratch0 and dscratch1. Outside debug mode the RTL decoder
 * raises illegal-instruction on any access to them (cv32e40p_decoder.sv). */
class Cv32e40pDebugCsr : public CsrAbtractReg
{
public:
    bool check_access(Iss *iss, bool write, bool read) override;
};

class Cv32e40pCsr : public Csr
{
public:
    // Implemented bits of mcountinhibit: CY, IR and one per HPM counter.
    static constexpr iss_reg_t MCOUNTINHIBIT_MASK =
        0x5 | (((1u << CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS) - 1) << 3);

    Cv32e40pCsr(Iss &iss);

    void start();
    void reset(bool active);

    /* Shadows of Csr::get_csr and Csr::access, reached as iss->csr.*. The
     * CV32E40P map drops the generic registers that the RTL does not
     * implement, and gives some others a CV32E40P type. */
    CsrAbtractReg *get_csr(iss_reg_t address);
    bool access(iss_insn_t *insn, bool is_write, iss_reg_t address, iss_reg_t &value);

    /* RTL fs_off. The FP instructions and CSRs are illegal without an FPU,
     * and with the F registers while mstatus.FS is Off. They are always
     * legal with ZFINX. */
    inline bool fp_access_illegal();

    /* Sets mstatus.FS to Dirty after an FP register write, an fflags update
     * or an FP CSR write, with the F registers only. SD is computed when
     * read. */
    void fp_state_dirty();

    /* Counts one retired instruction (Cv32e40pEvents::event_retire_account).
     * events is the OR of the hpm_events lines it fired, and count_instr is
     * the minstret line, which is low for ebreak (cv32e40p_id_stage.sv). */
    inline void hpm_commit(uint32_t events, bool count_instr);

    /* True while any implemented counter is enabled. The core then stays on
     * the full handlers, where the event lines fire (Cv32e40pExec). */
    inline bool hpm_counting();

    // True when dcsr.ebreakm is set: an ebreak outside debug mode then enters debug mode
    // (isa/debug.hpp).
    bool ebreak_m_mode_enters_debug() { return ((this->dcsr >> 15) & 1) != 0; }

    // CSRs absent from the generic register file, or with another type.
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

    // PULP custom CSRs (COREV_PULP).
    Cv32e40pRoCsr uhartid;       /* 0xCD0 */
    Cv32e40pRoCsr privlv;        /* 0xCD1 */
    Cv32e40pRoCsr zfinx_csr;     /* 0xCD2, undeclared when FPU=1 && ZFINX=0 */

    // Hardware-loop CSRs: 0xCC0..0xCC2 and 0xCC4..0xCC6.
    Cv32e40pHwloopCsr hwloop_csr[6];

    /* LPEND of each loop, set by the corev.hpp setters, because the Hwloop
     * module keeps LPEND - 4. A loop never programmed reads 0. */
    iss_reg_t hwloop_lpend[2] = {0, 0};

    /* mip in the CSR map. It reads the base register, driven by the wires,
     * and ignores writes, as the RTL does. The IrqRiscv write callback of
     * the base register would clear the fast lines. */
    CsrAbtractReg mip_view;

    /* Debug CSRs (0x7B0..0x7B3), over the base fields dcsr, depc, scratch0
     * and scratch1, which the debug entry and dret write directly. */
    Cv32e40pDebugCsr dcsr_view;      /* 0x7B0 */
    Cv32e40pDebugCsr dpc_view;       /* 0x7B1 */
    Cv32e40pDebugCsr dscratch0_view; /* 0x7B2 */
    Cv32e40pDebugCsr dscratch1_view; /* 0x7B3 */

    /* User counters: 0xC00, 0xC02, 0xC03..0xC1F and their high halves at
     * 0xC80, 0xC82, 0xC83..0xC9F. There is no time CSR. */
    Cv32e40pCounterAlias cycle_alias;
    Cv32e40pCounterAlias instret_alias;
    Cv32e40pCounterAlias hpmcounter_alias[29];
#if ISS_REG_WIDTH == 32
    Cv32e40pCounterAlias cycleh_alias;
    Cv32e40pCounterAlias instreth_alias;
    Cv32e40pCounterAlias hpmcounterh_alias[29];
#endif

    // fflags, frm, fcsr (0x001..0x003).
    Cv32e40pFpCsr fflags_csr;
    Cv32e40pFpCsr frm_csr;
    Cv32e40pFpCsr fcsr_csr;

private:
    // A CSR instruction on a removed address raises illegal-instruction.
    void remove_csr(iss_reg_t address);
    // Declares a register at an address of the generic register file.
    void replace_csr(CsrAbtractReg *reg, std::string name, iss_reg_t address);
    /* Applies the RTL write mask of a generic register with a write callback
     * that stores the value, since the generic mask may be 0. */
    void mask_writes(CsrReg &reg, iss_reg_t mask);

    std::set<iss_reg_t> removed_csrs;
    std::map<iss_reg_t, CsrAbtractReg *> replaced_csrs;

    bool fflags_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool frm_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool fcsr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool hwloop_csr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool tselect_read_zero(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool tdata_debug_gate(iss_insn_t *insn, bool is_write, iss_reg_t &value, CsrReg *reg,
        iss_reg_t mask);
    bool mstatus_write_mask(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mip_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dcsr_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dpc_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dscratch0_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dscratch1_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mcycle_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mcycleh_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool minstret_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool minstreth_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool cycle_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool cycleh_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool instret_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool instreth_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool hpm_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool hpmh_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool mcountinhibit_access(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mhpmcounter_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool mhpmevent_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index);
    bool mstatus_read_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool mtvec_write_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);

    /* The 64-bit mcycle is the register pair while mcountinhibit.CY is set,
     * and the clock plus an offset otherwise. */
    uint64_t mcycle_count();
    void mcycle_set(uint64_t count);

    int64_t mcycle_offset = 0;

    /* Flags set by a CSR write and cleared by hpm_commit at the retire of the
     * writing instruction. When minstret or minstreth is written, the write
     * wins over the increment (cv32e40p_cs_registers.sv, !write_lower &&
     * !write_upper). */
    bool minstret_written = false;

    /* When mcountinhibit is written, the writing instruction still counts
     * under the old value, mcountinhibit_q in the RTL
     * (cv32e40p_cs_registers.sv). */
    bool mcountinhibit_stale = false;
    iss_reg_t mcountinhibit_old = 0;

    // Bit i is set when mhpmcounter(h) i + 3 is written. As for minstret, the write wins.
    uint32_t mhpmcounter_written = 0;

    /* Bit i is set when mhpmevent i + 3 is written. The event lines of an
     * instruction reach the counters with its CSR write, one cycle after it
     * leaves ID (cv32e40p_id_stage.sv), so the old selector still applies to
     * it. */
    uint32_t mhpmevent_stale = 0;
    iss_reg_t mhpmevent_old[29] = {};
};

inline bool Cv32e40pCsr::fp_access_illegal()
{
    // cv32e40p_cs_registers.sv and cv32e40p_decoder.sv.
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
    /* CY is left out, because mcycle comes from the clock. Only minstret and
     * the HPM counters need the full handlers. */
    constexpr iss_reg_t event_bits = MCOUNTINHIBIT_MASK & ~(iss_reg_t)0x1;
    return (this->mcountinhibit.value & event_bits) != event_bits;
}

inline void Cv32e40pCsr::hpm_commit(uint32_t events, bool count_instr)
{
    // An instruction writing mcountinhibit counts under the old value.
    iss_reg_t inhibit = this->mcountinhibit_stale ? this->mcountinhibit_old
                                                  : this->mcountinhibit.value;
    this->mcountinhibit_stale = false;
    // minstret counts unless inhibited (IR), on ebreak, or written by this instruction.
    bool wrote_counter = this->minstret_written;
    this->minstret_written = false;
    if (count_instr && !wrote_counter && !(inhibit & 0x4))
    {
        if (++this->minstret.value == 0)
        {
#if ISS_REG_WIDTH == 32
            this->minstreth.value++;
#endif
        }
    }
    /* mhpmcounterN adds one per retire when its mhpmeventN selects a line
     * that fired and its mcountinhibit bit is clear, unless the instruction
     * wrote it. */
    uint32_t counter_written = this->mhpmcounter_written;
    uint32_t event_stale = this->mhpmevent_stale;
    this->mhpmcounter_written = 0;
    this->mhpmevent_stale = 0;
    for (int i = 0; i < CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS; i++)
    {
        iss_reg_t selected = ((event_stale >> i) & 1) ? this->mhpmevent_old[i]
                                                       : this->mhpmevent[i].value;
        if ((selected & events) && !((counter_written >> i) & 1)
            && !(inhibit & (1u << (3 + i))))
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
