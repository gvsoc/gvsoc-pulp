# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)


from __future__ import annotations

from typing import Iterable
from typing_extensions import override
from gvsoc.systree import Component
from cpu.iss_v2.riscv import (RiscvCommon, IrqExternal, IssModule, ExecInOrder, Regfile, Arch, LsuV2, Hwloop)
from cpu.iss.isa_gen.isa_gen import Isa, IsaSubset
from cpu.iss.isa_gen.isa_riscv_gen import RiscvIsa
from cpu.iss.isa_gen.isa_pulpv2 import PulpV2
from cpu.iss.isa_gen.isa_pulpnn import PulpNn
from cpu.iss.isa_gen.isa_smallfloats import Xf16, Xf16alt, Xfvec
from cpu.iss_v2.riscv_config import RiscvConfig

isa_instances: dict[tuple[str, str], Isa] = {}


class Ri5kyExec(ExecInOrder):
    def __init__(self):
        super().__init__(scoreboard=True, class_name='Ri5kyExec',
                         inorder_commit=True)

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        iss.isa.add_include('<cpu/iss_v2/include/cores/ri5ky/exec.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/ri5ky/exec_implem.hpp>')

class Ri5kyLsu(LsuV2):
    """io_v2 LSU extended with the p.elw event load (park puts the core to
    sleep; an interrupt replays the instruction after the handler)."""
    def __init__(self, nb_outstanding: int=1, class_name: str='Ri5kyLsu'):
        super().__init__(nb_outstanding=nb_outstanding, class_name=class_name)

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        iss.isa.add_define('CONFIG_GVSOC_ISS_ELW', '1')
        iss.isa.add_include('<cpu/iss_v2/include/cores/ri5ky/lsu.hpp>')
        iss.add_sources(['cpu/iss_v2/src/cores/ri5ky/lsu.cpp'])

class Ri5kyNnLsu(Ri5kyLsu):
    """ri5ky LSU with the PULP-NN loads into registers of the core outside
    the register file (special-purpose registers, pv.qnt thresholds)."""
    def __init__(self, nb_outstanding: int=1):
        super().__init__(nb_outstanding=nb_outstanding, class_name='Ri5kyNnLsu')

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        iss.isa.add_include('<cpu/iss_v2/include/cores/ri5kynn/lsu.hpp>')

class Ri5kyEvent(IssModule):
    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_EVENT', 'Ri5kyEvents')
        iss.isa.add_include('<cpu/iss_v2/include/cores/ri5ky/events.hpp>')
        iss.add_sources(['cpu/iss_v2/src/event/event.cpp'])
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/ri5ky/events_implem.hpp>')
        iss.add_sources(['cpu/iss_v2/src/cores/ri5ky/events.cpp'])

class Ri5kyConfig(RiscvConfig):
    pass

class Ri5kyCsr(IssModule):
    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_CSR', 'Ri5kyCsr')
        iss.isa.add_include('<cpu/iss_v2/include/cores/ri5ky/csr.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/ri5ky/csr_implem.hpp>')
        iss.isa.add_define('CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR', 1)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR_FIRST', 12)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR_LAST', 16)
        iss.add_sources([
            'cpu/iss_v2/src/cores/ri5ky/csr.cpp',
            'cpu/iss_v2/src/csr.cpp'
        ])

class Ri5ky(RiscvCommon):

    # Tag used in ISA-cache and generated ISA-class names. Subclasses that
    # layer extra ISA extensions on top must override this so they get their
    # own ISA instance (and don't collide with the base ri5ky cache entry).
    isa_name: str = 'ri5ky'

    def __init__(self, parent: Component, name: str, config: Ri5kyConfig,
                 extra_extensions: Iterable[IsaSubset] = (),
                 exec_module: IssModule | None = None,
                 arch_module: IssModule | None = None,
                 lsu_module: IssModule | None = None):

        cache_key = (type(self).isa_name, config.isa)
        isa_instance: Isa | None = isa_instances.get(cache_key)

        if isa_instance is None:

            # Ordering matters: `pulp_v2.hpp` uses iss_v2 macros (REG_GET,
            # REG_OUT, iss_insn_next, ...) without including them itself, so
            # an iss_v2-aware header must be emitted before it.
            # `rvXf16.hpp` pulls in `cpu/iss_v2/include/isa_lib/macros.h`,
            # so the smallfloats extensions are listed before PulpV2 to
            # satisfy that dependency.
            extensions: list[IsaSubset] = [
                Xf16(), Xf16alt(), Xfvec(),
                *extra_extensions,
                PulpV2(),
            ]

            isa_instance = RiscvIsa(f"{type(self).isa_name}_{config.isa}",
                config.isa, extensions=extensions)

            isa_instances[cache_key] = isa_instance

        modules: dict[str, IssModule] = {
            'arch': Arch('Ri5ky', source='cpu/iss_v2/src/cores/ri5ky/ri5ky.cpp')
                if arch_module is None else arch_module,
            'irq': IrqExternal(),
            'event': Ri5kyEvent(),
            'csr': Ri5kyCsr(),
            'exec': Ri5kyExec() if exec_module is None else exec_module,
            # LSU is single-issue at the request level. Ri5kyExec's
            # inorder_commit=True path absorbs the back-to-back-load case
            # (same-cycle re-dispatch in insn_terminate) so a stalled
            # second load doesn't see a spurious stall cycle.
            'lsu': Ri5kyLsu(nb_outstanding=1) if lsu_module is None else lsu_module,
            'regfile': Regfile(scoreboard=True),
            'hwloop': Hwloop(),
        }
        super().__init__(parent, name, config=config, isa=isa_instance, modules=modules,
                         stack_checker=True)


class Ri5kyNn(Ri5ky):
    """Ri5ky with the PULP-NN extension (XpulpNN: SIMD on nibbles and crumbs,
    mac-load, quantization), whose special-purpose registers are held by its
    arch (cpu/iss_v2/include/cores/ri5kynn)."""

    isa_name = 'ri5ky_nn'

    def __init__(self, parent: Component, name: str, config: Ri5kyConfig,
                 extra_extensions: Iterable[IsaSubset] = ()):
        pulpnn = PulpNn()
        pulpnn.includes = ['<cpu/iss_v2/include/isa/pulp_nn.hpp>']
        super().__init__(parent, name, config=config,
                         extra_extensions=[pulpnn, *extra_extensions],
                         arch_module=Arch('Ri5kyNn',
                             source='cpu/iss_v2/src/cores/ri5kynn/ri5kynn.cpp'),
                         lsu_module=Ri5kyNnLsu(nb_outstanding=1))
        self.add_sources(['cpu/iss_v2/src/cores/ri5ky/ri5ky.cpp'])
