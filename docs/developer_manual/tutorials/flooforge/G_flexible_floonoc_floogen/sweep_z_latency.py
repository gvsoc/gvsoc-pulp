#!/usr/bin/env python3
"""
Runs the G tutorial's system once per Z-axis link latency and plots how the
total simulation completion time scales with it - the flexible model's
whole point demonstrated numerically: changing the topology config changes
timing, with no code change.

Usage:
    python3 sweep_z_latency.py --z-latencies 1,2,4,8

All other topology parameters (grid size, X/Y link latency) default to the
same values as the Makefile's `topology` target, and can be overridden the
same way, e.g.:

    python3 sweep_z_latency.py --z-latencies 1,2,4,8,16 --dim-x 2 --dim-y 2
"""

import argparse
import os
import re
import subprocess

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))

# Matches a trace line dumped by vp::Trace: "<time_ps>: <cycles>: [<path>] <msg>"
_FINISH_RE = re.compile(r'^(\d+):\s*(\d+):.*All generators finished')


def run_one(z_latency, dim_x, dim_y, dim_z, link_latency, builddir):
    make_vars = [
        f'DIM_X={dim_x}', f'DIM_Y={dim_y}', f'DIM_Z={dim_z}',
        f'LINK_LATENCY={link_latency}', f'Z_LINK_LATENCY={z_latency}',
        f'BUILDDIR={builddir}',
    ]

    subprocess.run(['make', 'prepare', 'gvsoc'] + make_vars, cwd=_THIS_DIR, check=True)

    result = subprocess.run(
        ['make', 'run', 'runner_args=--trace=driver'] + make_vars,
        cwd=_THIS_DIR, check=True, capture_output=True, text=True)
    output = result.stdout + result.stderr

    match = None
    for line in output.splitlines():
        m = _FINISH_RE.match(line)
        if m:
            match = m
    if match is None:
        raise RuntimeError(
            f"Could not find a completion trace line for z-link-latency={z_latency}. "
            f"Full output:\n{output}")

    time_ps, cycles = int(match.group(1)), int(match.group(2))
    return time_ps, cycles


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--z-latencies', default='1,2,4,8',
        help='Comma-separated list of Z-axis link latencies to sweep, in cycles')
    parser.add_argument('--dim-x', type=int, default=3)
    parser.add_argument('--dim-y', type=int, default=3)
    parser.add_argument('--dim-z', type=int, default=2)
    parser.add_argument('--link-latency', type=int, default=1,
        help='X/Y link latency, kept fixed across the sweep')
    parser.add_argument('--builddir', default='sweep')
    parser.add_argument('--csv', default='generated/z_latency_sweep.csv',
        help='Written alongside the generated topology files; wiped by `make clean`')
    parser.add_argument('--out', default='generated/z_latency_sweep.png')
    args = parser.parse_args()

    z_latencies = [int(v) for v in args.z_latencies.split(',')]

    results = []
    for z in z_latencies:
        print(f'--- z-link-latency={z} ---')
        time_ps, cycles = run_one(z, args.dim_x, args.dim_y, args.dim_z,
            args.link_latency, args.builddir)
        print(f'  finished after {cycles} cycles ({time_ps} ps)')
        results.append((z, cycles, time_ps))

    csv_path = os.path.join(_THIS_DIR, args.csv)
    with open(csv_path, 'w') as f:
        f.write('z_link_latency,cycles,time_ps\n')
        for z, cycles, time_ps in results:
            f.write(f'{z},{cycles},{time_ps}\n')
    print(f'Wrote "{csv_path}"')

    xs = [r[0] for r in results]
    ys = [r[1] for r in results]

    fig, ax = plt.subplots(figsize=(6, 4.5))
    ax.plot(xs, ys, marker='o', linestyle='-', color='#4C72B0', linewidth=2, markersize=7)
    ax.set_xlabel('Z-axis link latency (cycles)')
    ax.set_ylabel('Simulation completion time (cycles)')
    ax.set_title(f'Completion time vs Z-axis link latency ({args.dim_x}x{args.dim_y}x{args.dim_z} mesh)')
    ax.grid(True, alpha=0.3)
    fig.tight_layout()

    out_path = os.path.join(_THIS_DIR, args.out)
    fig.savefig(out_path, dpi=150)
    print(f'Wrote "{out_path}"')


if __name__ == '__main__':
    main()
