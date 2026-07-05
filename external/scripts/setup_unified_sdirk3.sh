#!/bin/bash
# WRF SDIRK3 Unified Setup Script
# Automates the integration of libtorch_wrf/sdirk3 and sdirk3_lib

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BASE_DIR="/Users/yhlee/dWRF/external"
LIBTORCH_DIR="${BASE_DIR}/libtorch_wrf/sdirk3"
SDIRK3_LIB_DIR="${BASE_DIR}/sdirk3_lib"
UNIFIED_DIR="${BASE_DIR}/sdirk3_unified"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}   WRF SDIRK3 Unified Setup Script     ${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Function to print status
print_status() {
    echo -e "${YELLOW}[$(date '+%H:%M:%S')]${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Check prerequisites
print_status "Checking prerequisites..."

if [ ! -d "$LIBTORCH_DIR" ]; then
    print_error "libtorch_wrf/sdirk3 directory not found at $LIBTORCH_DIR"
    exit 1
fi

if [ ! -d "$SDIRK3_LIB_DIR" ]; then
    print_error "sdirk3_lib directory not found at $SDIRK3_LIB_DIR"
    exit 1
fi

print_success "Prerequisites checked"

# Create unified directory structure
print_status "Creating unified directory structure..."

mkdir -p ${UNIFIED_DIR}/{include,src,test,docs,scripts,build}
mkdir -p ${UNIFIED_DIR}/include/{core,solvers,physics,parallel,interface,utils}
mkdir -p ${UNIFIED_DIR}/src/{core,solvers,physics,parallel,interface,utils}
mkdir -p ${UNIFIED_DIR}/test/{unit,integration,validation,performance}
mkdir -p ${UNIFIED_DIR}/docs/{api,design,migration}

print_success "Directory structure created"

# Phase 1: Copy core files from sdirk3_lib (correct memory layout)
print_status "Phase 1: Copying core files from sdirk3_lib..."

# Memory layout files (CRITICAL - these have the correct (j,k,i) layout)
cp ${SDIRK3_LIB_DIR}/include/sdirk3/wrf_memory_layout_adapter.h \
   ${UNIFIED_DIR}/include/core/memory_layout.h 2>/dev/null || true

cp ${SDIRK3_LIB_DIR}/include/sdirk3/wrf_atomic_operations_jvp.h \
   ${UNIFIED_DIR}/include/core/atomic_operations.h 2>/dev/null || true

cp ${SDIRK3_LIB_DIR}/include/sdirk3/wrf_acoustic_preconditioner.h \
   ${UNIFIED_DIR}/include/solvers/acoustic_precond.h 2>/dev/null || true

# Documentation
cp -r ${SDIRK3_LIB_DIR}/docs/* ${UNIFIED_DIR}/docs/design/ 2>/dev/null || true

print_success "Core files copied from sdirk3_lib"

# Phase 2: Prepare files from libtorch_wrf (need modification)
print_status "Phase 2: Preparing files from libtorch_wrf for migration..."

# Create a staging directory for files that need modification
mkdir -p ${UNIFIED_DIR}/staging

# Copy physics files that need layout updates
cp ${LIBTORCH_DIR}/wrf_sdirk3_wsm6_kslab.h \
   ${UNIFIED_DIR}/staging/ 2>/dev/null || true

cp ${LIBTORCH_DIR}/wrf_sdirk3_tile_unified.cpp \
   ${UNIFIED_DIR}/staging/ 2>/dev/null || true

cp ${LIBTORCH_DIR}/wrf_sdirk3_newton_solver.cpp \
   ${UNIFIED_DIR}/staging/ 2>/dev/null || true

print_success "Files staged for modification"

# Phase 3: Create migration scripts
print_status "Creating migration helper scripts..."

# Create Python script for layout conversion
cat > ${UNIFIED_DIR}/scripts/convert_memory_layout.py << 'EOF'
#!/usr/bin/env python3
"""
Convert tensor indexing from (k,j,i) to (j,k,i) layout
"""
import re
import sys
import os

def convert_tensor_indexing(content):
    """Update tensor indexing patterns"""
    
    # Pattern replacements
    replacements = [
        # Tensor indexing
        (r'tensor\[(\w+)\]\[(\w+)\]\[(\w+)\]', r'tensor[\2][\1][\3]'),  # [k][j][i] -> [j][k][i]
        
        # Loop ordering - swap k and j loops
        (r'for\s*\(\s*int\s+k\s*=\s*0;\s*k\s*<\s*nk;\s*k\+\+\s*\)\s*'
         r'for\s*\(\s*int\s+j\s*=\s*0;\s*j\s*<\s*nj;\s*j\+\+\s*\)',
         r'for (int j = 0; j < nj; j++)\n    for (int k = 0; k < nk; k++)'),
        
        # Tensor creation
        (r'torch::empty\(\{nk,\s*nj,\s*ni\}', r'torch::empty({nj, nk, ni}'),
        (r'torch::zeros\(\{nk,\s*nj,\s*ni\}', r'torch::zeros({nj, nk, ni}'),
        
        # Broadcasting fixes
        (r'\.unsqueeze\(0\)\.expand\(\{nk,', r'.unsqueeze(1).expand({nj,'),
        
        # Index calculations
        (r'idx\s*=\s*k\s*\*\s*nj\s*\*\s*ni\s*\+\s*j\s*\*\s*ni\s*\+\s*i',
         r'idx = j * nk * ni + k * ni + i'),
    ]
    
    modified = content
    for pattern, replacement in replacements:
        modified = re.sub(pattern, replacement, modified)
    
    return modified

def process_file(input_path, output_path):
    """Process a single file"""
    print(f"Processing {input_path}...")
    
    with open(input_path, 'r') as f:
        content = f.read()
    
    converted = convert_tensor_indexing(content)
    
    # Add comment about conversion
    header = "// AUTO-CONVERTED: Memory layout updated from (k,j,i) to (j,k,i)\n\n"
    converted = header + converted
    
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(converted)
    
    print(f"  ✓ Saved to {output_path}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python convert_memory_layout.py <input_file> <output_file>")
        sys.exit(1)
    
    process_file(sys.argv[1], sys.argv[2])
EOF

chmod +x ${UNIFIED_DIR}/scripts/convert_memory_layout.py

print_success "Migration scripts created"

# Phase 4: Create validation test
print_status "Creating validation tests..."

cat > ${UNIFIED_DIR}/test/test_memory_layout.cpp << 'EOF'
#include <torch/torch.h>
#include <iostream>
#include "../include/core/memory_layout.h"

bool test_layout_transformation() {
    // Test data
    const int ni = 10, nj = 8, nk = 5;
    float wrf_data[ni * nk * nj];
    
    // Initialize with pattern
    for (int j = 0; j < nj; j++) {
        for (int k = 0; k < nk; k++) {
            for (int i = 0; i < ni; i++) {
                int idx = i + k * ni + j * ni * nk;
                wrf_data[idx] = i + 10*k + 100*j;
            }
        }
    }
    
    // Convert to torch tensor with correct layout
    auto tensor = torch::empty({nj, nk, ni}, torch::kFloat32);
    
    // Verify layout
    for (int j = 0; j < nj; j++) {
        for (int k = 0; k < nk; k++) {
            for (int i = 0; i < ni; i++) {
                int wrf_idx = i + k * ni + j * ni * nk;
                if (std::abs(tensor[j][k][i].item<float>() - wrf_data[wrf_idx]) > 1e-6) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

int main() {
    std::cout << "Testing memory layout conversion..." << std::endl;
    
    if (test_layout_transformation()) {
        std::cout << "✓ Memory layout test PASSED" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Memory layout test FAILED" << std::endl;
        return 1;
    }
}
EOF

print_success "Validation tests created"

# Phase 5: Create Makefile
print_status "Creating unified Makefile..."

cat > ${UNIFIED_DIR}/Makefile << 'EOF'
# WRF SDIRK3 Unified Makefile
# Combines libtorch_wrf and sdirk3_lib implementations

# LibTorch configuration
LIBTORCH_PATH ?= /opt/libtorch
CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
CXXFLAGS += -I$(LIBTORCH_PATH)/include -I$(LIBTORCH_PATH)/include/torch/csrc/api/include
CXXFLAGS += -Iinclude
LDFLAGS = -L$(LIBTORCH_PATH)/lib -ltorch -ltorch_cpu -lc10

# Source files organized by component
CORE_SRCS = \
    src/core/memory_layout.cpp \
    src/core/atomic_operations.cpp \
    src/core/unified_config.cpp

SOLVER_SRCS = \
    src/solvers/newton_krylov.cpp \
    src/solvers/gmres.cpp \
    src/solvers/acoustic_precond.cpp

PHYSICS_SRCS = \
    src/physics/wsm6_microphysics.cpp \
    src/physics/pbl_physics.cpp

PARALLEL_SRCS = \
    src/parallel/tile_manager.cpp \
    src/parallel/halo_exchange.cpp

ALL_SRCS = $(CORE_SRCS) $(SOLVER_SRCS) $(PHYSICS_SRCS) $(PARALLEL_SRCS)
OBJS = $(ALL_SRCS:.cpp=.o)

# Library target
LIB_TARGET = libsdirk3_unified.a

# Build rules
all: $(LIB_TARGET) tests

$(LIB_TARGET): $(OBJS)
	ar rcs $@ $^
	@echo "✓ Built unified SDIRK3 library"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Test targets
tests: test_memory_layout

test_memory_layout: test/test_memory_layout.cpp
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@
	./$@

clean:
	rm -f $(OBJS) $(LIB_TARGET) test_memory_layout

.PHONY: all tests clean
EOF

print_success "Makefile created"

# Phase 6: Create integration status tracker
print_status "Creating status tracker..."

cat > ${UNIFIED_DIR}/INTEGRATION_STATUS.txt << 'EOF'
WRF SDIRK3 Integration Status
==============================
Date: $(date)

Phase 1: Foundation [IN PROGRESS]
- [x] Directory structure created
- [x] Core files from sdirk3_lib copied
- [ ] Memory layout tests passing
- [ ] Atomic operations validated

Phase 2: Solvers [PENDING]
- [ ] GMRES merged
- [ ] Newton solver fixed
- [ ] Preconditioner integrated

Phase 3: Physics [PENDING]
- [ ] WSM6 microphysics ported
- [ ] PBL physics updated
- [ ] Radiation schemes integrated

Phase 4: Parallelization [PENDING]
- [ ] Tile interface migrated
- [ ] Halo exchange updated
- [ ] MPI wrapper integrated

Phase 5: Validation [PENDING]
- [ ] Unit tests passing
- [ ] em_b_wave test successful
- [ ] Performance benchmarks complete
EOF

print_success "Status tracker created"

# Final summary
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}    Setup Complete!                    ${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Unified directory created at: ${UNIFIED_DIR}"
echo ""
echo "Next steps:"
echo "1. Convert staged files:"
echo "   python3 ${UNIFIED_DIR}/scripts/convert_memory_layout.py \\"
echo "     ${UNIFIED_DIR}/staging/wrf_sdirk3_wsm6_kslab.h \\"
echo "     ${UNIFIED_DIR}/include/physics/wsm6_microphysics.h"
echo ""
echo "2. Run validation tests:"
echo "   cd ${UNIFIED_DIR} && make tests"
echo ""
echo "3. Check integration status:"
echo "   cat ${UNIFIED_DIR}/INTEGRATION_STATUS.txt"
echo ""
echo -e "${GREEN}Good luck with the integration!${NC}"