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
from cpu.iss.isa_gen.isa_pulpnn import PulpNn


def _build_fc_isa(name, pulpnn=False, pulpv2=True):

    extensions = [ Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ]

    if pulpv2:
        extensions.append(PulpV2())

    if pulpnn:
        extensions.append(PulpNn())

    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, 'rv32imfc', extensions=extensions)

    return isa

def _build_cluster_isa(name, pulpnn=False, pulpv2=True, fp32_latency=0):

    extensions = [ Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ]

    if pulpv2:
        extensions.append(PulpV2())

    if pulpnn:
        extensions.append(PulpNn())

    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, 'rv32imfc', extensions=extensions)

    if fp32_latency != 0:
        # Pipeline registers in the FP32 FMA unit (C_LAT_FP32 of the core): an
        # instruction depending on an FP32 add / mul / FMA gets its result as
        # many cycles later than from the combinational FMA of pulp-platform/pulp
        # (measured on the RTL with 1: 2 cycles from one to the next one using
        # it, independent ones still one per cycle)
        for insn in isa.get_insns():
            if insn.label.endswith('.s') and any(tag in insn.tags for tag in ('fmadd', 'fadd', 'fmul')):
                insn.set_latency(fp32_latency)

    return isa





_cluster_isa = {}
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

    def __init__(self, parent, name, cluster_id: int=None, core_id: int=None, pulpv2=True, pulpnn=True,
            nb_fpu=4, fp32_latency=0):

        # The ISA is generated once per FP32 latency (its timing is compiled in)
        if fp32_latency not in _cluster_isa:
            _cluster_isa[fp32_latency] = _build_cluster_isa(
                'pulp_cluster' if fp32_latency == 0 else f'pulp_cluster_fp32lat{fp32_latency}',
                pulpv2=pulpv2, pulpnn=pulpnn, fp32_latency=fp32_latency)

        super().__init__(parent, name, isa=_cluster_isa[fp32_latency], cluster_id=cluster_id, core_id=core_id, external_pccr=True)

        # The FPUs of the cluster the cores share, core i on FPU i % nb_fpu
        self.add_c_flags([f'-DCONFIG_GVSOC_ISS_PULP_NB_FPU={nb_fpu}'])




class FcCore(PulpCore):

    def __init__(self, parent, name, fetch_enable: bool=False, boot_addr: int=0, cluster_id: int=31, pulpv2=True, pulpnn=True):

        global _fc_isa

        if _fc_isa is None:
            _fc_isa = _build_fc_isa('pulp_fc', pulpv2=pulpv2, pulpnn=pulpnn)

        super().__init__(parent, name, isa=_fc_isa, cluster_id=cluster_id, core_id=0,
            fetch_enable=fetch_enable, boot_addr=boot_addr)
