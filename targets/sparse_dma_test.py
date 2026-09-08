# SPDX-License-Identifier: Apache-2.0
import gvsoc.runner
import gvsoc.systree
from vp.clock_domain import Clock_domain
from pulp.idma.snitch_dma import SnitchDma


class Test(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, options=options)
        clock = Clock_domain(self, 'clock', frequency=1_000_000_000)
        driver = gvsoc.systree.Component(self, 'driver')
        driver.add_sources(['tests/sparse_dma/model.cpp'])
        dma = SnitchDma(self, 'idma', transfer_queue_size=2, burst_queue_size=2,
                        loc_base=0x100000, loc_size=0x20000, tcdm_width=64,
                        gather_enable=True)
        self.bind(clock, 'out', driver, 'clock')
        self.bind(clock, 'out', dma, 'clock')
        self.bind(driver, 'offload', dma, 'offload')
        self.bind(dma, 'offload_grant', driver, 'grant')
        for port in ('axi_read', 'axi_write'):
            self.bind(dma, port, driver, 'axi')
        for port in ('tcdm_read', 'tcdm_write'):
            self.bind(dma, port, driver, 'tcdm')
        self.bind(dma, 'index', driver, 'index')


class Target(gvsoc.runner.Target):
    def __init__(self, parser, options):
        super().__init__(parser, options, model=Test,
                         description='Sparse iDMA functional and backpressure checks')
