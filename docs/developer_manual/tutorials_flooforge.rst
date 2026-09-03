Tutorial - GVSoC and the flexible FlooNoC (FlooForge)
-------------------------------------------------------
This is a tutorial covering the basics of writing GVSoC
components and systems, and building up to modeling a Network-on-Chip based
in the FlooNoC interconnect: first
with the original, fixed-shape FlooNoC 2D-mesh model, then with the flexible
FlooNoC model (FlooForge), whose whole topology 
is generated from a `floogen <https://pypi.org/project/floogen/>`_ YAML
description.

The tutorial files are located here: ``pulp/docs/developer_manual/tutorials/flooforge``

Each folder contains one step of the tutorial, including its own ``Makefile``.
They also contain the solution under ``solution/``.
Before running commands, set ``GVSOC_ROOT``:

.. code-block:: bash

   $ cd pulp/docs/developer_manual/tutorials/flooforge
   $ export GVSOC_ROOT=<path_to_gvsoc>

A - Build a system from scratch
.........................................
Folder: ``A_build_a_system``

The goal of this step is to build, entirely from scratch, a minimal RV64
system made of a core, an interconnect and a memory, and run a "Hello"
binary on it. There is no ``my_system.py`` in this folder yet: writing it is
the exercise. 

A small runtime is available at
``../utils``.

.. admonition:: Information - the Makefile
   :class: explanation

   For every section's ``Makefile``, ``make gvsoc`` builds
   GVSoC itself with our tutorial folder added to its list of modules, so it
   can find ``my_system``:

   .. code-block:: text

      gvsoc:
          make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) ... build

   ``make all`` cross-compiles ``main.c`` against the shared runtime into a
   test binary, and ``make run`` launches GVSoC on it, adding our folder to
   the target search path and passing the binary in as a ``TargetParameter``:

   .. code-block:: text

      run:
          $(FULL_BUILDDIR)/install/bin/gvrun --target-dir=$(CURDIR) --target=my_system \
              --work-dir=$(FULL_BUILDDIR)/work --parameter binary=$(FULL_BUILDDIR)/test/test run $(runner_args)

   Until ``my_system.py`` exists, ``make gvsoc`` will fail.

.. admonition:: Task - A.1 Declare the target
   :class: task

   Create ``my_system.py``. Every gvsoc system is described by a
   ``gvsoc.runner.Target`` subclass, which gapy (GVSoC's runner) looks for by
   convention when it loads the module:

   .. code-block:: python

      import gvsoc.systree
      import gvsoc.runner

      class Target(gvsoc.runner.Target):

          gapy_description = "Minimal RV64 system"
          model = Rv64
          name = "test"

   ``model`` points at the top-level component class we are about to write
   (``Rv64`` below), and ``name`` registers it in gapy's target listing.
   We have not defined ``Rv64`` yet, so we will do that next.

.. admonition:: Task - A.2 Declare the top-level wrapping component
   :class: task

   Every component tree needs a clock source, and the convention is a small
   wrapping component whose only job is to instantiate the real system and
   give it a clock. Start with the bare class and its constructor argument:

   .. code-block:: python

      from gvrun.parameter import TargetParameter

      class Rv64(gvsoc.systree.Component):

          def __init__(self, parent, name=None):
              super().__init__(parent, name)

              binary = TargetParameter(
                  self, name='binary', value=None, description='Binary to be simulated'
              ).get_value()

   ``TargetParameter`` is how a system reads values from the gvrun command
   line. For example, this is used in the Makefile's ``--parameter binary=...`` to 
   pass the path to the test binary down to the ``Rv64`` component.

.. admonition:: Task - A.3 Instantiate the clock generator and connect the system to it
   :class: task

   .. code-block:: python

      import vp.clock_domain

      clock = vp.clock_domain.Clock_domain(self, 'clock', frequency=100000000)
      soc = Soc(self, 'soc', binary)
      clock.o_CLOCK(soc.i_CLOCK())

   A ``Clock_domain`` is itself a component; connecting ``Soc`` underneath it
   makes the clock propagate automatically to every component nested inside
   the SoC. ``Soc`` does not need to declare ``i_CLOCK()`` itself - every
   ``gvsoc.systree.Component`` already exposes a generic clock input port.

.. admonition:: Task - A.4 Declare the SoC class
   :class: task

   .. code-block:: python

      import memory.memory
      import interco.router
      import cpu.iss.riscv
      import utils.loader.loader

      class Soc(gvsoc.systree.Component):

          def __init__(self, parent, name, binary):
              super().__init__(parent, name)

   Bare class, taking the binary path down from ``Rv64``. We fill it in next.

.. admonition:: Task - A.5 Memory, interconnect, core and loader
   :class: task

   .. code-block:: python

      # Main memory
      mem = memory.memory.Memory(self, 'mem', size=0x00100000)

      # Main interconnect
      ico = interco.router.Router(self, 'ico')
      ico.o_MAP(mem.i_INPUT(), 'mem', base=0x00000000, size=0x00100000, rm_base=True)

      # Core
      host = cpu.iss.riscv.Riscv(self, 'host', isa='rv64imafdc', binaries=[binary])
      host.o_FETCH     (ico.i_INPUT    ())
      host.o_DATA      (ico.i_INPUT    ())
      host.o_DATA_DEBUG(ico.i_INPUT    ())

      # ELF loader
      loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)
      loader.o_OUT     (ico.i_INPUT    ())
      loader.o_START   (host.i_FETCHEN ())
      loader.o_ENTRY   (host.i_ENTRY   ())

   We now configure the system, instantiating a 1MB memory, the interconnect between
   the core and the memory, and the core itself.
   * The interconnect's ``o_MAP`` routes any request whose address falls in ``[base, base+size)``
   to the memory; ``rm_base=True`` subtracts ``base`` from the address first,
   so the memory model always sees a local, 0-based offset.
   * The core gets a
   fetch port and a data port, plus a debug data port used by GDB (see
   A.6).
   * GVSoC has no built-in ELF loader, so
   loading is itself modeled as a component. It issues ordinary memory-write
   requests through the interconnect (``o_OUT``), behaving like a real
   bootloader; once done, it drives the core's boot address (``i_ENTRY``) and
   raises its fetch-enable pin (``i_FETCHEN``) so it starts executing.

