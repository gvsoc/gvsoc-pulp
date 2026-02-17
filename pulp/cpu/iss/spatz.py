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

#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#

from typing_extensions import override
from gvsoc.gui import Signal, DisplayStringBox, DisplayPulse
from cpu.iss.isa_gen.isa_gen import Isa
from pulp.snitch.snitch_isa import Xdma
from cpu.iss.isa_gen.isa_smallfloats import Xf16, Xf16alt, Xf8, XfvecSnitch, Xfaux
import gvsoc.systree
from gvsoc.systree import Component
import pulp.ara.ara_v2
import cpu.iss.isa_gen.isa_riscv_gen
from cpu.iss_v2.riscv import (Arch, ExecInOrder, Regfile, PrefetchSingleLine, Offload, Irq,
                              IrqExternal)
from pulp.cpu.iss.spatz_config import SpatzConfig
from cpu.iss_v2.riscv import RiscvCommon, IssModule


isa_instances: dict[str,Isa] = {}

class Spatz(RiscvCommon):

    def __init__(self,
            parent: Component,
            name: str,
            config: SpatzConfig
        ):

        isa_instance: Isa | None = isa_instances.get(config.isa)

        if isa_instances.get(config.isa) is None:

            extensions = [ Xdma(), Xf16(), Xf16alt(), Xf8(), XfvecSnitch(), Xfaux() ]

            isa_instance = cpu.iss.isa_gen.isa_riscv_gen.RiscvIsa("spatz_" + config.isa,
                config.isa, extensions=extensions)
            isa_instances[config.isa] = isa_instance

            pulp.ara.ara_v2.extend_isa(isa_instance)

        modules: dict[str, IssModule] = {
            'arch': Arch('Spatz'),
            'exec': ExecInOrder(scoreboard=True),
            'regfile': Regfile(scoreboard=True),
            'prefetch': PrefetchSingleLine(),
            'offload': Offload(),
            'irq': IrqExternal() if config.irq == 'external' else Irq()
        }

        super().__init__(parent, name, config=config, isa=isa_instance, modules=modules)

        self.add_sources([
            'cpu/iss_v2/src/cores/spatz/spatz.cpp',
        ])

        pulp.ara.ara_v2.attach(self, config.vlen, nb_lanes=config.nb_lanes, use_spatz=True)


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
        self.itf_bind(f'vlsu_{port}', itf, signature='io')

    @override
    def gen_gui(self, parent_signal: Signal) -> Signal:
        active = super().gen_gui(parent_signal)

        ara = Signal(self, active, name='ara', path='ara/label', groups=['regmap'],
            display=DisplayStringBox())

        _ = Signal(self, ara, name="queue", path="ara/queue", groups=['regmap'])
        _ = Signal(self, ara, name="pc", path="ara/pc", groups=['regmap'])
        _ = Signal(self, ara, name="active", path="ara/active",
            display=DisplayPulse(), groups=['regmap'])
        _ = Signal(self, ara, name="queue_full", path="ara/queue_full",
            display=DisplayPulse(), groups=['regmap'])
        _ = Signal(self, ara, name="pending_insn", path="ara/nb_pending_insn", groups=['regmap'])
        _ = Signal(self, ara, name="waiting_insn", path="ara/nb_waiting_insn", groups=['regmap'])

        vlsu = Signal(self, ara, name='vlsu', path='ara/vlsu/label', groups=['regmap'],
                      display=DisplayStringBox())
        _ = Signal(self, vlsu, name="active", path="ara/vlsu/active", display=DisplayPulse(),
                   groups=['regmap'])
        _ = Signal(self, vlsu, name="queue", path="ara/vlsu/queue", groups=['regmap'])
        _ = Signal(self, vlsu, name="pc", path="ara/vlsu/pc", groups=['regmap'])
        _ = Signal(self, vlsu, name="pending_insn", path="ara/vlsu/nb_pending_insn",
                   groups=['regmap'])
        for i in range(0, self.get_property('ara/nb_ports', int)):
            port = Signal(self, vlsu, name=f"port_{i}", path=f"ara/vlsu/port_{i}/addr",
                          groups=['regmap'])
            _ = Signal(self, port, name="size", path=f"ara/vlsu/port_{i}/size",
                       groups=['regmap'])
            _ = Signal(self, port, name="is_write", path=f"ara/vlsu/port_{i}/is_write",
                       display=DisplayPulse(), groups=['regmap'])

        vfpu = Signal(self, ara, name='vfpu', path='ara/vfpu/label', groups=['regmap'],
                      display=DisplayStringBox())
        _ = Signal(self, vfpu, name="active", path="ara/vfpu/active", display=DisplayPulse(),
                   groups=['regmap'])
        _ = Signal(self, vfpu, name="pc", path="ara/vfpu/pc", groups=['regmap'])

        vslide = Signal(self, ara, name='vslide', path='ara/vslide/label', groups=['regmap'],
                        display=DisplayStringBox())
        _ = Signal(self, vslide, name="active", path="ara/vslide/active",
                   display=DisplayPulse(), groups=['regmap'])
        _ = Signal(self, vslide, name="pc", path="ara/vslide/pc", groups=['regmap'])

        return active
