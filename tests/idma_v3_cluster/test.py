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

"""Model-level testbench for the two-stream cluster iDMA (ClusterDmaV3).

Topology, mirroring the block's place in a cluster:

* ``tcdm``: one memory (latency 1) behind an untimed router which takes the
  DMA's four narrow TCDM ports and the tester, standing for the TCDM
  crossbar: every port sees the same bytes and answers inline.
* ``l2``: one memory behind a beat router of the AXI width which takes the
  DMA's AXI read and write masters and the tester, standing for the cluster
  AXI and the SoC side.
* The tester programs the register port, launches by reading NEXT_ID, waits
  for DONE_ID and counts the completion events, then reads the destinations
  back.

Each case is a list of transfers; ``build_case`` documents them.
"""

from __future__ import annotations

import os

import gvsoc.systree
import gvsoc.runner
import vp.clock_domain
from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import (Router, RouterConfig, RouterMapping, KIND_UNTIMED, KIND_BEAT)
from gvrun.parameter import TargetParameter
from ips.pulp.idma_v3.cluster_dma import ClusterDmaV3
from ips.pulp.idma_v3.cluster_dma_config import ClusterDmaV3Config
from cluster_dma_tester import ClusterDmaTester


TCDM_BASE = 0x10000000
TCDM_SIZE = 0x20000
L2_BASE   = 0x1c000000
L2_SIZE   = 0x40000
REGS_BASE = 0x10201800
REGS_SIZE = 0x400

PROT_AXI = 0
PROT_OBI = 1

# Streams and their protocol pairs
S0_L1_TO_L2 = dict(stream=0, src_prot=PROT_OBI, dst_prot=PROT_AXI)
S1_L2_TO_L1 = dict(stream=1, src_prot=PROT_AXI, dst_prot=PROT_OBI)
S1_L1_TO_L1 = dict(stream=1, src_prot=PROT_OBI, dst_prot=PROT_OBI)


def transfer(kind, src, dst, length, *, src_stride=0, dst_stride=0, reps=1, nd=0,
             src_stride_3=0, dst_stride_3=0, reps_3=1, seed=0xa5, expect_denied=0):
    return {**kind, 'src': src, 'dst': dst, 'length': length,
            'src_stride': src_stride, 'dst_stride': dst_stride, 'reps': reps, 'nd': nd,
            'src_stride_3': src_stride_3, 'dst_stride_3': dst_stride_3, 'reps_3': reps_3,
            'seed': seed, 'expect_denied': expect_denied}


