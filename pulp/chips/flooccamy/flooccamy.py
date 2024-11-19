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
from pulp.chips.flooccamy.eoc_registers import EoC_Registers
from vp.clock_domain import Clock_domain
import memory.dramsys
import memory.memory
from elftools.elf.elffile import *
from elftools.elf.sections import SymbolTableSection
import interco.router as router
import utils.loader.loader
import pulp.floonoc.floonoc
import math
from typing import List
import cpu.iss.riscv as iss

#Function to get EoC entry
def find_eoc_entry(elf_filename):
    # Open the ELF file in binary mode
    with open(elf_filename, 'rb') as f:
        elffile = ELFFile(f)

        # Find the symbol table section in the ELF file
        for section in elffile.iter_sections():
            if isinstance(section, SymbolTableSection):
                # Iterate over symbols in the symbol table
                for symbol in section.iter_symbols():
                    # Check if this symbol's name matches "tohost"
                    if symbol.name == 'tohost':
                        # Return the symbol's address
                        return symbol['st_value']

    # If the symbol wasn't found, return None
    return None

# Class containing public properties accessible via Parser
class FlooccamyArchProperties:

    def __init__(self):
        # Default values if not specified via parser
        self.nb_cluster              = 16
        self.nb_core_per_cluster     = 9
        self.hbm_size                = 0x4000_0000
        self.hbm_type                = 'simple'
        # self.noc_type                = 'floonoc'


    def declare_target_properties(self, target:gvsoc.systree.Component):

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

        # self.noc_type = target.declare_user_property(
        #     name='noc_type', value=self.hbm_type, allowed_values=['simple', 'floonoc'], description='Type of the NoC'
        # )



# Class containg all properties and configs
class FlooccamyArch:

    def __init__(self, target, properties:FlooccamyArchProperties=None):
        
        # If no public targets specified use defaults
        if properties is None:
            properties = FlooccamyArchProperties()

        # Make the properties available to the Target
        properties.declare_target_properties(target)

        # hbm properties
        self.hbm_type = properties.hbm_type
        self.hbm          = Area( 0x8000_0000,       properties.hbm_size)
        self.bootrom      = Area( 0x0000_1000,       0x0001_0000)
        
        current_hartid = 0
        # self.noc_type_is_floonoc = properties.noc_type == 'floonoc'
        self.nb_cluster = properties.nb_cluster
        self.cluster = Area(0x1000_0000, 0x0004_0000)


        self.cluster_archs = []
        for id in range(0, self.nb_cluster):
            cluster_arch = ClusterArch(properties, self.get_cluster_base(id),
                current_hartid)
            self.cluster_archs.append(cluster_arch)
            current_hartid += self.cluster_archs[id].nb_core

        # if self.noc_type_is_floonoc:
        self.nb_x_tiles = int(math.sqrt(self.nb_cluster))
        self.nb_y_tiles = int(self.nb_cluster / self.nb_x_tiles)
        self.nb_banks = 2*self.nb_x_tiles + 2*self.nb_y_tiles
        self.bank_size = self.hbm.size / self.nb_banks


    def get_cluster_base(self, id:int)->int:
        return self.cluster.base + id * self.cluster.size

    def get_cluster_arch(self, id: int)->ClusterArch:
        return self.cluster_archs[id]





