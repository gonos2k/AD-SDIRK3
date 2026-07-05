# WRF SDIRK3 Unified Integration Plan
## Merging libtorch_wrf/sdirk3 and sdirk3_lib Codebases

**Date**: 2024-01-16  
**Version**: 1.0  
**Status**: PLANNING

---

## Executive Summary

This document outlines the comprehensive plan to integrate two parallel SDIRK3 implementations:
1. **libtorch_wrf/sdirk3**: Mature production code (80% complete) with physics integration
2. **sdirk3_lib**: New design with corrected memory layout and improved JVP implementation

The integration will leverage the strengths of both codebases while addressing critical technical issues.

## Current State Analysis

### libtorch_wrf/sdirk3 (Mature Implementation)

#### Strengths ✅
- **60+ production-ready source files**
- **Complete physics integration**: WSM6 microphysics, PBL, radiation
- **Tile-based parallelization**: MPI and OpenMP support  
- **Zero-copy interface**: Efficient memory management
- **Halo exchange**: Domain decomposition support
- **Newton-Krylov solver**: Mature implementation with GMRES
- **Validated consistency**: 80% complete with verified results
- **WRF build integration**: Makefile and interface ready

#### Critical Issues ❌
- **INCORRECT memory layout**: Uses (k,j,i) instead of optimal (j,k,i)
- **Residual .item() usage**: Breaks gradient flow in some places
- **Suboptimal cache performance**: Due to memory layout

### sdirk3_lib (New Design)

#### Strengths ✅
- **CORRECT memory layout**: Proper (j,k,i) for optimal cache performance
- **Clean atomic operations**: Custom JVP for mass-weighted variables
- **Acoustic preconditioner**: Advanced vertical coupling solver
- **Modern design patterns**: Better separation of concerns
- **Comprehensive documentation**: Clear design rationale

#### Limitations ❌
- **No physics integration**: Missing microphysics, PBL, radiation
- **No parallelization**: Single-tile implementation only
- **Limited testing**: Design phase, not production-ready
- **No WRF interface**: Missing Fortran bindings

## Integration Architecture

### Core Principle
**Use sdirk3_lib's corrected memory layout as foundation, port libtorch_wrf's mature features**

### Unified Directory Structure
```
/Users/yhlee/dWRF/external/sdirk3_unified/
├── include/
│   ├── core/                    # From sdirk3_lib (corrected layout)
│   │   ├── memory_layout.h      # (j,k,i) layout adapter
│   │   ├── atomic_operations.h  # Custom JVP operations
│   │   └── acoustic_precond.h   # Vertical coupling
│   ├── solvers/                 # Merged from both
│   │   ├── newton_krylov.h      # From libtorch_wrf
│   │   ├── gmres.h              # Fixed .item() removal
│   │   └── sdirk3_solver.h      # Unified solver
│   ├── physics/                 # From libtorch_wrf
│   │   ├── wsm6_microphysics.h  # Ported with new layout
│   │   ├── pbl_physics.h        # Boundary layer
│   │   └── radiation.h          # Radiation schemes
│   ├── parallel/                # From libtorch_wrf
│   │   ├── tile_interface.h     # Tile parallelization
│   │   ├── halo_exchange.h      # Domain decomposition
│   │   └── mpi_wrapper.h        # MPI support
│   └── interface/               # WRF integration
│       ├── fortran_bindings.h   # C-Fortran interface
│       └── zero_copy_view.h     # Memory efficiency
├── src/
│   ├── [corresponding .cpp files]
├── tests/
│   ├── unit/                    # Component tests
│   ├── integration/             # System tests
│   └── validation/              # WRF comparisons
├── docs/
│   ├── architecture.md
│   ├── migration_guide.md
│   └── api_reference.md
└── build/
    ├── Makefile
    ├── CMakeLists.txt
    └── configure.wrf
```

## Migration Strategy

### Phase 1: Foundation Migration (Week 1-2)

#### 1.1 Memory Layout Unification
```cpp
// Priority: CRITICAL
// Source: sdirk3_lib/include/sdirk3/wrf_memory_layout_adapter.h
// Target: sdirk3_unified/include/core/memory_layout.h

class MemoryLayoutAdapter {
    // Use sdirk3_lib's corrected (j,k,i) layout
    torch::Tensor from_wrf_3d(float* wrf_data, ...) {
        auto tensor = torch::empty({nj, nk, ni}, torch::kFloat32);
        // Correct transformation from Fortran (i,k,j) to C++ (j,k,i)
        for (int j = 0; j < nj; j++) {
            for (int k = 0; k < nk; k++) {
                for (int i = 0; i < ni; i++) {
                    int wrf_idx = i + k * ni + j * ni * nk;
                    tensor[j][k][i] = wrf_data[wrf_idx];
                }
            }
        }
        return tensor;
    }
};
```

