#!/usr/bin/env python3

#
# Copyright (C) 2020 ETH Zurich
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

import cpu.iss.riscv
from cpu.iss.isa_gen.isa_riscv_gen import *
from cpu.iss.isa_gen.isa_smallfloats import *
from cpu.iss.isa_gen.isa_pulpv2 import *
from cpu.iss.isa_gen.isa_cv32e40pv2 import *
from cpu.iss.isa_gen.isa_pulpnn import PulpNn


def _build_fc_isa(name, pulpnn=False, pulpv2=True):

    extensions = [ Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ]

    if pulpv2:
        extensions.append(PulpV2())

    if pulpnn:
        extensions.append(PulpNn())

    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, 'rv32imfc', extensions=extensions)

    return isa

def _build_cluster_isa(name, pulpnn=False, pulpv2=True):

    extensions = [ Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ]

    if pulpv2:
        extensions.append(PulpV2())

    if pulpnn:
        extensions.append(PulpNn())

    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, 'rv32imfc', extensions=extensions)

    return isa


def _build_cv32e40p_isa(name, pulpv2=True, fpu=True, zfinx=False, core_version=2):
    """Build the CV32E40P ISA.

    fpu=True  → rv32imfc (F instructions in decoder, regardless of zfinx).
    fpu=False → rv32imc  (no F instructions).

    ZFINX uses the same F-extension opcodes but routes them to GPR via
    ISS_SINGLE_REGFILE (set in RiscvCommon.__init__).  The MISA F-bit and
    mstatus FS mask are controlled separately by fpu_in_isa in add_properties.
    """

    # ZFINX needs F instructions in the decoder (routed to GPR by ISS_SINGLE_REGFILE).
    base_isa = 'rv32imfc' if fpu else 'rv32imc'

    extensions = []
    if pulpv2:
        if core_version == 2:
            extensions.append(CoreV2())
        else:
            extensions.append(PulpV2())

    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, base_isa, extensions=extensions)

    return isa


_cluster_isa = None
_fc_isa = None



class PulpCore(cpu.iss.riscv.RiscvCommon):

    def __init__(self, parent, name, isa, cluster_id: int, core_id: int, fetch_enable: bool=False,
            boot_addr: int=0, external_pccr: bool=False):

        super().__init__(parent, name, isa=isa,
            riscv_dbg_unit=True, fetch_enable=fetch_enable, boot_addr=boot_addr,
            first_external_pcer=12, debug_handler=0x1a190800, misa=0x40000000, core="ri5ky",
            cluster_id=cluster_id, core_id=core_id, wrapper="pulp/cpu/iss/pulp_iss_wrapper.cpp",
            scoreboard=True, timed=True, handle_misaligned=True, external_pccr=external_pccr)

        self.add_c_flags([
            "-DPIPELINE_STALL_THRESHOLD=1",
            "-DCONFIG_ISS_CORE=ri5cy",
            '-DISS_SINGLE_REGFILE=1',
            '-DCONFIG_GVSOC_ISS_NO_MSTATUS_FS=1'
        ])


class ClusterCore(PulpCore):

    def __init__(self, parent, name, cluster_id: int=None, core_id: int=None, pulpv2=True, pulpnn=True):

        global _cluster_isa

        if _cluster_isa is None:
            _cluster_isa = _build_cluster_isa('pulp_cluster', pulpv2=pulpv2, pulpnn=pulpnn)

        super().__init__(parent, name, isa=_cluster_isa, cluster_id=cluster_id, core_id=core_id, external_pccr=True)




class FcCore(PulpCore):

    def __init__(self, parent, name, fetch_enable: bool=False, boot_addr: int=0, cluster_id: int=31, pulpv2=True, pulpnn=True):

        global _fc_isa

        if _fc_isa is None:
            _fc_isa = _build_fc_isa('pulp_fc', pulpv2=pulpv2, pulpnn=pulpnn)

        super().__init__(parent, name, isa=_fc_isa, cluster_id=cluster_id, core_id=0,
            fetch_enable=fetch_enable, boot_addr=boot_addr)



