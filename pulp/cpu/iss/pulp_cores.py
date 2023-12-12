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


def __build_isa(name):
    isa = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(name, 'rv32imfcXpulpv2Xf8Xf16XfvecXfauxXf16altXgap9')

    isa.add_tree(IsaDecodeTree('sfloat', [Xf16(), Xf16alt(), Xf8(), Xfvec(), Xfaux()]))
    isa.add_tree(IsaDecodeTree('pulpv2', [PulpV2()]))

    for insn in isa.get_insns():

        if "load" in insn.tags:
            insn.get_out_reg(0).set_latency(2)
        elif "mul" in insn.tags:
            insn.get_out_reg(0).set_latency(2)
        elif "mulh" in insn.tags:
            insn.set_latency(5)

    return isa




def __build_cluster_isa():

    def __attach_resource(insn, resource, latency, bandwidth, tags=[]):
        if len(tags) == 0:
            insn.attach_resource(resource, latency, bandwidth)
        else:
            for tag in tags:
                if tag in insn.tags:
                    insn.attach_resource(resource, latency, bandwidth)

    isa = __build_isa('pulp_cluster')

    # Declare the 3 kind of shared resources with appropriate latency and bandwidth
    isa.add_resource('fpu_base', instances=4)
    isa.add_resource('fpu_sqrt', instances=1)

    # And attach resources to instructions
    for insn in isa.get_tree('f').get_insns() + isa.get_tree('sfloat').get_insns():

        # All float operations are handled by the same unit
        __attach_resource(insn, 'fpu_base', latency=1, bandwidth=1, tags=[
            'fmadd', 'fadd', 'fmul', 'fconv', 'fother',
            'sfmadd', 'sfadd', 'sfmul', 'sfconv', 'sfother',
        ])

        # Except div, rem and sqrt which goes to the sqrt unit
        __attach_resource(insn, 'fpu_sqrt', latency=14, bandwidth=14, tags=[
            'fdiv'
        ])

        # Except div, rem and sqrt which goes to the sqrt unit
        __attach_resource(insn, 'fpu_sqrt', latency=10, bandwidth=10, tags=[
            'sfdiv'
        ])


    return isa




def __build_fc_isa():
    isa = __build_isa('pulp_fc')

    for insn in isa.get_insns():

        if "fdiv" in insn.tags:
            insn.get_out_reg(0).set_latency(15)
        elif "sfdiv" in insn.tags:
            insn.get_out_reg(0).set_latency(15)

    return isa



_cluster_isa = __build_cluster_isa()

_fc_isa = __build_fc_isa()





class PulpCore(cpu.iss.riscv.RiscvCommon):

    def __init__(self, parent, name, isa, cluster_id: int, core_id: int, fetch_enable: bool=False,
            boot_addr: int=0):

        super().__init__(parent, name, isa=isa,
            riscv_dbg_unit=True, fetch_enable=fetch_enable, boot_addr=boot_addr,
            first_external_pcer=12, debug_handler=0x1a190800, misa=0x40000000, core="ri5ky",
            cluster_id=cluster_id, core_id=core_id, wrapper="pulp/cpu/iss/pulp_iss_wrapper.cpp")

        self.add_c_flags([
            "-DPIPELINE_STAGES=2",
            "-DCONFIG_ISS_CORE=ri5cy",
            '-DISS_SINGLE_REGFILE',
        ])




class ClusterCore(PulpCore):

    def __init__(self, parent, name, cluster_id: int=None, core_id: int=None):

        super().__init__(parent, name, isa=_cluster_isa, cluster_id=cluster_id, core_id=core_id)



class FcCore(PulpCore):

    def __init__(self, parent, name, fetch_enable: bool=False, boot_addr: int=0):

        super().__init__(parent, name, isa=_fc_isa, cluster_id=31, core_id=0,
            fetch_enable=fetch_enable, boot_addr=boot_addr)
