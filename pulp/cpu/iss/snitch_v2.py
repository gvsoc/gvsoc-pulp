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

"""Snitch core on iss_v2 with its decoupled FP subsystem.

One iss_v2 instance models the integer core and the FP subsystem of the RTL
``snitch_cc``: the integer core offloads every FP instruction (and FREP) to
the FP subsystem and keeps going, the FP subsystem sequences them (16-entry
buffer, FREP loops with staggering) and issues them one per cycle to the FPU
with its own register scoreboard. FP loads and stores go out on the
``fp_data`` port, the 3 stream semantic registers on ``ssr_0..2``. All the
memory ports are io_v2. See cpu/iss_v2/include/cores/snitchv2/snitchv2.hpp.
"""

from typing_extensions import override

import gvsoc.systree
import cpu.iss.isa_gen.isa_riscv_gen
from config_tree import cfg_field
from cpu.iss.isa_gen.isa_gen import Isa
from cpu.iss.isa_gen.isa_smallfloats import Xf16, Xf16alt, Xf8, Xf8Snitch, XfvecSnitch, Xfaux
from cpu.iss_v2.riscv import (Arch, ExecInOrder, IssModule, LsuV2, Offload, PrefetchSingleLine,
                              Regfile, RiscvCommon)
from cpu.iss_v2.riscv_config import RiscvConfig
from gvsoc.signature import IoV2SingleReq
from gvsoc.systree import Component
from pulp.snitch.snitch_isa import Xdma, Rv32frep, Rv32ssr


class SnitchV2Config(RiscvConfig):
    isa: str = cfg_field(default='rv32imfdca', dump=True, desc=(
        "ISA string of the core"
    ))
    lsu_nb_outstanding: int = cfg_field(default=4, dump=True, desc=(
        "Outstanding integer loads and stores (RTL NumIntOutstandingMem)."
    ))
    lsu_nb_outstanding_loads: int = cfg_field(default=1, dump=True, desc=(
        "Outstanding integer loads among them (RTL NumIntOutstandingLoads)."
    ))
    lsu_width: int = cfg_field(default=8, dump=True, desc=(
        "Width in bytes of the core data port."
    ))
    seq_buffer_size: int = cfg_field(default=16, dump=True, desc=(
        "Entries of the FP sequencer buffer (RTL snitch_sequencer Depth)."
    ))
    frep_config_size: int = cfg_field(default=8, dump=True, desc=(
        "Pending FREP loop configurations (RTL snitch_sequencer RptDepth)."
    ))
    offload_latency: int = cfg_field(default=2, dump=True, desc=(
        "Cycles from the offload by the integer core to the earliest FPU issue "
        "(offload request cut plus sequencer buffer read)."
    ))
    result_latency: int = cfg_field(default=1, dump=True, desc=(
        "Cycles from an FP result being written back to the FP registers to it "
        "being readable by the next instruction (no forwarding)."
    ))
    resp_latency: int = cfg_field(default=1, dump=True, desc=(
        "Cycles for a result of the FP subsystem to reach the integer register "
        "file (offload response cut)."
    ))
    fma_latency: int = cfg_field(default=3, dump=True, desc=(
        "Pipeline depth of the FPU add / mul / fma / dot-product operations."
    ))
    noncomp_latency: int = cfg_field(default=1, dump=True, desc=(
        "Pipeline depth of the FPU non-computational operations (sign "
        "injection, min/max, compare, classify, moves)."
    ))
    conv_latency: int = cfg_field(default=2, dump=True, desc=(
        "Pipeline depth of the FPU conversions."
    ))
    divsqrt_latency: int = cfg_field(default=11, dump=True, desc=(
        "Latency of the FPU divisions and square roots (not pipelined)."
    ))
    fp_lsu_nb_outstanding: int = cfg_field(default=4, dump=True, desc=(
        "Outstanding FP loads and stores (RTL NumFPOutstandingLoads)."
    ))
    caq_depth: int = cfg_field(default=8, dump=True, desc=(
        "Entries of the consistency address queue: FP loads and stores "
        "offloaded and not done yet, on which the integer memory accesses to "
        "the same address wait (RTL snitch_cc CaqDepth)."
    ))
    nb_ssrs: int = cfg_field(default=3, dump=True, desc=(
        "Number of stream semantic registers, mapped to ft0 and up."
    ))
    ssr_fifo_depth: int = cfg_field(default=4, dump=True, desc=(
        "Data FIFO depth of each stream semantic register."
    ))


class ArchSnitchV2(Arch):
    """Selects the SnitchV2 C++ core class and passes its parameters as
    compile-time constants."""

    def __init__(self, config: SnitchV2Config):
        super().__init__(class_name='SnitchV2',
            source='cpu/iss_v2/src/cores/snitchv2/snitchv2.cpp')
        self.config = config

    @override
    def gen(self, iss: RiscvCommon):
        super().gen(iss)
        config = self.config
        iss.isa.add_implem_include('<cpu/iss_v2/include/cores/snitchv2/snitchv2_implem.hpp>')
        iss.add_sources([
            'cpu/iss_v2/src/cores/snitchv2/fp_ss.cpp',
            'cpu/iss_v2/src/cores/snitchv2/ssr.cpp',
        ])
        defines = {
            'SEQ_BUFFER_SIZE': config.seq_buffer_size,
            'FREP_CONFIG_SIZE': config.frep_config_size,
            'OFFLOAD_LATENCY': config.offload_latency,
            'RESULT_LATENCY': config.result_latency,
            'RESP_LATENCY': config.resp_latency,
            'FMA_LATENCY': config.fma_latency,
            'NONCOMP_LATENCY': config.noncomp_latency,
            'CONV_LATENCY': config.conv_latency,
            'DIVSQRT_LATENCY': config.divsqrt_latency,
            'FP_LSU_NB_OUTSTANDING': config.fp_lsu_nb_outstanding,
            'CAQ_DEPTH': config.caq_depth,
            'NB_SSRS': config.nb_ssrs,
            'SSR_FIFO_DEPTH': config.ssr_fifo_depth,
        }
        for name, value in defines.items():
            iss.isa.add_define(f'CONFIG_GVSOC_ISS_SNITCH_{name}', value)


