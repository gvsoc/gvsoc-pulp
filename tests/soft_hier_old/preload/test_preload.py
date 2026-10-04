#!/usr/bin/env python3
"""End-to-end direct ELF preload regression; requires a built SoftHier/SystemC target."""
# Copyright (C) 2026 ETH Zurich and University of Bologna
# SPDX-License-Identifier: Apache-2.0
import argparse
import os
from pathlib import Path
import re
import signal
import struct
import subprocess

HERE = Path(__file__).resolve().parent


def elf64(path, regions):
    """Write independent PT_LOAD records (physical addresses, including ELF64 aliases)."""
    headers, payload = bytearray(), bytearray()
    offset = 64 + 56 * len(regions)
    for address, data, memsz in regions:
        headers += struct.pack('<IIQQQQQQ', 1, 6, offset, address, address,
                               len(data), memsz, 1)
        payload += data
        offset += len(data)
    path.write_bytes(b'\x7fELF\x02\x01\x01' + bytes(9) +
                     struct.pack('<HHIQQQIHHHHHH', 2, 243, 1, 0, 64, 0, 0,
                                 64, 56, len(regions), 0, 0, 0) + headers + payload)


def extend_elf32(path, regions):
    """Append load segments while preserving the compiler's symbol table and code."""
    elf = bytearray(path.read_bytes())
    phoff = struct.unpack_from('<I', elf, 28)[0]
    phentsize, phnum = struct.unpack_from('<HH', elf, 42)
    assert phentsize == 32
    headers = elf[phoff:phoff + phnum * phentsize]
    for address, data, memsz in regions:
        headers += struct.pack('<IIIIIIII', 1, len(elf), address, address,
                               len(data), memsz, 6, 1)
        elf += data
    struct.pack_into('<I', elf, 28, len(elf))
    struct.pack_into('<H', elf, 44, phnum + len(regions))
    path.write_bytes(elf + headers)


