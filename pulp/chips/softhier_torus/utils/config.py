import re
import ast
import math
import argparse

parser = argparse.ArgumentParser(description="Generate C and S header files from a Softhier configuration file.")
parser.add_argument("input_file", nargs="?", default="pulp/pulp/chips/softhier/softhier_arch.py", help="Path to the input Python file")
args = parser.parse_args()
input_file = args.input_file

# Read the input Python file
C_header_file = 'pulp/pulp/chips/softhier/sw/runtime/include/softhier_arch.h'
S_header_file = 'pulp/pulp/chips/softhier/sw/runtime/include/softhier_arch.inc'

# Initialize a dictionary to store the class attributes and their values
attributes = {}

# Read the input file and extract the class attributes
with open(input_file, 'r') as file:
    lines = file.readlines()
    for line in lines:
        match = re.match(r'\s*self\.(\w+)\s*=\s*(.+)', line)
        if match:
            attr_name = match.group(1)
            attr_value = match.group(2)
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
