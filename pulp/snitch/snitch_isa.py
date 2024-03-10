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
        ])
