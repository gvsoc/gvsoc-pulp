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
import pulp.snitch.snitch_core as iss
import memory.memory as memory
import interco.router as router
import gvsoc.systree
from pulp.snitch.snitch_cluster.cluster_registers import ClusterRegisters
from pulp.snitch.zero_mem import ZeroMem
from elftools.elf.elffile import *
from pulp.idma.snitch_dma import SnitchDma
import gvsoc.runner


GAPY_TARGET = True

class SnitchCluster(gvsoc.systree.Component):

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        #
        # Components
        #

        # Main router
        ico = router.Router(self, 'ico')

        # Dedicated router for dma to TCDM
        tcdm_dma_ico = router.Router(self, 'tcdm_dma_ico', bandwidth=64)

        # L1 Memory
        tcdm = memory.Memory(self, 'tcdm', size=arch.tcdm.size, atomics=True, width_log2=7)

        # Zero memory
        zero_mem = ZeroMem(self, 'zero_mem', size=arch.zero_mem.size)

        # Cores
        cores = []
        for core_id in range(0, arch.nb_core):
            cores.append(iss.Snitch(self, f'pe{core_id}', isa='rv32imfdvca', fetch_enable=True,
                                    boot_addr=arch.boot_addr, core_id=arch.first_hartid + core_id))

        # Cluster peripherals
        cluster_registers = ClusterRegisters(self, 'cluster_registers', nb_cores=arch.nb_core)

        # Cluster DMA
        idma = SnitchDma(self, 'idma', loc_base=arch.tcdm.base, loc_size=arch.tcdm.size,
            tcdm_width=64)

        #
        # Bindings
        #

        # Main router
        self.o_INPUT(ico.i_INPUT())
        ico.o_MAP(tcdm.i_INPUT(), base=arch.tcdm.base, size=arch.tcdm.size, rm_base=True)
        ico.o_MAP(zero_mem.i_INPUT(), base=arch.zero_mem.base, size=arch.zero_mem.size, rm_base=True)
        ico.o_MAP(cluster_registers.i_INPUT(), base=arch.peripheral.base, size=arch.peripheral.size, rm_base=True)
        ico.o_MAP(self.i_SOC())

        # Dedicated router for dma to TCDM
        tcdm_dma_ico.o_MAP(tcdm.i_INPUT(), base=arch.tcdm.base, size=arch.tcdm.size, rm_base=True)

        # Cores
        cores[arch.nb_core-1].o_OFFLOAD(idma.i_OFFLOAD())
        idma.o_OFFLOAD_GRANT(cores[arch.nb_core-1].i_OFFLOAD_GRANT())

        for core_id in range(0, arch.nb_core):
            cores[core_id].o_BARRIER_REQ(cluster_registers.i_BARRIER_ACK(core_id))
        for core_id in range(0, arch.nb_core):
            cores[core_id].o_DATA(ico.i_INPUT())
            cores[core_id].o_FETCH(ico.i_INPUT())

        # Cluster peripherals
        for core_id in range(0, arch.nb_core):
            self.bind(cluster_registers, f'barrier_ack', cores[core_id], 'barrier_ack')
        for core_id in range(0, arch.nb_core):
            cluster_registers.o_EXTERNAL_IRQ(core_id, cores[core_id].i_IRQ(arch.barrier_irq))

        # Cluster DMA
        idma.o_AXI(ico.i_INPUT())
        idma.o_TCDM(tcdm_dma_ico.i_INPUT())

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('input', itf, signature='io', composite_bind=True)

    def i_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'soc', signature='io')

    def o_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('soc', itf, signature='io')
