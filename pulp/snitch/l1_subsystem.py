#
# Copyright (C) 2020 GreenWaves Technologies
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

import gvsoc.systree
from memory.memory import Memory
from interco.router import Router
from interco.converter import Converter
from interco.interleaver_snitch import Interleaver
import math


class L1_subsystem(gvsoc.systree.Component):
    """
    Cluster L1 subsystem (memory banks + interconnects)

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    cluster: Cluster
        The cluster class.
    nb_pe : int
        The number of processing elements sharing the subsystem.
    size: int
        The size of the memory in bytes.
    l1_banking_factor : int
        Number of memory banks per processing element. 
    nb_port: int
        Number of TCDM ports per PE.   
    interleaving_bits : int
        Number of interleaving bits, i.e. the size in bits of the accesseses dispatched
        to banks.    
    bandwidth: int
        Global bandwidth, in bytes per cycle, applied to all incoming request. This impacts the
        end time of the burst.
        
    """
        
    def __init__(self, parent: gvsoc.systree.Component, name: str, cluster, nb_pe: int=0, size: int=0, 
                 l1_banking_factor: int=0, nb_port: int=0, bandwidth: int=0, interleaving_bits: int=0):
        super(L1_subsystem, self).__init__(parent, name)

        #
        # Properties
        #
        
        self.add_property('nb_pe', nb_pe)
        self.add_property('size', size)
        self.add_property('l1_banking_factor', l1_banking_factor)
        self.add_property('nb_port', nb_port)
        self.add_property('interleaving_bits', interleaving_bits)
        self.add_property('bandwidth', bandwidth)
        
        nb_l1_banks = 1<<int(math.log(nb_pe * l1_banking_factor, 2.0))
        l1_bank_size = int(size / nb_l1_banks)
        l1_interleaver_nb_masters = nb_pe * nb_port + 1 # one for ext2loc 


        #
        # Components
        #

        # TCDM L1-Memory banks
        # Snitch TCDM 4 superbanks and 32 banks, each PE has 4 banks
        # Each bank has size 4kB (depth*width: 512*8), total size 128kB
        l1_banks = []
        for i in range(0, nb_l1_banks):
            tcdm = Memory(self, 'tcdm_bank%d' % i, size=l1_bank_size, width_log2=int(math.log(bandwidth, 2.0)), 
                            atomics=True, core='snitch', mem='tcdm')
            l1_banks.append(tcdm)

        # Per-PE interconnects
        # PE side has 3 ports (NumSsrs) per core
        pe_icos = []
        for i in range(0, nb_pe):
            pe_icos.append(Router(self, 'pe%d_ico_0' % i, bandwidth=8, latency=0))
            pe_icos.append(Router(self, 'pe%d_ico_1' % i, bandwidth=8, latency=0))
            pe_icos.append(Router(self, 'pe%d_ico_2' % i, bandwidth=8, latency=0))

        # L1 interleaver
        # TCDM interconnection, one port per bank, 8 ports per superbank
        interleaver = Interleaver(self, 'interleaver', nb_slaves=nb_l1_banks, nb_masters=l1_interleaver_nb_masters, 
                                    interleaving_bits=interleaving_bits)

        # EXT2LOC
        # The connetction between L1 subsystem and cluster crossbar 
        ext2loc = Converter(self, 'ext2loc', output_width=4, output_align=4)


        #
        # Bindings
        #

        # Per-PE interconnects
        for i in range(0, nb_pe):
            # for j in range(0, nb_port):
                # port_id = int(i * nb_port + j)
                # self.bind(pe_icos[i], 'l1', interleaver, 'in_%d' % port_id)
            port_id = int(i * nb_port)
            
            
            # Memory port 0, shared by Integer LSU, FP LSU and data mover 0.
            # pe (iss) "data" -> pe interconnect (router) "input"
            self.bind(self, 'data_pe_%d' % i, pe_icos[port_id], 'input')

            # pe interconnect (router) "output" -> tcdm interleaver (interleaver) "in_%d"
            pe_icos[port_id].add_mapping('l1', base=0x10000000, remove_offset=0x10000000, size=0x20000, id=0)
            self.bind(pe_icos[port_id], 'l1', interleaver, 'in_%d' % port_id)

            # connect to cluster axi crossbar
            pe_icos[port_id].add_mapping('cluster_ico', id=1)
            self.bind(pe_icos[port_id], 'cluster_ico', self, 'cluster_ico')
            
        
            # Memory port 1, private for data mover 1
            port_id += 1
            self.bind(self, 'ssr_1_pe_%d' % i, pe_icos[port_id], 'input')
            pe_icos[port_id].add_mapping('l1', base=0x10000000, remove_offset=0x10000000, size=0x20000, id=0)
            self.bind(pe_icos[port_id], 'l1', interleaver, 'in_%d' % port_id)
            
            
            # Memory port 2, private for data mover 2
            port_id += 1
            self.bind(self, 'ssr_2_pe_%d' % i, pe_icos[port_id], 'input')
            pe_icos[port_id].add_mapping('l1', base=0x10000000, remove_offset=0x10000000, size=0x20000, id=0)
            self.bind(pe_icos[port_id], 'l1', interleaver, 'in_%d' % port_id)


        # L1 interleaver
        # tcdm interleaver "out_%d" -> l1_banks (memory) "input"
        for i in range(0, nb_l1_banks):
            self.bind(interleaver, 'out_%d' % i, l1_banks[i], 'input')

        # EXT2LOC
        self.bind(self, 'ext2loc', ext2loc, 'input')
        self.bind(ext2loc, 'out', interleaver, 'in_%d' % int(nb_pe * nb_port))
    
    
    def add_mapping(self, name: str, base: int=None, size: int=None, remove_offset: int=None,
            add_offset: int=None, id: int=None, latency: int=None):
        """Add a target port with an associated target memory map.

        The port is created with the specified name, so that the same name can be used to connect
        the router to the target for this mapping. Any incoming request whose address is inside this
        memory map is forwarded to thsi port.

        On top of the global latency, a latency specific to this mapping can be added when a request
        goes through this mapping.

        Parameters
        ----------
        name: str
            Name of the mapping. An interface of the same name will be created, and so a binding
            with the same name for the master can be created.
        base: int
            Base address of the target memory area.
        size: int
            Size of the target memory area.
        remove_offset: int
            This address is substracted to the address of any request going through this mapping.
            This can be used to convert an address into a local offset.
        id: int
            Counter id where this mapping is reporting statistics. All mappings with same id
            are cumulated together, which is a way to collect statistics fro several mappings.
        latency: int
            Latency applied to any request going through this mapping. This impacts the start time
            of the request.
        """

        mapping = {}

        if base is not None:
            mapping['base'] = base

        if size is not None:
            mapping['size'] = size

        if remove_offset is not None:
            mapping['remove_offset'] = remove_offset

        if add_offset is not None:
            mapping['add_offset'] = add_offset

        if latency is not None:
            mapping['latency'] = latency

        if id is not None:
            mapping['id'] = id

        self.get_property('mappings')[name] =  mapping