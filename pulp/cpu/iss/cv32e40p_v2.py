#
# Copyright (C) 2026 Fondazione Chips-it
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

#
# Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
#

from __future__ import annotations

from typing import Iterable
from typing_extensions import override
from gvsoc.systree import Component
from cpu.iss_v2.riscv import (RiscvCommon, IssModule, ExecInOrder,
                              Regfile, LsuV2, Hwloop)
from cpu.iss.isa_gen.isa_gen import Isa, IsaSubset
from cpu.iss.isa_gen.isa_riscv_gen import RiscvIsa
from cpu.iss.isa_gen.isa_cv32e40pv2 import CoreV2
from cpu.iss_v2.riscv_config import RiscvConfig

isa_instances: dict[tuple[str, str], Isa] = {}

# misa: MXL=1 | I | M | C, plus X for the PULP extensions and F when the
# FPU registers are in the ISA (not for ZFINX). Same values as the v1
# model (pulp/cpu/iss/pulp_cores.py).
_MISA_BASE = 0x40001104


def _apply_rtl_decode_fixes(isa: Isa) -> None:
    """Decode-level differences between the generated RISC-V tables and the
    CV32E40P RTL, applied once per ISA instance:

    - FENCE/FENCE.I: the RTL decoder checks funct3 only and ignores the
      reserved rd/rs1/fm fields (cv32e40p_decoder.sv, OPCODE_FENCE), as the
      unprivileged spec requires for forward compatibility; the generated
      encodings pin those fields to zero, turning e.g. a fence with rd=x31
      into an illegal instruction.
    - CSRRC with rs1=x0 and CSRRSI/CSRRCI with uimm=0 must not write the CSR
      (privileged spec §2.2): the priv subset is routed to the core's
      handlers (cores/cv32e40p/priv.hpp), same fix as the v1 model.
    """
    relaxed = {
        'fence':   '------- ----- ----- 000 ----- 0001111',
        'fence.i': '------- ----- ----- 001 ----- 0001111',
    }
    for insn in isa.get_isa('rv32i').instrs:
        encoding = relaxed.get(insn.label)
        if encoding is not None:
            # Same transform as Instr.__init__ (reversed, spaces stripped).
            insn.encoding = encoding[::-1].replace(' ', '')

    isa.get_isa('priv').includes = [
        '<cpu/iss_v2/include/cores/cv32e40p/priv.hpp>',
    ]


class Cv32e40pConfig(RiscvConfig):
    pass


class Cv32e40pExec(ExecInOrder):
    """CV32E40P execution loop: stays on the full handlers while any
    implemented counter is enabled, so the event lines fire (same scheme
    as Ri5kyExec)."""

    def __init__(self):
        super().__init__(scoreboard=True, class_name='Cv32e40pExec',
                         inorder_commit=True)

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/exec.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/cv32e40p/exec_implem.hpp>')


class Cv32e40pIrq(IssModule):
    """CV32E40P interrupt personality.

    Selects the Cv32e40pIrq C++ class for the irq slot: RISC-V privileged
    interrupt scheme (mie/mip/mtvec, standard mcause codes) with the RTL
    priority order (fast lines irq[31:16] above MEI/MSI/MTI) and vectored
    entry (base + 4*id).
    """

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_IRQ', 'Cv32e40pIrq')
        iss.isa.add_define('CONFIG_GVSOC_ISS_RISCV_EXCEPTIONS', 1)
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/irq.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/irq/irq_riscv.cpp',
            'cpu/iss_v2/src/cores/cv32e40p/irq.cpp',
        ])


class Cv32e40pEvent(IssModule):
    """CV32E40P event accounting.

    Selects the Cv32e40pEvents C++ class for the event slot: routes the
    architectural event lines (instr, load, store, jump, branch, taken
    branch, compressed) into the mhpm counters via Cv32e40pCsr::hpm_commit.
    """

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_EVENT', 'Cv32e40pEvents')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/events.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/cv32e40p/events_implem.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/event/event.cpp',
            'cpu/iss_v2/src/cores/cv32e40p/events.cpp',
        ])


