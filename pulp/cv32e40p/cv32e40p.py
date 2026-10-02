# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

"""CV32E40P core, built on the iss_v2 core.

One recipe covers every RTL configuration, given by Cv32e40pConfig. The CV32E40P
behaviour is in slot classes under cpu/iss_v2/*/cores/cv32e40p, as for Ri5ky.
"""

from __future__ import annotations

from typing import Iterable
from typing_extensions import override
from gvsoc.systree import Component
from cpu.iss_v2.riscv import RiscvCommon, IssModule, ExecInOrder, LsuV2, Hwloop
from cpu.iss.isa_gen.isa_gen import Isa, IsaSubset
from cpu.iss.isa_gen.isa_riscv_gen import RiscvIsa
from pulp.cv32e40p.cv32e40p_isa import CoreV2
from pulp.cv32e40p.cv32e40p_config import Cv32e40pConfig

isa_instances: dict[tuple[str, str], Isa] = {}

# misa has MXL=1, I, M and C, plus X with COREV_PULP and F with the F registers
# (cv32e40p_cs_registers.sv, MISA_VALUE).
_MISA_BASE = 0x40001104


def _apply_rtl_decode_fixes(isa: Isa) -> None:
    """Apply the decode differences between the generated RISC-V tables and the
    RTL.

    For fence and fence.i the RTL decoder checks funct3 only and ignores the
    reserved fields (cv32e40p_decoder.sv, OPCODE_FENCE), which the generated
    encodings require to be zero. The priv subset uses the handlers of
    cores/cv32e40p/priv.hpp, and ebreak and c.ebreak those of
    cores/cv32e40p/isa/debug.hpp. The FP instructions with a rounding-mode
    field get the 'cv32e40p_fp_rm' tag, so that Cv32e40pCsr checks the
    rounding mode.
    """
    relaxed = {
        'fence':   '------- ----- ----- 000 ----- 0001111',
        'fence.i': '------- ----- ----- 001 ----- 0001111',
    }
    for insn in isa.get_isa('rv32i').instrs:
        encoding = relaxed.get(insn.label)
        if encoding is not None:
            # Reversed and without spaces, as Instr.__init__ stores it.
            insn.encoding = encoding[::-1].replace(' ', '')

    isa.get_isa('priv').includes = [
        '<cpu/iss_v2/include/cores/cv32e40p/priv.hpp>',
    ]

    # Subset includes come after iss.hpp, and rv32i is in every variant.
    isa.get_isa('rv32i').includes.append(
        '<cpu/iss_v2/include/cores/cv32e40p/isa/debug.hpp>')
    for insn in isa.get_insns():
        if insn.label in ('ebreak', 'c.ebreak'):
            insn.set_exec_label(f"cv32e40p_{insn.name}")
        # rm is instr[14:12], in the reversed encoding string.
        if 'fp_op' in insn.tags and insn.encoding[12:15] == '---':
            insn.add_tag('cv32e40p_fp_rm')


class Cv32e40pExec(ExecInOrder):
    """Execution loop. It stays on the full handlers while a counter is
    enabled, as Ri5kyExec does, and in co-simulation. It also hosts the
    co-simulation model."""

    def __init__(self):
        super().__init__(scoreboard=True, class_name='Cv32e40pExec',
                         inorder_commit=True)

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/exec.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/cv32e40p/exec_implem.hpp>')
        # The co-simulation model (cores/cv32e40p/cosim.hpp) is in the exec slot.
        iss.add_sources(['cpu/iss_v2/src/cores/cv32e40p/cosim.cpp'])


class Cv32e40pLsu(LsuV2):
    """io_v2 LSU reporting the accesses to the co-simulation model."""

    def __init__(self):
        super().__init__(nb_outstanding=1, class_name='Cv32e40pLsu')

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/lsu.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/cv32e40p/lsu_implem.hpp>')


