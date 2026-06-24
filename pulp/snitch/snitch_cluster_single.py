#
# Copyright (C) 2020 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

try:
    from typing import override  # Python 3.12+
except ImportError:
    from typing_extensions import override  # Python 3.10–3.11

import os

import gvsoc.runner
import pulp.snitch.snitch_core as iss
import memory.memory as memory
from pulp.snitch.hierarchical_cache import Hierarchical_cache
from vp.clock_domain import Clock_domain
import pulp.snitch.l1_subsystem as l1_subsystem
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from pulp.snitch.snitch_cluster.cluster_registers import ClusterRegisters
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from pulp.idma.snitch_dma import SnitchDma
from pulp.snitch.zero_mem import ZeroMem
from interco.bus_watchpoint import Bus_watchpoint
from pulp.snitch.sequencer import Sequencer
from elftools.elf.elffile import *
import gvsoc.runner as gvsoc
from gvrun.parameter import TargetParameter

import math


def _get_elf_info(binary):
    # The HTIF watchpoints need the addresses of the tohost/fromhost symbols of the
    # binary to catch accesses to them, and the cluster registers expose its entry
    # point as boot address.
    tohost_addr = 0
    fromhost_addr = 0
    entry = 0
    with open(binary, 'rb') as file:
        elffile = ELFFile(file)
        entry = elffile['e_entry']
        for section in elffile.iter_sections():
            if isinstance(section, SymbolTableSection):
                for symbol in section.iter_symbols():
                    if symbol.name == 'tohost':
                        tohost_addr = symbol.entry['st_value']
                    if symbol.name == 'fromhost':
                        fromhost_addr = symbol.entry['st_value']
    return tohost_addr, fromhost_addr, entry


