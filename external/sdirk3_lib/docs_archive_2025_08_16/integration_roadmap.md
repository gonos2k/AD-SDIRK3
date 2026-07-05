# WRF SDIRK3 Integration Roadmap

## Executive Summary
Complete integration plan for WRF SDIRK3 with flux-form equations, proper memory layout handling, and JVP-AD implementation.

## Phase 1: Foundation (Week 1-2)

### 1.1 Memory Layout Adapter ✅
**Files:** `wrf_memory_layout_adapter.h`
- [x] Implement (i,k,j) → (nk,nj,ni) transformation
- [x] Handle C-grid staggering correctly
- [x] Optimize for cache performance
- [ ] Unit tests for all transformations
- [ ] Benchmark memory access patterns

### 1.2 Base State Management
```cpp
// Implementation checklist:
- [ ] Load base state from WRF initialization
- [ ] Apply stop_gradient to all base variables
- [ ] Verify perturbation separation: φ = φ₀ + φ'
- [ ] Test mass conservation with perturbation form
```

### 1.3 Fortran-C++ Interface
```fortran
! wrf_sdirk3_interface.F90
module wrf_sdirk3_interface
  use iso_c_binding
  implicit none
  
  interface
    subroutine wrf_sdirk3_init_c(grid) bind(C, name="wrf_sdirk3_init")
      use module_domain, only : domain
      type(domain), intent(inout) :: grid
    end subroutine
    
    subroutine wrf_sdirk3_step_c(grid, dt) bind(C, name="wrf_sdirk3_step")
      use module_domain, only : domain
      type(domain), intent(inout) :: grid
      real(c_double), value :: dt
    end subroutine
  end interface
  
end module
```

## Phase 2: Atomic Operations (Week 3-4)

### 2.1 Mass-Weighted Transformations ✅
**Files:** `wrf_atomic_operations_jvp.h`
- [x] MassWeightingOp with custom JVP
- [x] MassDeweightingOp with safe division
- [ ] Test with WRF coupled variables
- [ ] Verify δU = u·δμ_d + μ_d·δu

### 2.2 Flux Divergence
```cpp
// Critical implementation points:
- [ ] Match WRF's flux5/flux6 exactly
- [ ] Handle upwind biasing correctly
- [ ] Apply map scale factors properly
- [ ] Frozen limiters for JVP consistency
```

### 2.3 Diagnostic Equations
- [ ] Equation of State with custom JVP
- [ ] Hydrostatic balance solver
- [ ] Pressure gradient (perturbation form)
- [ ] Taylor test validation

## Phase 3: Acoustic Preconditioner (Week 5-6)

### 3.1 Vertical Coupling ✅
**Files:** `wrf_acoustic_preconditioner.h`
- [x] Tridiagonal system setup
- [x] Thomas algorithm implementation
- [ ] Hydrostatic constraint coupling
- [ ] Column-wise parallel solve

### 3.2 Horizontal Smoothing
- [ ] 2D elliptic operator
- [ ] Multigrid V-cycle
- [ ] Divergence damping
- [ ] Boundary condition handling

### 3.3 Schur Complement
```cpp
// Implementation tasks:
- [ ] (U,V) ↔ (W,φ) coupling approximation
- [ ] Low-order Schur complement
- [ ] Performance optimization
```

## Phase 4: Newton-Krylov Integration (Week 7-8)

### 4.1 SDIRK3 Stages
```cpp
// Per stage implementation:
for (int stage = 0; stage < 3; stage++) {
    // [ ] Setup stage residual
    // [ ] Newton iteration with adaptive tolerance
    // [ ] GMRES solve with preconditioner
    // [ ] Convergence monitoring
}
```

### 4.2 Adaptive Time Stepping
- [ ] Error estimation
- [ ] Step size control
- [ ] Rejection handling
- [ ] Statistics collection

## Phase 5: Validation (Week 9-10)

### 5.1 Unit Tests
```bash
# Test sequence:
./test_memory_layout      # Memory transformations
./test_jvp_rules         # JVP accuracy
./test_mass_conservation # Conservation properties
./test_preconditioner    # Solver convergence
```

### 5.2 Idealized Tests
- [ ] em_hill2d_x (2D mountain wave)
- [ ] em_quarter_ss (supercell)
- [ ] em_b_wave (baroclinic wave)
- [ ] em_convrad (radiation)

### 5.3 Real Data Tests
- [ ] 12km CONUS domain
- [ ] 3km convection-permitting
- [ ] 1km LES configuration

## Phase 6: Optimization (Week 11-12)

### 6.1 Performance Tuning
```cpp
// Optimization targets:
- [ ] Vectorize i-dimension loops
- [ ] OpenMP parallelization
- [ ] NUMA-aware memory allocation
- [ ] GPU offloading (future)
```

### 6.2 Memory Optimization
- [ ] Reduce temporary allocations
- [ ] Reuse workspace tensors
- [ ] Optimize tensor operations
- [ ] Profile with VTune/NSight

