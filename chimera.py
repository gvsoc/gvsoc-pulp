import gvsoc.runner
import gvsoc.systree

import pulp.chips.chimera.apb_soc_ctrl as apb_soc_ctrl
import pulp.chips.chimera.chimera_reg_top as chimera_reg_top
import pulp.cpu.iss.pulp_cores as iss
import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import hjson
from pulp.fll.fll_v1 import Fll
from pulp.padframe.padframe_v1 import Padframe
from utils.clock_generator import Clock_generator
import pulp.soc_eu.soc_eu_v2 as soc_eu_module
import pulp.itc.itc_v1 as itc
import cpu.clint
from pulp.snitch.snitch_cluster.snitch_cluster import ClusterArch, Area, SnitchCluster
from pulp.chips.snitch.snitch import SnitchArchProperties
from pulp.stdout.stdout_v3 import Stdout


class SnitchClusterGroup(gvsoc.systree.Component):
    def __init__(self, parent, name):
        super().__init__(parent, name)

        entry = 0x30000000
        nb_clusters = 5

        # Bootrom
        snitch_rom = memory.Memory(
            self,
            'snitch_rom',
            size=0x1000,
            stim_file="pulp/pulp/chips/chimera/snitch/snitch_bootrom.bin")

        # Narrow 64bits router
        self.narrow_axi = router.Router(self, 'narrow_axi', bandwidth=4)

        # Wide 512 bits router
        self.wide_axi = router.Router(self, 'wide_axi', bandwidth=64)

        cluster_start_addr = [
            0x40800000, 0x40600000, 0x40400000, 0x40200000, 0x40000000
        ]

        self.clusters = []
        for id in range(0, nb_clusters):
            properties = SnitchArchProperties()
            cluster_arch = ClusterArch(properties=properties,
                                       base=cluster_start_addr[id],
                                       first_hartid=(id * 9) + 1,
                                       auto_fetch=True,
                                       boot_addr=0x30000000)
            self.clusters.append(
                SnitchCluster(self, f'cluster_{id}', cluster_arch,
                              entry=entry))

        self.narrow_axi.o_MAP(snitch_rom.i_INPUT(),
                              base=0x30000000,
                              size=0x1000,
                              rm_base=True)
        self.narrow_axi.add_mapping("clu_reg",
                                    base=0x30001000,
                                    size=0x1000,
                                    remove_offset=0x0)
        self.narrow_axi.add_mapping("mem_island",
                                    base=0x48000000,
                                    size=0x08000000,
                                    remove_offset=0x0)
        self.narrow_axi.add_mapping("clint",
                                    base=0x02040000,
                                    size=0x0010_0000,
                                    remove_offset=0x0)
        self.bind(self.narrow_axi, "clu_reg", self, "clu_reg_ext")
        self.bind(self.narrow_axi, "clint", self, "clint_ext")
        self.bind(self.narrow_axi, "mem_island", self, "mem_island_ext")
        self.o_NARROW_INPUT(self.narrow_axi.i_INPUT())

        # Clusters
        for id in range(0, nb_clusters):
            self.clusters[id].o_NARROW_SOC(self.narrow_axi.i_INPUT())
            self.clusters[id].o_WIDE_SOC(self.wide_axi.i_INPUT())
            self.narrow_axi.o_MAP(self.clusters[id].i_NARROW_INPUT(),
                                  base=cluster_start_addr[id],
                                  size=0x00200000,
                                  rm_base=False)

            # self.bind(self.narrow_axi, "mem_island", self, "mem_island_ext")

            for core in range(0, cluster_arch.nb_core):
                core_id = core + id * cluster_arch.nb_core
                self.__o_SW_IRQ(core_id, self.clusters[id].i_MSIP(core))

    def i_SW_IRQ(self, core) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self,
                                      f'sw_irq_{core}',
                                      signature='wire<bool>')

    def __o_SW_IRQ(self, core, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'sw_irq_{core}',
                      itf,
                      signature='wire<bool>',
                      composite_bind=True)

    def i_NARROW_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'narrow_input', signature='io')

    def o_NARROW_INPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('narrow_input', itf, signature='io', composite_bind=True)


