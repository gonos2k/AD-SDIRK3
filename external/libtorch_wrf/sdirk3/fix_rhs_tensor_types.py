#!/usr/bin/env python3
"""
Fix tensor vs float type issues in wrf_sdirk3_unified_rhs.cpp
to preserve autograd computation graph
"""

import re
import sys

def fix_tensor_types(content):
    """Fix all tensor/float type issues to preserve autograd graph"""
    
    # Replace float variable declarations with torch::Tensor
    content = re.sub(r'\bfloat\s+(rho|K|xkmh|alpha_at_v|msfvx_val|msfvy_val)\s*=', 
                     r'torch::Tensor \1 =', content)
    
    # Remove .item<float>() calls that break the autograd graph
    content = re.sub(r'\.item<float>\(\)', '', content)
    content = re.sub(r'\.item<double>\(\)', '', content)
    
    # Fix assignments where Tensor is assigned to float
    # Replace patterns like: rho = (expression)
    content = re.sub(r'(\s+)(rho|K)\s*=\s*([^;]+);', 
                     lambda m: f'{m.group(1)}torch::Tensor {m.group(2)}_temp = {m.group(3)};\n{m.group(1)}{m.group(2)} = {m.group(2)}_temp;', 
                     content)
    
    # Fix tau array indexing - use tensor indexing instead of array accessor
    content = re.sub(r'tau11\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'tau11.index({\1, \2, \3})', content)
    content = re.sub(r'tau12\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'tau12.index({\1, \2, \3})', content)
    content = re.sub(r'tau22\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'tau22.index({\1, \2, \3})', content)
    content = re.sub(r'tau13\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'tau13.index({\1, \2, \3})', content)
    content = re.sub(r'tau23\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'tau23.index({\1, \2, \3})', content)
    content = re.sub(r'tau33\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'tau33.index({\1, \2, \3})', content)
    
    # Fix deformation tensor indexing
    content = re.sub(r'defor11\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'defor11.index({\1, \2, \3})', content)
    content = re.sub(r'defor12\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'defor12.index({\1, \2, \3})', content)
    content = re.sub(r'defor22\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'defor22.index({\1, \2, \3})', content)
    content = re.sub(r'defor13\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'defor13.index({\1, \2, \3})', content)
    content = re.sub(r'defor23\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'defor23.index({\1, \2, \3})', content)
    content = re.sub(r'defor33\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'defor33.index({\1, \2, \3})', content)
    
    # Fix xkmh indexing
    content = re.sub(r'xkmh\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'xkmh.index({\1, \2, \3})', content)
    content = re.sub(r'xkmh_3d\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'xkmh_3d.index({\1, \2, \3})', content)
    
    # Fix state.mu_full indexing
    content = re.sub(r'state\.mu_full\[(\w+)\]\[(\w+)\]', r'state.mu_full.index({\1, \2})', content)
    
    # Ensure torch::Tensor declarations are used where needed
    lines = content.split('\n')
    fixed_lines = []
    
    for line in lines:
        # Skip lines that are already fixed
        if 'torch::Tensor rho, K;' in line and line.strip() == 'torch::Tensor rho, K;':
            continue  # Remove redundant declarations
            
        # Fix float declarations for specific variables
        if re.match(r'\s*float\s+(rho|K|xkmh)\s*=', line):
            line = re.sub(r'float\s+', 'torch::Tensor ', line)
            
        # Fix map scale factor declarations
        if 'float msfvx_val = 1.0f;' in line:
            line = line.replace('float msfvx_val = 1.0f;', 'torch::Tensor msfvx_val = torch::tensor(1.0f);')
        if 'float msfvy_val = 1.0f;' in line:
            line = line.replace('float msfvy_val = 1.0f;', 'torch::Tensor msfvy_val = torch::tensor(1.0f);')
            
        # Fix alpha_at_v declaration
        if 'float alpha_at_v =' in line:
            line = line.replace('float alpha_at_v =', 'torch::Tensor alpha_at_v =')
            
        fixed_lines.append(line)
    
    return '\n'.join(fixed_lines)

def main():
    filename = '/Users/yhlee/dWRF/external/libtorch_wrf/sdirk3/wrf_sdirk3_unified_rhs.cpp'
    
    # Read the file
    with open(filename, 'r') as f:
        content = f.read()
    
    # Apply fixes
    fixed_content = fix_tensor_types(content)
    
    # Write back
    with open(filename, 'w') as f:
        f.write(fixed_content)
    
    print(f"Fixed tensor type issues in {filename}")
    print("All .item() calls removed to preserve autograd graph")
    print("Float variables converted to torch::Tensor where needed")

if __name__ == '__main__':
    main()