## Critical Path Items

### Must Complete First:
1. **Memory layout adapter** - Everything depends on this
2. **Base state separation** - Required for stability
3. **Mass-weighted JVP** - Core of flux-form equations
4. **Vertical preconditioner** - Essential for convergence

### Can Parallelize:
- Atomic operations implementation
- Unit test development
- Documentation writing
- Performance profiling

### Risk Mitigation:

**Risk 1: Memory layout bugs**
- Mitigation: Extensive unit tests comparing with WRF arrays
- Validation: Bit-reproducible results for simple cases

**Risk 2: Newton convergence failure**
- Mitigation: Start with simplified physics
- Fallback: Increase GMRES iterations, relax tolerances

**Risk 3: Conservation violations**
- Mitigation: Monitor mass/energy every timestep
- Fix: Check JVP rules for mass-weighted variables

**Risk 4: Performance regression**
- Mitigation: Profile early and often
- Optimization: Focus on hot spots identified by profiler

## Integration with WRF Build System

### Registry Modifications
```
# Registry/Registry.EM_COMMON
state   real   ru_m          ikj     dyn_em      1     -     -
state   real   rv_m          ikj     dyn_em      1     -     -
state   real   ww_m          ikj     dyn_em      1     -     -

package sdirk3_solver       use_sdirk3==1       -       state:ru_m,rv_m,ww_m
```

### Configure Script
```bash
# arch/configure.defaults
# Add libtorch flags for option 37
LIBTORCH_PATH = /path/to/libtorch
LIBTORCH_INC = -I$(LIBTORCH_PATH)/include
LIBTORCH_LIB = -L$(LIBTORCH_PATH)/lib -ltorch -lc10
```

### Makefile Integration
```makefile
# external/sdirk3_lib/Makefile
OBJS = wrf_memory_layout_adapter.o \
       wrf_atomic_operations_jvp.o \
       wrf_acoustic_preconditioner.o \
       wrf_sdirk3_solver.o

libsdirk3.a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)
	$(RANLIB) $@
```

## Testing Protocol

### Daily Tests:
```bash
#!/bin/bash
# daily_test.sh
cd $WRF_DIR/test/em_b_wave
./ideal.exe
mpirun -np 4 ./wrf.exe
python validate_conservation.py
```

### Weekly Tests:
- Full 10-day baroclinic wave
- Memory leak detection (valgrind)
- Performance regression tests
- Different domain decompositions

### Release Tests:
- All idealized cases
- Real data case (GFS initial conditions)
- Strong/weak scaling analysis
- Comparison with operational configuration

## Documentation Requirements

### Code Documentation:
- Doxygen comments for all public APIs
- Mathematical derivation for each JVP rule
- Memory layout diagrams
- Performance considerations

### User Documentation:
- Quick start guide
- Namelist options reference
- Troubleshooting guide
- Performance tuning guide

### Developer Documentation:
- Architecture overview
- Adding new physics modules
- Debugging convergence issues
- Contributing guidelines

## Success Metrics

### Functionality:
- ✅ Passes all WRF regression tests
- ✅ Conserves mass to machine precision
- ✅ Newton convergence in ≤4 iterations
- ✅ Stable for CFL > 1

### Performance:
- ⚡ < 2x overhead vs RK3 for same timestep
- ⚡ Net speedup with 3x larger timestep
- ⚡ Scales to 1000+ cores
- ⚡ Memory usage < 1.5x RK3

### Maintainability:
- 📚 100% documented public APIs
- 🧪 >80% test coverage
- 🔧 Automated CI/CD pipeline
- 📊 Performance monitoring dashboard

## Timeline Summary

| Phase | Duration | Deliverable | Status |
|-------|----------|-------------|--------|
| 1. Foundation | 2 weeks | Memory adapter, base state | 🟡 In Progress |
| 2. Atomic Ops | 2 weeks | JVP rules, flux divergence | 🟡 In Progress |
| 3. Preconditioner | 2 weeks | Acoustic solver | ⚪ Planned |
| 4. Integration | 2 weeks | Newton-Krylov | ⚪ Planned |
| 5. Validation | 2 weeks | Test suite | ⚪ Planned |
| 6. Optimization | 2 weeks | Performance | ⚪ Planned |

**Total Duration:** 12 weeks
**Current Week:** 2
**Completion Target:** [Date]

## Next Steps

### Immediate (This Week):
1. Complete memory layout adapter unit tests
2. Implement flux divergence with correct staggering
3. Test mass-weighted JVP rules
4. Begin vertical preconditioner implementation

### Next Week:
1. Complete atomic operations
2. Integrate with Newton solver
3. Run first em_b_wave test
4. Profile memory access patterns

### Following Week:
1. Debug convergence issues
2. Optimize preconditioner
3. Add real physics modules
4. Document APIs

---

**Contact:** [Developer Name]
**Repository:** https://github.com/[org]/dWRF
**Issue Tracker:** https://github.com/[org]/dWRF/issues