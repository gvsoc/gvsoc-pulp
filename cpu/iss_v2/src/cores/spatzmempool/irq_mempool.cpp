// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/iss.hpp>

void IrqMempool::wfi_handle(iss_insn_t *insn)
{
    // Same gating as IrqRiscv: only sleep if no pending+enabled interrupt.
    if ((this->iss.csr.mie.value & this->iss.csr.mip.value) == 0)
    {
        // Consume a banked barrier wake-up if any; otherwise sleep.
        if (this->iss.arch.wakeup_check())
        {
            return;
        }

        this->iss.exec.wfi.set(true);
        this->iss.exec.wfi_start = this->iss.clock.get_cycles();
        this->iss.exec.retain_inc();
        this->wfi_entry = this->iss.exec.insn_hold(insn);
    }
}
