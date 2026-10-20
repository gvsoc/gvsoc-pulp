#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#

import argparse
import os

from gvsoc.systree import Component
import gvsoc.runner

from pulp.ri5ky.ri5ky_testbench_config import Ri5kyTestbenchBoardConfig
from pulp.ri5ky.ri5ky_testbench import Ri5kyTestbenchBoard


def _gvsoc_svsim_design() -> str | None:
    """The testbench compiled for gvsoc_svsim by `make -C $RI5KY_HOME/gv_sim
    gvsoc_svsim` (top gv_sim/ri5ky_svsim_tb.sv)."""
    home = os.environ.get('RI5KY_HOME')
    if not home:
        return None
    return os.path.join(home, 'gv_sim', 'obj_dir_gvsoc_svsim', 'ri5ky_gwt.hsim')


def _is_rtl(args) -> bool:
    return args is not None and args.platform == 'rtl'


class Target(gvsoc.runner.Target):

    gapy_description: str = "Ri5ky testbench"
    model: type[Component] = Ri5kyTestbenchBoard
    name: str = ""
    config: Ri5kyTestbenchBoardConfig = Ri5kyTestbenchBoardConfig("ri5ky_testbench")

    def __init__(self, parser, options=None, **kwargs):
        if parser is not None:
            try:
                parser.add_argument('--rtl-trace', dest='rtl_trace',
                    default=None,
                    help='Optional VCD/FST trace output path of the RTL '
                         'simulation')
            except argparse.ArgumentError:
                pass

        # On --platform rtl, swap the board to a VerilatorControl-backed one
        # that runs the RTL testbench on gvsoc_svsim instead of instantiating
        # the GVSoC native model.
        peek = argparse.ArgumentParser(add_help=False)
        peek.add_argument('--platform', default=None)
        peek.add_argument('--gui', action='store_true', default=False)
        peek_args, _ = peek.parse_known_args()
        if peek_args.platform == 'rtl':
            from functools import partial
            from utils.verilator import VerilatorBoard
            self.model = partial(VerilatorBoard,
                target_name='ri5ky.testbench',
                inject_signals=peek_args.gui)

        super().__init__(parser=parser, options=options, **kwargs)

        args = self.args
        if _is_rtl(args):
            # The RTL runs on gvsoc_svsim, one plugin for every use (GUI,
            # --rtl-trace)
            from utils.verilator import gvsoc_svsim_setup
            gvsoc_svsim_setup(self.model, _gvsoc_svsim_design(), 'ri5ky_testbench',
                              'make -C $RI5KY_HOME/gv_sim gvsoc_svsim ($RI5KY_HOME must be set)')
            if self.model.get_parameter('exit_signal') is None:
                self.model.set_parameter('exit_signal', 'ri5ky_svsim_tb.exit_status')
            if args.rtl_trace is not None:
                self.model.set_parameter('trace_path', args.rtl_trace)

    def run(self, args=None):
        if _is_rtl(args):
            self.model.run_objcopy()
        if args is None:
            return super().run()
        return super().run(args)
