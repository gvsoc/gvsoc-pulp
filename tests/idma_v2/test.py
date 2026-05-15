#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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

"""idma_v2 model-level testbench.

Wires an :class:`IDmaTesterV2` (stimulus + checker) to a :class:`RegDmaV2`
(device under test) through a single io_v2 router and io_v2 memories
(``memory_v3``). The tester programs the iDMA registers, waits for the
completion IRQ, and verifies the destination buffer matches the source
pattern. Each ``case`` selects a different scenario via TargetParameter.
"""

from __future__ import annotations

import os
import struct

import gvsoc.systree
import gvsoc.runner
import vp.clock_domain
from memory.memory_v3 import Memory, MemoryV3Config
from interco.router_v2 import (Router, RouterConfig, RouterMapping,
                                  KIND_BANDWIDTH, KIND_BACKPRESSURE)
from gvrun.parameter import TargetParameter

from pulp.idma_v2.reg_dma import RegDmaV2
from pulp.idma_v2.reg_dma_config import RegDmaConfig

from idma_tester import IDmaTesterV2


# Address map (single shared router).
MEM_A_BASE = 0x10000000
MEM_A_SIZE = 0x10000
MEM_B_BASE = 0x20000000
MEM_B_SIZE = 0x10000
TCDM_BASE  = 0x30000000
TCDM_SIZE  = 0x10000
REGS_BASE  = 0x40000000
REGS_SIZE  = 0x100


def build_case(case_name: str) -> dict:
    """Return the per-case parameters for the iDMA tester and router.

    Each case picks src/dst regions, transfer geometry (length, strides,
    reps, config) and a pattern seed. Optionally, a case can override the
    shared_router's bandwidth (bytes/cycle) and latency (cycles) — used by
    the bandwidth- and latency-scaling cases.
    """
    common = dict(
        src_stride=0, dst_stride=0, reps=1, config=0,
        router_bandwidth=8, router_latency=1,
        router_kind=KIND_BANDWIDTH,
        idma_axi_width=8,
    )
    if case_name == '1d_small':
        # 64 byte 1D copy mem_a -> mem_b. Smallest interesting case.
        return {**common,
            'src': MEM_A_BASE + 0x100, 'dst': MEM_B_BASE + 0x200,
            'length': 64, 'pattern_seed': 0xa5,
        }
    if case_name == '1d_line':
        # 4 KiB 1D copy mem_a -> mem_b. Stays inside one AXI page.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x1000, 'pattern_seed': 0x12,
        }
    if case_name == '1d_page_cross':
        # Crosses an AXI page boundary; exercises iDMA's get_burst_size split.
        return {**common,
            'src': MEM_A_BASE + 0x800, 'dst': MEM_B_BASE + 0x800,
            'length': 0x1000, 'pattern_seed': 0x33,
        }
    if case_name == '2d_basic':
        # 8 lines x 256 B with 1 KiB strides each side (config bit 1 = 2D).
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 256, 'src_stride': 0x400, 'dst_stride': 0x400,
            'reps': 8, 'config': (1 << 1), 'pattern_seed': 0x77,
        }
    if case_name == 'mem_to_tcdm':
        # AXI source, TCDM destination. Forces both backends to participate.
        return {**common,
            'src': MEM_A_BASE, 'dst': TCDM_BASE,
            'length': 0x400, 'pattern_seed': 0x44,
        }
    if case_name == 'tcdm_to_mem':
        return {**common,
            'src': TCDM_BASE, 'dst': MEM_A_BASE,
            'length': 0x400, 'pattern_seed': 0x55,
        }
    if case_name == 'bw_4':
        # 4 KiB 1D copy with router bandwidth halved (4 B/cyc).
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x1000, 'pattern_seed': 0xb4,
            'router_bandwidth': 4,
        }
    if case_name == 'bw_16':
        # 4 KiB 1D copy with router bandwidth doubled (16 B/cyc). Widen the
        # iDMA's AXI master to match — otherwise the master caps the
        # effective throughput at axi_width * 1 beat/cyc = 8 B/cyc and the
        # extra router bandwidth has nowhere to go.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x1000, 'pattern_seed': 0x16,
            'router_bandwidth': 16,
            'idma_axi_width': 16,
        }
    if case_name == 'lat_10':
        # 4 KiB 1D copy with extra fixed router latency annotation.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x1000, 'pattern_seed': 0xaa,
            'router_latency': 10,
        }
    if case_name == 'bp_1d_small':
        # 64 byte 1D copy through router_v2_backpressure. Single burst per
        # direction, so we exercise the IO_REQ_GRANTED + axi_response async
        # path of the iDMA without triggering DENIED (which the v1 router
        # surface didn't have).
        return {**common,
            'src': MEM_A_BASE + 0x100, 'dst': MEM_B_BASE + 0x200,
            'length': 64, 'pattern_seed': 0xbb,
            'router_kind': KIND_BACKPRESSURE,
        }
    if case_name == 'bp_1d_line':
        # 4 KiB single-burst transfer through backpressure. Exercises the
        # GRANTED -> deferred resp() round trip on a bandwidth-bound burst.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x1000, 'pattern_seed': 0xcc,
            'router_kind': KIND_BACKPRESSURE,
        }
    if case_name == 'zero_size':
        # length=0 -> the iDMA frontend acks the transfer inline without
        # ever forwarding it to the middle-end. The IRQ should fire
        # essentially immediately, no AXI/TCDM traffic at all.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0, 'pattern_seed': 0xde,
        }
    if case_name == 'zero_reps_2d':
        # 2D config (bit 1 set) with reps=0. The iDMA frontend handles this
        # symmetric edge case the same way as length=0 -> immediate ack.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 256, 'reps': 0, 'config': (1 << 1),
            'pattern_seed': 0xed,
        }
    if case_name == '1d_8k':
        # 8 KiB 1D copy: AXI page is 4 KiB, so the BE splits this into two
        # back-to-back 4 KiB bursts. Exercises the iDMA's burst-queue
        # pipeline (burst_queue_size=4) and read-then-write coupling
        # across consecutive bursts.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x2000, 'pattern_seed': 0x88,
        }
    if case_name == '1d_16k':
        # 16 KiB 1D copy = four 4 KiB bursts. Pipelines all four through
        # the bandwidth router; total cycles tracks 4 * burst_duration.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x4000, 'pattern_seed': 0x16,
        }
    if case_name == 'bp_2d_basic':
        # 2D 8 reps x 256 B through backpressure. The middle-end serialises
        # reps (one rep at a time enqueued to the BE), so each rep is one
        # burst per direction and the "one-pending rule" of backpressure
        # never triggers a DENIED. Exercises the GRANTED -> resp() path
        # repeatedly and stresses the FE's per-rep ack accounting.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 256, 'src_stride': 0x400, 'dst_stride': 0x400,
            'reps': 8, 'config': (1 << 1), 'pattern_seed': 0xb2,
            'router_kind': KIND_BACKPRESSURE,
        }
    if case_name == 'bp_1d_8k':
        # 8 KiB 1D copy through backpressure. AXI page=4 KiB so the BE
        # splits into two back-to-back bursts. The second read hits the
        # router's "one-pending rule" deny while the first is still in
        # the deferred-forward window, so this exercises the iDMA's
        # denied_blocked -> axi_retry path. Until the router fix that
        # made send_handler call retry(), this case would deadlock.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x2000, 'pattern_seed': 0x82,
            'router_kind': KIND_BACKPRESSURE,
        }
    if case_name == 'bp_1d_16k':
        # 16 KiB 1D copy through backpressure -> four 4 KiB bursts. Multiple
        # denials, multiple retries — the strongest stress on the iDMA's
        # deny+retry handshake.
        return {**common,
            'src': MEM_A_BASE, 'dst': MEM_B_BASE,
            'length': 0x4000, 'pattern_seed': 0xb1,
            'router_kind': KIND_BACKPRESSURE,
        }

    raise ValueError(f'Unknown case: {case_name!r}')


