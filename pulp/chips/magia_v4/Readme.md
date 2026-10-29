# MAGIA v4: GVSoC Virtual Platform

MAGIA v4 is the GVSoC model of the MAGIA RTL branch
`lb/Collectives-Parametrize-Magiav4`, in its homogeneous tile configuration:
every tile has RedMulE, Snitch + Spatz and the PULP cluster, a CV32E40P
control core, and FractalSync is on.

The mesh, the L2, the NoC and most blocks of the tile are the ones of
[MAGIA v3](../magia_v3/Readme.md), which documents them; this file covers what
is different. The blocks both platforms use are in `../magia_common`.

Not modelled:

- the NoC collectives (the `0xB000_0000` window, `collective_gen`, the USER
  registers of the iDMA); the collective registers of the tile exist, as
  plain registers;
- the heterogeneous tile configurations (`HETERO_TILE_CFGS`);
- the CV32E40X control core;
- the serialization of the core data demuxes behind an event unit access
  (RTL `SerialSlv`).

---

## Build and Run

As for v3, the mesh size and the number of PULP cores per tile are target
parameters, part of the target name:

```bash
make build TARGETS="magia_v4:n_tiles_x=4,n_tiles_y=4,nb_pulp_cores=8"

gvrun --target=magia_v4:n_tiles_x=4,n_tiles_y=4,nb_pulp_cores=8 \
      --param binary=<elf> --work-dir <dir> run
```

The RTL tests of the "tile" testbench run on one tile
(`n_tiles_x=1,n_tiles_y=1`): as the single-tile testbench of the RTL, the hart
ids and the FractalSync widths then count 16 tiles (`InstanceCount`), which is
what these tests are built for.

The Spatz boot ROM is the one of the RTL (`spatz_bootrom.bin`, the 4
instructions of `spatz_bootrom.sv`, which jump to `SPATZ_TASKBIN`).
`--attr magia_v4/spatz_romfile=<path>` replaces it.

---

## Tile Architecture (`MagiaV4Tile`)

### Data paths of the cores

The control core and each PULP cluster core have a data demux (RTL
`core_data_demux`, no latency of its own):

| Window | Destination |
|---|---|
| reserved area `0x3900..0xFFFF`, stack `0x10000..0x1FFFF`, L1 of the tile | a TCDM port of the core |
| its event unit (`0x0700` for the control core, `0x1900` for the cluster) | the direct link of the event unit (`demux_in_<core>`) |
| everything else | the OBI crossbar |

A router output binds a single slave, so the two TCDM windows (the reserved
area and the stack are contiguous) join on a small router in front of the
TCDM port.

TCDM narrow ports, as the RTL HCI core ports: 0 the OBI crossbar, 1 to 4 the
Spatz VLSU, then the cluster cores, the control core last.

### OBI crossbar

| Window | Destination |
|---|---|
| reserved area, stack, L1 of the tile | TCDM port 0 |
| `0x0000..0x06FF`, `0x1700..0x28FF` | the peripheral demux (RTL `ctrl_demux`): RedMulE `0x100`, iDMA `0x200`, FractalSync `0x600`, `tile_ctrl` `0x1700` |
| `0x0700..0x16FF` | the control-core event unit, OBI view (`input`) |
| `0x2900..0x38FF` | the cluster event unit, OBI view (`input`) |
| default | the AXI crossbar (L2, other tiles) |

Each output has a cut, as the RTL. The peripherals answer one cycle after the
request (registered `rvalid`). The kill module and stdout windows are served in
the tile, as in v3.

### AXI crossbar

Inputs: control-core icache refill, OBI crossbar, NoC, Spatz icache refill,
cluster icache refill. Outputs: the L1 of the tile (to the OBI crossbar), the
Spatz boot ROM (`0x1000_0000`), the NoC.

### Event units

Both are the io_v2 event unit of `../magia_common/event_unit`, configured as the
RTL instances of `event_unit_flex` and its `cluster_event_map`:

| | Control core | PULP cluster |
|---|---|---|
| Cores | 1 | `nb_pulp_cores` |
| Barriers, mutexes, dispatch FIFO | 2, 1, 1 | one per core, 1, 8 |
| Barrier / mutex / dispatch event | 0 / 0 / 1 | 16 / 17 / 18 |
| SW events | from bit 6 | from bit 4 (8 events) |
| Interrupt | not connected (the core sleeps on `cv.elw`) | none |

