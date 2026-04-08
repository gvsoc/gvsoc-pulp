# Copyright (C) 2026 OpenHW Group
# SPDX-License-Identifier: Apache-2.0
#
# CV32E40P standalone GVSOC target for UVM co-simulation with RVVI bridge.
#
# Follows the pattern of tutorial 17 (how_to_control_gvsoc_from_an_external_simulator)
# adapted for CV32E40P (ri5cy/PULP FC core) with correct memory map.
#
# Memory map (from cv32e40p/bsp/):
#   0x00000000  4MB   Main RAM        (entry point 0x00000080)
#   0x10000000  256B  Virtual STDOUT  (write-only sink)
#   0x15000000  256B  Virtual TIMER   (write-only sink)
#   0x1A110800  16KB  Debug ROM       (read-only sink)
#   0x20000000  256B  Virtual EXIT    (write-only sink)

import cpu.iss.riscv
import memory.memory
import vp.clock_domain
import interco.router
import utils.loader.loader
import gvsoc.systree
import gvsoc.runner
from gvrun.parameter import TargetParameter
from pulp.cpu.iss.pulp_cores import cv32e40p
from cv32e40p_exit_device import Cv32e40pExitDevice


class Cv32e40pSoc(gvsoc.systree.Component):

    def __init__(self, parent, name, binary, corev_pulp, fpu, zfinx, corev_cluster, core_version,
                 num_mhpmcounters=1):
        super().__init__(parent, name)

        # Main interconnect
        ico = interco.router.Router(self, 'ico')

        # 4MB main RAM @ 0x00000000 (entry point 0x00000080)
        mem = memory.memory.Memory(self, 'mem', size=0x00400000, init=False)
        ico.o_MAP(mem.i_INPUT(), 'mem', base=0x00000000, size=0x00400000, rm_base=True)

        # Virtual STDOUT sink @ 0x10000000 (256B)
        stdout_mem = memory.memory.Memory(self, 'stdout', size=0x100, init=False)
        ico.o_MAP(stdout_mem.i_INPUT(), 'stdout', base=0x10000000, size=0x100, rm_base=True)

        # Virtual TIMER sink @ 0x15000000 (256B)
        timer_mem = memory.memory.Memory(self, 'timer', size=0x100, init=False)
        ico.o_MAP(timer_mem.i_INPUT(), 'timer', base=0x15000000, size=0x100, rm_base=True)

        # Debug ROM sink @ 0x1A110800 (16KB)
        debug_mem = memory.memory.Memory(self, 'debug_rom', size=0x4000, init=False)
        ico.o_MAP(debug_mem.i_INPUT(), 'debug_rom', base=0x1A110800, size=0x4000, rm_base=True)

        # Virtual EXIT device @ 0x20000000 (256B)
        # Terminates GVSOC when the program writes exit_valid to offset +0x04
        exit_dev = Cv32e40pExitDevice(self, 'exit')
        ico.o_MAP(exit_dev.i_INPUT(), 'exit', base=0x20000000, size=0x100, rm_base=True)

        # CV32E40P core: uses the cv32e40p model which sets CONFIG_ISS_CORE=cv32e40p
        # and computes misa/mimpid from fpu/zfinx/pulpv2 to match the RTL configuration.
        core = cv32e40p(self, 'core', fetch_enable=False, boot_addr=0x00000080,
                            cluster_id=0, pulpv2=corev_pulp, fpu=fpu, zfinx=zfinx,
                            core_version=core_version,
                            num_mhpmcounters=num_mhpmcounters)
        core.o_FETCH(ico.i_INPUT())
        core.o_DATA(ico.i_INPUT())


        # ELF loader: loads binary into RAM, signals core entry point and fetch enable
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        loader.o_OUT(ico.i_INPUT())
        loader.o_START(core.i_FETCHEN())
        loader.o_ENTRY(core.i_ENTRY())


# Wrapping component that attaches a clock generator, following tutorial 17 pattern.
class Cv32e40p(gvsoc.systree.Component):

    def __init__(self, parent, name=None):
        super().__init__(parent, name)

        binary = TargetParameter(
            self, name='binary', value=None, description='ELF binary to simulate'
        ).get_value()

        # Core configuration parameters — match CV32E40P RTL generics.
        # Pass via --parameter on the gvrun command line (default = base config).
        corev_pulp    = TargetParameter(self, name='corev_pulp',    value=False,
                            description='Enable PULP extensions (COREV_PULP RTL param)').get_value()
        fpu           = TargetParameter(self, name='fpu',           value=False,
                            description='Enable FPU (FPU RTL param)').get_value()
        zfinx         = TargetParameter(self, name='zfinx',         value=False,
                            description='FPU uses integer regfile (ZFINX RTL param)').get_value()
        corev_cluster = TargetParameter(self, name='corev_cluster', value=False,
                            description='Cluster variant (COREV_CLUSTER RTL param)').get_value()
        core_version  = TargetParameter(self, name='core_version',  value=2,
                            description='ISA version: 1 for legacy PULP, 2 for CORE-V v2').get_value()
        num_mhpmcounters = TargetParameter(self, name='num_mhpmcounters', value=1,
                            description='Number of HPM counters (NUM_MHPMCOUNTERS RTL param)').get_value()

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=50000000)
        soc = Cv32e40pSoc(self, 'soc', binary,
                          corev_pulp=corev_pulp, fpu=fpu,
                          zfinx=zfinx, corev_cluster=corev_cluster,
                          core_version=core_version,
                          num_mhpmcounters=num_mhpmcounters)
        clock.o_CLOCK(soc.i_CLOCK())


# Top target that gvrun will instantiate (mirrors tutorial 17 Target class structure)
class Target(gvsoc.runner.Target):

    description = "CV32E40P standalone for UVM co-simulation"
    model = Cv32e40p
    name = "cv32e40p-standalone"
