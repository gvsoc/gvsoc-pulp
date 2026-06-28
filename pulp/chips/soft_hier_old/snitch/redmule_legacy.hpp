/*
 * Compatibility handlers for the old SoftHier RedMule ISA.
 *
 * Old applications encode RedMule with custom opcodes 0x0a and 0x2a. The
 * current core interface uses generic ISS offload, so the local offload decoder
 * dispatches these requests to the copied old RedMule model.
 */

#pragma once

#include <cpu/iss/include/types.hpp>

iss_reg_t mcnfig_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc);
iss_reg_t marith_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc);
