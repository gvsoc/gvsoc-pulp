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
import cpu.iss.riscv as iss
import memory.memory as memory
import interco.router as router
import gvsoc.systree
from pulp.snitch.snitch_cluster.cluster_registers import ClusterRegisters
from elftools.elf.elffile import *
import gvsoc.runner


GAPY_TARGET = True

class SnitchCluster(gvsoc.systree.Component):

    def __init__(self, parent, name):
        super().__init__(parent, name)

        nb_cores = 9
        cores = []

        tcdm = memory.Memory(self, 'tcdm', size=0x40000)

        ico = router.Router(self, 'ico')

        for core_id in range(0, nb_cores):
            cores.append(iss.Snitch(self, f'pe{core_id}', isa='rv32imfdvc', fetch_enable=True,
                                    boot_addr=0x01000000, core_id=core_id+1))

        cluster_registers = ClusterRegisters(self, 'cluster_registers', boot_addr=0x0,
            nb_cores=nb_cores)
        ico.add_mapping('cluster_registers', base=0x10020000, remove_offset=0x10020000, size=0x1000)
        self.bind(ico, 'cluster_registers', cluster_registers, 'input')

        ico.add_mapping('tcdm', base=0x10000000, remove_offset=0x10000000, size=0x20000)
        self.bind(ico, 'tcdm', tcdm, 'input')

        ico.add_mapping('soc', base=0x00000000, remove_offset=0x00000000, size=0x10000000)
        self.bind(ico, 'soc', self, 'soc')

        ico.add_mapping('soc_high', base=0x11000000, remove_offset=0x00000000, size=0x20000000000 - 0x11000000)
        self.bind(ico, 'soc_high', self, 'soc')

        for core_id in range(0, nb_cores):
            self.bind(cores[core_id], 'barrier_req', cluster_registers, f'barrier_req_{core_id}')
            self.bind(cluster_registers, f'barrier_ack', cores[core_id], 'barrier_ack')

        for core_id in range(0, nb_cores):
            cluster_registers.o_EXTERNAL_IRQ(core_id, cores[core_id].i_IRQ(19))

        for core_id in range(0, nb_cores):
            self.bind(cores[core_id], 'data', ico, 'input')
            self.bind(cores[core_id], 'fetch', ico, 'input')

        self.bind(self, 'input', ico, 'input')

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('soc', itf, signature='io')
