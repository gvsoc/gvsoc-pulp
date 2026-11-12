// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/iss.hpp>

void IrqMempool::wfi_handle(iss_insn_t *insn)
{
    // Consume a banked barrier wake-up if any; otherwise sleep.
    if (this->iss.arch.wakeup_check())
    {
        return;
    }

    this->iss.exec.sleep_enter(insn);
}
