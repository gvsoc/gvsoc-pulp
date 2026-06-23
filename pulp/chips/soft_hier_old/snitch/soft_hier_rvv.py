#
# Local RVV compatibility extensions for the old SoftHier target.
#
# These instructions are emitted by old SoftHier applications as raw .word
# encodings. Keep the decode and handlers target-local so the shared core RVV
# model remains unchanged.
#

from cpu.iss.isa_gen.isa_gen import *


Format_SOFT_HIER_FEXP = [
    OutVRegF(0, Range(7, 5)),
    InVRegF(0, Range(15, 5)),
    UnsignedImm(0, Range(25, 1)),
]

Format_SOFT_HIER_OPVX = [
    OutVRegF(0, Range(7, 5)),
    InReg(0, Range(15, 5)),
    InVRegF(1, Range(20, 5)),
    InVRegF(2, Range(7, 5), dumpName=False),
    UnsignedImm(0, Range(25, 1)),
]


def _soft_hier_instr(label, fmt, encoding):
    instr = Instr(label, fmt, encoding, tags=['fp_op'])
    instr.set_exec_label('soft_hier_' + instr.name)
    return instr


def _soft_hier_rvv_instrs():
    return [
        _soft_hier_instr(
            'vfexp.vv',
            Format_SOFT_HIER_FEXP,
            '001100 - ----- ----- 001 ----- 1010111',
        ),
        _soft_hier_instr(
            'vfredmax.vx',
            Format_SOFT_HIER_OPVX,
            '000111 - ----- ----- 011 ----- 1010111',
        ),
        _soft_hier_instr(
            'vfredsum.vx',
            Format_SOFT_HIER_OPVX,
            '000001 - ----- ----- 011 ----- 1010111',
        ),
    ]


def extend_soft_hier_old_rvv(isa_instance):
    v_isa = isa_instance.get_isa('v')
    if v_isa is None:
        return

    isa_instance.add_include(
        '<pulp/chips/soft_hier_old/snitch/soft_hier_rvv.hpp>'
    )

    for instr in _soft_hier_rvv_instrs():
        if isa_instance.get_insn(instr.name) is not None:
            continue

        instr.set_isa(v_isa)
        v_isa.instrs.append(instr)
        v_isa.instrs_dict[instr.name] = instr
        isa_instance.instrs_dict[instr.name] = instr

        for tag in instr.tags:
            isa_instance.add_insn_tag(instr, tag)
