// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/iss.hpp>

SpatzMempool::SpatzMempool(Iss &iss)
: vu(iss), iss(iss)
{
    this->iss.traces.new_trace("spatz_mempool", &this->trace, vp::DEBUG);

    this->iss.new_reg("wakeup", &this->wakeup, 0);

    // Snitch-compatible barrier CSR at 0x7C2.
    this->iss.csr.declare_csr(&this->barrier, "barrier", 0x7C2);
    this->barrier.register_callback(std::bind(&SpatzMempool::barrier_update,
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    this->barrier_ack_itf.set_sync_meth(&SpatzMempool::barrier_sync);
    this->iss.new_slave_port("barrier_ack", &this->barrier_ack_itf, (vp::Block *)this);
    this->iss.new_master_port("barrier_req", &this->barrier_req_itf);
}

void SpatzMempool::reset(bool active)
{
    this->vu.reset(active);
}

bool SpatzMempool::barrier_update(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    // Notify the central barrier on read. The actual sleep is done by a
    // following WFI instruction; barrier_sync on the way back either wakes
    // it up (if it has already issued WFI) or banks a wake-up credit.
    if (!is_write)
    {
        if (this->barrier_req_itf.is_bound())
        {
            this->barrier_req_itf.sync(1);
        }
    }
    return false;
}

void SpatzMempool::barrier_sync(vp::Block *__this, bool value)
{
    SpatzMempool *_this = (SpatzMempool *)__this;

    if (_this->iss.exec.wfi.get())
    {
        // The core is already sleeping on WFI — wake it up. Mirror the
        // IrqRiscv::check_interrupts wake sequence so we release the same
        // InsnEntry that IrqMempool::wfi_handle parked via insn_hold.
        _this->iss.exec.wfi.set(false);
        _this->iss.exec.retain_dec();
        _this->iss.exec.insn_terminate(_this->iss.irq.wfi_entry);
    }
    else
    {
        // The wake-up beat the WFI. Bank a credit; the next WFI will
        // consume it and not sleep.
        _this->wakeup.inc(1);
    }
}
