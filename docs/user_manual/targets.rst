Targets
=======

This page lists every PULP target modelled in ``gvsoc/pulp``: a short
description, the binary property prefix to use when loading an application,
the run command, and the ready-to-run examples bundled with the target (if
any). All commands assume the target has already been built (see
:doc:`running`).

Each example is run with the ``gvrun`` command shown. The example binary
paths are relative to the SDK's ``gvsoc`` directory (i.e. they start with
``pulp/examples/``).

rv64
----

A 64-bit RISC-V (CVA6-class) application core. Binaries use the flat
``soc/binary`` prefix; a bare-metal payload is typically run through a proxy
kernel passed as ``soc/binary`` with the application as ``soc/args``::

    gvrun --target rv64 --param soc/binary=<pk> --param soc/args=<elf> run

**Example — hello**: a bare-metal application run on top of the RISC-V proxy
kernel (``pk``). ::

    gvrun --target rv64 --param soc/binary=pulp/examples/rv64/pk --param soc/args=pulp/examples/rv64/hello run

rv64_untimed
------------

Functional (untimed) variant of ``rv64`` for fast functional runs. Same
binary prefix (``soc/binary``) as ``rv64``.

**Example — linux**: boots a Linux image from the bundled firmware payload
``spike_fw_payload.elf``. ::

    gvrun --target rv64_untimed --param soc/binary=pulp/examples/rv64/spike_fw_payload.elf run

pulp-open
---------

The PULP-open SoC: a fabric controller (FC) core plus an 8-core cluster with
shared L1, DMA and L2. Binaries are loaded through a flash image, so the run
sequence builds the image first::

    gvrun --target pulp-open --param chip/soc/binary=<elf> image flash run

**Example — hello**: a "Hello from FC" bare-metal application. ::

    gvrun --target pulp-open --param chip/soc/binary=pulp/examples/pulp-open/hello image flash run

pulp-open-nn
------------

PULP-open with the neural-network ISA extensions enabled. Same binary prefix
(``chip/soc/binary``) and run flow as ``pulp-open``. No bundled example.

pulp-open with RedMule
----------------------

The ``pulp-open`` target with the RedMule matrix-multiply accelerator
attached, selected via the attribute qualifier::

    gvrun --target "pulp-open:attr.chip/cluster/has_redmule=true" --param chip/soc/binary=<elf> image flash run

No bundled example.

occamy
------

The Occamy multi-cluster Snitch system. Binary prefix ``chip/soc/binary``::

    gvrun --target occamy --param chip/soc/binary=<elf> run

**Example — occamy**: a multi-cluster offload application. ::

    gvrun --target occamy --param chip/soc/binary=pulp/examples/occamy/offload-multi_cluster.elf run

siracusa
--------

Siracusa: a PULP cluster augmented with the Neureka neural-network
accelerator. Binary prefix ``chip/soc/binary``::

    gvrun --target siracusa --param chip/soc/binary=<elf> run

Building this target requires the xtensor headers — see the module ``README``
for the dependency.

**Example — siracusa**: a Neureka neural-network accelerator test. ::

    gvrun --target siracusa --param chip/soc/binary=pulp/examples/siracusa/neureka_test run

snitch
------

A Snitch compute cluster. Binary prefix ``chip/soc/binary``::

    gvrun --target snitch --param chip/soc/binary=<elf> run

A faster instruction-set simulator can be selected with the ``core_type=fast``
qualifier (``--target snitch:core_type=fast``). The default (slow) core model
will be deprecated soon, so prefer the ``core_type=fast`` variant.

**Example — fp32_computation_vector**: a single-precision vector computation
on the Snitch cluster. ::

    gvrun --target snitch --param chip/soc/binary=pulp/examples/snitch/fp32_computation_vector.elf run

ara / ara_v2
------------

Ara, the CVA6 application core coupled with a RISC-V vector unit. Two models
are available: ``ara`` is the faster, less detailed model, while ``ara_v2``
models the microarchitecture more precisely (more cycle-accurate timing) at
the cost of slower simulation. Use ``ara`` for functional runs and quick
throughput estimates, ``ara_v2`` when accurate timing matters. ``ara`` (v1)
will be deprecated soon in favour of ``ara_v2``. Both use the
``chip/soc/binary`` prefix. No bundled example.

spatz / spatz_v2
----------------

Spatz, a compact RISC-V vector cluster. As with Ara, two models are
available: ``spatz`` is the faster, less detailed model, while ``spatz_v2``
models the microarchitecture more precisely (more cycle-accurate timing) at
the cost of slower simulation — use it when accurate timing matters. ``spatz``
(v1) will be deprecated soon in favour of ``spatz_v2``. Both use the
``chip/soc/binary`` prefix; substitute ``spatz_v2`` for ``spatz`` in any of
the commands below to run on the more precise model. ::

    gvrun --target spatz --param chip/soc/binary=<elf> run

**Example — fconv2d**: a double-precision 2D convolution benchmark from the
Spatz benchmark suite. ::

    gvrun --target spatz --param chip/soc/binary=pulp/examples/spatz/test-spatzBenchmarks-dp-fconv2d_M64_N64_K7 run

**Example — vfadd**: a vector floating-point add test from the RISC-V vector
tests. ::

    gvrun --target spatz --param chip/soc/binary=pulp/examples/spatz/test-riscvTests-vfadd run

pulp.snitch.snitch_cluster_single
---------------------------------

A single Snitch cluster (flat SoC, ``soc/binary`` prefix). This is the
original target for the slow core model and will be deprecated soon. ::

    gvrun --target pulp.snitch.snitch_cluster_single --param soc/binary=<elf> run

**Example — fp32_computation_vector**: a single-precision vector computation
on the single-cluster Snitch target. ::

    gvrun --target pulp.snitch.snitch_cluster_single --param soc/binary=pulp/examples/snitch_cluster_single/fp32_computation_vector.elf run

mempool
-------

MemPool, a many-core cluster with a shared, low-latency L1. Binary prefix
``mempool_soc/binary``::

    gvrun --target mempool --param mempool_soc/binary=<elf> run

**Example — mempool**: a compute test for the MemPool many-core cluster. ::

    gvrun --target mempool --param mempool_soc/binary=pulp/examples/mempool/mempool_test run

chimera
-------

A research SoC target. Runnable with the general ``gvrun --target chimera
... run`` pattern. No bundled example.

snitch_testbench
----------------

A minimal Snitch test-bench target used for model bring-up. No bundled
example.

magia_v2
--------

A many-core target derived from ``pulp-open``. Runnable with the general
``gvrun --target magia_v2 ... run`` pattern. No bundled example.

ri5ky_testbench
---------------

A minimal core test-bench target used for model bring-up. No bundled example.
