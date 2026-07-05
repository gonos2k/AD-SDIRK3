# SDIRK3 File-by-File Migration Map
## Detailed Component Integration Strategy

**Date**: 2024-01-16  
**Version**: 1.0  
**Purpose**: Concrete file mapping for merging libtorch_wrf/sdirk3 and sdirk3_lib

---

## 🎯 Migration Categories

### Category A: Use from sdirk3_lib AS-IS ✅
*These files have the correct (j,k,i) memory layout and improved design*

| Source File | Target Location | Priority | Notes |
|------------|-----------------|----------|-------|
| `sdirk3_lib/include/sdirk3/wrf_memory_layout_adapter.h` | `unified/core/memory_layout.h` | **CRITICAL** | Foundation for all tensor operations |
| `sdirk3_lib/include/sdirk3/wrf_atomic_operations_jvp.h` | `unified/core/atomic_operations.h` | **CRITICAL** | Custom JVP for mass weighting |
| `sdirk3_lib/include/sdirk3/wrf_acoustic_preconditioner.h` | `unified/solvers/acoustic_precond.h` | HIGH | Vertical coupling solver |
| `sdirk3_lib/include/sdirk3/memory_layout_converter.h` | `unified/core/layout_converter.h` | HIGH | Fortran-C++ conversion |
| `sdirk3_lib/docs/memory_layout_design_v2.md` | `unified/docs/` | HIGH | Critical documentation |

### Category B: Port from libtorch_wrf with MODIFICATIONS 🔧
*These files need memory layout updates from (k,j,i) to (j,k,i)*

| Source File | Modifications Required | Target Location | Priority |
|------------|----------------------|-----------------|----------|
| `libtorch_wrf/sdirk3/wrf_sdirk3_wsm6_kslab.h` | Update all tensor indexing [k][j][i] → [j][k][i] | `unified/physics/wsm6_microphysics.h` | HIGH |
| `libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.cpp` | Fix loop ordering, update halo indexing | `unified/parallel/tile_manager.cpp` | HIGH |
| `libtorch_wrf/sdirk3/wrf_sdirk3_halo_exchange.cpp` | Adjust ghost zone layout | `unified/parallel/halo_exchange.cpp` | MEDIUM |
| `libtorch_wrf/sdirk3/wrf_zero_copy_view.cpp` | Update view creation for new layout | `unified/interface/zero_copy.cpp` | HIGH |
| `libtorch_wrf/sdirk3/wrf_sdirk3_interface.cpp` | Adapt Fortran binding for new layout | `unified/interface/fortran_binding.cpp` | CRITICAL |

#### Specific Code Changes Example:
```cpp
// OLD (libtorch_wrf) - WRONG
for (int k = 0; k < nk; k++)
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++)
            tensor[k][j][i] = compute();

// NEW (unified) - CORRECT
for (int j = 0; j < nj; j++)
    for (int k = 0; k < nk; k++)
        for (int i = 0; i < ni; i++)
            tensor[j][k][i] = compute();
```

### Category C: MERGE Both Implementations 🔀
*Take best features from both codebases*

| Component | From sdirk3_lib | From libtorch_wrf | Target File | Priority |
|-----------|----------------|-------------------|-------------|----------|
| **Newton Solver** | No .item() usage | Mature algorithm | `unified/solvers/newton_krylov.cpp` | CRITICAL |
| **GMRES** | Gradient preservation | Restart strategy | `unified/solvers/gmres.cpp` | CRITICAL |
| **RHS Computation** | Atomic operations | Physics coupling | `unified/core/unified_rhs.cpp` | CRITICAL |
| **Config System** | Clean headers | Runtime options | `unified/core/config.h` | MEDIUM |

#### Merge Strategy for Newton Solver:
```cpp
// Combine best of both
class UnifiedNewtonSolver {
    // From libtorch_wrf: Robust convergence logic
    // From sdirk3_lib: Gradient flow preservation
    
    torch::Tensor solve(...) {
        // sdirk3_lib approach: Keep as tensor
        torch::Tensor res_norm = residual.norm();
        
        // libtorch_wrf logic: Advanced line search
        for (int ls = 0; ls < max_line_search; ls++) {
            // ... line search implementation
        }
    }
};
```

### Category D: CREATE NEW Components 🆕
*New unified interfaces and adapters*

| Component | Purpose | Target File | Priority |
|-----------|---------|-------------|----------|
| **Unified Builder** | Factory for solver creation | `unified/core/builder.h` | MEDIUM |
| **Migration Adapter** | Legacy compatibility layer | `unified/compat/legacy_adapter.h` | LOW |
| **Performance Monitor** | Track optimization metrics | `unified/utils/profiler.h` | LOW |
| **Test Harness** | Validation framework | `unified/test/harness.h` | HIGH |

