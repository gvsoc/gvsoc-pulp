import gvsoc.runner
import gvsoc.systree 

import pulp.chips.chimera.apb_soc_ctrl as apb_soc_ctrl
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
import math

class MemoryIsland(gvsoc.systree.Component):
    def __init__(self, parent, name, config):
        super(MemoryIsland, self).__init__(parent, name)
        self.add_properties(config)            
        nb_wide_banks               =  self.get_property("properties/nb_wide_banks", int)
        nb_narrow_per_wide_bank     =  self.get_property("properties/nb_narrow_per_wide_bank", int)
        memory_size                 =  self.get_property("mapping/size", int)
        narrow_word_size            =  self.get_property("properties/narrow_word_size", int)
        wide_word_size              =  int(narrow_word_size*nb_narrow_per_wide_bank)
        nb_narrow_banks             =  int(nb_wide_banks * nb_narrow_per_wide_bank)
        memory_size_per_narrow_bank =  memory_size // nb_narrow_banks  
        nb_masters                  = 4
        # Ensure memory size is a multiple of word size
        assert memory_size                 % narrow_word_size == 0, f"Memory size {memory_size} is not a multiple of narrow word size {narrow_word_size}"
        assert memory_size                 % wide_word_size   == 0, f"Memory size {memory_size} is not a multiple of wide word size {wide_word_size}"
        assert memory_size                 % nb_narrow_banks  == 0, f"Memory size {memory_size} is not a multiple of number of narrow banks {nb_narrow_banks}"
        assert memory_size_per_narrow_bank % narrow_word_size == 0, f"Memory size per bank {memory_size_per_narrow_bank} is not a multiple of number of narrow_word_size {narrow_word_size}"

        # Memory banks corresponds to the narrow banks         
        banks = []
        for bank in range(0, nb_narrow_banks):
            banks.append(memory.Memory(self, f'bank_{bank}', size=memory_size_per_narrow_bank, width_log2=int(math.log2(narrow_word_size))))
        
        from pulp.cluster.l1_interleaver import L1_interleaver
        from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver # using the wide interleaver from Snitch, priority is always wide
        # print(f"numer of narrow banks")
        narrow_interleaver = L1_interleaver(self, 'narrow_interleaver',    nb_slaves = nb_narrow_banks, nb_masters = nb_masters, interleaving_bits=int(math.log2(narrow_word_size)))
        wide_interleaver   = DmaInterleaver(self,  slave= nb_narrow_banks, nb_master_ports=nb_masters,  nb_banks=nb_narrow_banks, bank_width=narrow_word_size)

        for bank in range(0, nb_narrow_banks):
            self.bind(narrow_interleaver, 'out_%d' % bank, banks[bank], 'input')
            self.bind(wide_interleaver, 'out_%d' % bank, banks[bank], 'input')

        for master in range(0, nb_masters):
            self.bind(self, f'in_narrow_{master}', narrow_interleaver, f'in_{master}')
            self.bind(self, f'in_wide_{master}', wide_interleaver, f'input')
        
    def i_NARROW_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in_narrow_{port}', signature='io')
    def i_WIDE_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in_wide_{port}', signature='io')


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

        soc_eu    = soc_eu_module.Soc_eu(self, 'soc_eu', ref_clock_event=soc_events['soc_evt_ref_clock'], **self.get_property('peripherals/soc_eu/config'))
        soc_ctrl  = apb_soc_ctrl.Apb_soc_ctrl(self, 'soc_ctrl', self)

        fll_soc     = Fll(self, 'fll_soc')
        fll_periph  = Fll(self, 'fll_periph')
        fll_cluster = Fll(self, 'fll_cluster')

        fc_events = self.get_property('peripherals/fc_itc/irq')
        fc_itc    = itc.Itc_v1(self, 'fc_itc')

        # Access memory and cluster configuration from the HJSON file
        self.memory_config  = config['memory_config']
        self.cluster_l1_config = config['cluster_l1_config']
        self.memory_island_config = config['memory_island_config']

        # Create loader using the binary provided
        loader      = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        obi_ico     = router.Router(self, "obi_ico")
        l2_tcdm_ico = router.Router(self, "l2_tcdm_ico")

        # Create the memory and router components based on the configuration
        cluster_l1_placeholder = memory.Memory(self, 'cluster_l1_placeholder', size=self.cluster_l1_config["size"])

        # Memory component placeholder for the memory island
        memory_island = MemoryIsland(self, 'memory_island', config=self.memory_island_config)

        l2_private_data_memory = memory.Memory(self, 'private_data_memory', size=self.memory_config["data"]["size"])
        l2_private_inst_memory = memory.Memory(self, 'private_inst_memory', size=self.memory_config["inst"]["size"])

        # Setup host and loader connections
        host = iss.FcCore(self, 'fc', cluster_id = 0)
        # host = iss.FcCore(self, 'fc', cluster_id = 31)

        
        host.o_FETCH      ( l2_tcdm_ico.i_INPUT() )
        host.o_DATA       ( l2_tcdm_ico.i_INPUT() )
        host.o_DATA_DEBUG ( l2_tcdm_ico.i_INPUT() )

        loader.o_OUT      ( l2_tcdm_ico.i_INPUT() )
        loader.o_START    ( host.i_FETCHEN()      )
        loader.o_ENTRY    ( host.i_ENTRY()        )

        # Mapping configurations for data, inst, and cluster memory
        l2_tcdm_ico.o_MAP( l2_private_data_memory.i_INPUT(),              "data_memory",            **self.get_property('memory_config/data'))
        l2_tcdm_ico.o_MAP( l2_private_inst_memory.i_INPUT(),              "inst_memory",            **self.get_property('memory_config/inst'))
        l2_tcdm_ico.o_MAP( cluster_l1_placeholder.i_INPUT(),              "cluster_l1_placeholder", **self.get_property('cluster_l1_config'))
        l2_tcdm_ico.o_MAP( obi_ico.i_INPUT(),                             "obi_ico",                **self.get_property('obi_ico/mapping'))
        l2_tcdm_ico.o_MAP( memory_island.i_NARROW_INPUT("l2_ico_master"), "memory_island",          **self.get_property('memory_island_config/mapping'))

        obi_ico.add_mapping( 'soc_ctrl'   , **self.get_property('obi_ico/soc_ctrl'   ))             
        obi_ico.add_mapping( 'fll_soc'    , **self.get_property('obi_ico/fll_soc'    ))             
        obi_ico.add_mapping( 'fll_periph' , **self.get_property('obi_ico/fll_periph' ))             
        obi_ico.add_mapping( 'fll_cluster', **self.get_property('obi_ico/fll_cluster'))             
        obi_ico.add_mapping( 'soc_eu'     , **self.get_property('obi_ico/soc_eu'     ))             
        obi_ico.add_mapping( 'fc_itc'     , **self.get_property('obi_ico/fc_itc'     ))             

        # Create and connect the SOC control component
        self.bind( obi_ico, 'soc_ctrl'   , soc_ctrl   , 'input' )
        self.bind( obi_ico, 'soc_eu'     , soc_eu     , 'input' )
        self.bind( obi_ico, 'fll_soc'    , fll_soc    , 'input' )
        self.bind( obi_ico, 'fll_periph' , fll_periph , 'input' )
        self.bind( obi_ico, 'fll_cluster', fll_cluster, 'input' )
        self.bind( obi_ico, 'fc_itc'     , fc_itc     , 'input' )

        self.bind( self       , 'ref_clock', fll_soc    , 'ref_clock'         )
        self.bind( self       , 'ref_clock', fll_periph , 'ref_clock'         )
        self.bind( self       , 'ref_clock', fll_cluster, 'ref_clock'         )
        self.bind( fll_soc    , 'clock_out', self       , 'fll_soc_clock'     )
        self.bind( fll_periph , 'clock_out', self       , 'fll_periph_clock'  )
        self.bind( fll_cluster, 'clock_out', self       , 'fll_cluster_clock' )

        # Interrupts
        for name, irq in fc_events.items():
            if len(name.split('.')) == 2:
                comp_name, itf_name = name.split('.')
                self.bind(self.get_component(comp_name), itf_name, fc_itc, 'in_event_%d' % irq)

        self.bind( self    , 'ref_clock'      , soc_eu, 'ref_clock')
        self.bind( soc_eu  , 'ref_clock_event', fc_itc, 'in_event_%d' % fc_events['evt_clkref'])
        self.bind( soc_ctrl, 'event'          , soc_eu, 'event_in')


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

    gapy_description="Chimera virtual board"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
            model=ChimeraBoard)
