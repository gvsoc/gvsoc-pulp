# floonoc_flex

A generic-topology network-on-chip model. Not tied to a mesh: topology is just a list of router/NI nodes plus links, with routing tables computed by one of several generators or loaded verbatim from a `routing.yml`.

## What's modeled

- Independent **narrow** and **wide** links, each with its own width (bytes/cycle) and its own set of per-node routers/queues; a burst is split into internal requests sized to whichever link it travels.
- **Collectives primitives** (multicast/broadcast and in-network reduction) dependent on the `collective_enable` setting, and currently mesh-only:
  - Multicast/broadcast to an arbitrary sub-grid (row, column, rectangle,
    or the whole mesh), via a mask carried in the request (see `COLL_MASK_AXIS_BITS`/`coll_mask_set_axis` in `floonoc_flex.hpp`).
  - Six arithmetic reductions (ADD/MAX × uint16/int16/fp16).
  - `LsbAnd`: a group-barrier/wakeup primitive (AND bit 0 of every participant's byte, byte-by-byte).
  - A test matrix in `pulp/tests/floonoc_flex_collective/`.

## Integrating this NoC into a system model

- **Node IDs are a single flat space shared by routers and NIs** (e.g. a
  floogen-generated 4x4 mesh gets routers 0-15 and NIs 16-31). This is set at `id_map` (returned by `load_from_floogen`, or built yourself via `add_router`/`add_network_interface`).
- **Every NI needs exactly one router link.**
- **Collectives need router names FlooGen can parse as coordinates in Mesh** (`..._x_y` suffix) and `route_algo: "XY"`/`"YX"`.

## How software drives a collective

A collective is just a normal burst to a normal address, with a 9-byte
sideband descriptor (1 byte op + 8 bytes mask, `req->get_payload()`) that
the NI reads once `collective_policy` exists:

- **Mask** selects which bits of the destination's coordinate are "don't care" (i.e. which nodes are members).
- **Op** is `0` (`Unicast`) for plain traffic, or picks the collective behavior: leaving it `0` with a non-zero mask auto-promotes to `Multicast`, anything else must be set explicitly.
- **Write = broadcast.** Every member's node gets the same data written to the same relative address (same tile offset, different node).
- **Read = reduction/barrier gather.** The root reads from the same address and the result it gets back is every member's contribution folded together by the op.
- The root is just whichever node injects the request.
