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

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True, 
            prefetcher_size=32)

        self.add_c_flags([
            "-DPIPELINE_STAGES=1",
            "-DCONFIG_ISS_CORE=snitch",
        ])

        self.add_sources([
            "cpu/iss/src/snitch/snitch.cpp",
            "cpu/iss/src/snitch/regfile.cpp",
            "cpu/iss/src/ssr.cpp",
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


        isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_" + isa, isa,
            extensions=[ Rv32ssr(), Rv32frep(), Xdma(), Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ] )

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True, 
            prefetcher_size=32, timed=timed)

        self.add_c_flags([
            "-DCONFIG_ISS_CORE=snitch_fp_ss",
        ])

        self.add_sources([
            "cpu/iss/src/snitch/snitch_fp_ss.cpp",
            "cpu/iss/src/ssr.cpp",
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
            "-DCONFIG_ISS_CORE=snitch",
        ])

        self.add_sources([
            "cpu/iss/src/spatz.cpp",
        ])