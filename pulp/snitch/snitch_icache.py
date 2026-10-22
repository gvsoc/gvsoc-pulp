# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Snitch instruction cache component.

Generator for ``snitch_icache.cpp``, the model of the two levels of the RTL
``snitch_icache`` (cluster_icache) used by the Snitch and Spatz clusters: the
per-core L0 (:class:`SnitchIcacheL0`) and the shared L1
(:class:`SnitchIcacheL1`). The configuration only carries the parameters of
the RTL; the behaviour of each level is fixed.
"""

from __future__ import annotations

from typing_extensions import override

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2SingleReq
from gvsoc.gui import Signal


class SnitchIcacheConfig(Config):
    """Configuration of one level of the Snitch instruction cache.

    The fields are the parameters of the RTL ``snitch_icache``.
    """

    level: int = cfg_field(default=0, dump=True, desc=(
        "0 for the per-core L0 (snitch_icache_l0), 1 for the shared L1 "
        "(snitch_icache_lookup and snitch_icache_handler)"))
    line_width: int = cfg_field(default=256, dump=True, desc=(
        "Line width in bits (LINE_WIDTH)"))
    line_count: int = cfg_field(default=8, dump=True, desc=(
        "L0: number of lines (L0_LINE_COUNT), the L0 being fully associative. "
        "L1: number of sets (LINE_COUNT)"))
    way_count: int = cfg_field(default=1, dump=True, desc=(
        "L1: number of ways (WAY_COUNT)"))
    nr_fetch_ports: int = cfg_field(default=1, dump=True, desc=(
        "L1: number of input ports, one per L0 (NR_FETCH_PORTS)"))
    num_axi_outstanding: int = cfg_field(default=2, dump=True, desc=(
        "L1: number of line refills in flight (NUM_AXI_OUTSTANDING). The L0 "
        "always has one"))
    cacheable_base: int = cfg_field(default=0, dump=True, desc=(
        "Base of the cacheable window (the PMA of the cluster). The other "
        "fetches are forwarded through the refill port without touching the "
        "cache"))
    cacheable_size: int = cfg_field(default=0, dump=True, desc=(
        "Size of the cacheable window, 0 for everything cacheable"))


class SnitchIcache(Component):
    """One level of the Snitch instruction cache, on the io_v2 protocol.

    Use :class:`SnitchIcacheL0` and :class:`SnitchIcacheL1`, which take the
    parameters of their level.

    L0 (``snitch_icache_l0``)
        One per core, fully associative, its lines replaced in turn, the line
        hitting skipped. It has one refill in flight, shared by the demand
        miss and the prefetch, and serves its hits during it. On a hit with no
        refill pending, its prefetcher fetches the line of the first
        statically-taken branch or JAL of the fetched line from the fetch
        position, else the next line, when it is not in the cache. It also
        acts on the address the core shows while it waits (a request of size
        0, which asks for nothing).

    L1 (``snitch_icache_lookup`` and ``snitch_icache_handler``)
        Shared by the L0s, one input port each (``input``, ``input_1``, ...).
        One lookup per cycle, granted round-robin among the ports and answered
        in its cycle. A miss takes one of ``num_axi_outstanding`` refill slots
        (merged with a miss on the same line) and the lookups go on; they only
        stall when every slot is busy. The way is chosen when the line lands,
        by the eviction LFSR of the RTL, and the line answers all the requests
        waiting for it at once.

    Both levels forward the fetches outside the cacheable window, and all of
    them while disabled (``ENABLE``), through ``REFILL`` without touching the
    cache.

    Parameters
    ----------
    parent : Component
        Parent component.
    name : str
        Local name of the cache within ``parent``.
    config : SnitchIcacheConfig
        Parameters of the level.
    """

    # Developer-manual doc registration. Discovered by AST scan at doc
    # build time; the class docstring above is rendered via autoclass.
    __gvsoc_doc__ = {
        'title': 'Snitch instruction cache',
    }

    def __init__(self, parent: Component, name: str, config: SnitchIcacheConfig):

        super(SnitchIcache, self).__init__(parent, name, config=config)
        self.add_sources(['pulp/snitch/snitch_icache.cpp'])

        # Geometry, for the GUI
        line_size = config.line_width // 8
        ways = config.way_count if config.level == 1 else config.line_count
        nb_sets = config.line_count if config.level == 1 else 1
        self.add_properties({
            'size': nb_sets * ways * line_size,
            'ways': ways,
            'line_size': line_size,
        })

    def i_INPUT(self, port: int=0) -> SlaveItf:
        """Returns an input slave port for fetch requests.

        Each request gets a single-beat response (inline DONE or GRANTED +
        one resp()), so the port speaks the single-req contract. The L1 has
        one per L0.
        """
        name = 'input' if port == 0 else f'input_{port}'
        return SlaveItf(self, name, signature=IoV2SingleReq())

    def i_FLUSH(self) -> SlaveItf:
        """Returns the flush slave port.

        A ``True`` pulse on this wire invalidates the entire cache and then
        pulses the ``FLUSH_ACK`` master.
        """
        return SlaveItf(self, 'flush', signature='wire<bool>')

    def i_FLUSH_LINE(self) -> SlaveItf:
        """Returns the flush-line slave port.

        A ``True`` pulse invalidates the single line whose address was
        previously latched via ``FLUSH_LINE_ADDR``.
        """
        return SlaveItf(self, 'flush_line', signature='wire<bool>')

    def i_FLUSH_LINE_ADDR(self) -> SlaveItf:
        """Returns the flush-line-address slave port.

        Latches the address used by the next ``FLUSH_LINE`` pulse.
        """
        return SlaveItf(self, 'flush_line_addr', signature='wire<uint32_t>')

    def i_ENABLE(self) -> SlaveItf:
        """Returns the enable slave port.

        Writing ``True`` enables the cache, ``False`` disables it. While
        disabled every incoming request is forwarded through the ``REFILL``
        port. The cache is enabled at reset.
        """
        return SlaveItf(self, 'enable', signature='wire<bool>')

    def o_FLUSH_ACK(self, itf: SlaveItf):
        """Binds the flush-ack master port.

        Pulsed with ``True`` once a full flush completes.
        """
        self.itf_bind('flush_ack', itf, signature='wire<bool>')

    def o_REFILL(self, itf: SlaveItf):
        """Binds the refill master port to the next level.

        The refill master routes responses by request identity and expects a
        single-beat response, so it is declared :class:`IoV2SingleReq`:
        against a beat slave (e.g. a ``router_v2`` beat crossbar) the
        framework auto-inserts the single-req-to-beat adapter.
        """
        self.itf_bind('refill', itf, signature=IoV2SingleReq())

    @override
    def gen_gui(self, parent_signal: Signal):
        """Generate GUI trace signals mirroring cache_v3."""
        cache = Signal(self, parent_signal, name=self.name, path='req_addr',
            groups=['cache', 'active'])
        _ = Signal(self, cache, name='refill', path='refill_addr', groups=['cache'])
        tags = Signal(self, cache, name='tags')

        ways = self.get_property('ways')
        size = self.get_property('size')
        line_size = self.get_property('line_size')

        for way in range(0, ways):
            set_trace = Signal(self, tags, name=f'set_{way}')
            for line in range(0, int(size / line_size / ways)):
                _ = Signal(self, set_trace, name=f'line_{line}', path=f'set_{way}/line_{line}',
                    groups=['cache'])


class SnitchIcacheL0(SnitchIcache):
    """Per-core L0 of the Snitch instruction cache (``snitch_icache_l0``).

    Parameters
    ----------
    parent : Component
        Parent component.
    name : str
        Local name of the cache within ``parent``.
    line_count : int
        Number of lines (L0_LINE_COUNT).
    line_width : int
        Line width in bits (LINE_WIDTH).
    cacheable_base, cacheable_size : int
        Cacheable window (PMA), size 0 for everything cacheable.
    """

    def __init__(self, parent: Component, name: str, line_count: int, line_width: int,
            cacheable_base: int=0, cacheable_size: int=0):
        super().__init__(parent, name, config=SnitchIcacheConfig(level=0,
            line_count=line_count, line_width=line_width,
            cacheable_base=cacheable_base, cacheable_size=cacheable_size))


class SnitchIcacheL1(SnitchIcache):
    """Shared L1 of the Snitch instruction cache (``snitch_icache_lookup`` and
    ``snitch_icache_handler``).

    Parameters
    ----------
    parent : Component
        Parent component.
    name : str
        Local name of the cache within ``parent``.
    line_count : int
        Number of sets (LINE_COUNT).
    way_count : int
        Number of ways (WAY_COUNT).
    line_width : int
        Line width in bits (LINE_WIDTH).
    nr_fetch_ports : int
        Number of input ports, one per L0 (NR_FETCH_PORTS).
    num_axi_outstanding : int
        Number of line refills in flight (NUM_AXI_OUTSTANDING).
    cacheable_base, cacheable_size : int
        Cacheable window (PMA), size 0 for everything cacheable.
    """

    def __init__(self, parent: Component, name: str, line_count: int, way_count: int,
            line_width: int, nr_fetch_ports: int, num_axi_outstanding: int,
            cacheable_base: int=0, cacheable_size: int=0):
        super().__init__(parent, name, config=SnitchIcacheConfig(level=1,
            line_count=line_count, way_count=way_count, line_width=line_width,
            nr_fetch_ports=nr_fetch_ports, num_axi_outstanding=num_axi_outstanding,
            cacheable_base=cacheable_base, cacheable_size=cacheable_size))
