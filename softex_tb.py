import gvsoc.runner
import cpu.iss.riscv as iss
import memory.memory as memory
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gvsoc.systree as st
from elftools.elf.elffile import *
from pulp.softex.softex import Softex
from pulp.stdout.stdout_v3 import Stdout
from pulp.chips.magia_v2.kill_module.kill_module import KillModule

class SoftexTestbench(st.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary

        imemory = memory.Memory(self, 'imem', size=0x10000, atomics=True)
        dmemory = memory.Memory(self, 'dmem', size=0x10000, atomics=True)
        stackmemory = memory.Memory(self, 'stackmem', size=0x30000, atomics=True)

        ico = router.Router(self, 'ico')
        host = iss.Riscv(self, 'host', isa='rv32imc', timed=True)
        softex = Softex(self, 'softex')
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
        stdout = Stdout(self, 'stdout')

        # Simulation engine killer
        killer=KillModule(self,'kill-module',kill_addr_base=0x80000000,kill_addr_size=0x400,nb_cores_to_wait=1, done_irq_enable=False)

        ico.o_MAP(stdout.i_INPUT(), base=0xFFFF0004, size=0x4)
        ico.o_MAP(softex.i_INPUT(), base=0x00100000, size=0x10000)
        ico.o_MAP(imemory.i_INPUT(), base=0x1c000000, size=0x8000)
        ico.o_MAP(dmemory.i_INPUT(), base=0x1c010000, size=0x30000)
        ico.o_MAP(dmemory.i_INPUT(), name='dmem_alias', base=0x00010000, size=0x30000)
        ico.o_MAP(stackmemory.i_INPUT(), base=0x1c040000, size=0x30000)

        ico.o_MAP(killer.i_INPUT(), base=0x80000000, size=0x400, rm_base=False)

        loader.o_OUT(ico.i_INPUT())
        loader.o_START(host.i_FETCHEN())
        loader.o_ENTRY(host.i_ENTRY())
        host.o_DATA(ico.i_INPUT())
        host.o_FETCH(ico.i_INPUT())
        softex.o_OUT(ico.i_INPUT())
        softex.o_IRQ(host.i_IRQ(3))

class SoftexTestbenchWrapper(st.Component):

    def __init__(self, parent, name, parser, options):

        super(SoftexTestbenchWrapper, self).__init__(parent, name, options=options)

        clock = Clock_domain(self, 'clock', frequency=1000000000)

        soc = SoftexTestbench(self, 'soc', parser)

        self.bind(clock, 'out', soc, 'clock')

class Target(gvsoc.runner.Target):

    gapy_description = "Softex testbench"

    def __init__(self, parser, options):
        super(Target, self).__init__(parser, options,
                                     model=SoftexTestbenchWrapper)