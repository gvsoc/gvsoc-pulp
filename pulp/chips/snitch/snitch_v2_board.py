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

"""Snitch virtual board on the io_v2 stack, with the iss_v2 Snitch core.

Snitch cluster (see ``pulp.snitch.snitch_cluster.snitch_v2_cluster``) with
the SoC fabric of the RTL testbench: wide 512-bit and narrow 64-bit AXI
routers, boot ROM at 0x1000, HBM at 0x8000_0000. Same memory map and boot
flow as the v1 ``snitch`` board: the loader writes the entry point into the
boot ROM, which jumps to it. Everything is io_v2: routers are
``interco.router_v2``, memories are ``memory.memory_v3`` and the binary is
loaded through ``utils.loader.loader_v2``.
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
from pulp.snitch.snitch_cluster.snitch_v2_cluster import ClusterArch, SnitchCluster


class SnitchArch(Tree):
    def __init__(self, parent, name):
        super().__init__(parent, name)

        self.chip = SnitchArch.Chip(self, 'chip')
        self.hbm = SnitchArch.Hbm(self, 'hbm')

    class Hbm(Tree):

        def __init__(self, parent, name):
            super().__init__(parent, name)
            self.size = 0x8000_0000

    class Chip(Tree):

        def __init__(self, parent, name):
            super().__init__(parent, name)

            self.soc = SnitchArch.Chip.Soc(self, 'soc')

        class Soc(Tree):

            def __init__(self, parent, name):
                super().__init__(parent, name)

                self.bootrom = Area(self, 'rom', 0x0000_1000, 0x0001_0000, description='Bootrom range')
                self.hbm     = Area(self, 'hbm', 0x8000_0000, 0x8000_0000, description='HBM range')
                self.cluster = Area(self, 'cluster', 0x1000_0000, 0x0004_0000, description='Cluster range')

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

        #
        # Components
        #

        # Bootrom
        rom = memory_v3.Memory(self, 'rom', config=MemoryV3Config(
            size=arch.bootrom.size, latency=1,
            stim_file=self.get_file_path('pulp/snitch/bootrom.bin')))

        # Narrow 64-bit and wide 512-bit routers
        narrow_axi = router_v2.Router(self, 'narrow_axi', config=RouterConfig(
            kind=KIND_BEAT, width=8, max_pending_bursts_per_input=4))
        wide_axi = router_v2.Router(self, 'wide_axi', config=RouterConfig(
            kind=KIND_BEAT, width=64, max_pending_bursts_per_input=4))

        # Clusters
        clusters = []
        for id in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{id}', arch.get_cluster(id)))

        # Binary loader. The entry point is written into the boot ROM, which
        # jumps to it.
        loader = loader_v2.ElfLoader(self, 'loader', binary=binary,
            entry_addr=arch.bootrom.base + 0x20)

        # The loader emits 64 KB chunks; the TCDM crossbar wants requests
        # that stay within one 8-byte granule. Split the chunks to the
        # narrow bus width before they enter the SoC fabric.
        loader_limiter = limiter_v2.Limiter(self, 'loader_limiter',
            config=LimiterConfig(bandwidth=8))

        #
        # Bindings
        #

        # HBM, behind the testbench memory which accepts 2 read and 2 write
        # transactions at a time (same as the spatz_v3 board, not calibrated
        # on the Snitch RTL yet).
        wide_axi.o_MAP(self.i_HBM(),
            RouterMapping(base=arch.hbm.base, size=arch.hbm.size, max_pending_bursts=2),
            name='hbm')

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

        # Clusters. The zero memory window of a cluster is not decoded by its
        # narrow crossbar, the core accesses to it come out of the cluster and
        # the testbench memory, which answers any address, serves them as the
        # HBM ones (with the same latency, reading 0 until written).
        for id in range(0, arch.nb_cluster):
            cluster = arch.get_cluster(id)
            clusters[id].o_NARROW_SOC(narrow_axi.i_INPUT(0))
            clusters[id].o_WIDE_SOC(wide_axi.i_INPUT(0))
            narrow_axi.o_MAP(clusters[id].i_NARROW_INPUT(),
                RouterMapping(base=arch.get_cluster_base(id),
                    size=cluster.zero_mem.base - arch.get_cluster_base(id),
                    remove_base=False), name=f'cluster_{id}')
            tb_zero_mem = memory_v3.Memory(self, f'tb_zero_mem_{id}', config=MemoryV3Config(
                size=cluster.zero_mem.size, atomics=True, latency=10, init=False))
            narrow_axi.o_MAP(narrow_wide_bridge.i_INPUT(2 + id),
                RouterMapping(base=cluster.zero_mem.base, size=cluster.zero_mem.size,
                    remove_base=False), name=f'cluster_{id}_zero_mem')
            wide_axi.o_MAP(tb_zero_mem.i_INPUT(),
                RouterMapping(base=cluster.zero_mem.base, size=cluster.zero_mem.size,
                    max_pending_bursts=2), name=f'tb_zero_mem_{id}')

        # Binary loader
        loader.o_OUT(loader_limiter.i_INPUT())
        loader_limiter.o_OUTPUT(narrow_axi.i_INPUT(arch.nb_cluster))
        for id in range(0, arch.nb_cluster):
            if id == 0:
                loader.o_START(clusters[id].i_FETCHEN())

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


class Snitch(gvsoc.systree.Component):

    def __init__(self, parent, name: str, parser, arch, binary, debug_binaries):
        super().__init__(parent, name)

        soc = Soc(self, 'soc', parser, arch.soc, binary, debug_binaries)

        soc.o_HBM(self.i_HBM())

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature=IoV2Beat(64))

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature=IoV2Beat(64))


class SnitchBoard(gvsoc.systree.Component):

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

        arch = SnitchArch(self, 'snitch')

        # Expose the attributes so that tools like the build process can query
        # the architecture (memory map, cluster characteristics, and so on)
        self.set_attributes(arch)

        # Attach a target name so that the build process knows how to compile
        # executables for this board
        self.set_target_name('snitch')

        chip = Snitch(self, 'chip', parser, arch.chip, binary, debug_binaries)

        # The latency models the AXI path to the testbench memory (taken from
        # the spatz_v3 board, not calibrated on the Snitch RTL yet).
        mem = memory_v3.Memory(self, 'mem', config=MemoryV3Config(
            size=arch.hbm.size, atomics=True, latency=10, init=False))

        self.bind(clock, 'out', chip, 'clock')
        self.bind(clock, 'out', mem, 'clock')
        # Signatured binding so the auto-bridge pass sees IoV2Beat(64) master
        # vs IoV2Sync memory and inserts the beat-to-sync converter (a raw
        # bind() carries no signatures and would leave the beat router facing
        # the sync memory's inline DONE, which a beat master must never see).
        chip.o_HBM(mem.i_INPUT())
