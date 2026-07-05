#!/usr/bin/env python3
"""
Script to wrap unconditional debug output with debug level checks
"""

import re
import os
import sys

def wrap_debug_output(filename, debug_level=2):
    """Wrap std::cout and std::cerr statements with debug level checks"""
    
    # Read the file
    with open(filename, 'r') as f:
        content = f.read()
    
    # Pattern to match debug output lines
    # Match std::cout or std::cerr that are not already inside an if statement
    patterns = [
        # Match standalone std::cout/cerr lines
        (r'(\s*)(std::(cout|cerr)\s*<<[^;]+;)', r'\1if (wrf::sdirk3::g_sdirk3_config.debug_level >= {}) {{\n\1    \2\n\1}}'),
        # Match multi-line debug outputs starting with std::cout/cerr
        (r'(\s*)(std::(cout|cerr)\s*<<[^;]+\n(?:\s*<<[^;]+\n)*\s*<<[^;]+;)', 
         lambda m: f'{m.group(1)}if (wrf::sdirk3::g_sdirk3_config.debug_level >= {debug_level}) {{\n' + 
                   '\n'.join(f'{m.group(1)}    {line}' for line in m.group(2).split('\n')) + 
                   f'\n{m.group(1)}}}')
    ]
    
    # Check if the file already includes the necessary header
    if 'wrf_sdirk3_config.h' not in content and 'g_sdirk3_config' in content:
        # Add include at the top after other includes
        include_pos = content.find('#include')
        if include_pos != -1:
            # Find the end of includes section
            last_include = content.rfind('#include', 0, content.find('\n\n', include_pos))
            if last_include != -1:
                end_of_line = content.find('\n', last_include)
                content = content[:end_of_line+1] + '#include "wrf_sdirk3_config.h"\n' + content[end_of_line+1:]
    
    modified = False
    for pattern, replacement in patterns:
        # Check if we have any matches that aren't already wrapped
        matches = list(re.finditer(pattern, content, re.MULTILINE))
        
        # Filter out matches that are already inside if statements
        valid_matches = []
        for match in matches:
            # Look backwards to check if this is inside an if block
            start_pos = match.start()
            # Get previous lines
            prev_text = content[:start_pos]
            prev_lines = prev_text.split('\n')[-5:]  # Check last 5 lines
            
            # Simple heuristic: if we see "if" and "debug_level" nearby, skip
            nearby_text = '\n'.join(prev_lines)
            if 'if' in nearby_text and 'debug_level' in nearby_text:
                continue
                
            valid_matches.append(match)
        
        if valid_matches:
            print(f"Found {len(valid_matches)} debug outputs to wrap in {filename}")
            modified = True
            
            # Replace from end to beginning to preserve positions
            for match in reversed(valid_matches):
                if callable(replacement):
                    new_text = replacement(match)
                else:
                    new_text = replacement.format(debug_level).replace('{}', str(debug_level))
                    new_text = new_text.replace(r'\1', match.group(1))
                    new_text = new_text.replace(r'\2', match.group(2))
                
                content = content[:match.start()] + new_text + content[match.end():]
    
    if modified:
        # Write back the file
        with open(filename, 'w') as f:
            f.write(content)
        print(f"Updated {filename}")
        return True
    else:
        print(f"No changes needed in {filename}")
        return False

def main():
    # Files identified in the cleanup report as having excessive debug output
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
    total_fixed = 0
    for filename in files_to_fix:
        if os.path.exists(filename):
            if wrap_debug_output(filename):
                total_fixed += 1
        else:
            print(f"Warning: {filename} not found")
    
    print(f"\nTotal files updated: {total_fixed}")

if __name__ == '__main__':
    main()