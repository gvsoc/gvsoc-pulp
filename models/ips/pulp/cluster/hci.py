# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync
from gvsoc.signature import IoV2SingleReq


class HciConfig(Config):
    nb_masters:         int = cfg_field(default=0, desc="Number of narrow master input ports")
    nb_slaves:          int = cfg_field(default=0, desc="Number of slave output ports (banks)")
    interleaving_width: int = cfg_field(default=0, desc="Number of low-order address bits kept local to a bank (granule size = 1 << interleaving_width)")
    nb_wide_masters:    int = cfg_field(default=0, desc="Number of wide (HWPE) input ports")
    wide_max_stall:     int = cfg_field(default=0, desc="Consecutive conflict cycles after which the low-priority side gets one exclusive cycle (0 disables, HW low_prio_max_stall)")
    wide_prio:          bool = cfg_field(default=False, desc="Give the wide side priority over the narrow side (HW hwpe_prio)")
    nb_group0_masters:  int = cfg_field(default=0, desc="Narrow masters [0, n) form the first arbitration group and the rest the second (HW XBAR_TCDM CH0 cores / CH1 DMA+ext: round-robin inside each group, then a per-bank 2:1 round-robin between the groups); 0 keeps one flat round-robin over all narrow masters")


class Hci(Component):
    """PULP cluster L1 heterogeneous interconnect (io_v2).

    The log_ico_v2 crossbar (narrow masters -> banks, per-bank round-robin,
    optionally two-level with ``nb_group0_masters`` as on the HW XBAR_TCDM,
    zero-buffer deny/retry) extended with the HCI wide (HWPE) ports of the
    real hardware: one request = one wide transaction spanning consecutive
    granules, all-or-nothing grant, low priority against the narrow side
    with a ``wide_max_stall`` starvation counter and a ``wide_prio``
    inversion (see ``hci.cpp`` for the detailed semantics).

    Narrow inputs and bank outputs behave exactly as ``interco.log_ico_v2``:
    banks must be IoV2Sync (inline DONE, e.g. ``memory.memory_v3``); narrow
    masters must implement the synchronous deny/retry handshake. Wide
    masters hold their denied request and re-issue it synchronously inside
    their retry() callback; the data is moved bank-by-bank at grant time and
    the request completes inline DONE with the max bank latency annotated.
    """

    def __init__(self, parent: Component, name: str, config: HciConfig):
        super().__init__(parent, name, config=config)

        self.add_sources(['ips/pulp/cluster/hci.cpp'])

        # GUI labels of the master ports (see gen_gui_metrics); the enclosing
        # cluster overrides them with the names of the masters it wired.
        self.gui_master_names = [f'input_{i}' for i in range(config.nb_masters)]
        self.gui_wide_names = [f'wide_{i}' for i in range(config.nb_wide_masters)]
        # Set when another component (typically the TCDM group) embeds the
        # metrics in its own GUI node, so the default gen_gui adds nothing.
        self.gui_embedded = False

        self.nb_slaves = config.nb_slaves

        self.add_properties({
            'nb_masters': config.nb_masters,
            'nb_slaves': config.nb_slaves,
            'interleaving_width': config.interleaving_width,
            'nb_wide_masters': config.nb_wide_masters,
            'wide_max_stall': config.wide_max_stall,
            'wide_prio': config.wide_prio,
            'nb_group0_masters': config.nb_group0_masters,
        })

    # -------- ports --------

    def i_INPUT(self, id: int) -> SlaveItf:
        """Narrow master input ``input_<id>`` (cores / DMA word ports)."""
        return SlaveItf(self, f'input_{id}', signature=IoV2SingleReq())

    def i_CTRL(self) -> SlaveItf:
        """Runtime arbitration control (cluster_ctrl hci_ctrl field).

        On sync, bits [7:0] override ``wide_max_stall`` and bit [8]
        overrides ``wide_prio`` (arb_policy bits [10:9] are ignored). The
        construction-time values act as the defaults and are restored on
        reset.
        """
        return SlaveItf(self, 'ctrl', signature='wire<uint32_t>')

    def i_WIDE(self, id: int) -> SlaveItf:
        """Wide (HWPE) master input ``wide_input_<id>``.

        One request = one wide transaction covering a contiguous byte range
        of up to ``nb_slaves`` granules. All-or-nothing grant against the
        narrow side per the HCI shallow-mux priority scheme.
        """
        return SlaveItf(self, f'wide_input_{id}', signature=IoV2SingleReq())

    def o_OUTPUT(self, id: int, itf: SlaveItf):
        """Binds bank ``output_<id>`` to ``itf`` (IoV2Sync, inline DONE)."""
        self.itf_bind(f'output_{id}', itf, signature=IoV2Sync())

    # -------- GUI --------

    def gui_set_master_names(self, master_names=None, wide_names=None):
        """Names shown in the GUI for the narrow and wide master ports."""
        if master_names is not None:
            self.gui_master_names = list(master_names)
        if wide_names is not None:
            self.gui_wide_names = list(wide_names)

    def gen_gui_metrics(self, parent_signal):
        """Per-cycle arbitration metrics under ``parent_signal``.

        ``denied``: one decimal lane per bank with the number of requests
        that wanted the bank this cycle and were not served (high-Z when
        none); its area average column shows the denied requests per cycle
        (switch it to Sum in the GUI for the total over the selection). ``masters``: one lane per master port showing GRANTED (green)
        when its access went through this cycle and DENIED (red) when it
        requested and lost the election, with two child pulses (``granted``,
        ``denied``) whose time average is the rate of each outcome.
        """
        import gvsoc.gui

        denied = gvsoc.gui.Signal(self, parent_signal, name='denied')
        for i in range(self.nb_slaves):
            gvsoc.gui.Signal(self, denied, name=f'bank{i}', path=f'output_{i}/denied',
                groups=['tcdm'],
                display=gvsoc.gui.DisplayBox(format='dec', aggregation='average'))

        access_display = gvsoc.gui.DisplayStateBox({
            1: ('GRANTED', 'green'),
            2: ('DENIED', 'red'),
        })
        masters = gvsoc.gui.Signal(self, parent_signal, name='masters')
        ports = [(name, f'input_{i}') for i, name in enumerate(self.gui_master_names)]
        ports += [(name, f'wide_input_{i}') for i, name in enumerate(self.gui_wide_names)]
        for name, port in ports:
            master = gvsoc.gui.Signal(self, masters, name=name, path=f'{port}/access',
                groups=['tcdm'], display=access_display)
            gvsoc.gui.Signal(self, master, name='granted', path=f'{port}/granted',
                groups=['tcdm'], display=gvsoc.gui.DisplayPulse())
            gvsoc.gui.Signal(self, master, name='denied', path=f'{port}/denied',
                groups=['tcdm'], display=gvsoc.gui.DisplayPulse())

        return parent_signal

    def gen_gui(self, parent_signal):
        if self.gui_embedded:
            return parent_signal
        import gvsoc.gui
        top = gvsoc.gui.Signal(self, parent_signal, name=self.name)
        return self.gen_gui_metrics(top)
