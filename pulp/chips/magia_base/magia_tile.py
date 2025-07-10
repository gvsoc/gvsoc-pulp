#
# Copyright (C) 2025 ETH Zurich, University of Bologna and Fondazione ChipsIT
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
import memory.memory as memory
import interco.router as router
import gdbserver.gdbserver
from pulp.stdout.stdout_v3 import Stdout

import pulp.cpu.iss.pulp_cores as iss
from pulp.cluster.l1_interleaver import L1_interleaver
from pulp.snitch.hierarchical_cache import Hierarchical_cache

from pulp.chips.magia_base.magia_arch import MagiaArch
from pulp.chips.magia_base.magia_core import CV32CoreTest
from pulp.redmule.light_redmule import LightRedmule
from pulp.idma.snitch_dma import SnitchDma
from pulp.xif_decoder.xif_decoder import XifDecoder


# adapted from snitch cluster model
# interface i_INPUT -> interleaver -> banks
class MagiaTileTcdm(gvsoc.systree.Component):
    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        # TODO: for early tests only. Move to json later
        nb_banks = MagiaArch.N_MEM_BANKS
        bank_size = MagiaArch.N_WORDS_BANK * MagiaArch.BYTES_PER_WORD
        nb_masters = 1

        # interleaver for the whole TDCM
        interleaver = L1_interleaver(self, 'interleaver', nb_slaves=nb_banks,
                                     nb_masters=nb_masters,
                                     interleaving_bits=2)
        banks = []
        for i in range(nb_banks):
            # Instantiate a new memory bank
            bank = memory.Memory(self, f'bank_{i}', atomics=True, size=bank_size)
            banks.append(bank)

            # Bind the new bank (slave) to the interleaver (master)
            self.bind(interleaver, f'out_{i}', bank, 'input')

        # Bind external ports (input->[internal]output->interleaver)
        for i in range(nb_masters):
            self.bind(self, f'input_{i}', interleaver, f'in_{i}')

    # Input ports (port number as arguments)
    def i_INPUT(self, id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'input_{id}', signature='io')


