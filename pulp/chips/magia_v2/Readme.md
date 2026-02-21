# MAGIA v2 – GVSoC Virtual Platform

MAGIA v2 is a **tile-based RISC-V manycore architecture** modeled in **GVSoC**.  
It targets scalable AI / HPC workloads and provides a realistic simulation of:

- A 2D mesh of compute tiles
- Local L1 scratchpads (TCDM)
- A shared L2 memory accessed through a NoC
- Hardware accelerators (RedMulE)
- DMA engines (iDMA)
- A hierarchical **Fractal Synchronization Network**
- Optional **Snitch + Spatz** vector cores per tile

The platform is fully **memory-mapped**, configurable at runtime, and designed to be easily extended.

---

## Prerequisites

The following are assumed to be **already installed and working**:

- GVSoC
- A micromamba environment with **Python ≥ 3.12**
- All GVSoC dependencies correctly set up

This README focuses only on **MAGIA v2 usage and architecture**.

---

## Repository Setup

```bash
git clone https://github.com/FondazioneChipsIT/gvsoc
git submodule update --init --recursive

cd pulp
git checkout lz/magia-v2-pulp
cd ..

cd core
git checkout lz/magia-v2-core
cd ..
```

---

## Build the MAGIA v2 Platform

```bash
make build TARGETS=magia_v2
```

This builds the MAGIA v2 virtual platform and installs the `gvrun` executable under:

```
./install/bin/gvrun
```

---

## Running a Simulation (Base Configuration)

```bash
./install/bin/gvrun \
  --target magia_v2 \
  --work-dir /home/gvsoc/Documents/test \
  --param binary=/home/gvsoc/Documents/magia-sdk/build/bin/test_mm_os \
  run \
  --attr magia_v2/n_tiles_x=4 \
  --attr magia_v2/n_tiles_y=4
```

### Command-line Parameters

| Parameter | Description |
|---------|-------------|
| `--work-dir` | Directory where GVSoC writes simulation outputs |
| `--param binary=...` | Path to the ELF binary to be executed |
| `--attr magia_v2/n_tiles_x` | Number of tiles in X dimension |
| `--attr magia_v2/n_tiles_y` | Number of tiles in Y dimension |

The total number of tiles is:

```
NB_CLUSTERS = n_tiles_x × n_tiles_y
```

---

## Running with Snitch + Spatz Enabled

If **Spatz** is enabled in `arch.py`, you must also provide the Spatz boot ROM:

```bash
./install/bin/gvrun \
  --target magia_v2 \
  --work-dir /home/gvsoc/Documents/test \
  --param binary=/home/gvsoc/Documents/MAGIA/sw/tests/mesh_dotp_spatz_test/build/verif \
  --trace-level=trace \
  run \
  --attr magia_v2/n_tiles_x=4 \
  --attr magia_v2/n_tiles_y=4 \
  --attr magia_v2/spatz_romfile=/home/gvsoc/Documents/MAGIA/spatz/bootrom/spatz_init.bin
```

### Additional Parameter

| Parameter | Description |
|---------|-------------|
| `--attr magia_v2/spatz_romfile` | Path to the Snitch-Spatz boot ROM |

---

## High-Level Architecture Overview

MAGIA v2 is organized in three main layers:

```
+---------------------------+
|        Board Layer        |
|     (magia_v2_board)      |
+---------------------------+
|         SoC Layer         |
|       (MagiaV2Soc)        |
+---------------------------+
|        Tile Layer         |
|      (MagiaV2Tile)        |
+---------------------------+
```

---

## Board Layer (`MagiaV2Board`)

The **board** is the GVSoC entry point:

- Declares runtime parameters (e.g. `binary`)
- Instantiates the SoC
- Connects GVSoC runner logic to the model

This is the component bound to `--target magia_v2`.

---

## SoC Layer (`MagiaV2Soc`)

The SoC is responsible for:

- Creating the **tile mesh**
- Instantiating **L2 memory**
- Building the **2D NoC (FlooNoC)**
- Connecting tiles to memory and NoC
- Instantiating the **Fractal Synchronization Tree**
- Managing simulation termination via `KillModule`

### Tile Placement

Tiles are arranged in a row-major 2D grid:

```
X →
0   1   2   3
4   5   6   7
8   9  10  11
12 13  14  15
↓
Y
```

Each tile has:
- A private L1 address space
- Access to remote L1s and shared L2 via NoC

---

## Tile Architecture (`MagiaV2Tile`)

Each tile is a **self-contained compute cluster** composed of:

### Compute Cores
- **CV32** RISC-V core (always present)
- Optional **Snitch + Spatz** vector core

### Local Memory
- **TCDM (L1 scratchpad)**
  - 32 banks
  - Multi-ported via interleavers
  - Shared by cores, DMA, and accelerators

### Accelerators
- **RedMulE** (matrix / tensor engine)
- Memory-mapped control interface

### DMA Engines
- Two **Snitch DMA** engines per tile
- Controlled through a memory-mapped iDMA controller
- Support local and remote transfers

### Synchronization
- **Fractal Sync MM Controller**
- Hardware synchronization via a hierarchical fractal tree
- Neighbor and multi-level synchronization supported

### Interconnect
- OBI crossbar (control / local memory)
- AXI crossbar (remote accesses, L2)
- Narrow or Narrow+Wide NoC channels (configurable)

### Event & Debug
- Event Unit (interrupt routing)
- UART (stdout)
- GDB server support

---

## Fractal Synchronization Network

MAGIA v2 implements a **hierarchical fractal synchronization tree**:

- Level 0: tile-to-fractal connections
- Intermediate levels: aggregation and propagation
- Root level: global synchronization

This allows:
- Fast barrier-like synchronization
- Scalable coordination across large meshes
- Explicit modeling of sync latency and topology

---

## Memory Map (Per Tile – Simplified)

| Region | Description |
|------|-------------|
| Local L1 | Tile private TCDM |
| RedMulE CTRL | Accelerator control |
| iDMA CTRL | DMA control |
| FSYNC CTRL | Fractal sync control |
| Event Unit | Interrupts & events |
| L2 | Shared memory via NoC |
| STDOUT | Simulation UART |
| TEST_END | Simulation termination |

Exact addresses are defined in `arch.py`.

---

## Configuration and Extension Points

### Architecture Parameters (`arch.py`)
- Tile grid size defaults
- Memory sizes
- Clock frequency
- Enable / disable Spatz
- Narrow vs Wide NoC channels

### Easy Extensions
You can extend MAGIA v2 by:
- Adding a new accelerator inside `MagiaV2Tile`
- Adding new MMIO controllers
- Changing NoC topology or parameters
- Modifying Fractal Sync behavior
- Tuning latency / bandwidth parameters

The design intentionally keeps **clear separation** between:
- Architecture description
- SoC composition
- Tile micro-architecture

---

## Simulation Termination

The simulation ends when all tiles write to the **TEST_END** address range.  
This is handled by the `KillModule`, which waits for all clusters before stopping GVSoC.

---

## Summary

MAGIA v2 is a **scalable, realistic, and extensible** GVSoC platform designed for:

- Research on tiled AI architectures
- Memory hierarchy exploration
- Synchronization mechanisms
- Accelerator / DMA co-design

It trades simplicity for **explicitness**: everything is visible, configurable, and hackable.

Happy hacking
