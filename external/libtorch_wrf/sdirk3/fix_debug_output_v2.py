#!/usr/bin/env python3
"""
Script to fix the incorrectly wrapped debug output (removes literal \n)
"""

import os
import re

def fix_debug_wrapping(filename):
    """Fix incorrectly wrapped debug output statements"""
    
    # Read the file
    with open(filename, 'r') as f:
        content = f.read()
    
    # Fix the pattern where we have literal \n instead of actual newlines
    # Pattern: if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {\n
    content = re.sub(r'if \(wrf::sdirk3::g_sdirk3_config\.debug_level >= \d+\) \{\\n',
                     lambda m: m.group(0).replace('\\n', '\n'), content)
    
    # Fix closing braces with literal \n
    content = re.sub(r'<< std::endl;\\n(\s*)\}', r'<< std::endl;\n\1}', content)
    
    # Fix any remaining \n at the end of lines
    content = re.sub(r'\\n\s*$', '\n', content, flags=re.MULTILINE)
    
    # Write back the file
    with open(filename, 'w') as f:
        f.write(content)
    
    print(f"Fixed {filename}")

def main():
    # Files that were incorrectly modified
    files_to_fix = [
        'wrf_sdirk3_base_state_zerocopy.cpp',
        'wrf_sdirk3_tile_unified_impl.cpp',
        'wrf_sdirk3_interface_zerocopy.cpp',
        'wrf_sdirk3_unified_rhs.cpp',
        'wrf_sdirk3_solver.cpp',
        'wrf_sdirk3_newton_solver.cpp',
        'wrf_sdirk3_unified_rhs_extended.cpp'
    ]
    
    # Process each file
    for filename in files_to_fix:
        if os.path.exists(filename):
            fix_debug_wrapping(filename)
        else:
            print(f"Warning: {filename} not found")

if __name__ == '__main__':
    main()