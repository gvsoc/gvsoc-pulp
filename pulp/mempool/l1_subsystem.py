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
# Author: Yichao Zhang (ETH Zurich) (yiczhang@iis.ee.ethz.ch)
#         Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)

import gvsoc.systree
from memory.memory import Memory
from interco.router import Router
from interco.interleaver import Interleaver
from pulp.mempool.xbar.mempool_xbar import MempoolXbar
from pulp.mempool.l1_interconnect.l1_remote_itf import L1_RemoteItf
from pulp.mempool.l1_interconnect.l1_bank_arbiter import L1_BankArbiter
from pulp.mempool.xbar.mempool_xbar_selector import MempoolXbarSelector
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

    def __init__(self, parent: gvsoc.systree.Component, name: str, terapool: bool=False, async_l1_interco: bool=False, tile_id: int=0, sub_group_id: int=0, group_id: int=0,
                 nb_tiles_per_sub_group: int=4, nb_sub_groups_per_group: int=1, nb_groups: int=4, nb_remote_local_masters: int=1, nb_remote_group_masters: int=4,
                 nb_remote_sub_group_masters: int=4, nb_pe: int=0, size: int=0, nb_banks_per_tile: int=0, bandwidth: int=0, axi_data_width: int=512, terapool_group_latency: int=7,
                 tensorpool: bool=False, redmule_bandwidth: int=64):
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
        # Dedicated wide RedMulE remote channel: synchronous-only, built on every tile (symmetric).
        # RedMulE exists if and only if tensorpool is set; there is no RedMulE support otherwise.
        assert not (tensorpool and async_l1_interco), \
            "RedMulE (tensorpool) is only supported on the synchronous L1 interconnect"
        redmule_path = tensorpool
        # Per-bank hold arbiter: cycles a bank is reserved per wide (TE) / narrow (PE) access.
        bank_hold = 10
        bank_hold_pe = 0
        bank_queue_depth = 4
        l1_bank_size = size / nb_banks_per_tile
        nb_masters = nb_pe + nb_remote_local_masters + nb_remote_group_masters + nb_remote_sub_group_masters
        nb_remote_masters = nb_remote_local_masters + nb_remote_group_masters + nb_remote_sub_group_masters
        total_banks = nb_groups * nb_sub_groups_per_group * nb_tiles_per_sub_group * nb_banks_per_tile
        global_tile_id = tile_id + sub_group_id * nb_tiles_per_sub_group + group_id * nb_sub_groups_per_group * nb_tiles_per_sub_group
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
        if async_l1_interco:
            l1_adapters = []
            for i in range(0, nb_banks_per_tile):
                l1_adapters.append(L1_RemoteItf(self, 'tcdm_bank_adapter%d' % i, bandwidth=bandwidth, shared_rw_bandwidth=True, synchronous=False))

        # L1 interleaver (virtual)
        local_interleavers = []
        for i in range(0, nb_pe):
            local_interleavers.append(Interleaver(self, f'local_interleaver{i}', nb_slaves=total_banks, nb_masters=1, 
                                             interleaving_bits=int(math.log2(bandwidth)), offset_translation=False))
        # Dedicated wide RedMulE path: route per *destination tile* (one wide burst/hop) instead of
        # per bank, so the router charges ceil(size/bandwidth) once per hop, not per 4-byte slice.
        if redmule_path:
            total_tiles = total_banks // nb_banks_per_tile
            tile_interleaving_bits = int(math.log2(bandwidth * nb_banks_per_tile))
            redmule_tile_interleaver = Interleaver(self, 'redmule_tile_interleaver', nb_slaves=total_tiles, nb_masters=1,
                                             interleaving_bits=tile_interleaving_bits, offset_translation=False)
            # Self-tile chunk split into this tile's banks (4-byte interleaving).
            redmule_local_interleaver = Interleaver(self, 'redmule_local_interleaver', nb_slaves=nb_banks_per_tile, nb_masters=1,
                                             interleaving_bits=int(math.log2(bandwidth)), offset_translation=False)

        remote_local_interleavers = []
        for i in range(0, nb_remote_local_masters):
            remote_local_interleavers.append(Interleaver(self, f'remote_local_interleaver{i}', nb_slaves=total_banks, nb_masters=1, 
                                             interleaving_bits=int(math.log2(bandwidth)), offset_translation=False))

        remote_group_interleavers = []
        for i in range(0, nb_remote_group_masters):
            remote_group_interleavers.append(Interleaver(self, f'remote_group_interleaver{i}', nb_slaves=total_banks, nb_masters=1, 
                                             interleaving_bits=int(math.log2(bandwidth)), offset_translation=False))

        remote_sub_group_interleavers = []
        for i in range(0, nb_remote_sub_group_masters):
            remote_sub_group_interleavers.append(Interleaver(self, f'remote_sub_group_interleaver{i}', nb_slaves=total_banks, nb_masters=1, 
                                             interleaving_bits=int(math.log2(bandwidth)), offset_translation=False))

        if async_l1_interco:
            # Remote group interleavers (PE cores only: RedMulE is not supported on this path)
            remote_out_interface = MempoolXbar(self, 'remote_out_itf', latency=1, bandwidth=bandwidth, nb_input_port=nb_pe, nb_output_port=nb_remote_masters,
                                        shared_rw_bandwidth=True, max_input_pending_size=4)

            remote_local_output_selectors = []
            for i in range(0, nb_pe):
                pe_selector_list = []
                for j in range(0, nb_remote_local_masters):
                    pe_selector_list.append(MempoolXbarSelector(self, f'remote_local_output_selector_core{i}_out{j}', output_id=j))
                remote_local_output_selectors.append(pe_selector_list)

            remote_sub_group_output_selectors = []
            for i in range(0, nb_pe):
                pe_selector_list = []
                for j in range(0, nb_remote_sub_group_masters):
                    pe_selector_list.append(MempoolXbarSelector(self, f'remote_sub_group_output_selector_core{i}_out{j}', output_id=j + nb_remote_local_masters))
                remote_sub_group_output_selectors.append(pe_selector_list)

            remote_group_output_selectors = []
            for i in range(0, nb_pe):
                pe_selector_list = []
                for j in range(0, nb_remote_group_masters):
                    pe_selector_list.append(MempoolXbarSelector(self, f'remote_group_output_selector_core{i}_out{j}', output_id=j + nb_remote_local_masters + nb_remote_sub_group_masters))
                remote_group_output_selectors.append(pe_selector_list)
        else:
            #change max_input_pending_size from 4 to 5 because addition of redmule?
            remote_local_out_interfaces = []
            for i in range(0, nb_remote_local_masters):
                remote_local_out_interfaces.append(Router(self, f'remote_local_out_itf{i}', bandwidth=bandwidth, latency=1, shared_rw_bandwidth=True, \
                                                synchronous=True, max_input_pending_size=4))
                remote_local_out_interfaces[i].add_mapping('output')

            remote_sub_group_out_interfaces = []
            for i in range(0, nb_remote_sub_group_masters):
                remote_sub_group_out_interfaces.append(Router(self, f'remote_sub_group_out_itf{i}', bandwidth=bandwidth, latency=1, shared_rw_bandwidth=True, \
                                                synchronous=True, max_input_pending_size=4))
                remote_sub_group_out_interfaces[i].add_mapping('output')

            remote_group_out_interfaces = []
            for i in range(0, nb_remote_group_masters):
                remote_group_out_interfaces.append(Router(self, f'remote_group_out_itf{i}', bandwidth=bandwidth, latency=1, shared_rw_bandwidth=True, \
                                                synchronous=True, max_input_pending_size=4))
                remote_group_out_interfaces[i].add_mapping('output')

            # Dedicated wide RedMulE remote out interfaces; PE cores keep the narrow routers above.
            if redmule_path:
                redmule_remote_local_out_interfaces = []
                for i in range(0, nb_remote_local_masters):
                    redmule_remote_local_out_interfaces.append(Router(self, f'redmule_remote_local_out_itf{i}', bandwidth=redmule_bandwidth, latency=1, shared_rw_bandwidth=True, \
                                                    synchronous=True, max_input_pending_size=4))
                    redmule_remote_local_out_interfaces[i].add_mapping('output')

                redmule_remote_sub_group_out_interfaces = []
                for i in range(0, nb_remote_sub_group_masters):
                    redmule_remote_sub_group_out_interfaces.append(Router(self, f'redmule_remote_sub_group_out_itf{i}', bandwidth=redmule_bandwidth, latency=1, shared_rw_bandwidth=True, \
                                                    synchronous=True, max_input_pending_size=4))
                    redmule_remote_sub_group_out_interfaces[i].add_mapping('output')

                redmule_remote_group_out_interfaces = []
                for i in range(0, nb_remote_group_masters):
                    redmule_remote_group_out_interfaces.append(Router(self, f'redmule_remote_group_out_itf{i}', bandwidth=redmule_bandwidth, latency=1, shared_rw_bandwidth=True, \
                                                    synchronous=True, max_input_pending_size=4))
                    redmule_remote_group_out_interfaces[i].add_mapping('output')

        #Remote interfaces
        remote_local_in_interfaces = []
        for i in range(0, nb_remote_local_masters):
            remote_local_in_interfaces.append(L1_RemoteItf(self, f'remote_local_in_itf{i}', bandwidth=bandwidth, resp_latency=1, synchronous=not async_l1_interco))

        remote_sub_group_in_interfaces = []
        for i in range(0, nb_remote_sub_group_masters):
            remote_sub_group_in_interfaces.append(L1_RemoteItf(self, f'remote_sub_group_in_itf{i}', bandwidth=bandwidth, resp_latency=2, synchronous=not async_l1_interco))

        if terapool:
            if terapool_group_latency == 9:
                remote_group_resp_latency = 4
            else:
                remote_group_resp_latency = 3
        elif (nb_sub_groups_per_group * nb_tiles_per_sub_group) > 1:
            remote_group_resp_latency = 2
        else:
            remote_group_resp_latency = 1
        remote_group_in_interfaces = []
        for i in range(0, nb_remote_group_masters):
            remote_group_in_interfaces.append(L1_RemoteItf(self, f'remote_group_in_itf{i}', bandwidth=bandwidth, \
                                                resp_latency=remote_group_resp_latency, synchronous=not async_l1_interco))

        # Dedicated wide RedMulE remote in interfaces (built on every tile so any can be a destination).
        if redmule_path:
            redmule_remote_local_in_interfaces = []
            for i in range(0, nb_remote_local_masters):
                redmule_remote_local_in_interfaces.append(L1_RemoteItf(self, f'redmule_remote_local_in_itf{i}', bandwidth=redmule_bandwidth, resp_latency=1, synchronous=True))

            redmule_remote_sub_group_in_interfaces = []
            for i in range(0, nb_remote_sub_group_masters):
                redmule_remote_sub_group_in_interfaces.append(L1_RemoteItf(self, f'redmule_remote_sub_group_in_itf{i}', bandwidth=redmule_bandwidth, resp_latency=2, synchronous=True))

            redmule_remote_group_in_interfaces = []
            for i in range(0, nb_remote_group_masters):
                redmule_remote_group_in_interfaces.append(L1_RemoteItf(self, f'redmule_remote_group_in_itf{i}', bandwidth=redmule_bandwidth, \
                                                    resp_latency=remote_group_resp_latency, synchronous=True))

            # Fans remote RedMulE ingress out to banks at 4-byte granularity (NOT redmule_bandwidth,
            # so bank decoding is unchanged); contention is modelled per-bank below (l1_bank_arbiter).
            redmule_recv_interleaver = Interleaver(self, 'redmule_recv_interleaver', nb_slaves=total_banks, nb_masters=1, \
                                             interleaving_bits=int(math.log2(bandwidth)), offset_translation=False)

        # DMA Interface
        dma_interface = Router(self, 'dma_itf', bandwidth=axi_data_width, latency=0, shared_rw_bandwidth=True)
        dma_interface.add_mapping('output')

        # DMA Interleaver
        dma_interleaver = Interleaver(self, 'dma_interleaver', nb_slaves=nb_banks_per_tile, nb_masters=1, interleaving_bits=2, enable_shift=(total_banks - 1).bit_length(), offset_translation=False)

        #
        # Bindings
        #

        if async_l1_interco:
            for i in range(0, nb_banks_per_tile):
                self.bind(l1_adapters[i], 'output', l1_banks[i], 'input')

        #Core input
        for i in range(0, nb_pe):
            self.bind(self, f'pe_in{i}', local_interleavers[i], 'in_0')

        #Redmule input
        if redmule_path:
            # Per-destination-tile routing (wide bursts) for the dedicated RedMulE channel.
            self.bind(self, f'RedMulE_input', redmule_tile_interleaver, f'input')

        #Remote input
        for i in range(0, nb_remote_local_masters):
            self.bind(self, f'remote_local_in{i}', remote_local_in_interfaces[i], 'input')
            self.bind(remote_local_in_interfaces[i], 'output', remote_local_interleavers[i], 'in_0')

        for i in range(0, nb_remote_sub_group_masters):
            self.bind(self, f'remote_sub_group_in{i}', remote_sub_group_in_interfaces[i], 'input')
            self.bind(remote_sub_group_in_interfaces[i], 'output', remote_sub_group_interleavers[i], 'in_0')

        for i in range(0, nb_remote_group_masters):
            self.bind(self, f'remote_group_in{i}', remote_group_in_interfaces[i], 'input')
            self.bind(remote_group_in_interfaces[i], 'output', remote_group_interleavers[i], 'in_0')

        #Dedicated RedMulE remote input: all ingress -> shared interleaver -> per-bank arbiter -> banks
        if redmule_path:
            for i in range(0, nb_remote_local_masters):
                self.bind(self, f'redmule_remote_local_in{i}', redmule_remote_local_in_interfaces[i], 'input')
                self.bind(redmule_remote_local_in_interfaces[i], 'output', redmule_recv_interleaver, 'in_0')
            for i in range(0, nb_remote_sub_group_masters):
                self.bind(self, f'redmule_remote_sub_group_in{i}', redmule_remote_sub_group_in_interfaces[i], 'input')
                self.bind(redmule_remote_sub_group_in_interfaces[i], 'output', redmule_recv_interleaver, 'in_0')
            for i in range(0, nb_remote_group_masters):
                self.bind(self, f'redmule_remote_group_in{i}', redmule_remote_group_in_interfaces[i], 'input')
                self.bind(redmule_remote_group_in_interfaces[i], 'output', redmule_recv_interleaver, 'in_0')

        #Remote output
        if async_l1_interco:
            for i in range(0, nb_remote_local_masters):
                for j in range(0, nb_pe):
                    self.bind(remote_local_output_selectors[j][i], 'output', remote_out_interface, 'input' if j == 0 else f'input_{j}')
                self.bind(remote_out_interface, 'output', self, f'remote_local_out{i}') # only one local port, so no index offset
            for i in range(0, nb_remote_sub_group_masters):
                for j in range(0, nb_pe):
                    self.bind(remote_sub_group_output_selectors[j][i], 'output', remote_out_interface, 'input' if j == 0 else f'input_{j}')
                self.bind(remote_out_interface, f'output_{i + nb_remote_local_masters}', self, f'remote_sub_group_out{i}')
            for i in range(0, nb_remote_group_masters):
                for j in range(0, nb_pe):
                    self.bind(remote_group_output_selectors[j][i], 'output', remote_out_interface, 'input' if j == 0 else f'input_{j}')
                self.bind(remote_out_interface, f'output_{i + nb_remote_local_masters + nb_remote_sub_group_masters}', self, f'remote_group_out{i}')
        else:
            for i in range(0, nb_remote_local_masters):
                self.bind(remote_local_out_interfaces[i], 'output', self, f'remote_local_out{i}')

            for i in range(0, nb_remote_sub_group_masters):
                self.bind(remote_sub_group_out_interfaces[i], 'output', self, f'remote_sub_group_out{i}')

            for i in range(0, nb_remote_group_masters):
                self.bind(remote_group_out_interfaces[i], 'output', self, f'remote_group_out{i}')

            if redmule_path:
                for i in range(0, nb_remote_local_masters):
                    self.bind(redmule_remote_local_out_interfaces[i], 'output', self, f'redmule_remote_local_out{i}')
                for i in range(0, nb_remote_sub_group_masters):
                    self.bind(redmule_remote_sub_group_out_interfaces[i], 'output', self, f'redmule_remote_sub_group_out{i}')
                for i in range(0, nb_remote_group_masters):
                    self.bind(redmule_remote_group_out_interfaces[i], 'output', self, f'redmule_remote_group_out{i}')

        self.bind(self, 'dma', dma_interface, 'input')
        self.bind(dma_interface, 'output', dma_interleaver, 'in_0')

        for i in range(0, nb_banks_per_tile):
            self.bind(dma_interleaver, f'out_{i}', l1_banks[i], 'input')

        #
        # Address sorting
        #

        #virtual interleaver -> bank + remote_interfaces
        for i in range(0, total_banks):
            tgt_grp_id = int(i / (nb_sub_groups_per_group * nb_tiles_per_sub_group * nb_banks_per_tile))
            tgt_sg_id = int((i % (nb_sub_groups_per_group * nb_tiles_per_sub_group * nb_banks_per_tile)) / (nb_tiles_per_sub_group * nb_banks_per_tile))
            if (i >= start_bank_id and i < end_bank_id):
                remove_offset = Interleaver(self, f'remove_offset_{i}', nb_slaves=1, nb_masters=1, interleaving_bits=2, enable_shift=(total_banks - 1).bit_length(), offset_translation=False)
                # Requester-aware per-bank hold arbiter (redmule_path only): PE/remote-PE use the
                # 'input_pe' port (hold_pe cycles), wide RedMulE reads use 'input' (hold cycles), all
                # on one shared timeline. Off redmule_path the PE feeds bind straight to remove_offset.
                if redmule_path:
                    bank_arb = L1_BankArbiter(self, f'bank_arbiter{i}', hold=bank_hold, hold_pe=bank_hold_pe, queue_depth=bank_queue_depth)
                    pe_dst, pe_port = bank_arb, 'input_pe'
                else:
                    pe_dst, pe_port = remove_offset, 'in_0'
                for local_interleaver in local_interleavers:
                    self.bind(local_interleaver, 'out_%d' % i, pe_dst, pe_port)
                for remote_local_interleaver in remote_local_interleavers:
                    self.bind(remote_local_interleaver, 'out_%d' % i, pe_dst, pe_port)
                for remote_sub_group_interleaver in remote_sub_group_interleavers:
                    self.bind(remote_sub_group_interleaver, 'out_%d' % i, pe_dst, pe_port)
                for remote_group_interleaver in remote_group_interleavers:
                    self.bind(remote_group_interleaver, 'out_%d' % i, pe_dst, pe_port)
                if redmule_path:
                    # Wide slices (self-tile chunk + remote-receive ingress) -> wide port.
                    self.bind(redmule_local_interleaver, 'out_%d' % (i - start_bank_id), bank_arb, 'input')
                    self.bind(redmule_recv_interleaver, 'out_%d' % i, bank_arb, 'input')
                    self.bind(bank_arb, 'output', remove_offset, 'in_0')
                if async_l1_interco:
                    bank_dst = l1_adapters[i - start_bank_id]
                else:
                    bank_dst = l1_banks[i - start_bank_id]
                self.bind(remove_offset, 'out_0', bank_dst, 'input')
            elif tgt_grp_id == group_id:
                if tgt_sg_id == sub_group_id:
                    # RedMulE remote traffic is routed per destination tile by the loop below.
                    for j, local_interleaver in enumerate(local_interleavers):
                        self.bind(local_interleaver, 'out_%d' % i, remote_local_output_selectors[j][0] if async_l1_interco else remote_local_out_interfaces[0], 'input')
                else:
                    for j, local_interleaver in enumerate(local_interleavers):
                        self.bind(local_interleaver, 'out_%d' % i, remote_sub_group_output_selectors[j][(tgt_sg_id ^ sub_group_id) - 1] if async_l1_interco else remote_sub_group_out_interfaces[(tgt_sg_id ^ sub_group_id) - 1], 'input')
            else:
                for j, local_interleaver in enumerate(local_interleavers):
                    self.bind(local_interleaver, 'out_%d' % i, remote_group_output_selectors[j][(tgt_grp_id ^ group_id) - 1] if async_l1_interco else remote_group_out_interfaces[(tgt_grp_id ^ group_id) - 1], 'input')

        # Dedicated RedMulE remote routing: one wide burst per destination tile. Same local/sub-group/
        # group class selection as the per-bank decode; the self tile is bank-split locally.
        if redmule_path:
            tiles_per_group = nb_sub_groups_per_group * nb_tiles_per_sub_group
            for t in range(0, total_tiles):
                if t == global_tile_id:
                    self.bind(redmule_tile_interleaver, 'out_%d' % t, redmule_local_interleaver, 'in_0')
                    continue
                tgt_grp_id = t // tiles_per_group
                tgt_sg_id = (t % tiles_per_group) // nb_tiles_per_sub_group
                if tgt_grp_id == group_id:
                    if tgt_sg_id == sub_group_id:
                        self.bind(redmule_tile_interleaver, 'out_%d' % t, redmule_remote_local_out_interfaces[0], 'input')
                    else:
                        self.bind(redmule_tile_interleaver, 'out_%d' % t, redmule_remote_sub_group_out_interfaces[(tgt_sg_id ^ sub_group_id) - 1], 'input')
                else:
                    self.bind(redmule_tile_interleaver, 'out_%d' % t, redmule_remote_group_out_interfaces[(tgt_grp_id ^ group_id) - 1], 'input')
                    

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