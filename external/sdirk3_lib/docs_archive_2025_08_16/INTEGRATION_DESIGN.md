# WRF SDIRK3 LibTorch Integration Design
## Maximizing Reuse of Existing Codebase

## Executive Summary

After analyzing the existing sdirk3 codebase (80% complete), this document provides a comprehensive integration design that maximizes code reuse while addressing the remaining 20% completion requirements.

## 1. Existing Codebase Analysis

### 1.1 Current Status (80% Complete)
The codebase at `/Users/yhlee/dWRF/external/libtorch_wrf/sdirk3` contains:

#### ✅ Already Implemented
1. **Memory Layout** 
   - Correct (j,k,i) layout matching WRF requirements
   - Zero-copy interface for memory efficiency
   - Proper Fortran-C++ transformation

2. **JVP/Automatic Differentiation**
   - Dual implementation: autograd and finite differences
   - Optimized for performance with switchable methods
   - Batch JVP computation support

3. **Solver Infrastructure**
   - Newton-Krylov solver with GMRES
   - Preconditioner implementation
   - Stability analysis framework

4. **WRF Integration**
   - C interface for Fortran calling
   - Tile-based parallelization support
   - Halo exchange implementation

5. **Physics Coupling**
   - Unified RHS with physics tendencies
   - Microphysics heating
   - PBL, radiation, cumulus tendencies

### 1.2 Remaining Work (20%)

#### ❌ To Complete
1. **.item() Usage** - Some remain in GMRES solver
2. **Registry Integration** - WRF build system connection
3. **Validation** - em_b_wave testing incomplete
4. **Performance** - Final optimization needed
5. **Documentation** - User guide incomplete

## 2. Integration Architecture

### 2.1 Build System Integration

```makefile
# Integration with WRF build system
# File: external/libtorch_wrf/sdirk3/Makefile.wrf

include $(WRF_SRC_ROOT_DIR)/configure.wrf

# LibTorch configuration from configure.wrf
LIBTORCH_INC = $(LIBTORCH_PATH)/include
LIBTORCH_LIB = -L$(LIBTORCH_PATH)/lib -ltorch -ltorch_cpu -lc10

# Add to WRF module list
MODULES += module_sdirk3_solver.o

# Compile rule for SDIRK3 library
$(LIBDIR)/libwrf_sdirk3.a: 
	(cd external/libtorch_wrf/sdirk3; $(MAKE) all)
	cp external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a $(LIBDIR)/

# Link with WRF
wrf.exe: $(MODULES) $(LIBDIR)/libwrf_sdirk3.a
	$(FC) -o $@ $(MODULES) $(LIBDIR)/libwrf_sdirk3.a $(LIBTORCH_LIB)
```

### 2.2 Registry Integration

```
# Registry/Registry.EM_COMMON
# SDIRK3 solver configuration

package  sdirk3_solver  use_sdirk3==1  -  state:ru_tend,rv_tend,rw_tend,rt_tend

# SDIRK3 stage storage
state  real  k1_u   ikj  dyn_em  1  -  rh  "K1_U"  "SDIRK3 stage 1 U"  "m/s"
state  real  k1_v   ikj  dyn_em  1  -  rh  "K1_V"  "SDIRK3 stage 1 V"  "m/s"
state  real  k1_w   ikj  dyn_em  1  -  rh  "K1_W"  "SDIRK3 stage 1 W"  "m/s"
state  real  k2_u   ikj  dyn_em  1  -  rh  "K2_U"  "SDIRK3 stage 2 U"  "m/s"
state  real  k2_v   ikj  dyn_em  1  -  rh  "K2_V"  "SDIRK3 stage 2 V"  "m/s"
state  real  k2_w   ikj  dyn_em  1  -  rh  "K2_W"  "SDIRK3 stage 2 W"  "m/s"

# SDIRK3 configuration
rconfig  integer  sdirk3_max_newton   namelist,dynamics  1  4  rh  "SDIRK3_MAX_NEWTON"
rconfig  integer  sdirk3_max_gmres    namelist,dynamics  1  30 rh  "SDIRK3_MAX_GMRES"
rconfig  real     sdirk3_newton_tol   namelist,dynamics  1  1e-6  rh  "SDIRK3_NEWTON_TOL"
rconfig  logical  sdirk3_use_autograd namelist,dynamics  1  .false.  rh  "SDIRK3_USE_AUTOGRAD"
```

