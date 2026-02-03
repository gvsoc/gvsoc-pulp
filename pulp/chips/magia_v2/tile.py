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

import json
import gvsoc.systree
import memory.memory as memory
import interco.router as router
import gdbserver.gdbserver
from pulp.stdout.stdout_v3 import Stdout

from pulp.snitch.snitch_core import *
from pulp.cluster.l1_interleaver import L1_interleaver
from pulp.light_redmule.hwpe_interleaver import HWPEInterleaver
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from pulp.snitch.hierarchical_cache import Hierarchical_cache
from pulp.chips.magia_v2.cv32.hierarchical_cache import CV32_Hierarchical_cache

from pulp.chips.magia_v2.arch import *
from pulp.chips.magia_v2.cv32.core import CV32CoreTest
from pulp.light_redmule.light_redmule import LightRedmule
from pulp.idma.snitch_dma import SnitchDma
from pulp.chips.magia_v2.fractal_sync_mm_ctrl.fractal_sync_mm_ctrl import FSync_mm_ctrl
from pulp.chips.magia_v2.idma_mm_ctrl.idma_mm_ctrl import iDMA_mm_ctrl
from pulp.chips.magia_v2.spatz.snitch_spatz_regs import SnitchSpatzRegs
from pulp.event_unit.event_unit_v3 import Event_unit


# adapted from snitch cluster model
# interface i_INPUT -> interleaver -> banks
class MagiaTileTcdm(gvsoc.systree.Component):
    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        nb_banks = MagiaArch.N_MEM_BANKS
        bank_size = MagiaArch.N_WORDS_BANK * MagiaArch.BYTES_PER_WORD

        # 1 master: OBI
        L1_masters = 1
        interleaver = L1_interleaver(self, 'interleaver', nb_slaves=nb_banks, nb_masters=L1_masters, interleaving_bits=2)

        # 3 masters: OBI/WIDE-NOC, iDMA0, iDMA1
        dma_masters = 3
        dma_interleaver = DmaInterleaver(self, 'dma_interleaver', nb_master_ports=dma_masters, nb_banks=nb_banks, bank_width=MagiaArch.BYTES_PER_WORD)
        
        # 1 master: redmule
        redmule_masters = 1
        redmule_interleaver = HWPEInterleaver(self, 'redmule_interleaver', nb_master_ports=redmule_masters, nb_banks=nb_banks, bank_width=MagiaArch.BYTES_PER_WORD)

        # 4 master: VLSU0, VLSU1, VLSU2, VLSU03
        snitch_spatz_vlsu = 4
        snitch_spatz_interleaver = L1_interleaver(self, 'snitch_spatz_interleaver', nb_slaves=nb_banks, nb_masters=snitch_spatz_vlsu, interleaving_bits=2, offset_mask=MagiaArch.L1_TILE_OFFSET-1)

        banks = []
        for i in range(nb_banks):
            # Instantiate a new memory bank
            bank = memory.Memory(self, f'bank_{i}', atomics=True, size=bank_size, latency=MagiaDSE.TILE_TCDM_LATENCY)
            banks.append(bank)

            # Bind the new bank (slave) to the interleaver (master)
            self.bind(interleaver, f'out_{i}', bank, 'input')
            self.bind(dma_interleaver, f'out_{i}', bank, 'input')
            self.bind(redmule_interleaver, f'out_{i}', bank, 'input')
            self.bind(snitch_spatz_interleaver, f'out_{i}', bank, 'input')

        # Bind external ports (input->[internal]output->interleaver)
        for i in range(L1_masters):
            self.bind(self, f'L1_input_{i}', interleaver, f'in_{i}')

        for i in range(dma_masters):
            self.bind(self, f'IDMA_input', dma_interleaver, f'input')

        for i in range(redmule_masters):
            self.bind(self, f'RedMulE_input', redmule_interleaver, f'input')

        for i in range(snitch_spatz_vlsu):
            self.bind(self, f'SnitchSpatz_input_{i}', snitch_spatz_interleaver, f'in_{i}')

    # Input ports (port number as arguments)
    def i_INPUT(self, id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'L1_input_{id}', signature='io')
    
    def i_DMA_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'IDMA_input', signature='io')
    
    def i_REDMULE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'RedMulE_input', signature='io')
    
    def i_SNITCH_SPATZ(self, id: int)  -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'SnitchSpatz_input_{id}', signature='io')


