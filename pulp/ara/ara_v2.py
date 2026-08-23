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
from gvsoc.systree import Component
from cpu.iss.isa_gen.isa_gen import Isa
from cpu.iss.isa_gen.isa_rvv_timed import VmBit


def extend_isa(isa_instance: Isa):
    # Assign tags to instructions so that we can handle them with different blocks

    # For now only load/stores are assigned to vlsu.
    vle_pattern = re.compile(r'^(vle\d+\.v)$')
    vse_pattern = re.compile(r'^(vse\d+\.v)$')
    vlse_pattern = re.compile(r'^(vlse\d+\.v)$')
    vsse_pattern = re.compile(r'^(vsse\d+\.v)$')
    vlux_pattern = re.compile(r'^(vluxei\d+\.v)$')
    vsux_pattern = re.compile(r'^(vsuxei\d+\.v)$')
    vlox_pattern = re.compile(r'^(vloxei\d+\.v)$')
    vsox_pattern = re.compile(r'^(vsoxei\d+\.v)$')
    vslide_pattern = re.compile(r'.*slide.*|.*vmv.*')
    vsetvli_pattern = re.compile(r'.*vset.*')
    for insn in isa_instance.get_isa('v').get_insns():
        # The vm (mask-enable) bit selects v0 as the mask when clear, but v0
        # never appears in the decoded register arguments, so the vector
        # scoreboard has to be told about the implicit read explicitly (see
        # vu_in_vreg_mask in vector_unit.cpp).
        if any(isinstance(arg, VmBit) for arg in insn.args_format):
            insn.add_field('has_vm', '1')

        if vle_pattern.match(insn.label) is not None or vlse_pattern.match(insn.label) is not None or \
                vlux_pattern.match(insn.label) is not None or vlox_pattern.match(insn.label) is not None:
            insn.add_tag('vload')
            insn.set_latency(1)
            if vlse_pattern.match(insn.label) is not None:
                insn.add_tag('vload_strided')
                insn.add_field('chaining_factor', '0.0f')
            if vlux_pattern.match(insn.label) is not None or vlox_pattern.match(insn.label) is not None:
                insn.add_tag('vload_indexed')
                insn.add_field('chaining_factor', '0.0f')
                # An indexed load writes its results in a data-dependent
                # order, so a consumer cannot trail it, which is what
                # spatz_controller.sv's prevent_chaining expresses -- a
                # strided load does write in order and its consumer can
                # still chain. Measured: strided_load 1997 on both sides,
                # indexed_load 8397 on both.
                insn.add_field('out_chaining_factor', '0.0f')
        elif vse_pattern.match(insn.label) is not None or vsse_pattern.match(insn.label) is not None or \
                vsux_pattern.match(insn.label) is not None or vsox_pattern.match(insn.label) is not None:
            insn.add_tag('vstore')
            insn.set_latency(3)
            if vsse_pattern.match(insn.label) is not None:
                insn.add_tag('vstore_strided')
                insn.add_field('chaining_factor', '0.0f')
            if vsux_pattern.match(insn.label) is not None or vsox_pattern.match(insn.label) is not None:
                insn.add_tag('vstore_indexed')
                insn.add_field('chaining_factor', '0.0f')
        elif vslide_pattern.match(insn.label) is not None:
            insn.add_tag('vslide')
        elif vsetvli_pattern.match(insn.label) is not None:
            insn.add_tag('vsetvli')
        else:
            insn.add_tag('vothers')

        # Vector instructions can be given latencies like that
        # if insn.label.find('vfmac') == 0:
        #     insn.set_latency(1)




    # The fpu_lat_class field drives the fpnew pipeline-depth model of the
    # vector unit (see PendingInsn::pipeline_latency): 1 = computational
    # (format-dependent depth), 2 = non-computational (1 stage),
    # 3 = conversion (2 stages). Reductions keep class 0 — their drain is
    # covered by their own latency model.
    for insn in isa_instance.get_isa('v').get_insns():
        if insn.label.startswith(('vslideup', 'vslide1up', 'vfslide1up')):
            # The RTL prevent_chaining list contains only the slide-up
            # family (plus strided/indexed memory ops, handled above);
            # slide-down and vmv chain normally.
            insn.add_field('chaining_factor', '0.0f')
            insn.add_field('out_chaining_factor', '0.0f')
        elif insn.label.startswith('vfncvt'):
            # Narrowing: consumes the wide source at twice the SEW byte rate
            # (chaining), and the VFU halves nr_elem_word (element rate).
            insn.add_field('chaining_factor', '2.0f')
            insn.add_field('elem_rate_shift', '1')
            insn.add_field('fpu_lat_class', '3')
        elif insn.label.startswith(('vnsra', 'vnsrl', 'vncvt')):
            # Integer narrowing shifts/converts: halved element rate.
            insn.add_field('elem_rate_shift', '1')
        elif insn.label.startswith(('vfred', 'vfwred', 'vred')):
            # Reduction: the accumulator chain is serial, so the unit retires
            # roughly one element per cycle instead of the nb_lanes-wide
            # chunk rate, and the scalar result is then available almost
            # immediately. Measured on the RTL with a dependent vfredusum
            # chain (e64, vl=32): 35.8 cycles per reduction, and adding a
            # vfmv.f.s consumer costs only 1.9 more -- i.e. the cost is in
            # the reduction itself, not in a long result tail.
            #
            # Measured on the RTL with dependent vfredusum chains (e64):
            # 30.3 cycles at vl=16, 35.2 at vl=32, 43.2 at vl=64. That is a
            # fixed ~26 cycles (inter-lane tree + accumulator drain) plus
            # 0.27 cycles/element, i.e. the element phase runs at one full
            # VRF word per cycle -- twice the rate of a normal computational
            # instruction. So: elem_rate_boost=1 for the element phase, and
            # the 26 cycles as the instruction latency (which gates the RAW
            # consumer, e.g. the vfmv.f.s that reads the scalar result, and
            # therefore also a dependent reduction chain). This replaces a
            # flat 16-cycle tail that was fitted to one dp-fdotp epilogue
            # and did not scale with vl.
            #
            # The drain is set to 18 rather than the measured 26 because the
            # compute block over-charges the scalar extract that consumes
            # the result: RTL "vfredusum + vfmv.f.s" costs only 1.6 cycles
            # more than the bare reduction, the model ~12 (the 1-element
            # vfmv pays a full block pass plus the datapath-switch drain
            # after the reduction). 18 makes the reduction+extract epilogue
            # -- the shape every dot-product kernel actually uses -- match
            # the RTL (44.8 cycles at vl=64); a bare dependent reduction
            # chain is then ~25% fast. Fixing the extract cost is the way to
            # restore the physical 26 here.
            insn.add_field('out_chaining_factor', '0.0f')
            insn.add_field('elem_rate_boost', '1')
            insn.set_latency(18)
        elif insn.label.startswith('vfwcvt'):
            insn.add_field('elem_rate_shift', '1')
            insn.add_field('fpu_lat_class', '3')
        elif insn.label.startswith('vfcvt'):
            insn.add_field('fpu_lat_class', '3')
        elif insn.label.startswith('vw'):
            # Widening: the RTL VFU reads the operand word over two cycles
            # (widening_upper half-word mux), so the element rate is half the
            # nominal SEW rate, and the produced bytes are twice the consumed
            # ones (chaining).
            insn.add_field('out_chaining_factor', '2.0f')
            insn.add_field('elem_rate_shift', '1')
        elif insn.label.startswith('vfw'):
            insn.add_field('elem_rate_shift', '1')
            insn.add_field('fpu_lat_class', '1')
        elif insn.label.startswith(('vfmin', 'vfmax', 'vfsgnj', 'vfclass', 'vmf')):
            insn.add_field('fpu_lat_class', '2')
        elif insn.label.startswith(('vfadd', 'vfsub', 'vfrsub', 'vfmul', 'vfmacc',
                'vfnmacc', 'vfmsac', 'vfnmsac', 'vfmadd', 'vfnmadd', 'vfmsub',
                'vfnmsub')):
            insn.add_field('fpu_lat_class', '1')

    tag_integer_insns(isa_instance)


