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

from cpu.iss.isa_gen.isa_gen import *
from cpu.iss.isa_gen.isa_riscv_gen import *


class Xdma(IsaSubset):

    def __init__(self):
        super().__init__(name='Xdma', instrs=[

            Instr('dmsrc',     Format_R  ,   '0000000 ----- ----- 000 00000 0101011'),
            Instr('dmdst',     Format_R  ,   '0000001 ----- ----- 000 00000 0101011'),
            Instr('dmstr',     Format_R  ,   '0000110 ----- ----- 000 00000 0101011'),
            Instr('dmrep',     Format_R  ,   '0000111 ----- ----- 000 00000 0101011'),
            Instr('dmcpy',     Format_R  ,   '0000011 ----- ----- 000 ----- 0101011'),
            Instr('dmstat',    Format_R  ,   '0000101 ----- ----- 000 ----- 0101011'),
            Instr('dmcpyi',    Format_I1U,   '0000010 ----- ----- 000 ----- 0101011'),
            Instr('dmstati',   Format_I1U,   '0000100 ----- ----- 000 ----- 0101011'),
        ], includes=[
            '<cpu/iss_v2/include/isa/xdma.hpp>',
        ])

# Encodings for extended Snitch instruction set
      #   3 3 2 2 2 2 2       2 2 2 2 2       1 1 1 1 1       1 1 1       1 1
      #   1 0 9 8 7 6 5       4 3 2 1 0       9 8 7 6 5       4 3 2       1 0 9 8 7       6 5 4 3 2 1 0
      #   X X X X X X X   |   X X X X X   |   X X X X X   |   X X X   |   X X X X X   |   X X X X X X X
# FREP#              ui2[11:0]            |      rs1      |  ui1[2:0] |   ui0[3:0]  |        opcode

Format_FREP = [
    UnsignedImm(0, Range(8, 4)),
    UnsignedImm(1, Range(12, 3)),
    InReg(0, Range(15, 5)),
    UnsignedImm(2, Range(20, 12)),
]

# Encodings for extended Snitch instruction set
      #   3 3 2 2 2 2 2       2 2 2 2 2       1 1 1 1 1       1 1 1       1 1
      #   1 0 9 8 7 6 5       4 3 2 1 0       9 8 7 6 5       4 3 2       1 0 9 8 7       6 5 4 3 2 1 0
      #   X X X X X X X   |   X X X X X   |   X X X X X   |   X X X   |   X X X X X   |   X X X X X X X
# SCFGRI#    ui1[6:0]     |    ui0[4:0]   |     00000     |    001    |      rd       |      opcode
# SCFGWI#    ui1[6:0]     |    ui0[4:0]   |      rs1      |    010    |     00000     |      opcode
# SCFGR #    0000000      |      rs1      |     00001     |    001    |      rd       |      opcode
# SCFGW #    0000000      |      rs2      |      rs1      |    010    |     00001     |      opcode

Format_SCFGRI = [
    OutReg(0, Range(7, 5)),
    UnsignedImm(0, Range(20, 5)),
    UnsignedImm(1, Range(25, 7))
]

Format_SCFGWI = [
    InReg(0, Range(15, 5)),
    UnsignedImm(0, Range(20, 5)),
    UnsignedImm(1, Range(25, 7))
]

Format_SCFGR = [
    OutReg(0, Range(7, 5)),
    InReg(0, Range(20, 5))
]

Format_SCFGW = [
    InReg(0, Range(15, 5)),
    InReg(1, Range(20, 5))
]



# RV32 Extension FREP
class Rv32frep(IsaSubset):

    def __init__(self):
        super().__init__(name='Xfrep', instrs=[
            Instr('frep.o', Format_FREP, '------------ ----- --- ---- 10001011', tags=["frep"]),
            Instr('frep.i', Format_FREP, '------------ ----- --- ---- 00001011', tags=["frep"]),
        ])



# RV32 Extension SSR
class Rv32ssr(IsaSubset):

    def __init__(self):
        super().__init__(name='Xssr', instrs=[
            Instr('scfgri', Format_SCFGRI, '------------ 00000 001 ---- -0101011', tags=["ssr", 'nseq', 'fp_op']),
            Instr('scfgwi', Format_SCFGWI, '------------ ----- 010 0000 00101011', tags=["ssr", 'nseq', 'fp_op']),
            Instr('scfgr', Format_SCFGR, '0000000----- 00001 001 ---- -0101011', tags=["ssr", 'nseq', 'fp_op']),
            Instr('scfgw', Format_SCFGW, '0000000----- ----- 010 0000 00101011', tags=["ssr", 'nseq', 'fp_op']),
        ])
