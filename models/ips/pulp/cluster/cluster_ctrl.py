# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync


class ClusterCtrlConfig(Config):
    nb_core: int = cfg_field(default=0, desc="Number of cores driven by the cluster control unit")


class ClusterCtrl(Component):
    """PULP cluster control unit (io_v2).

    Drives per-core boot address (i_ENTRY) and fetch-enable, exposes the
    debug halt/resume registers. Ported from pulp.cluster.cluster_ctrl_v2.
    """

    def __init__(self, parent: Component, name: str, config: ClusterCtrlConfig):
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/cluster/cluster_ctrl.cpp'])

        self.add_properties({
            'nb_core': config.nb_core,
        })

    def i_INPUT(self) -> SlaveItf:
        return SlaveItf(self, 'input', signature=IoV2Sync())

    def i_RESET(self) -> SlaveItf:
        return SlaveItf(self, 'pmu_reset', signature='wire<bool>')

    def o_BOOTADDR(self, core: int, itf: SlaveItf):
        self.itf_bind(f'bootaddr_{core}', itf, signature='wire<uint64_t>')

    def o_FETCHEN(self, core: int, itf: SlaveItf):
        self.itf_bind(f'fetchen_{core}', itf, signature='wire<bool>')

    def o_HALT(self, core: int, itf: SlaveItf):
        self.itf_bind(f'halt_{core}', itf, signature='wire<bool>')

    def i_HALT_STATUS(self, core: int) -> SlaveItf:
        return SlaveItf(self, f'core_halt_{core}', signature='wire<bool>')

    def o_HCI_CTRL(self, itf: SlaveItf):
        """HCI arbitration control towards the L1 crossbar.

        Carries the hci_ctrl field of the CLUSTER_CFG register (offset
        0x18): {arb_policy[10:9], hwpe_prio[8], low_prio_max_stall[7:0]}.
        Synced on every software write.
        """
        self.itf_bind('hci_ctrl', itf, signature='wire<uint32_t>')

    def o_CLOCK_GATING_EN(self, itf: SlaveItf):
        """Automatic clock-gating enable (CLUSTER_CLK_GATE register, offset
        0x20), towards the cluster composite power model. Synced on every
        software write."""
        self.itf_bind('clock_gating_en', itf, signature='wire<bool>')