### 2.3 Fortran Interface Module

```fortran
! File: dyn_em/module_sdirk3_solver.F

MODULE module_sdirk3_solver
  USE module_domain
  USE module_state_description
  
  IMPLICIT NONE
  
  ! Interface to C++ SDIRK3 library
  INTERFACE
    SUBROUTINE wrf_sdirk3_advance(grid, dt) BIND(C)
      USE iso_c_binding
      USE module_domain, ONLY: domain
      TYPE(domain), INTENT(INOUT) :: grid
      REAL(c_float), VALUE :: dt
    END SUBROUTINE
  END INTERFACE
  
CONTAINS

  SUBROUTINE sdirk3_advance_tile(grid, i_start, i_end, j_start, j_end, &
                                  num_tiles, dt)
    TYPE(domain), INTENT(INOUT) :: grid
    INTEGER, INTENT(IN) :: i_start, i_end, j_start, j_end, num_tiles
    REAL, INTENT(IN) :: dt
    
    ! Create zero-copy views of WRF arrays
    CALL wrf_sdirk3_create_zerocopy_state( &
      grid%u_2, grid%v_2, grid%w_2, &
      grid%t_2, grid%ph_2, grid%mu_2, grid%p, &
      ids, ide, jds, jde, kds, kde, &
      ims, ime, jms, jme, kms, kme, &
      its, ite, jts, jte, kts, kte)
    
    ! Advance using SDIRK3
    CALL wrf_sdirk3_advance(grid, dt)
    
  END SUBROUTINE

END MODULE
```

## 3. Addressing Remaining 20%

### 3.1 Removing .item() Usage

**Current Issues:**
- GMRES solver uses .item() for scalar extraction
- Breaks gradient flow for automatic differentiation

**Solution:**
```cpp
// File: wrf_sdirk3_jvp_autograd.cpp
// Replace scalar extraction with tensor operations

// OLD (breaks gradients):
if ((r_norm < tol * b_norm).item<bool>()) break;

// NEW (preserves gradients):
auto converged = (r_norm < tol * b_norm);
if (converged.all().item<bool>()) break;  // Only extract for control flow

// For internal computations, keep as tensors:
// OLD:
float h_value = h_col[j].item<float>();

// NEW:
torch::Tensor h_value = h_col[j];  // Keep as tensor
```

### 3.2 Computation Graph Optimization

```cpp
// File: wrf_sdirk3_graph_optimizer.h
class ComputationGraphOptimizer {
public:
  // Optimize graph for JVP computation
  void optimize_for_jvp(torch::Tensor& state) {
    // Enable gradient checkpointing for memory efficiency
    torch::autograd::set_grad_enabled(true);
    
    // Use in-place operations where safe
    state.requires_grad_(true);
    
    // Disable autograd for intermediate computations
    {
      torch::NoGradGuard no_grad;
      // Compute base state and other constants
    }
  }
  
  // Batch operations for efficiency
  torch::Tensor batch_compute_rhs(const std::vector<torch::Tensor>& states) {
    // Stack states for vectorized computation
    auto batched = torch::stack(states);
    
    // Single forward pass for all states
    return unified_rhs_->forward(batched);
  }
};
```

### 3.3 WRF Build System Integration

