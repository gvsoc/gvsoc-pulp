import re
import argparse
import importlib.util
import inspect
import os

parser = argparse.ArgumentParser(description="Generate C and S header files for a SoftHier topology.")
parser.add_argument("topology", help="Topology name (key into softhier_arch_base.TOPOLOGIES)")
parser.add_argument("out_dir", help="Path to the output directory for header files")
parser.add_argument("--param", action="append", default=[], metavar="KEY=VALUE",
                     help="Override an individual arch attribute (repeatable)")
parser.add_argument("--arch-file", default=None,
                     help="Bypass the built-in topology registry entirely and scrape "
                          "self.X = value assignments directly from this custom file "
                          "(equivalent to the old cfg=<path> behavior).")
args = parser.parse_args()

out_dir = args.out_dir
os.makedirs(out_dir, exist_ok=True)
C_header_file = os.path.join(out_dir, 'softhier_arch.h')
S_header_file = os.path.join(out_dir, 'softhier_arch.inc')

attributes = {}

if args.arch_file:
    # Full custom override: scrape the given file's source directly, same
    # as the original single-file version of this script.
    with open(args.arch_file, 'r') as file:
        source = file.read()
else:
    # Load softhier_arch_base.py by file path, relative to this script, so
    # this tool works standalone regardless of the caller's PYTHONPATH/package
    # installation state (matches the original script's zero-dependency design).
    _this_dir = os.path.dirname(os.path.abspath(__file__))
    _arch_base_path = os.path.normpath(os.path.join(_this_dir, '..', '..', 'softhier_arch_base.py'))
    _spec = importlib.util.spec_from_file_location('softhier_arch_base', _arch_base_path)
    _arch_base = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_arch_base)

    if args.topology not in _arch_base.TOPOLOGIES:
        raise SystemExit(
            f"Unknown topology '{args.topology}', expected one of: "
            f"{sorted(_arch_base.TOPOLOGIES.keys())}")
    arch_cls = _arch_base.TOPOLOGIES[args.topology]

    # Regex-scrape the base class's __init__ (identical attributes) and the
    # topology subclass's __init__ (varying attributes) preserving
    # each value's exact source formatting 
    base_cls = arch_cls.__mro__[1]
    source = inspect.getsource(base_cls.__init__) + '\n' + inspect.getsource(arch_cls.__init__)

for line in source.splitlines():
    match = re.match(r'\s*self\.(\w+)\s*=\s*(.+)', line)
    if match:
        attr_name = match.group(1)
        attr_value = match.group(2)
        attributes[attr_name] = attr_value

# Individual --param overrides, applied on top of the topology's defaults
# (or on top of the --arch-file's values, if that mode was used).
overrides = {}
for item in args.param:
    key, _, value = item.partition('=')
    overrides[key] = value

# Apply --param overrides. An override's value is inserted verbatim as
# source text, so numeric/hex overrides need no quoting on the command
# line (e.g. --param num_cluster=48); string-valued fields would need
# their own quotes supplied by the caller (e.g. --param topology='"Foo"').
for attr_name, attr_value in overrides.items():
    attributes[attr_name] = attr_value

# Write the output C header file
with open(C_header_file, 'w') as file:
    file.write('#ifndef SOFTHIERARCH_H\n')
    file.write('#define SOFTHIERARCH_H\n\n')

    for attr_name, attr_value in attributes.items():
        # Convert attribute name to uppercase and prefix with 'ARCH_'
        define_name = f'ARCH_{attr_name.upper()}'
        file.write(f'#define {define_name} {attr_value}\n')

    file.write('\n#endif // SOFTHIERARCH_H\n')

print(f'Header file "{C_header_file}" generated successfully.')

# Write the output S header file
with open(S_header_file, 'w') as file:
    file.write('#ifndef SOFTHIERARCH_H\n')
    file.write('#define SOFTHIERARCH_H\n\n')

    for attr_name, attr_value in attributes.items():
        if attr_name == 'topology':
            continue
        # Convert attribute name to uppercase and prefix with 'ARCH_'
        define_name = f'ARCH_{attr_name.upper()}'
        file.write(f'.set {define_name}, {attr_value}\n')

    file.write('\n#endif // SOFTHIERARCH_H\n')

print(f'Header file "{S_header_file}" generated successfully.')
