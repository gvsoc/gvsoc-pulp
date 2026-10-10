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
from pulp.chips.soft_hier_old.memory import Memory
import interco.router as router
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gvsoc.systree
from pulp.chips.soft_hier_old.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.soft_hier_old.ctrl_registers import CtrlRegisters
from pulp.chips.soft_hier_old.flex_cluster_arch import FlexClusterArch
from pulp.chips.soft_hier_old.flex_mesh_noc import FlexMeshNoC
from pulp.chips.soft_hier_old.hbm_ctrl import hbm_ctrl
from pulp.chips.soft_hier_old.power_models import SUPPORTED_POWER_PROFILES
import memory.dramsys
import math
import importlib.util
import os

GAPY_TARGET = True

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

        # The co-simulation provider selects an immutable run-local architecture
        # without overwriting the tracked default preset or installed generators.
        architecture_file = os.environ.get('SOFTHIER_ARCH_FILE')
        if architecture_file:
            spec = importlib.util.spec_from_file_location('softhier_run_arch', architecture_file)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            arch = module.FlexClusterArch()
        else:
            arch = FlexClusterArch()
        num_clusters    = arch.num_cluster_x * arch.num_cluster_y
        noc_outstanding = (arch.num_cluster_x + arch.num_cluster_y) + arch.noc_outstanding
        has_hbm = any(arch.hbm_chan_placement)
        num_hbm_ctrl_x  = arch.num_cluster_x // arch.num_node_per_ctrl if has_hbm else 0
        num_hbm_ctrl_y  = arch.num_cluster_y // arch.num_node_per_ctrl if has_hbm else 0

        # Get Binary
        binary = None
        preload_binary = None
        has_preload_binary = 0
        core_model = None
        power_profile = None
        preload_mode = getattr(arch, 'preload_mode', 'direct')
        if parser is not None:
            parser.add_argument("--preload", type=str, help="Path to the HBM preload binary file")
            parser.add_argument("--preload-mode", choices=["direct", "timed"],
                default=preload_mode, help="ELF loading mode (default: direct, no simulated cycles)")
            parser.add_argument("--core-model", choices=["fast", "accurate"],
                help="SoftHier legacy core model to use")
            parser.add_argument("--power-profile", choices=SUPPORTED_POWER_PROFILES,
                help="Component leakage model profile")
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary
            preload_binary = args.preload
            preload_mode = args.preload_mode
            core_model = args.core_model
            power_profile = args.power_profile
            if preload_binary is not None:
                has_preload_binary = 1

        ######################
        # implicitly setting #
        ######################
        if not hasattr(arch, 'spatz_vlsu_port_width'): arch.spatz_vlsu_port_width = 32
        if not hasattr(arch, 'spatz_vreg_gather_eff'): arch.spatz_vreg_gather_eff = 100
        if not hasattr(arch, 'multi_idma_enable'): arch.multi_idma_enable = 0
        if not hasattr(arch, 'hbm_node_aliase'): arch.hbm_node_aliase = 1
        if not hasattr(arch, 'hbm_node_aliase_start_bit'): arch.hbm_node_aliase_start_bit = 48
        if not hasattr(arch, 'hbm_ctrl_xor_scrambling'): arch.hbm_ctrl_xor_scrambling = 0
        if not hasattr(arch, 'hbm_ctrl_red_scrambling'): arch.hbm_ctrl_red_scrambling = 0
        if not hasattr(arch, 'tech_node'): arch.tech_node = "5nm"
        if not hasattr(arch, 'core_model'): arch.core_model = "fast"
        if not hasattr(arch, 'power_profile'): arch.power_profile = "constant"
        if not hasattr(arch, 'power_estimate_scale'): arch.power_estimate_scale = 1.0
        if not hasattr(arch, 'dram3d_enable'): arch.dram3d_enable = 0
        if not hasattr(arch, 'dram3d_type'): arch.dram3d_type = 'hbm2-example.json'
        if not hasattr(arch, 'dram3d_addr_base'): arch.dram3d_addr_base = 0x10000000000
        if not hasattr(arch, 'dram3d_node_space'): arch.dram3d_node_space = 0xc0000000
        if core_model is not None: arch.core_model = core_model
        if power_profile is not None: arch.power_profile = power_profile
        if preload_mode not in ('direct', 'timed'):
            raise ValueError(f"Invalid preload_mode: {preload_mode}")
        direct_preload = preload_mode == 'direct'

        if arch.dram3d_enable:
            base, size = arch.dram3d_addr_base, arch.dram3d_node_space
            if not isinstance(base, int) or not isinstance(size, int) or size <= 0 or base < arch.cluster_tcdm_size:
                raise ValueError('dram3d requires a positive node space and a base above TCDM offsets')
            if base + size * num_clusters > 1 << 64:
                raise ValueError('dram3d address range exceeds 64 bits')
            if not isinstance(arch.dram3d_type, str) or not arch.dram3d_type:
                raise ValueError('dram3d_type must name a DRAMSys configuration')
            # Reject overlap with global destinations, including explicit HBM aliases.
            regions = [(arch.cluster_tcdm_remote, arch.cluster_tcdm_size * num_clusters),
                       (arch.soc_register_base, arch.soc_register_size)]
            edge_base = arch.hbm_start_base
            for channels, nodes in zip(arch.hbm_chan_placement,
                    (arch.num_cluster_y, arch.num_cluster_x, arch.num_cluster_y, arch.num_cluster_x)):
                if channels:
                    for node in range(nodes):
                        regions.append((edge_base + (node // arch.hbm_node_aliase) *
                            arch.hbm_node_addr_space * arch.hbm_node_aliase +
                            (1 << arch.hbm_node_aliase_start_bit) * (node % arch.hbm_node_aliase),
                            arch.hbm_node_addr_space * arch.hbm_node_aliase))
                edge_base += nodes * arch.hbm_node_addr_space
            if any(base < other + length and other < base + size * num_clusters
                   for other, length in regions):
                raise ValueError('dram3d address range overlaps another global destination')

        #############
        # Assertion #
        #############
        if has_hbm:
            is_power_of_two("arch.num_node_per_ctrl", arch.num_node_per_ctrl)
            assert 0 < arch.hbm_node_aliase <= arch.num_node_per_ctrl
            assert arch.num_cluster_x >= arch.num_node_per_ctrl and arch.num_cluster_x % arch.num_node_per_ctrl == 0
            assert arch.num_cluster_y >= arch.num_node_per_ctrl and arch.num_cluster_y % arch.num_node_per_ctrl == 0
            for edge, controllers in enumerate((num_hbm_ctrl_y, num_hbm_ctrl_x, num_hbm_ctrl_y, num_hbm_ctrl_x)):
                channels = arch.hbm_chan_placement[edge]
                is_power_of_two(f"arch.hbm_chan_placement[{edge}]", channels)
                assert channels == 0 or channels >= controllers

        ctrl_chan_west  = arch.hbm_chan_placement[0] // num_hbm_ctrl_y if has_hbm else 0
        ctrl_chan_north = arch.hbm_chan_placement[1] // num_hbm_ctrl_x if has_hbm else 0
        ctrl_chan_east  = arch.hbm_chan_placement[2] // num_hbm_ctrl_y if has_hbm else 0
        ctrl_chan_south = arch.hbm_chan_placement[3] // num_hbm_ctrl_x if has_hbm else 0

        ##############
        # Components #
        ##############

        #Clusters
        cluster_list=[]
        for cluster_id in range(num_clusters):
            cluster_arch = ClusterArch( nb_core_per_cluster =   arch.num_core_per_cluster,
                                        base                =   arch.cluster_tcdm_base,
                                        cluster_id          =   cluster_id,
                                        tcdm_size           =   arch.cluster_tcdm_size,
                                        stack_base          =   arch.cluster_stack_base,
                                        stack_size          =   arch.cluster_stack_size,
                                        zomem_base          =   arch.cluster_zomem_base,
                                        zomem_size          =   arch.cluster_zomem_size,
                                        reg_base            =   arch.cluster_reg_base,
                                        reg_size            =   arch.cluster_reg_size,
                                        sync_base           =   arch.sync_base,
                                        sync_itlv           =   arch.sync_interleave,
                                        sync_special_mem    =   arch.sync_special_mem,
                                        insn_base           =   arch.instruction_mem_base,
                                        insn_size           =   arch.instruction_mem_size,
                                        nb_tcdm_banks       =   arch.cluster_tcdm_bank_nb,
                                        tcdm_bank_width     =   arch.cluster_tcdm_bank_width/8,
                                        redmule_ce_height   =   arch.redmule_ce_height,
                                        redmule_ce_width    =   arch.redmule_ce_width,
                                        redmule_ce_pipe     =   arch.redmule_ce_pipe,
                                        redmule_elem_size   =   arch.redmule_elem_size,
                                        redmule_queue_depth =   arch.redmule_queue_depth,
                                        redmule_reg_base    =   arch.redmule_reg_base,
                                        redmule_reg_size    =   arch.redmule_reg_size,
                                        idma_outstand_txn   =   arch.idma_outstand_txn,
                                        idma_outstand_burst =   arch.idma_outstand_burst,
                                        num_cluster_x       =   arch.num_cluster_x,
                                        num_cluster_y       =   arch.num_cluster_y,
                                        spatz_core_list     =   arch.spatz_attaced_core_list,
                                        spatz_num_vlsu      =   arch.spatz_num_vlsu_port,
                                        spatz_num_fu        =   arch.spatz_num_function_unit,
                                        spatz_vlsu_bw       =   arch.spatz_vlsu_port_width,
                                        spatz_vreg_gather_eff = arch.spatz_vreg_gather_eff,
                                        data_bandwidth      =   arch.noc_link_width/8,
                                        multi_idma_enable   =   arch.multi_idma_enable,
                                        core_model          =   arch.core_model,
                                        tech_node           =   arch.tech_node,
                                        power_profile       =   arch.power_profile,
                                        power_estimate_scale = arch.power_estimate_scale,
                                        dram3d_base         = arch.dram3d_addr_base + cluster_id * arch.dram3d_node_space,
                                        dram3d_size         = arch.dram3d_node_space if arch.dram3d_enable else 0)
            cluster_list.append(ClusterUnit(self,f'cluster_{cluster_id}', cluster_arch, binary,
                direct_preload=direct_preload))
            pass

        #Virtual router, just for debugging and non-performance-critical jobs (eg, Printf, EoC, Check HBM stored value)
        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)

        #Control register
        csr = CtrlRegisters(self, 'ctrl_registers', num_cluster_x=arch.num_cluster_x,
            num_cluster_y=arch.num_cluster_y, has_preload_binary=has_preload_binary,
            direct_preload=direct_preload)

        #Synchronization bus
        sync_bus = FlexMeshNoC(self, 'sync_bus', width=4,
                tech_node=arch.tech_node, power_profile=arch.power_profile, power_estimate_scale=arch.power_estimate_scale,
                nb_x_clusters=arch.num_cluster_x, nb_y_clusters=arch.num_cluster_y,
                ni_outstanding_reqs=noc_outstanding, router_input_queue_size=noc_outstanding * num_clusters, atomics=1, collective=1)

        #HBM channels
        dram3d_channels = []
        if arch.dram3d_enable:
            for cluster_id in range(num_clusters):
                dram3d_channels.append(memory.dramsys.Dramsys(self,
                    f'dram3d_chan_{cluster_id}', dram_type=arch.dram3d_type))

        hbm_chan_list_west = []
        for hbm_ch in range(arch.hbm_chan_placement[0]):
            hbm_chan_list_west.append(memory.dramsys.Dramsys(self, f'west_hbm_chan_{hbm_ch}', dram_type='hbm4-emu-example.json'))
            pass

        hbm_chan_list_north = []
        for hbm_ch in range(arch.hbm_chan_placement[1]):
            hbm_chan_list_north.append(memory.dramsys.Dramsys(self, f'north_hbm_chan_{hbm_ch}', dram_type='hbm4-emu-example.json'))
            pass

        hbm_chan_list_east = []
        for hbm_ch in range(arch.hbm_chan_placement[2]):
            hbm_chan_list_east.append(memory.dramsys.Dramsys(self, f'east_hbm_chan_{hbm_ch}', dram_type='hbm4-emu-example.json'))
            pass

        hbm_chan_list_south = []
        for hbm_ch in range(arch.hbm_chan_placement[3]):
            hbm_chan_list_south.append(memory.dramsys.Dramsys(self, f'south_hbm_chan_{hbm_ch}', dram_type='hbm4-emu-example.json'))
            pass

        #HBM controllers
        hbm_ctrl_list_west = []
        for hbm_ct in range(num_hbm_ctrl_y if ctrl_chan_west else 0):
            nb_slaves=ctrl_chan_west
            hbm_ctrl_list_west.append(hbm_ctrl(self, f'west_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        hbm_ctrl_list_north = []
        for hbm_ct in range(num_hbm_ctrl_x if ctrl_chan_north else 0):
            nb_slaves=ctrl_chan_north
            hbm_ctrl_list_north.append(hbm_ctrl(self, f'north_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        hbm_ctrl_list_east = []
        for hbm_ct in range(num_hbm_ctrl_y if ctrl_chan_east else 0):
            nb_slaves=ctrl_chan_east
            hbm_ctrl_list_east.append(hbm_ctrl(self, f'east_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        hbm_ctrl_list_south = []
        for hbm_ct in range(num_hbm_ctrl_x if ctrl_chan_south else 0):
            nb_slaves=ctrl_chan_south
            hbm_ctrl_list_south.append(hbm_ctrl(self, f'south_hbm_ctrl_{hbm_ct}', nb_slaves=nb_slaves, nb_masters=arch.num_node_per_ctrl, interleaving_bits=int(math.log2(arch.noc_link_width/8)), node_addr_offset=arch.hbm_node_addr_space, hbm_node_aliase=arch.hbm_node_aliase, xor_scrambling=arch.hbm_ctrl_xor_scrambling, red_scrambling=arch.hbm_ctrl_red_scrambling))
            pass

        #NoC
        data_noc = FlexMeshNoC(self, 'data_noc', width=arch.noc_link_width/8,
                tech_node=arch.tech_node, power_profile=arch.power_profile, power_estimate_scale=arch.power_estimate_scale,
                nb_x_clusters=arch.num_cluster_x, nb_y_clusters=arch.num_cluster_y,
                ni_outstanding_reqs=noc_outstanding, router_input_queue_size=noc_outstanding * num_clusters, collective=1,
                edge_node_alias=arch.hbm_node_aliase, edge_node_alias_start_bit=arch.hbm_node_aliase_start_bit)


        #Debug Memory
        debug_mem = Memory(self,'debug_mem', size=1)

        #HBM Preloader
        hbm_preloader = utils.loader.loader.ElfLoader(self, 'hbm_preloader',
            binary=preload_binary, direct=direct_preload)

        ############
        # Bindings #
        ############

        #Debug memory
        virtual_interco.o_MAP(debug_mem.i_INPUT())
        if has_hbm:
            virtual_interco.o_MAP(data_noc.i_CLUSTER_INPUT(0, 0), base=arch.hbm_start_base, size=arch.hbm_node_addr_space * 2 * (arch.num_cluster_x + arch.num_cluster_y), rm_base=False)
        if arch.dram3d_enable:
            virtual_interco.o_MAP(data_noc.i_CLUSTER_INPUT(0, 0),
                name='dram3d', base=arch.dram3d_addr_base,
                size=arch.dram3d_node_space * num_clusters, rm_base=False)

        #Control register
        virtual_interco.o_MAP(csr.i_INPUT(), base=arch.soc_register_base, size=arch.soc_register_size, rm_base=True)

        #HBM Preloader
        hbm_preloader.o_OUT(data_noc.i_CLUSTER_INPUT(0, 0))
        hbm_preloader.o_START(csr.i_HBM_PRELOAD_DONE())

        #Clusters
        for cluster_id in range(num_clusters):
            cluster_list[cluster_id].o_NARROW_SOC(virtual_interco.i_INPUT())
            csr.o_HBM_PRELOAD_DONE_TO_CLUSTER(cluster_list[cluster_id].i_HBM_PRELOAD_DONE(),cluster_id)
            pass

        #Data NoC + Sync NoC
        for node_id in range(num_clusters):
            x_id = int(node_id%arch.num_cluster_x)
            y_id = int(node_id/arch.num_cluster_x)
            cluster_list[node_id].o_WIDE_SOC(data_noc.i_CLUSTER_INPUT(x_id, y_id))
            cluster_list[node_id].o_SYNC_OUTPUT(sync_bus.i_CLUSTER_INPUT(x_id, y_id))
            data_noc.o_MAP(cluster_list[node_id].i_WIDE_INPUT(), base=arch.cluster_tcdm_remote  + node_id*arch.cluster_tcdm_size,   size=arch.cluster_tcdm_size,    x=x_id+1, y=y_id+1)
            if arch.dram3d_enable:
                data_noc.o_MAP(cluster_list[node_id].i_WIDE_INPUT(),
                    name=f'dram3d_{node_id}', base=arch.dram3d_addr_base + node_id * arch.dram3d_node_space,
                    size=arch.dram3d_node_space, x=x_id+1, y=y_id+1, rm_base=False)
                cluster_list[node_id].o_DRAM3D(dram3d_channels[node_id].i_INPUT())
            sync_bus.o_MAP(cluster_list[node_id].i_SYNC_INPUT(), base=arch.sync_base            + node_id*(arch.sync_interleave + arch.sync_special_mem),     size=(arch.sync_interleave + arch.sync_special_mem),      x=x_id+1, y=y_id+1)
            pass

        #HBM controllers and channels connections
        #Mapping:
        #         1->
        #      ________
        #   ^ |        | ^
        #   | |        | |
        #   0 |        | 2
        #     |________|
        #
        #         3->
        hbm_edge_start_base = arch.hbm_start_base

        ## west
        for node_id in range(arch.num_cluster_y if ctrl_chan_west else 0):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'west_{node_id}')
            itf_router.add_mapping('output')
            aliase_offset = (1 << arch.hbm_node_aliase_start_bit)
            base_addr = hbm_edge_start_base + (node_id // arch.hbm_node_aliase) * (arch.hbm_node_addr_space * arch.hbm_node_aliase) + aliase_offset * (node_id % arch.hbm_node_aliase)
            node_size = arch.hbm_node_addr_space * arch.hbm_node_aliase
            data_noc.o_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, x=0, y=node_id+1)
            self.bind(itf_router, 'output', hbm_ctrl_list_west[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[0]):
            ctrl_id = chan_id // ctrl_chan_west
            self.bind(hbm_ctrl_list_west[ctrl_id], f'out_{chan_id % ctrl_chan_west}', hbm_chan_list_west[chan_id], 'input')
            pass
        hbm_edge_start_base += arch.num_cluster_y*arch.hbm_node_addr_space

        ## north
        for node_id in range(arch.num_cluster_x if ctrl_chan_north else 0):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'north_{node_id}')
            itf_router.add_mapping('output')
            aliase_offset = (1 << arch.hbm_node_aliase_start_bit)
            base_addr = hbm_edge_start_base + (node_id // arch.hbm_node_aliase) * (arch.hbm_node_addr_space * arch.hbm_node_aliase) + aliase_offset * (node_id % arch.hbm_node_aliase)
            node_size = arch.hbm_node_addr_space * arch.hbm_node_aliase
            data_noc.o_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, x=node_id+1, y=arch.num_cluster_y+1)
            self.bind(itf_router, 'output', hbm_ctrl_list_north[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[1]):
            ctrl_id = chan_id // ctrl_chan_north
            self.bind(hbm_ctrl_list_north[ctrl_id], f'out_{chan_id % ctrl_chan_north}', hbm_chan_list_north[chan_id], 'input')
            pass
        hbm_edge_start_base += arch.num_cluster_x*arch.hbm_node_addr_space

        ## east
        for node_id in range(arch.num_cluster_y if ctrl_chan_east else 0):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'east_{node_id}')
            itf_router.add_mapping('output')
            aliase_offset = (1 << arch.hbm_node_aliase_start_bit)
            base_addr = hbm_edge_start_base + (node_id // arch.hbm_node_aliase) * (arch.hbm_node_addr_space * arch.hbm_node_aliase) + aliase_offset * (node_id % arch.hbm_node_aliase)
            node_size = arch.hbm_node_addr_space * arch.hbm_node_aliase
            data_noc.o_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, x=arch.num_cluster_x+1, y=node_id+1)
            self.bind(itf_router, 'output', hbm_ctrl_list_east[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[2]):
            ctrl_id = chan_id // ctrl_chan_east
            self.bind(hbm_ctrl_list_east[ctrl_id], f'out_{chan_id % ctrl_chan_east}', hbm_chan_list_east[chan_id], 'input')
            pass
        hbm_edge_start_base += arch.num_cluster_y*arch.hbm_node_addr_space

        ## south
        for node_id in range(arch.num_cluster_x if ctrl_chan_south else 0):
            ctrl_id = node_id // arch.num_node_per_ctrl
            itf_router = router.Router(self, f'south_{node_id}')
            itf_router.add_mapping('output')
            aliase_offset = (1 << arch.hbm_node_aliase_start_bit)
            base_addr = hbm_edge_start_base + (node_id // arch.hbm_node_aliase) * (arch.hbm_node_addr_space * arch.hbm_node_aliase) + aliase_offset * (node_id % arch.hbm_node_aliase)
            node_size = arch.hbm_node_addr_space * arch.hbm_node_aliase
            data_noc.o_MAP(itf_router.i_INPUT(), base=base_addr, size=node_size, x=node_id+1, y=0)
            self.bind(itf_router, 'output', hbm_ctrl_list_south[ctrl_id], f'in_{node_id % arch.num_node_per_ctrl}')
            pass
        for chan_id in range(arch.hbm_chan_placement[3]):
            ctrl_id = chan_id // ctrl_chan_south
            self.bind(hbm_ctrl_list_south[ctrl_id], f'out_{chan_id % ctrl_chan_south}', hbm_chan_list_south[chan_id], 'input')
            pass



class FlexClusterBoard(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(FlexClusterBoard, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=1000000000)

        flex_cluster_system = FlexClusterSystem(self, 'chip', parser)

        self.bind(clock, 'out', flex_cluster_system, 'clock')

class Target(gvsoc.runner.Target):

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=FlexClusterBoard, description="Flex Cluster virtual board")

        # Legacy SoftHier verbose runs need VP_TRACE_ACTIVE but trip the debug
        # LSU memcheck path on split misaligned scalar loads. The runner turns
        # any --trace into debug-mode, so keep debug-mode semantics but launch
        # the profile binary for this target.
        self.model.add_properties({
            "gvsoc": {
                "launchers": {
                    "debug": "gvsoc_launcher_profile"
                }
            }
        })
        self.runner.full_config.set(
            "target/gvsoc/launchers/debug",
            "gvsoc_launcher_profile"
        )
        self.runner.full_config.set("target/gvsoc/debug-mode", False)
        self.runner.full_config.set("target/gvsoc/profile-mode", True)
