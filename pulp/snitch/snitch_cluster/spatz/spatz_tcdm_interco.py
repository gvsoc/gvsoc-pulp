# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Spatz TCDM interconnect component.

Generator for ``spatz_tcdm_interco.cpp``: a bank-interleaved io_v2
crossbar with round-robin narrow arbitration plus wide (DMA) inputs that
preempt the narrow masters with unconditional priority. It folds the RTL
spatz cluster's narrow ``spatz_tcdm_interconnect`` and the per-superbank
``mem_wide_narrow_mux`` into one model. Forked from the generic
``interco.log_ico_v2`` crossbar (which stays wide-agnostic); the
wide-input handling here is spatz-specific.
"""

from __future__ import annotations

from config_tree import Config, cfg_field
from gvsoc.systree import Component, SlaveItf
from gvsoc.signature import IoV2Sync, IoV2SingleReq


class SpatzTcdmIntercoConfig(Config):
    """Configuration for the io_v2 logarithmic crossbar component.

    Attributes
    ----------
    nb_masters : int
        Number of input master ports exposed by the crossbar. Each
        master can concurrently address any of the ``nb_slaves`` banks.
    nb_slaves : int
        Number of output bank ports. Address space is striped across
        these banks at :attr:`interleaving_width`-byte granularity.
    interleaving_width : int
        Number of low-order address bits defining the granule that
        stays within one bank before the traffic rolls over to the next
        one. Expressed in *bit positions*, not bytes: a value of
        ``4`` means 16-byte (``1 << 4``) granules.
    """

    nb_masters: int = cfg_field(default=0, dump=True, desc=(
        "Number of master input ports"
    ))
    nb_slaves: int = cfg_field(default=0, dump=True, desc=(
        "Number of slave output ports (banks)"
    ))
    interleaving_width: int = cfg_field(default=0, dump=True, desc=(
        "Number of low-order address bits kept local to a bank (granule size = 1 << interleaving_width)"
    ))
    nb_wide_masters: int = cfg_field(default=0, dump=True, desc=(
        "Number of wide master input ports. A wide access spans several "
        "consecutive banks served within the same arbiter tick, with "
        "priority over the narrow masters (RTL superbank mux behaviour)"
    ))
    wide_shared_port: bool = cfg_field(default=True, dump=True, desc=(
        "True if all the wide masters reach the banks through one port, which "
        "carries one access per cycle (the 512-bit port of the spatz cluster). "
        "False if each wide master has its own path and only conflicts on banks "
        "(the HWPE and DMA branches of a HCI interconnect)"
    ))



class SpatzTcdmInterco(Component):
    """Logarithmic (bank-interleaved) crossbar on the io_v2 protocol.

    Overview
    ~~~~~~~~

    An M-to-N crossbar that spreads a contiguous address range across
    ``nb_slaves`` banks at a fixed interleaving granularity, and lets
    up to ``nb_masters`` masters issue requests into that range. It is a
    **timed, round-robin arbiter**, not a zero-latency shuffle:

    - **Input side (async ``io_v2``).** A request arriving in the
      idle state is ``DENIED``; the crossbar records *which* input
      wants *which* bank in one bit of ``banks[bank_id].pending_mask``
      and arms a ``ClockEvent`` for the same cycle. The fsm raises an
      ``in_election`` flag, round-robin picks one winner per bank from
      the pending bitmask and calls ``retry()`` on it. Any request
      landing on the input port while the flag is raised (typically
      the retried master's synchronous re-issue) is forwarded inline
      to its bank instead of being denied. Two masters contending for
      the same bank are serialised across cycles. No request pointer
      is ever stored.
    - **Output side (:class:`IoV2Sync`).** The bank answers every request
      inline with ``IO_REQ_DONE`` and never uses the async ``resp()`` /
      ``retry()`` channel, so the inline forward returns DONE to the
      master in the same call — **no in-flight tracking, no async
      routing, no bank-stall handling**. Binds only to a sync slave such
      as ``memory.memory_v3``.

    Address decoding
    ~~~~~~~~~~~~~~~~

    The crossbar expects the input address to already be relative to
    its own region — the upstream router (or a per-master remapper)
    must strip any base before the request reaches the SpatzTcdmInterco. Given
    such an address ``addr``, the crossbar computes:

    .. code-block:: text

        slave_bits  = ceil_log2(nb_slaves)
        bank_id     = (addr >> interleaving_width) & (nb_slaves - 1)
        bank_offset = ((addr >> (slave_bits + interleaving_width)) << interleaving_width)
                      | (addr & ((1 << interleaving_width) - 1))

    The address delivered to the bank is ``bank_offset`` — the bank
    sees a compacted local address space starting at 0. Concretely,
    bits in the address play these roles:

    .. code-block:: text

        | high bits ... | bank_id (slave_bits) | granule bits (interleaving_width) |
                 |_________|     selector     |__________________________________|
                     |                                        |
                     +---- moved down, shifted left by        +---- kept in place, becomes
                           interleaving_width, into the             the low part of the bank
                           bank_offset high part                    local address

    Worked example
    ^^^^^^^^^^^^^^

    ``nb_slaves = 4`` (``slave_bits = 2``), ``interleaving_width = 4``
    (16-byte granules). Address ``0x0A34`` (already region-relative):

    - binary: ``0000_1010_0011_0100``
    - granule bits [3:0] = ``0100`` → low 4 bits of bank-local addr
    - bank selector [5:4] = ``11`` → bank ``3``
    - high bits [11:6] = ``101000`` → bank-local high part
    - bank-local address = ``(101000 << 4) | 0100`` = ``0x284``

    So byte ``0x0A34`` of the global range is served by bank ``3`` at
    its local offset ``0x284``.

    Memory map properties:

    - Bytes ``0`` .. ``(1 << interleaving_width) - 1`` all go to
      bank ``0`` at local addresses ``0`` .. ``(1 << interleaving_width) - 1``.
    - The next ``(1 << interleaving_width)`` bytes go to bank ``1`` at
      the same local addresses, and so on.
    - After ``nb_slaves`` granules the cycle repeats, incremented by
      one granule in each bank's local address space.

    Request flow
    ~~~~~~~~~~~~

    - **Incoming request** (``input_req``): decode the bank from the
      address. If ``in_election`` is set (the fsm is currently
      dispatching retries), forward inline to the bank and return
      ``IO_REQ_DONE``. Otherwise set bit ``id`` in
      ``banks[bank_id].pending_mask``, arm the fsm with zero delay,
      and return ``DENIED``. The crossbar never holds onto the request
      pointer — the master is expected to re-issue when retried.
    - **Arbiter tick** (``fsm_handler``): raise the ``in_election``
      flag; for each bank with a non-empty mask, find the first set
      bit at-or-after the bank's round-robin cursor (rotate +
      ``ctz``), clear it, and call ``retry()`` on the winner. The
      master's retry handler synchronously re-issues; the re-issue
      hits ``input_req`` with the flag still raised and is forwarded
      inline to the (IoV2Sync) bank for an inline ``IO_REQ_DONE``.
      After the loop the flag drops; the fsm re-arms for the next
      cycle if any bank still has pending bits, so each bank serves
      at most one master per cycle.

    Because the output is synchronous there is no GRANTED/DENIED/retry
    path from the bank, and because the input always defers via the
    retry handshake the crossbar never owns an in-flight request.

    Timing model
    ~~~~~~~~~~~~

    The crossbar arbitrates **one master per bank per cycle**. Cross-bank
    requests proceed in parallel within a tick; same-bank requests
    serialise across ticks. The crossbar adds no latency of its own
    beyond this arbitration throughput; any per-access cost (fixed
    latency, bandwidth) is modelled by the bank and surfaced via the
    inline ``req->latency`` annotation the master reads on response.

    Ports
    ~~~~~

    - **input_0 .. input_{M-1}** (slave, ``io_v2``, muxed) — one per
      master. ``M = nb_masters``. Async contract: every fresh request
      is ``DENIED``, and the master is later ``retry()``-ed by the
      arbiter — re-issuing then completes inline as ``DONE``.
    - **output_0 .. output_{N-1}** (master, :class:`IoV2Sync`, muxed) —
      one per bank. ``N = nb_slaves``. Each output receives requests
      whose bank selector bits equal its index, with the address
      rewritten to bank-local form, and must answer inline.

    The port methods :meth:`i_INPUT` and :meth:`o_OUTPUT` take an
    ``id`` argument to name the specific port.

    Constraints and limitations
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~

    - **``nb_masters`` is capped at 64.** ``pending_mask`` is a
      ``uint64_t``; building a SpatzTcdmInterco with more masters trips a
      vp_assert at construction.
    - **``nb_slaves`` should be a power of two for clean decode.** The
      decode uses ``(1 << slave_bits) - 1`` as the bank-id mask; with
      non-power-of-two ``nb_slaves``, a subset of bank-id values
      would map onto valid banks while the rest would index out of
      range. No runtime check is performed — it is the caller's
      responsibility.
    - **``interleaving_width`` must be non-negative.** A value of 0
      degenerates to *byte-level* interleaving — every byte goes to a
      different bank. Valid but usually not what you want; set it to
      the log2 of the granule size you actually want.
    - **Requests must not straddle a bank boundary.** The crossbar
      samples the bank selector once from the base address and
      forwards the whole request to that bank. A request that crosses
      a granule boundary delivers bytes beyond the granule to the
      wrong place inside the bank. Upstreams must keep each request
      within a single granule (``addr % (1 << iw) + size <= (1 << iw)``).
    - **Round-robin arbitration, one master per bank per cycle.** Two
      masters contending for the *same* bank are served on successive
      ticks; the rotor advances each tick a forward succeeded, so no
      master is starved. Masters targeting *different* banks are served
      in the same tick.
    - **Synchronous bank required.** The output is :class:`IoV2Sync`; a
      bank that returns ``GRANTED``/``DENIED`` or drives ``resp()`` /
      ``retry()`` violates the contract and is fatal at run time. Bind a
      sync slave such as ``memory.memory_v3.Memory``.
    - **Fixed topology at build time.** ``nb_masters``, ``nb_slaves``,
      ``interleaving_width`` are read once. Runtime reconfiguration is
      not supported.
    - **Address rewrite is not reversed.** The request object keeps the
      bank-local address after the forward; a master that re-reads
      ``req->addr`` after completion sees the bank-local value.

    Configuration fields
    ~~~~~~~~~~~~~~~~~~~~

    All crossbar parameters live on :class:`SpatzTcdmIntercoConfig`. Values are
    read once at construction; no runtime reconfiguration.

    ``nb_masters``
        Number of input master ports. Must be ≥ 1. Default: ``0`` —
        must be overridden.

    ``nb_slaves``
        Number of output bank ports. Must be ≥ 1; power of two for
        a clean address decode. Default: ``0`` — must be overridden.

    ``interleaving_width``
        Log2 of the bank-local granule size, in bytes. With
        ``interleaving_width=2`` the granule is 4 bytes (word-level
        interleaving); with ``interleaving_width=6`` the granule is
        64 bytes (cache-line interleaving). Default: ``0`` (byte
        interleaving).

    Example
    ~~~~~~~

    Four masters fan in to eight banks, 32-byte interleaving. The
    upstream router maps the crossbar's region (say
    ``0x1000_0000..0x1000_xxxx``) with the default ``remove_base=True``
    so the SpatzTcdmInterco's inputs see addresses starting at 0:

    .. code-block:: python

        xbar = SpatzTcdmInterco(self, 'xbar', config=SpatzTcdmIntercoConfig(
            nb_masters=4, nb_slaves=8,
            interleaving_width=5))
        for m in range(4):
            cores[m].o_DATA(xbar.i_INPUT(m))
        for s in range(8):
            xbar.o_OUTPUT(s, banks[s].i_INPUT())

    A request reaching the SpatzTcdmInterco at offset ``0x40`` (granule-id 0x2,
    bank 2) lands on ``banks[2]`` at bank-local address 0. A
    concurrent request at offset ``0x60`` (granule 0x3, bank 3) lands
    on ``banks[3]`` at bank-local 0 in parallel — the two masters do
    not stall each other.

    Parameters
    ----------
    parent : Component
        Parent component this crossbar is instantiated under.
    name : str
        Local name of the crossbar within ``parent``. Used for
        component path, traces, and as the lookup key for bindings.
    config : SpatzTcdmIntercoConfig
        All crossbar parameters live here. The instance is taken over
        by the crossbar — pass a fresh :class:`SpatzTcdmIntercoConfig` per
        instance.
    """

    # Developer-manual doc registration. Discovered by AST scan at doc
    # build time; the class docstring above is rendered via autoclass.
    __gvsoc_doc__ = {
        'title': 'SpatzTcdmInterco',
        'tests_dirs': [],
    }

    def __init__(self, parent: Component, name: str, config: SpatzTcdmIntercoConfig):
        super().__init__(parent, name, config=config)
        self.add_sources(['pulp/snitch/snitch_cluster/spatz/spatz_tcdm_interco.cpp'])
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
        """Returns the slave port ``wide_input_<id>`` for wide master ``id``.

        A wide master issues accesses spanning several consecutive banks
        (e.g. a 512-bit DMA hitting a whole superbank). The crossbar serves
        all spanned banks within the same arbiter tick and gives wide
        masters priority over the narrow ones, mirroring the RTL
        ``mem_wide_narrow_mux``. Same async DENY/retry contract as the
        narrow inputs. Atomics are not supported on this path.
        """
        return SlaveItf(self, f'wide_input_{id}', signature=IoV2SingleReq())

    def i_INPUT(self, id: int) -> SlaveItf:
        """Returns the slave port ``input_<id>`` for master ``id``.

        Each master gets its own muxed input port. The input side speaks
        the single-req contract (:class:`IoV2SingleReq`): every fresh
        request is ``DENIED`` and the master is later ``retry()``-ed
        by the round-robin arbiter; the re-issued request then
        completes inline as ``DONE``. A master bound here must
        therefore implement the standard DENY/retry handshake
        (so not ``IoV2Sync``).
        """
        return SlaveItf(self, f'input_{id}', signature=IoV2SingleReq())

    def o_OUTPUT(self, id: int, itf: SlaveItf):
        """Binds downstream bank ``output_<id>`` to ``itf``.

        ``id`` must be in ``[0, nb_slaves)``. The bound downstream receives
        requests whose selector bit field equals ``id``, with the address
        rewritten to bank-local form.

        The output is declared :class:`IoV2Sync`: the bank must answer every
        request inline with ``IO_REQ_DONE`` (it may annotate
        ``req->latency``) and must never use the async ``resp()`` /
        ``retry()`` channel. That guarantee is what lets the crossbar
        respond to the originating master within the arbiter tick, with no
        per-request in-flight tracking. ``memory.memory_v3.Memory`` is the
        canonical sync bank.
        """
        self.itf_bind(f'output_{id}', itf, signature=IoV2Sync())