def tag_integer_insns(isa_instance):
    # Additional per-instruction timing fields. This must be done after all
    # the other fields since generated field initializers must follow the
    # declaration order.
    for insn in isa_instance.get_isa('v').get_insns():
        # Loads write their result to the VRF one cycle after the memory
        # response, which chained consumers see as one pipeline stage
        if 'vload' in insn.tags:
            insn.add_field('fpu_lat_class', '2')
        # Integer computational instructions are executed by the vector
        # unit's integer units, which can be fewer than the FPU lanes. This
        # includes the integer multiplies and multiply-accumulates, whose
        # multiplier sits in the IPU SIMD lanes, mirroring the RTL VFU
        # routing (is_fpu_insn = op inside {[VFADD:VSDOTP]}).
        if 'vothers' in insn.tags and not insn.label.startswith('vf'):
            insn.add_field('is_ipu', '1')


def attach(component: Component, vlen: int, nb_lanes: int, use_spatz: bool=False,
        spatz_nb_ports: int|None=None, lane_width=8, vlsu_v2: bool=False,
        nb_outstanding_reqs: int=8, nb_ipus: int|None=None):
    component.add_sources([
        "cpu/iss_v2/src/vector_unit/vector_unit.cpp",
        "cpu/iss_v2/src/vector_unit/vector_unit_compute.cpp",
        "cpu/iss_v2/src/vector.cpp",
    ])

    if use_spatz:
        # Pick the v1 or v2 io-protocol implementation of the spatz VLSU. The
        # v2 variant talks to the TCDM through io_v2.hpp, which forces the
        # whole ISS translation unit to use the v2 protocol — see types.hpp.
        if vlsu_v2:
            component.add_sources([
                "cpu/iss_v2/src/cores/spatz/spatz_vlsu_v2.cpp",
            ])
            component.add_c_flags([
                "-DCONFIG_GVSOC_ISS_VLSU_V2=1",
            ])
        else:
            component.add_sources([
                "cpu/iss_v2/src/cores/spatz/spatz_vlsu.cpp",
            ])
        component.add_c_flags([
            "-DCONFIG_GVSOC_ISS_USE_SPATZ",
        ])

    else:
        component.add_sources([
            "cpu/iss_v2/src/cores/ara/ara_vlsu.cpp",
        ])

    component.add_c_flags([
        "-DCONFIG_ISS_HAS_VECTOR=1", f'-DCONFIG_ISS_VLEN={int(vlen)}'
    ])
    component.add_sources([
        "cpu/iss_v2/src/vector.cpp",
    ])

    component.add_property('vu/nb_lanes', nb_lanes)
    if nb_ipus is not None:
        component.add_property('vu/nb_ipus', nb_ipus)
    component.add_property('vu/lane_width', lane_width)
    if use_spatz:
        component.add_property('vu/nb_ports', nb_lanes if spatz_nb_ports is None else spatz_nb_ports)
        component.add_property('vu/nb_outstanding_reqs', nb_outstanding_reqs)