class SafetyIsland(gvsoc.systree.Component):
    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        [args, otherArgs] = parser.parse_known_args()
        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        # Load configuration from HJSON file
        with open('pulp/pulp/chips/chimera/safety-island.hjson', 'r') as file:
            config = hjson.load(file)
        self.add_properties(config)
        soc_events = self.get_property('soc_events')

        soc_eu = soc_eu_module.Soc_eu(
            self,
            'soc_eu',
            ref_clock_event=soc_events['soc_evt_ref_clock'],
            **self.get_property('peripherals/soc_eu/config'))
        soc_ctrl = apb_soc_ctrl.Apb_soc_ctrl(self, 'soc_ctrl', self)

        fll_soc = Fll(self, 'fll_soc')
        fll_periph = Fll(self, 'fll_periph')
        fll_cluster = Fll(self, 'fll_cluster')

        fc_events = self.get_property('peripherals/fc_itc/irq')
        fc_itc = itc.Itc_v1(self, 'fc_itc')

        # Access memory and cluster configuration from the HJSON file
        self.memory_config = config['memory_config']
        self.cluster_l1_config = config['cluster_l1_config']
        self.memory_island_config = config['memory_island_config']

        # Create loader using the binary provided
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        obi_ico = router.Router(self, "obi_ico")
        l2_tcdm_ico = router.Router(self, "l2_tcdm_ico")

        # Create the memory and router components based on the configuration
        cluster_l1_placeholder = memory.Memory(
            self,
            'cluster_l1_placeholder',
            size=self.cluster_l1_config["size"])

        rom = memory.Memory(
            self,
            'rom',
            size=0x1000,
            stim_file=self.get_file_path('pulp/chips/chimera/bootrom.bin'))

        clint = cpu.clint.Clint(self, 'clint', nb_cores=47)

        stdout = Stdout(self, 'stdout')

        # JUNGVI: Memory component placeholder for the memory island
        memory_island_placeholder = memory.Memory(
            self,
            'memory_island_placeholder',
            size=self.memory_island_config["size"])

        l2_private_data_memory = memory.Memory(
            self,
            'private_data_memory',
            size=self.memory_config["data"]["size"])
        l2_private_inst_memory = memory.Memory(
            self,
            'private_inst_memory',
            size=self.memory_config["inst"]["size"])
        # sn_cluster_cfgregs = memory.Memory(self, 'cluster_cfgregs', size=config["obi_ico"]["cfgreg"]["size"])
        reg_top = chimera_reg_top.chimera_reg_top(self, 'reg_top')

        # Setup host and loader connections
        host = iss.FcCore(self, 'fc', cluster_id=0, boot_addr=0x02000000)

        host.o_FETCH(l2_tcdm_ico.i_INPUT())
        host.o_DATA(l2_tcdm_ico.i_INPUT())
        host.o_DATA_DEBUG(l2_tcdm_ico.i_INPUT())

        loader.o_OUT(l2_tcdm_ico.i_INPUT())
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(soc_ctrl.i_ENTRY())

        # Mapping configurations for data, inst, and cluster memory
        l2_tcdm_ico.o_MAP(l2_private_data_memory.i_INPUT(), "data_memory",
                          **self.get_property('memory_config/data'))
        l2_tcdm_ico.o_MAP(l2_private_inst_memory.i_INPUT(), "inst_memory",
                          **self.get_property('memory_config/inst'))
        l2_tcdm_ico.o_MAP(cluster_l1_placeholder.i_INPUT(),
                          "cluster_l1_placeholder",
                          **self.get_property('cluster_l1_config'))
        l2_tcdm_ico.o_MAP(obi_ico.i_INPUT(), "obi_ico",
                          **self.get_property('obi_ico/mapping'))
        l2_tcdm_ico.o_MAP(memory_island_placeholder.i_INPUT(),
                          "memory_island_placeholder",
                          **self.get_property('memory_island_config'))
        l2_tcdm_ico.o_MAP(rom.i_INPUT(), base=0x2000000, size=0x1000)
        l2_tcdm_ico.o_MAP(clint.i_INPUT(), base=0x02040000, size=0x0010_0000)
        l2_tcdm_ico.o_MAP(reg_top.i_INPUT(),
                          **self.get_property('obi_ico/cfgreg'))

        obi_ico.add_mapping('soc_ctrl',
                            **self.get_property('obi_ico/soc_ctrl'))
        obi_ico.add_mapping('fll_soc', **self.get_property('obi_ico/fll_soc'))
        obi_ico.add_mapping('fll_periph',
                            **self.get_property('obi_ico/fll_periph'))
        obi_ico.add_mapping('fll_cluster',
                            **self.get_property('obi_ico/fll_cluster'))
        obi_ico.add_mapping('soc_eu', **self.get_property('obi_ico/soc_eu'))
        obi_ico.add_mapping('fc_itc', **self.get_property('obi_ico/fc_itc'))
        obi_ico.add_mapping('stdout', **self.get_property('obi_ico/stdout'))

        # Create and connect the SOC control component
        self.bind(obi_ico, 'soc_ctrl', soc_ctrl, 'input')
        self.bind(obi_ico, 'soc_eu', soc_eu, 'input')
        self.bind(obi_ico, 'fll_soc', fll_soc, 'input')
        self.bind(obi_ico, 'fll_periph', fll_periph, 'input')
        self.bind(obi_ico, 'fll_cluster', fll_cluster, 'input')
        self.bind(obi_ico, 'fc_itc', fc_itc, 'input')
        self.bind(obi_ico, 'stdout', stdout, 'input')

        self.bind(self, 'ref_clock', fll_soc, 'ref_clock')
        self.bind(self, 'ref_clock', fll_periph, 'ref_clock')
        self.bind(self, 'ref_clock', fll_cluster, 'ref_clock')
        self.bind(fll_soc, 'clock_out', self, 'fll_soc_clock')
        self.bind(fll_periph, 'clock_out', self, 'fll_periph_clock')
        self.bind(fll_cluster, 'clock_out', self, 'fll_cluster_clock')

        snitch_cluster_group = SnitchClusterGroup(self, "snitch_cluster_group")
        self.bind(snitch_cluster_group, "clu_reg_ext", l2_tcdm_ico, "input")
        self.bind(snitch_cluster_group, "clint_ext", l2_tcdm_ico, "input")
        self.bind(snitch_cluster_group, "mem_island_ext", l2_tcdm_ico, "input")
        l2_tcdm_ico.o_MAP(snitch_cluster_group.i_NARROW_INPUT(),
                          base=0x4000_0000,
                          size=0x0100_0000,
                          rm_base=False)

        for i in range(0, 46):
            clint.o_SW_IRQ(i + 1, snitch_cluster_group.i_SW_IRQ(i))

        # Interrupts
        for name, irq in fc_events.items():
            if len(name.split('.')) == 2:
                comp_name, itf_name = name.split('.')
                self.bind(self.get_component(comp_name), itf_name, fc_itc,
                          'in_event_%d' % irq)

        self.bind(self, 'ref_clock', soc_eu, 'ref_clock')
        self.bind(soc_eu, 'ref_clock_event', fc_itc,
                  'in_event_%d' % fc_events['evt_clkref'])
        self.bind(soc_ctrl, 'event', soc_eu, 'event_in')


class ChimeraBoard(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, options):

        super(ChimeraBoard, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=50000000)

        safety_island = SafetyIsland(self, 'safety_island', parser)

        padframe_config_file = 'pulp/chips/chimera/padframe.json'

        padframe = Padframe(self, 'padframe', config_file=padframe_config_file)

        ref_clock = Clock_domain(self, 'ref_clock', frequency=65536)
        ref_clock_generator = Clock_generator(self, 'ref_clock_generator')
        self.bind(ref_clock, 'out', ref_clock_generator, 'clock')

        soc_clock = Clock_domain(self, 'soc_clock_domain', frequency=50000000)
        self.bind(soc_clock, 'out', safety_island, 'clock')
        self.bind(soc_clock, 'out', padframe, 'clock')
        self.bind(safety_island, 'fll_soc_clock', self, 'clock_in')
        self.bind(ref_clock_generator, 'clock_sync', padframe, 'ref_clock_pad')

        self.bind(padframe, 'ref_clock', safety_island, 'ref_clock')


class Target(gvsoc.runner.Target):

    gapy_description = "Chimera virtual board"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options, model=ChimeraBoard)
