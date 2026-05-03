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
from pulp.chips.softhier.error_detector import ErrorDetector
# from pulp.floonoc.floonoc import FlooNocClusterGridNarrowWide
from pulp.floonoc_flex.floonoc_flex import FlooNocFlex
import math

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

        #############
        # Assertion #
        #############
        assert(arch.topology == '2DMesh', f'NoC Topology currectly only support 2DMesh')
        assert(arch.num_cluster_x * arch.num_cluster_y == arch.num_cluster, f"Topology dimesion not match total number of clusters")

        ##############
        # Components #
        ##############

        #Clusters
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

        #Virtual router, just for debugging and non-performance-critical jobs
        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)

        #Debug Memory
        error_detector = ErrorDetector(self,'error_detector')

        #Control register
        softhier_ctrl = SoftHierCtrl(self, 'softhier_ctrl', num_cluster=arch.num_cluster, num_core_per_cluster=arch.num_core_per_cluster)

        # --- FlooNoC Flex Initialization & Topology Building ---
        router_degrees = 5
        
        # Calculate Dimensions
        dim_x = arch.num_cluster_x + 2
        dim_y = arch.num_cluster_y + 2
        
        # Define Mesh size and number of nodes
        MESH_SIZE = dim_x * dim_y
        nb_nodes = MESH_SIZE * 2

        noc = FlooNocFlex(self, 'noc',  
                narrow_width=8,    
                wide_width=arch.noc_link_width,
                router_degrees=router_degrees,
                nb_nodes=nb_nodes,
                router_input_queue_size=16,
                ni_outstanding_reqs=arch.noc_outstanding)

        # Get router ID for 2D mesh
        def get_router_id(x, y):
            return y * dim_x + x
        
        # Offset by MESH_SIZE to separate from routers
        def get_ni_id(x, y):
            return MESH_SIZE + (y * dim_x + x)

        routers_map = {} 
        nis_map = {}     

        # Add routers at cluster centers
        for y in range(1, arch.num_cluster_y + 1):
            for x in range(1, arch.num_cluster_x + 1):
                r_id = get_router_id(x, y)
                routers_map[(x, y)] = r_id
                noc.add_router(r_id, num_queues=5) 

        '''
        # Add network interfaces everywhere except the 4 corners
        for y in range(dim_y):
            for x in range(dim_x):
                if (x == 0 and y == 0) or (x == 0 and y == dim_y - 1) or \
                   (x == dim_x - 1 and y == 0) or (x == dim_x - 1 and y == dim_y - 1):
                    continue
                ni_id = get_ni_id(x, y)
                nis_map[(x, y)] = ni_id
                noc.add_network_interface(ni_id)
        '''

        # Add ACTIVE NIs (no halo)
        for y in range(1, arch.num_cluster_y + 1):
            for x in range(1, arch.num_cluster_x + 1):
                ni_id = get_ni_id(x, y)
                nis_map[(x, y)] = ni_id
                noc.add_network_interface(ni_id)
        
                # Link directly to the co-located router
                r_id = routers_map[(x, y)]
                noc.add_link(ni_id, r_id, latency=1)

        '''
        # Add links (NI <-> Nearest Router)
        for (nx, ny), ni_id in nis_map.items():
            rx = max(1, min(nx, arch.num_cluster_x))
            ry = max(1, min(ny, arch.num_cluster_y))
            r_id = routers_map[(rx, ry)]
            # Only add the link once; C++ treats it as bidirectional
            noc.add_link(ni_id, r_id, latency=1)
        '''

        # Add links (Router <-> Router Mesh Network)
        for y in range(1, arch.num_cluster_y + 1):
            for x in range(1, arch.num_cluster_x + 1):
                r_id = routers_map[(x, y)]
                # Link East
                if x < arch.num_cluster_x:
                    east_id = routers_map[(x + 1, y)]
                    noc.add_link(r_id, east_id, latency=1)
                # Link South
                if y < arch.num_cluster_y:
                    south_id = routers_map[(x, y + 1)]
                    noc.add_link(r_id, south_id, latency=1)

        # Generate routing tables
        noc.generate_routing_tables_mesh_2d(dim_x=dim_x, dim_y=dim_y)

        ############
        # Bindings #
        ############

        # Debug memory
        virtual_interco.o_MAP(error_detector.i_INPUT())

        # Control register
        virtual_interco.o_MAP(softhier_ctrl.i_INPUT(), base=arch.soc_register_base, size=arch.soc_register_size, rm_base=True)

        # Clusters
        for cluster_id in range(arch.num_cluster):
            x_id = int(cluster_id % arch.num_cluster_x)
            y_id = int(cluster_id / arch.num_cluster_x)
            
            # Retrieve the UNIQUE Network Interface ID for this cluster
            ni_node_id = nis_map[(x_id + 1, y_id + 1)]
            
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
