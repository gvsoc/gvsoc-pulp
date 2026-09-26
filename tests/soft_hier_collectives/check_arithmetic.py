#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check all half encodings and randomized reductions against NumPy."""
import ctypes
import os
from pathlib import Path
import subprocess
import numpy as np

test = Path(__file__).resolve().parent
out = test / 'build/arithmetic'
out.mkdir(parents=True, exist_ok=True)
header = test.parents[1] / 'pulp/chips/soft_hier_old/floonoc_v2/collective_reduction.hpp'
source = out / 'probe.cpp'
source.write_text(f'''#include "{header}"
extern "C" void reduce(unsigned op, uint16_t *a, const uint16_t *b, unsigned n) {{
    softhier_collective::combine(op, (uint8_t *)a, (const uint8_t *)b, n*2);
}}
extern "C" void roundtrip(uint16_t *a, unsigned n) {{
    for (unsigned i=0;i<n;++i) a[i]=softhier_collective::to_half(softhier_collective::from_half(a[i]));
}}
''')
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-shared', '-fPIC',
                '-O2', str(source), '-o', str(out / 'probe.so')], check=True)
lib = ctypes.CDLL(str(out / 'probe.so'))
ptr = ctypes.POINTER(ctypes.c_uint16)
lib.roundtrip.argtypes = [ptr, ctypes.c_uint]
lib.reduce.argtypes = [ctypes.c_uint, ptr, ptr, ctypes.c_uint]
original = np.arange(65536, dtype=np.uint16)
actual = original.copy()
lib.roundtrip(actual.ctypes.data_as(ptr), len(actual))
finite = ~np.isnan(original.view(np.float16))
assert np.array_equal(actual[finite], original[finite])
assert np.isnan(actual[~finite].view(np.float16)).all()
rng = np.random.default_rng(42)
a = rng.integers(0, 65536, 100000, dtype=np.uint16)
b = rng.integers(0, 65536, len(a), dtype=np.uint16)
with np.errstate(all='ignore'):
    for op in range(2, 8):
        result = a.copy()
        lib.reduce(op, result.ctypes.data_as(ptr), b.ctypes.data_as(ptr), len(a))
        if op in (2, 3): golden = a + b
        elif op == 5: golden = np.maximum(a, b)
        elif op == 6: golden = np.maximum(a.view(np.int16), b.view(np.int16)).view(np.uint16)
        else:
            aa, bb = a.view(np.float16), b.view(np.float16)
            golden = (aa + bb if op == 4 else np.fmax(aa, bb)).view(np.uint16)
        nan = np.isnan(golden.view(np.float16)) if op in (4, 7) else np.zeros(len(a), dtype=bool)
        assert np.array_equal(result[~nan], golden[~nan]), f'op {op}'
        assert np.isnan(result[nan].view(np.float16)).all(), f'op {op} NaNs'
print('SOFTHIER_ARITHMETIC_PASS encodings=65536 random_pairs=100000 operations=6 seed=42')
