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
- Optional **PCIe VFIO bridge** for QEMU co-simulation

The platform is fully **memory-mapped**, configurable at runtime, and designed to be easily extended.

---

## Prerequisites

The following are assumed to be **already installed and working**:

- GVSoC
- A micromamba environment with **Python ≥ 3.12**
- All GVSoC dependencies correctly set up

If the PCIe VFIO bridge is enabled (`ENABLE_PCIE_VFIO = True` in `arch.py`), you additionally need **libvfio-user** — see the [PCIe VFIO Bridge section](#pcie-vfio-bridge-mode-enable_pcie_vfio) below.

This README focuses only on **MAGIA v2 usage and architecture**.

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

## PCIe VFIO Bridge Mode (`ENABLE_PCIE_VFIO`)

When `ENABLE_PCIE_VFIO = True` is set in `arch.py`, MAGIA v2 exposes its L2 memory to an external **QEMU** virtual machine as a **PCIe endpoint** via the `vfio-user` protocol.

```
+-------------------+        vfio-user socket         +------------------+
|      GVSoC        |  <---------------------------->  |      QEMU        |
|                   |                                  |                  |
|  PCIe endpoint    |                                  |  PCIe root port  |
|  (vfio-user)      |                                  |  guest VM        |
+-------------------+                                  +------------------+
```

This enables a guest OS running inside QEMU to drive DMA transfers to and from the MAGIA v2 L2 memory, load ELF binaries through the PCIe BAR, and control accelerator startup — all without modifying the GVSoC model itself.

In this mode:
- The ELF binary is **not** loaded via `--param binary`. The guest software is responsible for loading and starting the accelerator.
- The `KillModule` fires a `done_irq` signal to the bridge when all tiles have completed, instead of calling `quit()` directly.
- The bridge receives the `done_irq`, forces `fetch_en` low, and triggers a full GVSoC reset.

### Enabling the Bridge

In `pulp/pulp/chips/magia_v2/arch.py`:

```python
ENABLE_PCIE_VFIO = True   # default: False
```

Then rebuild:

```bash
make build TARGETS=magia_v2
```

### Running GVSoC in VFIO Bridge Mode

```bash
./install/bin/gvrun \
  --target magia_v2 \
  --work-dir /home/gvsoc/Documents/test \
  run \
  --attr magia_v2/n_tiles_x=4 \
  --attr magia_v2/n_tiles_y=4
```

GVSoC will start and **block** waiting for QEMU to connect on `/tmp/gvsoc.sock`.

### Dependencies: libvfio-user

```bash
git clone https://github.com/nutanix/libvfio-user
cd libvfio-user
meson build
ninja -C build
sudo ninja -C build install
```

Expected install paths:

```
/usr/local/include/vfio-user/
/usr/local/lib/x86_64-linux-gnu/
```

### QEMU Command Line

The QEMU build must support the `vfio-user-pci` device. Launch QEMU from the QEMU build directory:

```bash
./build/qemu-system-x86_64 \
  -object memory-backend-memfd,id=mem,size=2G,share=on \
  -machine q35,memory-backend=mem \
  -nodefaults \
  -display none \
  -serial none \
  -monitor tcp:127.0.0.1:45454,server,nowait \
  -drive id=hd0,file=/home/gvsoc/Documents/toolchain/qemu-img/debian-12-nocloud-amd64.qcow2,format=qcow2,if=none \
  -device virtio-blk-pci,drive=hd0 \
  -device e1000,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 \
  -device pcie-root-port,id=rp1 \
  --device '{"driver":"vfio-user-pci","socket":{"type":"unix","path":"/tmp/gvsoc.sock"}}'
```

> **Note:** `gvsoc.sock` must match `socket_path` configured in `soc.py` for the bridge instance.

### Guest VM

The guest VM image is a **stock Debian 12 (Bookworm)** cloud image (no-cloud variant).  
Download `debian-12-nocloud-amd64.qcow2` from:

https://cloud.debian.org/images/cloud/bookworm/latest/

To connect to the running guest over SSH (forwarded to host port 2222):

```bash
ssh -p 2222 root@127.0.0.1
```

### Typical Execution Flow

1. Configure GVSoC with `ENABLE_PCIE_VFIO = True` and rebuild
2. Start GVSoC — it will block waiting for QEMU
3. Start QEMU with the command above — GVSoC unblocks on attach
4. Boot the guest VM and log in via SSH
5. Inside the guest: build/load the kernel driver, run the DMA or loader test application
6. The guest programs BAR0 registers to trigger DMA and start the accelerator
7. When all tiles write to `TEST_END`, `KillModule` fires `done_irq` to the bridge
8. The bridge forces `fetch_en` low and triggers a full GVSoC reset

### Host-side Test Environment

The host-side software stack (kernel module, DMA test, ELF loader) is available at:

https://github.com/TheSSDGuy/gvsoc-vfio-test

---

## PCIe VFIO Bridge – Architecture Reference

### BAR0 Register Map

| Offset | Register | Description |
|--------|----------|-------------|
| `0x00` | `DMA_SRC_ADDR_LO` | DMA source address (low 32 bits) |
| `0x04` | `DMA_SRC_ADDR_HI` | DMA source address (high 32 bits) |
| `0x08` | `DMA_DST_ADDR_LO` | DMA destination address (low 32 bits) |
| `0x0C` | `DMA_DST_ADDR_HI` | DMA destination address (high 32 bits) |
| `0x10` | `DMA_LEN` | Transfer length in bytes |
| `0x14` | `DMA_CTRL` | Control: `START[0]`, `ABORT[1]`, `IRQ_EN[2]` |
| `0x18` | `DMA_STATUS` | Status: `BUSY[0]`, `DONE[1]`, `ERROR[2]` |
| `0x1C` | `DMA_ERROR` | Error code (0 = none) |
| `0x20` | `DMA_MAGIC` | Magic value `0x44504131` (read-only) |
| `0x24` | `DMA_DIRECTION` | `0` = host→card, `1` = card→host |
| `0x28` | `ENTRY_POINT` | Forwarded to `entry_addr` wire |
| `0x2C` | `FETCH_ENABLE` | Non-zero enables fetch on accelerator |
| `0x40` | MSI-X table | Vector 0 table entry |
| `0x80` | MSI-X PBA | Pending bit array |

### GVSoC Ports

| Port | Direction | Type | Description |
|------|-----------|------|-------------|
| `mem` | master | IO | DMA accesses to device memory |
| `done_irq` | slave | `wire<bool>` | End-of-compute signal from accelerator |
| `fetch_en` | master | `wire<bool>` | Driven from `FETCH_ENABLE` BAR register |
| `entry_addr` | master | `wire<uint64_t>` | Driven from `ENTRY_POINT` BAR register |

### Execution Model

The bridge uses a **split-thread** model:

- A dedicated **VFIO thread** runs the libvfio-user event loop (attach, polling, MSI-X)
- All **GVSoC-side work** executes on the GVSoC simulation thread via clock events

BAR callbacks only latch state and schedule a GVSoC clock event — they never directly touch the GVSoC memory hierarchy. This keeps the two execution domains correctly separated.

### DMA Model

- Only one DMA transfer can be in flight at a time
- Transfers are chunked (configurable via `dma_chunk_bytes`, default 16 bytes)
- Each chunk is issued as a GVSoC IO request on `mem`
- MSI-X vector 0 is triggered on completion when `DMA_CTRL_IRQ_EN` is set
- Abort cancels a pending-but-not-started request; an already in-flight GVSoC request runs to completion

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
- Optionally instantiating the **PCIe VFIO bridge** and connecting it to L2

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
- Narrow+Wide NoC channels

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

| Parameter | Default | Description |
|-----------|---------|-------------|
| `N_TILES_X` | `4` | Default tile grid width |
| `N_TILES_Y` | `4` | Default tile grid height |
| `TILE_CLK_FREQ` | `200 MHz` | Tile clock frequency |
| `SPATZ_ENABLE` | `False` | Enable Snitch+Spatz vector core per tile |
| `ENABLE_PCIE_VFIO` | `False` | Enable PCIe VFIO bridge (QEMU co-simulation) |

Memory sizes, latencies, and DSE parameters are defined in `MagiaArch` and `MagiaDSE`.

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

### Standard mode (`ENABLE_PCIE_VFIO = False`)

When all tiles write to the **TEST_END** address range, the `KillModule` calls `quit()` with the exit code written by the last tile, stopping the GVSoC engine.

### VFIO bridge mode (`ENABLE_PCIE_VFIO = True`)

When all tiles write to **TEST_END**, the `KillModule` fires a `done_irq` signal to the PCIe bridge. The bridge then:
1. Forces `fetch_en` low (stops accelerator fetch)
2. Triggers a full **GVSoC reset** by asserting and releasing the top-level reset hierarchy

This allows the QEMU guest to observe completion through the BAR0 status bits and MSI-X, and to restart a new run without restarting the simulation.

---

## Source Layout (relevant files)

```
pulp/pulp/chips/magia_v2/
├── arch.py                        # Architecture constants and parameters
├── board.py                       # GVSoC board entry point
├── soc.py                         # SoC composition (tiles, NoC, L2, bridge)
├── tile.py                        # Tile micro-architecture
├── fractal_sync/                  # Fractal synchronization module
├── kill_module/                   # Simulation termination module
│   ├── kill_module.py
│   └── kill_module.cpp
└── Readme.md

pulp/pulp/pcie_vfio_bridge/
├── CMakeLists.txt                 # Build: vp_model(NAME pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge ...)
├── pcie_vfio_mem_bridge.cpp       # C++ model implementation
└── pcie_vfio_mem_bridge.py        # Python systree binding
```

The bridge model is registered in `pulp/pulp/CMakeLists.txt` via:

```cmake
add_subdirectory(pcie_vfio_bridge)
```

---

## Summary

MAGIA v2 is a **scalable, realistic, and extensible** GVSoC platform designed for:

- Research on tiled AI architectures
- Memory hierarchy exploration
- Synchronization mechanisms
- Accelerator / DMA co-design
- QEMU co-simulation via PCIe VFIO bridge

It trades simplicity for **explicitness**: everything is visible, configurable, and hackable.

Happy hacking
