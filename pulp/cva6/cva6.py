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
import os



isa_instances = {}


class CVA6(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            core_id: int=0,
            htif: bool=False):


        isa_instance = isa_instances.get(isa)

        if isa_instances.get(isa) is None:
            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("cva6_" + isa, isa,
                extensions=[ ] )
            isa_instances[isa] = isa_instance

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="cva6", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True,
            prefetcher_size=64, htif=htif, binaries=binaries, timed=True, internal_atomics=True,
            supervisor=True, user=True, mmu=True, pmp=True, custom_sources=True)

        self.add_c_flags([
            "-DCONFIG_ISS_CORE=cva6",
        ])

        self.add_sources([
            "cpu/iss/src/cva6/cva6.cpp",
            "cpu/iss/src/prefetch/prefetch_single_line.cpp",
            "cpu/iss/src/csr.cpp",
            "cpu/iss/src/exec/exec_inorder.cpp",
            "cpu/iss/src/decode.cpp",
            "cpu/iss/src/lsu.cpp",
            "cpu/iss/src/timing.cpp",
            "cpu/iss/src/insn_cache.cpp",
            "cpu/iss/src/core.cpp",
            "cpu/iss/src/exception.cpp",
            "cpu/iss/src/regfile.cpp",
            "cpu/iss/src/resource.cpp",
            "cpu/iss/src/trace.cpp",
            "cpu/iss/src/syscalls.cpp",
            "cpu/iss/src/htif.cpp",
            "cpu/iss/src/memcheck.cpp",
            "cpu/iss/src/mmu.cpp",
            "cpu/iss/src/pmp.cpp",
            "cpu/iss/src/gdbserver.cpp",
            "cpu/iss/src/dbg_unit.cpp",
            "cpu/iss/flexfloat/flexfloat.c",
            "cpu/iss/src/vector.cpp",
            "cpu/iss/src/cva6/cva6.cpp",
            "cpu/iss/src/ara/ara.cpp",
        ])
