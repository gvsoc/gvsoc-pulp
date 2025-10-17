# Copyright (C) 2025 Fondazione Chips-IT

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



# Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)

import gvsoc.systree
import memory.memory as memory
import interco.router as router
import gdbserver.gdbserver
from pulp.stdout.stdout_v3 import Stdout

import pulp.cpu.iss.pulp_cores as iss
from pulp.cluster.l1_interleaver import L1_interleaver
from pulp.light_redmule.hwpe_interleaver import HWPEInterleaver
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from pulp.chips.magia.hierarchical_cache import Hierarchical_cache

from pulp.chips.magia.arch import MagiaArch
from pulp.chips.magia.core import CV32CoreTest
#from pulp.redmule.redmule import RedMule
from pulp.light_redmule.light_redmule import LightRedmule
from pulp.idma.snitch_dma import SnitchDma
from pulp.chips.magia.xif_decoder import XifDecoder
from pulp.chips.magia.idma_ctrl import Magia_iDMA_Ctrl



# adapted from snitch cluster model
# interface i_INPUT -> interleaver -> banks
class MagiaTileTcdm(gvsoc.systree.Component):
    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        nb_banks = MagiaArch.N_MEM_BANKS
        bank_size = MagiaArch.N_WORDS_BANK * MagiaArch.BYTES_PER_WORD

        # 1 master: OBI, iDMA0, iDMA1
        L1_masters = 3
        interleaver = L1_interleaver(self, 'interleaver', nb_slaves=nb_banks, nb_masters=L1_masters, interleaving_bits=2)

        # 3 masters: OBI
        dma_masters = 1
        dma_interleaver = DmaInterleaver(self, 'dma_interleaver', nb_master_ports=dma_masters, nb_banks=nb_banks, bank_width=4)
        
        # 1 master: redmule
        redmule_masters = 1
        redmule_interleaver = HWPEInterleaver(self, 'redmule_interleaver', nb_master_ports=redmule_masters, nb_banks=nb_banks, bank_width=4)

        banks = []
        for i in range(nb_banks):
            # Instantiate a new memory bank
            bank = memory.Memory(self, f'bank_{i}', atomics=True, size=bank_size, latency=1)
            banks.append(bank)

            # Bind the new bank (slave) to the interleaver (master)
            self.bind(interleaver, f'out_{i}', bank, 'input')
            self.bind(dma_interleaver, f'out_{i}', bank, 'input')
            self.bind(redmule_interleaver, f'out_{i}', bank, 'input')

        # Bind external ports (input->[internal]output->interleaver)
        for i in range(L1_masters):
            self.bind(self, f'L1_input_{i}', interleaver, f'in_{i}')

        for i in range(dma_masters):
            self.bind(self, f'IDMA_input', dma_interleaver, f'input')

        for i in range(redmule_masters):
            self.bind(self, f'RedMulE_input', redmule_interleaver, f'input')

    # Input ports (port number as arguments)
    def i_INPUT(self, id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'L1_input_{id}', signature='io')
    
    def i_DMA_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'IDMA_input', signature='io')
    
    def i_REDMULE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'RedMulE_input', signature='io')


