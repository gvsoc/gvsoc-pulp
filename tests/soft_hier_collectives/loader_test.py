# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import struct
import gvsoc.systree as st
import gvsoc.runner
from vp.clock_domain import Clock_domain
from utils.loader.loader import ElfLoader


class Chip(st.Component):
    def __init__(self, parent, name=None):
        super().__init__(parent, name)
        # File data followed by more than one 64-KiB zero-fill chunk.
        folder = Path(__file__).resolve().parent / 'build/loader'
        folder.mkdir(parents=True, exist_ok=True)
        binary = folder / 'preload.elf'
        ident = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9)
        header = struct.pack('<16sHHIQQQIHHHHHH', ident, 2, 243, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0)
        phdr = struct.pack('<IIQQQQQQ', 1, 6, 128, 0x1000, 0x1000, 16, 0x20000, 8)
        binary.write_bytes(header + phdr + bytes(128-len(header)-len(phdr)) + bytes(range(16)))
        clock = Clock_domain(self, 'clock', frequency=1000000000)
        soc = st.Component(self, 'soc')
        clock.o_CLOCK(soc.i_CLOCK())
        loader = ElfLoader(soc, 'loader', binary=str(binary))
        test = st.Component(soc, 'test')
        test.add_sources(['loader_probe.cpp'])
        loader.o_OUT(st.SlaveItf(test, 'input', signature='io'))
        loader.o_START(st.SlaveItf(test, 'done', signature='wire<bool>'))


class Target(gvsoc.runner.Target):
    gapy_description = 'Async ELF loader payload initialization and zero-fill lifetime'
    model = Chip
    name = 'loader_test'
