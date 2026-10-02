# SPDX-FileCopyrightText: 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Marco Paci (marco.paci@chips.it)

"""CORE-V (Xpulp v2) instruction subset of the CV32E40P, for the iss_v2 core."""

from cpu.iss.isa_gen.isa_gen import *
from cpu.iss.isa_gen.isa_riscv_gen import *
from cpu.iss.isa_gen.isa_corev import Format_SB2, Format_LPOST, Format_LRPOST, Format_LR, Format_SPOST, Format_SRPOST, Format_SR, Format_HL0, Format_HL1, Format_RRRR, Format_RRRR2, Format_RRRU2, Format_RRRRU, Format_R1, Format_I1U, Format_I4U, Format_I5U, Format_I5U2, Format_BITREV

# Formats of isa_pulpv2.py for the SIMD instructions with a scalar operand, which
# isa_corev does not export.
Format_RRU = [ OutReg(0, Range(7,  5)),
               InReg (0, Range(15, 5)),
               UnsignedImm(0, Ranges([[25, 1, 0], [20, 5, 1]])),
]
Format_RRU2 = [ OutReg(0, Range(7,  5)),
                InReg (0, Range(15, 5)),
                UnsignedImm(0, Ranges([[25, 1, 0], [20, 5, 1]])),
]
Format_RRS = [ OutReg(0, Range(7,  5)),
               InReg (0, Range(15, 5)),
               SignedImm(0, Ranges([[25, 1, 0], [20, 5, 1]])),
]
# Accumulating formats of isa_pulpv2.py (insert, sdotsp.sci, sdotusp.sci).
Format_RRRS = [ OutReg(0, Range(7,  5)),
                InReg (0, Range(7,  5)),
                InReg (1, Range(15, 5)),
                SignedImm(0, Ranges([[25, 1, 0], [20, 5, 1]])),
]
Format_RRRU = [ OutReg(0, Range(7,  5)),
                InReg (0, Range(7,  5)),
                InReg (1, Range(15, 5)),
                UnsignedImm(0, Ranges([[25, 1, 0], [20, 5, 1]])),
]

# Opcodes of the CORE-V instructions:
#   custom-0 0x0B: immediate branches, post-increment loads
#   custom-1 0x2B: post-increment stores, register loads and stores, ALU, hardware loops
#   custom-2 0x5B: bit manipulation, add/sub with normalization, multiply, MAC
#   custom-3 0x7B: SIMD