class MagiaTile(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, tid: int=0):
        super().__init__(parent, name)

        # Core model from pulp cores
        core_cv32 = CV32CoreTest(self, f'tile-{tid}-cv32-core',core_id=tid)

        # Instruction cache (from snitch cluster model)
        i_cache = Hierarchical_cache(self, f'tile-{tid}-icache', nb_cores=1, has_cc=0, l1_line_size_bits=4)

        # Data scratchpad
        l1_tcdm = MagiaTileTcdm(self, f'tile-{tid}-tcdm', parser)

        # AXI and OBI x-bars
        tile_xbar = router.Router(self, f'tile-{tid}-axi-xbar',bandwidth=4,latency=2)
        obi_xbar = router.Router(self, f'tile-{tid}-obi-xbar',bandwidth=4,latency=2)

        # IDMA Controller
        idma_ctrl= Magia_iDMA_Ctrl(self,f'tile-{tid}-idma-ctrl')

        # IDMA
        idma0 = SnitchDma(self,f'tile-{tid}-idma0',loc_base=tid*MagiaArch.L1_TILE_OFFSET,loc_size=MagiaArch.L1_SIZE,tcdm_width=4,transfer_queue_size=1,burst_queue_size=1)
        idma1 = SnitchDma(self,f'tile-{tid}-idma1',loc_base=tid*MagiaArch.L1_TILE_OFFSET,loc_size=MagiaArch.L1_SIZE,tcdm_width=4,transfer_queue_size=1,burst_queue_size=1)

        # Redmule
        redmule = LightRedmule(self, f'tile-{tid}-redmule',
                                    tcdm_bank_width     = MagiaArch.BYTES_PER_WORD,
                                    tcdm_bank_number    = MagiaArch.N_MEM_BANKS,
                                    elem_size           = 2, #max number of bytes per element --> if FP16 then elem_size=2. This is the max number to accomodate any supported format which for now are 8bits and 16bits data types 
                                    ce_height           = 8, # 8 in RTL
                                    ce_width            = 8, # 24 in RTL
                                    ce_pipe             = 1,
                                    queue_depth         = 1,
                                    loc_base            = tid*MagiaArch.L1_TILE_OFFSET)
        
        #new_rm=RedMule(self, 'new_redmule')
        
        # Xif decoder
        xifdec = XifDecoder(self,f'tile-{tid}-xifdec')

        # UART
        stdout = Stdout(self, f'tile-{tid}-stdout',max_cluster=MagiaArch.NB_CLUSTERS,max_core_per_cluster=1,user_set_core_id=0,user_set_cluster_id=tid)

        # Bind: cv32 core data -> obi interconnect
        core_cv32.o_DATA(obi_xbar.i_INPUT())
        core_cv32.o_DATA_DEBUG(obi_xbar.i_INPUT())

        # Bind: loader -> obi interconnect
        self.__o_LOADER(obi_xbar.i_INPUT())

        # Bind: cv32 core -> i cache
        core_cv32.o_FETCH(i_cache.i_INPUT(0))
        core_cv32.o_FLUSH_CACHE(i_cache.i_FLUSH())
        i_cache.o_FLUSH_ACK(core_cv32.i_FLUSH_CACHE_ACK())

        # Bind: icache -> tile interconnect
        i_cache.o_REFILL(tile_xbar.i_INPUT())

        # Bind obi xbar so that it can communicate with local reserved
        obi_xbar.o_MAP(l1_tcdm.i_INPUT(0), name="local-reserved",
                       base=MagiaArch.RESERVED_ADDR_START,
                       size=MagiaArch.RESERVED_SIZE, rm_base=False)
        
        # Bind obi xbar so that it can communicate with local stack
        obi_xbar.o_MAP(l1_tcdm.i_INPUT(0), name="local-stack",
                       base=MagiaArch.STACK_ADDR_START,
                       size=MagiaArch.STACK_SIZE, rm_base=False)
        
        # Bind obi xbar so that it can communicate with local L1
        obi_xbar.o_MAP(l1_tcdm.i_DMA_INPUT(), name="local-l1-mem", #here we use the iDMA interleaver because an iDMA axi request routed to obi (e.g. local L1 to off-tile L1 data movement) does not handle the right bank interleaving
                       base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
                       size=MagiaArch.L1_SIZE, rm_base=False, remove_offset=(tid*MagiaArch.L1_TILE_OFFSET))
        
        # Bind obi xbar so that it can communicate with tile xbar to get access to remote tiles l1 and reserved mem
        for tile_id in range(0,MagiaArch.NB_CLUSTERS):
            if (tile_id!=tid): #skip yourself
                obi_xbar.o_MAP(tile_xbar.i_INPUT(), name=f'obi2axi-off-tile-{tile_id}-l1-mem',
                        base=MagiaArch.L1_ADDR_START+(tile_id*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.L1_SIZE, rm_base=False)
        
        # Bind tile xbar so that it can communicate with remote tiles l1 and reserved mem
        for tile_id in range(0,MagiaArch.NB_CLUSTERS):
            if (tile_id!=tid): #skip yourself
                tile_xbar.o_MAP(self.__i_NARROW_OUTPUT(), name=f'axi-to-off-tile-{tile_id}-l1-mem',
                        base=MagiaArch.L1_ADDR_START+(tile_id*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.L1_SIZE, rm_base=False)

        # Bind tile xbar so that it can coomunicate with obi xbar l1 mem
        tile_xbar.o_MAP(obi_xbar.i_INPUT(), name="axi-to-obi-l1-mem",
                        base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.L1_SIZE, rm_base=False)
        
        # Bind tile xbar so that it can coomunicate with obi xbar reserved mem
        tile_xbar.o_MAP(obi_xbar.i_INPUT(), name="axi-to-obi-reserved-mem",
                        base=MagiaArch.RESERVED_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.RESERVED_SIZE, rm_base=False)
        
        # Mapping used by obi xbar to communicate with tile xbar
        obi_xbar.o_MAP(tile_xbar.i_INPUT(), name="obi2axi-off-tile-l2-mem",
                       base=MagiaArch.L2_ADDR_START,
                       size=MagiaArch.L2_SIZE, rm_base=False)

        # Bind tile xbar so that it can write l2 mem
        tile_xbar.o_MAP(self.__i_NARROW_OUTPUT(), name="axi-to-off-tile-l2-mem",
                        base=MagiaArch.L2_ADDR_START,
                        size=MagiaArch.L2_SIZE, rm_base=False)
        
        # Bind obi xbar so that it can write to kill_module
        obi_xbar.o_MAP(self.__i_KILLER_OUTPUT(), name="Kill-sim-mem",
                base=MagiaArch.TEST_END_ADDR_START,
                size=MagiaArch.TEST_END_SIZE, rm_base=False)
        
        # Bind obi xbar so that it can write to uart
        obi_xbar.o_MAP(stdout.i_INPUT(), name="local-uart-mem",
                base=MagiaArch.STDOUT_ADDR_START,
                size=MagiaArch.STDOUT_SIZE, rm_base=False)
        
        self.__o_NARROW_INPUT(tile_xbar.i_INPUT())

        # Bind: cv32 core enable ports -> matching composite ports
        self.__o_ENTRY(core_cv32.i_ENTRY())
        self.__o_FETCHEN(core_cv32.i_FETCHEN())

        # Bind: xif decoder
        core_cv32.o_OFFLOAD(xifdec.i_OFFLOAD_M())
        xifdec.o_OFFLOAD_GRANT_M(core_cv32.i_OFFLOAD_GRANT())

        # Bind: iDMA controller
        xifdec.o_OFFLOAD_S1(idma_ctrl.i_OFFLOAD_M())
        idma_ctrl.o_OFFLOAD_GRANT_M(xifdec.i_OFFLOAD_GRANT_S1())
        idma_ctrl.o_IRQ_DMA0(core_cv32.i_IRQ(26))
        idma_ctrl.o_IRQ_DMA1(core_cv32.i_IRQ(27))

        # Bind: idma0
        idma0.o_AXI(tile_xbar.i_INPUT())
        idma0.o_TCDM(l1_tcdm.i_INPUT(1)) #here we don't use the iDMA interleaver because here iDMA is directly connected to TCDM and iDMA has it's own interleaver for TCDM access (in iDMA-BE)
        idma_ctrl.o_OFFLOAD_iDMA0_AXI2OBI(idma0.i_OFFLOAD())
        idma0.o_OFFLOAD_GRANT(idma_ctrl.i_OFFLOAD_GRANT_iDMA0_AXI2OBI())

        # Bind: idma1
        idma1.o_AXI(tile_xbar.i_INPUT())
        idma1.o_TCDM(l1_tcdm.i_INPUT(2)) #here we don't use the iDMA interleaver because here iDMA is directly connected to TCDM and iDMA has it's own interleaver for TCDM access (in iDMA-BE)
        idma_ctrl.o_OFFLOAD_iDMA1_OBI2AXI(idma1.i_OFFLOAD())
        idma1.o_OFFLOAD_GRANT(idma_ctrl.i_OFFLOAD_GRANT_iDMA1_OBI2AXI())

        # Bind: redmule
        redmule.o_TCDM(l1_tcdm.i_REDMULE_INPUT())
        xifdec.o_OFFLOAD_S2(redmule.i_OFFLOAD())
        redmule.o_OFFLOAD_GRANT(xifdec.i_OFFLOAD_GRANT_S2())
        redmule.o_IRQ(core_cv32.i_IRQ(31))

        # Bind fractal sync ports
        xifdec.o_XIF_2_FRACTAL_EAST_WEST(self.__o_SLAVE_EAST_WEST_FRACTAL())
        self.__i_SLAVE_EAST_WEST_FRACTAL(xifdec.i_FRACTAL_2_XIF_EAST_WEST())

        xifdec.o_XIF_2_FRACTAL_NORD_SUD(self.__o_SLAVE_NORD_SUD_FRACTAL())
        self.__i_SLAVE_NORD_SUD_FRACTAL(xifdec.i_FRACTAL_2_XIF_NORD_SUD())

        xifdec.o_XIF_2_NEIGHBOUR_FRACTAL_EAST_WEST(self.__o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL())
        self.__i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(xifdec.i_NEIGHBOUR_FRACTAL_2_XIF_EAST_WEST())

        xifdec.o_XIF_2_NEIGHBOUR_FRACTAL_NORD_SUD(self.__o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL())
        self.__i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(xifdec.i_NEIGHBOUR_FRACTAL_2_XIF_NORD_SUD())

        # Enable debug
        gdbserver.gdbserver.Gdbserver(self, 'gdbserver')        

    
    # east west port to fractalsync
    def __o_SLAVE_EAST_WEST_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_east_west_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_EAST_WEST_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_east_west_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_EAST_WEST_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('east_west_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_EAST_WEST_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'east_west_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')

    # nord sud to fractalsync
    def __o_SLAVE_NORD_SUD_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_nord_sud_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_NORD_SUD_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_nord_sud_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_NORD_SUD_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('nord_sud_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_NORD_SUD_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'nord_sud_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')
    
    # east west port to neighbour fractalsync
    def __o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_east_west_neighbour_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_east_west_neighbour_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('east_west_neighbour_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'east_west_neighbour_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')
    
    # nord sud to neighbour fractalsync
    def __o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_nord_sud_neighbour_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_nord_sud_neighbour_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('nord_sud_neighbour_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'nord_sud_neighbour_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')

    # Output (master) port to off-tile L2 memory
    def o_NARROW_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_output', itf, signature='io')

    def __i_NARROW_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_output', signature='io')
    
    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature='io')

    def __o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature='io', composite_bind=True)

    # Input port for the loader
    def i_LOADER(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'loader', signature='io')

    def __o_LOADER(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('loader', itf, signature='io', composite_bind=True)

    def i_FETCHEN(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'fetchen', signature='wire<bool>')

    def __o_FETCHEN(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('fetchen', itf, signature='wire<bool>', composite_bind=True)

    def i_ENTRY(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'entry', signature='wire<uint64_t>')

    def __o_ENTRY(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('entry', itf, signature='wire<uint64_t>', composite_bind=True)

    # Killer port
    def o_KILLER_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('killer_output', itf, signature='io')

    def __i_KILLER_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'killer_output', signature='io')