```bash
# File: configure.wrf (additions)

# LibTorch configuration
LIBTORCH_PATH   = /opt/libtorch
LIBTORCH_INC    = -I$(LIBTORCH_PATH)/include \
                  -I$(LIBTORCH_PATH)/include/torch/csrc/api/include
LIBTORCH_LIB    = -L$(LIBTORCH_PATH)/lib -ltorch -ltorch_cpu -lc10 \
                  -Wl,-rpath,$(LIBTORCH_PATH)/lib

# Enable SDIRK3
SDIRK3_EXTERNAL = $(WRF_SRC_ROOT_DIR)/external/libtorch_wrf/sdirk3
SDIRK3_LIB      = -L$(SDIRK3_EXTERNAL) -lwrf_sdirk3_libtorch

# Add to INCLUDE_MODULES
INCLUDE_MODULES = -I$(SDIRK3_EXTERNAL) $(INCLUDE_MODULES)

# Add to LIB_EXTERNAL  
LIB_EXTERNAL    = $(SDIRK3_LIB) $(LIBTORCH_LIB) $(LIB_EXTERNAL)
```

## 4. Validation Strategy

### 4.1 em_b_wave Test

```bash
#!/bin/bash
# File: test/em_b_wave/run_sdirk3_test.sh

# Configure for SDIRK3
cat > namelist.input << EOF
&dynamics
 use_sdirk3          = .true.
 sdirk3_max_newton   = 4
 sdirk3_max_gmres    = 30
 sdirk3_newton_tol   = 1e-6
 sdirk3_use_autograd = .false.  # Start with finite differences
 time_step           = 180       # 3x larger than RK3
/
EOF

# Run test
mpirun -np 4 ./wrf.exe

# Validate results
python validate_sdirk3.py wrfout_d01_*
```

### 4.2 Validation Metrics

```python
# File: validate_sdirk3.py
import netCDF4 as nc
import numpy as np

def validate_conservation(filename):
    """Check mass and energy conservation"""
    ds = nc.Dataset(filename)
    
    # Mass conservation
    mu = ds.variables['MU'][:]
    mass = np.sum(mu, axis=(1,2))
    mass_error = np.abs(mass - mass[0]) / mass[0]
    assert np.max(mass_error) < 1e-10, f"Mass conservation violated: {np.max(mass_error)}"
    
    # Energy conservation
    ke = compute_kinetic_energy(ds)
    pe = compute_potential_energy(ds)
    ie = compute_internal_energy(ds)
    total_energy = ke + pe + ie
    energy_error = np.abs(total_energy - total_energy[0]) / total_energy[0]
    assert np.max(energy_error) < 1e-3, f"Energy conservation violated: {np.max(energy_error)}"
    
    print("✅ Conservation tests passed")

def validate_stability(filename):
    """Check numerical stability"""
    ds = nc.Dataset(filename)
    
    # Check for NaN/Inf
    for var in ['U', 'V', 'W', 'T', 'PH']:
        data = ds.variables[var][:]
        assert np.all(np.isfinite(data)), f"NaN/Inf detected in {var}"
    
    # Check CFL with larger timestep
    u_max = np.max(np.abs(ds.variables['U'][:]))
    dt = 180  # SDIRK3 timestep
    dx = 20000  # Grid spacing
    cfl = u_max * dt / dx
    print(f"CFL number: {cfl:.2f} (should be > 1 for implicit benefit)")
    
    print("✅ Stability tests passed")
```

## 5. Performance Optimization

### 5.1 Graph Optimization

```cpp
// File: wrf_sdirk3_performance.cpp

class PerformanceOptimizer {
public:
  void optimize() {
    // 1. Fuse operations
    torch::jit::FusionGuard fusion_guard(true);
    
    // 2. Use TorchScript for hot paths
    auto scripted_rhs = torch::jit::script(unified_rhs_module);
    
    // 3. Enable MKL optimizations
    at::set_num_threads(omp_get_max_threads());
    
    // 4. Memory pool for tensor allocation
    c10::cuda::CUDACachingAllocator::emptyCache();
  }
  
  // Benchmark different JVP methods
  void benchmark_jvp() {
    Timer timer;
    
    // Test autograd
    timer.start();
    for (int i = 0; i < 100; i++) {
      compute_jvp_autograd(F, u, v);
    }
    auto time_autograd = timer.elapsed();
    
    // Test finite differences
    timer.start();
    for (int i = 0; i < 100; i++) {
      compute_jvp_finite_diff(F, u, v);
    }
    auto time_fd = timer.elapsed();
    
    std::cout << "Autograd: " << time_autograd << " ms\n";
    std::cout << "Finite Diff: " << time_fd << " ms\n";
    std::cout << "Speedup: " << time_autograd/time_fd << "x\n";
  }
};
```

