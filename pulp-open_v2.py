# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

# pulp-open built from io_v2 components and iss_v2 cores (see
# pulp/chips/pulp_open_v2/pulp_open_v2.py).
#
# With --platform rtl, the board is the RTL of PULP with its testbench (see
# pulp/chips/pulp_open_v2/rtl.py), run by the RTL simulator of --rtl-simulator:
# gvsoc_svsim (default), as a GVSoC component, or questa, as a vsim process.

import argparse
import os

import gvrun.target
import gvsoc.runner
from pulp.chips.pulp_open_v2.pulp_open_v2 import PulpOpenV2Board


def _rtl_simulator(args):
    """The RTL simulator if the platform is the RTL one, else None."""
    if args is None or args.platform != 'rtl':
        return None
    simulator = args.rtl_simulator or 'gvsoc_svsim'
    from pulp.chips.pulp_open_v2 import rtl
    if simulator not in rtl.SIMULATORS:
        raise RuntimeError(f'pulp-open_v2 (rtl): unsupported RTL simulator "{simulator}" '
                           f'(supported: {", ".join(rtl.SIMULATORS)})')
    return simulator


class Target(gvsoc.runner.Target):

    gapy_description = "Pulp-open virtual board (io_v2 and iss_v2)"
    model = PulpOpenV2Board
    name = "pulp-open_v2"

    def __init__(self, parser, options=None, name=None):
        # --platform is registered by gvrun's own parser and the base class
        # parses the command line during __init__, so peek at it to pick the
        # board.
        peek = argparse.ArgumentParser(add_help=False)
        peek.add_argument('--platform', default=None)
        peek_args, _ = peek.parse_known_args()
        model = PulpOpenV2Board
        if peek_args.platform == 'rtl':
            from pulp.chips.pulp_open_v2 import rtl
            model = rtl.make_board()
        else:
            # The build lists the components on the gvsoc platform: have it
            # compile the model which runs the RTL platform too.
            import utils.verilator
            utils.verilator.declare()

        super(Target, self).__init__(parser, options,
            model=model, name=name)

        simulator = _rtl_simulator(self.args)
        if simulator is not None:
            self.model.work_dir = os.path.abspath(self.args.work_dir or os.getcwd())
        if simulator == 'gvsoc_svsim':
            from pulp.chips.pulp_open_v2 import rtl
            plugin = rtl.svsim_plugin()
            if plugin is None or not os.path.exists(plugin):
                raise RuntimeError(
                    'pulp-open_v2 (rtl, gvsoc_svsim): gvsoc_svsim plugin not found (set GVSOC_SVSIM_PLUGIN, '
                    'or build the SDK with tools/hdlsim: <install>/lib/libgvsoc_elab.so)')
            design = self.model.get_parameter('design') or rtl.svsim_design()
            if not os.path.exists(os.path.join(design, 'manifest')):
                raise RuntimeError(f'pulp-open_v2 (rtl, gvsoc_svsim): no compiled design at {design}; '
                                   'build it with: make test.build.pulp-rtl')
            self.model.set_parameter('plugin_path', plugin)
            self.model.set_parameter('design', design)
        elif simulator == 'questa':
            from pulp.chips.pulp_open_v2 import rtl
            self.register_runner('rtl', rtl.QuestaRunner(self))

    # With Questa the simulation is an external process: commands go to the
    # registered runner rather than to the GVSoC engine.

    def run(self, args=None):
        args_ = args if args is not None else getattr(self, 'args', None)
        simulator = _rtl_simulator(args_)
        if simulator == 'questa':
            return gvrun.target.Target.run(self, args)
        if simulator == 'gvsoc_svsim':
            self.model.prepare_inputs()
        if args is None:
            return super().run()
        return super().run(args)

    def handle_command(self, command, args=None):
        if _rtl_simulator(args if args is not None else getattr(self, 'args', None)) == 'questa':
            return gvrun.target.Target.handle_command(self, command, args)
        if args is None:
            return super().handle_command(command)
        return super().handle_command(command, args)