class CoreV2(IsaSubset):

    # elw is the RTL COREV_CLUSTER parameter. Without it cv.elw is illegal.
    def __init__(self, elw=False):
        instrs = []

        # --- Custom-0 (0x0B): Branching and Load Post-Increment ---
        # The names are the C++ handler names (case sensitive).
        instrs += [
            Instr('p.beqimm', Format_SB2, '------- ----- ----- 110 ----- 0001011', fast_handler=True, decode='bxx_decode', L='cv.beqimm'),
            Instr('p.bneimm', Format_SB2, '------- ----- ----- 111 ----- 0001011', fast_handler=True, decode='bxx_decode', L='cv.bneimm'),
            
            Instr('LB_POSTINC',    Format_LPOST, '------- ----- ----- 000 ----- 0001011', L='cv.lb' , fast_handler=True, tags=["load"]),
            Instr('LBU_POSTINC',   Format_LPOST, '------- ----- ----- 100 ----- 0001011', L='cv.lbu', fast_handler=True, tags=["load"]),
            Instr('LH_POSTINC',    Format_LPOST, '------- ----- ----- 001 ----- 0001011', L='cv.lh' , fast_handler=True, tags=["load"]),
            Instr('LHU_POSTINC',   Format_LPOST, '------- ----- ----- 101 ----- 0001011', L='cv.lhu', fast_handler=True, tags=["load"]),
            Instr('LW_POSTINC',    Format_LPOST, '------- ----- ----- 010 ----- 0001011', L='cv.lw' , fast_handler=True, tags=["load"]),
        ]

        if elw:
            instrs += [
                Instr('p.elw',     Format_L,     '------- ----- ----- 011 ----- 0001011', L='cv.elw', tags=["load"]),
            ]

        # --- Custom-1 (0x2B): Store Post-Increment and Register Indexed Load/Store ---
        instrs += [
            Instr('SB_POSTINC',    Format_SPOST, '------- ----- ----- 000 ----- 0101011', L='cv.sb' , fast_handler=True),
            Instr('SH_POSTINC',    Format_SPOST, '------- ----- ----- 001 ----- 0101011', L='cv.sh' , fast_handler=True),
            Instr('SW_POSTINC',    Format_SPOST, '------- ----- ----- 010 ----- 0101011', L='cv.sw' , fast_handler=True),

            Instr('LB_RR_POSTINC',   Format_LRPOST,  '0000000 ----- ----- 011 ----- 0101011', L='cv.lb' , fast_handler=True, tags=["load"]),
            Instr('LBU_RR_POSTINC',  Format_LRPOST,  '0001000 ----- ----- 011 ----- 0101011', L='cv.lbu', fast_handler=True, tags=["load"]),
            Instr('LH_RR_POSTINC',   Format_LRPOST,  '0000001 ----- ----- 011 ----- 0101011', L='cv.lh' , fast_handler=True, tags=["load"]),
            Instr('LHU_RR_POSTINC',  Format_LRPOST,  '0001001 ----- ----- 011 ----- 0101011', L='cv.lhu', fast_handler=True, tags=["load"]),
            Instr('LW_RR_POSTINC',   Format_LRPOST,  '0000010 ----- ----- 011 ----- 0101011', L='cv.lw' , fast_handler=True, tags=["load"]),

            Instr('LB_RR',    Format_LR, '0000100 ----- ----- 011 ----- 0101011', L='cv.lb' , fast_handler=True, tags=["load"]),
            Instr('LBU_RR',   Format_LR, '0001100 ----- ----- 011 ----- 0101011', L='cv.lbu', fast_handler=True, tags=["load"]),
            Instr('LH_RR',    Format_LR, '0000101 ----- ----- 011 ----- 0101011', L='cv.lh' , fast_handler=True, tags=["load"]),
            Instr('LHU_RR',   Format_LR, '0001101 ----- ----- 011 ----- 0101011', L='cv.lhu', fast_handler=True, tags=["load"]),
            Instr('LW_RR',    Format_LR, '0000110 ----- ----- 011 ----- 0101011', L='cv.lw' , fast_handler=True, tags=["load"]),

            Instr('SB_RR_POSTINC',   Format_SRPOST, '0010000 ----- ----- 011 ----- 0101011',  L='cv.sb' , fast_handler=True),
            Instr('SH_RR_POSTINC',   Format_SRPOST, '0010001 ----- ----- 011 ----- 0101011',  L='cv.sh' , fast_handler=True),
            Instr('SW_RR_POSTINC',   Format_SRPOST, '0010010 ----- ----- 011 ----- 0101011',  L='cv.sw' , fast_handler=True),

            Instr('SB_RR',    Format_SR, '0010100 ----- ----- 011 ----- 0101011', L='cv.sb', fast_handler=True),
            Instr('SH_RR',    Format_SR, '0010101 ----- ----- 011 ----- 0101011', L='cv.sh', fast_handler=True),
            Instr('SW_RR',    Format_SR, '0010110 ----- ----- 011 ----- 0101011', L='cv.sw', fast_handler=True),
        ]

        # --- Custom-1 Plane A (0x2B, funct3=011): General ALU Operations ---
        instrs += [
            Instr('p.ror',    Format_R,  '0100000 ----- ----- 011 ----- 0101011', L='cv.ror'),
            Instr('p.ff1',    Format_R1, '0100001 00000 ----- 011 ----- 0101011', L='cv.ff1'),
            Instr('p.fl1',    Format_R1, '0100010 00000 ----- 011 ----- 0101011', L='cv.fl1'),
            Instr('p.clb',    Format_R1, '0100011 00000 ----- 011 ----- 0101011', L='cv.clb'),
            Instr('p.cnt',    Format_R1, '0100100 00000 ----- 011 ----- 0101011', L='cv.cnt'),
            Instr('p.abs',    Format_R1, '0101000 00000 ----- 011 ----- 0101011', L='cv.abs'),
            Instr('p.slet',   Format_R,  '0101001 ----- ----- 011 ----- 0101011', L='cv.slet'),
            Instr('p.sletu',  Format_R,  '0101010 ----- ----- 011 ----- 0101011', L='cv.sletu'),
            Instr('p.min',    Format_R,  '0101011 ----- ----- 011 ----- 0101011', L='cv.min'),
            Instr('p.minu',   Format_R,  '0101100 ----- ----- 011 ----- 0101011', L='cv.minu'),
            Instr('p.max',    Format_R,  '0101101 ----- ----- 011 ----- 0101011', L='cv.max'),
            Instr('p.maxu',   Format_R,  '0101110 ----- ----- 011 ----- 0101011', L='cv.maxu'),
            Instr('p.exths',  Format_R1, '0110000 00000 ----- 011 ----- 0101011', L='cv.exths'),
            Instr('p.exthz',  Format_R1, '0110001 00000 ----- 011 ----- 0101011', L='cv.exthz'),
            Instr('p.extbs',  Format_R1, '0110010 00000 ----- 011 ----- 0101011', L='cv.extbs'),
            Instr('p.extbz',  Format_R1, '0110011 00000 ----- 011 ----- 0101011', L='cv.extbz'),
            Instr('p.clipi',  Format_I1U, '0111000 ----- ----- 011 ----- 0101011', L='cv.clip'),
            Instr('p.clipui', Format_I1U, '0111001 ----- ----- 011 ----- 0101011', L='cv.clipu'),
            Instr('cv.clipr',  Format_R,  '0111010 ----- ----- 011 ----- 0101011'),
            Instr('cv.clipur', Format_R,  '0111011 ----- ----- 011 ----- 0101011'),
        ]

        # --- Custom-1 Plane A (0x2B, funct3=011): Register-Register Add/Sub with Normalization ---
        # funct7[31:29]=100, funct7[27]=sub, funct7[26]=round, funct7[25]=unsigned
        # Shift amount from a register (Format_RRRR2).
        instrs += [
            Instr('p.addN',    Format_RRRR2, '1000000 ----- ----- 011 ----- 0101011', L='cv.addN'),
            Instr('p.adduN',   Format_RRRR2, '1000001 ----- ----- 011 ----- 0101011', L='cv.adduN'),
            Instr('p.addRN',   Format_RRRR2, '1000010 ----- ----- 011 ----- 0101011', L='cv.addRN'),
            Instr('p.adduRN',  Format_RRRR2, '1000011 ----- ----- 011 ----- 0101011', L='cv.adduRN'),
            Instr('p.subN',    Format_RRRR2, '1000100 ----- ----- 011 ----- 0101011', L='cv.subN'),
            Instr('p.subuN',   Format_RRRR2, '1000101 ----- ----- 011 ----- 0101011', L='cv.subuN'),
            Instr('p.subRN',   Format_RRRR2, '1000110 ----- ----- 011 ----- 0101011', L='cv.subRN'),
            Instr('p.subuRN',  Format_RRRR2, '1000111 ----- ----- 011 ----- 0101011', L='cv.subuRN'),
        ]

        # --- Custom-1 Plane A (0x2B, funct3=011): Register Bit-Manipulation ---
        # funct7[31:27]=00110 (7'b0011xxx), instr[27:25] selects variant
        instrs += [
            Instr('p.extract',  Format_R,    '0011000 ----- ----- 011 ----- 0101011', L='cv.extractr'),
            Instr('p.extractu', Format_R,    '0011001 ----- ----- 011 ----- 0101011', L='cv.extractur'),
            Instr('p.insert',   Format_I5U2, '0011010 ----- ----- 011 ----- 0101011', L='cv.insertr'),
            Instr('p.bclr',     Format_R,    '0011100 ----- ----- 011 ----- 0101011', L='cv.bclrr'),
            Instr('p.bset',     Format_R,    '0011101 ----- ----- 011 ----- 0101011', L='cv.bsetr'),
        ]

        # --- Custom-2 (0x5B): Bit-Manipulation, Add/Sub Norm, Multiply, MAC ---
        # bits[14:13]=00: Bit manipulation (immediate)
        # Selector: {bits[31:30], bit[12]}
        instrs += [
            Instr('p.extracti',  Format_I4U,    '00----- ----- ----- 000 ----- 1011011', L='cv.extract'),
            Instr('p.extractui', Format_I4U,    '01----- ----- ----- 000 ----- 1011011', L='cv.extractu'),
            Instr('p.inserti',   Format_I5U,    '10----- ----- ----- 000 ----- 1011011', L='cv.insert'),
            Instr('p.bclri',     Format_I4U,    '00----- ----- ----- 001 ----- 1011011', L='cv.bclr'),
            Instr('p.bseti',     Format_I4U,    '01----- ----- ----- 001 ----- 1011011', L='cv.bset'),
            Instr('p.bitrev',    Format_BITREV, '11000-- ----- ----- 001 ----- 1011011', L='cv.bitrev'),
        ]

        # --- Custom-2 (0x5B): Add/Sub Norm, Multiply, MAC ---
        # The 'p.' names select the PULP v2 handlers of isa/corev.hpp.
        # bits[31:30] select the variant, funct3 the family:
        #   funct3=010: addN variants    funct3=011: subN variants
        #   funct3=100: mulsN variants   funct3=101: muluN variants
        #   funct3=110: macsN variants   funct3=111: macuN variants
        # bits[31:30]: 00=base, 01=hh(MAC/MUL)/unsigned(ADD/SUB), 10=round, 11=hh+round/unsigned+round

        # Add/Sub with Normalization (funct3=010 add, 011 sub)
        instrs += [
            Instr('p.addNi',         Format_RRRU2,'00----- ----- ----- 010 ----- 1011011', L='cv.addN'),
            Instr('p.adduNi',        Format_RRRU2,'01----- ----- ----- 010 ----- 1011011', L='cv.adduN'),
            Instr('p.addRNi',        Format_RRRU2,'10----- ----- ----- 010 ----- 1011011', L='cv.addRN'),
            Instr('p.adduRNi',       Format_RRRU2,'11----- ----- ----- 010 ----- 1011011', L='cv.adduRN'),
            Instr('p.subNi',         Format_RRRU2,'00----- ----- ----- 011 ----- 1011011', L='cv.subN'),
            Instr('p.subuNi',        Format_RRRU2,'01----- ----- ----- 011 ----- 1011011', L='cv.subuN'),
            Instr('p.subRNi',        Format_RRRU2,'10----- ----- ----- 011 ----- 1011011', L='cv.subRN'),
            Instr('p.subuRNi',       Format_RRRU2,'11----- ----- ----- 011 ----- 1011011', L='cv.subuRN'),
        ]

        # MUL operations (funct3=100 signed, 101 unsigned)
        instrs += [
            Instr('p.mulsN',         Format_RRRRU,'00----- ----- ----- 100 ----- 1011011', L='cv.mulsN'),
            Instr('p.mulhhsN',       Format_RRRRU,'01----- ----- ----- 100 ----- 1011011', L='cv.mulhhsN'),
            Instr('p.mulsNR',        Format_RRRRU,'10----- ----- ----- 100 ----- 1011011', L='cv.mulsRN'),
            Instr('p.mulhhsNR',      Format_RRRRU,'11----- ----- ----- 100 ----- 1011011', L='cv.mulhhsRN'),
            Instr('p.muluN',         Format_RRRRU,'00----- ----- ----- 101 ----- 1011011', L='cv.muluN'),
            Instr('p.mulhhuN',       Format_RRRRU,'01----- ----- ----- 101 ----- 1011011', L='cv.mulhhuN'),
            Instr('p.muluNR',        Format_RRRRU,'10----- ----- ----- 101 ----- 1011011', L='cv.muluRN'),
            Instr('p.mulhhuNR',      Format_RRRRU,'11----- ----- ----- 101 ----- 1011011', L='cv.mulhhuRN'),
        ]

        # MAC operations (funct3=110 signed, 111 unsigned)
        instrs += [
            Instr('p.macsN',         Format_RRRRU,'00----- ----- ----- 110 ----- 1011011', L='cv.macsN'),
            Instr('p.machhsN',       Format_RRRRU,'01----- ----- ----- 110 ----- 1011011', L='cv.machhsN'),
            Instr('p.macsNR',        Format_RRRRU,'10----- ----- ----- 110 ----- 1011011', L='cv.macsRN'),
            Instr('p.machhsNR',      Format_RRRRU,'11----- ----- ----- 110 ----- 1011011', L='cv.machhsRN'),
            Instr('p.macuN',         Format_RRRRU,'00----- ----- ----- 111 ----- 1011011', L='cv.macuN'),
            Instr('p.machhuN',       Format_RRRRU,'01----- ----- ----- 111 ----- 1011011', L='cv.machhuN'),
            Instr('p.macuNR',        Format_RRRRU,'10----- ----- ----- 111 ----- 1011011', L='cv.macuRN'),
            Instr('p.machhuNR',      Format_RRRRU,'11----- ----- ----- 111 ----- 1011011', L='cv.machhuRN'),
        ]

        # HW loops (custom-1 0x2B, funct3=100): instr[11:8] selects the instruction,
        # instr[7] selects the loop number (0 or 1)
        instrs += [
            Instr('lp.starti',Format_HL0,'------- ----- 00000 100 0000- 0101011', L='cv.starti'),
            Instr('lp.start', Format_HL0,'0000000 00000 ----- 100 0001- 0101011', L='cv.start'),
            Instr('lp.endi',  Format_HL0,'------- ----- 00000 100 0010- 0101011', L='cv.endi'),
            Instr('lp.end',   Format_HL0,'0000000 00000 ----- 100 0011- 0101011', L='cv.end'),
            Instr('lp.counti',Format_HL0,'------- ----- 00000 100 0100- 0101011', L='cv.counti'),
            Instr('lp.count', Format_HL0,'0000000 00000 ----- 100 0101- 0101011', L='cv.count'),
            Instr('lp.setupi',Format_HL1,'------- ----- ----- 100 0110- 0101011', L='cv.setupi'),
            Instr('lp.setup', Format_HL0,'------- ----- ----- 100 0111- 0101011', L='cv.setup'),
        ]

        # mac/msu (custom-1 0x2B, funct3=011): cv.mac funct7=1001000, cv.msu funct7=1001001
        instrs += [
            Instr('p.mac',           Format_RRRR, '1001000 ----- ----- 011 ----- 0101011', L='cv.mac'),
            Instr('p.msu',           Format_RRRR, '1001001 ----- ----- 011 ----- 0101011', L='cv.msu'),
        ]


        # SIMD (custom-3 0x7B), with the PULP v2 handlers
        instrs += [
            Instr('pv.abs.b',           Format_R1,   '0111000 00000 ----- 001 ----- 1111011', L='cv.abs.b'),
            Instr('pv.abs.h',           Format_R1,   '0111000 00000 ----- 000 ----- 1111011', L='cv.abs.h'),
            Instr('pv.add.sci.b',       Format_RRS,  '000000- ----- ----- 111 ----- 1111011', L='cv.add.sci.b'),
            Instr('pv.add.sci.h',       Format_RRS,  '000000- ----- ----- 110 ----- 1111011', L='cv.add.sci.h'),
            Instr('pv.and.b',           Format_R,    '0110100 ----- ----- 001 ----- 1111011', L='cv.and.b'),
            Instr('pv.and.h',           Format_R,    '0110100 ----- ----- 000 ----- 1111011', L='cv.and.h'),
            Instr('pv.and.sc.b',        Format_R,    '0110100 ----- ----- 101 ----- 1111011', L='cv.and.sc.b'),
            Instr('pv.and.sc.h',        Format_R,    '0110100 ----- ----- 100 ----- 1111011', L='cv.and.sc.h'),
            Instr('pv.and.sci.b',       Format_RRS,  '011010- ----- ----- 111 ----- 1111011', L='cv.and.sci.b'),
            Instr('pv.and.sci.h',       Format_RRS,  '011010- ----- ----- 110 ----- 1111011', L='cv.and.sci.h'),
            Instr('pv.avg.b',           Format_R,    '0001000 ----- ----- 001 ----- 1111011', L='cv.avg.b'),
            Instr('pv.avg.h',           Format_R,    '0001000 ----- ----- 000 ----- 1111011', L='cv.avg.h'),
            Instr('pv.avg.sc.b',        Format_R,    '0001000 ----- ----- 101 ----- 1111011', L='cv.avg.sc.b'),
            Instr('pv.avg.sc.h',        Format_R,    '0001000 ----- ----- 100 ----- 1111011', L='cv.avg.sc.h'),
            Instr('pv.avg.sci.b',       Format_RRS,  '000100- ----- ----- 111 ----- 1111011', L='cv.avg.sci.b'),
            Instr('pv.avg.sci.h',       Format_RRS,  '000100- ----- ----- 110 ----- 1111011', L='cv.avg.sci.h'),
            Instr('pv.avgu.b',          Format_R,    '0001100 ----- ----- 001 ----- 1111011', L='cv.avgu.b'),
            Instr('pv.avgu.h',          Format_R,    '0001100 ----- ----- 000 ----- 1111011', L='cv.avgu.h'),
            Instr('pv.avgu.sc.b',       Format_R,    '0001100 ----- ----- 101 ----- 1111011', L='cv.avgu.sc.b'),
            Instr('pv.avgu.sc.h',       Format_R,    '0001100 ----- ----- 100 ----- 1111011', L='cv.avgu.sc.h'),
            Instr('pv.avgu.sci.b',      Format_RRU,  '000110- ----- ----- 111 ----- 1111011', L='cv.avgu.sci.b'),
            Instr('pv.avgu.sci.h',      Format_RRU,  '000110- ----- ----- 110 ----- 1111011', L='cv.avgu.sci.h'),
            Instr('pv.cmpeq.b',         Format_R,    '0000010 ----- ----- 001 ----- 1111011', L='cv.cmpeq.b'),
            Instr('pv.cmpeq.h',         Format_R,    '0000010 ----- ----- 000 ----- 1111011', L='cv.cmpeq.h'),
            Instr('pv.cmpeq.sc.b',      Format_R,    '0000010 ----- ----- 101 ----- 1111011', L='cv.cmpeq.sc.b'),
            Instr('pv.cmpeq.sc.h',      Format_R,    '0000010 ----- ----- 100 ----- 1111011', L='cv.cmpeq.sc.h'),
            Instr('pv.cmpeq.sci.b',     Format_RRS,  '000001- ----- ----- 111 ----- 1111011', L='cv.cmpeq.sci.b'),
            Instr('pv.cmpeq.sci.h',     Format_RRS,  '000001- ----- ----- 110 ----- 1111011', L='cv.cmpeq.sci.h'),
            Instr('pv.cmpge.b',         Format_R,    '0001110 ----- ----- 001 ----- 1111011', L='cv.cmpge.b'),
            Instr('pv.cmpge.h',         Format_R,    '0001110 ----- ----- 000 ----- 1111011', L='cv.cmpge.h'),
            Instr('pv.cmpge.sc.b',      Format_R,    '0001110 ----- ----- 101 ----- 1111011', L='cv.cmpge.sc.b'),
            Instr('pv.cmpge.sc.h',      Format_R,    '0001110 ----- ----- 100 ----- 1111011', L='cv.cmpge.sc.h'),
            Instr('pv.cmpge.sci.b',     Format_RRS,  '000111- ----- ----- 111 ----- 1111011', L='cv.cmpge.sci.b'),
            Instr('pv.cmpge.sci.h',     Format_RRS,  '000111- ----- ----- 110 ----- 1111011', L='cv.cmpge.sci.h'),
            Instr('pv.cmpgeu.b',        Format_R,    '0011110 ----- ----- 001 ----- 1111011', L='cv.cmpgeu.b'),
            Instr('pv.cmpgeu.h',        Format_R,    '0011110 ----- ----- 000 ----- 1111011', L='cv.cmpgeu.h'),
            Instr('pv.cmpgeu.sc.b',     Format_R,    '0011110 ----- ----- 101 ----- 1111011', L='cv.cmpgeu.sc.b'),
            Instr('pv.cmpgeu.sc.h',     Format_R,    '0011110 ----- ----- 100 ----- 1111011', L='cv.cmpgeu.sc.h'),
            Instr('pv.cmpgeu.sci.b',    Format_RRU,  '001111- ----- ----- 111 ----- 1111011', L='cv.cmpgeu.sci.b'),
            Instr('pv.cmpgeu.sci.h',    Format_RRU,  '001111- ----- ----- 110 ----- 1111011', L='cv.cmpgeu.sci.h'),
            Instr('pv.cmpgt.b',         Format_R,    '0001010 ----- ----- 001 ----- 1111011', L='cv.cmpgt.b'),
            Instr('pv.cmpgt.h',         Format_R,    '0001010 ----- ----- 000 ----- 1111011', L='cv.cmpgt.h'),
            Instr('pv.cmpgt.sc.b',      Format_R,    '0001010 ----- ----- 101 ----- 1111011', L='cv.cmpgt.sc.b'),
            Instr('pv.cmpgt.sc.h',      Format_R,    '0001010 ----- ----- 100 ----- 1111011', L='cv.cmpgt.sc.h'),
            Instr('pv.cmpgt.sci.b',     Format_RRS,  '000101- ----- ----- 111 ----- 1111011', L='cv.cmpgt.sci.b'),
            Instr('pv.cmpgt.sci.h',     Format_RRS,  '000101- ----- ----- 110 ----- 1111011', L='cv.cmpgt.sci.h'),
            Instr('pv.cmpgtu.b',        Format_R,    '0011010 ----- ----- 001 ----- 1111011', L='cv.cmpgtu.b'),
            Instr('pv.cmpgtu.h',        Format_R,    '0011010 ----- ----- 000 ----- 1111011', L='cv.cmpgtu.h'),
            Instr('pv.cmpgtu.sc.b',     Format_R,    '0011010 ----- ----- 101 ----- 1111011', L='cv.cmpgtu.sc.b'),
            Instr('pv.cmpgtu.sc.h',     Format_R,    '0011010 ----- ----- 100 ----- 1111011', L='cv.cmpgtu.sc.h'),
            Instr('pv.cmpgtu.sci.b',    Format_RRU,  '001101- ----- ----- 111 ----- 1111011', L='cv.cmpgtu.sci.b'),
            Instr('pv.cmpgtu.sci.h',    Format_RRU,  '001101- ----- ----- 110 ----- 1111011', L='cv.cmpgtu.sci.h'),
            Instr('pv.cmple.b',         Format_R,    '0010110 ----- ----- 001 ----- 1111011', L='cv.cmple.b'),
            Instr('pv.cmple.h',         Format_R,    '0010110 ----- ----- 000 ----- 1111011', L='cv.cmple.h'),
            Instr('pv.cmple.sc.b',      Format_R,    '0010110 ----- ----- 101 ----- 1111011', L='cv.cmple.sc.b'),
            Instr('pv.cmple.sc.h',      Format_R,    '0010110 ----- ----- 100 ----- 1111011', L='cv.cmple.sc.h'),
            Instr('pv.cmple.sci.b',     Format_RRS,  '001011- ----- ----- 111 ----- 1111011', L='cv.cmple.sci.b'),
            Instr('pv.cmple.sci.h',     Format_RRS,  '001011- ----- ----- 110 ----- 1111011', L='cv.cmple.sci.h'),
            Instr('pv.cmpleu.b',        Format_R,    '0100110 ----- ----- 001 ----- 1111011', L='cv.cmpleu.b'),
            Instr('pv.cmpleu.h',        Format_R,    '0100110 ----- ----- 000 ----- 1111011', L='cv.cmpleu.h'),
            Instr('pv.cmpleu.sc.b',     Format_R,    '0100110 ----- ----- 101 ----- 1111011', L='cv.cmpleu.sc.b'),
            Instr('pv.cmpleu.sc.h',     Format_R,    '0100110 ----- ----- 100 ----- 1111011', L='cv.cmpleu.sc.h'),
            Instr('pv.cmpleu.sci.b',    Format_RRU,  '010011- ----- ----- 111 ----- 1111011', L='cv.cmpleu.sci.b'),
            Instr('pv.cmpleu.sci.h',    Format_RRU,  '010011- ----- ----- 110 ----- 1111011', L='cv.cmpleu.sci.h'),
            Instr('pv.cmplt.b',         Format_R,    '0010010 ----- ----- 001 ----- 1111011', L='cv.cmplt.b'),
            Instr('pv.cmplt.h',         Format_R,    '0010010 ----- ----- 000 ----- 1111011', L='cv.cmplt.h'),
            Instr('pv.cmplt.sc.b',      Format_R,    '0010010 ----- ----- 101 ----- 1111011', L='cv.cmplt.sc.b'),
            Instr('pv.cmplt.sc.h',      Format_R,    '0010010 ----- ----- 100 ----- 1111011', L='cv.cmplt.sc.h'),
            Instr('pv.cmplt.sci.b',     Format_RRS,  '001001- ----- ----- 111 ----- 1111011', L='cv.cmplt.sci.b'),
            Instr('pv.cmplt.sci.h',     Format_RRS,  '001001- ----- ----- 110 ----- 1111011', L='cv.cmplt.sci.h'),
            Instr('pv.cmpltu.b',        Format_R,    '0100010 ----- ----- 001 ----- 1111011', L='cv.cmpltu.b'),
            Instr('pv.cmpltu.h',        Format_R,    '0100010 ----- ----- 000 ----- 1111011', L='cv.cmpltu.h'),
            Instr('pv.cmpltu.sc.b',     Format_R,    '0100010 ----- ----- 101 ----- 1111011', L='cv.cmpltu.sc.b'),
            Instr('pv.cmpltu.sc.h',     Format_R,    '0100010 ----- ----- 100 ----- 1111011', L='cv.cmpltu.sc.h'),
            Instr('pv.cmpltu.sci.b',    Format_RRU,  '010001- ----- ----- 111 ----- 1111011', L='cv.cmpltu.sci.b'),
            Instr('pv.cmpltu.sci.h',    Format_RRU,  '010001- ----- ----- 110 ----- 1111011', L='cv.cmpltu.sci.h'),
            Instr('pv.cmpne.b',         Format_R,    '0000110 ----- ----- 001 ----- 1111011', L='cv.cmpne.b'),
            Instr('pv.cmpne.h',         Format_R,    '0000110 ----- ----- 000 ----- 1111011', L='cv.cmpne.h'),
            Instr('pv.cmpne.sc.b',      Format_R,    '0000110 ----- ----- 101 ----- 1111011', L='cv.cmpne.sc.b'),
            Instr('pv.cmpne.sc.h',      Format_R,    '0000110 ----- ----- 100 ----- 1111011', L='cv.cmpne.sc.h'),
            Instr('pv.cmpne.sci.b',     Format_RRS,  '000011- ----- ----- 111 ----- 1111011', L='cv.cmpne.sci.b'),
            Instr('pv.cmpne.sci.h',     Format_RRS,  '000011- ----- ----- 110 ----- 1111011', L='cv.cmpne.sci.h'),
            Instr('pv.dotup.b',         Format_R,    '1000000 ----- ----- 001 ----- 1111011', L='cv.dotup.b'),
            Instr('pv.dotup.h',         Format_R,    '1000000 ----- ----- 000 ----- 1111011', L='cv.dotup.h'),
            Instr('pv.dotup.b.sc',      Format_R,    '1000000 ----- ----- 101 ----- 1111011', L='cv.dotup.sc.b'),
            Instr('pv.dotup.h.sc',      Format_R,    '1000000 ----- ----- 100 ----- 1111011', L='cv.dotup.sc.h'),
            Instr('pv.dotup.b.sci',     Format_RRU,  '100000- ----- ----- 111 ----- 1111011', L='cv.dotup.sci.b'),
            Instr('pv.dotup.h.sci',     Format_RRU,  '100000- ----- ----- 110 ----- 1111011', L='cv.dotup.sci.h'),
            Instr('pv.dotusp.b',        Format_R,    '1000100 ----- ----- 001 ----- 1111011', L='cv.dotusp.b'),
            Instr('pv.dotusp.h',        Format_R,    '1000100 ----- ----- 000 ----- 1111011', L='cv.dotusp.h'),
            Instr('pv.dotusp.b.sc',     Format_R,    '1000100 ----- ----- 101 ----- 1111011', L='cv.dotusp.sc.b'),
            Instr('pv.dotusp.h.sc',     Format_R,    '1000100 ----- ----- 100 ----- 1111011', L='cv.dotusp.sc.h'),
            Instr('pv.dotusp.b.sci',    Format_RRS,  '100010- ----- ----- 111 ----- 1111011', L='cv.dotusp.sci.b'),
            Instr('pv.dotusp.h.sci',    Format_RRS,  '100010- ----- ----- 110 ----- 1111011', L='cv.dotusp.sci.h'),
            Instr('pv.max.b',           Format_R,    '0011000 ----- ----- 001 ----- 1111011', L='cv.max.b'),
            Instr('pv.max.h',           Format_R,    '0011000 ----- ----- 000 ----- 1111011', L='cv.max.h'),
            Instr('pv.max.sc.b',        Format_R,    '0011000 ----- ----- 101 ----- 1111011', L='cv.max.sc.b'),
            Instr('pv.max.sc.h',        Format_R,    '0011000 ----- ----- 100 ----- 1111011', L='cv.max.sc.h'),
            Instr('pv.max.sci.b',       Format_RRS,  '001100- ----- ----- 111 ----- 1111011', L='cv.max.sci.b'),
            Instr('pv.max.sci.h',       Format_RRS,  '001100- ----- ----- 110 ----- 1111011', L='cv.max.sci.h'),
            Instr('pv.maxu.b',          Format_R,    '0011100 ----- ----- 001 ----- 1111011', L='cv.maxu.b'),
            Instr('pv.maxu.h',          Format_R,    '0011100 ----- ----- 000 ----- 1111011', L='cv.maxu.h'),
            Instr('pv.maxu.sc.b',       Format_R,    '0011100 ----- ----- 101 ----- 1111011', L='cv.maxu.sc.b'),
            Instr('pv.maxu.sc.h',       Format_R,    '0011100 ----- ----- 100 ----- 1111011', L='cv.maxu.sc.h'),
            Instr('pv.maxu.sci.b',      Format_RRU,  '001110- ----- ----- 111 ----- 1111011', L='cv.maxu.sci.b'),
            Instr('pv.maxu.sci.h',      Format_RRU,  '001110- ----- ----- 110 ----- 1111011', L='cv.maxu.sci.h'),
            Instr('pv.min.b',           Format_R,    '0010000 ----- ----- 001 ----- 1111011', L='cv.min.b'),
            Instr('pv.min.h',           Format_R,    '0010000 ----- ----- 000 ----- 1111011', L='cv.min.h'),
            Instr('pv.min.sc.b',        Format_R,    '0010000 ----- ----- 101 ----- 1111011', L='cv.min.sc.b'),
            Instr('pv.min.sc.h',        Format_R,    '0010000 ----- ----- 100 ----- 1111011', L='cv.min.sc.h'),
            Instr('pv.min.sci.b',       Format_RRS,  '001000- ----- ----- 111 ----- 1111011', L='cv.min.sci.b'),
            Instr('pv.min.sci.h',       Format_RRS,  '001000- ----- ----- 110 ----- 1111011', L='cv.min.sci.h'),
            Instr('pv.minu.b',          Format_R,    '0010100 ----- ----- 001 ----- 1111011', L='cv.minu.b'),
            Instr('pv.minu.h',          Format_R,    '0010100 ----- ----- 000 ----- 1111011', L='cv.minu.h'),
            Instr('pv.minu.sc.b',       Format_R,    '0010100 ----- ----- 101 ----- 1111011', L='cv.minu.sc.b'),
            Instr('pv.minu.sc.h',       Format_R,    '0010100 ----- ----- 100 ----- 1111011', L='cv.minu.sc.h'),
            Instr('pv.minu.sci.b',      Format_RRU,  '001010- ----- ----- 111 ----- 1111011', L='cv.minu.sci.b'),
            Instr('pv.minu.sci.h',      Format_RRU,  '001010- ----- ----- 110 ----- 1111011', L='cv.minu.sci.h'),
            Instr('pv.or.b',            Format_R,    '0101100 ----- ----- 001 ----- 1111011', L='cv.or.b'),
            Instr('pv.or.h',            Format_R,    '0101100 ----- ----- 000 ----- 1111011', L='cv.or.h'),
            Instr('pv.or.sc.b',         Format_R,    '0101100 ----- ----- 101 ----- 1111011', L='cv.or.sc.b'),
            Instr('pv.or.sc.h',         Format_R,    '0101100 ----- ----- 100 ----- 1111011', L='cv.or.sc.h'),
            Instr('pv.or.sci.b',        Format_RRS,  '010110- ----- ----- 111 ----- 1111011', L='cv.or.sci.b'),
            Instr('pv.or.sci.h',        Format_RRS,  '010110- ----- ----- 110 ----- 1111011', L='cv.or.sci.h'),
            Instr('pv.shuffle.b',       Format_R,    '1100000 ----- ----- 001 ----- 1111011', L='cv.shuffle.b'),
            Instr('pv.shuffle.h',       Format_R,    '1100000 ----- ----- 000 ----- 1111011', L='cv.shuffle.h'),
            Instr('pv.shuffle.h.sci',   Format_RRU,  '110000- 0000- ----- 110 ----- 1111011', L='cv.shuffle.sci.h'),
            Instr('pv.shufflei0.b.sci', Format_RRU2, '110000- ----- ----- 111 ----- 1111011', L='cv.shufflei0.sci.b'),
            Instr('pv.sll.b',           Format_R,    '0101000 ----- ----- 001 ----- 1111011', L='cv.sll.b'),
            Instr('pv.sll.h',           Format_R,    '0101000 ----- ----- 000 ----- 1111011', L='cv.sll.h'),
            Instr('pv.sll.sc.b',        Format_R,    '0101000 ----- ----- 101 ----- 1111011', L='cv.sll.sc.b'),
            Instr('pv.sll.sc.h',        Format_R,    '0101000 ----- ----- 100 ----- 1111011', L='cv.sll.sc.h'),
            Instr('pv.sll.sci.b',       Format_RRU,  '010100- 000-- ----- 111 ----- 1111011', L='cv.sll.sci.b'),
            Instr('pv.sll.sci.h',       Format_RRU,  '010100- 00--- ----- 110 ----- 1111011', L='cv.sll.sci.h'),
            Instr('pv.sra.b',           Format_R,    '0100100 ----- ----- 001 ----- 1111011', L='cv.sra.b'),
            Instr('pv.sra.h',           Format_R,    '0100100 ----- ----- 000 ----- 1111011', L='cv.sra.h'),
            Instr('pv.sra.sc.b',        Format_R,    '0100100 ----- ----- 101 ----- 1111011', L='cv.sra.sc.b'),
            Instr('pv.sra.sc.h',        Format_R,    '0100100 ----- ----- 100 ----- 1111011', L='cv.sra.sc.h'),
            Instr('pv.sra.sci.b',       Format_RRS,  '010010- 000-- ----- 111 ----- 1111011', L='cv.sra.sci.b'),
            Instr('pv.sra.sci.h',       Format_RRS,  '010010- 00--- ----- 110 ----- 1111011', L='cv.sra.sci.h'),
            Instr('pv.srl.b',           Format_R,    '0100000 ----- ----- 001 ----- 1111011', L='cv.srl.b'),
            Instr('pv.srl.h',           Format_R,    '0100000 ----- ----- 000 ----- 1111011', L='cv.srl.h'),
            Instr('pv.srl.sc.b',        Format_R,    '0100000 ----- ----- 101 ----- 1111011', L='cv.srl.sc.b'),
            Instr('pv.srl.sc.h',        Format_R,    '0100000 ----- ----- 100 ----- 1111011', L='cv.srl.sc.h'),
            Instr('pv.srl.sci.b',       Format_RRU,  '010000- 000-- ----- 111 ----- 1111011', L='cv.srl.sci.b'),
            Instr('pv.srl.sci.h',       Format_RRU,  '010000- 00--- ----- 110 ----- 1111011', L='cv.srl.sci.h'),
            Instr('pv.sub.b',           Format_R,    '0000100 ----- ----- 001 ----- 1111011', L='cv.sub.b'),
            Instr('pv.sub.h',           Format_R,    '0000100 ----- ----- 000 ----- 1111011', L='cv.sub.h'),
            Instr('pv.sub.sc.b',        Format_R,    '0000100 ----- ----- 101 ----- 1111011', L='cv.sub.sc.b'),
            Instr('pv.sub.sc.h',        Format_R,    '0000100 ----- ----- 100 ----- 1111011', L='cv.sub.sc.h'),
            Instr('pv.sub.sci.b',       Format_RRS,  '000010- ----- ----- 111 ----- 1111011', L='cv.sub.sci.b'),
            Instr('pv.sub.sci.h',       Format_RRS,  '000010- ----- ----- 110 ----- 1111011', L='cv.sub.sci.h'),
            Instr('pv.xor.b',           Format_R,    '0110000 ----- ----- 001 ----- 1111011', L='cv.xor.b'),
            Instr('pv.xor.h',           Format_R,    '0110000 ----- ----- 000 ----- 1111011', L='cv.xor.h'),
            Instr('pv.xor.sc.b',        Format_R,    '0110000 ----- ----- 101 ----- 1111011', L='cv.xor.sc.b'),
            Instr('pv.xor.sc.h',        Format_R,    '0110000 ----- ----- 100 ----- 1111011', L='cv.xor.sc.h'),
            Instr('pv.xor.sci.b',       Format_RRS,  '011000- ----- ----- 111 ----- 1111011', L='cv.xor.sci.b'),
            Instr('pv.xor.sci.h',       Format_RRS,  '011000- ----- ----- 110 ----- 1111011', L='cv.xor.sci.h'),
            Instr('pv.add.h',           Format_R,    '0000000 ----- ----- 000 ----- 1111011', L='cv.add.h'),
            Instr('pv.add.b',           Format_R,    '0000000 ----- ----- 001 ----- 1111011', L='cv.add.b'),
            Instr('pv.add.sc.h',        Format_R,    '0000000 ----- ----- 100 ----- 1111011', L='cv.add.sc.h'),
            Instr('pv.add.sc.b',        Format_R,    '0000000 ----- ----- 101 ----- 1111011', L='cv.add.sc.b'),
            # extract / insert / shuffle2 / shufflei / pack + dotsp / sdot
            Instr('pv.extract.h',       Format_RRU,  '101110- 00000 ----- 000 ----- 1111011', L='cv.extract.h'),
            Instr('pv.extract.b',       Format_RRU,  '101110- 0000- ----- 001 ----- 1111011', L='cv.extract.b'),
            Instr('pv.extractu.h',      Format_RRU,  '101110- 00000 ----- 010 ----- 1111011', L='cv.extractu.h'),
            Instr('pv.extractu.b',      Format_RRU,  '101110- 0000- ----- 011 ----- 1111011', L='cv.extractu.b'),
            Instr('pv.insert.h',        Format_RRRU, '101110- 00000 ----- 100 ----- 1111011', L='cv.insert.h'),
            Instr('pv.insert.b',        Format_RRRU, '101110- 0000- ----- 101 ----- 1111011', L='cv.insert.b'),
            Instr('cv.pack.h',          Format_R,    '1111001 ----- ----- 000 ----- 1111011', L='cv.pack.h'),
            Instr('cv.packhi.b',        Format_RRRR, '1111101 ----- ----- 001 ----- 1111011', L='cv.packhi.b'),
            Instr('cv.packlo.b',        Format_RRRR, '1111100 ----- ----- 001 ----- 1111011', L='cv.packlo.b'),
            Instr('pv.shuffle2.h',      Format_RRRR, '1110000 ----- ----- 000 ----- 1111011', L='cv.shuffle2.h'),
            Instr('pv.shuffle2.b',      Format_RRRR, '1110000 ----- ----- 001 ----- 1111011', L='cv.shuffle2.b'),
            Instr('pv.shufflei1.b.sci', Format_RRU2, '110010- ----- ----- 111 ----- 1111011', L='cv.shufflei1.sci.b'),
            Instr('pv.shufflei2.b.sci', Format_RRU2, '110100- ----- ----- 111 ----- 1111011', L='cv.shufflei2.sci.b'),
            Instr('pv.shufflei3.b.sci', Format_RRU2, '110110- ----- ----- 111 ----- 1111011', L='cv.shufflei3.sci.b'),
            Instr('pv.dotsp.b',         Format_R,    '1001000 ----- ----- 001 ----- 1111011', L='cv.dotsp.b'),
            Instr('pv.dotsp.h',         Format_R,    '1001000 ----- ----- 000 ----- 1111011', L='cv.dotsp.h'),
            Instr('pv.dotsp.b.sc',      Format_R,    '1001000 ----- ----- 101 ----- 1111011', L='cv.dotsp.sc.b'),
            Instr('pv.dotsp.h.sc',      Format_R,    '1001000 ----- ----- 100 ----- 1111011', L='cv.dotsp.sc.h'),
            Instr('pv.dotsp.b.sci',     Format_RRS,  '100100- ----- ----- 111 ----- 1111011', L='cv.dotsp.sci.b'),
            Instr('pv.dotsp.h.sci',     Format_RRS,  '100100- ----- ----- 110 ----- 1111011', L='cv.dotsp.sci.h'),
            Instr('pv.sdotsp.b',        Format_RRRR, '1010100 ----- ----- 001 ----- 1111011', L='cv.sdotsp.b'),
            Instr('pv.sdotsp.h',        Format_RRRR, '1010100 ----- ----- 000 ----- 1111011', L='cv.sdotsp.h'),
            Instr('pv.sdotsp.b.sc',     Format_RRRR, '1010100 ----- ----- 101 ----- 1111011', L='cv.sdotsp.sc.b'),
            Instr('pv.sdotsp.h.sc',     Format_RRRR, '1010100 ----- ----- 100 ----- 1111011', L='cv.sdotsp.sc.h'),
            Instr('pv.sdotsp.b.sci',    Format_RRRS, '101010- ----- ----- 111 ----- 1111011', L='cv.sdotsp.sci.b'),
            Instr('pv.sdotsp.h.sci',    Format_RRRS, '101010- ----- ----- 110 ----- 1111011', L='cv.sdotsp.sci.h'),
            Instr('pv.sdotup.b',        Format_RRRR, '1001100 ----- ----- 001 ----- 1111011', L='cv.sdotup.b'),
            Instr('pv.sdotup.h',        Format_RRRR, '1001100 ----- ----- 000 ----- 1111011', L='cv.sdotup.h'),
            Instr('pv.sdotup.b.sc',     Format_RRRR, '1001100 ----- ----- 101 ----- 1111011', L='cv.sdotup.sc.b'),
            Instr('pv.sdotup.h.sc',     Format_RRRR, '1001100 ----- ----- 100 ----- 1111011', L='cv.sdotup.sc.h'),
            Instr('pv.sdotup.b.sci',    Format_RRRU, '100110- ----- ----- 111 ----- 1111011', L='cv.sdotup.sci.b'),
            Instr('pv.sdotup.h.sci',    Format_RRRU, '100110- ----- ----- 110 ----- 1111011', L='cv.sdotup.sci.h'),
            Instr('pv.sdotusp.b',       Format_RRRR, '1010000 ----- ----- 001 ----- 1111011', L='cv.sdotusp.b'),
            Instr('pv.sdotusp.h',       Format_RRRR, '1010000 ----- ----- 000 ----- 1111011', L='cv.sdotusp.h'),
            Instr('pv.sdotusp.b.sc',    Format_RRRR, '1010000 ----- ----- 101 ----- 1111011', L='cv.sdotusp.sc.b'),
            Instr('pv.sdotusp.h.sc',    Format_RRRR, '1010000 ----- ----- 100 ----- 1111011', L='cv.sdotusp.sc.h'),
            Instr('pv.sdotusp.b.sci',   Format_RRRS, '101000- ----- ----- 111 ----- 1111011', L='cv.sdotusp.sci.b'),
            Instr('pv.sdotusp.h.sci',   Format_RRRS, '101000- ----- ----- 110 ----- 1111011', L='cv.sdotusp.sci.h'),
            # complex multiply / div-rounding / pack
            Instr('cv.add.div2',        Format_R,    '0110110 ----- ----- 010 ----- 1111011', L='cv.add.div2'),
            Instr('cv.add.div4',        Format_R,    '0110110 ----- ----- 100 ----- 1111011', L='cv.add.div4'),
            Instr('cv.add.div8',        Format_R,    '0110110 ----- ----- 110 ----- 1111011', L='cv.add.div8'),
            Instr('cv.sub.div2',        Format_R,    '0111010 ----- ----- 010 ----- 1111011', L='cv.sub.div2'),
            Instr('cv.sub.div4',        Format_R,    '0111010 ----- ----- 100 ----- 1111011', L='cv.sub.div4'),
            Instr('cv.sub.div8',        Format_R,    '0111010 ----- ----- 110 ----- 1111011', L='cv.sub.div8'),
            Instr('cv.subrotmj',        Format_R,    '0110010 ----- ----- 000 ----- 1111011', L='cv.subrotmj'),
            Instr('cv.subrotmj.div2',   Format_R,    '0110010 ----- ----- 010 ----- 1111011', L='cv.subrotmj.div2'),
            Instr('cv.subrotmj.div4',   Format_R,    '0110010 ----- ----- 100 ----- 1111011', L='cv.subrotmj.div4'),
            Instr('cv.subrotmj.div8',   Format_R,    '0110010 ----- ----- 110 ----- 1111011', L='cv.subrotmj.div8'),
            Instr('cv.cplxconj',        Format_R1,   '0101110 00000 ----- 000 ----- 1111011', L='cv.cplxconj'),
            Instr('cv.cplxmul.r',       Format_RRRR, '0101010 ----- ----- 000 ----- 1111011', L='cv.cplxmul.r'),
            Instr('cv.cplxmul.r.div2',  Format_RRRR, '0101010 ----- ----- 010 ----- 1111011', L='cv.cplxmul.r.div2'),
            Instr('cv.cplxmul.r.div4',  Format_RRRR, '0101010 ----- ----- 100 ----- 1111011', L='cv.cplxmul.r.div4'),
            Instr('cv.cplxmul.r.div8',  Format_RRRR, '0101010 ----- ----- 110 ----- 1111011', L='cv.cplxmul.r.div8'),
            Instr('cv.cplxmul.i',       Format_RRRR, '0101011 ----- ----- 000 ----- 1111011', L='cv.cplxmul.i'),
            Instr('cv.cplxmul.i.div2',  Format_RRRR, '0101011 ----- ----- 010 ----- 1111011', L='cv.cplxmul.i.div2'),
            Instr('cv.cplxmul.i.div4',  Format_RRRR, '0101011 ----- ----- 100 ----- 1111011', L='cv.cplxmul.i.div4'),
            Instr('cv.cplxmul.i.div8',  Format_RRRR, '0101011 ----- ----- 110 ----- 1111011', L='cv.cplxmul.i.div8'),
            Instr('cv.pack',            Format_R,    '1111000 ----- ----- 000 ----- 1111011', L='cv.pack'),
        ]
        # iss_v2 includes only the headers that the subsets and modules declare.
        super().__init__(name='pulpv2', instrs=instrs, includes=[
            '<cpu/iss_v2/include/cores/cv32e40p/isa/corev.hpp>',
        ])
