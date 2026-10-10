#!/usr/bin/env python3
# Copyright (C) 2026 ETH Zurich and University of Bologna
# SPDX-License-Identifier: Apache-2.0
"""Stacked-channel routing regression; requires a built SoftHier/DRAMSys target."""
import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "preload"))
from test_preload import elf64


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gvsoc", default="gvsoc")
    parser.add_argument("--cc", default="riscv32-unknown-elf-gcc")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    linker = out / "memory.ld"
    linker.write_text('ENTRY(_start)\nSECTIONS { . = 0x80000000; '
                      '.text : { *(.text.start) *(.text*) *(.rodata*) } }\n')
    app = out / "check.elf"
    subprocess.run([args.cc, "-march=rv32im", "-mabi=ilp32", "-O2", "-nostdlib",
                    "-fno-builtin", "-msmall-data-limit=0", "-Wl,--no-relax",
                    f"-T{linker}", str(HERE / "check.c"), "-o", str(app)], check=True)
    regions = []
    for cid in range(4):
        for address, length, seed in [
                (0x10000000000 + cid * 0x1000000 + 0x101, 1293, 19 + cid),
                (0x30000000 + cid * 0x10000 + 0x301, 259, 41 + cid)]:
            regions.append((address, bytes((i * 37 + seed) & 255 for i in range(length)), length))
    regions.append((0x10000000000 + 0x1000000 - 17,
                    bytes((i * 37 + 91) & 255 for i in range(66)), 66))
    preload = out / "preload.elf"
    elf64(preload, regions)
    for side_hbm in (False, True):
        arch = out / f"arch_{int(side_hbm)}.py"
        values = dict(num_cluster_x=2, num_cluster_y=2, num_core_per_cluster=1,
                      cluster_tcdm_size=0x10000, cluster_tcdm_bank_width=32,
                      cluster_tcdm_bank_nb=128, spatz_attaced_core_list=[],
                      redmule_ce_height=32, redmule_ce_width=16, redmule_ce_pipe=1,
                      hbm_chan_placement=[2, 0, 0, 0] if side_hbm else [0, 0, 0, 0],
                      num_node_per_ctrl=2, hbm_node_aliase=2,
                      hbm_node_addr_space=0x1000000,
                      dram3d_enable=1, dram3d_type="hbm2-example.json",
                      dram3d_addr_base=0x10000000000, dram3d_node_space=0x1000000)
        arch.write_text("from pulp.chips.soft_hier_old.flex_cluster_arch import FlexClusterArch as Base\n"
                        "class FlexClusterArch(Base):\n"
                        "    def __init__(self):\n        super().__init__()\n" +
                        "".join(f"        self.{key} = {value!r}\n" for key, value in values.items()))
        for mode in ("direct", "timed"):
            directory = out / f"{'mixed' if side_hbm else 'stacked'}_{mode}"
            directory.mkdir(exist_ok=True)
            logpath = directory / "simulation.log"
            with logpath.open("w") as log:
                process = subprocess.Popen([
                    args.gvsoc, "--target=pulp.chips.soft_hier_old.flex_cluster",
                    f"--binary={app}", f"--preload={preload}", f"--preload-mode={mode}",
                    "--trace=loader", "run"], cwd=directory,
                    env=dict(os.environ, SOFTHIER_ARCH_FILE=str(arch)),
                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    code = process.wait(timeout=args.timeout)
                except (subprocess.TimeoutExpired, KeyboardInterrupt):
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                    raise AssertionError(f"Simulation interrupted or timed out: {logpath}")
            text = logpath.read_text()
            assert code == 0 and "DRAM3D_ROUTE_PASS" in text, logpath
            assert "DRAM3D_ROUTE_FAIL" not in text, logpath
            print(f"PASS {directory.name}", flush=True)


if __name__ == "__main__":
    main()