class Cv32e40pIrq(IssModule):
    """Interrupts and debug entry (Cv32e40pIrq), with the RTL priorities, the
    fast lines irq[31:16] first and vectored entry at base + 4 * id. It also
    adds the debug request and mtvec_addr inputs.
    """

    def __init__(self, cluster: bool):
        self.cluster = cluster

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_IRQ', 'Cv32e40pIrq')
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_COREV_CLUSTER', 1 if self.cluster else 0)
        iss.isa.add_define('CONFIG_GVSOC_ISS_RISCV_EXCEPTIONS', 1)
        # Reserved RVC encodings (c.addi4spn nzuimm=0, c.addi16sp/c.lui imm=0,
        # c.lwsp rd=0, c.jr rs1=0) are illegal, as in the RTL (isa/rv32c.hpp).
        iss.isa.add_define('CONFIG_GVSOC_ISS_RVC_STRICT', 1)
        # There is no instruction cache, so code written by a store runs
        # without fence.i (lsu_v2.cpp).
        iss.isa.add_define('CONFIG_GVSOC_ISS_COHERENT_FETCH', 1)
        # FPnew detects tininess after rounding (fpnew_fma.sv).
        iss.add_c_flags(['-DFLEXFLOAT_TININESS_AFTER_ROUNDING=1'])
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/irq.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/irq/irq_riscv.cpp',
            'cpu/iss_v2/src/cores/cv32e40p/irq.cpp',
        ])


class Cv32e40pEvent(IssModule):
    """Event lines of the HPM counters and pipeline timing (Cv32e40pEvents)."""

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
    """CSR map of the RTL (Cv32e40pCsr), M-mode only, with the RTL write
    masks, the PULP and hardware-loop CSRs, the FP legality and the HPM
    counters.
    """

    def __init__(self, fpu: bool, zfinx: bool, pulp: bool, num_mhpmcounters: int):
        self.fpu = fpu
        self.zfinx = zfinx
        self.pulp = pulp
        self.num_mhpmcounters = num_mhpmcounters

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_CSR', 'Cv32e40pCsr')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/csr.hpp>')
        # FPU_IN_ISA enables the F registers and mstatus.FS (FPU=1, ZFINX=0).
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_FPU_IN_ISA',
                           1 if self.fpu and not self.zfinx else 0)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_ZFINX', 1 if self.zfinx else 0)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_PULP', 1 if self.pulp else 0)
        iss.isa.add_define('CONFIG_GVSOC_ISS_CV32E40P_NUM_MHPMCOUNTERS', self.num_mhpmcounters)
        iss.add_sources([
            'cpu/iss_v2/src/cores/cv32e40p/csr.cpp',
            'cpu/iss_v2/src/csr.cpp',
        ])


class Cv32e40pHwloopModule(Hwloop):
    """Hardware loops (Cv32e40pHwloop). A loop-end instruction that traps, or
    an ebreak entering debug mode, does not update the loop.
    """

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        iss.isa.add_define('CONFIG_GVSOC_ISS_HWLOOP_OBJ', 'Cv32e40pHwloop')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/hwloop.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/cv32e40p/hwloop_implem.hpp>')


class Cv32e40pRegfileModule(IssModule):
    """Register file (Cv32e40pRegfile). x0 stays zero on the CORE-V
    post-increment write-backs, and the dispatch of each instruction goes
    through the pipeline timing, the debug decisions and the co-simulation
    model.
    """

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_REGFILE', 'Cv32e40pRegfile')
        iss.isa.add_define('CONFIG_GVSOC_ISS_REGFILE_SCOREBOARD', '1')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/regfile.hpp>')
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/cv32e40p/regfile_implem.hpp>')
        iss.add_sources(['cpu/iss_v2/src/regfile.cpp'])


class Cv32e40pCoreModule(IssModule):
    """Core (Cv32e40pCore), with the reset values and an MRET that keeps
    mcause."""

    @override
    def gen(self, iss: RiscvCommon):
        iss.isa.add_define('CONFIG_GVSOC_ISS_CORE', 'Cv32e40pCore')
        iss.isa.add_include('<cpu/iss_v2/include/cores/cv32e40p/core.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/core.cpp',
            'cpu/iss_v2/src/cores/cv32e40p/core.cpp',
        ])


