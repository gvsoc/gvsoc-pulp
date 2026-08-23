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

"""Spatz virtual board, v3 generation — full io_v2 stack.

Single Spatz cluster (see ``pulp.snitch.snitch_cluster.spatz_cluster_v3``)
with the SoC fabric of the RTL testbench: wide 512-bit and narrow 64-bit
AXI routers, boot ROM at 0x1000, HBM at 0x8000_0000 (8 cycles of latency
modeling the RTL AXI path to the testbench memory: registered crossbars,
IW/DW converters and the axi_to_reg frontend). Everything is io_v2:
routers are
``interco.router_v2``, memories are ``memory.memory_v3`` and the binary
is loaded through ``utils.loader.loader_v2``.
"""

import os

import gvsoc.systree
from gvsoc.signature import IoV2Beat
from vp.clock_domain import Clock_domain
import memory.memory_v3 as memory_v3
from memory.memory_v3 import MemoryV3Config
import interco.router_v2 as router_v2
from interco.router_v2 import RouterConfig, RouterMapping, KIND_BEAT, KIND_UNTIMED
import interco.limiter_v2 as limiter_v2
from interco.limiter_v2 import LimiterConfig
import utils.loader.loader_v2 as loader_v2
from elftools.elf.elffile import *
from gvrun.parameter import TargetParameter
from gvrun.attribute import Tree, Area
from pulp.snitch.snitch_cluster.spatz_cluster_v3 import ClusterArch, SnitchCluster


class SpatzArch(Tree):
    def __init__(self, parent, name):
        super().__init__(parent, name)

        self.chip = SpatzArch.Chip(self, 'chip')
        self.hbm = SpatzArch.Hbm(self, 'hbm')

    class Hbm(Tree):

        def __init__(self, parent, name):
            super().__init__(parent, name)
            self.size = 0x8000_0000

    class Chip(Tree):

        def __init__(self, parent, name):
            super().__init__(parent, name)

            self.soc = SpatzArch.Chip.Soc(self, 'soc')

        class Soc(Tree):

            def __init__(self, parent, name):
                super().__init__(parent, name)

                self.bootrom = Area(self, 'rom', 0x0000_1000, 0x0001_0000, description='Bootrom range')
                self.hbm     = Area(self, 'hbm', 0x8000_0000, 0x8000_0000, description='HBM range')
                self.cluster = Area(self, 'cluster', 0x0010_0000, 0x0004_0000, description='Cluster range')

                self.nb_cluster = 1
                self.clusters = []
                current_hartid = 0
                for id in range(0, self.nb_cluster):
                    cluster_arch = ClusterArch(self, 'cluster', self.get_cluster_base(id),
                        current_hartid)
                    self.clusters.append(cluster_arch)
                    current_hartid += self.clusters[id].nb_core

            def get_cluster_base(self, id: int):
                return self.cluster.base + id * self.cluster.size

            def get_cluster(self, id: int):
                return self.clusters[id]


