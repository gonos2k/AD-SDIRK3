# WRF SDIRK3 Validation Framework

> **HISTORICAL RECORD (dated snapshot).** Solver/API descriptions in this
> document (e.g. the pre-FGMRES GMRES/BiCGSTAB era) reflect the code as of
> the date of this record, not the current contract. For the current state
> (FGMRES solver, MPI support boundary, CTest inventory) see the repository
> root `README.md` and `external/libtorch_wrf/sdirk3/README.md`.


## 1. Unit Tests (컬럼 단위)

### 1.1 Memory Layout Tests
```cpp
TEST(MemoryLayout, FortranToCppConversion) {
    // Test (i,k,j) → (nk,nj,ni) conversion
    // Verify zero-copy where possible
    // Check staggered grid dimensions
}

TEST(MemoryLayout, CachePerformance) {
    // Measure cache misses for different access patterns
    // Verify i-dimension contiguity is preserved
}
```

### 1.2 Atomic Operations JVP Tests
```cpp
TEST(JVP, MassWeighting) {
    // Test: δU = u * δμ_d + μ_d * δu
    // Compare AD vs analytical JVP
    // Tolerance: ||AD - Analytical|| < 1e-7
}

TEST(JVP, EquationOfState) {
    // Test: δp = γ * p * (δθ_m/θ_m - δα_d/α_d)
    // Verify perturbation pressure calculation
}

TEST(JVP, HydrostaticBalance) {
    // Test: ∂(δφ)/∂η = -(δα_d * μ_d + α_d * δμ_d)
    // Column integration accuracy
}
```

### 1.3 Taylor Test for JVP Accuracy
```cpp
void taylor_test_jvp() {
    // For decreasing h = 2^(-k), k = 1, ..., 10
    // Check: ||F(x + h*v) - F(x)|| = O(h)
    // Check: ||F(x + h*v) - F(x) - h*JVP(x,v)|| = O(h²)
}
```

## 2. 2D Tests (Gravity Wave)

### 2.1 Mountain Wave Test
```bash
# Configuration: em_hill2d_x
# Domain: 100km × 20km
# Resolution: dx = 1km, dz = 500m
# Mountain: h₀ = 250m, half-width = 10km
# Background flow: U₀ = 10 m/s, N = 0.01 s⁻¹
```

**Validation Metrics:**
- Wave amplitude vs analytical solution
- Phase speed accuracy
- Energy conservation: |E(t) - E(0)|/E(0) < 1e-4
- Mass conservation: |M(t) - M(0)|/M(0) < 1e-10

## 3. 3D Tests (em_b_wave)

### 3.1 Test Configuration
```fortran
! namelist.input modifications
&time_control
 run_days                   = 10,        ! Long integration
 run_hours                  = 0,
 run_minutes                = 0,
 run_seconds                = 0,
 start_year                 = 0001,
 start_month                = 01,
 start_day                  = 01,
 start_hour                 = 00,
 history_interval           = 360,       ! Output every 6 hours
 frames_per_outfile         = 1,
 restart                    = .false.,
 restart_interval           = 1440,
 io_form_history            = 2
 io_form_restart            = 2
 io_form_input              = 2
 io_form_boundary           = 2
/

&dynamics
 rk_ord                     = 3,         ! Compare with RK3
 diff_opt                   = 1,
 km_opt                     = 4,
 diff_6th_opt               = 2,
 diff_6th_factor            = 0.12,
 base_temp                  = 290.,
 damp_opt                   = 3,         ! Upper boundary damping
 zdamp                      = 5000.,
 dampcoef                   = 0.2,
 w_damping                  = 1,
 khdif                      = 0,
 kvdif                      = 0,
 non_hydrostatic            = .true.,
 moist_adv_opt              = 1,
 scalar_adv_opt             = 1,
 use_sdirk3                 = .true.,    ! Enable SDIRK3
/
```

### 3.2 Monitoring Script
```bash
#!/bin/bash
# monitor_em_b_wave.sh

# Extract key metrics from rsl.error.0000
grep "SDIRK3: Newton" rsl.error.0000 > newton_convergence.log
grep "SDIRK3: GMRES" rsl.error.0000 > gmres_iterations.log
grep "Mass conservation" rsl.error.0000 > mass_conservation.log
grep "Energy" rsl.error.0000 > energy_conservation.log

# Plot convergence history
python plot_convergence.py newton_convergence.log
python plot_conservation.py mass_conservation.log energy_conservation.log

# Compare with RK3 reference
ncl compare_sdirk3_rk3.ncl wrfout_d01_* wrfout_rk3_*
```

### 3.3 Validation Criteria

**Convergence Metrics:**
- Newton iterations per stage: ≤ 4
- GMRES iterations per Newton: ≤ 30
- Relative residual: ||R||/||K|| < 1e-6