def build_case(case_name: str) -> dict:
    """Return the transfers of a case plus its bench options."""
    common = dict(l2_latency=1, tcdm_latency=1, enable_gate=0, port=0)
    l1 = TCDM_BASE
    l2 = L2_BASE

    if case_name == 's1_1d_256':
        return {**common, 'transfers': [transfer(S1_L2_TO_L1, l2, l1, 256, seed=0x11)]}

    if case_name == 's0_1d_256':
        return {**common, 'transfers': [transfer(S0_L1_TO_L2, l1, l2, 256, seed=0x22)]}

    if case_name == 's1_1d_4k':
        return {**common, 'transfers': [transfer(S1_L2_TO_L1, l2, l1, 4096, seed=0x14)]}

    if case_name == 's0_1d_4k':
        return {**common, 'transfers': [transfer(S0_L1_TO_L2, l1, l2, 4096, seed=0x24)]}

    if case_name == 's1_unaligned':
        # Source and destination misaligned differently: one extra word on
        # each side
        return {**common, 'transfers': [
            transfer(S1_L2_TO_L1, l2 + 3, l1 + 5, 253, seed=0x33)]}

    if case_name == 's0_unaligned':
        return {**common, 'transfers': [
            transfer(S0_L1_TO_L2, l1 + 6, l2 + 1, 250, seed=0x34)]}

    if case_name == 's0_2d_16x64':
        # 16 lines of 64 B gathered from a 256 B stride into a linear buffer
        return {**common, 'transfers': [
            transfer(S0_L1_TO_L2, l1, l2, 64, src_stride=256, dst_stride=64, reps=16, nd=1,
                     seed=0x44)]}

    if case_name == 's1_2d_16x64':
        return {**common, 'transfers': [
            transfer(S1_L2_TO_L1, l2, l1, 64, src_stride=256, dst_stride=64, reps=16, nd=1,
                     seed=0x45)]}

    if case_name == 's1_l1_to_l1':
        return {**common, 'transfers': [
            transfer(S1_L1_TO_L1, l1, l1 + 0x8000, 512, seed=0x55)]}

    if case_name == 's1_3d':
        # 4 pages of 3 lines of 32 B; lines 64 B apart, pages 1 KiB apart on
        # the source, packed on the destination
        return {**common, 'transfers': [
            transfer(S1_L2_TO_L1, l2, l1, 32, src_stride=64, dst_stride=32, reps=3, nd=2,
                     src_stride_3=1024, dst_stride_3=32, reps_3=4, seed=0x66)]}

    if case_name == 'zero_len':
        # An empty transfer behind a real one: rejected at once (ahead of the
        # real one, as in the RTL), the ids advance and two events fire
        return {**common, 'transfers': [
            transfer(S1_L2_TO_L1, l2, l1, 256, seed=0x77),
            transfer(S1_L2_TO_L1, l2 + 0x1000, l1 + 0x1000, 0, seed=0x78)]}

    if case_name == 'queue_full':
        # Ten launches on one stream, faster than 4 KiB transfers drain: the
        # first leaves the queue at once, the next eight fill it, the tenth is
        # denied until a slot frees
        return {**common, 'transfers': [
            transfer(S1_L2_TO_L1, l2 + i * 0x1000, l1 + i * 0x1000, 4096, seed=0x80 + i,
                     expect_denied=1 if i == 9 else 0)
            for i in range(10)]}

    if case_name == 'both_streams':
        # L1->L2 and L1->L1 at the same time share the read ports
        return {**common, 'transfers': [
            transfer(S0_L1_TO_L2, l1, l2, 2048, seed=0x90),
            transfer(S1_L1_TO_L1, l1 + 0x4000, l1 + 0xc000, 2048, seed=0x91)]}

    if case_name == 'two_ports':
        return {**common, 'port': 1, 'transfers': [
            transfer(S1_L2_TO_L1, l2, l1, 256, seed=0xa1)]}

    if case_name == 'gated':
        # The clock gate is wired and left off: the first access is refused,
        # which is fatal in the model (the RTL hangs the requester)
        return {**common, 'enable_gate': 1, 'transfers': [
            transfer(S1_L2_TO_L1, l2, l1, 256, seed=0xb1)]}

    if case_name == 'lat_10':
        return {**common, 'l2_latency': 10, 'transfers': [
            transfer(S1_L2_TO_L1, l2, l1, 4096, seed=0xc1)]}

    raise ValueError(f'Unknown case: {case_name!r}')


def _region(addr):
    if TCDM_BASE <= addr < TCDM_BASE + TCDM_SIZE:
        return 'tcdm', TCDM_BASE
    if L2_BASE <= addr < L2_BASE + L2_SIZE:
        return 'l2', L2_BASE
    raise ValueError(f'address 0x{addr:x} outside the memories')


def _pattern(seed, offset):
    return (seed + offset) & 0xff


