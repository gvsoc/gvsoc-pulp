#
# Copyright (C) 2020 GreenWaves Technologies
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

import os
import gvsoc.systree as st
import pulp.cpu.iss.pulp_cores as iss
import memory.memory as memory
import interco.router as router
import cache.cache as cache
import interco.interleaver as interleaver
from pulp.chips.pulp_open.l2_subsystem import L2Subsystem, L2Attr
import pulp.chips.pulp_open.soc_interco as soc_interco
import pulp.chips.pulp_open.apb_soc_ctrl as apb_soc_ctrl
import pulp.itc.itc_v1 as itc
import pulp.gpio.gpio_v3 as gpio_module
import pulp.soc_eu.soc_eu_v2 as soc_eu_module
from pulp.timer.timer_v2 import Timer
from pulp.stdout.stdout_v3 import Stdout
from pulp.icache_ctrl.icache_ctrl_v2 import Icache_ctrl
from pulp.fll.fll_v1 import Fll
from pulp.chips.pulp_open.cluster import get_cluster_name
from vp.clock_domain import Clock_domain
from pulp.chips.pulp_open.udma import Udma
from interco.bus_watchpoint import Bus_watchpoint
from pulp.adv_dbg_unit.pulp_tap import Pulp_tap
from pulp.adv_dbg_unit.riscv_tap import Riscv_tap
from gdbserver.gdbserver import Gdbserver
import utils.loader.loader
from gvrun.parameter import TargetParameter
from gvrun.attribute import Tree


class SocAttr(Tree):
    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.l2 = L2Attr(self, 'l2')

class SocConf(st.Component):
    def __init__(self, property_file):
        super(SocConf, self).__init__(parent=None, name='')

        self.add_properties(self.load_property_file(property_file))