class cv32e40p(cpu.iss.riscv.RiscvCommon):

    def __init__(self, parent, name, fetch_enable: bool=False, boot_addr: int=0, cluster_id: int=31, core_id: int=0,
            pulpv2: bool=True, fpu: bool=True, zfinx: bool=False, core_version: int=2,
            num_mhpmcounters: int=1):
        """
        CV32E40P core model.

        Parameters
        ----------
        pulpv2 : bool
            Enable PULP extensions (COREV_PULP). Default True.
        fpu : bool
            Enable FPU (rv32imfc ISA, misa bit F set). Default True.
        zfinx : bool
            FPU uses integer registers (Zfinx). Clears misa bit F. Default False.
        core_version : int
            1 for legacy PULP v1 encodings, 2 for CORE-V v2 (custom-0/1/2). Default 2.
        num_mhpmcounters : int
            Number of HPM counters (RTL NUM_MHPMCOUNTERS param). Default 1.
        """
        # Unique ISA name per instance/configuration to avoid generator collisions
        isa_name = f'cv32e40p_{name}_v{core_version}_{"f" if fpu else "nof"}_{"z" if zfinx else "noz"}'
        isa = _build_cv32e40p_isa(isa_name, pulpv2=pulpv2, fpu=fpu, zfinx=zfinx, core_version=core_version)

        # misa: MXL=1(RV32) | I(bit8) | M(bit12) | C(bit2) [| F(bit5) if fpu and not zfinx] [| X(bit23) if pulpv2]
        _MISA_BASE = 0x40001104   # RV32 | I | M | C
        fpu_in_isa = fpu and not zfinx
        misa = _MISA_BASE | (0x20 if fpu_in_isa else 0) | (0x00800000 if pulpv2 else 0)

        mimpid = 0x1 if (fpu or pulpv2) else 0x0

        super().__init__(parent, name, isa=isa,
            riscv_dbg_unit=True, fetch_enable=fetch_enable, boot_addr=boot_addr,
            first_external_pcer=12, debug_handler=0x1a110800, misa=misa, core="riscv",
            cluster_id=cluster_id, core_id=core_id, wrapper="pulp/cpu/iss/default_iss_wrapper.cpp",
            scoreboard=True, timed=True, handle_misaligned=True, zfinx=zfinx,
            riscv_exceptions=True)

        # CV32E40P / PULP vendor CSR values — written to JSON config.
        # These are read by Cv32e40pCsr::build() in csr_cv32e40p.cpp.
        # Values derived from cv32e40p_cs_registers.sv and cv32e40p_pkg.sv.
        # mstatus writable bits: MIE[3], MPIE[7], MPP[12:11]
        # With FPU (not zfinx): add FS[14:13]
        # D62: mstatus effective write mask — matches RTL always_ff forcing (PULP_SECURE=0).
        # RTL cv32e40p_cs_registers.sv:1222-1230 forces MPP=M, MPRV=0, UIE=0, UPIE=0.
        # Only MIE(3) + MPIE(7) are writable. With FPU: add FS(14:13).
        mstatus_mask = 0x6088 if fpu_in_isa else 0x0088
        # mcountinhibit: CY(0) + IR(2) + HPM3..HPM(2+N) — bit 1 always reserved
        # With N=1: mask=0x0D (bits 0,2,3). With N=29: mask=0xFFFFFFFD (all except bit 1).
        mcountinhibit_mask = 0x5 | (((1 << num_mhpmcounters) - 1) << 3)
        self.add_properties({
            'mvendorid_value': 0x602,
            'marchid_value': 0x4,
            'mimpid': mimpid,
            'fpu_in_isa': fpu_in_isa,
            'mtvec_reset': 0x1,
            'mtvec_write_mask': 0xFFFFFF01,     # bits[7:1] hardwired 0
            'mcause_mask': 0x8000001F,           # bit[31] + bits[4:0]
            'mcountinhibit_mask': mcountinhibit_mask,
            'mstatus_write_mask': mstatus_mask,  # MPP/MPIE/MIE [+FS if FPU]
            'mie_write_mask': 0xFFFF0888,        # IRQ_MASK
            'mip_write_mask': 0x0,               # read-only in M-mode
            'mtval_write_mask': 0x00000000,      # D58: CV32E40P mtval is hardwired to 0 (BUG-23)
            'tdata1_reset': 0x28001040,          # type=2,dmode=1,action=1,m=1,u=0 (no U-mode)
            'tdata1_write_mask': 0x00000000,     # D59: writable ONLY from Debug Mode (RTL: tmatch_control_we = csr_we_int & debug_mode_i)
            'tdata2_write_mask': 0x00000000,     # D59: writable ONLY from Debug Mode (RTL: tmatch_value_we = csr_we_int & debug_mode_i)
            'tinfo_reset': 0x4,                  # bit[2] = mcontrol supported
            'num_mhpmcounters': num_mhpmcounters,
            'num_hpm_events': 16,
            'pulpv2': pulpv2,                      # COREV_PULP — gates UHARTID/PRIVLV
            'zfinx': zfinx,                        # ZFINX mode — gates CSR_ZFINX (0xCD2)
        })

        self.add_c_flags([
            "-DPIPELINE_STALL_THRESHOLD=1",
            "-DCONFIG_ISS_CORE=cv32e40p",
            f"-DCONFIG_GVSOC_CORE_VERSION={core_version}",
            "-DCONFIG_GVSOC_ISS_HWLOOP=1",
            "-DCONFIG_GVSOC_ISS_CV32E40P=1",
        ])

        # CV32E40P-specific CSR subclass (Cv32e40pCsr) — must be compiled
        # into the ISS model .so.  The base riscv.py add_sources() list
        # doesn't include it because it's CV32E40P-specific.
        self.add_sources([
            "cpu/iss/src/csr_cv32e40p.cpp",
            "cpu/iss/src/irq_cv32e40p.cpp",
        ])
