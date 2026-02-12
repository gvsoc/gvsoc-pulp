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

from dataclasses import dataclass

from gvrun.config import Config, cfg_field
from memory.memory_config import MemoryConfig
from interco.router_config import RouterMapping, RouterConfig
from pulp.snitch.snitch_core_config import SnitchCoreConfig

# Disable repr to avoid displaying long name in tree
@dataclass(repr=False)
class SnitchTestbenchConfig(Config):
    """Configuration for Snitch testbench.

    This class defines the configuration parameters for a Snitch testbench,
    including memory hierarchies, routing, and asynchronous path options.

    Attributes:
    ----------
    base_addr: int
        Base address of the module.
    has_async: bool
        Instantiate asynchronous path.
    """

    base_addr: int = cfg_field(default=0x8000_0000, fmt="hex", dump=True, desc=(
        "Base address of the module"
    ))

    has_async: bool = cfg_field(default=True, dump=True, desc=(
        "Instantiate asynchronous path"
    ))

    core: SnitchCoreConfig = cfg_field(init=False, desc=(
        "Main core configuration"
    ))

    mem_l0: MemoryConfig = cfg_field(init=False, desc=(
        "Memory with low latency"
    ))

    mem_l1: MemoryConfig = cfg_field(init=False, desc=(
        "Memory with mid latency"
    ))

    mem_l2: MemoryConfig = cfg_field(init=False, desc=(
        "Memory with high latency"
    ))

    router_sync: RouterConfig = cfg_field(init=False, desc=(
        "Router for synchronous area"
    ))

    router_async: RouterConfig = cfg_field(init=False, desc=(
        "Router for asynchronous area"
    ))

    router_async_2: RouterConfig = cfg_field(init=False, desc=(
        "Router for second asynchronous area"
    ))

    mem_l0_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the memory for synchronous accesses and low latency"
    ))

    mem_l1_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the memory for synchronous accesses and mid latency"
    ))

    mem_l2_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the memory for synchronous accesses and high latency"
    ))

    mem_l0_async_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the memory for asynchronous accesses and low latency"
    ))

    mem_l1_async_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the memory for asynchronous accesses and mid latency"
    ))

    mem_l2_async_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the memory for asynchronous accesses and high latency"
    ))

    async_area_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the memories for asynchronous accesses"
    ))

    mem_l0_2_async_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the second memory for asynchronous accesses and low latency"
    ))

    mem_l1_2_async_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the second memory for asynchronous accesses and mid latency"
    ))

    mem_l2_2_async_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the second memory for asynchronous accesses and high latency"
    ))

    async_2_area_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the second memories for asynchronous accesses"
    ))

    def __post_init__(self):
        super().__post_init__()

        self.core = SnitchCoreConfig(isa='rv32imfdca', nb_outstanding=8)
        self.mem_l0 = MemoryConfig(self, 'mem_l0', size=0x10_0000, atomics=True, latency=0)
        self.mem_l1 = MemoryConfig(self, 'mem_l1', size=0x10_0000, atomics=True, latency=10)
        self.mem_l2 = MemoryConfig(self, 'mem_l2', size=0x10_0000, atomics=True, latency=1000)
        self.router_sync = RouterConfig(synchronous=True)
        self.mem_l0_mapping = RouterMapping(self, 'mem_l0_mapping', self.base_addr + 0x0000_0000, 0x10_0000)
        self.mem_l1_mapping = RouterMapping(self, 'mem_l1_mapping', self.base_addr + 0x0010_0000, 0x10_0000)
        self.mem_l2_mapping = RouterMapping(self, 'mem_l2_mapping', self.base_addr + 0x0020_0000, 0x10_0000)

        if self.has_async:
            self.router_async = RouterConfig(synchronous=False, max_input_pending_size=4)
            self.router_async_2 = RouterConfig(synchronous=True, max_input_pending_size=4, bandwidth=1)
            self.mem_l0_async_mapping = RouterMapping(self, 'mem_l0_async_mapping', self.base_addr + 0x1000_0000, 0x10_0000, latency=0)
            self.mem_l1_async_mapping = RouterMapping(self, 'mem_l1_async_mapping', self.base_addr + 0x1010_0000, 0x10_0000, latency=10)
            self.mem_l2_async_mapping = RouterMapping(self, 'mem_l2_async_mapping', self.base_addr + 0x1020_0000, 0x10_0000, latency=1000)
            self.async_area_mapping   = RouterMapping(self, 'async_area_mapping', self.base_addr + 0x1000_0000, 0x30_0000, remove_base=False)
            self.mem_l0_2_async_mapping = RouterMapping(self, 'mem_l0_2_async_mapping', self.base_addr + 0x2000_0000, 0x10_0000, latency=0)
            self.mem_l1_2_async_mapping = RouterMapping(self, 'mem_l1_2_async_mapping', self.base_addr + 0x2010_0000, 0x10_0000, latency=10)
            self.mem_l2_2_async_mapping = RouterMapping(self, 'mem_l2_2_async_mapping', self.base_addr + 0x2020_0000, 0x10_0000, latency=1000)
            self.async_2_area_mapping   = RouterMapping(self, 'async_2_area_mapping', self.base_addr + 0x2000_0000, 0x30_0000, remove_base=False)


@dataclass(repr=False)
class SnitchTestbenchBoardConfig(Config):

    frequency: int = cfg_field(default=10000000, dump=True, desc=(
        "Frequency in Hz of the SoC"
    ))

    base_addr: int = cfg_field(default=0x8000_0000, dump=True, inlined_dump=True, fmt="hex", desc=(
        "Base address of the module"
    ))

    soc: SnitchTestbenchConfig = cfg_field(init=False, desc=(
        "Snitch testbench configuration"
    ))

    def __post_init__(self):
        super().__post_init__()
        self.soc = SnitchTestbenchConfig(self, 'soc', base_addr=self.base_addr)


@dataclass(repr=False)
class SnitchTestbenchMultiBoardConfig(Config):

    nb_boards: int = cfg_field(default=2, dump=True, desc=(
        "Number of boards"
    ))

    boards: list[SnitchTestbenchBoardConfig] = cfg_field(init=False, desc=(
        "Boards configs"
    ))

    def __post_init__(self):
        super().__post_init__()

        self.boards = []
        base_addr = 0x8000_0000
        for id in range(0, self.nb_boards):
            self.boards.append(SnitchTestbenchBoardConfig(self, f'board_{id}', base_addr=base_addr))
            base_addr += 0x3000_0000
