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

.. admonition:: Task - A.6 Add a GDB server
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

The goal of this step is to write a brand-new component from scratch, add it
to the system from section A, and access it from the binary.
``my_system.py`` is already provided, and ``main.c`` has been changed to access 
the dedicated region we want to redirect to our component:

.. code-block:: c

   printf("Hello, got 0x%x from my comp\n", *(uint32_t *)0x20000000);

.. admonition:: Task - B.1 Declare the component skeleton
   :class: task

   Create ``my_comp.py``:

   .. code-block:: python

      import gvsoc.systree

      class MyComp(gvsoc.systree.Component):

          def __init__(self, parent: gvsoc.systree.Component, name: str, value: int):
              super().__init__(parent, name)

   Every Python generator takes ``parent`` and ``name`` and hands them to the
   base class - this is how a component knows where it sits in the tree.
   ``value`` is our own parameter: whoever instantiates ``MyComp`` will choose
   what it reads back.

.. admonition:: Task - B.2 Register the C++ source and the value as a property
   :class: task

   .. code-block:: python

      self.add_sources(['my_comp.cpp'])

      self.add_properties({
          "value": value
      })

   ``add_sources`` is what triggers compilation of our component into a
   loadable library. ``add_properties`` puts ``value`` into the component's 
   JSON configuration, which is how the C++ files will be able to read it back.

.. admonition:: Task - B.3 Add the input port method
   :class: task

   .. code-block:: python

      def i_INPUT(self) -> gvsoc.systree.SlaveItf:
          return gvsoc.systree.SlaveItf(self, 'input', signature='io')

   The port name (``'input'``) must match the one declared on the C++ side.
   ``signature='io'`` is just information for the framework, so it can check
   that two ports being bound together are of the same kind.

.. admonition:: Task - B.4 Declare the C++ class
   :class: task

   Create ``my_comp.cpp``:

   .. code-block:: cpp

      #include <vp/vp.hpp>
      #include <vp/itf/io.hpp>

      class MyComp : public vp::Component
      {
      public:
          MyComp(vp::ComponentConf &config);

      private:
          static vp::IoReqStatus handle_req(vp::Block *__this, vp::IoReq *req);

          vp::IoSlave input_itf;
          uint32_t value;
      };

      MyComp::MyComp(vp::ComponentConf &config)
          : vp::Component(config)
      {
          this->input_itf.set_req_meth(&MyComp::handle_req);
          this->new_slave_port("input", &this->input_itf);

          this->value = this->get_js_config()->get_child_int("value");
      }

      extern "C" vp::Component *gv_new(vp::ComponentConf &config)
      {
          return new MyComp(config);
      }

   ``gv_new`` is the factory function the framework calls to instantiate our
   class once the shared library is loaded. The constructor registers
   ``input_itf`` as the port named ``"input"`` (matching ``i_INPUT()``), and
   reads ``value`` back out of the JSON configuration that ``add_properties``
   wrote on the Python side.

.. admonition:: Task - B.5 Implement the request handler
   :class: task

   .. code-block:: cpp

      vp::IoReqStatus MyComp::handle_req(vp::Block *__this, vp::IoReq *req)
      {
          MyComp *_this = (MyComp *)__this;

          printf("Received request at offset 0x%lx, size 0x%lx, is_write %d\n",
              req->get_addr(), req->get_size(), req->get_is_write());
          if (!req->get_is_write() && req->get_addr() == 0 && req->get_size() == 4)
          {
              *(uint32_t *)req->get_data() = _this->value;
          }
          return vp::IO_REQ_OK;
      }

   The handler is ``static`` (a plain function pointer),
   so the cast back to ``MyComp*`` is needed to reach instance state. On a
   4-byte read at offset 0, it writes ``value`` into the request's data
   buffer.

.. admonition:: Task - B.6 Wire the component into my_system.py
   :class: task

   .. code-block:: python

      import my_comp

      comp = my_comp.MyComp(self, 'my_comp', value=0x12345678)
      ico.o_MAP(comp.i_INPUT(), 'comp', base=0x20000000, size=0x00001000, rm_base=True)

   After this, we can now compile and run the application.

