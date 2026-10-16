# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

from config_tree import cfg_field
from cpu.iss_v2.riscv_config import RiscvConfig


class Cv32e40pConfig(RiscvConfig):
    """CV32E40P configuration, named after the RTL top-level parameters
    (cv32e40p_top.sv). The F extension is taken from the ISA string."""

    isa: str = cfg_field(default='rv32imc', dump=True, desc=(
        "ISA string of the core, rv32imc or rv32imfc (RTL FPU parameter)"
    ))
    zfinx: bool = cfg_field(default=False, dump=True, desc=(
        "True if the FP instructions use the integer register file (RTL ZFINX parameter)"
    ))
    corev_pulp: bool = cfg_field(default=False, dump=True, desc=(
        "True if the CORE-V PULP extensions are implemented (RTL COREV_PULP parameter)"
    ))
    corev_cluster: bool = cfg_field(default=False, dump=True, desc=(
        "True if the core is built for a PULP cluster: cv.elw is implemented and a WFI "
        "never sleeps (RTL COREV_CLUSTER parameter, requires corev_pulp)"
    ))
    num_mhpmcounters: int = cfg_field(default=1, dump=True, desc=(
        "Number of implemented mhpmcounter registers (RTL NUM_MHPMCOUNTERS parameter)"
    ))
    debug_handler: int = cfg_field(default=0x1A110800, dump=True, fmt="hex", desc=(
        "Debug mode entry address (RTL dm_halt_addr_i input)"
    ))
    debug_exception_handler: int = cfg_field(default=0x1A111600, dump=True, fmt="hex", desc=(
        "Entry address of exceptions taken in debug mode (RTL dm_exception_addr_i input)"
    ))
    mtvec_addr: int = cfg_field(default=0, dump=True, fmt="hex", desc=(
        "mtvec base at boot (RTL mtvec_addr_i input), for a chip tying the input to a "
        "constant; the mtvec_addr port, when driven, overrides it"
    ))