Events of the control-core event unit: 2 / 3 iDMA done, 8 Spatz done, 10
RedMulE, 12 cluster done, 24 FractalSync done. The cluster cores sleep on
their event unit with `cv.elw`; core 0 is woken by event 13 (`CLUSTER_START`).

### Instruction caches

As v3 for the control core and Spatz. The cluster has the
`MagiaClusterDefaultCfg` one: a private L0 of 512 B per core and a shared L1
of 4 KiB with 4 ways, 256-bit lines, serial lookup, 4 refills in flight. The
L0 prefetchers are off (tied to 0, as the RTL).

### Control registers (`tile_ctrl`, `0x1700`)

One component for the three RTL slaves behind the peripheral demux. Offsets
from `0x1700`; unused offsets of a block read `0xDEADBEEF`.

| Offset | Register | Behaviour |
|---|---|---|
| `0x000` | `SPATZ_CLK_EN` | bit 0: fetch enable of Spatz |
| `0x004` | `SPATZ_READY` | bit 0 |
| `0x008` | `SPATZ_START` | bit 0: start of Spatz (a level on its interrupt 11) |
| `0x00C` | `SPATZ_TASKBIN` | stored |
| `0x010` | `SPATZ_DATA` | stored |
| `0x014` | `SPATZ_RETURN` | stored |
| `0x018` | `SPATZ_DONE` | bit 0 pulses event 8 of the control-core event unit |
| `0x040` | `CLUSTER_FETCH_EN` | non-zero: fetch enable of the cluster cores; clears the READY count |
| `0x044` | `CLUSTER_BINARY` | boot address of the cluster cores (`0xCC000080` at reset) |
| `0x048` | `CLUSTER_DONE` | a write pulses event 12 of the control-core event unit |
| `0x04C` | `CLUSTER_TASKBIN` | stored |
| `0x050` | `CLUSTER_DATA` | stored |
| `0x054` | `CLUSTER_START` | non-zero: START = 1 and event 13 of core 0 of the cluster event unit; 0: START = 0 |
| `0x058` | `CLUSTER_READY` | a write counts one core; reads 1 once every core counted |
| `0x05C` | `CLUSTER_RETURN` | stored |
| `0x100` | `COLL_MASK` | stored (collectives not modelled) |
| `0x104` | `COLL_OP` | bits 3:0, stored |

### Hart ids

Control core and Spatz: the tile id. Cluster core `k` of tile `t`:
`2 * InstanceCount + t * nb_pulp_cores + k`.

---

## Memory Map (Per Tile)

| Base | Size | Block |
|---|---|---|
| `0x0000_0100` | `0x100` | RedMulE |
| `0x0000_0200` | `0x400` | iDMA (two channels) |
| `0x0000_0600` | `0x100` | FractalSync controller |
| `0x0000_0700` | `0x1000` | Control-core event unit |
| `0x0000_1700` | `0x200` | `tile_ctrl` (Spatz, cluster, collectives) |
| `0x0000_1900` | `0x1000` | Cluster event unit, direct links of the cluster cores |
| `0x0000_2900` | `0x1000` | Cluster event unit, OBI view |
| `0x0000_3900` | up to `0xFFFF` | Reserved area (private) |
| `0x0001_0000` | `0x10000` | Stack |
| `0x0002_0000 + t * 0x10_0000` | `0xE_0000` | L1 of tile `t` |
| `0x1000_0000` | `0x100` | Spatz boot ROM |
| `0xC000_0000` | `l2_size` | L2 |

---

## RTL Tests

The 36 bare-metal tests of the RTL for the CV32E40P control core (`sw/tests`
of the RTL repository, the 12 collective tests excluded) pass: every tile
writes an exit code of `0x800` and no test reports errors. Note that the exit
code only keeps the number of errors modulo 2048: a test also prints its
error count, which is the one to check.

---

## Source Layout

```
pulp/pulp/chips/magia_v4/
├── arch.py              # Address map, design-space parameters, MagiaTree
├── board.py             # GVSoC board entry point
├── soc.py               # SoC composition (tiles, NoC, L2, FractalSync tree)
├── tile.py              # Tile micro-architecture
├── tile_ctrl/           # Control registers of Spatz, the cluster and the collectives
│   ├── tile_ctrl.cpp
│   └── tile_ctrl.py
├── spatz_bootrom.bin    # Spatz boot ROM of the RTL
├── CMakeLists.txt       # Installs the boot ROM
└── Readme.md

pulp/pulp/chips/magia_common/   # Blocks shared with magia_v3
```