.. admonition:: Task - A.6 (optional) Add a GDB server
   :class: task

   .. code-block:: python

      import gdbserver.gdbserver

      gdbserver.gdbserver.Gdbserver(self, 'gdbserver')

   This adds an server component which listens for a GDB connection and 
   drives the core's debug port (``i_DATA_DEBUG``) to inspect and control execution. 

.. admonition:: Verify - A
   :class: solution

   .. code-block:: bash

      $ make gvsoc
      $ make all
      $ make run

   You should see ``Hello`` printed. If ``my_system.py`` does not match, the
   finished version is at ``solution/my_system.py``.

   Try instruction tracing:

   .. code-block:: bash

      $ make run runner_args="--trace=insn"

   And GDB, in a second terminal once ``make run runner_args=--gdbserver``
   is waiting for a connection, typing from GVSoC root:

   .. code-block:: text

      $ riscv64-unknown-elf-gdb ./build_flooforge/A_build_a_system/test/test
      (gdb) target remote:12345
      (gdb) break main
      (gdb) c

B - Write a component from scratch
............................................
Folder: ``B_build_a_component``

Author a brand-new component, ``MyComp``, in Python + C++, map it at
``0x20000000``, and have the binary read a fixed value back from it.

.. admonition:: Task - B.1 Write my_comp.py and my_comp.cpp
   :class: task

   - ``my_comp.py``: a ``gvsoc.systree.Component`` taking a ``value``
     property, exposing an ``i_INPUT()`` slave port with signature ``'io'``.
   - ``my_comp.cpp``: a ``vp::Component`` with a ``vp::IoSlave input_itf``
     registered via ``new_slave_port``, and a static ``handle_req`` callback
     that returns ``value`` on any 4-byte read.
   - Wire it into ``my_system.py`` at ``0x20000000`` via
     ``ico.o_MAP(comp.i_INPUT(), 'comp', base=0x20000000, size=0x1000, rm_base=True)``.

   The finished version is under ``solution/``.

