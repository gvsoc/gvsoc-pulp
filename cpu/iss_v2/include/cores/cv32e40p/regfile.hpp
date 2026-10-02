// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/regfile.hpp>

/* Shadows of the Regfile methods (CONFIG_GVSOC_ISS_REGFILE), defined in
 * regfile_implem.hpp. */
class Cv32e40pRegfile : public Regfile
{
public:
    Cv32e40pRegfile(Iss &iss) : Regfile(iss), iss(iss) {}

    /* x0 stays zero (cv32e40p_register_file_ff.sv). The decoder redirects
     * rd = x0 to ISS_DUMMY_REG, but the CORE-V post-increment accesses write
     * the base register back through in_regs (IN_REG_SET), which can be x0. */
    inline void set_reg(int reg, uint64_t value);
    inline void set_freg(int reg, uint64_t value);

    /* Dispatch and retry of the instruction in flight. They go through the
     * pipeline stalls, the debug requests decided with the instruction in ID
     * (Cv32e40pIrq::dispatch_decide) and the co-simulation model. */
    inline bool scoreboard_insn_check(iss_insn_t *insn);
    inline void scoreboard_insn_clear(iss_insn_t *insn);

private:
    Iss &iss;
};
