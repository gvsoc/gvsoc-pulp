# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Snitch instruction cache component.

Generator for ``snitch_icache.cpp``: the io_v2 set-associative cache used by
the Snitch/Spatz clusters as their per-core L0 and shared L1 instruction
caches. It models what the RTL ``snitch_icache`` does beyond a plain cache,
which is what separates it from the generic ``cache.cache_v4`` it was forked
from:

* **Miss-status holding registers** (:attr:`SnitchIcacheConfig.nb_refills`) --
  several line refills in flight at once, with a secondary miss on a line
  already on its way merged onto the refill that is fetching it. The RTL uses
  two, one for the demand miss and one for the prefetch.
* **Branch-target prefetching**
  (:attr:`SnitchIcacheConfig.prefetch_branch_target`) --
  ``snitch_icache_l0.sv`` aims its prefetch at the first statically-taken
  branch or JAL in the line just fetched rather than at the next line, which
  is what lets a loop whose body slightly exceeds the cache keep streaming.

The generic ``cache.cache_v4`` deliberately carries neither: it is shared with
gap9, el1 and voscap, none of which model a snitch_icache.
"""

from __future__ import annotations

from typing_extensions import override

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2SingleReq
from gvsoc.gui import Signal


class SnitchIcacheConfig(Config):
    """Configuration for the Snitch instruction cache component.

    Attributes
    ----------
    size : int
        Total cache size in bytes.
    line_size : int
        SnitchIcache line size in bytes.
    ways : int
        Number of ways (associativity).
    enabled : bool
        True if the cache is enabled at reset.
    refill_shift : int
        Shift applied to the address when issuing a refill request to the next
        memory level.
    refill_offset : int
        Offset added to the address when issuing a refill request to the next
        memory level.
    refill_latency : int
        Latency in cycles added to synchronous refill completions.
    """

    size: int = cfg_field(default=0, dump=True, desc=(
        "Total cache size in bytes"
    ))

    line_size: int = cfg_field(default=16, dump=True, desc=(
        "SnitchIcache line size in bytes"
    ))

    ways: int = cfg_field(default=1, dump=True, desc=(
        "Number of ways (associativity) of the cache"
    ))

    enabled: bool = cfg_field(default=True, dump=True, desc=(
        "True if the cache is enabled at reset"
    ))

    refill_shift: int = cfg_field(default=0, dump=True, desc=(
        "Shift applied to the address when issuing a refill request to the next memory level"
    ))

    refill_offset: int = cfg_field(default=0, dump=True, desc=(
        "Offset added to the address when issuing a refill request to the next memory level"
    ))

    refill_latency: int = cfg_field(default=0, dump=True, desc=(
        "Latency in cycles for a refill request to the next memory level"
    ))

    nb_refills: int = cfg_field(default=1, dump=True, desc=(
        "Number of miss-status holding registers, i.e. how many line refills "
        "can be in flight at once. With 1 (the default) a refill blocks every "
        "other miss and each line pays the full memory round trip; the RTL "
        "snitch_icache uses 2, one for the demand miss and one for the "
        "prefetch"))
    prefetch_branch_target: bool = cfg_field(default=False, dump=True, desc=(
        "Aim the prefetcher at the first statically-taken branch or JAL of "
        "each fetched line instead of always at the next line, the way the "
        "RTL snitch_icache L0 prefetcher does. Only meaningful on an "
        "instruction cache, and only together with prefetch"))
    prefetch: bool = cfg_field(default=False, dump=True, desc=(
        "Enable the sequential next-line prefetcher: a demand miss (or the "
        "first hit on a prefetched line) prefetches the following line "
        "through the refill port when it is otherwise idle, hiding the "
        "refill latency of streaming code (RTL snitch_icache prefetcher)"
    ))


class SnitchIcache(Component):
    """Set-associative cache on the io_v2 protocol.

    Overview
    ~~~~~~~~

    A functional, cycle-approximate set-associative cache sitting between
    a master that issues memory accesses (``INPUT``) and a downstream
    memory level that serves refills (``REFILL``). The cache observes
    requests, decides hit vs miss against its tag array, serves hits
    locally, and forwards misses as line-sized refill requests to the
    next memory level. Data for every line is stored verbatim inside the
    model (byte-accurate) so reads return the value that would flow
    through the real hardware. Replacement on full sets uses a
    pseudo-random 8-bit LFSR (deterministic across runs with a fixed seed
    pattern) — there is no LRU tracking.

    This is the io_v2 port of :class:`cache.cache_v3.SnitchIcache`. The
    functional scope (replacement policy, flush semantics, bypass mode)
    is identical; only the IO-side plumbing is new:

    - input and refill ports carry the ``io_v2`` signature
    - status is ``IO_REQ_DONE`` / ``IO_REQ_GRANTED`` / ``IO_REQ_DENIED``
    - error reporting is via ``req->set_resp_status(IO_RESP_INVALID)`` +
      ``IO_REQ_DONE``
    - replies travel back on the cache's own slave port (no v1
      ``resp_port`` indirection)

    Geometry
    ~~~~~~~~

    The three geometry knobs (:attr:`SnitchIcacheConfig.size`,
    :attr:`SnitchIcacheConfig.line_size`, :attr:`SnitchIcacheConfig.ways`) pin down
    the cache shape. The model derives:

    - ``nb_sets = size / ways / line_size``
    - address split into ``tag | set_index | line_offset`` using
      ``nb_sets_bits = ceil_log2(nb_sets)`` and
      ``line_size_bits = ceil_log2(line_size)``

    The configuration must satisfy ``size == nb_sets * ways * line_size``
    exactly — the cache rejects geometries where one of the three does
    not divide the others cleanly. ``line_size`` and ``nb_sets`` should
    be powers of two for the tag/offset split to work as expected.

    Request flow
    ~~~~~~~~~~~~

    - **Hit**: the cache reads/writes the line's byte buffer and returns
      ``IO_REQ_DONE`` inline. If the line has just been filled and its
      ``timestamp`` is still in the future (because an earlier miss in
      the same cycle started a multi-cycle refill), the hit is stalled
      via ``req->inc_latency(timestamp - now)`` so the master paces
      itself. Hits never stall an in-flight refill.
    - **Miss, no refill pending**: a refill request is sent to the
      ``REFILL`` port. A synchronous ``IO_REQ_DONE`` from the downstream
      yields an inline ``IO_REQ_DONE`` on the input (with
      :attr:`SnitchIcacheConfig.refill_latency` + any latency the downstream
      annotated on ``req->latency`` both added to the master's
      accumulated latency). A ``IO_REQ_GRANTED`` parks the CPU request
      in an internal FIFO; the master sees ``IO_REQ_GRANTED`` and later
      receives ``resp()`` once the refill lands.
    - **Miss, refill pending**: only one refill is ever in flight. A new
      miss arriving mid-refill is acked as ``IO_REQ_GRANTED`` and
      pushed to the FIFO. The internal ``fsm_handler`` drains the FIFO
      one request per cycle once the current refill resolves — the next
      miss in the FIFO may itself start a new refill, or be a hit if the
      landing refill happened to bring in its line.
    - **Bypass** (cache disabled): the CPU request is forwarded verbatim
      through ``REFILL`` after the address is rewritten by
      :attr:`SnitchIcacheConfig.refill_shift` / :attr:`SnitchIcacheConfig.refill_offset`.
      Both sync and async target responses are bounced back on the
      input slave port. No tag is allocated, no line is touched.
    - **Refill DENIED**: the cache propagates ``IO_REQ_DENIED`` to the
      master and remembers that it owes an ``input_itf.retry()`` once
      its own refill port retries. The master is expected to hold the
      request until retry.
    - **Error** (straddled access, unmapped address propagated by the
      refill target): a single ``IO_REQ_DONE`` with
      ``IO_RESP_INVALID`` is returned on the input.

    Address transformation
    ~~~~~~~~~~~~~~~~~~~~~~

    Refill requests (and bypass forwards) rewrite the address as
    ``(addr << refill_shift) + refill_offset``. Use this to:

    - Chain caches at different address widths (``refill_shift``).
    - Relocate the refill window into a different memory region
      (``refill_offset``) — for example, an I-cache whose refills
      actually originate from a flash controller mapped at a high
      address while the CPU fetches at a low "program" address.

    Timing model (big-packet)
    ~~~~~~~~~~~~~~~~~~~~~~~~~

    The cache is a big-packet io_v2 model: it reports latency on the
    logical transaction via ``req->inc_latency(n)`` rather than by
    scheduling beat-per-cycle events on the input. Accumulated latency
    on a hit or sync miss is ``base-hit-latency`` (0) + any refill
    latency + any latency the downstream already annotated. On an async
    miss, wall-clock time between ``req()`` and ``resp()`` is the
    timing signal. Either way, the cache itself never adds bandwidth
    shaping — pair it with a :class:`interco.limiter_v2.Limiter` on the
    refill path if throughput shaping is needed.

    Control wires
    ~~~~~~~~~~~~~

    - **ENABLE** (``i_ENABLE``) — runtime enable/disable. While
      disabled every incoming request takes the bypass path described
      above. Writing ``True`` re-enables; no flush is implied, so stale
      lines present before the disable are still hits after the
      re-enable unless :attr:`SnitchIcacheConfig.enabled` was ``False`` at
      reset (in which case the tag array never became valid).
    - **FLUSH** (``i_FLUSH``) — pulse to invalidate every line. The
      cache then pulses ``FLUSH_ACK`` to tell the driver the operation
      has completed. A flush is a single-cycle logical event in this
      model — no dirty writeback occurs because lines are not marked
      dirty (writes go straight through to the refilled line; there is
      no write-back buffer).
    - **FLUSH_LINE** / **FLUSH_LINE_ADDR** — latch a byte address via
      ``FLUSH_LINE_ADDR`` then pulse ``FLUSH_LINE`` to invalidate the
      single line containing that address. No ``FLUSH_ACK`` is pulsed
      for per-line flushes.

    Ports
    ~~~~~

    - **INPUT** (slave, ``io_v2``) — master-facing. Accepts reads and
      writes of any size up to ``line_size``.
    - **REFILL** (master, ``io_v2``) — downstream memory. Refills
      emit one request per miss at line granularity; in bypass mode,
      the CPU request is forwarded verbatim (size unchanged).
    - **ENABLE** (slave, ``wire<bool>``) — enable/disable control.
    - **FLUSH** (slave, ``wire<bool>``), **FLUSH_ACK** (master,
      ``wire<bool>``) — full-flush request/ack.
    - **FLUSH_LINE** (slave, ``wire<bool>``), **FLUSH_LINE_ADDR**
      (slave, ``wire<uint32_t>``) — line-flush request.

    Constraints and limitations
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~

    - **Single refill in flight.** A new miss arriving while a refill
      is already outstanding is queued but never overlapped. For
      workloads that expect multiple outstanding refills (MSHR-style
      caches), use a different model.
    - **No write-back.** Writes hit lines in place; there is no dirty
      bit and no eviction writeback traffic. A write that crosses a
      miss refills the line first, then mutates the refilled data.
    - **Requests must fit within a single line.** An access that
      straddles two lines (``offset + size > line_size`` across a set
      boundary) is rejected with ``IO_RESP_INVALID`` — the cache does
      not split an input request into multiple internal refills.
    - **Replacement is pseudo-random.** Set-level replacement uses an
      8-bit LFSR tick per replacement. There is no LRU, no way-locking,
      no explicit bias. With ``ways=1`` the LFSR is effectively
      constant so the cache becomes fully deterministic — useful for
      tests.
    - **Access size up to the bus width.** No internal serialisation of
      wide accesses: a 64-byte read served by a 32-byte line requires
      the caller to split.
    - **``FLUSH_LINE`` has no ack.** If your driver cares about flush
      completion, use the full ``FLUSH`` + ``FLUSH_ACK`` handshake
      instead.

    Configuration fields
    ~~~~~~~~~~~~~~~~~~~~

    All cache parameters live on :class:`SnitchIcacheConfig`. Each field is
    read once at construction; there is no runtime reconfiguration
    (except ``enabled`` which is mirrored onto the ``ENABLE`` wire).

    ``size``
        Total cache storage, in bytes. Must equal
        ``ways * line_size * nb_sets`` with ``nb_sets`` a power of two.
        Typical values: 2 KiB – 32 KiB. No default — must be set.

    ``line_size``
        SnitchIcache line size, in bytes. Power of two. Sets the granularity
        of refills: a miss on any byte within a line fetches exactly
        ``line_size`` bytes from ``REFILL``. Default: ``16``.

    ``ways``
        Associativity. ``1`` is direct-mapped, higher values are set
        associative with pseudo-random replacement. Default: ``1``.

    ``enabled``
        Initial state of the ``ENABLE`` wire. ``True`` makes the cache
        active from reset; ``False`` boots in bypass mode (every
        request forwarded to ``REFILL``). Default: ``True``.

    ``refill_shift``
        Left-shift applied to the address before the refill or bypass
        forward (``addr << refill_shift``). Default: ``0``.

    ``refill_offset``
        Offset added to the shifted address. The full transformation
        is ``(addr << refill_shift) + refill_offset``. Default: ``0``.

    ``refill_latency``
        Extra cycles stacked onto every synchronous refill completion.
        Added to ``req->latency`` on the inline ``IO_REQ_DONE`` path.
        Does not apply to async refills (the wall-clock time at which
        the downstream's ``resp()`` fires is already the signal).
        Default: ``0``.

    Parameters
    ----------
    parent : Component
        Parent component this cache is instantiated under.
    name : str
        Local name of the cache within ``parent``. Used for component
        path, traces, and as the lookup key for bindings.
    config : SnitchIcacheConfig
        All cache parameters live here. The instance is taken over by
        the cache — pass a fresh :class:`SnitchIcacheConfig` per instance.
    """

    # Developer-manual doc registration. Discovered by AST scan at doc
    # build time; the class docstring above is rendered via autoclass.
    __gvsoc_doc__ = {
        'title': 'Snitch instruction cache',
    }

    def __init__(self, parent: Component, name: str, config: SnitchIcacheConfig):

        super(SnitchIcache, self).__init__(parent, name, config=config)
        self.add_sources(['pulp/snitch/snitch_icache.cpp'])

        self.add_properties({
            'size': config.size,
            'ways': config.ways,
            'line_size': config.line_size,
            'refill_latency': config.refill_latency,
            'nb_refills': config.nb_refills,
            'prefetch_branch_target': config.prefetch_branch_target,
            'refill_offset': config.refill_offset,
            'refill_shift': config.refill_shift,
            'enabled': config.enabled,
        })

    def i_INPUT(self) -> SlaveItf:
        """Returns the input slave port for memory access requests.

        Incoming io_v2 requests land here. Hits are served inline; misses
        trigger a refill via the ``REFILL`` master port. Each request gets a
        single-beat response (inline DONE on a hit, GRANTED + one resp() on
        a miss), so the port speaks the single-req contract.
        """
        return SlaveItf(self, 'input', signature=IoV2SingleReq())

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
        disabled every incoming request is forwarded verbatim through the
        ``REFILL`` port (address transformed by ``refill_shift`` /
        ``refill_offset``).
        """
        return SlaveItf(self, 'enable', signature='wire<bool>')

    def o_FLUSH_ACK(self, itf: SlaveItf):
        """Binds the flush-ack master port.

        Pulsed with ``True`` once a full flush completes.
        """
        self.itf_bind('flush_ack', itf, signature='wire<bool>')

    def o_REFILL(self, itf: SlaveItf):
        """Binds the refill master port to the next memory level.

        The address sent downstream is transformed according to the
        configured ``refill_shift`` and ``refill_offset``.

        The refill master routes responses by request identity and keeps a
        single refill in flight, expecting a single-beat response — so it is
        declared :class:`IoV2SingleReq`: against a beat slave (e.g. a
        ``router_v2`` beat crossbar) the framework auto-inserts the
        single-req-to-beat adapter instead of letting the per-beat response
        stream reach the cache directly.
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
