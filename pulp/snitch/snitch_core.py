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

import cpu.iss.riscv
from pulp.snitch.snitch_isa import *
import cpu.iss.isa_gen.isa_rvv
import cpu.iss.isa_gen.isa_rvv_timed
from cpu.iss.isa_gen.isa_smallfloats import *
from cpu.iss.isa_gen.isa_pulpv2 import *
import gvsoc.systree
import os
import pulp.ara.ara


def add_latencies(isa, is_fast=False, use_spatz=False):


    if is_fast and not use_spatz:
        isa.get_insn('flb').set_exec_label('flb_snitch')
        isa.get_insn('fsb').set_exec_label('fsb_snitch')
        isa.get_insn('flh').set_exec_label('flh_snitch')
        isa.get_insn('fsh').set_exec_label('fsh_snitch')
        isa.get_insn('flw').set_exec_label('flw_snitch')
        isa.get_insn('fsw').set_exec_label('fsw_snitch')
        if isa.get_insn('fld') is not None:
            isa.get_insn('fld').set_exec_label('fld_snitch')
            isa.get_insn('fsd').set_exec_label('fsd_snitch')

    # To model the fact that alt fp16 and fp18 instructions are dynamically enabled through a
    # CSR, we insert a stub which call the proper handler depending on the CSR value
    xf16_isa = isa.get_isa('f16')
    for insn in xf16_isa.get_insns():
        if insn.name not in ['flh', 'fsh']:
            insn.exec_func = insn.exec_func + '_switch'
            insn.exec_func_fast = insn.exec_func_fast + '_switch'

    xf8_isa = isa.get_isa('f8')
    for insn in xf8_isa.get_insns():
        if insn.name not in ['flb', 'fsb']:
            insn.exec_func = insn.exec_func + '_switch'
            insn.exec_func_fast = insn.exec_func_fast + '_switch'

    xfvec_isa = isa.get_isa('fvec')
    for insn in xfvec_isa.get_insns():
        if ('f16vec' in insn.isa_tags or 'f16vecd' in insn.isa_tags or 'f8vec' in insn.isa_tags \
                 or 'f8vecf' in insn.isa_tags or 'f8vecd' in insn.isa_tags) and not 'noalt' in insn.isa_tags:
            insn.exec_func = insn.exec_func + '_switch'
            insn.exec_func_fast = insn.exec_func_fast + '_switch'

    # Set snitch instruction latency:
    # 1. the latency of instruction, the core stalls for n cycles at the current instruction. (insn->latency)
    # 2. the latency of output register, the output is ready after n cycles to check data dependency. (reg.latency)
    for insn in isa.get_insns():

        # Pipelined instruction (allow parallelsim)
        if "fmadd" in insn.tags:
            insn.set_latency(3)
            insn.get_out_reg(0).set_latency(3)
        elif "fadd" in insn.tags:
            insn.set_latency(3)
            insn.get_out_reg(0).set_latency(3)
        elif "fmul" in insn.tags:
            insn.set_latency(3)
            insn.get_out_reg(0).set_latency(3)
        elif "fnoncomp" in insn.tags:
            insn.set_latency(1)
            insn.get_out_reg(0).set_latency(1)
        elif "fconv" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)
        elif "fmv" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)
        elif "fother" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)
        elif "fdiv" in insn.tags:
            insn.set_latency(11)
            insn.get_out_reg(0).set_latency(11)
        elif "load" in insn.tags and not "fload" in insn.tags:
            insn.set_latency(2)
            insn.get_out_reg(0).set_latency(2)



isa_instances = {}


