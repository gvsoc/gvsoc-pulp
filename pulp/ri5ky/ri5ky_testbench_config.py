#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#

from config_tree import Config, cfg_field
from memory.memory_v3 import MemoryV3Config
from interco.router_v2 import RouterConfig, RouterMapping
from ips.gap.cpu.ri5ky import Ri5kyConfig


class Ri5kyTestbenchConfig(Config):
    """Configuration for the Ri5ky testbench SoC.

    Layout matches hw/ri5ky_gwt/gv_tb/riscv_soc.sv:
      - mem       at 0x0000_0000 (1 MB, shared between fetch and data, latency=0)
      - slow_mem  at 0x4000_0000 (64 KB): SYNCHRONOUS high-latency memory
                  (memory_v3, IO_REQ_DONE + annotated latency). GVSoC-only
                  calibration aid — exercises the sync response path.
      - async_mem at 0x5000_0000 (64 KB): ASYNCHRONOUS high-latency memory
                  (Ri5kyAsyncMem, grants then replies `latency` cycles later),
                  mirroring RTL slow_mem.sv. Drives p.elw's clock-gated
                  park/wake path and the registered misaligned-beat handoff.
      - MMIO      at 0x1000_0000 (4 KB): putchar @ +0, exit @ +4
    """

    mem_base: int = cfg_field(default=0x0000_0000, fmt="hex", dump=True, desc=(
        "Base address of the fast memory"
    ))

    mem_size: int = cfg_field(default=0x10_0000, fmt="hex", dump=True, desc=(
        "Size of the fast memory"
    ))

    slow_mem_base: int = cfg_field(default=0x4000_0000, fmt="hex", dump=True, desc=(
        "Base address of the synchronous slow memory (GVSoC calibration only)"
    ))

    slow_mem_size: int = cfg_field(default=0x1_0000, fmt="hex", dump=True, desc=(
        "Size of the synchronous slow memory"
    ))

    slow_mem_latency: int = cfg_field(default=5, dump=True, desc=(
        "Response latency of the synchronous slow memory"
    ))

    async_mem_base: int = cfg_field(default=0x5000_0000, fmt="hex", dump=True, desc=(
        "Base address of the asynchronous slow memory (mirrors RTL slow_mem.sv)"
    ))

    async_mem_size: int = cfg_field(default=0x1_0000, fmt="hex", dump=True, desc=(
        "Size of the asynchronous slow memory"
    ))

    async_mem_latency: int = cfg_field(default=5, dump=True, desc=(
        "Response latency of the asynchronous slow memory"
    ))

    mmio_base: int = cfg_field(default=0x1000_0000, fmt="hex", dump=True, desc=(
        "Base address of the MMIO peripheral"
    ))

    mmio_size: int = cfg_field(default=0x1000, fmt="hex", dump=True, desc=(
        "Size of the MMIO peripheral window"
    ))

    boot_addr: int = cfg_field(default=0x80, fmt="hex", dump=True, desc=(
        "Boot address (matches RTL BOOT_ADDR)"
    ))

    core: Ri5kyConfig = cfg_field(init=False, desc=(
        "Ri5ky core configuration"
    ))

    mem: MemoryV3Config = cfg_field(init=False, desc=(
        "Backing memory configuration"
    ))

    slow_mem: MemoryV3Config = cfg_field(init=False, desc=(
        "Synchronous slow memory configuration (calibration only)"
    ))

    async_mem: MemoryV3Config = cfg_field(init=False, desc=(
        "Asynchronous slow memory configuration (mirrors RTL slow_mem.sv)"
    ))

    mmio: MemoryV3Config = cfg_field(init=False, desc=(
        "Stub MMIO peripheral (memory-backed for now)"
    ))

    router: RouterConfig = cfg_field(init=False, desc=(
        "Router configuration"
    ))

    mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the fast memory"
    ))

    slow_mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the synchronous slow memory"
    ))

    async_mem_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the asynchronous slow memory"
    ))

    mmio_mapping: RouterMapping = cfg_field(init=False, desc=(
        "Address range of the MMIO peripheral"
    ))

    def __post_init__(self):
        super().__post_init__()
        self.core = Ri5kyConfig(isa='rv32imafc_zfinx', boot_addr=self.boot_addr)
        # latency=1 matches the RTL: response arrives the cycle after the
        # request. Pipelined loads cost 1 cycle each; load-use pays a
        # 1-cycle scoreboard bubble.
        self.mem = MemoryV3Config('mem', size=self.mem_size, atomics=True, latency=1)
        self.slow_mem = MemoryV3Config('slow_mem', size=self.slow_mem_size,
                                       atomics=False, latency=self.slow_mem_latency)
        self.async_mem = MemoryV3Config('async_mem', size=self.async_mem_size,
                                        atomics=False, latency=self.async_mem_latency)
        self.mmio = MemoryV3Config('mmio', size=self.mmio_size, atomics=False, latency=0)
        self.router = RouterConfig(kind='bandwidth')
        self.mem_mapping = RouterMapping(name='mem_mapping',
                                         base=self.mem_base, size=self.mem_size)
        self.slow_mem_mapping = RouterMapping(name='slow_mem_mapping',
                                              base=self.slow_mem_base, size=self.slow_mem_size)
        self.async_mem_mapping = RouterMapping(name='async_mem_mapping',
                                               base=self.async_mem_base, size=self.async_mem_size)
        self.mmio_mapping = RouterMapping(name='mmio_mapping',
                                          base=self.mmio_base, size=self.mmio_size)


class Ri5kyTestbenchBoardConfig(Config):

    frequency: int = cfg_field(default=10000000, dump=True, desc=(
        "Frequency in Hz of the SoC"
    ))

    soc: Ri5kyTestbenchConfig = cfg_field(init=False, desc=(
        "Ri5ky testbench SoC configuration"
    ))

    def __post_init__(self):
        super().__post_init__()
        self.soc = Ri5kyTestbenchConfig('soc')
