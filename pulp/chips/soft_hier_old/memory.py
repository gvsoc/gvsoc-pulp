#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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
from typing import Annotated, ClassVar
from config_tree import Config, cfg_field
from gvrun.runtime import Runtime
from vp.power_config import PowerSourceConfig
from pulp.chips.soft_hier_old.power_models import (
    memory_power_sources,
    power_source_config,
    validate_power_profile,
)


class MemoryPowerConfig(Config):
    """Power tables of the memory, computed from its size and the selected power profile."""

    _defer_parent_init: ClassVar[bool] = True

    background: PowerSourceConfig = cfg_field(default_factory=PowerSourceConfig, desc=(
        "Background power and leakage, registered as power source 'leakage'"))
    access_byte: PowerSourceConfig = cfg_field(default_factory=PowerSourceConfig, desc=(
        "Energy of one accessed byte"))


class MemoryConfig(Config):
    """Configuration of the SoftHier (old) memory."""

    size: int = cfg_field(default=0, fmt="hex", dump=True, desc="Size of the memory, in bytes")
    stim_file: Annotated[str, Runtime] = cfg_field(default="", dump=True, desc=(
        "Raw binary file preloaded at the beginning of the memory, empty for none"))
    power_trigger: bool = cfg_field(default=False, dump=True, desc=(
        "True if the memory triggers power report generation on dedicated accesses"))
    check: bool = cfg_field(default=False, dump=True, desc=(
        "True to check accesses to uninitialized data"))
    width_bits: int = cfg_field(default=0, dump=True, desc=(
        "Log2 of the bandwidth, in bytes per cycle, no bandwidth model if negative"))
    align: int = cfg_field(default=0, dump=True, desc="Alignment of the allocated memory, 0 for none")
    latency: int = cfg_field(default=0, dump=True, desc="Extra latency added to every request")
    memcheck_id: int = cfg_field(default=0, dump=True, desc=(
        "Global memory check id, -1 if the memory does not track buffer overflows"))
    memcheck_base: int = cfg_field(default=0, fmt="hex", dump=True, desc=(
        "Absolute base of memory where buffer overflow is tracked"))
    memcheck_virtual_base: int = cfg_field(default=0, fmt="hex", dump=True, desc=(
        "Absolute virtual base of allocated buffers"))
    memcheck_expansion_factor: int = cfg_field(default=0, dump=True, desc=(
        "Extra size used to track buffer overflow"))
    power: MemoryPowerConfig = cfg_field(default_factory=MemoryPowerConfig, desc="Power tables")


class Memory(gvsoc.systree.Component):
    """Memory array

    This models a simple memory model.
    It can be preloaded with initial data.
    It contains a timing model of a bandwidth, reported through latency.
    It can support riscv atomics.

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    size: int
        The size of the memory in bytes.
    width_log2: int
        The log2 of the bandwidth to the memory, i.e. the number of bytes it can transfer per cycle.
        No timing model is applied if it is zero and the memory is then having an infinite
        bandwidth.
    stim_file: str
        The path to a binary file which should be preloaded at beginning of the memory. The format
        is a raw binary, and is loaded with an fread.
    power_trigger: bool
        True if the memory should trigger power report generation based on dedicated accesses.
    align: int
        Specify a required alignment for the allocated memory used for the memory model.
    atomics: bool
        True if the memory should support riscv atomics. Since this is slowing down the model, it
        should be set to True only if needed.
    latency: int
        Specify extra latency which will be added to any incoming request.
    memcheck_id: int
        If this memory is used to track buffer overflow, this gives the global memory check id.
    memcheck_base: int
        Absolute base of memory where buffer overflow is tracked.
    memcheck_virtual_base: int
        Absolute virtual base of allocated buffers.
    memcheck_expansion_factor: int
        Extra size used to track buffer overflow.
    """
    def __init__(self, parent: gvsoc.systree.Component, name: str, size: int, width_log2: int=2,
            stim_file: str=None, power_trigger: bool=False,
            align: int=0, atomics: bool=False, latency=0, memcheck_id: int=-1, memcheck_base: int=0,
            memcheck_virtual_base: int=0, memcheck_expansion_factor: int=5, tech_node: str="5nm",
            power_profile: str="constant"):

        power_profile = validate_power_profile(power_profile)

        config = MemoryConfig(size=int(size), stim_file=stim_file if stim_file is not None else "",
            power_trigger=power_trigger, width_bits=width_log2, align=align, latency=latency,
            memcheck_id=memcheck_id, memcheck_base=memcheck_base,
            memcheck_virtual_base=memcheck_virtual_base,
            memcheck_expansion_factor=memcheck_expansion_factor)

        power_sources = memory_power_sources(
            size_bytes=size,
            tech_node=tech_node,
            profile=power_profile,
        )
        config.power.background = power_source_config('background', power_sources['background'])
        config.power.access_byte = power_source_config('access_byte', power_sources['access_byte'])

        super().__init__(parent, name, config=config)

        self.add_sources(['pulp/chips/soft_hier_old/memory.cpp'])

        # Since atomics are slowing down the model, this is better to compile the support only
        # if needed. Note that the framework will take care of compiling this model twice
        # if both memories with and without atomics are instantiated.
        if atomics:
            self.add_c_flags(['-DCONFIG_ATOMICS=1'])



    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        """Returns the input port.

        Incoming requests to be handled by the memory should be sent to this port.\n
        It instantiates a port of type vp::IoSlave.\n

        Returns
        ----------
        gvsoc.systree.SlaveItf
            The slave interface
        """
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