class Snitch(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            inc_spatz: bool=False,
            core_id: int=0,
            htif: bool=False,
            pulp_v2: bool=False,
            nb_outstanding: int=1):

        isa_instance = isa_instances.get(isa)

        if isa_instances.get(isa) is None:

            if pulp_v2:
                extensions = [ PulpV2(hwloop=False, elw=False), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux() ]
            else:
                extensions = [ Rv32ssr(), Rv32frep(), Xdma(), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux() ]

            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_" + isa, isa,
                no_hash=True, extensions=extensions)
            add_latencies(isa_instance)
            isa_instances[isa] = isa_instance

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True,
            prefetcher_size=32, custom_sources=True, htif=htif, binaries=binaries,
            nb_outstanding=nb_outstanding)

        if pulp_v2:
            self.add_c_flags([f'-DCONFIG_GVSOC_ISS_SNITCH_PULP_V2=1'])

        self.add_c_flags([
            "-DPIPELINE_STAGES=1",
            "-DCONFIG_ISS_CORE=snitch",
        ])

        self.add_sources([
            "cpu/iss/src/snitch/snitch.cpp",
            "cpu/iss/src/snitch/regfile.cpp",
            "cpu/iss/src/snitch/ssr.cpp",
            "cpu/iss/src/prefetch/prefetch_single_line.cpp",
            "cpu/iss/src/csr.cpp",
            "cpu/iss/src/exec/exec_inorder.cpp",
            "cpu/iss/src/snitch/decode.cpp",
            "cpu/iss/src/lsu.cpp",
            "cpu/iss/src/timing.cpp",
            "cpu/iss/src/insn_cache.cpp",
            "cpu/iss/src/snitch/iss.cpp",
            "cpu/iss/src/core.cpp",
            "cpu/iss/src/exception.cpp",
            "cpu/iss/src/regfile.cpp",
            "cpu/iss/src/resource.cpp",
            "cpu/iss/src/trace.cpp",
            "cpu/iss/src/syscalls.cpp",
            "cpu/iss/src/memcheck.cpp",
            "cpu/iss/src/htif.cpp",
            "cpu/iss/src/gdbserver.cpp",
            "cpu/iss/src/dbg_unit.cpp",
            "cpu/iss/flexfloat/flexfloat.c",
        ])

        if inc_spatz:
            self.add_sources([
                "cpu/iss/src/spatz.cpp",
            ])

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_req', itf, signature='wire<bool>')