def _source_bytes(transfers):
    """Preload images of the memories holding the sources of the transfers.

    Byte ``i`` of transfer ``t`` (in transfer order, lines then pages) is
    ``(seed + i) & 0xff``.
    """
    images = {'tcdm': bytearray(TCDM_SIZE), 'l2': bytearray(L2_SIZE)}
    for t in transfers:
        if t['length'] == 0:
            continue
        name, base = _region(t['src'])
        reps = t['reps'] if t['nd'] >= 1 and t['reps'] != 0 else 1
        reps_3 = t['reps_3'] if t['nd'] >= 2 and t['reps_3'] != 0 else 1
        addr = t['src']
        offset = 0
        for k in range(reps_3):
            for j in range(reps):
                for b in range(t['length']):
                    images[name][addr - base + b] = _pattern(t['seed'], offset)
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
            self, name='case', value='s1_1d_256',
            description='idma_v3_cluster model-level test case', cast=str,
        ).get_value()
        spec = build_case(case)

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100_000_000)

        work_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), 'build', 'stims'))
        images = _source_bytes(spec['transfers'])
        stims = {name: _write_stim(work_dir, f'{case}_{name}', bytes(blob))
                 for name, blob in images.items()}

        tcdm = Memory(self, 'tcdm', config=MemoryV3Config(
            size=TCDM_SIZE, latency=spec['tcdm_latency'], stim_file=stims['tcdm']))
        l2 = Memory(self, 'l2', config=MemoryV3Config(
            size=L2_SIZE, latency=spec['l2_latency'], stim_file=stims['l2']))
        clock.o_CLOCK(tcdm.i_CLOCK())
        clock.o_CLOCK(l2.i_CLOCK())

        dma_config = ClusterDmaV3Config()
        dma_config.obi_addr_width = TCDM_SIZE.bit_length() - 1
        dma = ClusterDmaV3(self, 'dma', config=dma_config)
        clock.o_CLOCK(dma.i_CLOCK())

        tester = ClusterDmaTester(self, 'tester',
            regs_addr=REGS_BASE,
            transfers=spec['transfers'],
            nb_events=dma_config.nb_events,
            enable_gate=spec['enable_gate'],
        )
        clock.o_CLOCK(tester.i_CLOCK())

        # TCDM crossbar stand-in: the narrow ports and the tester, inline
        tcdm_ico = Router(self, 'tcdm_ico', config=RouterConfig(kind=KIND_UNTIMED))
        clock.o_CLOCK(tcdm_ico.i_CLOCK())
        tcdm_ico.o_MAP(tcdm.i_INPUT(),
            mapping=RouterMapping(name='tcdm', base=0, size=TCDM_SIZE, remove_base=False))
        for i in range(dma_config.obi_ports_per_access):
            dma.o_TCDM_WRITE(i, tcdm_ico.i_INPUT(i))
            dma.o_TCDM_READ(i, tcdm_ico.i_INPUT(dma_config.obi_ports_per_access + i))

        # Cluster AXI stand-in: beat router of the AXI width
        axi = Router(self, 'axi', config=RouterConfig(kind=KIND_BEAT, width=dma_config.axi_width,
            max_pending_bursts_per_input=8))
        clock.o_CLOCK(axi.i_CLOCK())
        axi.o_MAP(l2.i_INPUT(), mapping=RouterMapping(name='l2', base=L2_BASE, size=L2_SIZE))
        dma.o_AXI_READ(axi.i_INPUT(0))
        dma.o_AXI_WRITE(axi.i_INPUT(1))

        # Tester paths: registers directly, memories through an untimed router
        # so the tester's 4-byte accesses reach both memories
        tester_ico = Router(self, 'tester_ico', config=RouterConfig(kind=KIND_UNTIMED))
        clock.o_CLOCK(tester_ico.i_CLOCK())
        tester_ico.o_MAP(tcdm_ico.i_INPUT(2 * dma_config.obi_ports_per_access),
            mapping=RouterMapping(name='tcdm', base=TCDM_BASE, size=TCDM_SIZE))
        tester_ico.o_MAP(axi.i_INPUT(2),
            mapping=RouterMapping(name='l2', base=L2_BASE, size=L2_SIZE, remove_base=False))
        tester_ico.o_MAP(dma.i_INPUT(spec['port']),
            mapping=RouterMapping(name='regs', base=REGS_BASE, size=REGS_SIZE))
        tester.o_MEM(tester_ico.i_INPUT(0))

        for i in range(dma_config.nb_events):
            dma.o_EVENT(i, tester.i_EVENT(i))
        dma.o_FC_EVENT(tester.i_FC_EVENT())
        if spec['enable_gate']:
            tester.o_ENABLE(dma.i_ENABLE())


class Target(gvsoc.runner.Target):

    gapy_description = 'idma_v3_cluster model-level testbench'
    model = Chip
    name = 'test'
