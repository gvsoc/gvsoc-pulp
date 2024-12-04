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
from pulp.snitch.snitch_cluster.snitch_cluster import ClusterArch, Area, SnitchCluster
from vp.clock_domain import Clock_domain
import memory.dramsys
import memory.memory
from elftools.elf.elffile import *
import interco.router as router
import utils.loader.loader
import pulp.floonoc.floonoc
import math



class SnitchArchProperties:

    def __init__(self):
        self.nb_cluster              = 1
        self.nb_core_per_cluster     = 9
        self.hbm_size                = 0x80000000
        self.hbm_type                = 'simple'
        self.noc_type                = 'simple'
        self.core_type                = 'accurate'


    def declare_target_properties(self, target):

        self.hbm_size = target.declare_user_property(
            name='hbm_size', value=self.hbm_size, cast=int, description='Size of the HBM external memory'
        )

        self.hbm_type = target.declare_user_property(
            name='hbm_type', value=self.hbm_type, allowed_values=['simple', 'dramsys'],
            description='Type of the HBM external memory'
        )

        self.nb_cluster = target.declare_user_property(
            name='soc/nb_cluster', value=self.nb_cluster, cast=int, description='Number of clusters'
        )

        self.nb_core_per_cluster = target.declare_user_property(
            name='soc/cluster/nb_core', value=self.nb_core_per_cluster, cast=int, description='Number of cores per cluster'
        )

        self.noc_type = target.declare_user_property(
            name='noc_type', value=self.hbm_type, allowed_values=['simple', 'floonoc'], description='Type of the NoC'
        )

        self.core_type = target.declare_user_property(
            name='core_type', value=self.core_type, allowed_values=['accurate', 'fast'], description='Type of the snitch model'
        )


class SnitchArch:

    def __init__(self, target, properties=None):

        if properties is None:
            properties = SnitchArchProperties()

        properties.declare_target_properties(target)

        self.chip = SnitchArch.Chip(properties)
        self.hbm = SnitchArch.Hbm(properties)

    class Hbm:

        def __init__(self, properties):
            self.size = properties.hbm_size
            self.type = properties.hbm_type

    class Chip:

        def __init__(self, properties):

            self.soc = SnitchArch.Chip.Soc(properties)

        class Soc:

            def __init__(self, properties):
                current_hartid = 0

                self.bootrom      = Area( 0x0000_1000,       0x0001_0000)
                self.hbm          = Area( 0x8000_0000,       0x8000_0000)
                self.floonoc      = properties.noc_type == 'floonoc'

                self.nb_cluster = properties.nb_cluster
                self.cluster  = Area(0x1000_0000, 0x0004_0000)


                self.clusters = []
                for id in range(0, self.nb_cluster):
                    cluster_arch = ClusterArch(properties, self.get_cluster_base(id),
                        current_hartid)
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

    def __init__(self, parent, name, arch, binary, debug_binaries):
        super().__init__(parent, name)

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

        # Wide 512 bits router
        wide_axi = router.Router(self, 'wide_axi', bandwidth=64)

        # Clusters
        clusters = []
        for id in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{id}', arch.get_cluster(id), entry=entry))

        # NoC
        if arch.floonoc:
            noc = pulp.floonoc.floonoc.FlooNocClusterGrid(self, 'noc', nb_x_clusters=arch.nb_x_tiles, nb_y_clusters=arch.nb_y_tiles)


        # Extra component for binary loading
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry_addr=arch.bootrom.base + 0x20)

        #
        # Bindings
        #

        # HBM
        wide_axi.o_MAP ( self.i_HBM(), base=arch.hbm.base, size=arch.hbm.size, rm_base=True, latency=100 )
        narrow_axi.o_MAP ( wide_axi.i_INPUT(), base=arch.hbm.base, size=arch.hbm.size, rm_base=False )

        # ROM
        wide_axi.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )
        narrow_axi.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )

        # Clusters
        for id in range(0, arch.nb_cluster):
            clusters[id].o_NARROW_SOC(narrow_axi.i_INPUT())
            clusters[id].o_WIDE_SOC(wide_axi.i_INPUT())
            narrow_axi.o_MAP ( clusters[id].i_NARROW_INPUT (), base=arch.get_cluster_base(id),
                size=arch.cluster.size, rm_base=True  )

        # Binary loader
        loader.o_OUT(narrow_axi.i_INPUT())
        for id in range(0, arch.nb_cluster):
            if id == 0:
                loader.o_START(clusters[id].i_FETCHEN())


    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature='io')



class SocFlooNoc(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, debug_binaries):
        super().__init__(parent, name)

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
        for i in range(1, arch.nb_y_tiles+1):
            bank = memory.memory.Memory(self, f'bank_{arch.nb_x_tiles+1}_{i}', size=arch.bank_size)
            noc.o_MAP(bank.i_INPUT(), base=current_bank_base, size=arch.bank_size,
                x=arch.nb_x_tiles+1, y=i)
            current_bank_base += arch.bank_size

        # ROM
        narrow_axi.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )

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


    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature='io')


class Snitch(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, debug_binaries):
        super(Snitch, self).__init__(parent, name)

        if arch.soc.floonoc:
            soc = SocFlooNoc(self, 'soc', arch.soc, binary, debug_binaries)
        else:
            soc = Soc(self, 'soc', arch.soc, binary, debug_binaries)

        soc.o_HBM(self.i_HBM())

    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')



class SnitchBoard(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(SnitchBoard, self).__init__(parent, name, options=options)

        [args, otherArgs] = parser.parse_known_args()
        debug_binaries = []
        if args.binary is not None:
            debug_binaries.append(args.binary)

        clock = Clock_domain(self, 'clock', frequency=10000000)

        arch = SnitchArch(self)

        chip = Snitch(self, 'chip', arch.chip, args.binary, debug_binaries)

        if arch.hbm.type == 'dramsys':
            mem = memory.dramsys.Dramsys(self, 'ddr')
        else:
            mem = memory.memory.Memory(self, 'mem', size=arch.hbm.size, atomics=True)

        self.bind(clock, 'out', chip, 'clock')
        self.bind(clock, 'out', mem, 'clock')
        self.bind(chip, 'hbm', mem, 'input')