class SocFlooccamy(gvsoc.systree.Component):

    def __init__(self, parent, name, arch:FlooccamyArch, binary, debug_binaries):
        super().__init__(parent, name)

        entry = 0
        eoc_entry = 0
        if binary is not None:
            eoc_entry = find_eoc_entry(binary)
            with open(binary, 'rb') as file:
                elffile = ELFFile(file)
                entry = elffile['e_entry']

        #
        # Components
        #

        # Bootrom
        rom = memory.memory.Memory(self, 'rom', size=arch.bootrom.size,
            stim_file=self.get_file_path('pulp/snitch/bootrom.bin'), atomics=True)

        # Narrow 64bits router
        narrow_axi = router.Router(self, 'narrow_axi', bandwidth=0, synchronous=True)

        # Clusters
        clusters:List[SnitchCluster] = []
        for id in range(0, arch.nb_cluster):
            clusters.append(SnitchCluster(self, f'cluster_{id}', arch.get_cluster_arch(id), entry=entry))

        # NoC
        # if arch.noc_type_is_floonoc:
        noc = pulp.floonoc.floonoc.FlooNocClusterGrid(self, 'noc', width=512/8,
            nb_x_clusters=arch.nb_x_tiles, nb_y_clusters=arch.nb_y_tiles)

        # Extra component for binary loading
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary, entry_addr=arch.bootrom.base + 0x20)

        # EoC Registers
        eoc_registers = EoC_Registers(self, 'eoc_registers', eoc_entry)

        #
        # Bindings
        #

        # EoC
        eoc_registers.o_OUTPUT(narrow_axi.i_INPUT())


        # Clusters
        # Narrow 64bits router
        for id in range(0, arch.nb_cluster):
            clusters[id].o_NARROW_SOC(narrow_axi.i_INPUT())
            narrow_axi.o_MAP ( clusters[id].i_NARROW_INPUT(), base=arch.get_cluster_base(id),
                size=arch.cluster.size, rm_base=False  )


        # Wide 512 bits router / FlooNoC
        for id in range(0, arch.nb_cluster):
            tile_x = int(id / arch.nb_x_tiles)
            tile_y = int(id % arch.nb_x_tiles)

            noc.o_MAP(clusters[id].i_WIDE_INPUT(), base=arch.get_cluster_base(id), size=arch.cluster.size,
                x=tile_x+1, y=tile_y+1)
            clusters[id].o_WIDE_SOC(noc.i_CLUSTER_INPUT(tile_x, tile_y)) # <-----


        # Add a single HBM to allow running the binary
        bank_x = 0
        bank_y = 1
        bank1 = memory.memory.Memory(self, f'bank_{bank_x}_{bank_y}', size=arch.hbm.size, width_log2 = 6,atomics=True)
        noc.o_MAP(bank1.i_INPUT(), base=arch.hbm.base, size=arch.hbm.size,x=bank_x, y=bank_y, rm_base=True)
        narrow_axi.o_MAP (bank1.i_INPUT(), base=arch.hbm.base, size=arch.hbm.size, rm_base=True )

        # bank_x = 1
        # bank_y = 0
        # bank2 = memory.memory.Memory(self, f'bank_{bank_x}_{bank_y}', size=arch.hbm.size, atomics=True)
        # noc.o_MAP(bank2.i_INPUT(), base=arch.hbm.base+arch.hbm.size, size=arch.hbm.size,x=bank_x, y=bank_y, rm_base=True)
        # narrow_axi.o_MAP (bank2.i_INPUT(), base=arch.hbm.base+arch.hbm.size, size=arch.hbm.size, rm_base=True )

        # ROM
        narrow_axi.o_MAP ( rom.i_INPUT     (), base=arch.bootrom.base, size=arch.bootrom.size, rm_base=True  )

        # Binary loader
        loader.o_OUT(narrow_axi.i_INPUT())
        for id in range(0, arch.nb_cluster):
            loader.o_START(clusters[id].i_FETCHEN())


    def i_HBM(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'hbm', signature='io')

    def o_HBM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('hbm', itf, signature='io')



class FlooccamyBoard(gvsoc.systree.Component):

    def __init__(self, parent, name:str, parser, options):
        super().__init__(parent, name, options=options)

        [args, otherArgs] = parser.parse_known_args()
        debug_binaries = []
        if args.binary is not None:
            debug_binaries.append(args.binary)

        # Create the Clock generator
        clock = Clock_domain(self, 'clock', frequency=1_000_000_000)

        # Create the property object
        arch = FlooccamyArch(self)

        # Create the actual Soc
        chip = SocFlooccamy(self, 'chip', arch, args.binary, debug_binaries)

        # # Create the memory
        # if arch.hbm_type == 'dramsys':
        #     mem = memory.dramsys.Dramsys(self, 'ddr')
        # else:
        #     mem = memory.memory.Memory(self, 'mem', size=arch.hbm.size, atomics=True)

        self.bind(clock, 'out', chip, 'clock')
        # self.bind(clock, 'out', mem, 'clock')
        # self.bind(chip, 'hbm', mem, 'input')