class Soc(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, arch, binary, debug_binaries):
        super().__init__(parent, name)

        TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        entry = 0
        if binary is not None:
            with open(binary, 'rb') as file:
                elffile = ELFFile(file)
                entry = elffile['e_entry']

        #
        # Components
        #

        # Bootrom
        rom = memory_v3.Memory(self, 'rom', config=MemoryV3Config(
            size=arch.bootrom.size, latency=1,
            stim_file=self.get_file_path('pulp/snitch/bootrom_spatz.bin')))

        # Narrow 64-bit and wide 512-bit routers
        narrow_axi = router_v2.Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=8, max_pending_bursts_per_input=4))
        wide_axi = router_v2.Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=64, max_pending_bursts_per_input=4))

        # Clusters
        clusters = []
        for id in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{id}', arch.get_cluster(id),
                entry=entry))

        # Binary loader. The entry point is written into the cluster
        # peripheral CLUSTER_BOOT_CONTROL register (offset 0x58) like the
        # RTL testbench does; the bootrom then jumps to it.
        loader = loader_v2.ElfLoader(self, 'loader', binary=binary,
            entry_addr=arch.get_cluster(0).peripheral.base + 0x58)

        # The loader emits 64 KB chunks; the TCDM crossbar wants requests
        # that stay within one 8-byte granule. Split the chunks to the
        # narrow bus width before they enter the SoC fabric.
        loader_limiter = limiter_v2.Limiter(self, 'loader_limiter',
            config=LimiterConfig(bandwidth=8))

        #
        # Bindings
        #

        # HBM. The memory itself answers immediately in the RTL testbench,
        # but the AXI path to it (registered crossbars, IW/DW converters,
        # axi_to_reg) costs ~8 cycles — carried by the board memory latency.
        wide_axi.o_MAP(self.i_HBM(),
            RouterMapping(base=arch.hbm.base, size=arch.hbm.size), name='hbm')

        # ROM sits on the wide crossbar (RTL BootROM slave).
        wide_axi.o_MAP(rom.i_INPUT(),
            RouterMapping(base=arch.bootrom.base, size=arch.bootrom.size), name='rom')

        # Narrow accesses to the HBM and ROM cross to the wide crossbar (RTL
        # IW/DW-upsize path). A beat router cannot bind another one of a
        # different width directly, so the crossing goes through an untimed
        # bridge (the framework inserts the beat adapters on both sides).
        narrow_wide_bridge = router_v2.Router(self, 'narrow_wide_bridge',
            config=RouterConfig(kind=KIND_UNTIMED))
        narrow_axi.o_MAP(narrow_wide_bridge.i_INPUT(0),
            RouterMapping(base=arch.hbm.base, size=arch.hbm.size, remove_base=False),
            name='hbm')
        narrow_axi.o_MAP(narrow_wide_bridge.i_INPUT(1),
            RouterMapping(base=arch.bootrom.base, size=arch.bootrom.size, remove_base=False),
            name='rom')
        narrow_wide_bridge.o_MAP_DEFAULT(wide_axi.i_INPUT(1), name='wide')

        # Clusters
        for id in range(0, arch.nb_cluster):
            clusters[id].o_NARROW_SOC(narrow_axi.i_INPUT(0))
            clusters[id].o_WIDE_SOC(wide_axi.i_INPUT(0))
            narrow_axi.o_MAP(clusters[id].i_NARROW_INPUT(),
                RouterMapping(base=arch.get_cluster_base(id), size=arch.cluster.size,
                    remove_base=False), name=f'cluster_{id}')

        # Binary loader
        loader.o_OUT(loader_limiter.i_INPUT())
        loader_limiter.o_OUTPUT(narrow_axi.i_INPUT(arch.nb_cluster))
        for id in range(0, arch.nb_cluster):
            if id == 0:
                loader.o_START(clusters[id].i_FETCHEN())
                for core in range(0, arch.get_cluster(id).nb_core):
                    loader.o_START(clusters[id].i_MEIP(core))

        # Make sure the loader is notified by any executable attached to the
        # hierarchy of this component so that it is automatically loaded
        self.loader = loader
        self.clusters = clusters
        self.register_binary_handler(self.handle_binary)

    def configure(self):
        # We configure the loader binary now in the configure step since it is
        # coming from a parameter which can be set either from command line or
        # from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)
            # Under gvrun, the binary may not exist yet at configure time since it can be
            # produced by the build step. Register it as an executable so that the cores
            # handle it at generate time, once it has been built. Binaries built by gvrun
            # register themselves as executables, so only register the missing ones,
            # like prebuilt binaries given through the binary parameter.
            if os.environ.get('USE_GVRUN2') is None:
                for cluster in self.clusters:
                    cluster.handle_executable(binary)
            else:
                from gvrun.systree import ExecutableContainer
                registered = [exe.get_binary() for exe in self.get_executables()]
                if binary not in registered:
                    self.add_executable(ExecutableContainer(binary))

    def handle_binary(self, binary):
        # This gets called when an executable is attached to a hierarchy of
        # components containing this one
        self.set_parameter('binary', binary)

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature=IoV2Beat(64))

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature=IoV2Beat(64))


class Spatz(gvsoc.systree.Component):

    def __init__(self, parent, name: str, parser, arch, binary, debug_binaries):
        super(Spatz, self).__init__(parent, name)

        soc = Soc(self, 'soc', parser, arch.soc, binary, debug_binaries)

        soc.o_HBM(self.i_HBM())

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature=IoV2Beat(64))

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature=IoV2Beat(64))


class SpatzBoard(gvsoc.systree.Component):

    def __init__(self, parent, name: str, parser, options):
        super().__init__(parent, name, options=options)

        binary = None
        debug_binaries = []
        if os.environ.get('USE_GVRUN') is None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary
            if binary is not None:
                debug_binaries.append(binary)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        arch = SpatzArch(self, 'spatz')

        # Expose the attributes so that tools like the build process can query
        # the architecture (memory map, cluster characteristics, and so on)
        self.set_attributes(arch)

        # Attach a target name so that the build process knows how to compile
        # executables for this board (pulpos.spatz module)
        self.set_target_name('spatz')

        chip = Spatz(self, 'chip', parser, arch.chip, binary, debug_binaries)

        # 8 cycles model the RTL AXI path to the testbench memory
        # (calibrated against the RTL: a scalar load round trip costs
        # ~30 cycles there, an icache line refill similar).
        mem = memory_v3.Memory(self, 'mem', config=MemoryV3Config(
            size=arch.hbm.size, atomics=True, latency=10, init=False))

        self.bind(clock, 'out', chip, 'clock')
        self.bind(clock, 'out', mem, 'clock')
        # Signatured binding so the auto-bridge pass sees IoV2Beat(64) master
        # vs IoV2Sync memory and inserts the beat-to-sync converter (a raw
        # bind() carries no signatures and would leave the beat router facing
        # the sync memory's inline DONE, which a beat master must never see).
        chip.o_HBM(mem.i_INPUT())
