# CV32E40P testbench examples

Test programs of the CV32E40P UVM testbench of
[core-v-verif](https://github.com/openhwgroup/core-v-verif) (`cv32e40p/tests/programs/custom/`),
run on the `cv32e40p_testbench` target. The target has the memory map of that testbench, so the
programs run unchanged: they print through the virtual peripheral at `0x10000000` and report their
end at `0x20000000` (test status) or `0x20000004` (exit). A run passes when the program reports a
pass or exits with 0. As in the testbench, the memory outside the program and the virtual
peripherals reads 0 until written.

Each configuration is a target of its own, built from the root of gvsoc with the others, for example
`make build TARGETS="cv32e40p_testbench;cv32e40p_testbench:config.soc/corev_pulp=true:config.soc/fpu=true"`.
GVSOC runs in the work directory, so the binary is given with its full path:

```
gvrun --target cv32e40p_testbench --work-dir hello --param soc/binary=$PWD/hello-world.elf run
gvrun --target cv32e40p_testbench:config.soc/corev_pulp=true:config.soc/fpu=true \
      --work-dir post_inc --param soc/binary=$PWD/pulp_post_increment_load_store.elf run
```

`gvtest --target cv32e40p_testbench run` runs all of them, each on the configuration in the table below.

The RTL parameters of the core are fields of the SoC configuration: `fpu`, `zfinx`, `corev_pulp`,
`corev_cluster` and `num_mhpmcounters`. The `soc/mtvec_addr` parameter drives the `mtvec_addr_i`
input of the core, like the `+mtvec_addr` plusarg of the testbench.

| Program | Built for (core-v-verif configuration) | Run on |
|---|---|---|
| `hello-world.elf` | `default` | `cv32e40p_testbench` |
| `misalign.elf` | `default` | `cv32e40p_testbench` |
| `riscv_ebreak_test_0.elf` | `default` | `cv32e40p_testbench` |
| `hpmcounter_basic_test.elf` | `default` | `cv32e40p_testbench` |
| `mhpmcounter29_csr_access_test_1.elf` | `num_mhpmcounter_29` | `num_mhpmcounters=29` |
| `hello-world_pulp_fpu.elf` | `pulp_fpu` | `corev_pulp`, `fpu` |
| `pulp_post_increment_load_store.elf` | `pulp` | `corev_pulp`, `fpu` |
| `pulp_cluster_elw_test.elf` | `pulp_cluster` | `corev_pulp`, `corev_cluster`, `fpu`, `zfinx` |
| `zfinx_func_cov_improve_test.elf` | `pulp_cluster_fpu_zfinx` | `corev_pulp`, `corev_cluster`, `fpu`, `zfinx` |

The programs were built by the core-v-verif makefiles (`make test TEST=<program>
CFG=<configuration>`, with the `-march` of the configuration yaml) with the CORE-V GCC 14.1.0
toolchain (`corev-openhw-gcc-modded-v0.1`). The sources are in core-v-verif `cv32e40p/dev`
(57fbb288), except `pulp_cluster_elw_test`, which is part of a pending core-v-verif contribution.
The same binaries pass in the UVM testbench, where every instruction is compared with this model.

The target has no source of interrupts, debug requests or bus stalls, so the programs that need
the interrupt timer or the debugger of the testbench do not end here.
