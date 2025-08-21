#
# Copyright (C) 2025 ETH Zurich and University of Bologna
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
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.systree
from memory.memory import Memory
from interco.router import Router
from interco.converter import Converter
from interco.interleaver import Interleaver
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
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
    nb_banks_per_tile: int
        Number of TCDM banks
    bandwidth: int
        Global bandwidth, in bytes per cycle, applied to all incoming request. This impacts the
        end time of the burst.

    """

    def __init__(self, parent: gvsoc.systree.Component, name: str, tile_id: int=0, group_id: int=0,
                 nb_tiles_per_group: int=16, nb_groups: int=4, nb_remote_local_masters: int=1, nb_remote_group_masters: int=4,
                 nb_pe: int=0, size: int=0, nb_banks_per_tile: int=0, bandwidth: int=0):
        super(L1_subsystem, self).__init__(parent, name)

        #
        # Properties
        #

        self.add_property('nb_pe', nb_pe)
        self.add_property('size', size)
        self.add_property('nb_banks_per_tile', nb_banks_per_tile)
        self.add_property('bandwidth', bandwidth)
        self.add_property('tile_id', tile_id)
        self.add_property('group_id', group_id)

        assert nb_remote_local_masters == 1, "Only one remote local master is supported in the L1 subsystem"
        assert nb_remote_group_masters == 2, "Only two remote group masters are supported in the L1 subsystem"
        l1_bank_size = size / nb_banks_per_tile
        nb_masters = nb_pe + nb_remote_local_masters + nb_remote_group_masters
        nb_remote_masters = nb_remote_local_masters + nb_remote_group_masters
        total_banks = nb_groups * nb_tiles_per_group * nb_banks_per_tile
        global_tile_id = tile_id + group_id * nb_tiles_per_group
        start_bank_id = global_tile_id * nb_banks_per_tile
        end_bank_id = start_bank_id + nb_banks_per_tile

        #
        # Components
        #

        # TCDM L1-Memory banks
        l1_banks = []
        for i in range(0, nb_banks_per_tile):
            tcdm = Memory(self, 'tcdm_bank%d' % i, size=l1_bank_size, width_log2=int(math.log(bandwidth, 2.0)),
                            latency=1, atomics=True)
            l1_banks.append(tcdm)

        # L1 interleaver (virtual)
        local_interleavers = []
        for i in range(0, nb_pe):
            local_interleavers.append(Interleaver(self, f'local_interleaver{i}', nb_slaves=total_banks, nb_masters=1, 
                                             interleaving_bits=int(math.log2(bandwidth)), offset_translation=False))

        remote_interleaver = Interleaver(self, 'remote_interleaver', nb_slaves=total_banks, nb_masters=nb_remote_masters, 
                                    interleaving_bits=int(math.log2(bandwidth)), offset_translation=False)
        
        #Remote interfaces
        remote_local_out_interfaces = []
        remote_local_in_interfaces = []
        for i in range(0, nb_remote_local_masters):
            remote_local_out_interfaces.append(Router(self, f'remote_local_out_itf{i}', bandwidth=bandwidth, latency=2))
            remote_local_out_interfaces[i].add_mapping('output')
            remote_local_in_interfaces.append(Router(self, f'remote_local_in_itf{i}', bandwidth=bandwidth, latency=0))
            remote_local_in_interfaces[i].add_mapping('output')

        remote_group_out_interfaces = []
        remote_group_in_interfaces = []
        for i in range(0, nb_remote_group_masters):
            remote_group_out_interfaces.append(Router(self, f'remote_group_out_itf{i}', bandwidth=bandwidth, latency=2))
            remote_group_out_interfaces[i].add_mapping('output')
            remote_group_in_interfaces.append(Router(self, f'remote_group_in_itf{i}', bandwidth=bandwidth, latency=0))
            remote_group_in_interfaces[i].add_mapping('output')

        #
        # Bindings
        #

        #Core input
        for i in range(0, nb_pe):
            self.bind(self, f'pe_in{i}', local_interleavers[i], 'in_0')

        #Remote input
        for i in range(0, nb_remote_local_masters):
            self.bind(self, f'remote_local_in{i}', remote_local_in_interfaces[i], 'input')
            self.bind(remote_local_in_interfaces[i], 'output', remote_interleaver, 'in_%d' % i)

        for i in range(0, nb_remote_group_masters):
            self.bind(self, f'remote_group_in{i}', remote_group_in_interfaces[i], 'input')
            self.bind(remote_group_in_interfaces[i], 'output', remote_interleaver, 'in_%d' % (i + nb_remote_local_masters))

        #Remote output
        for i in range(0, nb_remote_local_masters):
            self.bind(remote_local_out_interfaces[i], 'output', self, f'remote_local_out{i}')

        for i in range(0, nb_remote_group_masters):
            self.bind(remote_group_out_interfaces[i], 'output', self, f'remote_group_out{i}')

        #
        # Address sorting
        #

        #virtual interleaver -> bank + remote_interfaces
        for i in range(0, total_banks):
            tgt_grp_id = int(i / (nb_tiles_per_group * nb_banks_per_tile))
            if (i >= start_bank_id and i < end_bank_id):
                remove_offset = Interleaver(self, f'remove_offset_{i}', nb_slaves=1, nb_masters=nb_pe+1, interleaving_bits=2, enable_shift=(total_banks - 1).bit_length(), offset_translation=False)
                for j, local_interleaver in enumerate(local_interleavers):
                    self.bind(local_interleaver, 'out_%d' % i, remove_offset, 'in_%d' % j)
                self.bind(remote_interleaver, 'out_%d' % i, remove_offset, 'in_%d' % nb_pe)
                self.bind(remove_offset, 'out_0', l1_banks[i - start_bank_id], 'input')
            elif tgt_grp_id == group_id:
                for local_interleaver in local_interleavers:
                    self.bind(local_interleaver, 'out_%d' % i, remote_local_out_interfaces[0], 'input')
            else:
                for j, local_interleaver in enumerate(local_interleavers):
                    if j % 2 == 0:
                        self.bind(local_interleaver, 'out_%d' % i, remote_group_out_interfaces[0], 'input')
                    else:
                        self.bind(local_interleaver, 'out_%d' % i, remote_group_out_interfaces[1], 'input')

    def i_DMA_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'dma_input', signature='io')

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