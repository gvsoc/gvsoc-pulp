# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

try:
    from typing import override
except ImportError:
    from typing_extensions import override
from config_tree import Config
from gvsoc.systree import Component, SlaveItf
from gvsoc.gui import Signal, DisplayPulse, DisplayBox
from gvsoc.signature import IoV2Sync


class TimerConfig(Config):
    pass

class Timer(Component):

    def __init__(self, parent: Component, name: str, config: TimerConfig):

        super(Timer, self).__init__(parent, name)

        self.add_sources(['ips/pulp/timer/timer_v2.cpp'])

    @override
    def gen_gui(self, parent_signal: Signal) -> Signal:
        active = Signal(self, parent_signal, name=self.name)

        _ = Signal(self, active, name='irq_0', path='subtimer_0/irq_state',
            display=DisplayPulse(), groups='regmap')
        _ = Signal(self, active, name='irq_1', path='subtimer_1/irq_state',
            display=DisplayPulse(), groups='regmap')

        for i in range(0, 2):
            timer_name = 'LO' if i == 0 else 'HI'
            timer = Signal(self, active, name=f'subtimer{i}')
            _ = Signal(self, timer, name='enabled', path=f'subtimer_{i}/enabled',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, timer, name='irq_enabled', path=f'subtimer_{i}/irq_enabled',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, timer, name='input_event', path=f'subtimer_{i}/input_event',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, timer, name='cmp_clear', path=f'subtimer_{i}/cmp_clear',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, timer, name='one_shot', path=f'subtimer_{i}/one_shot',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, timer, name='prescaler', path=f'subtimer_{i}/prescaler',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, timer, name='ref_clock', path=f'subtimer_{i}/ref_clock',
                display=DisplayPulse(), groups='regmap')
            _ = Signal(self, timer, name='count', path='regmap/CNT_' + timer_name,
                display=DisplayBox("int"), groups='regmap')
            _ = Signal(self, timer, name='compare', path='regmap/CMP_' + timer_name,
                display=DisplayBox("int"), groups='regmap')
            _ = Signal(self, timer, name='prescaler_value', path=f'subtimer_{i}/prescaler_value',
                display=DisplayBox("int"), groups='regmap')
            _ = Signal(self, timer, name='prescaler_current_value',
                path=f'subtimer_{i}/prescaler_current_value', display=DisplayBox("int"), groups='regmap')

        _ = Signal(self, active, name='regmap', path='regmap', is_group=True, groups=["regmap"])

        return active

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def i_REF_CLOCK(self) -> SlaveItf:
        return SlaveItf(self, 'ref_clock', signature='clock_gen')

    def o_IRQ0(self, itf: SlaveItf):
        self.itf_bind('irq_itf_0', itf, signature='wire<bool>')

    def o_IRQ1(self, itf: SlaveItf):
        self.itf_bind('irq_itf_1', itf, signature='wire<bool>')
