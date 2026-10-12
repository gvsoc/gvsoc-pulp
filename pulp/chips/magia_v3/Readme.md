# MAGIA v3 – GVSoC Virtual Platform

MAGIA v3 is a **tile-based RISC-V manycore architecture** modeled in **GVSoC**.  
It extends MAGIA v2 with a **PULP multi-core cluster** per tile and targets scalable AI / HPC workloads.  
It provides a realistic simulation of:

- A 2D mesh of compute tiles
- Local L1 scratchpads (TCDM)
- A shared L2 memory accessed through a NoC
- Hardware accelerators (RedMulE)
- DMA engines (iDMA)
- A hierarchical **Fractal Synchronization Network**
- Optional **Snitch + Spatz** vector cores per tile
- Optional **PULP multi-core cluster** per tile (up to 8 RISC-V cores)
- Optional **PCIe VFIO bridge** for QEMU co-simulation

The platform is fully **memory-mapped**, configurable at runtime, and designed to be easily extended.

---

## Prerequisites

The following are assumed to be **already installed and working**:

- GVSoC
- A micromamba environment with **Python ≥ 3.12**
- All GVSoC dependencies correctly set up

If the PCIe VFIO bridge is enabled (`ENABLE_PCIE_VFIO = True` in `arch.py`), you additionally need **libvfio-user** — see the [PCIe VFIO Bridge section](#pcie-vfio-bridge-mode-enable_pcie_vfio) below.

This README focuses only on **MAGIA v3 usage and architecture**.

---

## Build the MAGIA v3 Platform

The mesh size (`n_tiles_x`, `n_tiles_y`) and the number of PULP cores per tile
(`nb_pulp_cores`) are **target parameters**: they are part of the target *name*
and are baked into the compiled platform tree at build time. Pass them after the
target name, `key=value` separated by commas:

```bash
make build TARGETS="magia_v3:n_tiles_x=2,n_tiles_y=2,nb_pulp_cores=8"
```

Defaults (`1×1`, `nb_pulp_cores=8`) are used if omitted, i.e. `TARGETS=magia_v3`.
Several meshes can be built side by side (`;`-separated):

```bash
make build TARGETS="magia_v3:n_tiles_x=4,n_tiles_y=4,nb_pulp_cores=8;magia_v3:n_tiles_x=1,n_tiles_y=1,nb_pulp_cores=8"
```

This installs the `gvrun` executable under `./install/bin/gvrun`.

> **Important — build and run must use the SAME target string.**
> gvsoc compiles the whole component tree (its shape *and* every model's typed
> config) into `libplatform_tree_magia_v3.so` and checks it against the tree it
> rebuilds at run time. Reshaping the tree from a run-time `--attr` invalidates
> that library, drops to the JSON fallback (which cannot carry the typed io_v2 /
> iss_v2 configs), and the simulation dies with `std::bad_alloc`. So the mesh /
> core count live in the target name, not in `--attr`: pass the *identical*
> `--target=magia_v3:n_tiles_x=..,n_tiles_y=..,nb_pulp_cores=..` at run time.
> `--attr magia_v3/n_tiles_x=...` is now rejected on purpose.
> Only `spatz_romfile` stays a run-time `--attr` (it is a path overlaid at run
> time, never baked into the tree).

---

## SDK

The companion software SDK for MAGIA v3 is the `main` branch of:

**https://github.com/pulp-platform/magia-sdk**

### Building the SDK

Build from the SDK root directory (the default `target_platform=magia` selects
the MAGIA v3 tree of gvsoc):

```bash
make build \
  tiles=4 \
  pulp_cores=8 \
  LLVM_INSTALL_DIR=/path/to/llvm/install
```

SDK build parameters:

- **`tiles`** — number of tiles per dimension (e.g. `4` → 4×4 grid); must match `n_tiles_x`/`n_tiles_y` of the gvsoc target
- **`pulp_cores`** — number of PULP cores per tile (default: 8, `0` builds no PULP test); must match `nb_pulp_cores`
- **`spatz`** — `0` to build no Spatz test (default: 1)
- **`LLVM_INSTALL_DIR`** — LLVM/Clang toolchain, required for the Spatz tests
- **`CMAKE_BUILDDIR`** — build directory (default: `build`)
- **`profile_cmi`, `profile_cmo`, `profile_cmp`, `profile_snc`** — `1` to emit the profiling sentinels around the iDMA transfers, RedMulE jobs and synchronizations

### Running from the SDK

```bash
make run platform=gvsoc test=<test_name> tiles=4 pulp_cores=8 \
  GVSOC_DIR=/path/to/gvsoc
```

It calls `gvrun` with the target string matching `tiles`/`pulp_cores` and the
Spatz boot ROM of the build (`build/bin/bootrom/spatz_init.bin`).

---

## Running a Simulation (Base Configuration)

```bash
./install/bin/gvrun \
  --target=magia_v3:n_tiles_x=4,n_tiles_y=4,nb_pulp_cores=8 \
  --work-dir /path/to/workdir \
  --param binary=/path/to/magia-sdk/build/bin/<test_name> \
  run
```

Command-line parameters:

- **`--target=magia_v3:...`** — target string; **must be identical to the one used at build time**. Carries the tree-reshaping knobs `n_tiles_x`, `n_tiles_y`, `nb_pulp_cores`
- **`--work-dir`** — directory where GVSoC writes simulation outputs
- **`--param binary=...`** — path to the ELF binary to be executed (CV32 binary)

The total number of tiles is:

```
NB_CLUSTERS = n_tiles_x × n_tiles_y
```

---

## Running with PULP Cluster Enabled

When the binary includes a PULP workload, pass the number of PULP cores via
`nb_pulp_cores` in the target string:

```bash
./install/bin/gvrun \
  --target=magia_v3:n_tiles_x=1,n_tiles_y=1,nb_pulp_cores=8 \
  --work-dir /path/to/workdir \
  --param binary=/path/to/magia-sdk/build/bin/hello_pulp \
  run
```

- **`nb_pulp_cores`** (in the target string) — number of PULP cores instantiated per tile; must match the `pulp_cores` value used at SDK build time **and** the target string used when building the platform

---

## Running with Snitch + Spatz Enabled

If **Spatz** is enabled, provide the Spatz boot ROM:

```bash
./install/bin/gvrun \
  --target=magia_v3:n_tiles_x=4,n_tiles_y=4,nb_pulp_cores=8 \
  --work-dir /path/to/workdir \
  --param binary=/path/to/magia-sdk/build/bin/<spatz_test> \
  run \
  --attr magia_v3/spatz_romfile=/path/to/magia-sdk/build/bin/bootrom/spatz_init.bin
```

- **`--attr magia_v3/spatz_romfile`** — path to the Snitch-Spatz boot ROM binary. Unlike the mesh knobs, this stays a run-time `--attr`: it is a stim-file path overlaid at run time and never baked into the compiled tree

---

## Enabling Trace Output

Add `--trace-level=trace` and optionally filter by component:

```bash
./install/bin/gvrun \
  --target=magia_v3:n_tiles_x=1,n_tiles_y=1,nb_pulp_cores=8 \
  --work-dir /path/to/workdir \
  --param binary=/path/to/magia-sdk/build/bin/hello_pulp \
  --trace-level=trace \
  run \
  --trace=tile-0-cluster-regs
```

---

## PCIe VFIO Bridge Mode (`ENABLE_PCIE_VFIO`)

> **Not available on the io_v2 platform.** The PCIe VFIO bridge
> (`pulp/pcie_vfio_bridge`) is still an io (v1) model, which cannot bind to the
> io_v2 models of MAGIA v3: the SoC refuses `ENABLE_PCIE_VFIO = True` until the
> bridge is ported. The rest of this section describes the mode as it worked on
> the io (v1) platform.

When `ENABLE_PCIE_VFIO = True` is set in `arch.py`, MAGIA v3 exposes its L2 memory to an external **QEMU** virtual machine as a **PCIe endpoint** via the `vfio-user` protocol.

```
+-------------------+        vfio-user socket         +------------------+
|      GVSoC        |  <---------------------------->  |      QEMU        |
|                   |                                  |                  |
|  PCIe endpoint    |                                  |  PCIe root port  |
|  (vfio-user)      |                                  |  guest VM        |
+-------------------+                                  +------------------+
```

This enables a guest OS running inside QEMU to drive DMA transfers to and from the MAGIA v3 L2 memory, load ELF binaries through the PCIe BAR, and control accelerator startup — all without modifying the GVSoC model itself.

In this mode:
- The ELF binary is **not** loaded via `--param binary`. The guest software is responsible for loading and starting the accelerator.
- The `KillModule` fires a `done_irq` signal to the bridge when all tiles have completed, instead of calling `quit()` directly.
- The bridge receives the `done_irq`, forces `fetch_en` low, and triggers a full GVSoC reset.

### Enabling the Bridge

In `pulp/pulp/chips/magia_v3/arch.py`:

```python
ENABLE_PCIE_VFIO = True   # default: False
```

Then rebuild:

```bash
make build TARGETS=magia_v3
```

### Running GVSoC in VFIO Bridge Mode

```bash
./install/bin/gvrun \
  --target=magia_v3:n_tiles_x=4,n_tiles_y=4,nb_pulp_cores=8 \
  --work-dir /path/to/workdir \
  run
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
  -drive id=hd0,file=/path/to/debian-12-nocloud-amd64.qcow2,format=qcow2,if=none \
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

### Host-side Test Environment

The host-side software stack (kernel module, DMA test, ELF loader) is available at:

https://github.com/TheSSDGuy/gvsoc-vfio-test

---

## High-Level Architecture Overview

MAGIA v3 is organized in three main layers:

```
+---------------------------+
|        Board Layer        |
|     (magia_v3_board)      |
+---------------------------+
|         SoC Layer         |
|       (MagiaV3Soc)        |
+---------------------------+
|        Tile Layer         |
|      (MagiaV3Tile)        |
+---------------------------+
```

---

## Board Layer (`MagiaV3Board`)

The **board** is the GVSoC entry point:

- Declares runtime parameters (e.g. `binary`)
- Instantiates the SoC
- Connects GVSoC runner logic to the model

This is the component bound to `--target magia_v3`.

---

## SoC Layer (`MagiaV3Soc`)

The SoC is responsible for:

- Creating the **tile mesh**
- Instantiating **L2 memory** (`memory_v3`, behind an untimed router joining the NoC channels)
- Building the **2D NoC (FlooNoC v2)**
- Connecting tiles to memory and NoC
- Instantiating the **Fractal Synchronization Tree**
- Managing simulation termination via `KillModule`

The whole platform is described with **io_v2** models. An io_v2 slave port
binds exactly one master, so every fan-in is explicit (routers in front of the
L2, the TCDM banks, the event unit and the instruction caches), and the
x-bars leave the tile through one catch-all mapping instead of one mapping per
remote tile.

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

## Tile Architecture (`MagiaV3Tile`)

Each tile is a **self-contained compute cluster** composed of:

### Compute Cores

- **CV32CtrlCore** (`ctrl_core/`) — tile controller, always present
- Optional **Snitch + Spatz** vector core
- Optional **PULP cluster** — up to 8 **CV32PulpCore** workers (`pulp_core/`)

The control core and the PULP cores are the **CV32E40P** model of the iss_v2
(`pulp/cv32e40p`), configured as in `magia_tile.sv`: `COREV_PULP` and
`COREV_CLUSTER`, FPU with `ZFINX`, 29 HPM counters. The SDK builds for
`zhinxmin` on top of it, whose half-precision conversions come from the Xf16
subsets. With `COREV_CLUSTER`, `cv.elw` sleeps until the event unit answers,
and an interrupt taken meanwhile replays it after the handler.

- The control core reaches the Event Unit on a direct link (RTL
  `core_data_demux_eu_direct`); the Event Unit request drives its machine
  external interrupt (`mei`) and the core acknowledges it (`irq_ack`). Its
  `mtvec` starts at the boot address (`MagiaArch.BOOT_ADDR`, `mtvec_addr` of
  the core configuration), as the RTL ties `mtvec_addr_i` to `boot_addr_i`:
  the SDK crt0 relies on it (vector table at the start of the binary).
- The PULP cores are started by `ClusterRegs` on their `mei` line (bypassing
  the Event Unit); the line stays raised until the core acknowledges it
  (`irq_ack`), as `cluster_start_irq_pending` in the RTL.

Each group of cores has a two-level instruction cache (`icache.py`, built on
`cache_v4`): one private L0 per core in front of a shared L1.

### Local Memory
- **TCDM (L1 scratchpad)**
  - 32 banks behind a single crossbar (`SpatzTcdmInterco`) with bank
    conflicts modelled; wide masters (NoC wide channel, iDMA, RedMulE) span
    several banks and take priority over the narrow ones
  - Shared by cores, DMA, accelerators, and PULP cluster

### Accelerators
- **RedMulE** (matrix / tensor engine)
- Memory-mapped control interface

### DMA Engines
- Two **iDMA** channels per tile (`AxiObiDmaV3`, RTL `idma_axi_obi_transfer_ch`):
  AXI to OBI (L2 to L1) and OBI to AXI (L1 to L2)
- Programmed through their `idma_reg32_3d` register file (iDMA v0.6.4 layout:
  `NEXT_ID_0` at `0x44`, `DONE_ID_0` at `0x84`); the iDMA controller window
  decodes `+0x000` to the AXI to OBI channel and `+0x200` to the OBI to AXI
  one (RTL `idma_obi_ctrl_decoder`)
- The completion of each channel is an Event Unit event (`2` for AXI to OBI,
  `3` for OBI to AXI)

### Synchronization
- **Fractal Sync MM Controller**
- Hardware synchronization via a hierarchical fractal tree
- Neighbor and multi-level synchronization supported

### Interconnect
- OBI crossbar (control / local memory), beat router at the word width
- AXI crossbar (remote accesses, L2), beat router at the word width
- Narrow+Wide NoC channels; the two iDMA channels share the wide one

### Event & Debug
- Event Unit (interrupt routing for CV32CtrlCore; PULP done IRQ at `in_event_12_pe_0`)
- UART (stdout)
- GDB server support

## PULP Cluster

### Overview

Each tile can optionally host a **PULP multi-core cluster**: up to 8 RISC-V cores sharing the tile L1 TCDM.

The **CV32CtrlCore** acts as the cluster controller and follows a two-phase protocol:

**Init phase** (`pulp_init`):
1. Writes the PULP binary entry point to `PULP_BINARY`
2. Broadcasts clock enable to **all** PULP cores via `PULP_CLK_EN` (write `1`)
3. Polls `PULP_READY` until all cores have booted and are waiting for tasks

**Dispatch phase** (`pulp_run_task`):
1. Writes the task function address to `PULP_TASKBIN` and an optional data pointer to `PULP_DATA`
2. Sets `PULP_NB_CORES_TO_WAIT = popcount(core_mask)`
3. Writes the one-hot `core_mask` to `PULP_START` — `ClusterRegs` fires a 1-cycle MEI edge pulse to each selected core
4. Polls `PULP_START` until it clears to `0` (cleared when all selected cores ACK before executing their task)
5. Calls `eu_pulp_wait()` to wait for the DONE IRQ from the Event Unit

Each woken PULP core (from its MEI interrupt handler):
1. Reads `PULP_TASKBIN` and `PULP_DATA`
2. **ACKs** by writing `0` to `PULP_START` — before calling the task
3. Calls the task function with the data pointer as the first argument
4. After the task returns, writes `1` to `PULP_DONE`

`ClusterRegs` counts ACKs and DONE writes separately:
- When all `PULP_NB_CORES_TO_WAIT` ACKs received → clears `PULP_START` register (unblocks CV32 poll)
- When all `PULP_NB_CORES_TO_WAIT` DONE writes received → fires `pulp_done_irq` to the CV32 Event Unit (unblocks `eu_pulp_wait`)

### PULP Cluster Control Registers

The cluster control register block (`ClusterRegs`) is memory-mapped at **`CLUSTER_CTRL_BASE = 0x1700`** (tile-relative). All offsets below are from `CLUSTER_CTRL_BASE`.

#### Spatz sub-block — offsets `[0x00, 0x18]`

- **`0x00` `SPATZ_CLK_EN`** (R/W) — write `1` to enable Snitch+Spatz clock, `0` to disable
- **`0x04` `SPATZ_READY`** (R/W) — Snitch+Spatz ready status
- **`0x08` `SPATZ_START`** (R/W) — write `1` to assert start IRQ to Spatz (1-cycle pulse)
- **`0x0C` `SPATZ_TASKBIN`** (R/W) — task binary descriptor for Spatz
- **`0x10` `SPATZ_DATA`** (R/W) — data descriptor for Spatz
- **`0x14` `SPATZ_RETURN`** (R/W) — return value from Spatz task
- **`0x18` `SPATZ_DONE`** (W) — write `1` when Spatz is done; fires `spatz_done_irq`

#### PULP sub-block — offsets `[0x40, 0x5C]`

- **`0x40` `PULP_CLK_EN`** (R/W) — write `1` to broadcast clock enable to **all** PULP cores simultaneously; write `0` to disable
- **`0x44` `PULP_BINARY`** (R/W) — PULP binary entry point; CV32 writes `_pulp_binary_start` here before enabling the clock; drives the `pulp_entry` wire to all PULP cores
- **`0x48` `PULP_NB_CORES_TO_WAIT`** (R/W) — number of PULP cores expected to ACK and complete; written by `pulp_run_task()` at dispatch time
- **`0x4C` `PULP_DONE`** (W) — each PULP core writes `1` here after its task returns; `pulp_done_irq` fires after `PULP_NB_CORES_TO_WAIT` writes received
- **`0x50` `PULP_TASKBIN`** (R/W) — task function address; written by CV32 before dispatch; read by PULP cores in their interrupt handler
- **`0x54` `PULP_DATA`** (R/W) — opaque data pointer passed as first argument to the task function
- **`0x58` `PULP_START`** (R/W) — one-hot core dispatch bitmask; CV32 writes the mask to fire per-core MEI edge pulses; PULP cores write `0` (ACK before task); cleared when all `PULP_NB_CORES_TO_WAIT` ACKs received
- **`0x5C` `PULP_READY`** (R) — incremented by each PULP core after boot; CV32 polls until equal to `NB_PULP_CORES`

### PULP Binary Delivery

The PULP binary is **embedded inside the CV32 ELF** rather than being loaded from a separate file:

1. The PULP task is compiled independently as position-independent code (PIC)
2. The raw binary is converted to a C array header and placed in the `.pulp_binary` linker section of the CV32 ELF
3. At simulation start, the `ctrl_core_loader` loads the entire CV32 ELF (including the embedded PULP binary) into instruction RAM
4. **Init phase:**
   - CV32 writes `_pulp_binary_start` to `PULP_BINARY` (`0x1744`) → drives `pulp_entry` to all PULP cores
   - CV32 writes `1` to `PULP_CLK_EN` (`0x1740`) → all PULP cores begin fetching from `pulp_entry`
   - All cores execute `_start`: compute local hart ID, set up per-hart stack, clear BSS (core 0 only), enable MEIE+MIE, write `1` to `PULP_READY` (`0x175C`), enter WFI
   - CV32 polls `PULP_READY` until it equals `NB_PULP_CORES`
5. **Dispatch phase (per `pulp_run_task` call):**
   - CV32 writes task address to `PULP_TASKBIN` (`0x1750`) and data pointer to `PULP_DATA` (`0x1754`)
   - CV32 writes `popcount(mask)` to `PULP_NB_CORES_TO_WAIT` (`0x1748`)
   - CV32 writes one-hot `core_mask` to `PULP_START` (`0x1758`)
   - `ClusterRegs` fires a 1-cycle MEI edge pulse to each selected core; deasserts after 1 clock cycle
   - Each woken core: reads TASKBIN and DATA → writes `0` to PULP_START (ACK) → calls task → writes `1` to PULP_DONE
   - `ClusterRegs`: last ACK clears `PULP_START` → CV32 exits `while (PULP_START != 0)`
   - `ClusterRegs`: last DONE fires `pulp_done_irq` → CV32 Event Unit wakes `eu_pulp_wait()`
   - All woken cores `mret` back to `dispatcher_loop` (WFI), ready for the next dispatch

### GVSoC Ports (`ClusterRegs`)

- **`input`** (slave, IO) — MMIO register access
- **`spatz_clock_en`** (master, `wire<bool>`) — drives Spatz clock enable
- **`spatz_start_irq`** (master, `wire<bool>`) — asserts Spatz start interrupt (1-cycle pulse)
- **`spatz_done_irq`** (master, `wire<bool>`) — pulses when Spatz done
- **`pulp_clock_en`** (master, `wire<bool>`) — single broadcast wire; fan-out to all PULP cores' fetch-enable simultaneously
- **`pulp_start_irq_0` … `pulp_start_irq_N-1`** (master, `wire<bool>`) — one port per PULP core; each fires a 1-cycle edge pulse when the corresponding bit of `PULP_START` is set
- **`pulp_done_irq`** (master, `wire<bool>`) — pulses when all `PULP_NB_CORES_TO_WAIT` DONE writes received; wired to `in_event_12_pe_0` of the CV32 Event Unit
- **`pulp_entry`** (master, `wire<uint64_t>`) — PULP binary entry point, driven from `PULP_BINARY` write

### Configuration Property

- **`nb_pulp_cores`** — number of PULP cores (`CV32PulpCore` instances) and `pulp_start_irq_i` ports to allocate; set via the `nb_pulp_cores` **target parameter** (same string at platform build and at run) and it must match the `pulp_cores` value at SDK build time

---

## Fractal Synchronization Network

MAGIA v3 implements a **hierarchical fractal synchronization tree**:

- Level 0: tile-to-fractal connections
- Intermediate levels: aggregation and propagation
- Root level: global synchronization

This allows:
- Fast barrier-like synchronization
- Scalable coordination across large meshes
- Explicit modeling of sync latency and topology

---

## Memory Map (Per Tile – Simplified)

Exact addresses are defined in `arch.py`.

- **`0x0100`** RedMulE CTRL — accelerator control
- **`0x0200`** iDMA CTRL — DMA control
- **`0x0600`** FSYNC CTRL — fractal sync control
- **`0x0700`** Event Unit — interrupts and events
- **`0x1700`** Cluster CTRL — cluster control regs (Spatz + PULP)
- Stack — tile-relative, per-core stack area
- L1 (TCDM) — tile-relative, tile private scratchpad
- **`0xC000_0000`** L2 — shared memory via NoC
- **`0xFFFF_0004`** STDOUT — simulation UART
- TEST_END — simulation termination (L2 end + 1)

---

## Configuration and Extension Points

### Architecture Parameters (`arch.py`)

- **`N_TILES_X`** (default `1`) — tile grid width; **default** for the `n_tiles_x` target parameter
- **`N_TILES_Y`** (default `1`) — tile grid height; **default** for the `n_tiles_y` target parameter
- **`NB_PULP_CORES`** (default `8`) — PULP cores per cluster; **default** for the `nb_pulp_cores` target parameter
- **`TILE_CLK_FREQ`** (default `200 MHz`) — tile clock frequency
- **`SPATZ_ENABLE`** (default `True`) — enable Snitch+Spatz vector core per tile
- **`PULP_ENABLE`** (default `True`) — enable PULP multi-core cluster per tile
- **`ENABLE_PCIE_VFIO`** (default `False`) — enable PCIe VFIO bridge for QEMU co-simulation (not available yet on the io_v2 platform)

> `N_TILES_X`/`N_TILES_Y`/`NB_PULP_CORES` are only the **build-time defaults**.
> To run a different shape, override them in the target string at both build and
> run — e.g. `--target=magia_v3:n_tiles_x=2,n_tiles_y=2,nb_pulp_cores=4` — not
> with `--attr` (see the Build section for why).

Memory sizes, latencies, and DSE parameters are defined in `MagiaArch` and `MagiaDSE`.

### Easy Extensions

You can extend MAGIA v3 by:
- Adding a new accelerator inside `MagiaV3Tile`
- Adding new MMIO controllers in `ClusterRegs`
- Changing NoC topology or parameters
- Modifying Fractal Sync behavior
- Tuning latency / bandwidth parameters

The design intentionally keeps **clear separation** between:
- Architecture description (`arch.py`)
- SoC composition (`soc.py`)
- Tile micro-architecture (`tile.py`)
- Cluster register model (`cluster_regs/`)

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
pulp/pulp/chips/magia_v3/
├── arch.py                        # Architecture constants and parameters
├── board.py                       # GVSoC board entry point
├── soc.py                         # SoC composition (tiles, NoC, L2)
├── tile.py                        # Tile micro-architecture
├── icache.py                      # Two-level instruction cache
├── cluster_regs/                  # Cluster control registers (Spatz + PULP)
│   ├── cluster_regs.cpp           # C++ model implementation
│   └── cluster_regs.py            # Python systree binding
├── ctrl_core/                     # CV32E40P control core
│   └── core.py                    # CV32CtrlCore class
├── pulp_core/                     # CV32E40P PULP worker cores
│   └── core.py                    # CV32PulpCore class
├── idma_mm_ctrl/                  # Decoder of the two iDMA channel windows
├── fractal_sync/                  # Fractal synchronization module
├── kill_module/                   # Simulation termination module
│   ├── kill_module.py
│   └── kill_module.cpp
└── README.md

pulp/pulp/pcie_vfio_bridge/
├── CMakeLists.txt
├── pcie_vfio_mem_bridge.cpp       # C++ model implementation
└── pcie_vfio_mem_bridge.py        # Python systree binding
```

---

## Summary

MAGIA v3 is a **scalable, realistic, and extensible** GVSoC platform designed for:

- Research on tiled AI architectures with heterogeneous compute
- Memory hierarchy exploration
- Synchronization mechanisms
- Accelerator / DMA co-design
- PULP multi-core cluster offload from a CV32 controller
- Optional Snitch+Spatz vector processing
- QEMU co-simulation via PCIe VFIO bridge

It trades simplicity for **explicitness**: everything is visible, configurable, and hackable.

Happy hacking
