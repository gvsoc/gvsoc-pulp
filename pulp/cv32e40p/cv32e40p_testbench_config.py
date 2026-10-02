# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

from config_tree import Config, cfg_field
from memory.memory_v3 import MemoryV3Config
from interco.router_v2 import RouterConfig, RouterMapping
from pulp.cv32e40p.cv32e40p_config import Cv32e40pConfig
from pulp.cv32e40p.cv32e40p_testbench_mem import Cv32e40pTestbenchMemConfig
from pulp.cv32e40p.cv32e40p_testbench_straps import Cv32e40pTestbenchStrapsConfig


class Cv32e40pTestbenchConfig(Config):
    """Configuration of the CV32E40P testbench SoC.

    Memory map of the core-v-verif CV32E40P UVM testbench (uvmt_cv32e40p), so that its
    test programs run unchanged:
      - mem       at 0x0000_0000 (4 MB, the ram region of the linker script), boot address 0x80
      - debug     at 0x1A11_0800 (4 KB, the dbg region of the linker script, dm_halt_addr_i)
      - elsewhere the testbench memory, which reads 0 until written, with the virtual
        peripherals at their register addresses. The print register is at 0x1000_0000,
        the interrupt timer, debug control, random number and cycle counter at
        0x1500_0000, the test status at 0x2000_0000, exit at 0x2000_0004 and the
        signature at 0x2000_0008.
    The core parameters are named after the RTL top-level parameters.
    """

    fpu: bool = cfg_field(default=False, dump=True, desc=(
        "RTL FPU parameter"
    ))

    zfinx: bool = cfg_field(default=False, dump=True, desc=(
        "RTL ZFINX parameter (requires fpu)"
    ))

    corev_pulp: bool = cfg_field(default=False, dump=True, desc=(
        "RTL COREV_PULP parameter"
    ))

    corev_cluster: bool = cfg_field(default=False, dump=True, desc=(
        "RTL COREV_CLUSTER parameter (requires corev_pulp)"
    ))

    num_mhpmcounters: int = cfg_field(default=1, dump=True, desc=(
        "RTL NUM_MHPMCOUNTERS parameter"
    ))

    boot_addr: int = cfg_field(default=0x80, fmt="hex", dump=True, desc=(
        "Boot address (RTL boot_addr_i)"
    ))

    core: Cv32e40pConfig = cfg_field(init=False, desc=(
        "CV32E40P core configuration"
    ))

    mem: MemoryV3Config = cfg_field(init=False, desc=(
        "Main memory configuration"
    ))

    debug_mem: MemoryV3Config = cfg_field(init=False, desc=(
        "Debug memory configuration (debug handler code of the test programs)"
    ))

    tb_mem: Cv32e40pTestbenchMemConfig = cfg_field(init=False, desc=(
        "Testbench memory and virtual peripherals configuration"
    ))

    straps: Cv32e40pTestbenchStrapsConfig = cfg_field(init=False, desc=(
        "Static inputs of the core driven by the test"
    ))

    router: RouterConfig = cfg_field(init=False, desc=(
        "Router configuration"
    ))

    mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the main memory"
    ))

    debug_mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the debug memory"
    ))

    tb_mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Catch-all route to the testbench memory"
    ))

    def __post_init__(self):
        super().__post_init__()
        if self.zfinx and not self.fpu:
            raise ValueError('zfinx requires fpu')
        isa = 'rv32imfc' if self.fpu else 'rv32imc'
        self.core = Cv32e40pConfig(isa=isa, zfinx=self.zfinx, corev_pulp=self.corev_pulp,
                                   corev_cluster=self.corev_cluster,
                                   num_mhpmcounters=self.num_mhpmcounters,
                                   boot_addr=self.boot_addr)
        # With init=False the bytes never written read 0, as in the UVM testbench,
        # instead of the 0x57 poison.
        self.mem = MemoryV3Config('mem', size=0x40_0000, atomics=False, latency=0, init=False)
        self.debug_mem = MemoryV3Config('debug_mem', size=0x1000, atomics=False, latency=0,
                                        init=False)
        self.tb_mem = Cv32e40pTestbenchMemConfig('tb_mem')
        self.straps = Cv32e40pTestbenchStrapsConfig('straps')
        self.router = RouterConfig(kind='bandwidth')
        self.mem_mapping = RouterMapping(name='mem_mapping', base=0x0000_0000, size=0x40_0000)
        self.debug_mem_mapping = RouterMapping(name='debug_mem_mapping',
                                               base=0x1A11_0800, size=0x1000)
        self.tb_mem_mapping = RouterMapping(name='tb_mem_mapping', base=0x0000_0000, size=0,
                                            remove_base=False)


class Cv32e40pTestbenchBoardConfig(Config):

    frequency: int = cfg_field(default=50000000, dump=True, desc=(
        "Frequency in Hz of the SoC"
    ))

    soc: Cv32e40pTestbenchConfig = cfg_field(init=False, desc=(
        "CV32E40P testbench SoC configuration"
    ))

    def __post_init__(self):
        super().__post_init__()
        self.soc = Cv32e40pTestbenchConfig('soc')