.. admonition:: Verify - B.1
   :class: solution

   .. code-block:: bash

      $ cp solution/* .
      $ make gvsoc all
      $ make run

   Expected output: ``Hello, got 0x12345678 from my comp``.

C - Add traces and a VCD signal
.........................................
Folder: ``C_add_traces_and_vcd``

Two quick, additive diffs on top of ``MyComp``.

.. admonition:: Task - C.1 Add a text trace
   :class: task

   Declare a ``vp::Trace trace;`` member, register it with
   ``this->traces.new_trace("trace", &this->trace)``, and replace any
   ``printf`` in ``handle_req`` with
   ``this->trace.msg(vp::TraceLevel::DEBUG, "Received request at offset 0x%lx, size 0x%lx, is_write %d\\n", ...)``.

.. admonition:: Verify - C.1
   :class: solution

   .. code-block:: bash

      $ cp solution_traces/* .
      $ make gvsoc all
      $ make run runner_args=--trace=my_comp

   The trace is selectable by a regex on the component's path, independent of
   the log-level filtering ``printf`` can't give you.

.. admonition:: Task - C.2 Expose a VCD signal
   :class: task

   Add a ``vp::Signal<uint32_t> vcd_value;`` member, initialized as
   ``vcd_value(*this, "status", 32)``. On a write, call ``vcd_value.set(value)``;
   to demonstrate a high-impedance ("unknown") state, call
   ``vcd_value.release()`` when the written value is ``5``. Optionally add a
   ``gen_gtkw`` override in ``my_comp.py`` so the signal auto-appears in
   GTKWave's overview.

.. admonition:: Verify - C.2
   :class: solution

   .. code-block:: bash

      $ cp solution_vcd/* .
      $ make gvsoc all
      $ make run runner_args="--vcd --event=.*"
      $ gtkwave <work-dir>/gtkwave/view.gtkw   # open the SST pane, soc -> my_comp, Append

D - The IO request interface, sync vs async
......................................................
Folder: ``D_io_request_interface``

Contrast synchronous and asynchronous reply handling on the IO request API -
the mechanism you need for any component with realistic, non-zero response
latency.

.. admonition:: Task - D.1 Synchronous latency
   :class: task

   On the register-0 read path, call ``req->inc_latency(1000)`` before
   returning ``vp::IO_REQ_OK``. The reply is still immediate, but it reports
   1000 extra cycles of latency, which stalls the initiator.

.. admonition:: Task - D.2 Asynchronous reply
   :class: task

   Add a second register (offset 4). Store the incoming ``vp::IoReq *`` in a
   ``pending_req`` member, enqueue a ``vp::ClockEvent`` 2000 cycles out, and
   return ``vp::IO_REQ_PENDING`` instead of replying immediately. In the event
   handler, fill in the response data and call
   ``pending_req->get_resp_port()->resp(pending_req)``.

.. admonition:: Verify - D.1/D.2
   :class: solution

   .. code-block:: bash

      $ cp solution/my_comp.cpp .
      $ make gvsoc all
      $ make run

   The Python files are unchanged from section B - only ``my_comp.cpp``
   differs. This is the same pattern real components use for e.g. unknown-
   latency cache refills or bus arbitration: async replies are typically a
   chain of callbacks across several components before the initiator sees
   its response.

E - Add power sources
...............................
Folder: ``E_add_power_sources``

Time-boxed to the essential path: a single access-driven power source and a
terminal power report (skip the background/leakage power-controller wiring
and the VCD power-trace walkthrough if short on time - see ``solution/`` for
the full version, including ``my_comp2.cpp``'s power/voltage controller).

.. admonition:: Task - E.1 Declare an access power source
   :class: task

   In ``my_comp.py``, add an ``access_power`` property (a ``dynamic`` power
   table in pJ per access, keyed by temperature/voltage - see
   ``solution/my_comp.py`` for the JSON shape). In ``my_comp.cpp``, declare
   ``vp::PowerSource access_power;``, register it with
   ``this->power.new_power_source(...)``, and call
   ``access_power.account_energy_quantum()`` on every access in
   ``handle_req``.

.. admonition:: Verify - E.1
   :class: solution

   .. code-block:: bash

      $ cp solution/my_comp.py solution/my_comp.cpp .
      $ make gvsoc all
      $ make run runner_args=--power

   Look for the ``@power.measure_N@value@`` lines in the output and
   ``$GVSOC_ROOT/build_flooforge/E_add_power_sources/work/power_report.csv``
   (gvrun runs the simulation with that work directory as its current
   directory, which is where the power report gets written). If time allows,
   walk through
   ``solution/my_comp2.cpp`` (the power/voltage controller) and
   ``solution/my_system.py`` to show a full OFF / clock-gated / ON / ON-with-
   accesses power sweep across three voltages, and the matching GTKWave power
   traces (``power_vcd.png``, ``power_vcd_with_mem.png`` in this folder).

F - An array of components on the original FlooNoC
.............................................................
Folder: ``F_array_of_components_floonoc``

Move from a single component to a small **array** of them, connected through
GVSoC's original FlooNoC model: a fixed-shape 2D mesh
(``pulp.FlooNoC.FlooNoC.FlooNoCClusterGridNarrowWide``). This section is a
standalone gvsoc system (no RISC-V core/ISS): it is built and run directly.

.. admonition:: Information - the mesh grid
   :class: explanation

   A ``FlooNoCClusterGridNarrowWide(parent, name, wide_width, narrow_width, nb_x_clusters, nb_y_clusters, ...)``
   instantiates an ``(nb_x_clusters+2) x (nb_y_clusters+2)`` grid: one
   interior "cluster" node per ``(x, y)``, plus a one-tile border all around
   for external targets. A cluster injects traffic via
   ``noc.i_CLUSTER_NARROW_INPUT(x, y)``; a target is attached with
   ``noc.o_NARROW_MAP(itf, base, size, x, y, rm_base=True)``.

``my_system.py`` builds a 2x2 array of dummy traffic generators
(``interco.traffic.generator.Generator``) around the mesh, all targeting one
shared ``Memory`` sitting on a border node. Since a ``Generator`` only starts
once told to over its ``wire<TrafficGeneratorConfig>`` control port, a small
custom component, ``driver.cpp``/``driver.py``, fires a fixed dummy write
burst on every generator at reset and polls them until they are all done,
then stops the simulation.

.. admonition:: Task - F.1 Read through the wiring
   :class: task

   Open ``my_system.py``: identify where the mesh is created, where the
   shared memory is mapped onto a border node, and where each generator's
   output is bound to its cluster's input port. Then open ``driver.cpp`` and
   find the ``TrafficGeneratorConfigMaster::start()`` call that kicks each
   generator off.

.. admonition:: Verify - F.1
   :class: solution

   .. code-block:: bash

      $ make gvsoc
      $ make run runner_args=--trace=driver

   You should see the driver announce it started 4 generators, then "All
   generators finished, stopping simulation". Try
   ``make run runner_args="--vcd --event=.*"`` and look at the generators'
   ``req_addr``/``busy`` signals in GTKWave to see the traffic pattern.

G - The flexible FlooNoC: a 3D mesh from a floogen config
....................................................................
Folder: ``G_flexible_floonoc_floogen``

The original FlooNoC's shape (2D mesh, uniform link latency) is hardcoded in
its Python generator. The flexible FlooNoC model,
``pulp.FlooNoC_flex.FlooNoC_flex.FlooNoCFlex``, instead loads its whole
topology from a `floogen <https://pypi.org/project/floogen/>`_ YAML
description - any graph floogen can express, with per-link routing and
timing that a config file controls instead of a hardcoded formula.

.. admonition:: Information - floogen configuration files
   :class: explanation

   Three YAML files describe a flexible-FlooNoC topology:

   - ``<name>.floogen.yml`` - floogen's own schema: routers, network
     interfaces ("endpoints"), connections, and a routing algorithm
     (``XY``/``YX``/``ID``/...).
   - ``<name>.routing.yml`` - explicit ``{src: {dst: next_hop}}`` routing
     tables, used when ``route_algo: ID``.
   - ``<name>.link_latencies.yml`` - a **GVSoC-only side file**,
     ``{src: {dst: latency}}``, because floogen's own schema has no per-link
     timing field. Any link not listed there falls back to a single uniform
     default latency.

   ``gen_topology.py`` in this folder is a trimmed, standalone generator for
   a 3D mesh (adapted from the real one used for the SoftHier chip,
   ``pulp/pulp/chips/softhier/topologies/gen_floogen_topology.py``), extended
   with a **separate Z-axis latency** - e.g. modeling a slower inter-die/TSV
   hop compared to the in-die X/Y mesh links - to show that per-link timing
   is data, not code.

.. admonition:: Task - G.1 Generate the topology and plot it
   :class: task

   .. code-block:: bash

      $ python3 gen_topology.py --dim-x 2 --dim-y 2 --dim-z 2 \
            --link-latency 1 --z-link-latency 8 --out-dir generated --name mesh3d
      $ python3 plot_latency.py --dir generated --name mesh3d

   Open ``generated/mesh3d.png``: the mesh is drawn in 3D, with every link
   colored by its latency and Z-axis links drawn thick. The X/Y links should
   be a uniform color (latency 1) and every Z link should stand out
   (latency 8).

.. admonition:: Task - G.2 Instantiate the flexible NoC
   :class: task

   Open ``my_system.py``. Note the differences from section F:

   - Nodes are addressed by an integer ``node_id``, looked up by generated
     name in ``noc.id_map`` (e.g. ``noc.id_map['cluster_0_0_0_ni']``) rather
     than by ``(x, y)``.
   - ``FlooNoCFlex(..., network_path=..., routing_path=..., link_latencies_path=...)``
     loads and validates the three YAML files at construction time.
   - One cluster, ``(0, 0, 0)``, plays the shared-memory target; every other
     cluster's generator sends dummy traffic to it, so half of them cross the
     slow Z link to get there.

.. admonition:: Verify - G.2
   :class: solution

   .. code-block:: bash

      $ make gvsoc
      $ make run runner_args=--trace=driver

   Same completion message as section F. If time allows, compare a run with
   ``--z-link-latency 1`` (regenerate the topology first) against one with
   ``--z-link-latency 64`` and look at how much longer the generators
   crossing the Z axis take to finish - the flexible model's whole point is
   that this is a config change, not a code change.

Wrap-up
.......
In 30 minutes: a system, a component, traces and a VCD signal, sync/async IO
timing, power modeling, an array of components on a fixed 2D-mesh NoC, and
the same array re-wired onto a flexible, floogen-configured 3D mesh with
per-axis link latency. The natural next step from here is
``pulp/pulp/chips/softhier`` itself, which is exactly this pattern (clusters
+ ``FlooNoCFlex``) at real chip scale.
