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
from interco.interleaver import Interleaver
from pulp.mempool.xbar.mempool_xbar import MempoolXbar
from pulp.mempool.l1_interconnect.l1_remote_itf import L1_RemoteItf
from pulp.mempool.xbar.mempool_xbar_selector import MempoolXbarSelector
import math


class L1_subsystem(gvsoc.systree.Component):
    """
    Tile L1 subsystem (TCDM banks + interconnects).

    Ports
    -----
    local_in_{i}    (i = 0..nb_local_ports-1)
        Local master inputs. Snitch cores and the per-tile HWPE
        interleaver outputs all use this category. Each port goes
        through its own virtual interleaver and can target any bank
        (local or remote).

    remote_in_{i} / remote_out_{i}    (i = 0..nb_remote_ports-1)
        Remote-side I/O. By convention port 0 carries intra-group
        neighbor traffic and ports 1..N-1 carry inter-group NoC
        traffic; the tile is responsible for the actual wiring.

    dma
        DMA AXI port.
    """

    def __init__(self, parent: gvsoc.systree.Component, name: str,
                 tile_id: int = 0, group_id: int = 0,
                 nb_tiles_per_group: int = 16, nb_groups: int = 4,
                 nb_local_ports: int = 0, nb_remote_ports: int = 0,
                 size: int = 0, nb_banks_per_tile: int = 0,
                 bandwidth: int = 0, axi_data_width: int = 64):
        super(L1_subsystem, self).__init__(parent, name)

        assert nb_remote_ports >= 1, "Need at least one remote port (port 0 = intra-group)"
        nb_inter_group = nb_remote_ports - 1

        l1_bank_size      = size / nb_banks_per_tile
        total_banks       = nb_groups * nb_tiles_per_group * nb_banks_per_tile
        global_tile_id    = tile_id + group_id * nb_tiles_per_group
        start_bank_id     = global_tile_id * nb_banks_per_tile
        end_bank_id       = start_bank_id + nb_banks_per_tile
        interleaving_bits = int(math.log2(bandwidth))
        shift_bits        = (total_banks - 1).bit_length()

        #
        # Components
        #

        # TCDM L1 memory banks + per-bank adapters
        l1_banks = []
        l1_adapters = []
        for i in range(nb_banks_per_tile):
            l1_banks.append(Memory(self, f'tcdm_bank{i}', size=l1_bank_size,
                                   width_log2=int(math.log(bandwidth, 2.0)),
                                   latency=1, atomics=True))
            l1_adapters.append(L1_RemoteItf(self, f'tcdm_bank_adapter{i}',
                                            bandwidth=bandwidth, shared_rw_bandwidth=True,
                                            synchronous=False))

        # One virtual interleaver per local master (PE or HWPE sub-port).
        local_interleavers = [
            Interleaver(self, f'local_interleaver{i}', nb_slaves=total_banks, nb_masters=1,
                        interleaving_bits=interleaving_bits, offset_translation=False)
            for i in range(nb_local_ports)
        ]

        # Single virtual interleaver collecting all incoming remote requests.
        remote_interleaver = Interleaver(self, 'remote_interleaver',
                                         nb_slaves=total_banks, nb_masters=nb_remote_ports,
                                         interleaving_bits=interleaving_bits,
                                         offset_translation=False)

        # Outgoing remote: xbar + per-local selectors that pick the output port.
        remote_out_interface = MempoolXbar(self, 'remote_out_itf', latency=1, bandwidth=bandwidth,
                                           nb_input_port=nb_local_ports, nb_output_port=nb_remote_ports,
                                           shared_rw_bandwidth=True, max_input_pending_size=4)
        intra_group_selectors = [
            MempoolXbarSelector(self, f'intra_group_selector_{i}', output_id=0)
            for i in range(nb_local_ports)
        ]
        inter_group_selectors = [
            MempoolXbarSelector(self, f'inter_group_selector_{i}',
                                output_id=(i % nb_inter_group) + 1 if nb_inter_group > 0 else 0)
            for i in range(nb_local_ports)
        ] if nb_inter_group > 0 else []

        # Per-remote-port input adapters.
        remote_in_interfaces = [
            L1_RemoteItf(self, f'remote_in_itf{i}', bandwidth=bandwidth, resp_latency=1, synchronous=False)
            for i in range(nb_remote_ports)
        ]

        # DMA interface + per-tile DMA interleaver.
        dma_interface = Router(self, 'dma_itf', bandwidth=axi_data_width, latency=0,
                               shared_rw_bandwidth=True)
        dma_interface.add_mapping('output')
        dma_interleaver = Interleaver(self, 'dma_interleaver', nb_slaves=nb_banks_per_tile, nb_masters=1,
                                      interleaving_bits=2, enable_shift=shift_bits,
                                      offset_translation=False)

        #
        # Bindings
        #

        for i in range(nb_banks_per_tile):
            self.bind(l1_adapters[i], 'output', l1_banks[i], 'input')

        # Local-side input ports
        for i in range(nb_local_ports):
            self.bind(self, f'local_in_{i}', local_interleavers[i], 'in_0')

        # Remote-side input ports
        for i in range(nb_remote_ports):
            self.bind(self, f'remote_in_{i}', remote_in_interfaces[i], 'input')
            self.bind(remote_in_interfaces[i], 'output', remote_interleaver, f'in_{i}')

        # Selectors -> remote_out_interface inputs
        for i in range(nb_local_ports):
            xbar_in = 'input' if i == 0 else f'input_{i}'
            self.bind(intra_group_selectors[i], 'output', remote_out_interface, xbar_in)
            if nb_inter_group > 0:
                self.bind(inter_group_selectors[i], 'output', remote_out_interface, xbar_in)

        # Remote-out port fan-out
        for i in range(nb_remote_ports):
            xbar_out = 'output' if i == 0 else f'output_{i}'
            self.bind(remote_out_interface, xbar_out, self, f'remote_out_{i}')

        # DMA
        self.bind(self, 'dma', dma_interface, 'input')
        self.bind(dma_interface, 'output', dma_interleaver, 'in_0')
        for i in range(nb_banks_per_tile):
            self.bind(dma_interleaver, f'out_{i}', l1_banks[i], 'input')

        #
        # Address sorting (virtual interleavers -> per-bank arbiter or selectors)
        #
        bank_nb_masters = nb_local_ports + 1  # nb_local + 1 (remote interleaver)
        for i in range(total_banks):
            tgt_grp_id = int(i / (nb_tiles_per_group * nb_banks_per_tile))
            if start_bank_id <= i < end_bank_id:
                # Local bank: arbitrate among all locals + the remote_interleaver.
                remove_offset = Interleaver(self, f'remove_offset_{i}', nb_slaves=1,
                                            nb_masters=bank_nb_masters,
                                            interleaving_bits=2, enable_shift=shift_bits,
                                            offset_translation=False)
                for j, local_interleaver in enumerate(local_interleavers):
                    self.bind(local_interleaver, f'out_{i}', remove_offset, f'in_{j}')
                self.bind(remote_interleaver, f'out_{i}', remove_offset, f'in_{nb_local_ports}')
                self.bind(remove_offset, 'out_0', l1_adapters[i - start_bank_id], 'input')
            elif tgt_grp_id == group_id:
                # Intra-group remote target -> route through intra-group selectors.
                for j, local_interleaver in enumerate(local_interleavers):
                    self.bind(local_interleaver, f'out_{i}', intra_group_selectors[j], 'input')
            else:
                # Inter-group remote target -> route through inter-group selectors.
                assert nb_inter_group > 0, "Inter-group target requires nb_remote_ports > 1"
                for j, local_interleaver in enumerate(local_interleavers):
                    self.bind(local_interleaver, f'out_{i}', inter_group_selectors[j], 'input')

    def i_DMA_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'dma_input', signature='io')

    def add_mapping(self, name: str, base: int = None, size: int = None,
                    remove_offset: int = None, add_offset: int = None,
                    id: int = None, latency: int = None):
        mapping = {}
        if base          is not None: mapping['base']          = base
        if size          is not None: mapping['size']          = size
        if remove_offset is not None: mapping['remove_offset'] = remove_offset
        if add_offset    is not None: mapping['add_offset']    = add_offset
        if latency       is not None: mapping['latency']       = latency
        if id            is not None: mapping['id']            = id
        self.get_property('mappings')[name] = mapping