class Cv32e40pExceptionModule(IssModule):
    """Exceptions (Cv32e40pException). They enter at the mtvec base, or at
    dm_exception_addr in debug mode, and charge the cycles of the trap entry.
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
    """CV32E40P on the iss_v2 core, with the generic slots, the CORE-V subset
    and the CV32E40P slot classes.
    """

    # Prefix of the ISA cache keys and of the generated ISA names, as for Ri5ky.
    isa_name: str = 'cv32e40p'

    def __init__(self, parent: Component, name: str, config: Cv32e40pConfig,
                 extra_extensions: Iterable[IsaSubset] = ()):

        fpu = 'f' in config.isa[4:]
        zfinx = config.zfinx
        pulp = config.corev_pulp
        cluster = config.corev_cluster
        # cv.elw is decoded in the COREV_PULP custom-0 space (cv32e40p_decoder.sv).
        assert pulp or not cluster, "corev_cluster requires corev_pulp"

        # pulp, cluster, zfinx and the number of HPM counters change the code
        # built for one ISA string, so they are part of the cache key and name.
        isa_tag = f"{config.isa}_pulp" if pulp else config.isa
        if cluster:
            isa_tag += '_cluster'
        if zfinx:
            isa_tag += '_zfinx'
        if config.num_mhpmcounters != 1:
            isa_tag += f"_mhpm{config.num_mhpmcounters}"
        cache_key = (type(self).isa_name, isa_tag)
        isa_instance: Isa | None = isa_instances.get(cache_key)

        if isa_instance is None:
            extensions: list[IsaSubset] = [
                *extra_extensions,
            ]
            if pulp:
                extensions.append(CoreV2(elw=cluster))

            isa_instance = RiscvIsa(f"{type(self).isa_name}_{isa_tag}",
                config.isa, extensions=extensions)

            if zfinx:
                # No compressed FP loads and stores with ZFINX
                # (cv32e40p_compressed_decoder.sv).
                isa_instance.disable_from_isa_tag('cf')
                # Nor the FP loads and stores, fmv.x.w and fmv.w.x
                # (cv32e40p_decoder.sv).
                found = set()
                for insn in isa_instance.get_insns():
                    if insn.label in ('flw', 'fsw', 'fmv.x.s', 'fmv.s.x'):
                        insn.set_active(False)
                        found.add(insn.label)
                assert found == {'flw', 'fsw', 'fmv.x.s', 'fmv.s.x'}, found

            _apply_rtl_decode_fixes(isa_instance)

            # Inactive instructions never execute, and their handlers may be in
            # a subset header that this configuration does not include.
            for insn in isa_instance.get_insns():
                if not insn.active:
                    insn.exec_func = insn.exec_func_fast = 'NULL'
                    insn.decode = None

            isa_instances[cache_key] = isa_instance

        misa = _MISA_BASE
        if fpu and not zfinx:
            misa |= 1 << 5    # F
        if pulp:
            misa |= 1 << 23   # X

        modules: dict[str, IssModule] = {
            'irq': Cv32e40pIrq(cluster=cluster),
            'core': Cv32e40pCoreModule(),
            'exception': Cv32e40pExceptionModule(),
            'event': Cv32e40pEvent(),
            'csr': Cv32e40pCsr(fpu=fpu, zfinx=zfinx, pulp=pulp,
                               num_mhpmcounters=config.num_mhpmcounters),
            'exec': Cv32e40pExec(),
            'lsu': Cv32e40pLsu(),
            'regfile': Cv32e40pRegfileModule(),
            'hwloop': Cv32e40pHwloopModule(),
        }

        super().__init__(parent, name, config=config, isa=isa_instance,
                         misa=misa, zfinx=zfinx, modules=modules,
                         debug_handler=config.debug_handler)
        # Read by Cv32e40pException.
        self.add_properties({
            'debug_exception_handler': config.debug_exception_handler,
            # Checked by cv32e40p_cosim_acquire_v1 before it casts the component.
            'cv32e40p_cosim': True,
        })