def run(args, directory, arch, app, preload=None, mode=None, error=None):
    directory.mkdir(exist_ok=True)
    env = dict(os.environ, SOFTHIER_ARCH_FILE=str(arch))
    command = [args.gvsoc, '--target=pulp.chips.soft_hier_old.flex_cluster',
               f'--binary={app}', '--trace=loader', '--trace=ctrl_registers']
    if preload: command.append(f'--preload={preload}')
    if mode: command.append(f'--preload-mode={mode}')
    command.append('run')
    log = directory / 'simulation.log'
    with log.open('w') as output:
        process = subprocess.Popen(command, cwd=directory, env=env, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=args.timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise AssertionError(f'Simulation timed out: {log}')
    text = re.sub(r'\x1b\[[0-9;]*m', '', log.read_text())
    if error:
        assert code != 0 and error in text, f'Missing expected error: {log}'
    else:
        assert code == 0 and 'PRELOAD_PASS' in text and 'PRELOAD_FAIL' not in text, log
        if mode != 'timed':
            cycles = re.findall(r'Direct ELF preload complete .*cycle: (\d+)\)', text)
            assert len(cycles) == (5 if preload else 4) and set(cycles) == {'0'}, log
            starts = re.findall(r'^\d+:\s+(\d+):.*loader/trace.*Sending start', text, re.M)
            assert len(starts) == len(cycles) and set(starts) == {'1'}, log
            if preload:
                ready = re.findall(r'^\d+:\s+(\d+):.*HBM Preload Done,', text, re.M)
                assert ready == ['1'], log
    print(f'PASS {directory.name}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gvsoc', default='gvsoc')
    parser.add_argument('--cc', default='riscv32-unknown-elf-gcc')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--case', action='append', choices=['four_edges', 'aliases_xor',
                                                          'reduced', 'local_only'])
    args = parser.parse_args()
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cases = [('four_edges', 1, 1, 0, 0, [4, 4, 4, 4], 4, 16, 512),
             ('aliases_xor', 2, 2, 1, 0, [4, 0, 0, 4], 8, 8, 1024),
             ('reduced', 2, 1, 0, 1, [4, 0, 0, 4], 4, 16, 512),
             ('local_only', 1, 1, 0, 0, [0, 0, 0, 0], 8, 8, 1024)]
    for name, nodes, aliases, xor, reduced, channels, width, banks, noc in cases:
        if args.case and name not in args.case: continue
        directory = out / name
        directory.mkdir(exist_ok=True)
        arch = directory / 'arch.py'
        arch.write_text('from pulp.chips.soft_hier_old.flex_cluster_arch import FlexClusterArch as Base\n'
                        'class FlexClusterArch(Base):\n'
                        '    def __init__(self):\n'
                        '        super().__init__()\n' + ''.join(
            f'        self.{key} = {value!r}\n' for key, value in dict(
                num_cluster_x=2, num_cluster_y=2, num_core_per_cluster=1,
                cluster_tcdm_base=0x01000000, cluster_tcdm_size=0x10000,
                cluster_tcdm_remote=0x31000000, cluster_stack_base=0x11000000,
                instruction_mem_base=0x81000000, sync_base=0x41000000,
                cluster_tcdm_bank_width=width * 8, cluster_tcdm_bank_nb=banks,
                num_node_per_ctrl=nodes, hbm_node_aliase=aliases,
                hbm_chan_placement=channels, hbm_ctrl_xor_scrambling=xor,
                hbm_ctrl_red_scrambling=reduced, hbm_node_addr_space=0x1000000,
                noc_link_width=noc, redmule_ce_height=8, redmule_ce_width=8,
                redmule_ce_pipe=1, spatz_attaced_core_list=[]).items()))
        local, shared, checks = [], [], []

        def region(target, address, length, seed, bss=0, alias=0):
            data = bytes((i * 37 + seed) & 255 for i in range(length))
            target.append((address + alias, data, length + bss))
            if length: checks.append((address, length, seed, 0))
            if bss: checks.append((address + length, bss, 0, 1))

        region(local, 0x8100c003, 1027, 9)
        region(local, 0x01000103, 2003, 17, bss=257)
        region(local, 0x11000107, 539, 81, bss=19)
        # Pure ELF32 BSS must overwrite memory's nonzero initial fill.
        region(local, 0x01004000, 0, 0, bss=513)
        if name != 'local_only':
            for edge, count in enumerate(channels):
                if not count: continue
                for node in range(2):
                    base = 0xc0000000 + edge * 0x2000000 + node * 0x1000000
                    alias = (1 << 48) if aliases > 1 and node == 1 else 0
                    region(shared, base + 0x10003, 1027, 23 + edge * 2 + node, 31, alias)
                    # Fill then clear to prove pure ELF64 BSS actually loads.
                    shared.append((base + 0x12000, bytes([0xa5]) * 257, 257))
                    region(shared, base + 0x12000, 0, 0, bss=257)
                region(shared, 0xc0000000 + edge * 0x2000000 + 0x1000000 - 129,
                       301, 51 + edge)
            for cluster in range(4):
                shared.append((0x31008003 + cluster * 0x10000,
                               bytes((i * 37 + 101 + cluster) & 255 for i in range(259)), 259))
        (directory / 'regions.h').write_text(
            f'#define CHECK_REMOTE {int(bool(shared))}\n'
            'static const struct { unsigned address, length, seed, zero; } regions[] = {\n' +
            ''.join(f'    {{0x{a:x}, {n}, {s}, {z}}},\n' for a, n, s, z in checks) + '};\n')
        script = directory / 'link.ld'
        script.write_text('ENTRY(_start)\nSECTIONS { . = 0x81000000; '
                          '.text : { *(.text.start) *(.text*) *(.rodata*) } '
                          '.data : { *(.data*) *(.sdata*) } '
                          '.bss : { *(.bss*) *(.sbss*) } }\n')
        app = directory / 'check.elf'
        subprocess.run([args.cc, '-march=rv32im', '-mabi=ilp32', '-O2', '-nostdlib',
                        '-fno-builtin', '-msmall-data-limit=0', '-Wl,--no-relax',
                        f'-I{directory}', f'-T{script}', str(HERE / 'check.c'), '-o', str(app)],
                       check=True)
        extend_elf32(app, local)
        preload = directory / 'preload.elf' if shared else None
        if preload: elf64(preload, shared)
        run(args, directory / 'default_direct', arch, app, preload)
        if name == 'four_edges':
            bad = directory / 'invalid.elf'
            elf64(bad, [(0x60000000, b'bad', 3)])
            run(args, directory / 'invalid_address', arch, app, bad,
                error='Direct ELF preload failed')
            bad.write_bytes(b'\x7fELF\x02')
            run(args, directory / 'truncated_elf', arch, app, bad,
                error='Unable to load ELF')
            elf64(bad, [(0xfffffffffffffff0, bytes(32), 32)])
            run(args, directory / 'address_overflow', arch, app, bad,
                error='Unable to load ELF')
        if name == 'aliases_xor':
            bad = directory / 'disabled_edge.elf'
            elf64(bad, [(0xc2000000, b'bad', 3)])
            run(args, directory / 'disabled_edge', arch, app, bad,
                error='Direct ELF preload failed')


if __name__ == '__main__':
    main()
