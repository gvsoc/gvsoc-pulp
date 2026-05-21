# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Spatz core variant for Mempool-style clusters.

Adds a Snitch-compatible barrier CSR (0x7C2) and a wake-up counter that
absorbs the barrier_sync / WFI race: when the central barrier broadcasts a
wake-up synchronously from inside the local CSR notify (the broadcaster
hasn't yet hit its own WFI), the wake-up would otherwise be lost. With
the counter every "early" sync banks one credit, and the next WFI walks
straight through instead of sleeping.

The wake-up counter and the barrier ports live on the SpatzMempool core
class itself rather than in the generic ExecInOrder (where v1 carried it
behind an ifdef), so generic iss_v2 stays untouched.
"""

from typing_extensions import override

import cpu.iss.isa_gen.isa_riscv_gen
import gvsoc.systree
import pulp.ara.ara_v2
from cpu.iss.isa_gen.isa_gen import Isa
from cpu.iss.isa_gen.isa_smallfloats import Xf16, Xf16alt, Xf8, Xfaux, XfvecSnitch
from cpu.iss_v2.riscv import (Arch, ExecInOrder, IssModule, Lsu, LsuV2, Offload,
                              PrefetchSingleLine, Regfile, RiscvCommon)
from gvsoc.systree import Component
from pulp.cpu.iss.spatz_config import SpatzConfig
from pulp.snitch.snitch_isa import Xdma


class ArchSpatzMempool(Arch):
    """Arch module that selects the SpatzMempool C++ core class."""

    def __init__(self):
        super().__init__(
            class_name='SpatzMempool',
            source='cpu/iss_v2/src/cores/spatzmempool/spatzmempool.cpp')

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_ARCH', 'SpatzMempool')
        iss.isa.add_include(
            '<cpu/iss_v2/include/cores/spatzmempool/spatzmempool.hpp>')
        iss.add_sources([self.source])


class IrqMempool(IssModule):
    """Mempool-aware IRQ controller.

    Drop-in replacement for :class:`Irq` (``IrqRiscv``) whose only
    difference is that ``wfi_handle`` consults the SpatzMempool wake-up
    counter and decrements it instead of sleeping when a credit is
    available.
    """

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_IRQ', 'IrqMempool')
        # IrqMempool inherits from IrqRiscv; keep the same exceptions config.
        iss.isa.add_define('CONFIG_GVSOC_ISS_RISCV_EXCEPTIONS', 1)
        iss.isa.add_include(
            '<cpu/iss_v2/include/cores/spatzmempool/irq_mempool.hpp>')
        # The base IrqRiscv translation unit still provides the bulk of the
        # implementation (check_interrupts, mip/mie/mtvec handling, ...).
        iss.add_sources([
            'cpu/iss_v2/src/irq/irq_riscv.cpp',
            'cpu/iss_v2/src/cores/spatzmempool/irq_mempool.cpp',
        ])


_isa_instances: dict[str, Isa] = {}


class SpatzMempool(RiscvCommon):
    """Spatz core with Snitch-style barrier CSR and wake-up counter.

    Wiring (in addition to the standard :class:`Spatz` ports):

    - :meth:`o_BARRIER_REQ` master port — driven on every barrier CSR
      read; routes to the central barrier unit.
    - ``barrier_ack`` slave port — driven by the central barrier when all
      cores have arrived; either wakes a sleeping WFI or banks a wake-up
      credit.
    """

    def __init__(self,
            parent: Component,
            name: str,
            config: SpatzConfig
        ):

        isa_instance: Isa | None = _isa_instances.get(config.isa)

        if isa_instance is None:
            extensions = [Xdma(), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux()]
            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(
                'spatz_mempool_' + config.isa, config.isa,
                extensions=extensions)
            _isa_instances[config.isa] = isa_instance
            pulp.ara.ara_v2.extend_isa(isa_instance)

        modules: dict[str, IssModule] = {
            'arch': ArchSpatzMempool(),
            'exec': ExecInOrder(scoreboard=True),
            'regfile': Regfile(scoreboard=True),
            'prefetch': PrefetchSingleLine(),
            'offload': Offload(),
            'irq': IrqMempool(),
        }

        modules['lsu'] = LsuV2() if config.vlsu_v2 else Lsu()

        super().__init__(parent, name, config=config, isa=isa_instance,
            modules=modules)

        self._vlsu_v2 = config.vlsu_v2

        pulp.ara.ara_v2.attach(self, config.vlen, nb_lanes=config.nb_lanes,
            use_spatz=True, lane_width=config.lane_width,
            vlsu_v2=config.vlsu_v2)

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        """Notify-output to the central barrier (driven on every barrier
        CSR read)."""
        self.itf_bind('barrier_req', itf, signature='wire<bool>')

    def o_VLSU(self, port: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'vlsu_{port}', itf,
            signature='io_v2' if getattr(self, '_vlsu_v2', False) else 'io')
