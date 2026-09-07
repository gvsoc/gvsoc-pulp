#
# Copyright (C) 2026 ETH Zurich and University of Bologna
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

"""Model-level testbench for the single-stream AXI-to-AXI iDMA (RegDmaV3).

The DMA is configured like the Spatz cluster DMA: 512-bit data path, 3
outstanding bursts, 4 KiB pages, coupled legalizer and RAW coupler. Two
memories (sources in one, destinations in the other, so a single-ported
memory does not serialize the two sides) sit behind a beat router of the AXI
width which takes the DMA's read and write masters and the tester. The tester (shared with the cluster
testbench) programs the register port, launches by reading NEXT_ID, waits for
DONE_ID and counts the completion events, then reads the destinations back.
"""

from __future__ import annotations

import os
import sys

import gvsoc.systree
import gvsoc.runner
import vp.clock_domain
from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import (Router, RouterConfig, RouterMapping, KIND_UNTIMED, KIND_BEAT, KIND_BANDWIDTH)
from gvrun.parameter import TargetParameter
from ips.pulp.idma_v3.reg_dma import RegDmaV3
from ips.pulp.idma_v3.reg_dma_config import RegDmaV3Config
from ips.pulp.idma_v3.snitch_dma import SnitchDmaV3
from ips.pulp.idma_v3.snitch_dma_config import SnitchDmaV3Config
from xdma_tester import XdmaTester

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'idma_v3_cluster'))
from cluster_dma_tester import ClusterDmaTester  # noqa: E402

MEM_A_BASE = 0x10000000
MEM_B_BASE = 0x20000000
MEM_SIZE   = 0x80000
REGS_BASE = 0x30000000
REGS_SIZE = 0x400

AXI_WIDTH = 64

PROT_AXI = 0


def transfer(src, dst, length, *, src_stride=0, dst_stride=0, reps=1, nd=0,
             src_stride_3=0, dst_stride_3=0, reps_3=1, seed=0xa5, expect_denied=0,
             decouple_rw=0, decouple_aw=0):
    return {'stream': 0, 'src_prot': PROT_AXI, 'dst_prot': PROT_AXI,
            'src': src, 'dst': dst, 'length': length,
            'src_stride': src_stride, 'dst_stride': dst_stride, 'reps': reps, 'nd': nd,
            'src_stride_3': src_stride_3, 'dst_stride_3': dst_stride_3, 'reps_3': reps_3,
            'seed': seed, 'expect_denied': expect_denied,
            'decouple_rw': decouple_rw, 'decouple_aw': decouple_aw}


