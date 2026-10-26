# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
# SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com), spatz_tcdm_interco
#          Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it), MAGIA version

"""TCDM interconnect of the MAGIA tile.

Generator for ``tcdm_interco.cpp``, the model of the tile
``local_interconnect`` (an ``hci_interconnect``). Derived from the Spatz TCDM
interconnect (``pulp.snitch.snitch_cluster.spatz.spatz_tcdm_interco``), with
the wide side of the HCI: every wide master has its own branch to the banks.
"""

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync, IoV2SingleReq


class MagiaTcdmIntercoConfig(Config):
    """Configuration of the MAGIA TCDM interconnect."""

    nb_masters: int = cfg_field(default=0, dump=True, desc=(
        "Number of narrow master input ports (log branch of the HCI)"
    ))
    nb_slaves: int = cfg_field(default=0, dump=True, desc=(
        "Number of slave output ports (banks)"
    ))
    interleaving_width: int = cfg_field(default=0, dump=True, desc=(
        "Number of low-order address bits kept local to a bank (granule size = 1 << interleaving_width)"
    ))
    nb_wide_masters: int = cfg_field(default=0, dump=True, desc=(
        "Number of wide master input ports (DMA and HWPE branches of the HCI). "
        "A wide access spans several consecutive banks served in the same "
        "cycle, with priority over the narrow masters"
    ))


class MagiaTcdmInterco(Component):
    """Bank-interleaved crossbar of the MAGIA tile TCDM, on io_v2.

    Narrow masters (the log branch of the HCI: the cores through the OBI x-bar
    and the Spatz VLSU ports) arbitrate round-robin, one master per bank per
    cycle. A narrow request is denied on arrival and the master is retried by
    the arbiter, then re-issues and completes inline.

    Wide masters (the DMA and HWPE branches: the NoC wide channel, the two
    iDMA channels and RedMulE) access all the banks their access spans in the
    same cycle and take priority over the narrow masters. Each wide master has
    its own branch, so two wide accesses go in the same cycle unless they need
    the same bank. This is the difference with the Spatz version, where all
    the wide masters share one port carrying one access per cycle.

    The address is decoded as in the Spatz version: the bank is
    ``(addr >> interleaving_width) & (nb_slaves - 1)`` and the bank receives
    the bank-local offset. The banks must be :class:`IoV2Sync` slaves, such as
    ``memory.memory_v3.Memory``.

    Parameters
    ----------
    parent : Component
        Parent component.
    name : str
        Local name of the crossbar.
    config : MagiaTcdmIntercoConfig
        Crossbar parameters.
    """

    def __init__(self, parent: Component, name: str, config: MagiaTcdmIntercoConfig):
        super().__init__(parent, name, config=config)
        self.add_sources(['pulp/chips/magia_v3/tcdm_interco/tcdm_interco.cpp'])
        self.nb_slaves = config.nb_slaves

    def gen_gui(self, parent_signal):
        """Surface the per-bank accesses served by the crossbar in the GUI."""
        import gvsoc.gui
        top = gvsoc.gui.Signal(self, parent_signal, name=self.name,
            path="active", groups=['regmap', 'active'],
            display=gvsoc.gui.DisplayLogicBox('ACTIVE'))
        for i in range(self.nb_slaves):
            gvsoc.gui.Signal(self, top, f"output_{i}", path=f"output_{i}/addr",
                groups=['regmap', 'active'])

    def i_WIDE_INPUT(self, id: int) -> SlaveItf:
        """Slave port ``wide_input_<id>`` of wide master ``id``.

        Atomics are not supported on this path.
        """
        return SlaveItf(self, f'wide_input_{id}', signature=IoV2SingleReq())

    def i_INPUT(self, id: int) -> SlaveItf:
        """Slave port ``input_<id>`` of narrow master ``id``.

        The master must implement the DENY/retry handshake and re-issue
        inside its retry callback.
        """
        return SlaveItf(self, f'input_{id}', signature=IoV2SingleReq())

    def o_OUTPUT(self, id: int, itf: SlaveItf):
        """Binds bank ``output_<id>`` to ``itf``, an :class:`IoV2Sync` slave."""
        self.itf_bind(f'output_{id}', itf, signature=IoV2Sync())