class SnitchFast(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            inc_spatz: bool=False,
            core_id: int=0,
            htif: bool=False, vlen: int=512, spatz_nb_lanes=4,
            pulp_v2: bool=False,
            nb_outstanding: int=1,
            wakeup_counter: bool=False,
            vector_chaining: bool=False
        ):

        isa_instance = isa_instances.get(isa)

        if isa_instances.get(isa) is None:

            if pulp_v2:
                extensions = [ PulpV2(hwloop=False, elw=False), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux() ]
            else:
                extensions = [ Xdma(), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux() ]

                if not inc_spatz:
                    extensions += [Rv32ssr(), Rv32frep()]

            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_" + isa, isa,
                extensions=extensions)
            add_latencies(isa_instance, is_fast=True, use_spatz=inc_spatz)
            isa_instances[isa] = isa_instance

            if inc_spatz:
                pulp.ara.ara.extend_isa(isa_instance)

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True,
            prefetcher_size=32, htif=htif, binaries=binaries, handle_misaligned=True, custom_sources=True,
            nb_outstanding=nb_outstanding, vector_chaining=vector_chaining)

        self.inc_spatz = inc_spatz

        if pulp_v2:
            self.add_c_flags([f'-DCONFIG_GVSOC_ISS_SNITCH_PULP_V2=1'])

        if wakeup_counter:
            self.add_c_flags([f'-DCONFIG_GVSOC_ISS_EXEC_WAKEUP_COUNTER=1'])

        self.add_c_flags([
            "-DPIPELINE_STAGES=1",
            "-DCONFIG_ISS_CORE=snitch_fast",
            "-DCONFIG_GVSOC_ISS_SNITCH_FAST",
        ])

        if inc_spatz:
            pulp.ara.ara.attach(self, vlen, nb_lanes=spatz_nb_lanes, use_spatz=True)

            self.add_c_flags([
                "-DCONFIG_GVSOC_ISS_USE_SPATZ",
            ])

            self.add_sources([
                "cpu/iss/src/spatz/fpu_sequencer.cpp",
            ])
        else:
            self.add_sources([
                "cpu/iss/src/snitch_fast/sequencer.cpp",
            ])

        self.add_sources([
            "cpu/iss/src/snitch_fast/snitch.cpp",
            "cpu/iss/src/snitch_fast/ssr.cpp",
            "cpu/iss/src/snitch_fast/fpu_lsu.cpp",
            "cpu/iss/src/prefetch/prefetch_single_line.cpp",
            "cpu/iss/src/csr.cpp",
            "cpu/iss/src/exec/exec_inorder.cpp",
            "cpu/iss/src/decode.cpp",
            "cpu/iss/src/lsu.cpp",
            "cpu/iss/src/timing.cpp",
            "cpu/iss/src/insn_cache.cpp",
            "cpu/iss/src/core.cpp",
            "cpu/iss/src/exception.cpp",
            "cpu/iss/src/regfile.cpp",
            "cpu/iss/src/resource.cpp",
            "cpu/iss/src/trace.cpp",
            "cpu/iss/src/syscalls.cpp",
            "cpu/iss/src/htif.cpp",
            "cpu/iss/src/memcheck.cpp",
            "cpu/iss/src/gdbserver.cpp",
            "cpu/iss/src/dbg_unit.cpp",
            "cpu/iss/flexfloat/flexfloat.c",
        ])

        path = os.path.dirname(__file__)
        self.add_properties({
            'regmap': {
                'name': 'ssr',
                'spec': f'{path}/archi/ssr.md',
                'header_prefix':  f'{path}/archi/ssr',
                'headers': [ 'gvsoc', 'regfields' ]
            }
        })


    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_req', itf, signature='wire<bool>')

    def o_VLSU(self, port: int, itf: gvsoc.systree.SlaveItf):
        """Binds the vector data port.

        This port is used for issuing data accesses to the memory for vector loads and stores.\n
        It instantiates a port of type vp::IoMaster.\n
        It is mandatory to bind it.\n

        Parameters
        ----------
        slave: gvsoc.systree.SlaveItf
            Slave interface
        """
        if self.inc_spatz:
            self.itf_bind(f'vlsu_{port}', itf, signature='io')
        else:
            raise RuntimeError('Vector data interface is not available')

    def gen_gui(self, parent_signal):
        active = super().gen_gui(parent_signal)

        if self.inc_spatz:
            ara = gvsoc.gui.Signal(self, active, name='ara', path='ara/label', groups=['regmap'], display=gvsoc.gui.DisplayStringBox())

            gvsoc.gui.Signal(self, ara, name="queue", path="ara/queue", groups=['regmap'])
            gvsoc.gui.Signal(self, ara, name="pc", path="ara/pc", groups=['regmap'])
            gvsoc.gui.Signal(self, ara, name="active", path="ara/active",
                display=gvsoc.gui.DisplayPulse(), groups=['regmap'])
            gvsoc.gui.Signal(self, ara, name="queue_full", path="ara/queue_full",
                display=gvsoc.gui.DisplayPulse(), groups=['regmap'])
            gvsoc.gui.Signal(self, ara, name="pending_insn", path="ara/nb_pending_insn", groups=['regmap'])
            gvsoc.gui.Signal(self, ara, name="waiting_insn", path="ara/nb_waiting_insn", groups=['regmap'])

            vlsu = gvsoc.gui.Signal(self, ara, name='vlsu', path='ara/vlsu/label', groups=['regmap'], display=gvsoc.gui.DisplayStringBox())
            gvsoc.gui.Signal(self, vlsu, name="active", path="ara/vlsu/active", display=gvsoc.gui.DisplayPulse(), groups=['regmap'])
            gvsoc.gui.Signal(self, vlsu, name="queue", path="ara/vlsu/queue", groups=['regmap'])
            gvsoc.gui.Signal(self, vlsu, name="pc", path="ara/vlsu/pc", groups=['regmap'])
            gvsoc.gui.Signal(self, vlsu, name="pending_insn", path="ara/vlsu/nb_pending_insn", groups=['regmap'])
            for i in range(0, self.get_property('ara/nb_ports', int)):
                port = gvsoc.gui.Signal(self, vlsu, name=f"port_{i}", path=f"ara/vlsu/port_{i}/addr", groups=['regmap'])
                gvsoc.gui.Signal(self, port, name="size", path=f"ara/vlsu/port_{i}/size", groups=['regmap'])
                gvsoc.gui.Signal(self, port, name="is_write", path=f"ara/vlsu/port_{i}/is_write", display=gvsoc.gui.DisplayPulse(), groups=['regmap'])

            vfpu = gvsoc.gui.Signal(self, ara, name='vfpu', path='ara/vfpu/label', groups=['regmap'], display=gvsoc.gui.DisplayStringBox())
            gvsoc.gui.Signal(self, vfpu, name="active", path="ara/vfpu/active", display=gvsoc.gui.DisplayPulse(), groups=['regmap'])
            gvsoc.gui.Signal(self, vfpu, name="pc", path="ara/vfpu/pc", groups=['regmap'])

            vslide = gvsoc.gui.Signal(self, ara, name='vslide', path='ara/vslide/label', groups=['regmap'], display=gvsoc.gui.DisplayStringBox())
            gvsoc.gui.Signal(self, vslide, name="active", path="ara/vslide/active", display=gvsoc.gui.DisplayPulse(), groups=['regmap'])
            gvsoc.gui.Signal(self, vslide, name="pc", path="ara/vslide/pc", groups=['regmap'])


