// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/irq/irq_riscv.hpp>

class Cv32e40pIrq : public IrqRiscv
{
public:
    /* Interrupt lines wired in the RTL: MSI(3), MTI(7), MEI(11) and the
     * sixteen fast lines irq[31:16] (cv32e40p_int_controller.sv IRQ_MASK). */
    static constexpr iss_reg_t IRQ_MASK = 0xFFFF0888;

    Cv32e40pIrq(Iss &iss) : IrqRiscv(iss) {}

    void start();

    /* Interrupt take with the RTL priority order and vectored entry;
     * shadows the generic RISC-V ladder (static dispatch via
     * CONFIG_GVSOC_ISS_IRQ). */
    int check();

private:
    bool mie_write_fixup(iss_insn_t *insn, bool is_write, iss_reg_t &value);
};
