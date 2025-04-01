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


def __build_fc_isa(name):

    extensions = [ PulpV2(), Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ]

    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, 'rv32imc', extensions=extensions)

    return isa

def __build_cluster_isa(name):

    extensions = [ PulpV2(), Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux() ]

    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, 'rv32imfc', extensions=extensions)

    return isa





_cluster_isa = __build_cluster_isa('pulp_cluster')

_fc_isa = __build_fc_isa('pulp_fc')






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
            '-DCONFIG_GVSOC_ISS_NO_MSTATUS_FS=1'
        ])




class ClusterCore(PulpCore):

    def __init__(self, parent, name, cluster_id: int=None, core_id: int=None):

        super().__init__(parent, name, isa=_cluster_isa, cluster_id=cluster_id, core_id=core_id, external_pccr=True)



class FcCore(PulpCore):

    def __init__(self, parent, name, fetch_enable: bool=False, boot_addr: int=0):

        super().__init__(parent, name, isa=_fc_isa, cluster_id=31, core_id=0,
            fetch_enable=fetch_enable, boot_addr=boot_addr)
