## Getting Started

1. Navigate to the root directory of the GVSOC repository and add the following line to your `Makefile`:

   ```makefile
   include pulp/pulp/chips/softhier/softhier.mk
   ```

2. From the root directory, run the following command to install the required toolchains:

   ```bash
   source pulp/pulp/chips/softhier/softhier_init.sh
   ```

3. Ensure that the environment variables `CC`, `CXX`, and `CMAKE` are correctly set.

4. Compile the SoftHier hardware:

   ```bash
   make sh-hw
   ```

5. Compile the SoftHier software:

   ```bash
   make sh-sw
   ```

   The default application is located at:

   ```
   pulp/pulp/chips/softhier/common/sw/app_example
   ```

6. Run the simulation:

   ```bash
   make sh-run
   ```

7. Chips with specific topologies can be called as follows:

   ```bash
      make sh-hw TOPOLOGY=[topology]
      make sh-sw TOPOLOGY=[topology]
      make sh-run TOPOLOGY=[topology]
   ```

   Where `[topology]` can be `2d_mesh` (the default, used when `TOPOLOGY=` is
   omitted), `2d_torus`, `3d_mesh`, `3d_torus`, `ring`, `hierarchical_ring`,
   `hexamesh` or `folded_hexatorus`.

## Topology configuration

Every topology's parameters (cluster count, dimensions, link latencies, ...)
are defined as a class in
[`softhier_arch_base.py`](softhier_arch_base.py), keyed by topology name in
its `TOPOLOGIES` dict. At `make sh-config` time,
[`topologies/gen_floogen_topology.py`](topologies/gen_floogen_topology.py)
generates the FlooGen topology (`.floogen.yml`), routing table
(`.routing.yml`) and per-link latencies (`.link_latencies.yml`) for the
selected topology straight from its `TOPOLOGIES` entry, and writes them to
`topologies/generated/`.
The same command also regenerates the C headers consumed by the SoftHier
runtime (`common/sw/runtime/include/softhier_arch.h`/`.inc`).

Three ways to change a topology's parameters:

- **`PARAMS=key=value,key2=value2`** (at `make sh-config`/`sh-sw` time). Example:

  ```bash
  make sh-config TOPOLOGY=2d_torus PARAMS=num_cluster=100,link_latency=2
  ```

- **`--config-opt <component-path>/<property>=<value>`** (at `make sh-run`
  time, via `RUN_ARGS`). Example:

  ```bash
  make sh-run TOPOLOGY=2d_torus RUN_ARGS="--config-opt system/num_cluster=48"
  ```

- **`cfg=<path-to-file>`** (at `make sh-config` time): bypasses the
  `TOPOLOGIES` registry entirely and scrapes arch parameters from a custom
  Python file instead, for a full replacement rather than a few field
  overrides.