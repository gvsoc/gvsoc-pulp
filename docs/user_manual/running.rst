Running a target
================

Every PULP target is launched with ``gvrun``, selecting the platform with
``--target`` and pointing it at the application binary with a ``--param``
override. The general shape is::

    gvrun --target <target> --work-dir <dir> --param <soc>/binary=<elf> run

where

- ``--target`` is the target name (see :doc:`targets`);
- ``--work-dir`` is a directory where the generated platform and run
  artifacts are placed (one per run is recommended);
- ``--param <soc>/binary=<elf>`` loads ``<elf>`` into the target. The
  property prefix depends on the platform's system-tree layout —
  ``chip/soc/binary`` for chips with a ``chip`` wrapper (PULP-open, Snitch,
  Occamy, Siracusa, ...), ``soc/binary`` for the flat SoCs (rv64,
  single Snitch cluster), and ``mempool_soc/binary`` for MemPool. The exact
  prefix for each target is given in :doc:`targets`;
- ``run`` is the command that actually simulates. Some platforms first need
  a flash image to be built, in which case the command sequence is
  ``image flash run`` (e.g. PULP-open).

The target must have been built first. Targets are built from the SDK root
with::

    make build TARGETS="pulp-open;snitch;spatz"

See the top-level ``README``/``Makefile`` for the full build flow.

Running the bundled examples
----------------------------

The ``gvsoc/pulp/examples`` directory ships a set of prebuilt applications,
one or more per target. Each one is run with the ``gvrun`` command given
alongside its target in :doc:`targets` — for example::

    gvrun --target pulp-open --work-dir pulp-open \
        --param chip/soc/binary=pulp/examples/pulp-open/hello image flash run
