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


def _verilator_plugin_path(flavour: str) -> str | None:
    """Default location of the GVSoC verilator plugin .so under
    $RI5KY_HOME/gv_sim. Three flavours are built side-by-side
    (mirrors acu_core_v2).
    """
    home = os.environ.get('RI5KY_HOME')
    if not home:
        return None
    paths = {
        'fast':   ('obj_dir_plugin_fast',   'vlt_ri5ky_gwt_fast.so'),
        'fst':    ('obj_dir_plugin',        'vlt_ri5ky_gwt.so'),
        'inject': ('obj_dir_plugin_inject', 'vlt_ri5ky_gwt_inject.so'),
    }
    sub = paths[flavour]
    return os.path.join(home, 'gv_sim', *sub)


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
                    help='Optional VCD/FST trace output path forwarded to the '
                         'verilator plugin')
            except argparse.ArgumentError:
                pass

        # On --platform rtl, swap the board to a VerilatorControl-backed one
        # that loads the precompiled plugin .so instead of instantiating the
        # GVSoC native model.
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
            if args.gui:
                flavour = 'inject'
            elif args.rtl_trace is not None:
                flavour = 'fst'
            else:
                flavour = 'fast'
            plugin = _verilator_plugin_path(flavour)
            if plugin is None:
                raise RuntimeError(
                    'ri5ky_testbench: $RI5KY_HOME must be set '
                    '(looking for gv_sim/obj_dir_plugin*/vlt_ri5ky_gwt*.so)')
            if not os.path.exists(plugin):
                raise RuntimeError(
                    f'ri5ky_testbench: verilator plugin not found at {plugin}. '
                    f'Build it with: make -C $RI5KY_HOME/gv_sim plugin')
            self.model.set_parameter('plugin_path', plugin)
            if args.rtl_trace is not None:
                self.model.set_parameter('trace_path', args.rtl_trace)

    def run(self, args=None):
        if _is_rtl(args):
            self.model.run_objcopy()
        if args is None:
            return super().run()
        return super().run(args)