def _src_memory(spec):
    """Return ``(name, base, size)`` of the memory containing the source range,
    or ``None`` if the source isn't in any of our memories.
    """
    src = spec['src']
    end = spec['src'] + max(1, (spec['reps'] - 1) * spec['src_stride'] + spec['length'])
    for name, base, size in (
        ('mem_a', MEM_A_BASE, MEM_A_SIZE),
        ('mem_b', MEM_B_BASE, MEM_B_SIZE),
        ('tcdm',  TCDM_BASE,  TCDM_SIZE),
    ):
        if base <= src and end <= base + size:
            return name, base, size
    return None


def _stim_bytes_for_source(spec):
    """Build the binary blob the source memory should preload at startup so
    the iDMA reads back the pattern the C++ tester compares against.

    Returns (None, None) when there is no source data to preload (zero-size
    or zero-rep transfer) — those cases skip stim_file altogether.
    """
    sm = _src_memory(spec)
    if sm is None:
        return None, None
    if spec['length'] == 0 or spec['reps'] == 0:
        return None, None
    name, base, _ = sm
    src_off = spec['src'] - base
    seed = spec['pattern_seed']
    length = spec['length']
    src_stride = spec['src_stride']
    reps = spec['reps']
    end_off = src_off + max(0, (reps - 1) * src_stride) + length
    buf = bytearray(end_off)  # zero-filled prefix; iDMA never reads it
    for r in range(reps):
        for b in range(length):
            buf[src_off + r * src_stride + b] = (seed + r * length + b) & 0xff
    return name, bytes(buf)


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
            self, name='case', value='1d_small',
            description='idma_v2 model-level test case', cast=str,
        ).get_value()

        spec = build_case(case)
        # Router/iDMA knobs live in spec but must not leak into the tester ctor.
        router_bandwidth = spec.pop('router_bandwidth')
        router_latency   = spec.pop('router_latency')
        router_kind      = spec.pop('router_kind')
        idma_axi_width   = spec.pop('idma_axi_width')

        clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100_000_000)

        # --- Memories (source memory preloaded with the test pattern) ---
        work_dir = os.path.abspath(os.path.join(
            os.path.dirname(__file__), 'build', 'stims'))
        src_mem_name, blob = _stim_bytes_for_source(spec)
        stim_paths = {}
        if blob is not None:
            stim_paths[src_mem_name] = _write_stim(work_dir, f'{case}_{src_mem_name}', blob)

        mem_a = Memory(self, 'mem_a', config=MemoryV3Config(
            size=MEM_A_SIZE, latency=1, stim_file=stim_paths.get('mem_a', '')))
        mem_b = Memory(self, 'mem_b', config=MemoryV3Config(
            size=MEM_B_SIZE, latency=1, stim_file=stim_paths.get('mem_b', '')))
        tcdm  = Memory(self, 'tcdm',  config=MemoryV3Config(
            size=TCDM_SIZE,  latency=1, stim_file=stim_paths.get('tcdm', '')))
        clock.o_CLOCK(mem_a.i_CLOCK())
        clock.o_CLOCK(mem_b.i_CLOCK())
        clock.o_CLOCK(tcdm.i_CLOCK())

        # --- iDMA (DUT) ---
        idma = RegDmaV2(self, 'idma', config=RegDmaConfig(
            transfer_queue_size=4,
            burst_queue_size=4,
            burst_size=0,
            loc_base=TCDM_BASE,
            loc_size=TCDM_SIZE,
            tcdm_width=8,
            axi_width=idma_axi_width,
        ))
        clock.o_CLOCK(idma.i_CLOCK())

        # --- Tester ---
        tester = IDmaTesterV2(self, 'tester',
            regs_addr=REGS_BASE,
            **spec,
        )
        clock.o_CLOCK(tester.i_CLOCK())

        # --- Routers ---
        # `shared_router`: top-level interconnect. The tester reaches every
        # memory and the iDMA's register interface through it; the iDMA's
        # AXI master uses it too (memory addresses are global). All bindings
        # rebase global -> downstream local via remove_base=True.
        shared_cfg = RouterConfig(kind=router_kind,
                                  latency=router_latency,
                                  bandwidth=router_bandwidth)
        shared_router = Router(self, 'router', config=shared_cfg)
        clock.o_CLOCK(shared_router.i_CLOCK())

        # `tcdm_junction`: 2-input pass-through router in front of the TCDM.
        # The iDMA's o_TCDM emits *already-translated* local addresses (it
        # subtracts loc_base internally), so the junction has a single
        # mapping at base=0 / size=TCDM_SIZE / remove_base=False so local
        # addresses go through unchanged. The tester reaches the same
        # junction via shared_router's `tcdm` mapping which rebases the
        # global TCDM_BASE address into the same local space first.
        tcdm_cfg = RouterConfig(kind=KIND_BANDWIDTH, latency=0, bandwidth=8)
        tcdm_junction = Router(self, 'tcdm_junction', config=tcdm_cfg)
        clock.o_CLOCK(tcdm_junction.i_CLOCK())
        tcdm_junction.o_MAP(tcdm.i_INPUT(),
            mapping=RouterMapping(name='tcdm', base=0, size=TCDM_SIZE,
                                   remove_base=False))

        # shared_router downstream mappings: mem_a, mem_b, tcdm (-> junction),
        # regs (-> iDMA input).
        shared_router.o_MAP(mem_a.i_INPUT(),
            mapping=RouterMapping(name='mem_a', base=MEM_A_BASE, size=MEM_A_SIZE))
        shared_router.o_MAP(mem_b.i_INPUT(),
            mapping=RouterMapping(name='mem_b', base=MEM_B_BASE, size=MEM_B_SIZE))
        shared_router.o_MAP(tcdm_junction.i_INPUT(0),
            mapping=RouterMapping(name='tcdm',  base=TCDM_BASE,  size=TCDM_SIZE))
        shared_router.o_MAP(idma.i_INPUT(),
            mapping=RouterMapping(name='regs',  base=REGS_BASE,  size=REGS_SIZE))

        # shared_router upstream: tester regs, tester mem, iDMA AXI read,
        # iDMA AXI write. The iDMA's axi_read and axi_write are independent
        # IoMaster instances and need separate router inputs — v2 IoSlave
        # supports only one master per slave port.
        tester.o_REGS(shared_router.i_INPUT(0))
        tester.o_MEM (shared_router.i_INPUT(1))
        idma.o_AXI_READ (shared_router.i_INPUT(2))
        idma.o_AXI_WRITE(shared_router.i_INPUT(3))

        # iDMA's o_TCDM read+write also need separate junction inputs.
        idma.o_TCDM_READ (tcdm_junction.i_INPUT(1))
        idma.o_TCDM_WRITE(tcdm_junction.i_INPUT(2))

        # IRQ wire (not via router — direct).
        idma.o_IRQ(tester.i_IRQ())


class Target(gvsoc.runner.Target):
    gapy_description = 'idma_v2 model-level testbench'
    model = Chip
    name = 'test'
