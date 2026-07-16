# Analysis of solve_em.F Differences Between Modified and Original WRF v4.7.0

> **HISTORICAL RECORD (dated snapshot).** Solver/API descriptions in this
> document (e.g. the pre-FGMRES GMRES/BiCGSTAB era) reflect the code as of
> the date of this record, not the current contract. For the current state
> (FGMRES solver, MPI support boundary, CTest inventory) see the repository
> root `README.md` and `external/libtorch_wrf/sdirk3/README.md`.


## Overview
This analysis compares the modified solve_em.F with the original WRF v4.7.0 code, focusing on changes that could affect the RK3 (split-explicit) implementation.

## Key Differences Found

### 1. **Added SDIRK3 Module References** (Lines 81-84)
```fortran
#ifdef USE_SDIRK3
   USE module_implicit_sdirk3
   USE module_sdirk3_integrate
#endif
```
- These modules are properly protected by `#ifdef USE_SDIRK3`
- Should not affect RK3 when `USE_SDIRK3` is not defined during compilation

### 2. **SDIRK3 Initialization Code** (Lines 318-330)
```fortran
#ifdef USE_SDIRK3
   ! Initialize SDIRK-3 tile solvers if using SDIRK-3 time integration
   IF (config_flags%time_integration_scheme == 1 .AND. config_flags%rk_ord == 3) THEN
      WRITE(wrf_err_message,'(A,I0,A,I0)') 'SDIRK3 DEBUG: time_integration_scheme=', &
           config_flags%time_integration_scheme, ', rk_ord=', config_flags%rk_ord
      CALL wrf_message(wrf_err_message)
      CALL init_implicit_sdirk3(grid, config_flags)
   END IF
#endif
```
- Also protected by `#ifdef USE_SDIRK3`
- Only executes when `time_integration_scheme == 1`

### 3. **Major Structural Change: SDIRK3 Branch Before RK Loop** (Lines 597-633)
```fortran
#ifdef USE_SDIRK3
   ! Check if using SDIRK3 unified implicit time integration
   IF (config_flags%time_integration_scheme == 1) THEN
      ! ... SDIRK3 timestep code ...
      CALL wrf_message('SDIRK3 timestep complete, skipping RK loop')
   ELSE
#endif

   Runge_Kutta_loop:  DO rk_step = 1, rk_order
      ! ... RK3 code ...
   END DO Runge_Kutta_loop

#ifdef USE_SDIRK3
   END IF  ! End of if-else for SDIRK3 vs RK3
#endif
```
- This creates a branching structure where either SDIRK3 OR the RK loop runs
- The RK loop is properly enclosed and should run when `time_integration_scheme == 0`

### 4. **SDIRK3 Namelist Parameters in Test Case**
In the namelist.input file (lines 90-94), SDIRK3 parameters are defined:
```
sdirk3_max_newton_iter              = 10,
sdirk3_newton_tol                   = 1.0e-6,
sdirk3_krylov_tol                   = 1.0e-4,
sdirk3_gmres_restart                = 30,
sdirk3_precond_type                 = 1,
```
- These are present even though `time_integration_scheme = 0` (RK3)
- This shouldn't cause issues as they should be ignored when RK3 is selected

## Potential Issues for RK3 Implementation

1. **No Direct Code Path Issues**: The SDIRK3 code is properly isolated with `#ifdef USE_SDIRK3` blocks and runtime checks for `time_integration_scheme == 1`.

2. **Compilation Flag Check**: The main concern would be if `USE_SDIRK3` is being defined during compilation when it shouldn't be. This would include the SDIRK3 code paths.

3. **Runtime Behavior**: With `time_integration_scheme = 0` in the namelist, the code should:
   - Skip all SDIRK3 initialization
   - Enter the normal `Runge_Kutta_loop`
   - Execute standard RK3 time integration

## Recommendations

1. **Verify Compilation Flags**: Check if `-DUSE_SDIRK3` is being passed during compilation. If so, this needs to be removed for pure RK3 runs.

2. **Check Module Dependencies**: Ensure that `module_implicit_sdirk3` and `module_sdirk3_integrate` are not being compiled/linked when not needed.

3. **Debug Output**: The code includes debug messages that would help verify which path is taken:
   - Look for "SDIRK3 DEBUG:" messages in the output
   - Look for "Using SDIRK3 implicit time integration" message

4. **Test Without SDIRK3 Parameters**: Remove the SDIRK3 parameters from the namelist to ensure they're not causing any parsing issues.

## Conclusion

The modifications to solve_em.F appear to be properly structured to maintain backward compatibility with RK3. The SDIRK3 code is well-isolated and should not interfere with RK3 operation when `time_integration_scheme = 0`. Any issues with RK3 are likely due to:
- Compilation flags including SDIRK3 when they shouldn't
- Other modifications in dependent modules not analyzed here
- Changes in the physics or dynamics routines called within the RK loop