class MagiaV2Tile(gvsoc.systree.Component):
    def ev_unit_config(self):
        event_unit_json = {
            "event_unit": {
                "version": "4",
                "mapping": {
                    "base": MagiaArch.EVENT_UNIT_ADDR_START,
                    "size": MagiaArch.EVENT_UNIT_SIZE,
                    "remove_offset": MagiaArch.EVENT_UNIT_ADDR_START
                },
                "config": {
                    "nb_core": 1,
                    "properties": {
                        "dispatch": {"size": 8},
                        "mutex": {"nb_mutexes": 0},
                        "barriers": {"nb_barriers": 0},
                        "soc_event": {"nb_fifo_events": 8, "fifo_event": 8},
                        "events": {
                            "dispatch": 8,
                            "mutex": 0,
                            "barrier": 0
                        }
                    }
                }
            }
        }
        json_data = json.dumps(event_unit_json)
        return json.loads(json_data)

    def __init__(self, parent, name, tree, parser, tid: int=0):
        super().__init__(parent, name)
        
        # Core model from pulp cores
        core_cv32 = CV32CoreTest(self, f'tile-{tid}-cv32-core',core_id=tid)

        if MagiaArch.ENABLE_SPATZ:

            # Snitch Spatz boot rom file
            snitch_spatz_rom = memory.Memory(self, 'snitch-spatz-rom', size=MagiaArch.SPATZ_BOOTROM_SIZE,stim_file=self.get_file_path(tree.romfile))

            # Snitch Spatz cores (fetch_enable is set to false as we control the boot sequence. The core automatically starts from the rom and the corresponding boot address as soon as we issue the fetch enable)
            snitch_spatz = SnitchFast(self, f'tile-{tid}-snitch-spatz',
                                        isa             = "rv32imfdav", #rv32imfdcav
                                        fetch_enable    = False,
                                        boot_addr       = MagiaArch.SPATZ_BOOTROM_ADDR,
                                        core_id         = tid + tree.NB_CLUSTERS,
                                        htif            = False,
                                        inc_spatz       = True,
                                        vlen            = 256,
                                        spatz_nb_lanes  = 4,
                                        pulp_v2         = False,
                                        ssr             = False)
            
            # Instruction cache (from snitch cluster model). PLEASE DOUBLE CHECK THAT THE INTERNAL PARAMETERS OF THIS MODEL ARE THE SAME OF THE CV32 I_CACHE.
            snitch_spatz_i_cache = Hierarchical_cache(self, f'tile-{tid}-snitch-spatz-icache', nb_cores=1, has_cc=0, l1_line_size_bits=7)

            # Snitch Spatz CC control registers
            snitch_spatz_regs = SnitchSpatzRegs(self, f'tile-{tid}-snitch-spatz-regs')

        # Instruction cache (from snitch cluster model)
        cv32_i_cache = CV32_Hierarchical_cache(self, f'tile-{tid}-cv32-icache', nb_cores=1, has_cc=0, l1_line_size_bits=4)

        # Data scratchpad
        l1_tcdm = MagiaTileTcdm(self, f'tile-{tid}-tcdm', parser)

        # AXI and OBI x-bars
        tile_xbar = router.Router(self, f'tile-{tid}-axi-xbar',bandwidth=4,latency=MagiaDSE.TILE_AXI_XBAR_LATENCY,synchronous=MagiaDSE.TILE_AXI_XBAR_SYNC)
        obi_xbar = router.Router(self, f'tile-{tid}-obi-xbar',bandwidth=4,latency=MagiaDSE.TILE_OBI_XBAR_LATENCY,synchronous=MagiaDSE.TILE_OBI_XBAR_SYNC)

        # IDMA Controller
        idma_mm_ctrl= iDMA_mm_ctrl(self,f'tile-{tid}-idma-ctrl-mm')

        if not MagiaArch.USE_NARROW_WIDE:
            # IDMA  
            idma0 = SnitchDma(self,f'tile-{tid}-idma0',loc_base=tid*MagiaArch.L1_TILE_OFFSET,loc_size=MagiaArch.L1_SIZE,tcdm_width=4,transfer_queue_size=1,burst_queue_size=MagiaDSE.TILE_IDMA0_BQUEUE_SIZE,burst_size=MagiaDSE.TILE_IDMA0_B_SIZE)
            idma1 = SnitchDma(self,f'tile-{tid}-idma1',loc_base=tid*MagiaArch.L1_TILE_OFFSET,loc_size=MagiaArch.L1_SIZE,tcdm_width=4,transfer_queue_size=1,burst_queue_size=MagiaDSE.TILE_IDMA1_BQUEUE_SIZE,burst_size=MagiaDSE.TILE_IDMA1_B_SIZE)
            # Redmule
            redmule = LightRedmule(self, f'tile-{tid}-redmule',
                                        tcdm_bank_width     = MagiaArch.BYTES_PER_WORD,
                                        tcdm_bank_number    = 16,# here we set 16 since tcdm_bank_width x tcdm_bank_number --> 16 x 4 = 64bytes, i.e., 512 bits. Please do not consider tcdm_bank_width and tcdm_bank_number as the boundaries of the TCDM, but rather the size of the port towards it. 
                                        elem_size           = 2, # max number of bytes per element --> if FP16 then elem_size=2. This is the max number to accomodate any supported format which for now are 8bits and 16bits data types 
                                        ce_height           = 8,
                                        ce_width            = 24,
                                        ce_pipe             = 3,
                                        queue_depth         = 1,
                                        loc_base            = (tid*MagiaArch.L1_TILE_OFFSET))
        else:
            # IDMA
            idma0 = SnitchDma(self,f'tile-{tid}-idma0',loc_base=(tid*MagiaArch.L1_TILE_OFFSET),loc_size=MagiaArch.L1_SIZE,tcdm_width=32,transfer_queue_size=1,burst_queue_size=MagiaDSE.TILE_IDMA0_BQUEUE_SIZE,burst_size=MagiaDSE.TILE_IDMA0_B_SIZE)
            idma1 = SnitchDma(self,f'tile-{tid}-idma1',loc_base=(tid*MagiaArch.L1_TILE_OFFSET),loc_size=MagiaArch.L1_SIZE,tcdm_width=32,transfer_queue_size=1,burst_queue_size=MagiaDSE.TILE_IDMA1_BQUEUE_SIZE,burst_size=MagiaDSE.TILE_IDMA1_B_SIZE)
            # Redmule
            redmule = LightRedmule(self, f'tile-{tid}-redmule',
                                        tcdm_bank_width     = MagiaArch.BYTES_PER_WORD,
                                        tcdm_bank_number    = 8, # here we set 8 since tcdm_bank_width x tcdm_bank_number --> 8 x 4 = 32bytes, i.e., 256 bits. Please do not consider tcdm_bank_width and tcdm_bank_number as the boundaries of the TCDM, but rather the size of the port towards it. 
                                        elem_size           = 2, # max number of bytes per element --> if FP16 then elem_size=2. This is the max number to accomodate any supported format which for now are 8bits and 16bits data types 
                                        ce_height           = 8,
                                        ce_width            = 8,
                                        ce_pipe             = 1,
                                        queue_depth         = 1,
                                        loc_base            = (tid*MagiaArch.L1_TILE_OFFSET))
            
        # Fsync mm controller
        fsync_mm_ctrl = FSync_mm_ctrl(self,f'tile-{tid}-fs-ctrl-mm')

        # Event Unit
        self.add_properties(self.ev_unit_config())
        event_unit = Event_unit(self, f'tile-{tid}-event-unit', self.get_property('event_unit/config'))

        # UART
        stdout = Stdout(self, f'tile-{tid}-stdout',max_cluster=tree.NB_CLUSTERS,max_core_per_cluster=1,user_set_core_id=0,user_set_cluster_id=tid)

        # Bind: loader -> obi interconnect
        self.__o_LOADER(obi_xbar.i_INPUT())

        if MagiaArch.ENABLE_SPATZ:
            # Bind: snitch spatz core data -> obi interconnect
            snitch_spatz.o_DATA(obi_xbar.i_INPUT())
            snitch_spatz.o_DATA_DEBUG(obi_xbar.i_INPUT())

            # Bind: snitch spatz core -> snitch spatz icache
            snitch_spatz.o_FETCH(snitch_spatz_i_cache.i_INPUT(0))
            snitch_spatz.o_FLUSH_CACHE(snitch_spatz_i_cache.i_FLUSH())
            snitch_spatz_i_cache.o_FLUSH_ACK(snitch_spatz.i_FLUSH_CACHE_ACK())

            # Bind: snitch spatz icache -> tile interconnect
            snitch_spatz_i_cache.o_REFILL(obi_xbar.i_INPUT())

            # Bind: snitch spatz TCDM
            snitch_spatz.o_VLSU(0,l1_tcdm.i_SNITCH_SPATZ(0))
            snitch_spatz.o_VLSU(1,l1_tcdm.i_SNITCH_SPATZ(1))
            snitch_spatz.o_VLSU(2,l1_tcdm.i_SNITCH_SPATZ(2))
            snitch_spatz.o_VLSU(3,l1_tcdm.i_SNITCH_SPATZ(3))

            # Bind: snitch spatz core complex registers
            snitch_spatz_regs.o_CLK_EN(snitch_spatz.i_FETCHEN())
            snitch_spatz_regs.o_START(snitch_spatz.i_IRQ(11))

        # Bind: cv32 core data -> obi interconnect
        core_cv32.o_DATA(obi_xbar.i_INPUT())
        core_cv32.o_DATA_DEBUG(obi_xbar.i_INPUT())

        # Bind: cv32 core -> icache
        core_cv32.o_FETCH(cv32_i_cache.i_INPUT(0))
        core_cv32.o_FLUSH_CACHE(cv32_i_cache.i_FLUSH())
        cv32_i_cache.o_FLUSH_ACK(core_cv32.i_FLUSH_CACHE_ACK())

        # Bind: icache -> tile interconnect
        cv32_i_cache.o_REFILL(tile_xbar.i_INPUT())

        # Bind obi xbar so that it can communicate with RedMule
        obi_xbar.o_MAP(redmule.i_INPUT(), name=f'redmule-mm-{tid}-mem',
                       base=MagiaArch.REDMULE_CTRL_ADDR_START,
                       size=MagiaArch.REDMULE_CTRL_SIZE, rm_base=True)
        
        # Bind obi xbar so that it can communicate with iDMA mmapped controller
        obi_xbar.o_MAP(idma_mm_ctrl.i_INPUT(), name=f'iDMA-ctrl-mm-{tid}-mem',
                       base=MagiaArch.IDMA_CTRL_ADDR_START,
                       size=MagiaArch.IDMA_CTRL_SIZE, rm_base=True)

        # Bind obi xbar so that it can communicate with fsync mmapped controller
        obi_xbar.o_MAP(fsync_mm_ctrl.i_INPUT(), name=f'fs-ctrl-mm-{tid}-mem',
                       base=MagiaArch.FSYNC_CTRL_ADDR_START,
                       size=MagiaArch.FSYNC_CTRL_SIZE, rm_base=True)
        
        # Bind obi xbar so that it can communicate with Event-Unit mmapped controller 
        obi_xbar.add_mapping('event_unit', **self.get_property('event_unit/mapping'))
        self.bind(obi_xbar, 'event_unit', event_unit, 'input')
        
        # Bind obi xbar so that it can communicate with local stack
        obi_xbar.o_MAP(l1_tcdm.i_INPUT(0), name="local-stack",
                       base=MagiaArch.STACK_ADDR_START,
                       size=MagiaArch.STACK_SIZE, rm_base=False)
        
        if MagiaArch.ENABLE_SPATZ:
            # Bind obi xbar so that it can communicate with snitch spatz bootrom
            obi_xbar.o_MAP(snitch_spatz_rom.i_INPUT(), name="snitch-spatz-bootrom",
                        base=MagiaArch.SPATZ_BOOTROM_ADDR,
                        size=MagiaArch.SPATZ_BOOTROM_SIZE, rm_base=True)
            
            obi_xbar.o_MAP(snitch_spatz_regs.i_INPUT(), name="snitch-spatz-regs",
                        base=MagiaArch.SPATZ_CTRL_START,
                        size=MagiaArch.SPATZ_CTRL_SIZE, rm_base=True)
            
        if not MagiaArch.USE_NARROW_WIDE:
            # Bind obi xbar so that it can communicate with local L1
            obi_xbar.o_MAP(l1_tcdm.i_DMA_INPUT(), name="local-l1-mem", #here we use the iDMA interleaver because an iDMA axi request routed to obi (e.g. local L1 to off-tile L1 data movement) does not handle the right bank interleaving
                        base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.L1_SIZE, rm_base=False, remove_offset=(tid*MagiaArch.L1_TILE_OFFSET))
            # Bind obi xbar so that it can communicate with tile xbar to get access to remote tiles l1 and reserved mem
            for tile_id in range(0,tree.NB_CLUSTERS):
                if (tile_id!=tid): #skip yourself
                    obi_xbar.o_MAP(tile_xbar.i_INPUT(), name=f'obi2axi-off-tile-{tile_id}-l1-mem',
                            base=MagiaArch.L1_ADDR_START+(tile_id*MagiaArch.L1_TILE_OFFSET),
                            size=MagiaArch.L1_SIZE, rm_base=False)
            # Bind tile xbar so that it can coomunicate with obi xbar l1 mem
            tile_xbar.o_MAP(obi_xbar.i_INPUT(), name="axi-to-obi-l1-mem",
                            base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
                            size=MagiaArch.L1_SIZE, rm_base=False)
            # Bind tile xbar so that it can communicate with remote tiles l1 and reserved mem
            for tile_id in range(0,tree.NB_CLUSTERS):
                if (tile_id!=tid): #skip yourself
                    tile_xbar.o_MAP(self.__i_NARROW_OUTPUT(), name=f'axi-to-off-tile-{tile_id}-l1-mem',
                            base=MagiaArch.L1_ADDR_START+(tile_id*MagiaArch.L1_TILE_OFFSET),
                            size=MagiaArch.L1_SIZE, rm_base=False)
        else:
            # Bind NoC wide channel so that it can communicate with local L1
            self.__o_WIDE_INPUT(l1_tcdm.i_DMA_INPUT())
        
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

        # Bind: idma0
        if not MagiaArch.USE_NARROW_WIDE:
            idma0.o_AXI(tile_xbar.i_INPUT())
        else:
            idma0.o_AXI(self.__i_WIDE_OUTPUT())
        idma0.o_TCDM(l1_tcdm.i_DMA_INPUT())
        idma_mm_ctrl.o_OFFLOAD_iDMA0_AXI2OBI(idma0.i_OFFLOAD())
        idma0.o_OFFLOAD_GRANT(idma_mm_ctrl.i_OFFLOAD_GRANT_iDMA0_AXI2OBI())

        # Bind: idma1
        if not MagiaArch.USE_NARROW_WIDE:
            idma1.o_AXI(tile_xbar.i_INPUT())
        else:
            idma1.o_AXI(self.__i_WIDE_OUTPUT())
        idma1.o_TCDM(l1_tcdm.i_DMA_INPUT())
        idma_mm_ctrl.o_OFFLOAD_iDMA1_OBI2AXI(idma1.i_OFFLOAD())
        idma1.o_OFFLOAD_GRANT(idma_mm_ctrl.i_OFFLOAD_GRANT_iDMA1_OBI2AXI())

        # Bind: redmule
        redmule.o_TCDM(l1_tcdm.i_REDMULE_INPUT())

        # Bind Event unit
        self.bind(event_unit, 'clock_0', core_cv32, 'clock')
        self.bind(core_cv32, 'irq_ack', event_unit, 'irq_ack_0')
        self.bind(event_unit, 'irq_req_0', core_cv32, 'irq_req')
        self.bind(idma_mm_ctrl, 'idma0_done_irq', event_unit, 'in_event_2_pe_0')
        self.bind(idma_mm_ctrl, 'idma1_done_irq', event_unit, 'in_event_3_pe_0')
        if MagiaArch.ENABLE_SPATZ:
            self.bind(snitch_spatz_regs, 'spatz_done_irq', event_unit, 'in_event_8_pe_0')
        self.bind(redmule, 'done_irq', event_unit, 'in_event_10_pe_0')
        self.bind(fsync_mm_ctrl, 'fsync_done_irq', event_unit, 'in_event_24_pe_0')

        # Bind fractal sync ports
        fsync_mm_ctrl.o_XIF_2_FRACTAL_EAST_WEST(self.__o_SLAVE_EAST_WEST_FRACTAL())
        self.__i_SLAVE_EAST_WEST_FRACTAL(fsync_mm_ctrl.i_FRACTAL_2_XIF_EAST_WEST())

        fsync_mm_ctrl.o_XIF_2_FRACTAL_NORD_SUD(self.__o_SLAVE_NORD_SUD_FRACTAL())
        self.__i_SLAVE_NORD_SUD_FRACTAL(fsync_mm_ctrl.i_FRACTAL_2_XIF_NORD_SUD())

        fsync_mm_ctrl.o_XIF_2_NEIGHBOUR_FRACTAL_EAST_WEST(self.__o_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL())
        self.__i_SLAVE_EAST_WEST_NEIGHBOUR_FRACTAL(fsync_mm_ctrl.i_NEIGHBOUR_FRACTAL_2_XIF_EAST_WEST())

        fsync_mm_ctrl.o_XIF_2_NEIGHBOUR_FRACTAL_NORD_SUD(self.__o_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL())
        self.__i_SLAVE_NORD_SUD_NEIGHBOUR_FRACTAL(fsync_mm_ctrl.i_NEIGHBOUR_FRACTAL_2_XIF_NORD_SUD())

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

    # Output (master) narrow ports to off-tile L2 memory
    def o_NARROW_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_output', itf, signature='io')

    def __i_NARROW_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_output', signature='io')
    
    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature='io')

    def __o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature='io', composite_bind=True)

    # Output (master) wide port to off-tile L2 memory
    def o_WIDE_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_output', itf, signature='io')

    def __i_WIDE_OUTPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_output', signature='io')
    
    def i_WIDE_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'wide_input', signature='io')

    def __o_WIDE_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('wide_input', itf, signature='io', composite_bind=True)

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