class Soc(st.Component):

    def __init__(self, parent, name, attr: SocAttr, parser, config_file, chip, cluster, pulpnn=False):
        super(Soc, self).__init__(parent, name)

        #
        # Properties
        #

        soc_conf = SocConf(config_file)
        self.conf = soc_conf

        nb_cluster = chip.get_property('nb_cluster', int)
        nb_pe = cluster.conf.get_property('nb_pe', int)
        soc_events = soc_conf.get_property('soc_events')
        udma_conf_path = 'pulp/chips/pulp_open/udma.json'
        udma_conf = self.load_property_file(udma_conf_path)
        fc_events = soc_conf.get_property('peripherals/fc_itc/irq')

        TargetParameter(
            self, name='binary', value=None, description='Binary to be loaded and started',
            cast=str
        )

        #
        # Components
        #

        # Loader
        binary = None
        if os.environ.get('USE_GVRUN') is None:
            if parser is not None:
                [args, otherArgs] = parser.parse_known_args()
                binary = args.binary

        loader = utils.loader.loader.ElfLoader(self, 'loader', binary)

        # Debug ROM
        debug_rom = memory.Memory(self, 'debug_rom',
            size=soc_conf.get_property('apb_ico/mappings/debug_rom/size'),
            stim_file=self.get_file_path('pulp/chips/pulp/debug_rom.bin')
        )

        # FLL
        fll_soc = Fll(self, 'fll_soc')
        fll_periph = Fll(self, 'fll_periph')
        fll_cluster = Fll(self, 'fll_cluster')

        # FC
        fc = iss.FcCore(self, 'fc', pulpnn=pulpnn)

        # FC ITC
        fc_itc = itc.Itc_v1(self, 'fc_itc')

        # FC icache
        fc_icache = cache.Cache(self, 'fc_icache', **soc_conf.get_property('peripherals/fc_icache/config'))

        # FC icache controller
        fc_icache_ctrl = Icache_ctrl(self, 'fc_icache_ctrl')

        # APB soc controller
        soc_ctrl = apb_soc_ctrl.Apb_soc_ctrl(self, 'apb_soc_ctrl', self)

        # APB
        apb_ico = router.Router(self, 'apb_ico', latency=8)

        # Soc interconnect
        soc_ico = soc_interco.Soc_interco(self, 'soc_ico', self, cluster)

        # AXI
        axi_ico = router.Router(self, 'axi_ico', latency=12)

        # GPIO
        gpio = gpio_module.Gpio(self, 'gpio', nb_gpio=soc_conf.get_property('peripherals/gpio/nb_gpio'), soc_event=soc_events['soc_evt_gpio'])

        # UDMA
        udma = Udma(self, 'udma', config_file=udma_conf_path)

        # RISCV bus watchpoint
        fc_tohost = soc_conf.get_property('fc/riscv_fesvr_tohost_addr')
        if fc_tohost is not None:
            bus_watchpoint = Bus_watchpoint(self, 'bus_watchpoint', fc_tohost)

        # L2
        self.l2 = L2Subsystem(self, 'l2', attr.l2)

        # SOC EU
        soc_eu = soc_eu_module.Soc_eu(self, 'soc_eu', ref_clock_event=soc_events['soc_evt_ref_clock'], **soc_conf.get_property('peripherals/soc_eu/config'))

        # Timers
        timer = Timer(self, 'timer')
        timer_1 = Timer(self, 'timer_1')

        # Stdout
        stdout = Stdout(self, 'stdout')

        # Pulp TAP
        pulp_tap = Pulp_tap(self, 'pulp_tag', **soc_conf.get_property('pulp_tap/config'))

        # RISCV TAP
        harts = []
        harts.append([(0 << 5) | (31 << 0), 'fc'])

        for cid in range(0, nb_cluster):
            for pe in range(0, nb_pe):
                hart_id = (cid << 5) | pe

                name = 'cluster%d_pe%d' % (cid, pe)
                harts.append([hart_id, name])

        riscv_tap = Riscv_tap(self, 'riscv_tap', **soc_conf.get_property('riscv_tap/config'), harts=harts)

        # GDB server
        gdbserver = Gdbserver(self, 'gdbserver')



        #
        # Bindings
        #

        # Loader
        self.bind(loader, 'out', axi_ico, 'input')
        self.bind(loader, 'start', fc, 'fetchen')
        self.bind(loader, 'entry', fc, 'bootaddr')

        # FLL
        self.bind(fll_soc, 'clock_out', self, 'fll_soc_clock')

        for cid in range(0, nb_cluster):
            self.bind(fll_cluster, 'clock_out', self, get_cluster_name(cid) + '_fll')

        periph_clock = Clock_domain(self, 'periph_clock', frequency=50000000)

        self.bind(fll_periph, 'clock_out', periph_clock, 'clock_in')
        self.bind(periph_clock, 'out', udma, 'periph_clock')

        # FC
        self.bind(fc, 'fetch', fc_icache, 'input_0')
        self.bind(fc, 'irq_ack', fc_itc, 'irq_ack')
        if soc_conf.get_property('fc/riscv_fesvr_tohost_addr') is not None:
            self.bind(fc, 'data', bus_watchpoint, 'input')
        else:
            self.bind(fc, 'data', soc_ico, 'fc_data')
        self.bind(fc, 'data_debug', soc_ico, 'fc_data')
        self.bind(fc, 'flush_cache_req', fc_icache, 'flush')

        # FC icache
        self.bind(fc_icache, 'refill', soc_ico, 'fc_fetch')
        self.bind(fc_icache, 'flush_ack', fc, 'flush_cache_ack')

        # FC ITC
        self.bind(fc_itc, 'irq_req', fc, 'irq_req')

        # FC icache controller
        self.bind(fc_icache_ctrl, 'enable', fc_icache, 'enable')
        self.bind(fc_icache_ctrl, 'flush', fc_icache, 'flush')
        self.bind(fc_icache_ctrl, 'flush', fc, 'flush_cache')
        self.bind(fc_icache_ctrl, 'flush_line', fc_icache, 'flush_line')
        self.bind(fc_icache_ctrl, 'flush_line_addr', fc_icache, 'flush_line_addr')

        # Interrupts
        for name, irq in fc_events.items():
            if len(name.split('.')) == 2:
                comp_name, itf_name = name.split('.')
                self.bind(self.get_component(comp_name), itf_name, fc_itc, 'in_event_%d' % irq)

        # APB soc controller
        self.bind(self, 'ref_clock', fll_soc, 'ref_clock')
        self.bind(self, 'ref_clock', fll_periph, 'ref_clock')
        self.bind(self, 'ref_clock', fll_cluster, 'ref_clock')

        self.bind(soc_ctrl, 'bootaddr', fc, 'bootaddr')
        self.bind(self, 'bootsel', soc_ctrl, 'bootsel')
        self.bind(soc_ctrl, 'confreg_soc', pulp_tap, 'confreg_soc')
        self.bind(pulp_tap, 'confreg_ext', soc_ctrl, 'confreg_ext')

        # APB
        apb_ico_mappings = soc_conf.get_property('apb_ico/mappings')
        apb_ico.add_property('mappings', apb_ico_mappings)
        self.bind(apb_ico, 'stdout', stdout, 'input')
        self.bind(apb_ico, 'fc_icache_ctrl', fc_icache_ctrl, 'input')
        self.bind(apb_ico, 'apb_soc_ctrl', soc_ctrl, 'input')
        self.bind(apb_ico, 'soc_eu', soc_eu, 'input')
        self.bind(apb_ico, 'gpio', gpio, 'input')
        self.bind(apb_ico, 'udma', udma, 'input')
        self.bind(apb_ico, 'fc_itc', fc_itc, 'input')
        self.bind(apb_ico, 'fc_dbg_unit', riscv_tap, 'input')
        self.bind(apb_ico, 'pmu', self, 'pmu_input')
        self.bind(apb_ico, 'debug_rom', debug_rom, 'input')
        self.bind(apb_ico, 'fll_soc', fll_soc, 'input')
        self.bind(apb_ico, 'fll_periph', fll_periph, 'input')
        self.bind(apb_ico, 'fll_cluster', fll_cluster, 'input')
        self.bind(apb_ico, 'fc_timer', timer, 'input')
        self.bind(apb_ico, 'fc_timer_1', timer_1, 'input')

        # Soc interconnect
        self.bind(soc_ico, 'apb', apb_ico, 'input')
        self.bind(soc_ico, 'axi_master', axi_ico, 'input')
        self.bind(soc_ico, 'l2_priv0', self.l2, 'priv0')
        self.bind(soc_ico, 'l2_priv1', self.l2, 'priv1')
        for i in range(0, attr.l2.shared.nb_regions):
            self.bind(soc_ico, 'l2_shared_%d' % i, self.l2, 'shared_%d' % i)

        # AXI
        self.bind(axi_ico, 'axi_proxy', self, 'axi_proxy')
        axi_ico.add_mapping('axi_proxy', base=0x20000000, size=0x10000000)
        self.bind(soc_ico, 'axi_proxy', axi_ico, 'input')
        self.bind(soc_ico, 'ddr', axi_ico, 'input')

        axi_ico.add_mapping('ddr', base=0x80000000, size=0x00100000, remove_offset=0x80000000)
        self.bind(axi_ico, 'ddr', self, 'ddr')

        self.bind(axi_ico, 'soc', soc_ico, 'axi_slave')
        self.bind(self, 'soc_input', axi_ico, 'input')
        axi_ico.add_mapping('soc'      , **soc_conf.get_property('mapping'))
        base = cluster.conf.get_property('mapping/base')
        size = cluster.conf.get_property('mapping/size')
        for cid in range(0, nb_cluster):
            axi_ico.add_mapping(get_cluster_name(cid), base=base + size * cid, size=size)
            self.bind(axi_ico, get_cluster_name(cid), self, get_cluster_name(cid) + '_input')

        # GPIO
        self.bind(gpio, 'irq', fc_itc, 'in_event_%d' % soc_conf.get_property('peripherals/fc_itc/irq/evt_gpio'))
        self.bind(gpio, 'event', soc_eu, 'event_in')
        for i in range(0, soc_conf.get_property('peripherals/gpio/nb_gpio')):
            self.bind(self, 'gpio%d' % i, gpio, 'gpio%d' % i)

        # UDMA
        self.bind(udma, 'l2_itf', soc_ico, 'udma_tx')
        self.bind(udma, 'event_itf', soc_eu, 'event_in')

        for itf in udma_conf['interfaces']:
            itf_conf = udma_conf.get(itf)
            nb_channels = itf_conf.get('nb_channels')
            is_master = itf_conf.get('is_master')
            is_slave = itf_conf.get('is_slave')
            is_dual = itf_conf.get('is_dual')
            for channel in range(0, nb_channels):
                itf_name = itf + str(channel)

                if is_master:
                    self.bind(udma, itf_name, self, itf_name)
                if is_slave:
                    if is_dual:
                        self.bind(self, itf + str(channel*2), udma, itf + str(channel*2))
                        self.bind(self, itf + str(channel*2+1), udma, itf + str(channel*2+1))
                    else:
                        self.bind(self, itf_name, udma, itf_name)


        # Riscv bus watchpoint
        if soc_conf.get_property('fc/riscv_fesvr_tohost_addr') is not None:
            self.bind(bus_watchpoint, 'output', soc_ico, 'fc_data')

        # Soc eu
        self.bind(soc_eu, 'fc_event_itf', fc_itc, 'soc_event')
        self.bind(self, 'event', soc_eu, 'event_in')
        self.bind(self, 'ref_clock', soc_eu, 'ref_clock')
        self.bind(soc_eu, 'ref_clock_event', fc_itc, 'in_event_%d' % fc_events['evt_clkref'])
        self.bind(soc_ctrl, 'event', soc_eu, 'event_in')

        # Timers
        self.bind(timer, 'irq_itf_0', fc_itc, 'in_event_10')
        self.bind(timer, 'irq_itf_1', fc_itc, 'in_event_11')

        self.bind(timer_1, 'irq_itf_0', fc_itc, 'in_event_12')
        self.bind(timer_1, 'irq_itf_1', fc_itc, 'in_event_13')

        self.bind(self, 'ref_clock', timer, 'ref_clock')
        self.bind(self, 'ref_clock', timer_1, 'ref_clock')

        # Pulp TAP
        self.bind(self, 'jtag0', pulp_tap, 'jtag_in')
        self.bind(pulp_tap, 'jtag_out', riscv_tap, 'jtag_in')

        # RISCV TAP
        self.bind(riscv_tap, 'jtag_out', self, 'jtag0_out')
        self.bind(riscv_tap, 'fc', fc, 'halt')

        for cluster in range(0, nb_cluster):
            for pe in range(0, nb_pe):
                name = 'cluster%d_pe%d' % (cluster, pe)
                self.bind(riscv_tap, name, self, 'halt_' + name)

        # Cluster
        self.bind(self, 'dma_irq', fc_itc, 'in_event_8')

        # Pulp TAP
        self.bind(pulp_tap, 'io', soc_ico, 'debug')

        # Make sure the loader is notified by any executable attached to the hieararchy of this
        # component so that it is automatically loaded
        self.loader = loader
        self.register_binary_handler(self.handle_binary)

    def gen_gtkw_conf(self, tree, traces):
        if tree.get_view() == 'overview':
            self.vcd_group(skip=True)
        else:
            self.vcd_group(skip=False)

    def configure(self):
        # We configure the loader binary now int he configure steps since it is coming from
        # a parameter which can be set either from command line or from the build process
        binary = self.get_parameter('binary')
        if binary is not None:
            self.loader.set_binary(binary)

    def handle_binary(self, binary):
        # This gets called when an executable is attached to a hierarchy of components containing
        # this one
        self.set_parameter('binary', binary)
