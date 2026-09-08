# SPDX-License-Identifier: Apache-2.0
"""Execute gather followed by a legacy collective on the actual old Snitch core."""
import gvsoc.runner
import gvsoc.systree
from vp.clock_domain import Clock_domain
from pulp.chips.soft_hier_old.idma.snitch_dma import SnitchDma
from pulp.chips.soft_hier_old.snitch.snitch_core import Snitch


class Test(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, options=options)
        clock = Clock_domain(self, 'clock', frequency=1_000_000_000)
        driver = gvsoc.systree.Component(self, 'driver')
        driver.add_sources(['tests/sparse_dma/soft_hier_old_isa.cpp'])
        core = Snitch(self, 'core', isa='rv32imafd', fetch_enable=True, boot_addr=0x20000000)
        dma = SnitchDma(self, 'idma', loc_base=0x100000, loc_size=0x1000,
                        tcdm_width=64, gather_enable=True)
        for component in (core, driver, dma):
            self.bind(clock, 'out', component, 'clock')
        core.o_OFFLOAD(dma.i_OFFLOAD())
        dma.o_OFFLOAD_GRANT(core.i_OFFLOAD_GRANT())
        core.o_FETCH(gvsoc.systree.SlaveItf(driver, 'fetch', signature='io'))
        core.o_DATA(gvsoc.systree.SlaveItf(driver, 'data', signature='io'))
        dma.o_AXI(gvsoc.systree.SlaveItf(driver, 'axi', signature='io'))
        dma.o_TCDM(gvsoc.systree.SlaveItf(driver, 'tcdm', signature='io'))
        dma.o_INDEX(gvsoc.systree.SlaveItf(driver, 'index', signature='io'))


class Target(gvsoc.runner.Target):
    def __init__(self, parser, options):
        super().__init__(parser, options, model=Test,
                         description='SoftHier old Snitch gather and collective ISA smoke test')
