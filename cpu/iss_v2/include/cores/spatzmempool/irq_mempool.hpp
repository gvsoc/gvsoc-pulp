// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <cpu/iss_v2/include/irq/irq_riscv.hpp>

// Mempool-aware IRQ controller. Identical to IrqRiscv except that
// wfi_handle consults the SpatzMempool wake-up counter first, decrementing
// it and walking past WFI instead of sleeping when a credit is available.
class IrqMempool : public IrqRiscv
{
public:
    IrqMempool(Iss &iss) : IrqRiscv(iss) {}

    void wfi_handle(iss_insn_t *insn);
};