---

## 📋 Detailed File Migration List

### Phase 1: Core Foundation (MUST DO FIRST)

```bash
# Commands to execute migration
mkdir -p /Users/yhlee/dWRF/external/sdirk3_unified/{include,src,test,docs}

# Step 1: Copy memory layout foundation
cp sdirk3_lib/include/sdirk3/wrf_memory_layout_adapter.h \
   sdirk3_unified/include/core/memory_layout.h

cp sdirk3_lib/include/sdirk3/wrf_atomic_operations_jvp.h \
   sdirk3_unified/include/core/atomic_operations.h

# Step 2: Create unified config
cat sdirk3_lib/include/sdirk3/sdirk3_config.h \
    libtorch_wrf/sdirk3/wrf_sdirk3_config.h \
    > sdirk3_unified/include/core/unified_config.h
```

### Phase 2: Solver Components

| Action | Source Files | Target | Script |
|--------|-------------|--------|--------|
| **Merge GMRES** | `libtorch_wrf/wrf_sdirk3_jvp_autograd_fixed.cpp` + `sdirk3_lib/newton_krylov_solver.h` | `unified/solvers/gmres_unified.cpp` | `merge_gmres.sh` |
| **Fix Newton** | `libtorch_wrf/wrf_sdirk3_newton_solver.cpp` | `unified/solvers/newton_fixed.cpp` | `fix_newton.py` |
| **Port Preconditioner** | `sdirk3_lib/wrf_acoustic_preconditioner.h` | `unified/solvers/precond.cpp` | Direct copy |

### Phase 3: Physics Migration

```python
# Python script to update physics files
import re

def update_tensor_indexing(filepath):
    """Update tensor indexing from [k][j][i] to [j][k][i]"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Pattern replacements
    patterns = [
        (r'tensor\[k\]\[j\]\[i\]', 'tensor[j][k][i]'),
        (r'for \(int k = 0; k < nk; k\+\+\)',
         'for (int j = 0; j < nj; j++)'),
        (r'\.unsqueeze\(0\)', '.unsqueeze(1)'),  # Broadcasting fix
    ]
    
    for old, new in patterns:
        content = re.sub(old, new, content)
    
    return content

# Files to process
physics_files = [
    'wrf_sdirk3_wsm6_kslab.h',
    'wrf_sdirk3_full_physics.cpp',
    'wrf_sdirk3_microphysics_vectorized.h'
]
```

### Phase 4: Build System Integration

```makefile
# Unified Makefile structure
UNIFIED_DIR = /Users/yhlee/dWRF/external/sdirk3_unified

# Source organization
CORE_SRCS = \
    $(UNIFIED_DIR)/src/core/memory_layout.cpp \
    $(UNIFIED_DIR)/src/core/atomic_operations.cpp \
    $(UNIFIED_DIR)/src/core/unified_config.cpp

SOLVER_SRCS = \
    $(UNIFIED_DIR)/src/solvers/newton_krylov.cpp \
    $(UNIFIED_DIR)/src/solvers/gmres.cpp \
    $(UNIFIED_DIR)/src/solvers/acoustic_precond.cpp

PHYSICS_SRCS = \
    $(UNIFIED_DIR)/src/physics/wsm6_microphysics.cpp \
    $(UNIFIED_DIR)/src/physics/pbl_physics.cpp \
    $(UNIFIED_DIR)/src/physics/radiation.cpp

PARALLEL_SRCS = \
    $(UNIFIED_DIR)/src/parallel/tile_manager.cpp \
    $(UNIFIED_DIR)/src/parallel/halo_exchange.cpp \
    $(UNIFIED_DIR)/src/parallel/mpi_wrapper.cpp
```

---

## 🚀 Execution Sequence

### Week 1-2: Foundation Setup
```bash
#!/bin/bash
# setup_foundation.sh

# 1. Create directory structure
./scripts/create_unified_structure.sh

# 2. Copy core files from sdirk3_lib
./scripts/copy_core_files.sh

# 3. Run memory layout tests
cd sdirk3_unified/test
./test_memory_layout

# 4. Validate atomic operations
./test_atomic_ops_jvp
```

