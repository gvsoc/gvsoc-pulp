/*
 * Compatibility handler for the old SoftHier Snitch DMA ISA.
 *
 * Current GVSoC already provides the standard xDMA handlers. The old runtime
 * names funct7=0000101 as dmmask instead of dmstat, so this target only adds
 * the missing legacy handler locally.
 */

#pragma once

#include <cpu/iss/include/types.hpp>

iss_reg_t dmmask_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc);