def build_case(case_name: str) -> dict:
    """Return the transfers of a case plus its bench options."""
    common = dict(latency=1, raw_coupling=1, max_pending=4, axi_width=AXI_WIDTH, xdma=0)
    src = MEM_A_BASE
    dst = MEM_B_BASE

    if case_name == '1d_4k_aligned':
        # One 4 KiB burst of 64 beats on each side
        return {**common, 'transfers': [transfer(src, dst, 4096, seed=0x11)]}
    if case_name == '1d_page_cross':
        # The source crosses a 4 KiB page after 2 KiB: the coupled legalizer
        # cuts both sides at 2 KiB
        return {**common, 'transfers': [transfer(src + 0x800, dst, 4096, seed=0x12)]}
    if case_name == 'decouple_rw':
        # Same geometry with the sides decoupled: two read bursts, one write
        return {**common, 'transfers': [
            transfer(src + 0x800, dst, 4096, seed=0x13, decouple_rw=1)]}
    if case_name == '1d_unaligned':
        # Both ends misaligned differently: byte shift through the buffer
        return {**common, 'transfers': [transfer(src + 5, dst + 17, 1000, seed=0x14)]}
    if case_name == '1d_small':
        return {**common, 'transfers': [transfer(src, dst, 64, seed=0x15)]}
    if case_name == '2d_16x256':
        return {**common, 'transfers': [
            transfer(src, dst, 256, src_stride=512, dst_stride=256, reps=16, nd=1, seed=0x21)]}
    if case_name == '1d_32k':
        return {**common, 'transfers': [transfer(src, dst, 32768, seed=0x31)]}
    if case_name == 'queue_full':
        # Five 32 KiB launches into a request FIFO of three: the fifth is
        # denied until a slot frees
        return {**common, 'transfers': [
            transfer(src + i * 0x8000, dst + i * 0x8000, 32768, seed=0x40 + i,
                     expect_denied=1 if i == 4 else 0)
            for i in range(5)]}
    if case_name == 'zero_len':
        return {**common, 'transfers': [
            transfer(src, dst, 4096, seed=0x51),
            transfer(src + 0x1000, dst + 0x1000, 0, seed=0x52)]}
    if case_name == 'lat_10':
        return {**common, 'latency': 10, 'transfers': [transfer(src, dst, 32768, seed=0x61)]}
    if case_name == 'w8_1d_4k':
        # el1-like width for cross-checking the topology
        return {**common, 'axi_width': 8, 'max_pending': 8, 'transfers': [transfer(src, dst, 4096, seed=0x81)]}
    if case_name == 'speed_big':
        return {**common, 'axi_width': 8, 'max_pending': 8, 'raw_coupling': 0, 'quit': 5_250_000,
            'transfers': [
            transfer(src, dst, 0x10000, src_stride=0, dst_stride=0, reps=640, nd=1, seed=0x5b)]}
    if case_name == 'speed_big64':
        return {**common, 'raw_coupling': 0, 'bw_router': 1, 'quit': 700_000, 'transfers': [
            transfer(src, dst, 0x10000, src_stride=0, dst_stride=0, reps=640, nd=1, seed=0x5b)]}
    if case_name == 'speed_big_bw':
        return {**common, 'axi_width': 8, 'max_pending': 8, 'raw_coupling': 0, 'bw_router': 1,
            'quit': 5_250_000, 'burst_len': 9, 'transfers': [
            transfer(src, dst, 0x10000, src_stride=0, dst_stride=0, reps=640, nd=1, seed=0x5b)]}
    if case_name == 'speed_small_bw':
        return {**common, 'axi_width': 8, 'max_pending': 8, 'raw_coupling': 0, 'bw_router': 1,
            'transfers': [transfer(src, dst, 64, seed=0x5c)]}
    if case_name == 'speed_small':
        return {**common, 'axi_width': 8, 'max_pending': 8, 'raw_coupling': 0, 'transfers': [
            transfer(src, dst, 64, seed=0x5c)]}
    if case_name == 'no_coupler':
        # Same as 1d_32k without the RAW coupler: the AWs leave at once
        return {**common, 'raw_coupling': 0, 'transfers': [transfer(src, dst, 32768, seed=0x71)]}

    # The same transfers driven through the xdma offload front-end (SnitchDmaV3)
    if case_name.startswith('xdma_'):
        spec = build_case(case_name[len('xdma_'):])
        return {**spec, 'xdma': 1}
    raise ValueError(f'Unknown case: {case_name!r}')


def _pattern(seed, offset):
    return (seed + offset) & 0xff


def _region(addr):
    if MEM_A_BASE <= addr < MEM_A_BASE + MEM_SIZE:
        return 'a', MEM_A_BASE
    if MEM_B_BASE <= addr < MEM_B_BASE + MEM_SIZE:
        return 'b', MEM_B_BASE
    raise ValueError(f'address 0x{addr:x} outside the memories')


def _source_bytes(transfers):
    """Preload images of the memories holding the sources of the transfers.

    Byte ``i`` of transfer ``t`` (in transfer order, lines then pages) is
    ``(seed + i) & 0xff``.
    """
    images = {'a': bytearray(MEM_SIZE), 'b': bytearray(MEM_SIZE)}
    for t in transfers:
        if t['length'] == 0:
            continue
        name, base = _region(t['src'])
        image = images[name]
        reps = t['reps'] if t['nd'] >= 1 and t['reps'] != 0 else 1
        reps_3 = t['reps_3'] if t['nd'] >= 2 and t['reps_3'] != 0 else 1
        addr = t['src']
        offset = 0
        for k in range(reps_3):
            for j in range(reps):
                for b in range(t['length']):
                    image[addr - base + b] = _pattern(t['seed'], offset)
                    offset += 1
                if j == reps - 1:
                    addr += t['src_stride_3']
                else:
                    addr += t['src_stride']
    return images


def _write_stim(work_dir, name, blob):
    os.makedirs(work_dir, exist_ok=True)
    path = os.path.join(work_dir, f'{name}_stim.bin')
    with open(path, 'wb') as f:
        f.write(blob)
    return path


