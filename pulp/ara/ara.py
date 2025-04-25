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

import re

def extend_isa(isa_instance):
    # Assign tags to instructions so that we can handle them with different blocks

    # For now only load/stores are assigned to vlsu
    vle_pattern = re.compile(r'^(vle\d+\.v)$')
    vse_pattern = re.compile(r'^(vse\d+\.v)$')
    vslide_pattern = re.compile(r'.*slide.*|.*vmv.*')
    vsetvli_pattern = re.compile(r'.*vset.*')
    for insn in isa_instance.get_isa('v').get_insns():
        if vle_pattern.match(insn.label) is not None:
            insn.add_tag('vload')
        elif vse_pattern.match(insn.label) is not None:
            insn.add_tag('vstore')
        elif vslide_pattern.match(insn.label) is not None:
            insn.add_tag('vslide')
        elif vsetvli_pattern.match(insn.label) is not None:
            insn.add_tag('vsetvli')
        else:
            insn.add_tag('vothers')

def attach(component, vlen):
    component.add_sources([
        "cpu/iss/src/ara/ara.cpp",
        "cpu/iss/src/ara/ara_vlsu.cpp",
        "cpu/iss/src/ara/ara_vcompute.cpp",
        "cpu/iss/src/vector.cpp",
    ])

    component.add_c_flags([
        "-DCONFIG_ISS_HAS_VECTOR=1", f'-DCONFIG_ISS_VLEN={int(vlen)}'
    ])
    component.add_sources([
        "cpu/iss/src/vector.cpp",
    ])
    component.add_property('ara/nb_lanes', 4)
