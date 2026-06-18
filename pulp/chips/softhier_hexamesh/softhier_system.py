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
from pulp.chips.softhier_hexamesh.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.softhier_hexamesh.softhier_ctrl import SoftHierCtrl
from pulp.chips.softhier_hexamesh.softhier_arch import SoftHierArch
from pulp.chips.softhier_hexamesh.error_detector import ErrorDetector
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
        assert(arch.topology == 'HexaMesh', f'NoC topology should be HexaMesh')
        assert(1 + 3 * arch.num_rings * (arch.num_rings + 1) == arch.num_cluster, f"Topology dimensions are mismatched")

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

        # --- FlooNoC Flex Initialization & HexaMesh Topology Building ---
        
        # A HexaMesh router has up to 6 neighbors + 1 NI connection
        router_degrees = 7
        nb_nodes = arch.num_cluster * 2 # N Routers + N NIs

        noc = FlooNocFlex(self, 'noc',  
                narrow_width=8,    
                wide_width=arch.noc_link_width,
                router_degrees=router_degrees,
                nb_nodes=nb_nodes,
                router_input_queue_size=16,
                ni_outstanding_reqs=arch.noc_outstanding)

        # --- Coordinate Generation (Axial Coordinates) ---
        ring_walk_dirs = [(-1, 1), (-1, 0), (0, -1), (1, -1), (1, 0), (0, 1)]
        
        coords = [(0, 0)]
        ring = 1
        while len(coords) < arch.num_cluster:
            q, r = ring, 0 
            for dq, dr in ring_walk_dirs:
                for _ in range(ring):
                    if len(coords) < arch.num_cluster:
                        coords.append((q, r))
                    q += dq
                    r += dr
            ring += 1

        coord_to_id = {coord: idx for idx, coord in enumerate(coords)}
        routers_map = {} 
        nis_map = {}     

        # Instantiate Routers and NIs
        for cluster_id, (q, r) in enumerate(coords):
            r_id = cluster_id
            ni_id = arch.num_cluster + cluster_id
            
            routers_map[cluster_id] = r_id
            nis_map[cluster_id] = ni_id

            noc.add_router(r_id, num_queues=router_degrees) 
            noc.add_network_interface(ni_id)
        
        # Axis 1: East (1, 0) / West (-1, 0) priority
        coords_q_desc = sorted(coords, key=lambda c: c[0], reverse=True)
        for q, r in coords_q_desc:
            east_neighbor = (q + 1, r)
            if east_neighbor in coord_to_id:
                r_id = routers_map[coord_to_id[(q, r)]]
                east_id = routers_map[coord_to_id[east_neighbor]]
                noc.add_link(r_id, east_id, latency=1)

        # Axis 2: SouthEast (0, 1) / NorthWest (0, -1) priority
        coords_r_desc = sorted(coords, key=lambda c: c[1], reverse=True)
        for q, r in coords_r_desc:
            se_neighbor = (q, r + 1)
            if se_neighbor in coord_to_id:
                r_id = routers_map[coord_to_id[(q, r)]]
                se_id = routers_map[coord_to_id[se_neighbor]]
                noc.add_link(r_id, se_id, latency=1)

        # Axis 3: SouthWest (-1, 1) / NorthEast (1, -1) priority
        coords_sw_desc = sorted(coords, key=lambda c: -c[0] + c[1], reverse=True)
        for q, r in coords_sw_desc:
            sw_neighbor = (q - 1, r + 1)
            if sw_neighbor in coord_to_id:
                r_id = routers_map[coord_to_id[(q, r)]]
                sw_id = routers_map[coord_to_id[sw_neighbor]]
                noc.add_link(r_id, sw_id, latency=1)

        # Add NI <-> Router links
        for cluster_id, _ in enumerate(coords):
            ni_id = nis_map[cluster_id]
            r_id = routers_map[cluster_id]
            noc.add_link(ni_id, r_id, latency=1)
        
        # Generate routing tables
        # noc.generate_routing_tables_deadlock_free() # is slightly slower
        # noc.generate_routing_tables_hexamesh()
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