#### 1.2 Atomic Operations Port
```cpp
// Source: sdirk3_lib/include/sdirk3/wrf_atomic_operations_jvp.h
// Target: sdirk3_unified/include/core/atomic_operations.h

struct MassWeightingOp : torch::autograd::Function<MassWeightingOp> {
    static torch::Tensor forward(...) {
        // Port with corrected broadcasting
        auto mu_d_3d = mu_d.unsqueeze(1).expand({nj, nk, ni});
        return u * mu_d_3d;
    }
    
    static tensor_list backward(...) {
        // Custom JVP: δU = u·δμ_d + μ_d·δu
    }
};
```

### Phase 2: Solver Integration (Week 3-4)

#### 2.1 Newton-Krylov Merger
```cpp
// Merge best of both implementations
// Base: libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp
// Enhancements: sdirk3_lib gradient preservation

class NewtonKrylovSolver {
    // From libtorch_wrf: Mature algorithm
    // From sdirk3_lib: No .item() usage
    
    torch::Tensor solve(...) {
        // Keep values as tensors for gradient flow
        torch::Tensor res_norm = residual.norm();
        torch::Tensor converged = res_norm < tol;
        if (converged.all().item<bool>()) {  // Only for control flow
            break;
        }
    }
};
```

#### 2.2 GMRES Unification
- Use fixed version from GRADIENT_PRESERVATION_GUIDE.md
- Integrate with acoustic preconditioner from sdirk3_lib

### Phase 3: Physics Migration (Week 5-6)

#### 3.1 WSM6 Microphysics Port
```cpp
// Source: libtorch_wrf/sdirk3/wrf_sdirk3_wsm6_kslab.h
// Update all tensor operations for new (j,k,i) layout

class WSM6Microphysics {
    void compute_tendencies(const torch::Tensor& state) {
        // Original: tensor[k][j][i]
        // Updated:  tensor[j][k][i]
        
        // Update all loop orders
        for (int j = 0; j < nj; j++) {
            for (int k = 0; k < nk; k++) {
                for (int i = 0; i < ni; i++) {
                    // Physics computations
                }
            }
        }
    }
};
```

#### 3.2 PBL and Radiation
- Port with layout corrections
- Maintain physics accuracy
- Add JVP support for sensitivity analysis

### Phase 4: Parallelization (Week 7-8)

#### 4.1 Tile Interface Migration
```cpp
// Source: libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.cpp
// Update for new memory layout

class TileInterface {
    void process_tile(int tile_id) {
        // Update tile indexing for (j,k,i) layout
        // Maintain OpenMP parallelization
        #pragma omp parallel for
        for (int j = j_start; j <= j_end; j++) {
            // Process with correct layout
        }
    }
};
```

#### 4.2 Halo Exchange
- Port from libtorch_wrf with layout updates
- Maintain MPI communication patterns
- Update ghost zone indexing

### Phase 5: Build System (Week 9)

#### 5.1 Unified Makefile
```makefile
# Combine both build systems
# Base: libtorch_wrf/sdirk3/Makefile
# Add: sdirk3_lib components

UNIFIED_SOURCES = \
    # Core (from sdirk3_lib)
    src/core/memory_layout.cpp \
    src/core/atomic_operations.cpp \
    src/core/acoustic_precond.cpp \
    # Solvers (merged)
    src/solvers/newton_krylov.cpp \
    src/solvers/gmres.cpp \
    # Physics (from libtorch_wrf)
    src/physics/wsm6_microphysics.cpp \
    src/physics/pbl_physics.cpp \
    # Parallel (from libtorch_wrf)
    src/parallel/tile_interface.cpp \
    src/parallel/halo_exchange.cpp
```

#### 5.2 WRF Integration
```fortran
! Registry/Registry.EM_COMMON
package sdirk3_unified use_sdirk3_unified==1 - state:...

! namelist.input
&dynamics
 use_sdirk3_unified = .true.
 sdirk3_memory_layout = 'optimized'  ! (j,k,i)
/
```

## Validation Plan

### Phase 6: Testing & Validation (Week 10-12)

#### 6.1 Unit Tests
- [ ] Memory layout transformation correctness
- [ ] Atomic operations with JVP
- [ ] Newton-Krylov convergence
- [ ] Physics tendency accuracy
- [ ] Parallel tile consistency