class Chip(gvsoc.systree.Component):

    def __init__(self, parent, name=None):
        super().__init__(parent, name)

        case = TargetParameter(
            self, name='case', value='1d_4k_aligned',
            description='idma_v3_axi model-level test case', cast=str,
        ).get_value()
        spec = build_case(case)

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100_000_000)

        work_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), 'build', 'stims'))
        images = _source_bytes(spec['transfers'])
        stims = {name: _write_stim(work_dir, f'{case}_{name}', bytes(blob))
                 for name, blob in images.items()}

        mem_a = Memory(self, 'mem_a', config=MemoryV3Config(
            size=MEM_SIZE, latency=spec['latency'], stim_file=stims['a']))
        mem_b = Memory(self, 'mem_b', config=MemoryV3Config(
            size=MEM_SIZE, latency=spec['latency'], stim_file=stims['b']))
        clock.o_CLOCK(mem_a.i_CLOCK())
        clock.o_CLOCK(mem_b.i_CLOCK())

        axi_width = spec['axi_width']
        if spec['xdma']:
            dma_config = SnitchDmaV3Config(
                axi_width=axi_width, num_ax_in_flight=3, buffer_depth=3, meta_fifo_depth=6,
                burst_len=spec.get('burst_len', 6), raw_coupling=bool(spec['raw_coupling']),
                req_fifo_depth=3, nb_dims=2)
            dma = SnitchDmaV3(self, 'dma', config=dma_config)
            tester = XdmaTester(self, 'tester', transfers=spec['transfers'],
                quit_after_cycles=spec.get('quit', 1_000_000))
            tester.o_OFFLOAD(dma.i_OFFLOAD())
            dma.o_OFFLOAD_GRANT(tester.i_OFFLOAD_GRANT())
            dma.o_IRQ(tester.i_IRQ())
        else:
            dma_config = RegDmaV3Config(
                axi_width=axi_width, num_ax_in_flight=3, buffer_depth=3, meta_fifo_depth=6,
                burst_len=6, raw_coupling=bool(spec['raw_coupling']), req_fifo_depth=3, nb_dims=2)
            dma = RegDmaV3(self, 'dma', config=dma_config)
            tester = ClusterDmaTester(self, 'tester',
                regs_addr=REGS_BASE,
                transfers=spec['transfers'],
                nb_events=0,
                nb_streams=1,
            )
            dma.o_IRQ(tester.i_FC_EVENT())
        clock.o_CLOCK(dma.i_CLOCK())
        clock.o_CLOCK(tester.i_CLOCK())

        # Wide AXI stand-in: beat router of the AXI width
        if spec.get('bw_router'):
            axi = Router(self, 'axi', config=RouterConfig(kind=KIND_BANDWIDTH, bandwidth=axi_width,
                latency=1))
        else:
            axi = Router(self, 'axi', config=RouterConfig(kind=KIND_BEAT, width=axi_width,
                max_pending_bursts_per_input=spec['max_pending']))
        clock.o_CLOCK(axi.i_CLOCK())
        axi.o_MAP(mem_a.i_INPUT(), mapping=RouterMapping(name='mem_a', base=MEM_A_BASE, size=MEM_SIZE))
        axi.o_MAP(mem_b.i_INPUT(), mapping=RouterMapping(name='mem_b', base=MEM_B_BASE, size=MEM_SIZE))
        dma.o_AXI_READ(axi.i_INPUT(0))
        dma.o_AXI_WRITE(axi.i_INPUT(1))

        # Tester paths: registers directly, memory through the AXI
        tester_ico = Router(self, 'tester_ico', config=RouterConfig(kind=KIND_UNTIMED))
        clock.o_CLOCK(tester_ico.i_CLOCK())
        tester_ico.o_MAP(axi.i_INPUT(2),
            mapping=RouterMapping(name='mem_a', base=MEM_A_BASE, size=MEM_SIZE, remove_base=False))
        tester_ico.o_MAP(axi.i_INPUT(2),
            mapping=RouterMapping(name='mem_b', base=MEM_B_BASE, size=MEM_SIZE, remove_base=False))
        if not spec['xdma']:
            tester_ico.o_MAP(dma.i_INPUT(),
                mapping=RouterMapping(name='regs', base=REGS_BASE, size=REGS_SIZE))
        tester.o_MEM(tester_ico.i_INPUT(0))


class Target(gvsoc.runner.Target):
    gapy_description = 'idma_v3_axi model-level testbench'
    model = Chip
    name = 'test'
