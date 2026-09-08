# SPDX-License-Identifier: Apache-2.0
"""Compatibility and sparse-gather checks for the private SoftHier iDMA."""
import gvsoc.runner
import gvsoc.systree
from vp.clock_domain import Clock_domain
from pulp.chips.soft_hier_old.idma.snitch_dma import SnitchDma


class Test(gvsoc.systree.Component):
    def __init__(self, parent, name, parser, options):
        super().__init__(parent, name, options=options)
        parser.add_argument('--test-mode', default='legacy',
                            choices=('legacy', 'legacy-enabled', 'gather-sync', 'gather-async'))
        args, _ = parser.parse_known_args()
        enabled = args.test_mode != 'legacy'
        clock = Clock_domain(self, 'clock', frequency=1_000_000_000)
        driver = gvsoc.systree.Component(self, 'driver')
        driver.add_sources(['tests/sparse_dma/soft_hier_old.cpp'])
        driver.add_property('mode', args.test_mode)
        # Match the existing SoftHier architecture's burst capacity; keep the
        # descriptor queue small to exercise gather offload backpressure.
        dma = SnitchDma(self, 'idma', transfer_queue_size=2, burst_queue_size=256,
                        loc_base=0x100000, loc_size=0x20000, tcdm_width=64)
        # The same target can measure the pre-extension model in legacy mode.
        dma.add_property('gather_enable', enabled)
        self.bind(clock, 'out', driver, 'clock')
        self.bind(clock, 'out', dma, 'clock')
        self.bind(driver, 'offload', dma, 'offload')
        self.bind(dma, 'offload_grant', driver, 'grant')
        for port in ('axi_read', 'axi_write'):
            self.bind(dma, port, driver, 'axi')
        for port in ('tcdm_read', 'tcdm_write'):
            self.bind(dma, port, driver, 'tcdm')
        if enabled:
            self.bind(dma, 'index', driver, 'index')


class Target(gvsoc.runner.Target):
    def __init__(self, parser, options):
        super().__init__(parser, options, model=Test,
                         description='SoftHier old iDMA compatibility and gather regression')
