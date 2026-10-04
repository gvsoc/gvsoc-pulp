# SoftHier legacy ELF loading

The `pulp.chips.soft_hier_old.flex_cluster` target defaults to direct ELF
initialization:

```sh
SOFTHIER_ARCH_FILE=/path/to/arch.py gvsoc \
  --target=pulp.chips.soft_hier_old.flex_cluster \
  --binary=/path/to/application.elf \
  --preload=/path/to/hbm.elf run
```

`--binary` loads the application into every cluster's local address space,
including instruction memory and any initialized TCDM or stack segments.
`--preload` loads a single ELF into the global data address space, typically HBM.
It is optional. Both paths use the physical address (`p_paddr`) of each ELF
`PT_LOAD` segment, copy its file bytes, and zero the rest of `p_memsz`.
ELF32 and ELF64 are supported, including segments containing only zero-filled
data and HBM addresses above 4 GiB. The application must retain its `_start`
symbol, as required by the cluster model's existing boot-address lookup.

Direct initialization writes the backing storage during reset release, at cycle
0. Completion signals release the cores on the first clock edge (cycle 1), after
reset has propagated. Loading does not create timed bus/NoC/DRAM transactions,
occupy queues, warm DRAM rows, or charge memory-access dynamic energy. Startup
cost is independent of ELF size; copying the data still takes host CPU time.

Address decoding comes from the instantiated architecture. The untimed path
preserves router offsets, TCDM bank interleaving, HBM edge/node/controller
selection, aliases, channel interleaving, and XOR/reduced scrambling. It does
not maintain a separate hard-coded memory map. DRAMSys must have storage enabled
(`StoreMode: Store`) and provide `dram_preload_byte`, as the patched DRAMSys
library does. Each SoftHier DRAMSys instance represents one channel; the HBM
controller performs channel selection before the storage write. ELF addresses must fit both the architecture mappings and the
backing DRAM configuration.

For the previous loading behavior, append `--preload-mode=timed`. An architecture
preset may also set `self.preload_mode = 'timed'`; the CLI overrides that setting.
The generic `utils.loader.ElfLoader` remains timed by default for other targets.

To inspect initialization and release cycles, add `--trace=loader
--trace=ctrl_registers`. Direct loaders print the number of initialized bytes
and their completion cycle. Rebuild both the `core` models and `pulp` models
after updating these sources.

## Regression

With the SoftHier target built and the usual GVSOC/SystemC/DRAMSys environment
set, run from the GVSOC repository root:

```sh
python3 pulp/tests/soft_hier_old/preload/test_preload.py \
  --cc /path/to/riscv32-unknown-elf-gcc \
  --build-dir build/preload-regression
```

The test builds a small bare-metal checker and generates ELF32/ELF64 fixtures.
It verifies all cluster copies, relocated instruction/TCDM/stack bases,
different bank widths and counts, all four HBM edges, controller multiplexing,
aliases above 32 bits, XOR and reduced scrambling, segments crossing bank/node
boundaries, remote TCDM initialization, BSS, and startup without an HBM ELF.
It also checks that invalid destinations and malformed ELF files fail, and
asserts that every direct initialization completes at cycle 0. Logs and
generated architecture files remain in the requested build directory.

## SDK evaluation

Evaluated `softhier-sdk` branch `soft_hier_old`, commit
`1244fdbc34977aff5a6a10ead079053fb5d31d00`, with its implementation architecture:
4 × 4 clusters, 5 cores per cluster, 4 Spatz cores, a 1024-bit NoC, and
west/south HBM placement with aliases and XOR scrambling. All three kernels
used the SDK's default 512 × 512 FP16 dimensions (GEMM K = 512).

| Kernel | HBM ELF data | Timed HBM-ready cycle | Direct HBM-ready cycle |
| --- | ---: | ---: | ---: |
| SummaGEMM | 2 MiB | 16,500 | 1 |
| RMSNorm | 1.5 MiB | 12,600 | 1 |
| Activation (SiLU) | 2.5 MiB | 20,700 | 1 |

Each application finished successfully in both modes; all 262,144 output FP16
values matched byte for byte. GEMM also matched the independent expected value
64.0 at every output position. RMSNorm and Activation were checked for
equivalence between loading modes, not against a mathematical reference.
Post-start kernel timings can differ because direct loading leaves DRAM rows
and queues untouched.

The SDK preload generators currently emit small placeholder arrays. This
evaluation used full matrices at the generated ELF addresses: 0.5 inputs,
0.25 GEMM weights, and zero-initialized outputs. Output dumps were added after
the SDK's measured execution region. The kernel algorithms were unchanged.

The bundled DRAMSys binary did not return the memory specification required by
the current wrapper. Evaluation used DRAMSys commit `8565f18` rebuilt with
`add_dramsyslib_patches/build_dynlib_from_github_dramsys5/patch`. Run-local HBM
address mapping was adjusted to the configured 32-bit pseudo-channel width;
storage was enabled and database recording disabled equally in both modes.
The loader now reports an incompatible memory specification instead of hanging.

In the evaluated checkout, `build/direct_preload/RESULTS.md` contains the setup
and rerun commands. `build/direct_preload/env.sh` selects the rebuilt simulator,
DRAMSys library, and configs; source it from the GVSOC repository root before
running. SDK sources, generated workloads, dependencies, and logs are all inside
the repository and are not part of the source change.