### Week 3-4: Solver Migration
```bash
#!/bin/bash
# migrate_solvers.sh

# 1. Merge GMRES implementations
python3 scripts/merge_gmres.py \
    --autograd libtorch_wrf/sdirk3/wrf_sdirk3_jvp_autograd_fixed.cpp \
    --base sdirk3_lib/include/newton_krylov_solver.h \
    --output sdirk3_unified/src/solvers/gmres_unified.cpp

# 2. Fix Newton solver .item() usage
python3 scripts/remove_item_usage.py \
    --input libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp \
    --output sdirk3_unified/src/solvers/newton_fixed.cpp

# 3. Test convergence
./test_newton_convergence
```

### Week 5-6: Physics Port
```bash
#!/bin/bash
# port_physics.sh

# Update all physics files for new layout
for file in libtorch_wrf/sdirk3/wrf_sdirk3_*physics*.{h,cpp}; do
    python3 scripts/update_tensor_layout.py \
        --input "$file" \
        --output "sdirk3_unified/src/physics/$(basename $file)"
done

# Run physics validation
./test_wsm6_microphysics
./test_pbl_physics
```

### Week 7-8: Parallelization
```bash
#!/bin/bash
# setup_parallel.sh

# Port tile interface with layout fixes
python3 scripts/fix_tile_indexing.py \
    --input libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.cpp \
    --layout "j,k,i" \
    --output sdirk3_unified/src/parallel/tile_manager.cpp

# Test MPI communication
mpirun -np 4 ./test_halo_exchange
```

---

## 🧪 Validation Checkpoints

### After Each Phase:

| Phase | Test Command | Expected Result | Rollback Point |
|-------|-------------|-----------------|----------------|
| Foundation | `./test_memory_layout` | All 10 tests pass | Git tag: `foundation-v1` |
| Solvers | `./test_newton_gmres` | Convergence < 1e-6 | Git tag: `solvers-v1` |
| Physics | `./test_wsm6_accuracy` | Bit-reproducible | Git tag: `physics-v1` |
| Parallel | `mpirun -np 4 ./test_tiles` | Consistent results | Git tag: `parallel-v1` |

---

## ⚠️ Critical Migration Rules

### DO ✅
1. **ALWAYS** use (j,k,i) layout in new code
2. **TEST** after each file migration
3. **PRESERVE** gradient flow (no .item() in computation)
4. **DOCUMENT** all layout changes
5. **BENCHMARK** performance at each step

### DON'T ❌
1. **NEVER** use (k,j,i) layout
2. **AVOID** mixing layout conventions
3. **DON'T** skip validation tests
4. **NO** direct copies without layout check
5. **PREVENT** .item() usage in core paths

---

## 📊 Progress Tracking

### File Migration Status

| Component | Files | Migrated | Tested | Integrated |
|-----------|-------|----------|--------|------------|
| Core/Memory | 5 | ⏳ | ⏳ | ⏳ |
| Solvers | 8 | ⏳ | ⏳ | ⏳ |
| Physics | 12 | ⏳ | ⏳ | ⏳ |
| Parallel | 6 | ⏳ | ⏳ | ⏳ |
| Interface | 4 | ⏳ | ⏳ | ⏳ |
| **TOTAL** | **35** | **0/35** | **0/35** | **0/35** |

---

## 🔧 Helper Scripts

### 1. Layout Converter Script
```python
# scripts/convert_layout.py
def convert_file(input_path, output_path):
    """Convert file from (k,j,i) to (j,k,i) layout"""
    # Implementation provided separately
```

### 2. Item Removal Script
```python
# scripts/remove_item.py
def remove_item_usage(input_path, output_path):
    """Remove .item() calls preserving functionality"""
    # Implementation in GRADIENT_PRESERVATION_GUIDE.md
```

### 3. Validation Script
```bash
# scripts/validate_migration.sh
#!/bin/bash
# Run all validation tests
```

---

## 📅 Daily Execution Plan

### Day 1-3: Foundation
- [ ] Setup directory structure
- [ ] Copy memory layout files
- [ ] Test basic tensor operations

### Day 4-7: Core Components
- [ ] Migrate atomic operations
- [ ] Setup config system
- [ ] Initial integration tests

### Day 8-14: Solvers
- [ ] Merge GMRES
- [ ] Fix Newton solver
- [ ] Integrate preconditioner

### Day 15-21: Physics
- [ ] Port WSM6
- [ ] Update PBL
- [ ] Migrate radiation

### Day 22-28: Parallel
- [ ] Fix tile interface
- [ ] Update halo exchange
- [ ] Test MPI

### Day 29-35: Integration
- [ ] Full system test
- [ ] Performance tuning
- [ ] Documentation

---

**Next Action**: Execute `setup_foundation.sh` to begin migration