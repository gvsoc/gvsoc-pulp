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

# Author: Chi Zhang <chizhang@ethz.ch>

import gvsoc.runner
import cpu.iss.riscv as iss
import memory.memory
import interco.router as router
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gvsoc.systree
from pulp.chips.softhier.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.softhier.softhier_ctrl import SoftHierCtrl
from pulp.chips.softhier.softhier_arch import SoftHierArch
from pulp.floonoc.floonoc import FlooNocClusterGridNarrowWide
import memory.dramsys
import math


def is_power_of_two(name, value):
    if value == 0:
        return True
    if value < 0 or (value & (value - 1)) != 0:
        raise AssertionError(f"The value of '{name}' must be a power of two, but got {value}.")
    return True


class FlexClusterSystem(gvsoc.systree.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        #################
        # Configuration #
        #################

        arch            = SoftHierArch()
        num_clusters    = arch.num_cluster_x * arch.num_cluster_y

        # Get Binary
        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        ######################
        # implicitly setting #
        ######################
        if not hasattr(arch, 'multi_idma_enable'): arch.multi_idma_enable = 0
        if not hasattr(arch, 'hbm_node_aliase'): arch.hbm_node_aliase = 1


        ##############
        # Components #
        ##############

        #Clusters
        cluster_list=[]
        for cluster_id in range(num_clusters):
            cluster_arch = ClusterArch( nb_core_per_cluster =   arch.num_core_per_cluster,
                                        base                =   arch.cluster_tcdm_base,
                                        remote_base         =   arch.cluster_tcdm_remote,
                                        cluster_id          =   cluster_id,
                                        tcdm_size           =   arch.cluster_tcdm_size,
                                        stack_base          =   arch.cluster_stack_base,
                                        stack_size          =   arch.cluster_stack_size,
                                        zomem_base          =   arch.cluster_zomem_base,
                                        zomem_size          =   arch.cluster_zomem_size,
                                        reg_base            =   arch.cluster_reg_base,
                                        reg_size            =   arch.cluster_reg_size,
                                        insn_base           =   arch.instruction_mem_base,
                                        insn_size           =   arch.instruction_mem_size,
                                        nb_tcdm_banks       =   arch.cluster_tcdm_bank_nb,
                                        tcdm_bank_width     =   arch.cluster_tcdm_bank_width/8,
                                        idma_outstand_txn   =   arch.idma_outstand_txn,
                                        idma_outstand_burst =   arch.idma_outstand_burst,
                                        num_cluster_x       =   arch.num_cluster_x,
                                        num_cluster_y       =   arch.num_cluster_y,
                                        spatz_core_list     =   arch.spatz_attaced_core_list,
                                        spatz_num_vlsu      =   arch.spatz_num_vlsu_port,
                                        multi_idma_enable   =   arch.multi_idma_enable)
            cluster_list.append(ClusterUnit(self,f'cluster_{cluster_id}', cluster_arch, binary))
            pass

        #Virtual router, just for debugging and non-performance-critical jobs
        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)

        #Control register
        softhier_ctrl = SoftHierCtrl(self, 'softhier_ctrl', num_cluster_x=arch.num_cluster_x, num_cluster_y=arch.num_cluster_y)

        #Debug Memory
        debug_mem = memory.memory.Memory(self,'debug_mem', size=1)

        #HBM Preloader
        hbm_preloader = utils.loader.loader.ElfLoader(self, 'hbm_preloader', binary=binary)

        #System Interconnets
        narrow_bus = router.Router(self, 'narrow_bus', bandwidth=8)
        wide_bus = router.Router(self, 'wide_bus', bandwidth=64)

        #HBM
        hbm_memory = memory.dramsys.Dramsys(self, f'hbm_memory', dram_type='hbm2')

        ############
        # Bindings #
        ############

        #Debug memory
        narrow_bus.o_MAP(debug_mem.i_INPUT())

        #Control register
        narrow_bus.o_MAP(softhier_ctrl.i_INPUT(), base=arch.soc_register_base, size=arch.soc_register_size, rm_base=True)

        #HBM Preloader
        self.bind(hbm_preloader, f'out', hbm_memory, 'input')
        wide_bus.o_START(softhier_ctrl.i_HBM_PRELOAD_DONE())

        #HBM
        narrow_bus.add_mapping('hbm', base=arch.instruction_mem_base, size=arch.main_memory_size, rm_base=True)
        wide_bus.add_mapping('hbm', base=arch.instruction_mem_base, size=arch.main_memory_size, rm_base=True)
        self.bind(wide_bus, f'hbm', hbm_memory, 'input')
        self.bind(narrow_bus, f'hbm', hbm_memory, 'input')

        #Clusters
        for cluster_id in range(num_clusters):
            cluster_list[cluster_id].o_NARROW_SOC(narrow_bus.i_INPUT())
            cluster_list[cluster_id].o_WIDE_SOC(wide_bus.i_INPUT())
            narrow_bus.o_MAP(cluster_list[cluster_id].i_NARROW_INPUT(), base=arch.cluster_tcdm_remote + cluster_id*arch.cluster_tcdm_size, size=arch.cluster_tcdm_size, rm_base=True)
            wide_bus.o_MAP(cluster_list[cluster_id].i_WIDE_INPUT(), 	base=arch.cluster_tcdm_remote + cluster_id*arch.cluster_tcdm_size, size=arch.cluster_tcdm_size, rm_base=True)
            softhier_ctrl.o_HBM_PRELOAD_DONE_TO_CLUSTER(cluster_list[cluster_id].i_HBM_PRELOAD_DONE(),cluster_id)
            pass


class SoftHierChip(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(SoftHierChip, self).__init__(parent, name, options=options)

        arch  = SoftHierArch()
        clock = Clock_domain(self, 'clock', frequency=(1000000000 if not hasattr(arch, 'frequence') else arch.frequence))

        flex_cluster_system = FlexClusterSystem(self, 'system', parser)

        self.bind(clock, 'out', flex_cluster_system, 'clock')
