// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* CSR map of the CV32E40P (cv32e40p_cs_registers.sv). The generic registers
 * get the RTL write masks, the CV32E40P registers are declared here, and a
 * CSR instruction on a register the RTL does not implement raises
 * illegal-instruction. */

#include <cpu/iss_v2/include/cores/cv32e40p/csr.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/irq.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

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

bool Cv32e40pCounterAlias::check_access(Iss *iss, bool write, bool read)
{
    if (write)
    {
        iss->exception.raise(iss->exec.current_insn, ISS_EXCEPT_ILLEGAL);
        return false;
    }
    return true;
}

bool Cv32e40pDebugCsr::check_access(Iss *iss, bool write, bool read)
{
    if (!iss->exec.debug_mode)
    {
        iss->exception.raise(iss->exec.current_insn, ISS_EXCEPT_ILLEGAL);
        return false;
    }
    return true;
}

void Cv32e40pCsr::remove_csr(iss_reg_t address)
{
    this->removed_csrs.insert(address);
}

void Cv32e40pCsr::replace_csr(CsrAbtractReg *reg, std::string name, iss_reg_t address)
{
    reg->name = strdup(name.c_str());
    this->replaced_csrs[address] = reg;
}

void Cv32e40pCsr::mask_writes(CsrReg &reg, iss_reg_t mask)
{
    reg.register_callback([&reg, mask](iss_insn_t *insn, bool is_write, iss_reg_t &value) {
        if (!is_write)
        {
            return true;
        }
        reg.value = (reg.value & ~mask) | (value & mask);
        return false;
    });
}

CsrAbtractReg *Cv32e40pCsr::get_csr(iss_reg_t address)
{
    auto replaced = this->replaced_csrs.find(address);
    if (replaced != this->replaced_csrs.end())
    {
        return replaced->second;
    }
    if (this->removed_csrs.count(address))
    {
        return NULL;
    }
    return this->Csr::get_csr(address);
}

bool Cv32e40pCsr::access(iss_insn_t *insn, bool is_write, iss_reg_t address, iss_reg_t &value)
{
    CsrAbtractReg *csr = this->get_csr(address);
    if (csr != NULL)
    {
        bool result = csr->access(insn, is_write, value);
        if (insn != NULL && !this->iss.exec.has_exception && this->iss.exec.cosim->enabled())
        {
            if (is_write)
            {
                this->iss.exec.cosim->csr(address, CV32E40P_COSIM_CSR_INSN);
            }
            else
            {
                this->iss.exec.cosim->csr_read(insn, address, value);
            }
        }
        return result;
    }

    // A CSR instruction does not get here, because priv.hpp raises illegal-instruction first.
    if (!is_write)
    {
        value = 0;
    }
    return false;
}

