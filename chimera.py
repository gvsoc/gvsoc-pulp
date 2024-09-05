# #
# # Copyright (C) 2020 ETH Zurich and University of Bologna
# #
# # Licensed under the Apache License, Version 2.0 (the "License");
# # you may not use this file except in compliance with the License.
# # You may obtain a copy of the License at
# #
# #     http://www.apache.org/licenses/LICENSE-2.0
# #
# # Unless required by applicable law or agreed to in writing, software
# # distributed under the License is distributed on an "AS IS" BASIS,
# # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# # See the License for the specific language governing permissions and
# # limitations under the License.
# #


import gvsoc.runner
import gvsoc.systree 

# import cpu.iss.riscv as iss
import pulp.cpu.iss.pulp_cores as iss

import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
from interco.bus_watchpoint import Bus_watchpoint


class SafetyIsland(gvsoc.systree.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        [args, otherArgs] = parser.parse_known_args()
        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary
        
                # Create a dictionary with memory configuration
        self.memory_config = {
            "data": {
                "remove_offset": 0x00000000,           
                "base": 0x00000000,   
                "size": 0x00010000    
            },
            "inst": {
                "remove_offset": 0x00010000, 
                "base": 0x00010000,   
                "size": 0x00010000  
            }
        }


        l2_tcdm_ico            = router.Router(self, "l2_tcdm_ico")
        l2_private_data_memory = memory.Memory(self, 'private_data_memory',
                                                     size=self.memory_config["data"]["size"])
        l2_private_inst_memory = memory.Memory(self, 'private_inst_memory', 
                                                      size=self.memory_config["inst"]["size"])

        # host = iss.Riscv(self, 'fc', isa='rv32imafdc')
        host = iss.FcCore(self, 'fc')
        host.o_FETCH     (l2_tcdm_ico.i_INPUT    ())
        host.o_DATA      (l2_tcdm_ico.i_INPUT    ())
        host.o_DATA_DEBUG(l2_tcdm_ico.i_INPUT    ())

        # Finally connect an ELF loader, which will execute first and will then
        # send to the core the boot address and notify him he can start
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        loader.o_OUT     (l2_tcdm_ico.i_INPUT    ())
        loader.o_START   (host.i_FETCHEN ())
        loader.o_ENTRY   (host.i_ENTRY   ())

        l2_tcdm_ico.o_MAP(
            l2_private_data_memory.i_INPUT(),
            "data_memory",
            **{
                "base"          : self.memory_config["data"]["base"],
                "remove_offset" : self.memory_config["data"]["remove_offset"],
                "size"          : self.memory_config["data"]["size"]
            }
        )

        l2_tcdm_ico.o_MAP(
            l2_private_inst_memory.i_INPUT(),
            "inst_memory",
            **{
                "base"          : self.memory_config["inst"]["base"],
                "remove_offset" : self.memory_config["inst"]["remove_offset"],
                "size"          : self.memory_config["inst"]["size"]
            }
        )


       
class ChimeraBoard(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):

        super(ChimeraBoard, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        safety_island = SafetyIsland(self, 'safety_island', parser)

        self.bind(clock, 'out', safety_island, 'clock')


class Target(gvsoc.runner.Target):

    gapy_description="Chimera virtual board"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=ChimeraBoard)