class Soc(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        # The cluster has 8 computation cores and one DMA core.
        nb_cores = 8 + 1
        Xfrep = 1

        # Snitch core complex
        int_cores = []
        fp_cores = []
        bus_watchpoints = []
        if Xfrep:
            fpu_sequencers = []

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        # With the legacy gvsoc launcher, configure() is never called and the binary comes
        # from the command line, so it must be resolved now. With gvrun, it comes from a
        # parameter and is handled in configure() since it can also be set from the build
        # process.
        binary = None
        if os.environ.get('USE_GVRUN') is None and parser is not None:
            parser.add_argument("--isa", dest="isa", type=str, default="rv32imfdca",
                help="RISCV-V ISA string (default: %(default)s)")

            [args, __] = parser.parse_known_args()
            binary = args.binary

        # Memory Components
        # rom = memory.Memory(self, 'rom', size=0x10000, width_log2=3, stim_file=self.get_file_path('pulp/chips/spatz/rom.bin'))
        rom = memory.Memory(self, 'rom', size=0x10000, width_log2=3, stim_file=self.get_file_path('pulp/snitch/bootrom.bin'))

        mem = memory.Memory(self, 'mem', size=0x80000000, width_log2=6, atomics=True)

        # Zero memory
        zero_mem = ZeroMem(self, 'zero_mem', size=0x10000)

        # Snitch TCDM (L1 subsystem)
        l1 = l1_subsystem.L1_subsystem(self, 'l1', self, nb_pe=nb_cores, size=0x20000, nb_port=3, bandwidth=8)

        # Shared icache
        icache = Hierarchical_cache(self, 'shared_icache', nb_cores=nb_cores, has_cc=0, l1_line_size_bits=7)


        # Main Router, i_cluster_xbar
        ico = router.Router(self, 'ico', bandwidth=8, latency=8)
        # Dedicated router for dma to TCDM, i_axi_dma_xbar
        dma_ico = router.Router(self, 'dma_ico', bandwidth=64, latency=0)


        # Core Complex
        for core_id in range(0, nb_cores):
            int_cores.append(iss.Snitch(self, f'pe{core_id}', isa='rv32imfdca', fetch_enable=False,
                                        boot_addr=0x0000_1000, core_id=core_id))
            fp_cores.append(iss.Snitch_fp_ss(self, f'fp_ss{core_id}', isa='rv32imfdca', fetch_enable=False,
                                        boot_addr=0x0000_1000, core_id=core_id))
            if Xfrep:
                fpu_sequencers.append(Sequencer(self, f'fpu_sequencer{core_id}', latency=0))

        # Binary Loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry=0x1000, entry_addr=0x1020)


        # Interconnction Bindings
        ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x80000000)
        self.bind(ico, 'mem', mem, 'input')
        dma_ico.add_mapping('mem', base=0x80000000, remove_offset=0x80000000, size=0x80000000)
        self.bind(dma_ico, 'mem', mem, 'input')

        ico.add_mapping('rom', base=0x00001000, remove_offset=0x00001000, size=0x10000)
        self.bind(ico, 'rom', rom, 'input')
        dma_ico.add_mapping('rom', base=0x00001000, remove_offset=0x00001000, size=0x10000)
        self.bind(dma_ico, 'rom', rom, 'input')

        ico.add_mapping('zero_mem', base=0x10030000, remove_offset=0x10030000, size=0x0010000)
        self.bind(ico, 'zero_mem', zero_mem, 'input')
        dma_ico.add_mapping('zero_mem', base=0x10030000, remove_offset=0x10030000, size=0x0010000)
        self.bind(dma_ico, 'zero_mem', zero_mem, 'input')

        self.bind(l1, 'cluster_ico', ico, 'input')
        self.bind(ico, 'l1', l1, 'input')
        ico.add_mapping('l1', base=0x10000000, remove_offset=0x10000000, size=0x20000)
        # self.bind(l1, 'cluster_ico', dma_ico, 'input')
        # self.bind(dma_ico, 'l1', l1, 'input')
        # dma_ico.add_mapping('l1', base=0x10000000, remove_offset=0x10000000, size=0x20000)

        icache.o_REFILL( dma_ico.i_INPUT() )


        # RISCV bus watchpoint
        tohost_addr = 0
        fromhost_addr = 0
        entry = 0
        if binary is not None:
            tohost_addr, fromhost_addr, entry = _get_elf_info(binary)


        # Cluster peripherals
        # cluster_registers = Cluster_registers(self, 'cluster_registers', boot_addr=entry, nb_cores=nb_cores)
        # ico.add_mapping('cluster_registers', base=0x00120000, remove_offset=0x00120000, size=0x1000)
        cluster_registers = ClusterRegisters(self, 'cluster_registers', boot_addr=entry, nb_cores=nb_cores)
        ico.add_mapping('cluster_registers', base=0x10020000, remove_offset=0x10020000, size=0x10000)
        self.bind(ico, 'cluster_registers', cluster_registers, 'input')


        # Cluster DMA
        idma = SnitchDma(self, 'idma', loc_base=0x10000000, loc_size=0x20000, tcdm_width=64)

        # TCDM and DMA bindings
        idma.o_AXI(dma_ico.i_INPUT())
        idma.o_TCDM(l1.i_DMA_INPUT())

        # DMA Core and DMA bindings
        int_cores[nb_cores-1].o_OFFLOAD(idma.i_OFFLOAD())
        idma.o_OFFLOAD_GRANT(int_cores[nb_cores-1].i_OFFLOAD_GRANT())


        # Core Interconnections
        for core_id in range(0, nb_cores):
            bus_watchpoints.append(Bus_watchpoint(self, f'tohost{core_id}', tohost_addr, fromhost_addr, word_size=32))

        for core_id in range(0, nb_cores):
            # Sync all core complex by integer cores.
            self.bind(int_cores[core_id], 'barrier_req', cluster_registers, f'barrier_req_{core_id}')
            self.bind(cluster_registers, f'barrier_ack', int_cores[core_id], 'barrier_ack')
            cluster_registers.o_EXTERNAL_IRQ(core_id, int_cores[core_id].i_IRQ(19))

        for core_id in range(0, nb_cores):
            # Buswatchpoint
            self.bind(bus_watchpoints[core_id], 'output', l1, 'data_pe_%d' % core_id)

            # Icache
            self.bind(int_cores[core_id], 'flush_cache_req', icache, 'flush')
            self.bind(icache, 'flush_ack', int_cores[core_id], 'flush_cache_ack')

            # Snitch integer cores
            self.bind(int_cores[core_id], 'data', bus_watchpoints[core_id], 'input')
            int_cores[core_id].o_FETCH( icache.i_INPUT(core_id))
            self.bind(loader, 'start', int_cores[core_id], 'fetchen')
            self.bind(loader, 'entry', int_cores[core_id], 'bootaddr')

            # Snitch fp subsystems
            # Pay attention to interactions and bandwidth between subsystem and tohost.
            self.bind(fp_cores[core_id], 'data', bus_watchpoints[core_id], 'input')
            # FP subsystem doesn't fetch instructions from core->ico->memory, but from integer cores acc_req.
            self.bind(loader, 'start', fp_cores[core_id], 'fetchen')
            self.bind(loader, 'entry', fp_cores[core_id], 'bootaddr')

            # SSR in fp subsystem datem mover <-> memory port
            self.bind(fp_cores[core_id], 'ssr_dm0', l1, 'data_pe_%d' % core_id)
            self.bind(fp_cores[core_id], 'ssr_dm1', l1, 'ssr_1_pe_%d' % core_id)
            self.bind(fp_cores[core_id], 'ssr_dm2', l1, 'ssr_2_pe_%d' % core_id)

            # Use WireMaster & WireSlave
            # Add fpu sequence buffer in between int core and fp core to issue instructions
            if Xfrep:
                self.bind(int_cores[core_id], 'acc_req', fpu_sequencers[core_id], 'input')
                self.bind(fpu_sequencers[core_id], 'output', fp_cores[core_id], 'acc_req')
                self.bind(int_cores[core_id], 'acc_req_ready', fpu_sequencers[core_id], 'acc_req_ready')
                self.bind(fpu_sequencers[core_id], 'acc_req_ready_o', fp_cores[core_id], 'acc_req_ready')
            else:
                # Comment out if we want to add sequencer
                self.bind(int_cores[core_id], 'acc_req', fp_cores[core_id], 'acc_req')
                self.bind(int_cores[core_id], 'acc_req_ready', fp_cores[core_id], 'acc_req_ready')

            self.bind(fp_cores[core_id], 'acc_rsp', int_cores[core_id], 'acc_rsp')

        # DMA and Wider AXI interconnections
        self.bind(loader, 'out', dma_ico, 'input')

        self.loader = loader
        self.bus_watchpoints = bus_watchpoints
        self.cluster_registers = cluster_registers
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        # We configure the loader binary now in the configure step since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

            tohost_addr, fromhost_addr, entry = _get_elf_info(binary)
            for watchpoint in self.bus_watchpoints:
                watchpoint.add_properties({
                    'riscv_fesvr_tohost_addr': tohost_addr,
                    'riscv_fesvr_fromhost_addr': fromhost_addr,
                })
            self.cluster_registers.add_properties({'boot_addr': entry})

    def handle_binary(self, binary: str):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


class SnitchSystem(st.Component):

    def __init__(self, parent, name, parser, options):

        super(SnitchSystem, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        soc = Soc(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')


class Target(gvsoc.Target):

    gapy_description="Snitch virtual board"
    model = SnitchSystem
    name = "snitch_cluster_single"

    def __init__(self, parser, options=None, name=None):
        super(Target, self).__init__(parser, options,
            model=SnitchSystem, name=name)
