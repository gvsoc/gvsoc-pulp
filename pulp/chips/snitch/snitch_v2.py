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

import gvsoc.runner
import gvsoc.systree
from vp.clock_domain import Clock_domain
import memory.dramsys
import memory.memory
from elftools.elf.elffile import *
import interco.router as router
import utils.loader.loader
import pulp.floonoc.floonoc
import math
from gvrun.parameter import TargetParameter
from gvrun.attribute import Tree, Area, Value
from pulp.snitch.snitch_cluster.snitch_cluster_v2 import ClusterArch, SnitchCluster


class SnitchAttr(Tree):
    def __init__(self, parent, name, nb_cluster=1, spatz=False, spatz_nb_lanes=4):
        super().__init__(parent, name)

        self.chip = SnitchAttr.Chip(self, 'chip', spatz, spatz_nb_lanes, nb_cluster)
        self.hbm = SnitchAttr.Hbm(self, 'hbm')

    class Hbm(Tree):

        def __init__(self, parent, name):
            super().__init__(parent, name)
            self.type = 'simple'
            self.size = 0x8000_0000

    class Chip(Tree):

        def __init__(self, parent, name, use_spatz, spatz_nb_lanes, nb_cluster):
            super().__init__(parent, name)

            self.soc = SnitchAttr.Chip.Soc(self, 'soc', use_spatz, spatz_nb_lanes, nb_cluster)

        class Soc(Tree):

            def __init__(self, parent, name, use_spatz, spatz_nb_lanes, nb_cluster):
                super().__init__(parent, name)
                current_hartid = 0
                noc_type = 'simple'

                self.bootrom      = Area(self, 'rom', 0x0000_1000, 0x0001_0000, description='Bootrom range')
                self.hbm          = Area(self, 'hbm', 0x8000_0000, 0x8000_0000, description='HBM range')
                self.floonoc      = noc_type == 'floonoc'
                self.use_spatz    = use_spatz

                self.nb_cluster = nb_cluster

                if use_spatz:
                    self.cluster  = Area(self, 'cluster', 0x0010_0000, 0x0004_0000, description='Cluster range')
                else:
                    self.cluster  = Area(self, 'cluster', 0x1000_0000, 0x0004_0000, description='Cluster range')


                self.clusters = []
                for id in range(0, self.nb_cluster):
                    cluster_arch = ClusterArch(self, 'cluster', self.get_cluster_base(id),
                        current_hartid, spatz=use_spatz, spatz_nb_lanes=spatz_nb_lanes)
                    self.clusters.append(cluster_arch)
                    current_hartid += self.clusters[id].nb_core

                if self.floonoc:
                    self.nb_x_tiles = int(math.sqrt(self.nb_cluster))
                    self.nb_y_tiles = int(self.nb_cluster / self.nb_x_tiles)
                    self.nb_banks = 2*self.nb_x_tiles + 2*self.nb_y_tiles
                    self.bank_size = self.hbm.size / self.nb_banks


            def get_cluster_base(self, id:int):
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

        # Spatz RTL benchmarks do HBM accesses with zero latency memory
        hbm_latency = 0 if arch.use_spatz else 100

        #
        # Components
        #

        # Bootrom
        romfile = 'pulp/snitch/bootrom.bin'
        if arch.use_spatz:
            romfile = 'pulp/snitch/bootrom_spatz.bin'

        rom = memory.memory.Memory(self, 'rom', size=arch.bootrom.size,
            stim_file=self.get_file_path(romfile))

        # Narrow 64bits router
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8)

        # Wide 512 bits router
        wide_axi = router.Router(self, 'wide_axi', bandwidth=64)

        # Clusters
        clusters = []
        for id in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{id}', arch.get_cluster(id), parser, entry=entry,
                binaries=debug_binaries))

        # NoC
        if arch.floonoc:
            noc = pulp.floonoc.floonoc.FlooNocClusterGrid(self, 'noc', nb_x_clusters=arch.nb_x_tiles, nb_y_clusters=arch.nb_y_tiles)


        # Extra component for binary loading
        if arch.use_spatz:
            entry_addr = arch.get_cluster(0).peripheral.base + 0x58
        else:
            entry_addr = arch.bootrom.base + 0x20
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry_addr=entry_addr)

        #
        # Bindings
        #

        # HBM
        wide_axi.o_MAP ( self.i_HBM(), base=arch.hbm.base, size=arch.hbm.size, rm_base=True, latency=hbm_latency )
        narrow_axi.o_MAP ( wide_axi.i_INPUT(), base=arch.hbm.base, size=arch.hbm.size, rm_base=False )

        # ROM
        wide_axi.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )
        narrow_axi.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )

        # Clusters
        for id in range(0, arch.nb_cluster):
            clusters[id].o_NARROW_SOC(narrow_axi.i_INPUT())
            clusters[id].o_WIDE_SOC(wide_axi.i_INPUT())
            narrow_axi.o_MAP ( clusters[id].i_NARROW_INPUT (), base=arch.get_cluster_base(id),
                size=arch.cluster.size, rm_base=False  )

        # Binary loader
        loader.o_OUT(narrow_axi.i_INPUT())
        for id in range(0, arch.nb_cluster):
            if id == 0:
                loader.o_START(clusters[id].i_FETCHEN())
                if arch.use_spatz:
                    for core in range(0, arch.get_cluster(id).nb_core):
                        loader.o_START(clusters[id].i_MEIP(core))

        # Make sure the loader is notified by any executable attached to the hieararchy of this
        # component so that it is automatically loaded
        self.loader = loader
        self.clusters = clusters
        self.register_binary_handler(self.handle_binary)

    def configure(self):
        # We configure the loader binary now int he configure steps since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)
            for cluster in self.clusters:
                cluster.handle_executable(binary)


    def handle_binary(self, binary):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature='io')