class Cv32e40pCsr(IssModule):
    """CV32E40P CSR personality.

    Selects the Cv32e40pCsr C++ class for the csr slot: M-mode-only CSR
    map, RTL write masks, PULP custom CSRs, hardware-loop CSRs and
    illegal-instruction on unsupported CSR accesses.
    """

    def __init__(self, fpu: bool=False, zfinx: bool=False, pulp: bool=True,
                 num_mhpmcounters: int=1):
        self.fpu = fpu
        self.zfinx = zfinx
        self.pulp = pulp
        self.num_mhpmcounters = num_mhpmcounters

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_CSR', 'Cv32e40pCsr')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/csr.hpp>')
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA', 1 if self.fpu else 0)
        if self.fpu:
            # FP write-backs must dirty mstatus.FS (see iss_v2 isa_lib/macros.h).
            iss.isa.add_define('CONFIG_GVSOC_ISS_FP_STATE_DIRTY', 1)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_ZFINX', 1 if self.zfinx else 0)
        if self.fpu or self.zfinx:
            # Reserved FP rounding modes raise illegal-instruction with no
            # architectural side effects (isa_lib int.h + Cv32e40pRegfile).
            iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_FP_TRAPS', 1)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_PULP', 1 if self.pulp else 0)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS', self.num_mhpmcounters)
        # mstatus write policy, applied by Core::mstatus_update: only
        # MIE/MPIE are writable, plus FS with FPU registers in the ISA.
        iss.isa.add_define('CONFIG_GVSOC_ISS_CORE_MSTATUS_WRITE_MASK',
            '0x6088' if self.fpu else '0x88')
        iss.add_sources([
            'cpu/iss_v2/src/cores/cv32e40p/csr.cpp',
            'cpu/iss_v2/src/csr.cpp',
        ])


class Cv32e40pRegfileModule(IssModule):
    """CV32E40P register-file personality.

    Selects the Cv32e40pRegfile C++ class for the regfile slot: a trapped
    instruction writes no destination register (the reserved-rounding-mode
    raise in isa_lib int.h arms the one-shot write-back suppression).
    """

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_REGFILE', 'Cv32e40pRegfile')
        iss.isa.add_define('CONFIG_GVSOC_ISS_REGFILE_SCOREBOARD', '1')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/regfile.hpp>')
        iss.add_sources(['cpu/iss_v2/src/regfile.cpp'])


class Cv32e40pCoreModule(IssModule):
    """CV32E40P core personality.

    Selects the Cv32e40pCore C++ class for the core slot: MRET keeps
    mcause (the RTL holds it until the next trap or an explicit CSR
    write, the generic handler clears it).
    """

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_CORE', 'Cv32e40pCore')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/core.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/core.cpp',
            'cpu/iss_v2/src/cores/cv32e40p/core.cpp',
        ])


class Cv32e40pExceptionModule(IssModule):
    """CV32E40P exception personality.

    Selects the Cv32e40pException C++ class for the exception slot:
    exceptions enter at the mtvec base, with the mode bits kept out of
    the entry PC.
    """

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_EXCEPTION', 'Cv32e40pException')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/exception.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/exception.cpp',
            'cpu/iss_v2/src/cores/cv32e40p/exception.cpp',
        ])


class Cv32e40p(RiscvCommon):
    """CV32E40P on the iss_v2 modular core.

    Bring-up recipe: generic v2 slots plus the CoreV ISA subset. The
    CV32E40P-specific CSR map, event counters and trap behaviour come in
    as dedicated slot overrides on top of this base (same layering as
    Ri5ky).
    """

    # Tag used in ISA-cache and generated ISA-class names (see Ri5ky).
    isa_name: str = 'cv32e40p_v2'

    def __init__(self, parent: Component, name: str, config: Cv32e40pConfig,
                 fpu: bool=False, zfinx: bool=False, pulp: bool=True,
                 num_mhpmcounters: int=1,
                 extra_extensions: Iterable[IsaSubset] = ()):

        # pulp and zfinx change what gets compiled behind one ISA string,
        # so both are part of the cache key and of the generated ISA name.
        isa_tag = f"{config.isa}_pulp" if pulp else config.isa
        if zfinx:
            isa_tag += '_zfinx'
        cache_key = (type(self).isa_name, isa_tag)
        isa_instance: Isa | None = isa_instances.get(cache_key)

        if isa_instance is None:
            extensions: list[IsaSubset] = [
                *extra_extensions,
            ]
            if pulp:
                extensions.append(CoreV2())

            isa_instance = RiscvIsa(f"{type(self).isa_name}_{isa_tag}",
                config.isa, extensions=extensions)

            if zfinx:
                # RTL decodes the compressed FP loads/stores only with
                # FPU == 1 && ZFINX == 0 (cv32e40p_compressed_decoder.sv).
                isa_instance.disable_from_isa_tag('cf')

            _apply_rtl_decode_fixes(isa_instance)

            isa_instances[cache_key] = isa_instance

        misa = _MISA_BASE
        if fpu and not zfinx:
            misa |= 1 << 5    # F
        if pulp:
            misa |= 1 << 23   # X

        modules: dict[str, IssModule] = {
            'irq': Cv32e40pIrq(),
            'core': Cv32e40pCoreModule(),
            'exception': Cv32e40pExceptionModule(),
            'event': Cv32e40pEvent(),
            'csr': Cv32e40pCsr(fpu=fpu, zfinx=zfinx, pulp=pulp,
                               num_mhpmcounters=num_mhpmcounters),
            'exec': Cv32e40pExec(),
            'lsu': LsuV2(),
            'regfile': Cv32e40pRegfileModule(),
            'hwloop': Hwloop(),
        }

        super().__init__(parent, name, config=config, isa=isa_instance,
                         misa=misa, zfinx=zfinx, modules=modules)
