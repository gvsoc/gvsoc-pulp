# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#
# PULP cluster hierarchical instruction cache (io_v2).
#
# Mirrors the legacy gvsoc Hierarchical_cache wiring (per-core private L0
# caches feeding a shared L1.5) on the io_v2 protocol, using the io_v2
# cache_v4 model. The hardware (fe/ips/hier-icache) has, per cluster:
#   - 9 private L0 caches: 512 B, 4-way, 16-byte lines
#   - a shared L1.5 of 2 banks x 2 KB (4 KB total), 4-way, 16-byte lines
# The two shared banks are collapsed into a single 4 KB shared cache here
# (the banking is a refill-bandwidth optimisation; functionally one bank is
# equivalent and avoids needing an io_v2 interleaver). The shared-cache
# refill exits through ``o_REFILL`` to the off-cluster path (cluster code
# lives in L2).

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2SingleReq, IoV2Sync
from cache.cache_v4 import Cache, CacheConfig
from ips.pulp.cluster.cluster_icache_ctrl import ClusterIcacheCtrl, ClusterIcacheCtrlConfig
from interco.router_v2 import Router, RouterConfig, RouterMapping, KIND_UNTIMED


class ClusterIcacheConfig(Config):
    """Configuration for the PULP cluster hierarchical instruction cache."""

    nb_cores:       int = cfg_field(default=9,   desc="Number of cores (one private L0 cache each)")
    line_size:      int = cfg_field(default=16,  desc="Cache line size in bytes (refill granularity)")
    l0_size:        int = cfg_field(default=512, desc="Per-core private L0 cache size in bytes")
    l0_ways:        int = cfg_field(default=4,   desc="L0 associativity")
    l1_size:        int = cfg_field(default=4096, desc="Shared L1.5 cache size in bytes")
    l1_ways:        int = cfg_field(default=4,   desc="L1.5 associativity")
    refill_latency: int = cfg_field(default=2,   desc="Latency in cycles added to a synchronous refill")
    l1_max_refills: int = cfg_field(default=1, desc=(
        "Number of L1.5 refills in flight per refill bank (1: a miss blocks the L1.5)"))
    l1_refill_banks: int = cfg_field(default=1, desc=(
        "Number of L1.5 banks with their own refill tracker (used with l1_max_refills > 1)"))
    refill_free_way_first: bool = cfg_field(default=False, desc=(
        "True if a refill fills a free way of the set first (the hardware), False if it "
        "always evicts a pseudo-random way"))
    enabled:        bool = cfg_field(default=False, desc=(
        "True if the caches are enabled at reset. The hardware resets the "
        "enable register of hier_icache_ctrl_unit to 0, which bypasses every "
        "L0 and disables the L1.5, so the default is False and software has "
        "to write CL_ICACHE_CTRL.ENABLE (the PulpOS cluster driver does it at "
        "pi_cluster_open, before releasing fetch enable)."))


class ClusterIcache(Component):
    """Hierarchical cluster instruction cache: per-core L0 + shared L1.5.

    Each core fetch port binds to its own private L0 cache (``i_FETCH(i)``).
    L0 misses are merged through an untimed router into the shared L1.5
    cache; L1.5 misses exit through ``o_REFILL`` to the off-cluster memory
    path.

    Like the hardware, the caches boot BYPASSED: ``hier_icache_ctrl_unit``
    resets its enable register to 0, which raises ``L1_icache_bypass_req`` for
    every private L0 and leaves the L1.5 disabled, so until software writes
    ``CL_ICACHE_CTRL.ENABLE`` the cluster fetches every instruction line from
    the L2. The PulpOS cluster driver writes it at ``pi_cluster_open``, just
    before releasing fetch enable.
    """

    def __init__(self, parent: Component, name: str, config: ClusterIcacheConfig):
        super().__init__(parent, name, config=config)

        nb_cores = config.nb_cores

        # Private per-core L0 caches.
        self.l0 = [
            Cache(self, f'l0_core{i}', config=CacheConfig(
                size=config.l0_size, line_size=config.line_size, ways=config.l0_ways,
                refill_latency=config.refill_latency, enabled=config.enabled,
                refill_free_way_first=config.refill_free_way_first))
            for i in range(nb_cores)
        ]

        # Shared L1.5 cache (the two hardware banks collapsed into one).
        self.l1 = Cache(self, 'l1', config=CacheConfig(
            size=config.l1_size, line_size=config.line_size, ways=config.l1_ways,
            refill_latency=config.refill_latency, enabled=config.enabled,
            max_refills=config.l1_max_refills, refill_banks=config.l1_refill_banks,
            refill_free_way_first=config.refill_free_way_first))

        # Memory-mapped control block (CL_ICACHE_CTRL). Placed here so it can
        # drive the caches directly; reached from the cluster peripheral router
        # through the 'ctrl' boundary port.
        self.ctrl = ClusterIcacheCtrl(self, 'ctrl',
            config=ClusterIcacheCtrlConfig(nb_l0=nb_cores))

        # Untimed router merging the L0 refill ports into the shared L1 input.
        self.l0_to_l1 = Router(self, 'l0_to_l1', config=RouterConfig(kind=KIND_UNTIMED))

        for i in range(nb_cores):
            # Expose each core's fetch entry as a composite boundary port that
            # forwards to the private L0 cache input.
            self.bind(self, f'fetch_{i}', self.l0[i], 'input')
            self.l0[i].o_REFILL(self.l0_to_l1.i_INPUT(i))
        self.l0_to_l1.o_MAP_DEFAULT(self.l1.i_INPUT(), name='l1')

        # Control registers enter through the 'ctrl' composite boundary port.
        self.bind(self, 'ctrl_input', self.ctrl, 'input')

        # Control wires fan out to every cache: enable and the global flush hit
        # the whole hierarchy, L0_FLUSH only the private caches.
        for cache in self.l0 + [self.l1]:
            self.ctrl.o_ENABLE(cache.i_ENABLE())
            self.ctrl.o_FLUSH(cache.i_FLUSH())
            self.ctrl.o_FLUSH_LINE(cache.i_FLUSH_LINE())
            self.ctrl.o_FLUSH_LINE_ADDR(cache.i_FLUSH_LINE_ADDR())
        for cache in self.l0:
            self.ctrl.o_L0_FLUSH(cache.i_FLUSH())

        # Shared L1.5 refill exits through the composite 'refill' boundary port
        # (wired externally by o_REFILL).
        self.l1.o_REFILL(SlaveItf(self, 'refill', signature=IoV2SingleReq()))

    def i_CTRL(self) -> SlaveItf:
        """Return the slave interface of the memory-mapped control block."""
        return SlaveItf(self, 'ctrl_input', signature=IoV2Sync())

    def i_FETCH(self, core_id: int) -> SlaveItf:
        """Return the fetch slave interface for core ``core_id`` (its L0 input)."""
        return SlaveItf(self, f'fetch_{core_id}', signature=IoV2SingleReq())

    def o_REFILL(self, itf: SlaveItf):
        """Bind the shared L1.5 refill master to the off-cluster memory path.

        Single-req master: declaring IoV2SingleReq lets the framework insert
        the single-req-to-beat adapter when the refill path goes to a beat
        slave (the cluster soc_ico), so the cache never sees the beat stream.
        """
        self.itf_bind('refill', itf, signature=IoV2SingleReq())
