# SoftHier v2

SoftHier v2 is the SoftHier platform (`pulp/pulp/chips/softhier`) built from
io_v2 models. The architecture, the topologies, the software runtime and the
make flow are the same; what changes is the models:

| | SoftHier | SoftHier v2 |
|---|---|---|
| Cores | Snitch + Spatz (iss) | Snitch + Spatz (iss_v2) with the Snitch hardware barrier CSR |
| TCDM | `L1_interleaver` + `DmaInterleaver` | `SpatzTcdmInterco` crossbar, `memory_v3` banks |
| iDMA | `SnitchDma` | `SnitchDmaV2` (io_v2 AXI back-end) |
| Crossbars, memories | io v1 routers, memories | `router_v2`, `memory_v3` |
| NoC | FlooNoC-Flex | FlooNoC v2 graph NoC (`pulp/floonoc_v2`): per-router ID tables, wormhole arbitration |

Differences to be aware of:

- The NoC topology is generated from the arch when the platform is built
  (with the same FlooGen generator, `topologies/gen_floogen_topology.py`),
  so the hardware needs no configuration step and does not depend on the
  current directory.
- The FlooNoC v2 routers use wormhole arbitration with a single virtual
  channel, like the FlooNoC RTL, so routing tables whose channel dependency
  graph has a cycle can deadlock under load. The NoC refuses such tables,
  except for the topologies setting `noc_allow_deadlock` in
  `softhier_arch_base.py` (`3d_torus`, `ring`, `hierarchical_ring`,
  `folded_hexatorus`), which are built with a warning showing the cycle.
- Arch overrides are gvrun target parameters (see below) instead of
  `--config-opt` options.
- The platform runs through gvrun (`make sh2-run`): like the other platforms
  built from typed model configs, it does not run with the legacy `gvsoc`
  launcher.

## Getting Started

1. Navigate to the root directory of the GVSOC repository and add the following line to your `Makefile`:

   ```makefile
   include pulp/pulp/chips/softhier_v2/softhier_v2.mk
   ```

2. From the root directory, run the following command to install the required toolchains:

   ```bash
   source pulp/pulp/chips/softhier_v2/softhier_init.sh
   ```

3. Ensure that the environment variables `CC`, `CXX`, and `CMAKE` are correctly set.

4. Compile the SoftHier hardware:

   ```bash
   make sh2-hw
   ```

5. Compile the SoftHier software:

   ```bash
   make sh2-sw
   ```

   The default application is located at:

   ```
   pulp/pulp/chips/softhier_v2/common/sw/app_example
   ```

   `common/sw/app_verify` checks the data paths of the platform (iDMA,
   remote accesses, vector accesses, atomics) and times a few traffic
   patterns (`make sh2-sw app=pulp/pulp/chips/softhier_v2/common/sw/app_verify`).

6. Run the simulation:

   ```bash
   make sh2-run
   ```

7. Chips with specific topologies can be called as follows:

   ```bash
      make sh2-hw TOPOLOGY=[topology]
      make sh2-sw TOPOLOGY=[topology]
      make sh2-run TOPOLOGY=[topology]
   ```

   Where `[topology]` can be `2d_mesh` (the default, used when `TOPOLOGY=` is
   omitted), `2d_torus`, `3d_mesh`, `3d_torus`, `ring`, `hierarchical_ring`,
   `hexamesh` or `folded_hexatorus`.

## Topology configuration

Every topology's parameters (cluster count, dimensions, link latencies, ...)
are defined as a class in
[`softhier_arch_base.py`](softhier_arch_base.py), keyed by topology name in
its `TOPOLOGIES` dict. When the platform is built,
[`topologies/gen_floogen_topology.py`](topologies/gen_floogen_topology.py)
generates the FlooGen topology, routing table and per-link latencies of the
selected topology from its `TOPOLOGIES` entry, and the NoC is built from
them. `make sh2-config` (run by `make sh2-sw`) generates the C headers
consumed by the SoftHier runtime (`common/sw/runtime/include/softhier_arch.h`/`.inc`).

Ways to change a topology's parameters:

- **`PARAMS=key=value,key2=value2`**, given the same to `sh2-sw`, `sh2-hw`
  and `sh2-run`. The parameters go to the software headers and to the
  platform, as target parameters. Example:

  ```bash
  make sh2-sw sh2-hw sh2-run TOPOLOGY=2d_torus PARAMS=num_cluster=100,link_latency=2
  ```

- **Target parameters**, when running gvrun directly: every arch attribute
  is a parameter of the `system` component, given on the target name:

  ```bash
  gvrun --target pulp.chips.softhier_v2.topologies.softhier_2d_mesh_target:system/num_core_per_cluster=2 ...
  ```

  The same target name must be built (`make TARGETS=<target name> build`),
  since a parameter can change the shape of the platform.

- **`cfg=<path-to-file>`** (at `make sh2-config` time): scrapes the arch
  parameters of the software headers from a custom Python file instead of
  the `TOPOLOGIES` registry. The platform keeps the registry values, so the
  file must not change what the platform depends on.

## PulpOS

Both SoftHier generations can run PulpOS applications (`pulpos/core`,
`python/pulpos/softhier.py` and `arch/softhier`): the boards declare the
`softhier` target and describe their architecture in their attributes. Set
`SOFTHIER_GCC` to the SoftHier toolchain (the `install` directory of
`third_party/toolchain`), then from an application directory:

```bash
gvrun --target pulp.chips.softhier_v2.topologies.softhier_2d_mesh_target build run
```

The same binary runs on every cluster: core 0 of each cluster runs `main`,
or every core with `--parameter <executable>/pulpos/multicore=true`. The
simulation stops once every core finished, with the OR of the statuses
returned by `main` as exit status. `arch/softhier/kernel/softhier.h` gives
the position of the core, the remote TCDM addresses and the cluster and
global barriers; the `.l1` section places buffers in the TCDM.