#### 6.2 Integration Tests
- [ ] em_b_wave benchmark
- [ ] 10-day stability test
- [ ] Conservation properties
- [ ] Bit-reproducibility (FD mode)

#### 6.3 Performance Tests
- [ ] Cache efficiency measurement
- [ ] Parallel scaling analysis
- [ ] JVP performance comparison
- [ ] Memory usage profiling

## Risk Management

### Critical Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Layout migration errors | HIGH | Automated testing at each step |
| Physics accuracy loss | HIGH | Bit-comparison with reference |
| Performance regression | MEDIUM | Continuous profiling |
| Build system conflicts | LOW | Gradual integration |

### Rollback Strategy
1. Maintain both codebases during transition
2. Feature flags for gradual enablement
3. A/B testing capability
4. Version control with clear tags

## Success Metrics

### Technical Metrics
- ✅ All unit tests passing (100%)
- ✅ em_b_wave stability (10+ days)
- ✅ Conservation errors < 1e-10
- ✅ Cache hit rate > 85%
- ✅ Parallel efficiency > 80%

### Performance Targets
- 15% faster horizontal operations (layout improvement)
- 6% fewer GMRES iterations (better preconditioner)
- 3x larger timestep than RK3
- Net speedup > 1.5x

## Timeline Summary

| Phase | Weeks | Status | Description |
|-------|-------|--------|-------------|
| 1. Foundation | 1-2 | 🔄 | Memory layout, atomic ops |
| 2. Solvers | 3-4 | ⏳ | Newton-Krylov, GMRES |
| 3. Physics | 5-6 | ⏳ | WSM6, PBL, radiation |
| 4. Parallel | 7-8 | ⏳ | Tiles, halo, MPI |
| 5. Build | 9 | ⏳ | Makefile, WRF integration |
| 6. Validation | 10-12 | ⏳ | Testing, optimization |

**Total Duration**: 12 weeks (3 months)

## Implementation Checklist

### Immediate Actions (Week 1)
- [x] Create unified directory structure
- [x] Set up version control
- [ ] Port memory layout adapter
- [ ] Migrate atomic operations
- [ ] Set up CI/CD pipeline

### Short Term (Weeks 2-4)
- [ ] Complete solver merger
- [ ] Fix all .item() usage
- [ ] Initial integration tests
- [ ] Performance baseline

### Medium Term (Weeks 5-8)
- [ ] Physics migration complete
- [ ] Parallelization working
- [ ] em_b_wave running
- [ ] Optimization phase

### Long Term (Weeks 9-12)
- [ ] Full WRF integration
- [ ] Documentation complete
- [ ] Release candidate
- [ ] Production deployment

## Technical Decisions

### Key Design Choices

1. **Memory Layout**: Use sdirk3_lib's (j,k,i) - FINAL
2. **Solver Base**: libtorch_wrf's Newton-Krylov with fixes
3. **Physics**: Port all from libtorch_wrf with layout updates
4. **Parallelization**: Preserve libtorch_wrf's tile approach
5. **Build System**: Enhanced Makefile with CMake option

### Technology Stack
- **C++17**: Core implementation
- **LibTorch**: Tensor operations and AD
- **OpenMP**: Thread parallelization
- **MPI**: Domain decomposition
- **Fortran**: WRF interface

## Documentation Requirements

### Developer Documentation
- Architecture overview
- API reference
- Migration guide from old layout
- Performance tuning guide

### User Documentation
- Installation instructions
- Configuration options
- Namelist parameters
- Troubleshooting guide

## Quality Assurance

### Code Review Process
1. All changes require review
2. Automated testing before merge
3. Performance regression checks
4. Physics validation required

### Continuous Integration
```yaml
# .github/workflows/ci.yml
on: [push, pull_request]
jobs:
  test:
    - unit-tests
    - integration-tests
    - performance-tests
    - physics-validation
```

## Conclusion

This integration plan merges the mature implementation from libtorch_wrf/sdirk3 with the corrected design from sdirk3_lib, creating a unified, optimized SDIRK3 solver for WRF. The 12-week timeline is aggressive but achievable with proper resource allocation and disciplined execution.

### Critical Success Factors
1. **Correct memory layout** from sdirk3_lib
2. **Mature physics** from libtorch_wrf
3. **Gradient preservation** for AD/ML integration
4. **Performance optimization** throughout
5. **Comprehensive validation** at each phase

---

**Next Step**: Begin Phase 1 with memory layout adapter migration

**Contact**: WRF-SDIRK3 Integration Team  
**Repository**: /Users/yhlee/dWRF/external/sdirk3_unified/