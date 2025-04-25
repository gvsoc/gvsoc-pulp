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
import pulp.ara.ara
from pulp.snitch.snitch_isa import *
from cpu.iss.isa_gen.isa_rvv import *
from cpu.iss.isa_gen.isa_smallfloats import *
import gvsoc.systree
import os
import gvsoc.gui



isa_instances = {}


class CVA6(cpu.iss.riscv.RiscvCommon):

    def __init__(self,
            parent,
            name,
            isa: str='rv32imafdc',
            misa: int=None,
            binaries: list=[],
            fetch_enable: bool=False,
            boot_addr: int=0,
            core_id: int=0,
            htif: bool=False,
            has_vector: bool=False,
            vlen: int=512):


        isa_instance = isa_instances.get(isa)

        self.has_vector = has_vector

        if isa_instances.get(isa) is None:
            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("cva6_" + isa, isa,
                extensions=[ ] )
            isa_instances[isa] = isa_instance

            if has_vector:
                pulp.ara.ara.extend_isa(isa_instance)

        if misa is None:
            misa = isa_instance.misa

        super().__init__(parent, name, isa=isa_instance, misa=misa, core="cva6", scoreboard=True,
            fetch_enable=fetch_enable, boot_addr=boot_addr, core_id=core_id, riscv_exceptions=True,
            prefetcher_size=64, htif=htif, binaries=binaries, timed=True, internal_atomics=True,
            supervisor=True, user=True, mmu=True, pmp=True, custom_sources=True)

        self.add_c_flags([
            "-DCONFIG_ISS_CORE=cva6",
        ])

        self.add_sources([
            "cpu/iss/src/cva6/cva6.cpp",
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
            "cpu/iss/src/mmu.cpp",
            "cpu/iss/src/pmp.cpp",
            "cpu/iss/src/gdbserver.cpp",
            "cpu/iss/src/dbg_unit.cpp",
            "cpu/iss/flexfloat/flexfloat.c",
            "cpu/iss/src/cva6/cva6.cpp",
        ])

        if has_vector:
            pulp.ara.ara.attach(self, vlen)

    def gen_gui(self, parent_signal):
        active = super().gen_gui(parent_signal)

        if self.has_vector:
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
            gvsoc.gui.Signal(self, vlsu, name="addr", path="ara/vlsu/addr", groups=['regmap'])
            gvsoc.gui.Signal(self, vlsu, name="size", path="ara/vlsu/size", groups=['regmap'])
            gvsoc.gui.Signal(self, vlsu, name="is_write", path="ara/vlsu/is_write", display=gvsoc.gui.DisplayPulse(), groups=['regmap'])

            vfpu = gvsoc.gui.Signal(self, ara, name='vfpu', path='ara/vfpu/label', groups=['regmap'], display=gvsoc.gui.DisplayStringBox())
            gvsoc.gui.Signal(self, vfpu, name="active", path="ara/vfpu/active", display=gvsoc.gui.DisplayPulse(), groups=['regmap'])
            gvsoc.gui.Signal(self, vfpu, name="pc", path="ara/vfpu/pc", groups=['regmap'])

            vslide = gvsoc.gui.Signal(self, ara, name='vslide', path='ara/vslide/label', groups=['regmap'], display=gvsoc.gui.DisplayStringBox())
            gvsoc.gui.Signal(self, vslide, name="active", path="ara/vslide/active", display=gvsoc.gui.DisplayPulse(), groups=['regmap'])
            gvsoc.gui.Signal(self, vslide, name="pc", path="ara/vslide/pc", groups=['regmap'])

    def o_VLSU(self, itf: gvsoc.systree.SlaveItf):
            """Binds the vector data port.

            This port is used for issuing data accesses to the memory for vector loads and stores.\n
            It instantiates a port of type vp::IoMaster.\n
            It is mandatory to bind it.\n

            Parameters
            ----------
            slave: gvsoc.systree.SlaveItf
                Slave interface
            """
            if self.has_vector:
                self.itf_bind('vlsu', itf, signature='io')
            else:
                raise RuntimeError('Vector data interface is not available')
