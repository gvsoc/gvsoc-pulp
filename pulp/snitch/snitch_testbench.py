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

#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#

from typing import override

from vp.clock_domain import ClockDomain
from gvsoc.systree import Component
from gvrun.parameter import TargetParameter

from pulp.snitch.snitch_testbench_config import SnitchTestbenchConfig, SnitchTestbenchBoardConfig, SnitchTestbenchMultiBoardConfig
from utils.loader.loader import ElfLoader
from pulp.snitch.snitch_core import SnitchFast
from memory.memory import Memory
from interco.router import Router


class SnitchTestbench(Component):

    def __init__(self, parent: Component, name: str, config: SnitchTestbenchConfig):
        super().__init__(parent, name, config)

        _ = TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        mem_l0     = Memory     ( self, 'mem_l0'    , attributes=config.mem_l0 )
        mem_l1     = Memory     ( self, 'mem_l1'    , attributes=config.mem_l1 )
        mem_l2     = Memory     ( self, 'mem_l2'    , attributes=config.mem_l2 )

        ico        = Router     ( self, 'ico'       , attributes=config.router_sync    )

        host       = SnitchFast ( self, 'core'      , config=config.core )
        loader     = ElfLoader  ( self, 'loader'                         )

        # Direct connections from sync router to memories for synchronous accesses
        ico.o_MAP       ( mem_l0.i_INPUT()    , mapping=config.mem_l0_mapping                      )
        ico.o_MAP       ( mem_l1.i_INPUT()    , mapping=config.mem_l1_mapping                      )
        ico.o_MAP       ( mem_l2.i_INPUT()    , mapping=config.mem_l2_mapping                      )

        loader.o_OUT     ( ico.i_INPUT()    )
        loader.o_START   ( host.i_FETCHEN() )
        loader.o_ENTRY   ( host.i_ENTRY()   )

        host.o_DATA      ( ico.i_INPUT() )
        host.o_FETCH     ( ico.i_INPUT() )

        if config.has_async:
            ico_async  = Router     ( self, 'ico_async' , attributes=config.router_async   )
            ico_async2 = Router     ( self, 'ico_async2', attributes=config.router_async_2 )

            # Additionnal connections to async routers for asynchronous accesses to memories
            ico.o_MAP       ( ico_async.i_INPUT() , mapping=config.async_area_mapping                  )
            ico.o_MAP       ( ico_async.i_INPUT() , mapping=config.async_2_area_mapping, name="async2" )

            # Second level asynchronous router
            ico_async.o_MAP ( mem_l0.i_INPUT()    , mapping=config.mem_l0_async_mapping )
            ico_async.o_MAP ( mem_l1.i_INPUT()    , mapping=config.mem_l1_async_mapping )
            ico_async.o_MAP ( mem_l2.i_INPUT()    , mapping=config.mem_l2_async_mapping )
            ico_async.o_MAP ( ico_async2.i_INPUT(), mapping=config.async_2_area_mapping )

            # Third level asynchronous router
            ico_async2.o_MAP ( mem_l0.i_INPUT()   , mapping=config.mem_l0_2_async_mapping )
            ico_async2.o_MAP ( mem_l1.i_INPUT()   , mapping=config.mem_l1_2_async_mapping )
            ico_async2.o_MAP ( mem_l2.i_INPUT()   , mapping=config.mem_l2_2_async_mapping )

        # Make sure the loader is notified by any executable attached to the hieararchy of this
        # component so that it is automatically loaded
        self.loader: ElfLoader = loader
        self.register_binary_handler(self.handle_binary)

    @override
    def configure(self) -> None:
        # We configure the loader binary now int he configure steps since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

    def handle_binary(self, binary: str):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)


class SnitchTestbenchBoard(Component):

    def __init__(self, parent: Component, name: str, config: SnitchTestbenchBoardConfig):

        super().__init__(parent, name, config)

        self.set_target_name('snitch.testbench')

        clock = ClockDomain     ( self, 'clock', frequency=config.frequency )
        soc   = SnitchTestbench ( self, 'soc',   config.soc                 )

        clock.o_CLOCK ( soc.i_CLOCK() )

class SnitchTestbenchMultiBoard(Component):

    def __init__(self, parent: Component, name: str, config: SnitchTestbenchMultiBoardConfig):

        super().__init__(parent, name, config)

        boards: list[SnitchTestbenchBoard] = []
        for i in range(0, config.nb_boards):
            boards.append(SnitchTestbenchBoard(self, f'board_{i}', config.boards[i]))
