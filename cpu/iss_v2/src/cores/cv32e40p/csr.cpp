// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* CV32E40P CSR personality for iss_v2.
 *
 * Same CSR map and write-legality rules as the v1 model
 * (cpu/iss/src/cv32e40p/csr_cv32e40p.cpp), expressed as a Csr subclass:
 * base registers are tightened through their write masks, core-only
 * registers are declared here, and registers the core does not implement
 * are undeclared so access falls through to the unsupported-CSR path,
 * which this core configures to raise illegal-instruction. */

#include <cpu/iss_v2/include/cores/cv32e40p/csr.hpp>

bool Cv32e40pRoCsr::check_access(Iss *iss, bool write, bool read)
{
    if (write)
    {
        iss->exception.raise(iss->exec.current_insn, ISS_EXCEPT_ILLEGAL);
        return false;
    }
    return true;
}

bool Cv32e40pFpCsr::check_access(Iss *iss, bool write, bool read)
{
    if (iss->csr.fp_access_illegal())
    {
        iss->exception.raise(iss->exec.current_insn, ISS_EXCEPT_ILLEGAL);
        return false;
    }
    return true;
}

bool Cv32e40pHwloopCsr::check_access(Iss *iss, bool write, bool read)
{
    if (write)
    {
        iss->exception.raise(iss->exec.current_insn, ISS_EXCEPT_ILLEGAL);
        return false;
    }
    return true;
}