.. admonition:: Verify - B
   :class: solution

   .. code-block:: bash

      $ make gvsoc all
      $ make run

   Expected output:

   .. code-block:: text

      Received request at offset 0x0, size 0x4, is_write 0
      Hello, got 0x12345678 from my comp

   If ``my_comp.py``/``my_comp.cpp`` do not match, the finished versions are
   under ``solution/``.

C - Add traces and a VCD signal
.........................................
Folder: ``C_add_traces_and_vcd``

In this step we will show how to add a trace and a VCD signal to the component,
so we can see what it is doing at runtime in the trace log and in GTKWave.
``main.c`` already writes the values 0-19 to the component (in addition to
its earlier read), showing the changes in the VCD signal.

.. admonition:: Task - C.1 Declare and register the trace
   :class: task

   .. code-block:: cpp

      vp::Trace trace;

   .. code-block:: cpp

      this->traces.new_trace("trace", &this->trace);

   The trace is declared as a member, then activated and named in the
   constructor. This name is what shows up in the trace path when it is
   dumped, and what you select on the command line.

.. admonition:: Task - C.2 Use the trace in the request handler
   :class: task

   .. code-block:: cpp

      _this->trace.msg(vp::TraceLevel::DEBUG, "Received request at offset 0x%lx, size 0x%lx, is_write %d\n",
          req->get_addr(), req->get_size(), req->get_is_write());

   A trace can be displayed using the function ``msg()``
   with a trace level (``DEBUG``, ``INFO``, ``WARNING``, ``ERROR``) and a
   ``printf``-style format string. When printed, the trace will be prefixed with the 
   timestamp and the component name.

.. admonition:: Task - C.3 Declare the VCD signal
   :class: task

   .. code-block:: cpp

      vp::Signal<uint32_t> vcd_value;

   .. code-block:: cpp

      MyComp::MyComp(vp::ComponentConf &config)
          : vp::Component(config), vcd_value(*this, "status", 32)

   The VCD signal's template type must be at least as wide as the signal itself.
   The constructor initializer gives it the name (``"status"``) and width
   (32 bits) shown in the VCD viewer and used to enable it from the command line.

.. admonition:: Task - C.4 Set or release the signal in the handler
   :class: task

   .. code-block:: cpp

      if (!req->get_is_write())
      {
          *(uint32_t *)req->get_data() = _this->value;
      }
      else
      {
          uint32_t value = *(uint32_t *)req->get_data();
          if (value == 5)
          {
              _this->vcd_value.release();
          }
          else
          {
              _this->vcd_value.set(value);
          }
      }

   ``set()`` is what the viewer sees change over time; ``release()`` shows
   the signal as high-impedance - useful for representing idleness.

.. admonition:: Verify - C
   :class: solution

   .. code-block:: bash

      $ make gvsoc all

   Check the trace, optionally combined with instruction tracing to see where
   the access happens:

   .. code-block:: bash

      $ make run runner_args="--trace=my_comp --trace=insn"

   Then check the VCD signal:

   .. code-block:: bash

      $ make run runner_args="--vcd --event=.*"
      $ gtkwave <work-dir>/view.gtkw
   The signal is  visible in GTKWave, found from the SST pane under ``soc -> my_comp``.
   As you can see, initially the signal is uninitialized ('X') until 
   writes from 0 to 19. When the value 5 is written, the signal is released and 
   shows as high-impedance ('Z').

D - The IO request interface, sync vs async
......................................................
Folder: ``D_io_request_interface``

Memory transactions are modeled as a request/response pair. To handle this,
the components implementa the IO interface. We will see different ways to handle
the incoming requests.

.. admonition:: Task - D.1 Add synchronous latency
   :class: task

   .. code-block:: cpp

      if (req->get_addr() == 0)
      {
          *(uint32_t *)req->get_data() = _this->value;
          req->inc_latency(1000);
          return vp::IO_REQ_OK;
      }

   First, if a request is received at address 0, we respond synchronously: 
   the master gets its response in the same
   function call. ``inc_latency`` tells
   the master the response took 1000 extra cycles, which is what actually
   stalls the core's pipeline. Latency added this way accumulates with
   whatever else is added further along the path from initiator to target.

