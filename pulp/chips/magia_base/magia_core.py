#
# Copyright (C) 2025 ETH Zurich, University of Bologna and Fondazione ChipsIT
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

import gvsoc.systree
import cpu.iss.riscv
from cpu.iss.isa_gen.isa_smallfloats import *
from pulp.chips.magia_base.magia_isa import *

# Tentative model of the cv32e40x adapted from pulp_cores.py
'''
class CV32CoreTest(cpu.iss.riscv.RiscvCommon):
    def __init__(self, parent, name, cluster_id: int, core_id: int,
                 fetch_enable: bool=False, boot_addr: int=0, external_pccr: bool=False):

        fc_isa = self.__build_fc_isa
        super().__init__(parent, name, isa=fc_isa, riscv_dbg_unit=True,
                         fetch_enable=fetch_enable, boot_addr=boot_addr,
                         first_external_pcer=12, debug_handler=0x1a190800,
                         misa=0x40000000, core="ri5ky", cluster_id=cluster_id,
                         core_id=core_id, wrapper="pulp/cpu/iss/pulp_iss_wrapper.cpp",
                         scoreboard=True, timed=True, handle_misaligned=True,
                         external_pccr=external_pccr)

        self.add_c_flags([
            "-DPIPELINE_STALL_THRESHOLD=1",
            "-DCONFIG_ISS_CORE=ri5cy",
            '-DCONFIG_GVSOC_ISS_NO_MSTATUS_FS=1'
        ])

    def __build_fc_isa():
        exts = [ PulpV2(), Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ]
        isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa('fc', 'rv32imc', extensions=exts)
        return isa
'''

# Basic rv32 core
class CV32CoreTest(cpu.iss.riscv.RiscvCommon):
    def __init__(self, parent: gvsoc.systree.Component, name: str, binaries: list=[],
                 fetch_enable: bool=False, boot_addr: int=0, timed: bool=True,
                 core_id: int=0):

        # Properties
        isa_str = 'rv32imac'
        misa = 0x40000000
        debug_handler = 0x1a190800
        fetch_enable = False
        riscv_exceptions = True

        # Instantiates the ISA from the string.
        isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa('cv32-base', isa_str, extensions=[iDMA_Ctrl(), Rv32redmule(), FSync(), Xf16alt()])

        super().__init__(parent, name, isa=isa, misa=misa, core_id=core_id,
                         debug_handler=debug_handler, fetch_enable=fetch_enable,
                         riscv_exceptions=riscv_exceptions)

        # TODO check later
        self.add_c_flags([
            "-DPIPELINE_STALL_THRESHOLD=1",
            "-DCONFIG_ISS_CORE=riscv",
        ])