Cv32e40pCsr::Cv32e40pCsr(Iss &iss)
: Csr(iss)
{
    // Generic registers the RTL does not implement. The core has M-mode only, without mcounteren.
    const iss_reg_t nonexistent[] = {
        0x100, 0x104, 0x105, 0x106,         /* sstatus, sie, stvec, scounteren */
        0x140, 0x141, 0x142, 0x143, 0x144,  /* sscratch, sepc, scause, stval, sip */
        0x180,                              /* satp */
        0x302, 0x303,                       /* medeleg, mideleg */
        0x306,                              /* mcounteren */
        0x740, 0x741, 0x742, 0x744,         /* mnscratch, mnepc, mncause, mnstatus */
        0x008, 0x009, 0x00A, 0x00F,         /* vstart, vxstat, vxrm, vcsr */
        0xC20, 0xC21, 0xC22,                /* vl, vtype, vlenb */
        0xC01,                              /* time */
    };
    for (iss_reg_t addr : nonexistent)
    {
        this->remove_csr(addr);
    }

    /* The RTL has no PMP, but the generic register file declares the PMP
     * CSRs even with the empty PMP module. */
    for (iss_reg_t addr = 0x3A0; addr < 0x3B0; addr++) /* pmpcfg0..15 */
    {
        this->remove_csr(addr);
    }
    for (iss_reg_t addr = 0x3B0; addr < 0x3F0; addr++) /* pmpaddr0..63 */
    {
        this->remove_csr(addr);
    }

    /* Machine information registers, read-only. mvendorid and marchid
     * replace the generic registers, which accept writes. */
    this->replace_csr(&this->mvendorid_ro, "mvendorid", 0xF11);
    this->replace_csr(&this->marchid_ro,   "marchid",   0xF12);
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA || CONFIG_GVSOC_ISS_CV32E40P_ZFINX || \
    CONFIG_GVSOC_ISS_CV32E40P_PULP
    this->declare_csr(&this->mimpid, "mimpid", 0xF13, 1);
#else
    this->declare_csr(&this->mimpid, "mimpid", 0xF13, 0);
#endif
    this->declare_csr(&this->mhartid_csr, "mhartid", 0xF14, this->mhartid);

    // Counter CSRs.
    this->declare_csr(&this->minstret, "minstret", 0xB02);
#if ISS_REG_WIDTH == 32
    this->declare_csr(&this->mcycleh,   "mcycleh",   0xB80);
    this->declare_csr(&this->minstreth, "minstreth", 0xB82);
#endif

    /* The callbacks only flag the write for hpm_commit, because the write
     * wins over the increment of the writing instruction. */
    this->minstret.register_callback(std::bind(&Cv32e40pCsr::minstret_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#if ISS_REG_WIDTH == 32
    this->minstreth.register_callback(std::bind(&Cv32e40pCsr::minstreth_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#endif

    /* mcycle and mcycleh are the clock plus an offset, frozen into the
     * register pair while mcountinhibit.CY is set. These callbacks are
     * registered after the base one, so they run last. */
    this->mcycle.register_callback(std::bind(&Cv32e40pCsr::mcycle_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#if ISS_REG_WIDTH == 32
    this->mcycleh.register_callback(std::bind(&Cv32e40pCsr::mcycleh_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#endif

    /* mcountinhibit resets with every implemented bit set, as in the RTL.
     * The callback freezes and restarts mcycle when CY changes. */
    const int num_hpm = CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS;
    this->mcountinhibit.reset_val = MCOUNTINHIBIT_MASK;
    this->mcountinhibit.register_callback(std::bind(&Cv32e40pCsr::mcountinhibit_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->mask_writes(this->mcountinhibit, MCOUNTINHIBIT_MASK);

    /* Only the first num_mhpmcounters counters and selectors are
     * implemented, and the others read 0. mhpmevent has 16 bits, one per
     * event line. */
    for (int i = 0; i < 29; i++)
    {
        iss_reg_t counter_mask = (i < num_hpm) ? (iss_reg_t)-1 : 0;
        this->mask_writes(this->mhpmcounter[i], counter_mask);
#if ISS_REG_WIDTH == 32
        this->mask_writes(this->mhpmcounterh[i], counter_mask);
#endif
        this->declare_csr(&this->mhpmevent[i], "mhpmevent" + std::to_string(i + 3),
            0x323 + i, 0, (i < num_hpm) ? 0xFFFF : 0);
        if (i < num_hpm)
        {
            this->mhpmcounter[i].register_callback(std::bind(&Cv32e40pCsr::mhpmcounter_access,
                this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, i));
#if ISS_REG_WIDTH == 32
            this->mhpmcounterh[i].register_callback(std::bind(&Cv32e40pCsr::mhpmcounter_access,
                this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, i));
#endif
            this->mhpmevent[i].register_callback(std::bind(&Cv32e40pCsr::mhpmevent_access,
                this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, i));
        }
    }

    /* The user counters at 0xC00..0xC1F and 0xC80..0xC9F read the machine
     * counters (cv32e40p_cs_registers.sv). */
    this->replace_csr(&this->cycle_alias, "cycle", 0xC00);
    this->cycle_alias.register_callback(std::bind(&Cv32e40pCsr::cycle_alias_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->replace_csr(&this->instret_alias, "instret", 0xC02);
    this->instret_alias.register_callback(std::bind(&Cv32e40pCsr::instret_alias_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#if ISS_REG_WIDTH == 32
    this->declare_csr(&this->cycleh_alias, "cycleh", 0xC80);
    this->cycleh_alias.register_callback(std::bind(&Cv32e40pCsr::cycleh_alias_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->declare_csr(&this->instreth_alias, "instreth", 0xC82);
    this->instreth_alias.register_callback(std::bind(&Cv32e40pCsr::instreth_alias_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#endif
    for (int i = 0; i < 29; i++)
    {
        this->declare_csr(&this->hpmcounter_alias[i], "hpmcounter" + std::to_string(i + 3),
            0xC03 + i);
        this->hpmcounter_alias[i].register_callback(std::bind(&Cv32e40pCsr::hpm_alias_access, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, i));
#if ISS_REG_WIDTH == 32
        this->declare_csr(&this->hpmcounterh_alias[i], "hpmcounter" + std::to_string(i + 3) + "h",
            0xC83 + i);
        this->hpmcounterh_alias[i].register_callback(std::bind(&Cv32e40pCsr::hpmh_alias_access,
            this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, i));
#endif
    }

    /* Interrupt and trap CSRs, with the RTL masks. The mstatus mask applies
     * before Core::mstatus_update, which the Core constructor registers
     * later. The Cv32e40pCore constructor sets the reset value. */
    this->mstatus.register_callback(std::bind(&Cv32e40pCsr::mstatus_write_mask, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    /* mie is masked by Cv32e40pIrq::mie_write_fixup, after IrqRiscv::mie_access
     * stores the value. mip_view replaces mip in the map. */
    this->replace_csr(&this->mip_view, "mip", 0x344);
    this->mip_view.register_callback(std::bind(&Cv32e40pCsr::mip_view_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    // mtvec is masked by mtvec_write_fixup, registered in start().
    this->mtvec.reset_val = 0x1;
    this->mask_writes(this->mtval, 0);
    this->mask_writes(this->mcause, 0x8000001F);

    /* There is one trigger, of type 2 (tinfo), so tselect reads 0. Only
     * debug mode writes tdata1 (bit 2, execute match) and tdata2 (address),
     * and a write from M-mode is ignored (tmatch_control_we, tmatch_value_we).
     * The match is checked at dispatch (Cv32e40pIrq::dispatch_check). */
    this->tselect.register_callback(std::bind(&Cv32e40pCsr::tselect_read_zero, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->tdata1.reset_val = 0x28001040;
    this->tdata1.register_callback(std::bind(&Cv32e40pCsr::tdata_debug_gate, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, &this->tdata1,
        (iss_reg_t)0x4));
    this->tdata2.register_callback(std::bind(&Cv32e40pCsr::tdata_debug_gate, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, &this->tdata2,
        (iss_reg_t)0xFFFFFFFF));
    this->mask_writes(this->tdata3, 0);
    this->declare_csr(&this->tinfo,    "tinfo",    0x7A4, 0x4, 0);
    this->declare_csr(&this->mcontext, "mcontext", 0x7A8, 0, 0);
    this->declare_csr(&this->scontext, "scontext", 0x7AA, 0, 0);

    // Debug CSRs, over the base fields that the debug entry and dret write.
    this->declare_csr(&this->dcsr_view,      "dcsr",      0x7B0);
    this->dcsr_view.register_callback(std::bind(&Cv32e40pCsr::dcsr_view_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->declare_csr(&this->dpc_view,       "dpc",       0x7B1);
    this->dpc_view.register_callback(std::bind(&Cv32e40pCsr::dpc_view_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->declare_csr(&this->dscratch0_view, "dscratch0", 0x7B2);
    this->dscratch0_view.register_callback(std::bind(&Cv32e40pCsr::dscratch0_view_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->declare_csr(&this->dscratch1_view, "dscratch1", 0x7B3);
    this->dscratch1_view.register_callback(std::bind(&Cv32e40pCsr::dscratch1_view_access, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#if CONFIG_GVSOC_ISS_CV32E40P_PULP
    // PULP custom CSRs, read-only.
    this->declare_csr(&this->uhartid, "uhartid", 0xCD0, this->mhartid);
    this->declare_csr(&this->privlv,  "privlv",  0xCD1, 3);
#if !CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    /* zfinx reads 1 with ZFINX and 0 without an FPU. With FPU=1 and
     * ZFINX=0 the RTL does not implement it. */
    this->declare_csr(&this->zfinx_csr, "zfinx", 0xCD2,
        CONFIG_GVSOC_ISS_CV32E40P_ZFINX ? 1 : 0);
#endif

    // Hardware-loop CSRs, read-only. The values are kept by the Hwloop module.
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

    // fflags, frm and fcsr, over the fcsr field.
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

#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA || CONFIG_GVSOC_ISS_CV32E40P_ZFINX
/* Stubs that start() installs on the FP instructions, called before their
 * handler (insn->stub_handler, iss_v2 decode.cpp). An FP instruction is
 * illegal while mstatus.FS is Off (fs_off_o), and so is a reserved rounding
 * mode, rm 101 or 110, or rm 111 with frm 101, 110 or 111. The FP
 * instructions other than the stores set mstatus.FS Dirty, because the RTL
 * raises fflags_we on every FPU result. */
static inline bool cv32e40p_fp_illegal(Iss *iss, iss_insn_t *insn, iss_reg_t pc, bool check_rm)
{
    bool illegal = iss->csr.fp_access_illegal();
    if (check_rm)
    {
        unsigned int rm = (insn->opcode >> 12) & 0x7;
        if (rm == 7)
        {
            rm = iss->csr.fcsr.frm;
        }
        illegal |= rm >= 5;
    }
    if (illegal)
    {
        iss->exception.raise(pc, ISS_EXCEPT_ILLEGAL);
        iss->exec.insn_stall();
    }
    return illegal;
}

static iss_reg_t cv32e40p_fp_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (cv32e40p_fp_illegal(iss, insn, pc, false))
        return pc;
    iss_reg_t next_pc = insn->stub_handler(iss, insn, pc);
    iss->csr.fp_state_dirty();
    return next_pc;
}

static iss_reg_t cv32e40p_fp_rm_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (cv32e40p_fp_illegal(iss, insn, pc, true))
        return pc;
    iss_reg_t next_pc = insn->stub_handler(iss, insn, pc);
    iss->csr.fp_state_dirty();
    return next_pc;
}

static iss_reg_t cv32e40p_fp_store_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    if (cv32e40p_fp_illegal(iss, insn, pc, false))
        return pc;
    return insn->stub_handler(iss, insn, pc);
}

static void cv32e40p_fp_wrap(iss_decoder_item_t *item,
    iss_reg_t (*stub)(Iss *, iss_insn_t *, iss_reg_t))
{
    if (item->u.insn.stub_handler == stub) return;
    /* The decoder items are shared by all the cores built with the same ISA,
     * so another core may have installed the stubs already. The rounding-mode
     * stub then stands for the plain one it replaced. */
    if (item->u.insn.stub_handler == &cv32e40p_fp_rm_stub && stub == &cv32e40p_fp_stub) return;
    if (item->u.insn.stub_handler != nullptr && item->u.insn.stub_handler != &cv32e40p_fp_stub)
        throw std::logic_error("cv32e40p FP stub: instruction already has a stub handler");
    item->u.insn.stub_handler = stub;
}
#endif

void Cv32e40pCsr::start()
{
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA || CONFIG_GVSOC_ISS_CV32E40P_ZFINX
    // Install the stubs before any decode, because a decoded instruction copies its stub.
    auto *fp_ops = this->iss.decode.get_insns_from_tag("fp_op");
    auto *fp_rm = this->iss.decode.get_insns_from_tag("cv32e40p_fp_rm");
    if (fp_ops == nullptr || fp_ops->empty() || fp_rm == nullptr || fp_rm->empty())
        throw std::logic_error("cv32e40p FP stub: no FP instructions in the ISA");
    for (iss_decoder_item_t *item : *fp_ops)
    {
        const std::string label = item->u.insn.label ? item->u.insn.label : "";
        cv32e40p_fp_wrap(item, label == "fsw" ? &cv32e40p_fp_store_stub : &cv32e40p_fp_stub);
    }
    for (iss_decoder_item_t *item : *fp_rm) cv32e40p_fp_wrap(item, &cv32e40p_fp_rm_stub);
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    auto *rvc = this->iss.decode.get_insns_from_isa("rv32c");
    unsigned compressed = 0;
    if (rvc != nullptr)
        for (iss_decoder_item_t *item : *rvc)
        {
            const std::string label = item->u.insn.label ? item->u.insn.label : "";
            if (label == "c.flw" || label == "c.flwsp")
            {
                cv32e40p_fp_wrap(item, &cv32e40p_fp_stub);
                compressed++;
            }
            else if (label == "c.fsw" || label == "c.fswsp")
            {
                cv32e40p_fp_wrap(item, &cv32e40p_fp_store_stub);
                compressed++;
            }
        }
    if (compressed != 4)
        throw std::logic_error("cv32e40p FP stub: compressed FP load/store set incomplete");
#endif
#endif

    /* Runs after Core::mstatus_update, which the Core constructor registers
     * and which sets the read value to the stored one. */
    this->mstatus.register_callback(std::bind(&Cv32e40pCsr::mstatus_read_fixup, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    /* Runs after IrqRiscv::mtvec_access, which the IrqRiscv constructor
     * registers and which stores the value with the mode bit cleared. */
    this->mtvec.register_callback(std::bind(&Cv32e40pCsr::mtvec_write_fixup, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void Cv32e40pCsr::reset(bool active)
{
    Csr::reset(active);

    if (active)
    {
        // dcsr resets with xdebugver = 4 and prv = M.
        this->dcsr = (4 << 28) | 0x3;

        this->depc = 0;
        this->scratch0 = 0;
        this->scratch1 = 0;

        this->mcycle_offset = 0;
        this->minstret_written = false;
        this->mcountinhibit_stale = false;

        // Not in the generic map, so not reset by Csr::reset.
        this->mvendorid_ro.value = 0x00000602;
        this->marchid_ro.value = 0x00000004;
        this->mip.value = 0;
        this->hwloop_lpend[0] = 0;
        this->hwloop_lpend[1] = 0;
    }
}

bool Cv32e40pCsr::mstatus_read_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    // SD (bit 31) is set when FS or XS is Dirty.
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

bool Cv32e40pCsr::mtvec_write_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (!is_write)
    {
        return true;
    }

    /* As in the RTL, the base is wdata[31:8], bits [7:1] read 0 and the mode
     * is wdata[0]. At reset (insn == NULL) the mode is 1 (MTVEC_MODE). */
    iss_reg_t mode = (insn == NULL) ? 1 : (value & 1);
    this->mtvec.value = (value & 0xFFFFFF00) | mode;
    return false;
}

bool Cv32e40pCsr::tselect_read_zero(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    // There is one trigger, so tselect reads 0. The base callback reads -1.
    if (!is_write)
    {
        value = 0;
    }
    return false;
}

bool Cv32e40pCsr::tdata_debug_gate(iss_insn_t *insn, bool is_write, iss_reg_t &value,
    CsrReg *reg, iss_reg_t mask)
{
    // Only debug mode writes the register. The RTL ignores a write from M-mode.
    if (!is_write)
    {
        return true;
    }
    if (this->iss.exec.debug_mode)
    {
        reg->value = (reg->value & ~mask) | (value & mask);
    }
    return false;
}

bool Cv32e40pCsr::mstatus_write_mask(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    /* Only MIE, MPIE and, with the F registers, FS are writable.
     * Core::mstatus_update stores the value next. */
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    constexpr iss_reg_t mask = 0x6088;
#else
    constexpr iss_reg_t mask = 0x88;
#endif
    if (is_write)
    {
        value = (this->mstatus.value & ~mask) | (value & mask);
    }
    return true;
}

bool Cv32e40pCsr::mip_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    // Reads the register driven by the wires and ignores the writes.
    if (!is_write)
    {
        value = this->mip.value;
    }
    return false;
}

/* In dcsr, ebreakm (15), stepie (11) and step (2) are writable and prv
 * reads M. The other fields keep their value, and the debug entry writes the
 * cause. */
bool Cv32e40pCsr::dcsr_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        constexpr iss_reg_t WRITABLE = (1u << 15) | (1u << 11) | (1u << 2);
        this->dcsr = (this->dcsr & ~WRITABLE) | (value & WRITABLE) | 0x3;
    }
    else
    {
        value = this->dcsr;
    }
    return false;
}

// The RTL aligns dpc to 16 bits (depc_n = wdata & ~1).
bool Cv32e40pCsr::dpc_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->depc = value & ~(iss_reg_t)1;
    }
    else
    {
        value = this->depc;
    }
    return false;
}

bool Cv32e40pCsr::dscratch0_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->scratch0 = value;
    }
    else
    {
        value = this->scratch0;
    }
    return false;
}

bool Cv32e40pCsr::dscratch1_view_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->scratch1 = value;
    }
    else
    {
        value = this->scratch1;
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
    /* The offset is unused while CY is set. mcountinhibit_access computes it
     * again from the register pair when CY clears. */
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

bool Cv32e40pCsr::minstret_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    // The write wins over the increment (hpm_commit). The register stores the value as usual.
    if (is_write)
    {
        this->minstret_written = true;
    }
    return true;
}

bool Cv32e40pCsr::minstreth_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    // A write to either half suppresses the increment.
    if (is_write)
    {
        this->minstret_written = true;
    }
    return true;
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

void Cv32e40pCsr::fp_state_dirty()
{
#if CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA
    // A trapped FP instruction has no effect.
    if (this->iss.exec.has_exception)
        return;
    this->mstatus.fs = 3;
#endif
}

/* The user counters are read-only views of the machine counters. A write
 * traps in Cv32e40pCounterAlias::check_access before it reaches the callback. */
bool Cv32e40pCsr::cycle_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    value = (iss_reg_t)this->mcycle_count();
    return false;
}

bool Cv32e40pCsr::cycleh_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    value = (iss_reg_t)(this->mcycle_count() >> 32);
    return false;
}

bool Cv32e40pCsr::instret_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    value = this->minstret.value;
    return false;
}

bool Cv32e40pCsr::instreth_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
#if ISS_REG_WIDTH == 32
    value = this->minstreth.value;
#endif
    return false;
}

bool Cv32e40pCsr::hpm_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index)
{
    value = this->mhpmcounter[index].value;
    return false;
}

bool Cv32e40pCsr::mhpmcounter_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index)
{
    /* A write to either half wins over the increment (hpm_commit). The
     * callback of mask_writes stores the value. */
    if (is_write)
    {
        this->mhpmcounter_written |= 1u << index;
    }
    return true;
}

bool Cv32e40pCsr::mhpmevent_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index)
{
    // Called before the new value is stored. The writing instruction is
    // still counted with the old event selector, so save it here.
    if (is_write)
    {
        this->mhpmevent_old[index] = this->mhpmevent[index].value;
        this->mhpmevent_stale |= 1u << index;
    }
    return true;
}

bool Cv32e40pCsr::hpmh_alias_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int index)
{
#if ISS_REG_WIDTH == 32
    value = this->mhpmcounterh[index].value;
#endif
    return false;
}

bool Cv32e40pCsr::mcountinhibit_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    /* Freezes mcycle into the register pair when CY sets, and restarts the
     * clock offset from it when CY clears. Runs before the masked store, so
     * mcountinhibit.value still holds the old bits. */
    if (is_write)
    {
        // The event lines fire from the full handlers only, as for the Ri5ky PCMR.
        this->iss.exec.switch_to_full_mode();
        // The writing instruction counts under the old bits (hpm_commit).
        this->mcountinhibit_old = this->mcountinhibit.value;
        this->mcountinhibit_stale = true;
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
        // The Hwloop module keeps LPEND - 4, so LPEND is read from hwloop_lpend.
        case 1: value = this->hwloop_lpend[loop]; break;
        case 2: value = this->iss.hwloop.get_count(loop); break;
    }
    return false;
}

bool Cv32e40pCsr::fflags_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write)
    {
        this->fcsr.fflags = value;
        // An fflags write (fflags_we_i) sets mstatus.FS Dirty.
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
