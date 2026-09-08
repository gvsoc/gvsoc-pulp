# SPDX-License-Identifier: Apache-2.0
"""Spatz Sparse-DMA / DRAMSys calibration board, using pulp/pulp/idma.

The memory map and binaries match spatz_cluster.default.dram.hjson. This
board deliberately uses the io-v1 fabric required by that iDMA implementation.
"""
import json
import os
from types import SimpleNamespace

import gvsoc.runner
import gvsoc.systree
from elftools.elf.elffile import ELFFile
from vp.clock_domain import Clock_domain
from memory.memory import Memory
from memory.dramsys import Dramsys
from interco.router import Router
from cache.cache import Cache
from utils.loader.loader import ElfLoader
from pulp.snitch.snitch_core import SnitchFast
from pulp.snitch.snitch_cluster.snitch_cluster import SnitchClusterTcdm
from pulp.snitch.snitch_cluster.spatz.cluster_registers import ClusterRegisters
from pulp.snitch.zero_mem import ZeroMem
from pulp.idma.snitch_dma import SnitchDma


class Board(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, options=options)
        args, _ = parser.parse_known_args()
        binary = getattr(args, 'binary', None)
        entry = 0
        if binary:
            with open(binary, 'rb') as stream:
                entry = ELFFile(stream)['e_entry']
        settings = dict(axi_latency=4, axi_bandwidth=0, core_request_latency=1, tcdm_latency=1,
                        index_path_latency=0, icache_refill_latency=2,
                        jump_stall_cycles=0,
                        l0_sets_bits=0, l0_ways_bits=3,
                        transfer_queue_size=64, burst_queue_size=64)
        if os.environ.get('SPARSE_DMA_CONFIG'):
            with open(os.environ['SPARSE_DMA_CONFIG']) as stream:
                settings.update(json.load(stream))
        self.add_property('calibration', settings)

        clock = Clock_domain(self, 'clock', frequency=1_000_000_000)
        chip = gvsoc.systree.Component(self, 'chip')
        self.bind(clock, 'out', chip, 'clock')
        wide = Router(chip, 'wide_axi', bandwidth=settings['axi_bandwidth'], latency=settings['axi_latency'])
        narrow = Router(chip, 'narrow_axi', bandwidth=8)
        rom = Memory(chip, 'rom', size=0x10000, latency=1,
                     stim_file=self.get_file_path('pulp/snitch/bootrom_spatz.bin'))
        dram = Dramsys(chip, 'ddr', dram_type='HBM2E-3600.json')
        dram_bridge = gvsoc.systree.Component(chip, 'dram_bridge')
        dram_bridge.add_sources(['pulp/snitch/sparse_dma_dram_bridge.cpp'])
        dram_bridge.add_property('max_inflight', 64)
        chip.bind(dram_bridge, 'output', dram, 'input')
        # Keep the io-v1 fabric endpoints in one binding scope. TCDM itself
        # forwards its ports to the banked interconnect below this scope.
        cluster = chip
        arch = SimpleNamespace(area=SimpleNamespace(base=0x100000, size=0x20000),
                               nb_superbanks=2, nb_banks_per_superbank=8,
                               bank_width=8, bank_size=8192, nb_masters=12)
        tcdm = SnitchClusterTcdm(cluster, 'tcdm', arch)
        for number in range(16):
            tcdm.get_component(f'bank_{number}').add_property('latency', settings['tcdm_latency'])
        registers = ClusterRegisters(cluster, 'registers', nb_cores=2, boot_addr=entry, eoc=True)
        uart = gvsoc.systree.Component(chip, 'uart')
        uart.add_sources(['pulp/snitch/sparse_dma_uart.cpp'])
        narrow.o_MAP(gvsoc.systree.SlaveItf(uart, 'input', signature='io'),
                     base=0xa0000000, size=0x1000, rm_base=True, name='uart')
        zero = ZeroMem(cluster, 'zero', size=0x10000)
        l1 = Cache(cluster, 'l1_icache', nb_sets_bits=6, nb_ways_bits=1,
                   line_size_bits=5, refill_latency=settings['icache_refill_latency'], enabled=True)
        l1.o_REFILL(wide.i_INPUT())
        wide.o_MAP(gvsoc.systree.SlaveItf(dram_bridge, 'input', signature='io'),
                   base=0x80000000, size=0x80000000, rm_base=True, name='dram')
        wide.o_MAP(rom.i_INPUT(), base=0x1000, size=0x10000, rm_base=True, name='rom')
        wide.o_MAP(tcdm.i_DMA_INPUT(), base=0x100000, size=0x20000, rm_base=True, name='tcdm')
        narrow.o_MAP(registers.i_INPUT(), base=0x120000, size=0x10000, rm_base=True, name='registers')
        narrow.o_MAP(zero.i_INPUT(), base=0x130000, size=0x10000, rm_base=True, name='zero')
        narrow.o_MAP(wide.i_INPUT())
        cores = []
        for number in range(2):
            # The unchanged runtime writes tohost before the EOC register.
            # Match RTL termination through that register; HTIF polling can
            # otherwise quit first, depending on the polling phase.
            core = SnitchFast(cluster, f'pe{number}', isa='rv32imafdcv',
                              fetch_enable=False, boot_addr=0x1000, core_id=number,
                              htif=False, binaries=[binary] if binary else [],
                              inc_spatz=True, spatz_nb_lanes=4, spatz_lane_width=8)
            core.add_property('jump_stall_cycles', settings['jump_stall_cycles'])
            cores.append(core)
            demux = Router(cluster, f'core{number}_demux', bandwidth=8,
                           latency=settings['core_request_latency'])
            demux.o_MAP(tcdm.i_INPUT(number*5), base=0x100000, size=0x20000, rm_base=True, name='tcdm')
            demux.o_MAP(registers.i_CORE_INPUT(number), base=0x120000, size=0x10000,
                        rm_base=True, name='registers')
            demux.o_MAP(narrow.i_INPUT())
            core.o_DATA(demux.i_INPUT())
            for lane in range(4):
                core.o_VLSU(lane, tcdm.i_INPUT(number*5+lane+1))
            # snitch_icache_l0 compares all eight line tags in parallel.
            l0 = Cache(cluster, f'l0_icache_{number}',
                       nb_sets_bits=settings['l0_sets_bits'], nb_ways_bits=settings['l0_ways_bits'],
                       line_size_bits=5, enabled=True)
            l0.add_property('round_robin', True)
            prefetch = gvsoc.systree.Component(cluster, f'icache_prefetch_{number}')
            prefetch.add_sources(['pulp/snitch/sparse_dma_icache_prefetch.cpp'])
            chip.bind(prefetch, 'output', l0, 'input')
            core.o_FETCH(gvsoc.systree.SlaveItf(prefetch, 'input', signature='io'))
            l0.o_REFILL(l1.i_INPUT())
            core.o_FLUSH_CACHE(l0.i_FLUSH())
            l0.o_FLUSH_ACK(core.i_FLUSH_CACHE_ACK())
            core.o_BARRIER_REQ(registers.i_BARRIER_ACK(number))
            chip.bind(registers, 'barrier_ack', core, 'barrier_ack')
            registers.o_EXTERNAL_IRQ(number, core.i_IRQ(19))

        dma = SnitchDma(cluster, 'idma', loc_base=0x100000, loc_size=0x20000,
                        tcdm_width=64, gather_enable=True,
                        transfer_queue_size=settings['transfer_queue_size'],
                        burst_queue_size=settings['burst_queue_size'])
        cores[0].o_OFFLOAD(dma.i_OFFLOAD())
        dma.o_OFFLOAD_GRANT(cores[0].i_OFFLOAD_GRANT())
        dma.o_AXI(wide.i_INPUT())
        dma.o_TCDM(tcdm.i_DMA_INPUT())
        # RTL connects core_idx_req directly to the TCDM crossbar; the bank
        # already accounts for its one-cycle response, without a core request cut.
        index = Router(cluster, 'index_path', bandwidth=8, latency=settings['index_path_latency'])
        index.o_MAP(tcdm.i_INPUT(10), base=0x100000, size=0x20000, rm_base=True)
        dma.o_INDEX(index.i_INPUT())

        loader = ElfLoader(chip, 'loader', binary=binary, entry_addr=0x120058, preload=True)
        loader.o_OUT(narrow.i_INPUT())
        for core in cores:
            loader.o_START(core.i_FETCHEN())
            loader.o_START(core.i_IRQ(11))


class Target(gvsoc.runner.Target):
    def __init__(self, parser, options):
        super().__init__(parser, options, model=Board,
                         description='RTL Sparse-DMA gather calibration with DRAMSys HBM2E-3600')