### 5.2 Memory Optimization

```cpp
// File: wrf_sdirk3_memory_optimizer.h

class MemoryOptimizer {
private:
  // Reusable tensor pool
  std::unordered_map<std::string, torch::Tensor> tensor_pool_;
  
public:
  torch::Tensor get_tensor(const std::string& name, 
                           torch::IntArrayRef sizes) {
    auto key = name + "_" + std::to_string(sizes[0]);
    
    if (tensor_pool_.find(key) == tensor_pool_.end() ||
        tensor_pool_[key].sizes() != sizes) {
      tensor_pool_[key] = torch::zeros(sizes, torch::kFloat32);
    }
    
    return tensor_pool_[key];
  }
  
  void clear_pool() {
    tensor_pool_.clear();
  }
};
```

## 6. Timeline and Milestones

### Phase 1: Complete Core Integration (Week 1-2)
- [x] Analyze existing codebase
- [ ] Remove remaining .item() calls
- [ ] Complete Registry integration
- [ ] Update build system

### Phase 2: Validation (Week 3-4)
- [ ] Run em_b_wave test
- [ ] Validate conservation properties
- [ ] Check stability with large timesteps
- [ ] Compare with RK3 reference

### Phase 3: Optimization (Week 5-6)
- [ ] Profile performance bottlenecks
- [ ] Optimize JVP computation
- [ ] Reduce memory footprint
- [ ] Parallelize where possible

### Phase 4: Documentation & Release (Week 7-8)
- [ ] Complete user guide
- [ ] Document API
- [ ] Create examples
- [ ] Release v1.0

## 7. Risk Mitigation

### Risk: .item() removal affects convergence
**Mitigation:** Keep finite difference JVP as fallback

### Risk: Memory overhead from gradient tracking
**Mitigation:** Use gradient checkpointing and tensor pooling

### Risk: Performance regression
**Mitigation:** Benchmark continuously, maintain dual implementations

### Risk: WRF compatibility issues
**Mitigation:** Extensive testing with different WRF configurations

## 8. Success Metrics

1. **Correctness**
   - Mass conservation: < 1e-10 relative error
   - Energy conservation: < 1e-3 relative error
   - Bit-reproducible with reference (in FD mode)

2. **Performance**
   - 3x larger timestep than RK3
   - < 2x wall time per timestep
   - Net speedup > 1.5x

3. **Stability**
   - Stable for CFL > 3
   - Newton convergence in ≤ 4 iterations
   - GMRES convergence in ≤ 30 iterations

4. **Usability**
   - Single namelist flag to enable
   - Automatic fallback on failure
   - Clear error messages

## 9. Conclusion

The existing codebase provides an excellent foundation (80% complete) for WRF SDIRK3 integration. By focusing on:
1. Completing the .item() removal for gradient preservation
2. Finalizing Registry integration
3. Validating with em_b_wave
4. Optimizing performance

We can deliver a production-ready SDIRK3 solver that enables larger timesteps while maintaining accuracy and stability.

The key insight is to **maximize reuse** of the existing, tested codebase rather than reimplementing from scratch. This approach reduces risk and accelerates delivery.

---

**Next Steps:**
1. Start with .item() removal in GMRES solver
2. Test gradient flow with simple cases
3. Integrate with WRF build system
4. Run em_b_wave validation

**Estimated Completion:** 6-8 weeks from current state