class SnitchBare(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            inc_spatz: bool=False,
            core_id: int=0):

        isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_" + isa, isa,
            extensions=[ Xdma() ] )

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True)

        self.add_c_flags([
            "-DCONFIG_ISS_CORE=snitch_bare",
        ])

        self.add_sources([
            "cpu/iss/src/snitch_bare/snitch.cpp",
        ])

        if inc_spatz:
            self.add_sources([
                "cpu/iss/src/spatz.cpp",
            ])

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_req', itf, signature='wire<bool>')



class Snitch_fp_ss(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            inc_spatz: bool=False,
            core_id: int=0,
            timed: bool=False,
            htif: bool=False,
            pulp_v2: bool=False):

        isa_instance = isa_instances.get(isa)

        if isa_instances.get(isa) is None:
            if pulp_v2:
                extensions = [ PulpV2(hwloop=False, elw=False), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux() ]
            else:
                extensions = [ Rv32ssr(), Rv32frep(), Xdma(), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux() ]

            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("snitch_fp_ss_" + isa, isa,
                extensions=extensions )
            add_latencies(isa_instance)
            isa_instances[isa] = isa_instance

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="snitch", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True,
            prefetcher_size=32, timed=timed, custom_sources=True, htif=htif)

        if pulp_v2:
            self.add_c_flags([f'-DCONFIG_GVSOC_ISS_SNITCH_PULP_V2=1'])

        self.add_c_flags([
            "-DCONFIG_ISS_CORE=snitch_fp_ss",
        ])

        self.add_sources([
            "cpu/iss/src/snitch/snitch_fp_ss.cpp",
            "cpu/iss/src/snitch/ssr.cpp",
            "cpu/iss/src/prefetch/prefetch_single_line.cpp",
            "cpu/iss/src/csr.cpp",
            "cpu/iss/src/snitch_fp_ss/exec_inorder.cpp",
            "cpu/iss/src/snitch/decode.cpp",
            "cpu/iss/src/lsu.cpp",
            "cpu/iss/src/timing.cpp",
            "cpu/iss/src/insn_cache.cpp",
            "cpu/iss/src/snitch_fp_ss/iss.cpp",
            "cpu/iss/src/core.cpp",
            "cpu/iss/src/exception.cpp",
            "cpu/iss/src/regfile.cpp",
            "cpu/iss/src/resource.cpp",
            "cpu/iss/src/trace.cpp",
            "cpu/iss/src/syscalls.cpp",
            "cpu/iss/src/memcheck.cpp",
            "cpu/iss/src/htif.cpp",
            "cpu/iss/src/gdbserver.cpp",
            "cpu/iss/src/dbg_unit.cpp",
            "cpu/iss/flexfloat/flexfloat.c",
        ])

        if inc_spatz:
            self.add_sources([
                "cpu/iss/src/spatz.cpp",
            ])

    def o_BARRIER_REQ(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('barrier_req', itf, signature='wire<bool>')




class Spatz(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            use_rv32v=False):


        extensions = [ cpu.iss.isa_gen.isa_rvv.Rv32v() ]

        isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("spatz_" + isa, isa, extensions=extensions)

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="spatz")

        self.add_c_flags([
            "-DPIPELINE_STAGES=1",
            "-DCONFIG_ISS_CORE=spatz",
        ])

        self.add_sources([
            "cpu/iss/src/spatz.cpp",
        ])