**Conservation Metrics:**
- Mass: |ΔM|/M₀ < 1e-10 over 10 days
- Energy: |ΔE|/E₀ < 1e-3 over 10 days
- Potential vorticity: Ertel PV conserved along trajectories

**Accuracy Metrics:**
- Compare with RK3: ||u_SDIRK3 - u_RK3||/||u_RK3|| < 0.01
- Baroclinic wave structure preserved
- Correct phase speed and amplitude

## 4. Performance Profiling

### 4.1 Memory Access Patterns
```cpp
void profile_memory_access() {
    // Use Intel VTune or perf to measure:
    // - Cache hit rates
    // - Memory bandwidth utilization
    // - NUMA effects for multi-socket systems
}
```

### 4.2 JVP Computation Overhead
```cpp
void benchmark_jvp_methods() {
    Timer timer;
    
    // Benchmark AD-JVP
    timer.start();
    for (int i = 0; i < 1000; i++) {
        auto jvp_ad = compute_jvp_ad(state, vector);
    }
    auto time_ad = timer.elapsed();
    
    // Benchmark FD-JVP
    timer.start();
    for (int i = 0; i < 1000; i++) {
        auto jvp_fd = compute_jvp_fd(state, vector);
    }
    auto time_fd = timer.elapsed();
    
    printf("AD-JVP: %.3f ms, FD-JVP: %.3f ms, Speedup: %.2fx\n",
           time_ad, time_fd, time_fd/time_ad);
}
```

### 4.3 Preconditioner Efficiency
```cpp
TEST(Preconditioner, IterationReduction) {
    // Without preconditioner
    auto iterations_no_pc = gmres_solve(A, b, nullptr);
    
    // With acoustic preconditioner
    auto iterations_with_pc = gmres_solve(A, b, preconditioner);
    
    // Should see 3-5x reduction in iterations
    EXPECT_LT(iterations_with_pc, iterations_no_pc / 3);
}
```

## 5. Debugging Tools

### 5.1 Residual Monitor
```cpp
class ResidualMonitor {
    void log_residual(int newton_iter, int gmres_iter, 
                      const torch::Tensor& residual) {
        // Log to rsl.error.*
        fprintf(fp, "SDIRK3: Newton %d, GMRES %d, ||R|| = %e\n",
                newton_iter, gmres_iter, residual.norm().item<float>());
        
        // Check for NaN/Inf
        if (torch::any(torch::isnan(residual)).item<bool>()) {
            dump_state_for_debugging();
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
};
```

### 5.2 JVP Consistency Check
```cpp
void check_jvp_consistency() {
    // Verify JVP satisfies: <JVP(x,v), w> = <v, JVP^T(x,w)>
    auto jvp_forward = compute_jvp(x, v);
    auto jvp_adjoint = compute_jvp_transpose(x, w);
    
    auto inner1 = torch::dot(jvp_forward, w);
    auto inner2 = torch::dot(v, jvp_adjoint);
    
    float relative_error = std::abs(inner1 - inner2) / std::abs(inner1);
    ASSERT_LT(relative_error, 1e-6);
}
```

## 6. Continuous Integration

### 6.1 GitHub Actions Workflow
```yaml
name: WRF SDIRK3 Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    
    - name: Build WRF with SDIRK3
      run: |
        ./configure 37  # GNU with libtorch
        ./compile -j 4 em_b_wave
        
    - name: Run unit tests
      run: |
        cd external/sdirk3_lib/test
        ./run_unit_tests.sh
        
    - name: Run em_b_wave test
      run: |
        cd test/em_b_wave
        mpirun -np 4 ./wrf.exe
        python validate_output.py wrfout_d01_*
        
    - name: Check conservation
      run: |
        python check_conservation.py rsl.error.0000
```

## 7. Long-term Stability Test

### 7.1 10-day Integration
```bash
# Run 10-day baroclinic wave simulation
cd test/em_b_wave
./ideal.exe
mpirun -np 16 ./wrf.exe

# Monitor for:
# - Numerical instability
# - Conservation violations
# - Excessive Newton iterations
# - Memory leaks
```

### 7.2 Success Criteria
- Completes 10-day integration without crashes
- Newton convergence maintained throughout
- Conservation errors remain bounded
- No significant drift from RK3 solution
- Memory usage remains constant

## 8. Documentation Requirements

### 8.1 Code Documentation
- Every JVP rule must have mathematical derivation
- Memory layout transformations clearly documented
- Performance implications of design choices explained

### 8.2 User Guide
- How to enable SDIRK3 in namelist
- Recommended settings for different applications
- Troubleshooting convergence issues
- Performance tuning guidelines