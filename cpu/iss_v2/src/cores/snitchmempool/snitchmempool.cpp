// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou

#include <cpu/iss_v2/include/iss.hpp>

SnitchMempool::SnitchMempool(Iss &iss)
#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
: vu(iss), iss(iss)
#else
: iss(iss)
#endif
{
    this->iss.traces.new_trace("snitch_mempool", &this->trace, vp::DEBUG);

    this->iss.new_reg("wakeup", &this->wakeup, 0);

    this->barrier_ack_itf.set_sync_meth(&SnitchMempool::barrier_sync);
    this->iss.new_slave_port("barrier_ack", &this->barrier_ack_itf, (vp::Block *)this);

    // Snitch stack CSRs 0x7d0-0x7d2 (CSR_STACK_CONF/START/END)
    this->iss.csr.declare_csr(&this->stack_conf,  "stack_conf",  0x7d0);
    this->iss.csr.declare_csr(&this->stack_start, "stack_start", 0x7d1);
    this->iss.csr.declare_csr(&this->stack_end,   "stack_end",   0x7d2);
}

void SnitchMempool::reset(bool active)
{
#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
    this->vu.reset(active);
#endif
    // The wake-up counter is reset through new_reg.
}

void SnitchMempool::barrier_sync(vp::Block *__this, bool value)
{
    SnitchMempool *_this = (SnitchMempool *)__this;

    if (value)
    {
        if (_this->iss.exec.wfi.get())
        {
            // Already sleeping on WFI: wake it (release the parked InsnEntry).
            _this->iss.exec.wfi.set(false);
            _this->iss.exec.retain_dec();
            _this->iss.exec.insn_terminate(_this->iss.irq.wfi_entry);
        }
        else
        {
            // Wake-up beat the WFI: bank a credit for the next WFI.
            _this->wakeup.inc(1);
        }
    }
}