class RegfileSnitchV2(Regfile):
    """Register file whose ft0..ft2 reads and writes go to the stream
    semantic registers while they are enabled."""

    @override
    def gen(self, iss):
        iss.isa.add_define('CONFIG_GVSOC_ISS_REGFILE', 'SnitchV2Regfile')
        iss.isa.add_define('CONFIG_GVSOC_ISS_REGFILE_SCOREBOARD', '1')
        iss.isa.add_include('<cpu/iss_v2/include/regfile.hpp>')
        iss.isa.add_include('<cpu/iss_v2/include/cores/snitchv2/regfile.hpp>')
        iss.add_sources(['cpu/iss_v2/src/regfile.cpp'])


_isa_instances: dict[str, Isa] = {}


def _isa_setup(isa: Isa):
    # Alternate FP16 / FP8 formats are selected at runtime by the fmode CSR:
    # their handlers go through a stub dispatching on it.
    for name in ['f16', 'f8']:
        for insn in isa.get_isa(name).get_insns():
            if insn.name not in ['flh', 'fsh', 'flb', 'fsb']:
                insn.exec_func = insn.exec_func + '_switch'
                insn.exec_func_fast = insn.exec_func_fast + '_switch'
    for insn in isa.get_isa('fvec').get_insns():
        if ('f16vec' in insn.isa_tags or 'f16vecd' in insn.isa_tags or 'f8vec' in insn.isa_tags
                or 'f8vecf' in insn.isa_tags or 'f8vecd' in insn.isa_tags) \
                and not 'noalt' in insn.isa_tags:
            insn.exec_func = insn.exec_func + '_switch'
            insn.exec_func_fast = insn.exec_func_fast + '_switch'

    # The FP loads and stores are executed by the FP subsystem, on its own
    # port and with the address captured at offload
    for name in ['flb', 'fsb', 'flh', 'fsh', 'flw', 'fsw', 'fld', 'fsd']:
        insn = isa.get_insn(name)
        if insn is not None:
            insn.set_exec_label(name + '_snitchv2')
    for insn in isa.get_isa('f8snitch').get_insns():
        insn.set_exec_label(insn.name + '_snitchv2')


class SnitchV2(RiscvCommon):
    """Snitch integer core + FP subsystem (FPU sequencer, FREP, SSRs)."""

    def __init__(self, parent: Component, name: str, config: SnitchV2Config):

        # The parameters are compiled into the ISS through the ISA header
        isa_key = config.isa + '_' + '_'.join(str(v) for v in [config.seq_buffer_size,
            config.frep_config_size, config.offload_latency, config.result_latency,
            config.resp_latency, config.fma_latency, config.noncomp_latency,
            config.conv_latency, config.divsqrt_latency, config.fp_lsu_nb_outstanding,
            config.caq_depth, config.nb_ssrs, config.ssr_fifo_depth,
            config.lsu_nb_outstanding, config.lsu_nb_outstanding_loads, config.lsu_width])
        isa_instance: Isa | None = _isa_instances.get(isa_key)

        if isa_instance is None:
            extensions = [Xdma(), Xf16(), Xf16alt(), Xf8(), Xf8Snitch(), XfvecSnitch(), Xfaux(),
                Rv32frep(), Rv32ssr()]
            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa(
                f'snitch_v2_{len(_isa_instances)}_' + config.isa, config.isa,
                extensions=extensions)
            _isa_setup(isa_instance)
            _isa_instances[isa_key] = isa_instance

        modules: dict[str, IssModule] = {
            'arch': ArchSnitchV2(config),
            'exec': ExecInOrder(scoreboard=True),
            'regfile': RegfileSnitchV2(scoreboard=True),
            'prefetch': PrefetchSingleLine(),
            'offload': Offload(),
            'lsu': LsuV2(nb_outstanding=config.lsu_nb_outstanding,
                         nb_outstanding_loads=config.lsu_nb_outstanding_loads,
                         width=config.lsu_width),
        }

        super().__init__(parent, name, config=config, isa=isa_instance, modules=modules)

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        """Notification to the cluster barrier unit, on each barrier CSR
        (0x7C2) read."""
        self.itf_bind('barrier_req', itf, signature='wire<bool>')

    def i_BARRIER_ACK(self) -> gvsoc.systree.SlaveItf:
        """Release from the cluster barrier unit, once all the cores arrived."""
        return gvsoc.systree.SlaveItf(self, 'barrier_ack', signature='wire<bool>')

    def o_FP_DATA(self, itf: gvsoc.systree.SlaveItf):
        """FP load / store port of the FP subsystem (io_v2 single-request
        initiator)."""
        self.itf_bind('fp_data', itf, signature=IoV2SingleReq())

    def o_SSR(self, ssr: int, itf: gvsoc.systree.SlaveItf):
        """Data port of stream semantic register ``ssr`` (io_v2 single-request
        initiator)."""
        self.itf_bind(f'ssr_{ssr}', itf, signature=IoV2SingleReq())
