#!/usr/bin/env python3
import re

# Read the file
with open('wrf_sdirk3_tile_unified_impl.cpp', 'r') as f:
    content = f.read()

# Pattern to match variable.zero_()
pattern = r'(\w+)\.zero_\(\)'

def replace_zero(match):
    var_name = match.group(1)
    return f'{var_name} = torch::zeros_like({var_name})'

# Replace all occurrences
new_content = re.sub(pattern, replace_zero, content)

# Write back
with open('wrf_sdirk3_tile_unified_impl.cpp', 'w') as f:
    f.write(new_content)

print("Replaced all .zero_() operations")