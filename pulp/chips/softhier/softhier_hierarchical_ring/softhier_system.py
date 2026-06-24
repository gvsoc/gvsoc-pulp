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
from pulp.chips.softhier.softhier_hierarchical_ring.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.softhier.softhier_hierarchical_ring.softhier_ctrl import SoftHierCtrl
from pulp.chips.softhier.softhier_hierarchical_ring.softhier_arch import SoftHierArch
from pulp.chips.softhier.softhier_hierarchical_ring.error_detector import ErrorDetector
from pulp.floonoc_flex.floonoc_flex import FlooNocFlex

class SoftHierSystem(gvsoc.systree.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        #################
        # Configuration #
        #################

        arch = SoftHierArch()

        # Get Binary
        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        ##############
        # Components #
        ##############

        # Clusters
        cluster_list=[]
        for cluster_id in range(arch.num_cluster):
            cluster_arch = ClusterArch( num_core 		    = arch.num_core_per_cluster,
            							cluster_id 			= cluster_id,
        								spatz_num_lane		= arch.spatz_num_lane,
        								spatz_lane_width	= arch.spatz_lane_width,
                                        tcdm_bank_nb        = arch.cluster_tcdm_bank_nb,
                                        tcdm_bank_width     = arch.cluster_tcdm_bank_width,
        								inst_base 			= arch.instruction_mem_base,
        								inst_size 			= arch.instruction_mem_size,
        								tcdm_base 			= arch.cluster_tcdm_base,
        								tcdm_size 			= arch.cluster_tcdm_size,
        								stack_base 			= arch.cluster_stack_base,
        								stack_size 			= arch.cluster_stack_size,
        								zomem_base 			= arch.cluster_zomem_base,
        								zomem_size 			= arch.cluster_zomem_size,
        								reg_base 			= arch.cluster_reg_base,
        								reg_size 			= arch.cluster_reg_size,
        								idma_outstand_txn 	= arch.idma_outstand_txn,
        								idma_outstand_burst = arch.idma_outstand_burst)
            cluster_list.append(ClusterUnit(self,f'cluster_{cluster_id}', cluster_arch, binary))
            pass

        # Virtual router, just for debugging and non-performance-critical jobs
        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)

        # Debug Memory
        error_detector = ErrorDetector(self,'error_detector')

        # Control register
        softhier_ctrl = SoftHierCtrl(self, 'softhier_ctrl', num_cluster=arch.num_cluster, num_core_per_cluster=arch.num_core_per_cluster)

        # --- FlooNoC Flex Initialization & Topology Building ---
        
        N = arch.num_cluster
        G = arch.num_global_clusters
        L = arch.num_local_clusters
        
        nb_nodes = (N * 2) + G

        router_degrees = 4

        noc = FlooNocFlex(self, 'noc',      
                wide_width=arch.noc_link_width,
                narrow_width=8,
                router_degrees=router_degrees,
                nb_nodes=nb_nodes,
                router_input_queue_size=16,
                ni_outstanding_reqs=arch.noc_outstanding)

        routers_map = {} 
        nis_map = {}     
        global_routers_map = {}

        # Instantiate Routers, NIs, and Link them hierarchically
        for g in range(G):
            # Instantiate the Global Router for this Local Ring
            gr_id = N + g
            global_routers_map[g] = gr_id
            noc.add_router(gr_id, num_queues=3)
            
            # Loop through the clusters within this Local Ring
            for l in range(L):
                cluster_id = (g * L) + l
                lr_id = cluster_id
                ni_id = N + G + cluster_id
                
                routers_map[cluster_id] = lr_id
                nis_map[cluster_id] = ni_id
                
                if l == 0:
                    noc.add_router(lr_id, num_queues=4)
                    noc.add_link(lr_id, gr_id, latency=1)
                else:
                    noc.add_router(lr_id, num_queues=3)
                
                noc.add_network_interface(ni_id)
                
                # Link NI to its Local Router
                noc.add_link(ni_id, lr_id, latency=1)
                
                # Link Local Ring (Wrap around within the group)
                next_l = (l + 1) % L
                next_lr_id = (g * L) + next_l
                noc.add_link(lr_id, next_lr_id, latency=1)

            # Link Global Ring (Wrap around between Global Routers)
            next_g = (g + 1) % G
            next_gr_id = N + next_g
            noc.add_link(gr_id, next_gr_id, latency=1)

        # Generate routing tables
        noc.generate_routing_tables_shortest_path()


        ############
        # Bindings #
        ############

        # Debug memory
        virtual_interco.o_MAP(error_detector.i_INPUT())

        # Control register
        virtual_interco.o_MAP(softhier_ctrl.i_INPUT(), base=arch.soc_register_base, size=arch.soc_register_size, rm_base=True)

        # Clusters
        for cluster_id in range(arch.num_cluster):
            ni_node_id = nis_map[cluster_id]
            
            narrow_arbiter = router.Router(self, f'narrow_arbiter_{cluster_id}', bandwidth=8)
            narrow_arbiter.o_MAP(virtual_interco.i_INPUT())
            narrow_arbiter.o_MAP(noc.i_NARROW_INPUT(ni_node_id),
                                 base=arch.cluster_tcdm_remote,
                                 size=arch.num_cluster * arch.cluster_tcdm_size,
                                 rm_base=False)
            
            wide_arbiter = router.Router(self, f'wide_arbiter_{cluster_id}', bandwidth=arch.noc_link_width)
            wide_arbiter.o_MAP(virtual_interco.i_INPUT())
            wide_arbiter.o_MAP(noc.i_WIDE_INPUT(ni_node_id),
                                 base=arch.cluster_tcdm_remote,
                                 size=arch.num_cluster * arch.cluster_tcdm_size,
                                 rm_base=False)
                                 
            cluster_list[cluster_id].o_NARROW_SOC(narrow_arbiter.i_INPUT())
            cluster_list[cluster_id].o_WIDE_SOC(wide_arbiter.i_INPUT())
            
            noc.o_NARROW_MAP(cluster_list[cluster_id].i_NARROW_INPUT(),
                           base=arch.cluster_tcdm_remote  + cluster_id * arch.cluster_tcdm_size,
                           size=arch.cluster_tcdm_size,
                           node_id=ni_node_id,
                           rm_base=True)
                           
            wide_base = arch.cluster_tcdm_remote + cluster_id * arch.cluster_tcdm_size
            wide_name = cluster_list[cluster_id].i_WIDE_INPUT().component.name
            
            noc.get_property('mappings')[f"wide_{wide_name}"] = {
                'base': wide_base, 'size': arch.cluster_tcdm_size, 'node_id': ni_node_id, 'remove_offset': wide_base
            }
            noc.o_WIDE_BIND(cluster_list[cluster_id].i_WIDE_INPUT(), ni_node_id)


class SoftHierPlatform(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(SoftHierPlatform, self).__init__(parent, name, options=options)

        arch  = SoftHierArch()
        clock = Clock_domain(self, 'clock', frequency=(1000000000 if not hasattr(arch, 'frequence') else arch.frequence))

        softhier_system = SoftHierSystem(self, 'system', parser)

        self.bind(clock, 'out', softhier_system, 'clock')