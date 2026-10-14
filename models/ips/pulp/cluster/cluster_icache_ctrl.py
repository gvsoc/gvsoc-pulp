# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Control block of the hierarchical cluster instruction cache.

Register map (CL_ICACHE_CTRL, cluster peripherals + 0x1400), matching
``archi/chips/gap9_v2/cluster_icache_ctrl``:

  0x00  ENABLE                    per-bank enable mask, non-zero enables
  0x04  FLUSH                     flush the whole hierarchy
  0x08  L0_FLUSH                  flush the private L0 caches only
  0x0C  SEL_FLUSH                 flush the line holding the written address
  0x18  ENABLE_SPECIAL_CORE_CACHE give the master core its own cache
  0x1C  ENABLE_L1_L15_PREFETCH    L1/L1.5 prefetching

The last two are configuration bits of the real hierarchy that the model does
not distinguish, so they are stored and read back but have no effect. Enable
and the three flushes drive the caches.
"""

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class ClusterIcacheCtrlConfig(Config):
    nb_l0: int = cfg_field(default=9, dump=True, desc=(
        "Number of private L0 caches driven by this block"
    ))


class ClusterIcacheCtrl(Component):

    def __init__(self, parent: Component, name: str, config: ClusterIcacheCtrlConfig):

        super(ClusterIcacheCtrl, self).__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/cluster/cluster_icache_ctrl.cpp'])

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def o_ENABLE(self, itf: SlaveItf):
        self.itf_bind('enable', itf, signature='wire<bool>')

    def o_FLUSH(self, itf: SlaveItf):
        self.itf_bind('flush', itf, signature='wire<bool>')

    def o_L0_FLUSH(self, itf: SlaveItf):
        self.itf_bind('l0_flush', itf, signature='wire<bool>')

    def o_FLUSH_LINE(self, itf: SlaveItf):
        self.itf_bind('flush_line', itf, signature='wire<bool>')

    def o_FLUSH_LINE_ADDR(self, itf: SlaveItf):
        self.itf_bind('flush_line_addr', itf, signature='wire<uint32_t>')
