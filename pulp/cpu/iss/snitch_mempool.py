# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Snitch core for Mempool-style clusters: Snitch stack CSRs, a memory-mapped
barrier (wake on the barrier_ack port) and a wake-up counter. config.vector
folds in the Spatz vector unit; config.lsu_v2 picks the io_v2 LSU (independent
of the vector unit)."""

from typing_extensions import override

import cpu.iss.isa_gen.isa_riscv_gen
import gvsoc.systree
import pulp.ara.ara_v2
from config_tree import cfg_field
from cpu.iss.isa_gen.isa_gen import Isa
from cpu.iss.isa_gen.isa_pulpv2 import PulpV2
from cpu.iss.isa_gen.isa_smallfloats import Xf16, Xf16alt, Xf8, Xfaux, XfvecSnitch
from cpu.iss_v2.riscv import (Arch, ExecInOrder, IssModule, Lsu, LsuV2, Offload,
                              PrefetchSingleLine, Regfile, RiscvCommon)
from cpu.iss_v2.riscv_config import RiscvConfig
from gvsoc.systree import Component
from pulp.snitch.snitch_isa import Xdma


class SnitchMempoolConfig(RiscvConfig):
    nb_outstanding: int = cfg_field(default=1, dump=True,
        desc="Max outstanding LSU requests.")
    zfinx: bool = cfg_field(default=True, dump=True,
        desc="Single-precision FP on the integer register file (Zfinx).")
    lsu_v2: bool = cfg_field(default=False, dump=True,
        desc="Use the io_v2 LSU variant (works with or without the vector unit).")
    vector: bool = cfg_field(default=False, dump=True,
        desc="Fold in the Spatz vector unit; False = scalar Snitch.")
    vlen: int = cfg_field(default=512, dump=True, desc="RISCV VLEN in bits (vector only).")
    nb_lanes: int = cfg_field(default=4, dump=True, desc="Number of vector lanes (vector only).")
    lane_width: int = cfg_field(default=8, dump=True,
        desc="Vector lane width in bytes (vector only).")


class ArchSnitchMempool(Arch):
    """Arch module selecting the SnitchMempool C++ core class."""

    def __init__(self):
        super().__init__(
            class_name='SnitchMempool',
            source='cpu/iss_v2/src/cores/snitchmempool/snitchmempool.cpp')

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_ARCH', 'SnitchMempool')
        iss.isa.add_include(
            '<cpu/iss_v2/include/cores/snitchmempool/snitchmempool.hpp>')
        iss.add_sources([self.source])


class IrqMempool(IssModule):
    """IrqRiscv whose wfi_handle consults the wake-up counter before sleeping."""

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_IRQ', 'IrqMempool')
        iss.isa.add_define('CONFIG_GVSOC_ISS_RISCV_EXCEPTIONS', 1)
        iss.isa.add_include(
            '<cpu/iss_v2/include/cores/snitchmempool/irq_mempool.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/irq/irq_riscv.cpp',
            'cpu/iss_v2/src/cores/snitchmempool/irq_mempool.cpp',
        ])


_isa_instances: dict[str, Isa] = {}


class SnitchMempool(RiscvCommon):
    """Snitch core with stack CSRs, a memory-mapped barrier and a wake-up
    counter. config.vector adds the Spatz vector unit; config.lsu_v2 picks the
    io_v2 LSU. CSR and irq/wake-up behaviour is the scalar Snitch's in both."""

    def __init__(self, parent: Component, name: str, config: SnitchMempoolConfig):

        cache_key = ('vector_' if config.vector else 'scalar_') + config.isa
        isa_instance: Isa | None = _isa_instances.get(cache_key)

        if isa_instance is None:
            if config.vector:
                extensions = [Xdma(), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux()]
            else:
                extensions = [Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux(),
                              PulpV2(hwloop=False, elw=False)]
            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(
                'snitch_mempool_' + cache_key, config.isa, extensions=extensions)
            if config.vector:
                pulp.ara.ara_v2.extend_isa(isa_instance)
            _isa_instances[cache_key] = isa_instance

        modules: dict[str, IssModule] = {
            'arch': ArchSnitchMempool(),
            'exec': ExecInOrder(scoreboard=True),
            'regfile': Regfile(scoreboard=True),
            'prefetch': PrefetchSingleLine(),
            'irq': IrqMempool(),
            'lsu': (LsuV2 if config.lsu_v2 else Lsu)(nb_outstanding=config.nb_outstanding),
        }
        if config.vector:
            modules['offload'] = Offload()

        super().__init__(parent, name, config=config, isa=isa_instance,
            modules=modules, zfinx=config.zfinx)

        if config.vector:
            self._lsu_v2 = config.lsu_v2
            pulp.ara.ara_v2.attach(self, config.vlen, nb_lanes=config.nb_lanes,
                use_spatz=True, lane_width=config.lane_width, vlsu_v2=config.lsu_v2)

    def o_VLSU(self, port: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'vlsu_{port}', itf,
            signature='io_v2' if getattr(self, '_lsu_v2', False) else 'io')
