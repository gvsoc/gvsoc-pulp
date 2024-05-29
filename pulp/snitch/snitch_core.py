#
# Copyright (C) 2020 ETH Zurich and University of Bologna
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
from pulp.snitch.snitch_isa import *
from cpu.iss.isa_gen.isa_rvv import *
from cpu.iss.isa_gen.isa_smallfloats import *
import gvsoc.systree


def add_latencies(isa):

    # To model the fact that alt fp16 and fp18 instructions are dynamically enabled through a
    # CSR, we insert a stub which call the proper handler depending on the CSR value
    xf16_isa = isa.get_isa('f16')
    for insn in xf16_isa.get_insns():
        insn.exec_func = insn.exec_func + '_switch'
        insn.exec_func_fast = insn.exec_func_fast + '_switch'

    xf8_isa = isa.get_isa('f8')
    for insn in xf8_isa.get_insns():
        insn.exec_func = insn.exec_func + '_switch'
        insn.exec_func_fast = insn.exec_func_fast + '_switch'

    # Set snitch instruction latency:
    # 1. the latency of instruction, the core stalls for n cycles at the current instruction. (insn->latency)
    # 2. the latency of output register, the output is ready after n cycles to check data dependency. (reg.latency)
    for insn in isa.get_insns():

        # Pipelined instruction (allow parallelsim)
        if "fmadd" in insn.tags:
            insn.set_latency(3)
            insn.get_out_reg(0).set_latency(3)
        elif "fadd" in insn.tags:
            insn.set_latency(3)
            insn.get_out_reg(0).set_latency(3)
        elif "fmul" in insn.tags:
            insn.set_latency(3)
            insn.get_out_reg(0).set_latency(3)
        elif "fnoncomp" in insn.tags:
            insn.set_latency(1)
            insn.get_out_reg(0).set_latency(1)
        elif "fconv" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)
        elif "fmv" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)
        elif "fother" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)
        elif "fdiv" in insn.tags:
            insn.set_latency(11)
            insn.get_out_reg(0).set_latency(11)
        elif "load" in insn.tags and not "fload" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)


class Snitch(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            inc_spatz: bool=False,
            core_id: int=0):


        isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_" + isa, isa,
            extensions=[ Rv32ssr(), Rv32frep(), Xdma(), Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ] )

        add_latencies(isa_instance)

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True, 
            prefetcher_size=32, custom_sources=True)

        self.add_c_flags([
            "-DPIPELINE_STAGES=1",
            "-DCONFIG_ISS_CORE=snitch",
        ])

        self.add_sources([
            "cpu/iss/src/snitch/snitch.cpp",
            "cpu/iss/src/snitch/regfile.cpp",
            "cpu/iss/src/snitch/ssr.cpp",
            "cpu/iss/src/prefetch/prefetch_single_line.cpp",
            "cpu/iss/src/csr.cpp",
            "cpu/iss/src/exec/exec_inorder.cpp",
            "cpu/iss/src/snitch/decode.cpp",
            "cpu/iss/src/lsu.cpp",
            "cpu/iss/src/timing.cpp",
            "cpu/iss/src/insn_cache.cpp",
            "cpu/iss/src/snitch/iss.cpp",
            "cpu/iss/src/core.cpp",
            "cpu/iss/src/exception.cpp",
            "cpu/iss/src/regfile.cpp",
            "cpu/iss/src/resource.cpp",
            "cpu/iss/src/trace.cpp",
            "cpu/iss/src/syscalls.cpp",
            "cpu/iss/src/memcheck.cpp",
            "cpu/iss/src/htif.cpp",
            "cpu/iss/src/mmu.cpp",
            "cpu/iss/src/pmp.cpp",
            "cpu/iss/src/gdbserver.cpp",
            "cpu/iss/src/dbg_unit.cpp",
            "cpu/iss/flexfloat/flexfloat.c",
        ])

        if inc_spatz:
            self.add_sources([
                "cpu/iss/src/spatz.cpp",
            ])

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_req', itf, signature='wire<bool>')


class SnitchBare(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            inc_spatz: bool=False,
            core_id: int=0):


        isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_" + isa, isa,
            extensions=[ Xdma() ] )

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True)

        self.add_c_flags([
            "-DCONFIG_ISS_CORE=snitch_bare",
        ])

        self.add_sources([
            "cpu/iss/src/snitch_bare/snitch.cpp",
        ])

        if inc_spatz:
            self.add_sources([
                "cpu/iss/src/spatz.cpp",
            ])

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_req', itf, signature='wire<bool>')



class Snitch_fp_ss(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            inc_spatz: bool=False,
            core_id: int=0,
            timed: bool=False):


        isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_fp_ss_" + isa, isa,
            extensions=[ Rv32ssr(), Rv32frep(), Xdma(), Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ] )

        add_latencies(isa_instance)

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True, 
            prefetcher_size=32, timed=timed, custom_sources=True)

        self.add_c_flags([
            "-DCONFIG_ISS_CORE=snitch_fp_ss",
        ])

        self.add_sources([
            "cpu/iss/src/snitch/snitch_fp_ss.cpp",
            "cpu/iss/src/snitch/ssr.cpp",
            "cpu/iss/src/prefetch/prefetch_single_line.cpp",
            "cpu/iss/src/csr.cpp",
            "cpu/iss/src/snitch_fp_ss/exec_inorder.cpp",
            "cpu/iss/src/snitch/decode.cpp",
            "cpu/iss/src/lsu.cpp",
            "cpu/iss/src/timing.cpp",
            "cpu/iss/src/insn_cache.cpp",
            "cpu/iss/src/snitch_fp_ss/iss.cpp",
            "cpu/iss/src/core.cpp",
            "cpu/iss/src/exception.cpp",
            "cpu/iss/src/regfile.cpp",
            "cpu/iss/src/resource.cpp",
            "cpu/iss/src/trace.cpp",
            "cpu/iss/src/syscalls.cpp",
            "cpu/iss/src/memcheck.cpp",
            "cpu/iss/src/htif.cpp",
            "cpu/iss/src/mmu.cpp",
            "cpu/iss/src/pmp.cpp",
            "cpu/iss/src/gdbserver.cpp",
            "cpu/iss/src/dbg_unit.cpp",
            "cpu/iss/flexfloat/flexfloat.c",
        ])

        if inc_spatz:
            self.add_sources([
                "cpu/iss/src/spatz.cpp",
            ])

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_req', itf, signature='wire<bool>')




class Spatz(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            use_rv32v=False):


        extensions = [ Rv32v() ]

        isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("spatz_" + isa, isa, extensions=extensions)

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="spatz")

        self.add_c_flags([
            "-DPIPELINE_STAGES=1",
            "-DCONFIG_ISS_CORE=spatz",
        ])

        self.add_sources([
            "cpu/iss/src/spatz.cpp",
        ])