class SocFlooNoc(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, debug_binaries):
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
        rom = memory.memory.Memory(self, 'rom', size=arch.bootrom.size,
            stim_file=self.get_file_path('pulp/snitch/bootrom.bin'))

        # Narrow 64bits router
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=8)

        # Clusters
        clusters = []
        for id in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{id}', arch.get_cluster(id), entry=entry))

        # NoC
        if arch.floonoc:
            noc = pulp.floonoc.floonoc.FlooNocClusterGrid(self, 'noc', width=512/8,
                nb_x_clusters=arch.nb_x_tiles, nb_y_clusters=arch.nb_y_tiles)


        # Extra component for binary loading
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry_addr=arch.bootrom.base + 0x20)

        #
        # Bindings
        #

        # Memory
        narrow_axi.o_MAP ( noc.i_INPUT     (1, 1), base=arch.hbm.base, size=arch.hbm.size, rm_base=False )

        for id in range(0, arch.nb_cluster):
            tile_x = int(id % arch.nb_x_tiles)
            tile_y = int(id / arch.nb_x_tiles)
            noc.o_MAP(clusters[id].i_WIDE_INPUT(), base=arch.get_cluster_base(id), size=arch.cluster.size,
                x=tile_x+1, y=tile_y+1)

            clusters[id].o_WIDE_SOC(noc.i_CLUSTER_INPUT(tile_x, tile_y))

        current_bank_base = arch.hbm.base
        for i in range(1, arch.nb_x_tiles+1):
            bank = memory.memory.Memory(self, f'bank_{i}_0', size=arch.bank_size)
            noc.o_MAP(bank.i_INPUT(), base=current_bank_base, size=arch.bank_size,
                x=i, y=0)
            current_bank_base += arch.bank_size
        for i in range(1, arch.nb_x_tiles+1):
            bank = memory.memory.Memory(self, f'bank_{i}_{arch.nb_y_tiles+1}', size=arch.bank_size)
            noc.o_MAP(bank.i_INPUT(), base=current_bank_base, size=arch.bank_size,
                x=i, y=arch.nb_x_tiles+1)
            current_bank_base += arch.bank_size
        for i in range(1, arch.nb_y_tiles+1):
            bank = memory.memory.Memory(self, f'bank_0_{i}', size=arch.bank_size)
            noc.o_MAP(bank.i_INPUT(), base=current_bank_base, size=arch.bank_size,
                x=0, y=i)
            current_bank_base += arch.bank_size
        for i in range(1, arch.nb_y_tiles):
            bank = memory.memory.Memory(self, f'bank_{arch.nb_x_tiles+1}_{i}', size=arch.bank_size)
            noc.o_MAP(bank.i_INPUT(), base=current_bank_base, size=arch.bank_size,
                x=arch.nb_x_tiles+1, y=i)
            current_bank_base += arch.bank_size

        # ROM
        narrow_axi.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )
        noc.o_MAP ( narrow_axi.i_INPUT     (), base=0, size=arch.bootrom.size, x=arch.nb_x_tiles+1, y=arch.nb_y_tiles  )

        # Clusters
        for id in range(0, arch.nb_cluster):
            clusters[id].o_NARROW_SOC(narrow_axi.i_INPUT())
            narrow_axi.o_MAP ( clusters[id].i_NARROW_INPUT (), base=arch.get_cluster_base(id),
                size=arch.cluster.size, rm_base=True  )

        # Binary loader
        loader.o_OUT(narrow_axi.i_INPUT())
        for id in range(0, arch.nb_cluster):
            if id == 0:
                loader.o_START(clusters[id].i_FETCHEN())

        # Make sure the loader is notified by any executable attached to the hieararchy of this
        # component so that it is automatically loaded
        self.loader = loader
        self.register_binary_handler(self.handle_binary)

    def configure(self):
        # We configure the loader binary now int he configure steps since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

    def handle_binary(self, binary):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature='io')


class Snitch(gvsoc.systree.Component):

    def __init__(self, parent, name:str, parser, arch, binary, debug_binaries):
        super(Snitch, self).__init__(parent, name)

        if arch.soc.floonoc:
            soc = SocFlooNoc(self, 'soc', arch.soc, binary, debug_binaries)
        else:
            soc = Soc(self, 'soc', parser, arch.soc, binary, debug_binaries)

        soc.o_HBM(self.i_HBM())

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')



class SnitchBoard(gvsoc.systree.Component):

    def __init__(self, parent, name:str, parser, options, spatz=False):
        super().__init__(parent, name, options=options)

        binary = None
        debug_binaries = []
        if os.environ.get('USE_GVRUN') is None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary
            if binary is not None:
                debug_binaries.append(binary)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        arch = SnitchAttr(self, 'snitch', spatz=spatz)

        chip = Snitch(self, 'chip', parser, arch.chip, binary, debug_binaries)

        if arch.hbm.type == 'dramsys':
            mem = memory.dramsys.Dramsys(self, 'ddr')
        else:
            mem = memory.memory.Memory(self, 'mem', size=arch.hbm.size, atomics=True,
                width_log2=2)

        self.bind(clock, 'out', chip, 'clock')
        self.bind(clock, 'out', mem, 'clock')
        self.bind(chip, 'hbm', mem, 'input')


class SpatzBoard(SnitchBoard):

    def __init__(self, parent, name:str, parser, options):
        super().__init__(parent, name, parser, options, spatz=True)
