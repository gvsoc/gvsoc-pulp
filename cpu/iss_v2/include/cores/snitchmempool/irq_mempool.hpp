// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <cpu/iss_v2/include/irq/irq_riscv.hpp>

// IrqRiscv whose wfi_handle consumes a SnitchMempool wake-up credit before sleeping.
class IrqMempool : public IrqRiscv
{
public:
    IrqMempool(Iss &iss) : IrqRiscv(iss) {}

    void wfi_handle(iss_insn_t *insn);
};
