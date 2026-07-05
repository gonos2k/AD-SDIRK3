#!/usr/bin/env python3
"""
Check for orphaned source files not included in Makefile
"""

import os
import re

def get_makefile_sources():
    """Extract source files from Makefile"""
    sources = set()
    headers = set()
    
    with open('Makefile', 'r') as f:
        content = f.read()
        
    # Extract source files
    source_section = re.search(r'WRF_SOURCES = \\\n(.*?)^\s*$', content, re.MULTILINE | re.DOTALL)
    if source_section:
        for line in source_section.group(1).split('\n'):
            line = line.strip()
            if line.endswith('.cpp \\') or line.endswith('.cpp'):
                filename = line.replace('\\', '').strip()
                sources.add(filename)
    
    # Extract header files
    header_section = re.search(r'WRF_HEADERS = \\\n(.*?)^\s*$', content, re.MULTILINE | re.DOTALL)
    if header_section:
        for line in header_section.group(1).split('\n'):
            line = line.strip()
            if line.endswith('.h \\') or line.endswith('.h'):
                filename = line.replace('\\', '').strip()
                headers.add(filename)
    
    return sources, headers

def get_directory_files():
    """Get all C++ source and header files in directory"""
    sources = set()
    headers = set()
    
    for filename in os.listdir('.'):
        if filename.endswith('.cpp'):
            sources.add(filename)
        elif filename.endswith('.h'):
            headers.add(filename)
    
    return sources, headers

def main():
    # Get files from Makefile
    makefile_sources, makefile_headers = get_makefile_sources()
    
    # Get files from directory
    dir_sources, dir_headers = get_directory_files()
    
    # Find orphaned files
    orphaned_sources = dir_sources - makefile_sources
    orphaned_headers = dir_headers - makefile_headers
    
    print("=== Orphaned Source Files Analysis ===\n")
    
    if orphaned_sources:
        print(f"Found {len(orphaned_sources)} orphaned source files not in Makefile:")
        for f in sorted(orphaned_sources):
            # Check file purpose
            purpose = "Unknown"
            if 'extended' in f:
                purpose = "Extended/Advanced features"
            elif 'optimized' in f:
                purpose = "Optimized implementation"
            elif 'fixed' in f:
                purpose = "Bug fix version"
            elif 'example' in f:
                purpose = "Example/Test code"
            elif 'benchmark' in f:
                purpose = "Benchmarking code"
            elif 'base_state_zerocopy' in f:
                purpose = "Zero-copy base state setter"
            elif 'divergence_damping' in f:
                purpose = "Divergence damping implementation"
            elif 'terrain_slope' in f:
                purpose = "Terrain slope calculations"
            elif 'full_physics' in f:
                purpose = "Full physics integration"
            elif 'generalization' in f:
                purpose = "Generalization utilities"
            elif 'forward' in f:
                purpose = "Forward-mode solver"
            elif 'integration' in f:
                purpose = "Integration test/example"
            
            print(f"  - {f:<50} ({purpose})")
    else:
        print("No orphaned source files found.")
    
    print()
    
    if orphaned_headers:
        print(f"Found {len(orphaned_headers)} orphaned header files not in Makefile:")
        for f in sorted(orphaned_headers):
            purpose = "Unknown"
            if 'extended' in f:
                purpose = "Extended/Advanced features"
            elif 'types' in f:
                purpose = "Type definitions"
            elif 'terrain_slope' in f:
                purpose = "Terrain slope support"
            elif 'divergence_damping' in f:
                purpose = "Divergence damping support"
            elif 'full_physics' in f:
                purpose = "Full physics integration"
            elif 'generalization' in f:
                purpose = "Generalization utilities"
            
            print(f"  - {f:<50} ({purpose})")
    else:
        print("No orphaned header files found.")
    
    # List files in Makefile that don't exist
    print("\n=== Missing Files in Makefile ===\n")
    
    missing_sources = makefile_sources - dir_sources
    missing_headers = makefile_headers - dir_headers
    
    if missing_sources:
        print(f"Found {len(missing_sources)} source files in Makefile that don't exist:")
        for f in sorted(missing_sources):
            print(f"  - {f}")
    else:
        print("All source files in Makefile exist.")
    
    print()
    
    if missing_headers:
        print(f"Found {len(missing_headers)} header files in Makefile that don't exist:")
        for f in sorted(missing_headers):
            print(f"  - {f}")
    else:
        print("All header files in Makefile exist.")

if __name__ == '__main__':
    main()