Cv32e40pCsr::Cv32e40pCsr(Iss &iss)
: Csr(iss)
{
    /* M-mode-only core: drop the registers the RTL does not implement so
     * access raises illegal-instruction through the unsupported-CSR path.
     * Compared with the v1 model this also drops satp and the user
     * counters (cycle/time/instret): no U-mode, no mcounteren. */
    const iss_reg_t nonexistent[] = {
        0x100, 0x104, 0x105, 0x106,         /* sstatus, sie, stvec, scounteren */
        0x140, 0x141, 0x142, 0x143, 0x144,  /* sscratch, sepc, scause, stval, sip */
        0x180,                              /* satp */
        0x302, 0x303,                       /* medeleg, mideleg */
        0x306,                              /* mcounteren */
        0x740, 0x741, 0x742, 0x744,         /* mnscratch, mnepc, mncause, mnstatus */
        0x008, 0x009, 0x00A, 0x00F,         /* vstart, vxstat, vxrm, vcsr */
        0xC20, 0xC21, 0xC22,                /* vl, vtype, vlenb */
        0xC00, 0xC01, 0xC02,                /* cycle, time, instret */
    };
    for (iss_reg_t addr : nonexistent)
    {
        this->undeclare_csr(addr);
    }

    this->raise_on_unsupported_csr = true;

    /* Machine information registers: read-only, writes raise illegal.
     * mvendorid/marchid are re-declared with the read-only register type
     * (the base class versions accept writes). */
    this->undeclare_csr(0xF11);
    this->undeclare_csr(0xF12);
    this->declare_csr(&this->mvendorid_ro, "mvendorid", 0xF11, 0x00000602);
    this->declare_csr(&this->marchid_ro,   "marchid",   0xF12, 0x00000004);
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA || CONFIG_GVSOC_ISS_CV32E40P_ZFINX || CONFIG_GVSOC_ISS_CV32E40P_PULP
    this->declare_csr(&this->mimpid, "mimpid", 0xF13, 1);
#else
    this->declare_csr(&this->mimpid, "mimpid", 0xF13, 0);
#endif
    this->declare_csr(&this->mhartid_csr, "mhartid", 0xF14, this->mhartid);

    /* Counter CSRs. */
    this->declare_csr(&this->minstret, "minstret", 0xB02);
#if ISS_REG_WIDTH == 32
    this->declare_csr(&this->mcycleh,   "mcycleh",   0xB80);
    this->declare_csr(&this->minstreth, "minstreth", 0xB82);
#endif

    /* mcycle/mcycleh: one 64-bit count derived from the clock with a write
     * offset, frozen into the register pair while mcountinhibit.CY is set.
     * Registered after the base callback so these have the last word. */
    this->mcycle.register_callback(std::bind(&Cv32e40pCsr::mcycle_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#if ISS_REG_WIDTH == 32
    this->mcycleh.register_callback(std::bind(&Cv32e40pCsr::mcycleh_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#endif

    /* mcountinhibit: reset with all implemented bits set (RTL behaviour),
     * i.e. counters disabled out of reset. The callback freezes/unfreezes
     * the mcycle count on CY toggles. */
    const int num_hpm = CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS;
    this->mcountinhibit.set_write_mask(MCOUNTINHIBIT_MASK);
    this->mcountinhibit.reset_val = MCOUNTINHIBIT_MASK;
    this->mcountinhibit.register_callback(std::bind(&Cv32e40pCsr::mcountinhibit_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    /* HPM counters and event selectors: only the first num_mhpmcounters
     * are implemented, the rest are WARL zero (writes ignored). Only
     * mhpmevent bits [15:0] exist (16 HPM event lines). */
    for (int i = 0; i < 29; i++)
    {
        iss_reg_t counter_mask = (i < num_hpm) ? (iss_reg_t)-1 : 0;
        this->mhpmcounter[i].set_write_mask(counter_mask);
#if ISS_REG_WIDTH == 32
        this->mhpmcounterh[i].set_write_mask(counter_mask);
#endif
        this->declare_csr(&this->mhpmevent[i], "mhpmevent" + std::to_string(i + 3),
            0x323 + i, 0, (i < num_hpm) ? 0xFFFF : 0);
    }

    /* Interrupt and trap CSRs: masks from the RTL (cv32e40p_cs_registers.sv).
     * mstatus: only MIE/MPIE (and FS with an FPU) are writable; the mask is
     * applied by Core::mstatus_update via CONFIG_GVSOC_ISS_CORE_MSTATUS_WRITE_MASK
     * set by the recipe. Reset is MPP=M, FS=Off for every configuration. */
    this->mstatus.reset_val = 0x00001800;
    this->mie.set_write_mask(0xFFFF0888);
    this->mip.set_write_mask(0);
    this->mtvec.set_write_mask(0xFFFFFF01);
    this->mtvec.reset_val = 0x1;
    this->mtval.set_write_mask(0);
    this->mcause.set_write_mask(0x8000001F);

    /* Trigger module: one trigger, tselect hardwired to 0, tdata* writable
     * only from debug mode (not modelled), tinfo reports type 2. */
    this->tselect.set_write_mask(0);
    this->tselect.register_callback(std::bind(&Cv32e40pCsr::tselect_read_zero, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->tdata1.reset_val = 0x28001040;
    this->tdata1.set_write_mask(0);
    this->tdata2.set_write_mask(0);
    this->tdata3.set_write_mask(0);
    this->declare_csr(&this->tinfo,    "tinfo",    0x7A4, 0x4, 0);
    this->declare_csr(&this->mcontext, "mcontext", 0x7A8, 0, 0);
    this->declare_csr(&this->scontext, "scontext", 0x7AA, 0, 0);

#if CONFIG_GVSOC_ISS_CV32E40P_PULP
    /* PULP custom CSRs. The RTL decoder raises illegal-instruction on any
     * write to them (read-only register type). */
    this->declare_csr(&this->uhartid, "uhartid", 0xCD0, this->mhartid);
    this->declare_csr(&this->privlv,  "privlv",  0xCD1, 3);
#if !CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    /* zfinx indicator: reads 1 on ZFINX, 0 without an FPU. With FPU=1 &&
     * ZFINX=0 the RTL rejects even reads, so it stays undeclared and the
     * unsupported-CSR path raises illegal-instruction. */
    this->declare_csr(&this->zfinx_csr, "zfinx", 0xCD2,
        CONFIG_GVSOC_ISS_CV32E40P_ZFINX ? 1 : 0);
#endif

    /* Hardware-loop CSRs, readable via CSR instructions only (writes go
     * through the cv.* instructions and csrrw raises illegal). The values
     * live in the Hwloop module. */
    for (int loop = 0; loop < 2; loop++)
    {
        static const char *names[] = { "lpstart", "lpend", "lpcount" };
        for (int kind = 0; kind < 3; kind++)
        {
            int index = loop * 3 + kind;
            this->declare_csr(&this->hwloop_csr[index],
                names[kind] + std::to_string(loop), 0xCC0 + loop * 4 + kind);
            this->hwloop_csr[index].register_callback(
                std::bind(&Cv32e40pCsr::hwloop_csr_access, this,
                    std::placeholders::_1, std::placeholders::_2,
                    std::placeholders::_3, index));
        }
    }
#endif

    /* fflags / frm / fcsr front-ends: legality gated on mstatus.FS by the
     * register type, value mapping onto the shared fcsr field here. */
    this->declare_csr(&this->fflags_csr, "fflags", 0x001);
    this->fflags_csr.register_callback(std::bind(&Cv32e40pCsr::fflags_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->declare_csr(&this->frm_csr, "frm", 0x002);
    this->frm_csr.register_callback(std::bind(&Cv32e40pCsr::frm_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->declare_csr(&this->fcsr_csr, "fcsr", 0x003);
    this->fcsr_csr.register_callback(std::bind(&Cv32e40pCsr::fcsr_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void Cv32e40pCsr::start()
{
    /* Registered here so it runs after Core::mstatus_update, which is
     * registered by the Core constructor (after this class is built) and
     * overwrites the read value with the stored one. */
    this->mstatus.register_callback(std::bind(&Cv32e40pCsr::mstatus_read_fixup, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void Cv32e40pCsr::reset(bool active)
{
    Csr::reset(active);

    if (active)
    {
        /* dcsr: xdebugver=4 in [31:28], prv=M in [1:0] (base reset leaves
         * prv=0). */
        this->dcsr = (4 << 28) | 0x3;

        this->mcycle_offset = 0;
    }
}

bool Cv32e40pCsr::mstatus_read_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    /* SD (bit 31) is derived on read: set when FS or XS is Dirty. */
    if (!is_write)
    {
        if (((value >> 13) & 3) == 3 || ((value >> 15) & 3) == 3)
        {
            value |= 1ULL << 31;
        }
    }
#endif
    return false;
}

bool Cv32e40pCsr::tselect_read_zero(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    /* One trigger: tselect always reads 0 (the base callback reads -1). */
    if (!is_write)
    {
        value = 0;
    }
    return false;
}

uint64_t Cv32e40pCsr::mcycle_count()
{
#if ISS_REG_WIDTH == 32
    uint64_t frozen = ((uint64_t)this->mcycleh.value << 32) | this->mcycle.value;
#else
    uint64_t frozen = this->mcycle.value;
#endif
    if (this->mcountinhibit.value & 0x1)
    {
        return frozen;
    }
    return (uint64_t)((int64_t)this->iss.clock.get_cycles() + this->mcycle_offset);
}

void Cv32e40pCsr::mcycle_set(uint64_t count)
{
    this->mcycle.value = (iss_reg_t)count;
#if ISS_REG_WIDTH == 32
    this->mcycleh.value = (iss_reg_t)(count >> 32);
#endif
    /* While frozen (CY set) this offset is dead: mcountinhibit_access
     * recomputes it from the register pair at unfreeze time. */
    this->mcycle_offset = (int64_t)count - (int64_t)this->iss.clock.get_cycles();
}

bool Cv32e40pCsr::mcycle_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->mcycle_set((this->mcycle_count() & ~(uint64_t)0xFFFFFFFF) | value);
    }
    else
    {
        value = (iss_reg_t)this->mcycle_count();
    }
    return false;
}

bool Cv32e40pCsr::mcycleh_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->mcycle_set(((uint64_t)value << 32) | (uint32_t)this->mcycle_count());
    }
    else
    {
        value = (iss_reg_t)(this->mcycle_count() >> 32);
    }
    return false;
}

bool Cv32e40pCsr::mcountinhibit_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    /* Freeze the count into the register pair when CY sets, re-anchor the
     * clock offset to the frozen value when CY clears. Runs before the
     * masked store, so this->mcountinhibit.value still holds the old CY. */
    if (is_write)
    {
        /* Event lines fire only from the full handlers (same scheme as the
         * Ri5ky PCMR write): switch when software touches the inhibit CSR. */
        this->iss.exec.switch_to_full_mode();
        bool old_cy = this->mcountinhibit.value & 0x1;
        bool new_cy = value & 0x1;
        if (old_cy != new_cy)
        {
            uint64_t count = this->mcycle_count();
            if (new_cy)
            {
                this->mcycle.value = (iss_reg_t)count;
#if ISS_REG_WIDTH == 32
                this->mcycleh.value = (iss_reg_t)(count >> 32);
#endif
            }
            else
            {
                this->mcycle_offset = (int64_t)count - (int64_t)this->iss.clock.get_cycles();
            }
        }
    }
    return true;
}

bool Cv32e40pCsr::hwloop_csr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index)
{
    int loop = index / 3;

    switch (index % 3)
    {
        case 0: value = this->iss.hwloop.get_start(loop); break;
        /* The Hwloop module stores the loop-back point, LPEND - 4 (see the
         * corev.hpp setters); the architectural LPEND is re-derived here. */
        case 1: value = this->iss.hwloop.get_end(loop) + 4; break;
        case 2: value = this->iss.hwloop.get_count(loop); break;
    }
    return false;
}

bool Cv32e40pCsr::fflags_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->fcsr.fflags = value;
        /* RTL: an fflags write (fflags_we_i) forces mstatus.FS=Dirty. */
        this->fp_state_dirty();
    }
    else
    {
        value = this->fcsr.fflags;
    }
    return false;
}

bool Cv32e40pCsr::frm_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->fcsr.frm = value;
        this->fp_state_dirty();
    }
    else
    {
        value = this->fcsr.frm;
    }
    return false;
}

bool Cv32e40pCsr::fcsr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->fcsr.raw = value & 0xff;
        this->fp_state_dirty();
    }
    else
    {
        value = this->fcsr.raw;
    }
    return false;
}