.. admonition:: Task - D.2 Declare the event and pending-request state
   :class: task

   .. code-block:: cpp

      vp::ClockEvent event;
      ...
      vp::IoReq *pending_req;

   .. code-block:: cpp

      MyComp::MyComp(vp::ComponentConf &config)
          : vp::Component(config), event(this, MyComp::handle_event)

   Let's now add the ``ClockEvent`` that will be used to schedule the asynchronous 
   reply, and a pointer to the pending request. The event is initialized with a callback to
   ``handle_event`` (written next).

.. admonition:: Task - D.3 Handle the second register asynchronously
   :class: task

   .. code-block:: cpp

      else if (req->get_addr() == 4)
      {
          _this->pending_req = req;
          _this->event.enqueue(2000);
          return vp::IO_REQ_PENDING;
      }

   Instead of replying, we store the request as pending, schedule the event 2000 cycles
   out, and return ``vp::IO_REQ_PENDING`` to tell the initiator that we will reply later.

.. admonition:: Task - D.4 Implement the event handler
   :class: task

   .. code-block:: cpp

      void MyComp::handle_event(vp::Block *__this, vp::ClockEvent *event)
      {
          MyComp *_this = (MyComp *)__this;

          *(uint32_t *)_this->pending_req->get_data() = _this->value;
          _this->pending_req->get_resp_port()->resp(_this->pending_req);
      }

   In the event handler, we write the value into the pending request's data buffer, and
   call ``resp()`` on the request's response port to complete it. 

.. admonition:: Verify - D
   :class: solution

   .. code-block:: bash

      $ make gvsoc all
      $ make run runner_args="--trace=my_comp --trace=insn"

   You should see the offset-0 read stall for 1000 cycles before
   the next instruction retires, and the offset-4 read stall for 2000
   cycles with the response arriving from ``handle_event`` rather than
   inline. In practice, real async replies are typically a chain of
   callbacks across several components before the initiator finally sees its
   response.


E - Add power sources
...............................
Folder: ``E_add_power_sources``

Power modeling is based on power sources: any number of them can be
instantiated in a component to represent something in it comsuming power. This
step extends ``MyComp`` with power models representingstatic leakage, idle dynamic
consumption, and access-driven dynamic power.

.. admonition:: Task - E.1 Declare two power sources
   :class: task

   .. code-block:: cpp

      vp::PowerSource access_power;
      vp::PowerSource background_power;

   ``background_power`` will model power that does not depend on how many
   accesses happen - leakage (static) and idle dynamic (toggling) power.
   ``access_power`` will model the dynamic energy spent per access.

.. admonition:: Task - E.2 Register them and start leakage and background power
   :class: task

   .. code-block:: cpp

      this->power.new_power_source("leakage", &background_power, this->get_js_config()->get("**/background_power"));
      this->power.new_power_source("access", &access_power, this->get_js_config()->get("**/access_power"));

      this->background_power.leakage_power_start();
      this->background_power.dynamic_power_start();

   Both power sources are started unconditionally in the constructor.

.. admonition:: Task - E.3 Feed power values from Python
   :class: task

   .. code-block:: python

      self.add_properties({
          "background_power": {
              "dynamic": {"type": "linear", "unit": "W",  "values": {"25": {"1.2": {"any": 0.00050}}}},
              "leakage": {"type": "linear", "unit": "W",  "values": {"25": {"1.2": {"any": 0.00010}}}},
          },
          "access_power": {
              "dynamic": {"type": "linear", "unit": "pJ", "values": {"25": {"1.2": {"any": 10.0}}}}
          }
      })

   Values are given at one temperature (25°C) and one voltage (1.2V, the
   engine's default).

.. admonition:: Task - E.4 Account access energy
   :class: task

   .. code-block:: cpp

      _this->access_power.account_energy_quantum();

   Added at the top of ``handle_req``. Whenever a request is received, the access
   power source is told to account for the energy consumed by that access.

.. admonition:: Verify - E
   :class: solution

   .. code-block:: bash

      $ make gvsoc all
      $ make run runner_args="--power"

   The engine will dump a single report covering the whole run at the end of the
   simulation, in
   ``$GVSOC_ROOT/build_flooforge/E_add_power_sources/work/power_report.csv``. 
   Each line has the shape
   ``Trace path; Dynamic power (W); Leakage power (W); Total (W); Percentage``.

   To see it as a VCD trace instead:

   .. code-block:: bash

      $ make run runner_args="--power --vcd --event=.*"


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