class MagiaTile(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, tid: int=0):
        super().__init__(parent, name)

        # Core model from pulp cores
        core_cv32 = CV32CoreTest(self, f'tile-{tid}-cv32-core',core_id=tid)

        # Instruction cache (from snitch cluster model)
        i_cache = Hierarchical_cache(self, f'tile-{tid}-icache', nb_cores=1, has_cc=0)

        # Data scratchpad
        l1_tcdm = MagiaTileTcdm(self, f'tile-{tid}-tcdm', parser)

        # Temporary test interconnects (use obi to access TCDM), to be refined later
        tile_xbar = router.Router(self, f'tile-{tid}-tile-xbar')
        obi_xbar = router.Router(self, f'tile-{tid}-obi-xbar')

        # IDMA
        idma = SnitchDma(self,f'tile-{tid}-idma',loc_base=MagiaArch.L1_ADDR_START,loc_size=MagiaArch.L1_SIZE,tcdm_width=4)

        # Redmule
        redmule_nb_banks = MagiaArch.N_MEM_BANKS
        #redmule_bank_size = MagiaArch.N_WORDS_BANK * MagiaArch.BYTES_PER_WORD
        redmule_bank_size = MagiaArch.BYTES_PER_WORD
        #these parameters are taken from flex_cluster...
        redmule_ce_height       = 128
        redmule_ce_width        = 32
        redmule_ce_pipe         = 3
        redmule_elem_size       = 2
        redmule_queue_depth     = 1
        redmule = LightRedmule(self, f'tile-{tid}-redmule',
                                    tcdm_bank_width     = redmule_bank_size,
                                    tcdm_bank_number    = redmule_nb_banks,
                                    elem_size           = redmule_elem_size,
                                    ce_height           = redmule_ce_height,
                                    ce_width            = redmule_ce_width,
                                    ce_pipe             = redmule_ce_pipe,
                                    queue_depth         = redmule_queue_depth)
        
        # Xif decoder
        xifdec = XifDecoder(self,f'tile-{tid}-xifdec')

        # UART
        stdout = Stdout(self, 'tile-{tid}-stdout')

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

        # Bind: obi interconnect -> L1 TCDM, L2 off-tile (through tile_xbar)
        obi_xbar.o_MAP(l1_tcdm.i_INPUT(0), name="local-reserved",
                       base=MagiaArch.RESERVED_ADDR_START,
                       size=MagiaArch.RESERVED_SIZE, rm_base=False)
        obi_xbar.o_MAP(l1_tcdm.i_INPUT(0), name="local-stack",
                       base=MagiaArch.STACK_ADDR_START,
                       size=MagiaArch.STACK_SIZE, rm_base=False)
        obi_xbar.o_MAP(l1_tcdm.i_INPUT(0), name="local-l1-mem",
                       base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
                       size=MagiaArch.L1_SIZE, rm_base=False, remove_offset=(tid*MagiaArch.L1_TILE_OFFSET))
        obi_xbar.o_MAP(stdout.i_INPUT(), name="local-uart-mem",
                       base=MagiaArch.STDOUT_START,
                       size=MagiaArch.STDOUT_SIZE, rm_base=False)

        # Mapping used by obi xbar to communicate with tile xbar
        obi_xbar.o_MAP(tile_xbar.i_INPUT(), name="obi-to-axi-off-tile-l2-mem",
                       base=MagiaArch.L2_ADDR_START,
                       size=MagiaArch.L2_SIZE, rm_base=False)
        for tile_id in range(0,MagiaArch.NB_CLUSTERS):
            if (tile_id!=tid):
                obi_xbar.o_MAP(tile_xbar.i_INPUT(), name=f'obi-to-axi-off-tile-{tile_id}-l1-mem',
                        base=MagiaArch.L1_ADDR_START+(tile_id*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.L1_SIZE, rm_base=False)

        # Bind (with address relocation): tile interconnect -> L2 off-tile
        # Bind tile xbar so that it can write l2 mem
        tile_xbar.o_MAP(self.__i_NARROW_OUTPUT(), name="axi-off-tile-l2-mem",
                        base=MagiaArch.L2_ADDR_START,
                        size=MagiaArch.L2_SIZE, rm_base=False)
        # Bind tile xbar so that it can communicate with other tiles l1 mem
        for tile_id in range(0,MagiaArch.NB_CLUSTERS):
            if (tile_id!=tid):
                tile_xbar.o_MAP(self.__i_NARROW_OUTPUT(), name=f'axi-off-tile-{tile_id}-l1-mem',
                        base=MagiaArch.L1_ADDR_START+(tile_id*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.L1_SIZE, rm_base=False)
        # Bind tile xbar so that it can coomunicate with obi xbar
        tile_xbar.o_MAP(obi_xbar.i_INPUT(), name="axi-to-obi-l1-mem",
                        base=MagiaArch.L1_ADDR_START+(tid*MagiaArch.L1_TILE_OFFSET),
                        size=MagiaArch.L1_SIZE, rm_base=False)
        
        
        self.__o_NARROW_INPUT(tile_xbar.i_INPUT()) #lets disable the ports to other clusters for now..

        # Bind: cv32 core enable ports -> matching composite ports
        self.__o_ENTRY(core_cv32.i_ENTRY())
        self.__o_FETCHEN(core_cv32.i_FETCHEN())

        # Bind: xif decoder
        core_cv32.o_OFFLOAD(xifdec.i_OFFLOAD_M())
        xifdec.o_OFFLOAD_GRANT_M(core_cv32.i_OFFLOAD_GRANT())

        # Bind: idma
        idma.o_AXI(tile_xbar.i_INPUT())
        idma.o_TCDM(l1_tcdm.i_INPUT(0))
        xifdec.o_OFFLOAD_S1(idma.i_OFFLOAD())
        idma.o_OFFLOAD_GRANT(xifdec.i_OFFLOAD_GRANT_S1())

        # Bind: redmule
        redmule.o_TCDM(l1_tcdm.i_INPUT(0))
        xifdec.o_OFFLOAD_S2(redmule.i_OFFLOAD())
        redmule.o_OFFLOAD_GRANT(xifdec.i_OFFLOAD_GRANT_S2())

        # Bind fractal sync ports
        xifdec.o_XIF_2_FRACTAL_EAST_WEST(self.__o_SLAVE_EAST_WEST_FRACTAL())
        self.__i_SLAVE_EAST_WEST_FRACTAL(xifdec.i_FRACTAL_2_XIF_EAST_WEST())

        xifdec.o_XIF_2_FRACTAL_NORD_SUD(self.__o_SLAVE_NORD_SUD_FRACTAL())
        self.__i_SLAVE_NORD_SUD_FRACTAL(xifdec.i_FRACTAL_2_XIF_NORD_SUD())

        # Enable debug
        gdbserver.gdbserver.Gdbserver(self, 'gdbserver')        

    
    # Ports to fractalsync

    def __o_SLAVE_EAST_WEST_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_east_west_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_EAST_WEST_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_east_west_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_EAST_WEST_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('east_west_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_EAST_WEST_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'east_west_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')

    def __o_SLAVE_NORD_SUD_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'xif_2_nord_sud_fractal', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_NORD_SUD_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('xif_2_nord_sud_fractal', itf, signature='wire<PortReq<uint32_t>*>')

    def __i_SLAVE_NORD_SUD_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('nord_sud_fractal_2_xif', itf, signature='wire<PortResp<uint32_t>*>',composite_bind=True)

    def i_SLAVE_NORD_SUD_FRACTAL(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'nord_sud_fractal_2_xif', signature='wire<PortResp<uint32_t>*>')

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