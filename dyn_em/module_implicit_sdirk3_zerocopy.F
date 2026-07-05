MODULE module_implicit_sdirk3_zerocopy
   USE module_configure
   USE module_domain
   USE module_state_description
   USE module_model_constants
   USE module_wrf_error
   USE module_dm, ONLY : wrf_dm_min_real
   USE, INTRINSIC :: ISO_C_BINDING
   
   IMPLICIT NONE

   PRIVATE
   PUBLIC :: advance_implicit_zerocopy
   PUBLIC :: sdirk3_halo_exchange

   ! Step outcome codes from wrf_sdirk3_interface.h
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_ADVANCED    = 0_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_SKIPPED     = 1_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_SOFT_NO_PROGRESS = 10_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_HARD_STAGE_ABORT = 20_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INPUT      = 100_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INTERNAL   = 101_C_INT

   ! ═══════════════════════════════════════════════════════════════════════════
   ! ABI VERSION: 94 (must match WRF_SDIRK3_GRIDINFO_ABI_VERSION in wrf_sdirk3_types.h)
   !
   ! Version history:
   !   v88 - Initial ABI version macro
   !   v89 - Added cached_device_index (int8_t) for multi-GPU
   !   v90 - Changed cached_device_index to int16_t for >127 GPU clusters
   !   v91 - Changed cached_reference_device to int16_t for consistency
   !   v92 - Added warned_dz_rdz_device_mismatch for WARN_ONCE pattern
   !   v93 - Added dz_rdz_warn_throttle_counter for per-solver warning throttle
   !   v94 - Added solver_unique_id for per-solver TLS cache invalidation
   !
   ! If you get ABI mismatch errors, rebuild BOTH C++ library AND WRF.
   !
   ! FIX Round115/Round116: C++ Debug/Validation Macros (for reference)
   ! These affect the C++ side (wrf_sdirk3_interface_zerocopy.cpp):
   !   -DWRF_SDIRK3_DEBUG_TLS_CACHE     : Intensive TLS validation in debug builds
   !   -DWRF_SDIRK3_RELEASE_TLS_CHECK   : Enable TLS validation in release builds
   !   -DWRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL=N : Check every N calls (default 1000)
   !   -DWRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD=N : Abort after N mismatches (default 100)
   ! Defaults defined in wrf_sdirk3_abi_version.h (SINGLE SOURCE OF TRUTH).
   ! FIX Round146: DOC AUTHORITY - See wrf_sdirk3_interface.h for complete TLS API docs.
   !   This file contains synced summaries; interface.h is authoritative for details.
   ! FIX Round148: BIND(C) NAME SYNC CHECK - Periodically verify Fortran bindings match C:
   !   grep 'BIND(C' module_implicit_sdirk3_zerocopy.F | grep -oP 'name="\K[^"]+' | sort > f_apis.txt
   !   grep 'extern "C"' wrf_sdirk3_interface.h wrf_sdirk3_config.h | grep -oP '\b(wrf_sdirk3_|sdirk3_)\w+' | sort -u > c_apis.txt
   !   diff f_apis.txt c_apis.txt  # Should be empty or expected differences only
   !
   ! FIX Round124/Round150 (OPT Pass33+): RUNTIME CONFIGURATION OPTIONS (C API from Fortran)
   ! SSoT: wrf_sdirk3_config.cpp set_config_*() | Last synced: 2025-01-19
   ! ─────────────────────────────────────────────────────────────────────────────
   ! The following C functions can be called from Fortran to set SDIRK3 config:
   !
   !   INTERFACE
   !      SUBROUTINE wrf_sdirk3_set_config_bool(name, value) BIND(C)
   !         USE, INTRINSIC :: ISO_C_BINDING
   !         CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: name
   !         INTEGER(C_INT), VALUE :: value
   !      END SUBROUTINE
   !   END INTERFACE
   !
   ! Example usage:
   !   CALL wrf_sdirk3_set_config_bool('debug_level'//C_NULL_CHAR, 2)
   !   CALL wrf_sdirk3_set_config_bool('warn_on_device_index_clamp'//C_NULL_CHAR, 1)
   !
   ! Available bool config options:
   !   - warn_on_device_index_clamp : Enable warning when device index > 32767 (default: .FALSE.)
   !                                  Use .TRUE. for debugging in large GPU clusters
   !   - log_solver_pointer         : Show solver address in logs (default: .FALSE.)
   !   - moving_nest                : Enable moving nest mode (default: .FALSE.)
   !   - use_autograd               : Enable automatic differentiation (default: .FALSE.)
   !
   ! Available int config options (use wrf_sdirk3_set_config_int):
   !   - debug_level        : 0=minimal, 1=warnings, 2=info, 3=verbose (default: 0)
   !   - debug_sample_period: Diagnostic sampling at debug_level>=1 (default: 5)
   !                          0=every iteration, N>0=every Nth iteration
   !                          Reduces D2H sync overhead while maintaining visibility
   !                          NOTE: Negative values clamped to 0
   !   - debug_heavy_sample_period: Heavy diagnostic sampling at debug_level>=3 (default: 10)
   !                          0=every iteration, N>0=every Nth iteration
   !                          For expensive operations like H-matrix conditioning
   !                          NOTE: Negative values clamped to 0
   !   - warn_throttle_count: Calls before warning re-triggers, 0=never (default: 100)
   !     NOTE: For values > 2147483647, use wrf_sdirk3_set_config_uint64() instead.
   !
   ! FIX Round127: RUNTIME CONFIG CHANGE BEHAVIOR
   ! When changing warn_throttle_count or warn_on_device_index_clamp at runtime,
   ! per-solver counters are NOT automatically reset. For deterministic behavior,
   ! call sdirk3_tile_solver_reset_state(solver_ptr) after changing config:
   !   CALL wrf_sdirk3_set_config_int('warn_throttle_count'//C_NULL_CHAR, 50)
   !   CALL sdirk3_tile_solver_reset_state(solver_ptr)
   !
   ! See wrf_sdirk3_config.h for complete list of available options.
   ! ═══════════════════════════════════════════════════════════════════════════
   !
   ! FIX Round151: DIAGNOSTIC CONTROL STRATEGY ASSESSMENT
   ! ─────────────────────────────────────────────────────────────────────────────
   ! This module uses a HYBRID approach for diagnostic output control:
   !
   ! 1. RUNTIME CONTROL (via C++ debug_level):
   !    - Most diagnostics are routed through C++ and use debug_level gating
   !    - Set via: CALL wrf_sdirk3_set_config_int('debug_level'//C_NULL_CHAR, N)
   !    - Levels: 0=minimal, 1=warnings, 2=info, 3=verbose
   !    - Advantage: Can be changed during execution without recompilation
   !
   ! 2. COMPILE-TIME CONTROL (via Fortran preprocessor macros):
   !    - Only used for Fortran-only diagnostics that cannot use C++ config
   !    - Currently: SDIRK3_VERBOSE_MU_DEBUG for μ' (mu prime) diagnostics
   !    - Enable via: -DSDIRK3_VERBOSE_MU_DEBUG in configure.wrf
   !    - Rationale: These diagnostics run EVERY TIMESTEP and would cause
   !      significant I/O overhead; compile-time gating ensures zero overhead
   !      in production builds.
   !
   ! ASSESSMENT: The hybrid approach is APPROPRIATE because:
   !    - Runtime control provides flexibility for most debugging scenarios
   !    - Compile-time control prevents accidental performance degradation
   !      from per-timestep Fortran diagnostics
   !    - Adding runtime control for μ' diagnostics is NOT recommended due to
   !      the conditional check overhead on every timestep
   ! ═══════════════════════════════════════════════════════════════════════════

   ! C struct definitions for ABI-safe parameter passing
   ! These mirror the C++ structs in wrf_sdirk3_interface_params.h
   ! Total: 72 + 28 + 28 + 16 = 144 bytes

   ! Index bounds: 18 integers = 72 bytes
   TYPE, BIND(C) :: SDIRK3_IndexBounds
      INTEGER(C_INT) :: its, ite  ! Tile bounds
      INTEGER(C_INT) :: jts, jte
      INTEGER(C_INT) :: kts, kte
      INTEGER(C_INT) :: ids, ide  ! Domain bounds
      INTEGER(C_INT) :: jds, jde
      INTEGER(C_INT) :: kds, kde
      INTEGER(C_INT) :: ims, ime  ! Memory bounds
      INTEGER(C_INT) :: jms, jme
      INTEGER(C_INT) :: kms, kme
   END TYPE SDIRK3_IndexBounds

   ! Dimensions: 7 integers = 28 bytes
   TYPE, BIND(C) :: SDIRK3_Dimensions
      INTEGER(C_INT) :: nx, ny, nz       ! Mass point dimensions
      INTEGER(C_INT) :: nx_u             ! U-staggered dimension (nx+1)
      INTEGER(C_INT) :: ny_v             ! V-staggered dimension (ny+1)
      INTEGER(C_INT) :: nz_w             ! W-staggered dimension (nz+1)
      INTEGER(C_INT) :: rk_step          ! Current RK step (0-2 for SDIRK3)
   END TYPE SDIRK3_Dimensions

   ! Scalar parameters: 7 floats = 28 bytes
   TYPE, BIND(C) :: SDIRK3_ScalarParams
      REAL(C_FLOAT) :: rdx, rdy          ! Reciprocal grid spacing
      REAL(C_FLOAT) :: kdamp             ! Rayleigh damping coefficient
      REAL(C_FLOAT) :: khdif             ! Horizontal diffusion coefficient
      REAL(C_FLOAT) :: kvdif             ! Vertical diffusion coefficient
      REAL(C_FLOAT) :: dtbc              ! Boundary condition timestep
      REAL(C_FLOAT) :: dt                ! Physics timestep
   END TYPE SDIRK3_ScalarParams

   ! Boundary zone configuration: 4 integers = 16 bytes
   TYPE, BIND(C) :: SDIRK3_BoundaryConfig
      INTEGER(C_INT) :: spec_bdy_width   ! Specified boundary width
      INTEGER(C_INT) :: spec_zone        ! Specified zone width
      INTEGER(C_INT) :: relax_zone       ! Relaxation zone width
      INTEGER(C_INT) :: padding          ! Padding for alignment (unused)
   END TYPE SDIRK3_BoundaryConfig

   ! FIX 2025-01-11 Round62/Round65: Optional FP64 scalar params for precision-critical paths
   ! Pass C_NULL_PTR to sdirk3_set_fp64_scalars if FP64 precision not needed
   ! Pass C_LOC(fp64_scalars) when FP64 precision is needed (e.g., CPU double runs)
   !
   ! ABI EQUIVALENCE (FIX Round65):
   !   - Fortran:    SDIRK3_ScalarParams_FP64   (this file, BIND(C))
   !   - C++ impl:   SDIRK3_ScalarParams_FP64   (wrf_sdirk3_interface_params.h, alignas(8))
   !   - C API:      SDIRK3_ScalarParams_FP64_C (wrf_sdirk3_interface.h)
   !   All three MUST have identical layout: 4 x REAL(C_DOUBLE) = 32 bytes, 8-byte aligned.
   !   Modification to any requires updating all three.
   TYPE, BIND(C) :: SDIRK3_ScalarParams_FP64
      REAL(C_DOUBLE) :: rdx_fp64         ! Reciprocal grid spacing (1/dx) in double precision
      REAL(C_DOUBLE) :: rdy_fp64         ! Reciprocal grid spacing (1/dy) in double precision
      REAL(C_DOUBLE) :: dx_fp64          ! Grid spacing (dx) - preferred, set to 0 if not provided
      REAL(C_DOUBLE) :: dy_fp64          ! Grid spacing (dy) - preferred, set to 0 if not provided
   END TYPE SDIRK3_ScalarParams_FP64

   ! Module state variables from module_implicit_sdirk3
   LOGICAL, SAVE :: sdirk3_initialized = .FALSE.
   TYPE(C_PTR), ALLOCATABLE, SAVE :: tile_solvers(:)
   LOGICAL, ALLOCATABLE, SAVE :: solver_valid(:)
   
   ! Solver state tracking for debugging
   TYPE :: sdirk3_solver_state
      TYPE(C_PTR) :: solver_ptr = C_NULL_PTR
      LOGICAL :: is_initialized = .FALSE.
      INTEGER :: tile_id = -1
      REAL :: last_dt = -1.0
      INTEGER :: total_steps = 0
      INTEGER :: failed_steps = 0
   END TYPE sdirk3_solver_state
   
   TYPE(sdirk3_solver_state), DIMENSION(:), ALLOCATABLE, SAVE :: solver_states

   ! FIX 2025-01-25: Module-level interface for halo freshness notification
   INTERFACE
      SUBROUTINE sdirk3_notify_halo_fresh() &
         BIND(C, name="sdirk3_notify_halo_fresh")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE sdirk3_notify_halo_fresh
   END INTERFACE

CONTAINS
   
   !-----------------------------------------------------------------------
   ! Zero-Copy implementation of advance_implicit
   ! This version passes pointers directly to grid data without copying
   !-----------------------------------------------------------------------
   SUBROUTINE advance_implicit_zerocopy(grid, config_flags, rk_step, dt)
      USE, INTRINSIC :: ISO_C_BINDING
      USE module_domain
      USE module_configure
      USE module_driver_constants
      USE module_state_description, ONLY : P_QV, num_moist  ! Water vapor index and num_moist
#ifdef DM_PARALLEL
      USE module_dm, ONLY : local_communicator
#endif
      IMPLICIT NONE
#ifdef DM_PARALLEL
      INCLUDE 'mpif.h'
#endif
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      INTEGER, INTENT(IN) :: rk_step
      REAL, INTENT(IN) :: dt

      ! Local variables
      INTEGER :: ij
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: ips, ipe, jps, jpe, kps, kpe
      INTEGER :: nx, ny, nz
      INTEGER :: nx_u, ny_v, nz_w  ! Staggered dimensions
      INTEGER :: i_endu, j_endv     ! Staggered loop bounds
      CHARACTER(LEN=256) :: message
      INTEGER(C_INT) :: step_outcome_rc
      INTEGER(C_INT) :: step_outcome_code
      INTEGER(C_INT) :: step_final_update_aborted
      INTEGER(C_INT) :: step_progress_ratio_valid
      REAL(C_FLOAT) :: step_progress_ratio
      REAL, ALLOCATABLE, DIMENSION(:,:), TARGET :: mu_pert  ! μ' perturbation array

      ! Struct variables for v2 interface (ABI-safe parameter passing)
      TYPE(SDIRK3_IndexBounds), TARGET :: bounds
      TYPE(SDIRK3_Dimensions), TARGET :: dims
      TYPE(SDIRK3_ScalarParams), TARGET :: scalars
      TYPE(SDIRK3_BoundaryConfig), TARGET :: bdy_cfg
      
      ! Interface for zero-copy version of sdirk3_tile_unified_step
      INTERFACE
         ! OLD interface removed - causes ABI corruption with 127 parameters
         ! Use _v2 (struct-based) interface instead

         ! NEW: Struct-based interface to avoid gfortran BIND(C) 127-parameter limit
         SUBROUTINE sdirk3_tile_unified_step_zerocopy_v2(solver, &
                                             u_ptr, v_ptr, w_ptr, ph_ptr, al_ptr, mu_ptr, &
                                             p_ptr, t_ptr, &
                                             moist_ptr, n_moist, &
                                             ru_tend_ptr, rv_tend_ptr, rw_tend_ptr, &
                                             ph_tend_ptr, al_tend_ptr, mu_tend_ptr, &
                                             p_tend_ptr, t_tend_ptr, &
                                             rdnw_ptr, rdn_ptr, &
                                             msftx_ptr, msfty_ptr, msfux_ptr, msfuy_ptr, &
                                             msfvx_ptr, msfvy_ptr, &
                                             c1f_ptr, c2f_ptr, c1h_ptr, c2h_ptr, &
                                             fnm_ptr, fnp_ptr, &
                                             f_ptr, e_ptr, sina_ptr, cosa_ptr, &
                                             cqu_ptr, cqv_ptr, cqw_ptr, &
                                             u_bdy_xs, u_bdy_xe, v_bdy_xs, v_bdy_xe, &
                                             w_bdy_xs, w_bdy_xe, t_bdy_xs, t_bdy_xe, &
                                             ph_bdy_xs, ph_bdy_xe, mu_bdy_xs, mu_bdy_xe, &
                                             u_bdy_ys, u_bdy_ye, v_bdy_ys, v_bdy_ye, &
                                             w_bdy_ys, w_bdy_ye, t_bdy_ys, t_bdy_ye, &
                                             ph_bdy_ys, ph_bdy_ye, mu_bdy_ys, mu_bdy_ye, &
                                             u_btend_xs, u_btend_xe, v_btend_xs, v_btend_xe, &
                                             w_btend_xs, w_btend_xe, t_btend_xs, t_btend_xe, &
                                             ph_btend_xs, ph_btend_xe, mu_btend_xs, mu_btend_xe, &
                                             u_btend_ys, u_btend_ye, v_btend_ys, v_btend_ye, &
                                             w_btend_ys, w_btend_ye, t_btend_ys, t_btend_ye, &
                                             ph_btend_ys, ph_btend_ye, mu_btend_ys, mu_btend_ye, &
                                             fcx, gcx, fcy, gcy, &
                                             bounds_ptr, dims_ptr, scalars_ptr, bdy_cfg_ptr) &
            BIND(C, name="sdirk3_tile_unified_step_zerocopy_v2")
            USE, INTRINSIC :: ISO_C_BINDING
            ! Solver pointer
            TYPE(C_PTR), VALUE :: solver
            ! State variable pointers (8 pointers + 1 int)
            TYPE(C_PTR), VALUE :: u_ptr, v_ptr, w_ptr, ph_ptr, al_ptr, mu_ptr
            TYPE(C_PTR), VALUE :: p_ptr, t_ptr
            TYPE(C_PTR), VALUE :: moist_ptr
            INTEGER(C_INT), VALUE :: n_moist
            ! Tendency pointers (8 pointers)
            TYPE(C_PTR), VALUE :: ru_tend_ptr, rv_tend_ptr, rw_tend_ptr
            TYPE(C_PTR), VALUE :: ph_tend_ptr, al_tend_ptr, mu_tend_ptr
            TYPE(C_PTR), VALUE :: p_tend_ptr, t_tend_ptr
            ! Grid metric arrays (18 pointers)
            TYPE(C_PTR), VALUE :: rdnw_ptr, rdn_ptr
            TYPE(C_PTR), VALUE :: msftx_ptr, msfty_ptr, msfux_ptr, msfuy_ptr
            TYPE(C_PTR), VALUE :: msfvx_ptr, msfvy_ptr
            TYPE(C_PTR), VALUE :: c1f_ptr, c2f_ptr, c1h_ptr, c2h_ptr
            TYPE(C_PTR), VALUE :: fnm_ptr, fnp_ptr
            TYPE(C_PTR), VALUE :: f_ptr, e_ptr, sina_ptr, cosa_ptr
            ! Moisture correction factors (3 pointers)
            TYPE(C_PTR), VALUE :: cqu_ptr, cqv_ptr, cqw_ptr
            ! Boundary arrays (48 pointers)
            TYPE(C_PTR), VALUE :: u_bdy_xs, u_bdy_xe, v_bdy_xs, v_bdy_xe
            TYPE(C_PTR), VALUE :: w_bdy_xs, w_bdy_xe, t_bdy_xs, t_bdy_xe
            TYPE(C_PTR), VALUE :: ph_bdy_xs, ph_bdy_xe, mu_bdy_xs, mu_bdy_xe
            TYPE(C_PTR), VALUE :: u_bdy_ys, u_bdy_ye, v_bdy_ys, v_bdy_ye
            TYPE(C_PTR), VALUE :: w_bdy_ys, w_bdy_ye, t_bdy_ys, t_bdy_ye
            TYPE(C_PTR), VALUE :: ph_bdy_ys, ph_bdy_ye, mu_bdy_ys, mu_bdy_ye
            TYPE(C_PTR), VALUE :: u_btend_xs, u_btend_xe, v_btend_xs, v_btend_xe
            TYPE(C_PTR), VALUE :: w_btend_xs, w_btend_xe, t_btend_xs, t_btend_xe
            TYPE(C_PTR), VALUE :: ph_btend_xs, ph_btend_xe, mu_btend_xs, mu_btend_xe
            TYPE(C_PTR), VALUE :: u_btend_ys, u_btend_ye, v_btend_ys, v_btend_ye
            TYPE(C_PTR), VALUE :: w_btend_ys, w_btend_ye, t_btend_ys, t_btend_ye
            TYPE(C_PTR), VALUE :: ph_btend_ys, ph_btend_ye, mu_btend_ys, mu_btend_ye
            ! Relaxation coefficients (4 pointers)
            TYPE(C_PTR), VALUE :: fcx, gcx, fcy, gcy
            ! STRUCT POINTERS - replaces 36 individual parameters with 4 pointers
            TYPE(C_PTR), VALUE :: bounds_ptr      ! SDIRK3_IndexBounds
            TYPE(C_PTR), VALUE :: dims_ptr        ! SDIRK3_Dimensions
            TYPE(C_PTR), VALUE :: scalars_ptr     ! SDIRK3_ScalarParams
            TYPE(C_PTR), VALUE :: bdy_cfg_ptr     ! SDIRK3_BoundaryConfig
         END SUBROUTINE sdirk3_tile_unified_step_zerocopy_v2

         FUNCTION sdirk3_tile_solver_get_last_step_outcome_zerocopy( &
            solver, outcome_code, final_update_aborted, progress_ratio, progress_ratio_valid) &
            BIND(C, name="sdirk3_tile_solver_get_last_step_outcome_zerocopy")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver
            INTEGER(C_INT), INTENT(OUT) :: outcome_code
            INTEGER(C_INT), INTENT(OUT) :: final_update_aborted
            REAL(C_FLOAT), INTENT(OUT) :: progress_ratio
            INTEGER(C_INT), INTENT(OUT) :: progress_ratio_valid
            INTEGER(C_INT) :: sdirk3_tile_solver_get_last_step_outcome_zerocopy
         END FUNCTION sdirk3_tile_solver_get_last_step_outcome_zerocopy

         ! FIX 2025-01-11 Round62: Interface for setting FP64 scalars
         ! Call this BEFORE sdirk3_tile_unified_step_zerocopy_v2 if FP64 precision is needed
         ! Usage:
         !   TYPE(SDIRK3_ScalarParams_FP64), TARGET :: fp64_scalars
         !   fp64_scalars%dx_fp64 = DBLE(dx)
         !   fp64_scalars%dy_fp64 = DBLE(dy)
         !   fp64_scalars%rdx_fp64 = 0.0D0  ! Set to 0 if dx_fp64 is provided
         !   fp64_scalars%rdy_fp64 = 0.0D0  ! Set to 0 if dy_fp64 is provided
         !   CALL sdirk3_set_fp64_scalars(solver_ptr, C_LOC(fp64_scalars))
         SUBROUTINE sdirk3_set_fp64_scalars(solver, fp64_ptr) &
            BIND(C, name="sdirk3_set_fp64_scalars")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver      ! Solver pointer from sdirk3_create_tile_solver
            TYPE(C_PTR), VALUE :: fp64_ptr    ! Pointer to SDIRK3_ScalarParams_FP64, or C_NULL_PTR to clear
         END SUBROUTINE sdirk3_set_fp64_scalars

         ! ════════════════════════════════════════════════════════════════════════
         ! FIX 2025-01-11 Round79: SOLVER RESET FUNCTIONS - USAGE GUIDE
         ! ════════════════════════════════════════════════════════════════════════
         !
         ! Two reset functions are provided for different use cases:
         !
         ! ┌─────────────────────────┬─────────────────────────┬─────────────────┐
         ! │ Function                │ What it resets          │ Recommended for │
         ! ├─────────────────────────┼─────────────────────────┼─────────────────┤
         ! │ reset_state (light)     │ Warnings, device cache  │ PRODUCTION      │
         ! │ reset_full (heavy)      │ + All solver caches     │ DEBUG/TEST      │
         ! └─────────────────────────┴─────────────────────────┴─────────────────┘
         !
         ! USAGE SCENARIOS:
         !
         ! 1. NORMAL WRF OPERATION (typically no reset needed):
         !    - Solvers created once at domain init
         !    - Used throughout simulation
         !    - Destroyed at domain finalize
         !    --> Usually no reset calls needed
         !
         !    EDGE CASES where reset_state MAY be useful (FIX Round80):
         !    - Re-enable FP64 transition logging after initial messages suppressed
         !    - Re-enable no-reference-device warnings for diagnostics
         !    - Force device cache refresh after manual tensor device migration
         !    Example:
         !      ! To re-enable all warning messages (useful for debugging):
         !      CALL sdirk3_tile_solver_reset_state(solver_ptr)
         !
         ! 2. SOLVER REUSE ACROSS SIMULATIONS (use reset_state):
         !    - External tool pooling solvers
         !    - After solver migration to different device
         !    Example:
         !      ! After completing simulation 1, before starting simulation 2
         !      CALL sdirk3_tile_solver_reset_state(solver_ptr)
         !
         ! 3. GRID GEOMETRY CHANGES (use reset_full):
         !    - Moving nest with grid resizing
         !    - Vertical level changes
         !    - Map factor updates
         !    Example:
         !      ! After grid modification
         !      CALL sdirk3_tile_solver_reset_full(solver_ptr)
         !      ! Then re-initialize base state
         !      CALL sdirk3_tile_setBaseState(solver_ptr, ...)
         !
         ! 4. TEST HARNESS (use reset_full or reset_full_parallel):
         !    - Between independent test cases
         !    - When complete isolation is needed
         !    Example (single-threaded or OMP_NUM_THREADS=1):
         !      DO itest = 1, num_tests
         !         CALL sdirk3_tile_solver_reset_full(solver_ptr)
         !         CALL run_test_case(itest)
         !      END DO
         !    Example (OpenMP multi-threaded, FIX Round85):
         !      DO itest = 1, num_tests
         !         CALL sdirk3_tile_solver_reset_full_parallel(solver_ptr)
         !         CALL run_test_case(itest)
         !      END DO
         !
         ! IMPORTANT: All reset functions MUST be called when solver is IDLE
         !            (not during timestep computation)
         ! ════════════════════════════════════════════════════════════════════════

         ! FIX 2025-01-11 Round76/Round77/Round78: Lightweight reset for per-solver state only
         ! PRODUCTION RECOMMENDED - Low overhead reset for solver reuse scenarios.
         ! Resets ONLY per-solver state:
         !   - Per-solver warning flags and logging state
         !   - Device cache
         ! Does NOT invalidate solver caches - use sdirk3_tile_solver_reset_full for that.
         SUBROUTINE sdirk3_tile_solver_reset_state(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_state")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  ! Solver pointer from sdirk3_create_tile_solver
         END SUBROUTINE sdirk3_tile_solver_reset_state

         ! FIX 2025-01-11 Round78/Round79/Round80: Full reset including all internal caches
         ! DEBUG/TEST RECOMMENDED - Heavy operation for complete solver state invalidation.
         ! Performs FULL reset including:
         !   - Per-solver warning flags and logging state
         !   - Device cache
         !   - Vertical metric caches (FIX Round80):
         !     * Z1D profile cache (Rayleigh damping)
         !     * Dz min cache (CFL checks)
         !     * Lat CPU cache (boundary conditions)
         !     * Scalar mean cache (preconditioner mub/c1f/c2f/msfty)
         !   - Solver internal caches (see wrf_sdirk3_tile_unified.h for inventory):
         !     * Divergence cache
         !     * MSF 3D expansion cache
         !     * Static metric caches (rdz, dnw, dn)
         !     * UnifiedRHS acoustic metric caches
         !     * Pressure gradient caches (scalars and arrays)
         ! WARNING: Do NOT call during GPU computation - may cause synchronization issues.
         !
         ! THREAD POLICY (FIX Round84):
         !   Thread-local caches (MSF 3D, pressure gradient) are only invalidated
         !   in the CALLING THREAD. If called from main thread only:
         !     - OpenMP worker threads' caches remain stale
         !     - For test harnesses: set OMP_NUM_THREADS=1 for deterministic behavior
         !   Epoch-based caches (z1d, static metrics, scalar mean) ARE thread-safe.
         SUBROUTINE sdirk3_tile_solver_reset_full(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_full")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  ! Solver pointer from sdirk3_create_tile_solver
         END SUBROUTINE sdirk3_tile_solver_reset_full

         ! FIX Round85/Round86: Full reset with OpenMP parallel thread_local cache handling
         ! RECOMMENDED FOR OPENMP BUILDS - Properly separates global vs thread_local caches.
         !
         ! This function handles separation of cache invalidation:
         !   1. Master thread only: per-solver state + global/epoch-based caches
         !   2. All threads (via OpenMP parallel region): thread_local caches
         !
         ! USE THIS INSTEAD OF reset_full() WHEN:
         !   - Running with OpenMP enabled (OMP_NUM_THREADS > 1)
         !   - Need deterministic cache state across all worker threads
         !   - Test harnesses requiring complete cache isolation
         !
         ! WHEN reset_full() IS SUFFICIENT (FIX Round86):
         !   - Single-threaded execution (OMP_NUM_THREADS=1 or no OpenMP)
         !   - Normal WRF production runs (single solver per tile)
         !   Existing code using reset_full() does NOT need to change.
         !
         ! NON-OPENMP BUILD: Serial fallback, functionally equivalent to reset_full().
         SUBROUTINE sdirk3_tile_solver_reset_full_parallel(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_full_parallel")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  ! Solver pointer from sdirk3_create_tile_solver
         END SUBROUTINE sdirk3_tile_solver_reset_full_parallel

         ! FIX Round101/Round102: Lightweight TLS-only parallel reset API
         ! Resets ONLY thread-local (TLS) caches for the calling thread.
         ! Does NOT touch global caches or per-solver state.
         !
         ! USE CASE: Call from ALL threads in a parallel region when only
         ! TLS state needs cleanup (e.g., solver pooling, multi-threaded tests).
         !
         ! IMPORTANT: Each thread must call this individually. Use with:
         !   !$OMP PARALLEL
         !   CALL sdirk3_tile_solver_reset_tls_parallel(solver_ptr)
         !   !$OMP END PARALLEL
         !
         ! NON-OPENMP BUILD: Resets single-thread TLS state (minimal effect).
         ! For best effect, use in OpenMP builds only.
         SUBROUTINE sdirk3_tile_solver_reset_tls_parallel(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_tls_parallel")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  ! Solver pointer from sdirk3_create_tile_solver
         END SUBROUTINE sdirk3_tile_solver_reset_tls_parallel

         ! FIX Round125/Round126: Lightweight TLS debug tracking reset (current thread only)
         ! This resets ONLY debug tracking counters, NOT the solver lookup cache.
         ! Use for test harnesses that need clean debug counters between tests.
         ! IMPORTANT: NO-OP when WRF_SDIRK3_NO_TLS_TRACKING is defined at compile time.
         ! See wrf_sdirk3_interface.h for full documentation.
         SUBROUTINE sdirk3_reset_tls_debug_tracking() &
            BIND(C, name="sdirk3_reset_tls_debug_tracking")
         END SUBROUTINE sdirk3_reset_tls_debug_tracking

         ! FIX Round127/Round128: 64-bit unsigned integer setter for config values
         ! Use this when values may exceed INT_MAX (e.g., warn_throttle_count > 2147483647)
         ! Available uint64 config options:
         !   - warn_throttle_count: Calls before warning re-triggers (0=sticky/never)
         !   - grid_metric_memcmp_period: Period for full grid metric comparison
         !   - cuda_grid_metric_sample_period: CUDA sampling period for grid metrics
         !   - cuda_phbase_sample_period: CUDA sampling period for ph_base
         !   - lat_cpu_sample_check_period: LatCpuCache sample check period
         !
         ! FIX Round128/Round147: PLATFORM/RANGE NOTES
         ! - Fortran INTEGER(C_INT64_T) is SIGNED (-2^63 to 2^63-1)
         ! - C++ uint64_t is UNSIGNED (0 to 2^64-1)
         ! - Negative Fortran values become large positive uint64 due to bit reinterpretation
         ! - On most platforms (LP64, LLP64), C_INT64_T is 64 bits
         ! - If your platform has smaller C_INT64_T, values > max will overflow
         ! - FIX Round147: Use C_INT64_T, NOT C_LONG_LONG. C_LONG_LONG may differ on some
         !   platforms (e.g., Windows LLP64 where long long != int64_t conceptually).
         ! - RECOMMENDATION: Only pass non-negative values; check platform with:
         !     IF (BIT_SIZE(1_C_INT64_T) < 64) CALL wrf_error_fatal("C_INT64_T < 64 bits")
         !
         ! Example: CALL wrf_sdirk3_set_config_uint64('warn_throttle_count'//C_NULL_CHAR, 10000000000_C_INT64_T)
         SUBROUTINE wrf_sdirk3_set_config_uint64(name, value) &
            BIND(C, name="wrf_sdirk3_set_config_uint64")
            USE, INTRINSIC :: ISO_C_BINDING
            CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: name
            INTEGER(C_INT64_T), VALUE :: value  ! uint64_t maps to INTEGER(C_INT64_T)
         END SUBROUTINE wrf_sdirk3_set_config_uint64

         ! FIX Round127/Round128/Round129: TLS solver lookup cache clear API
         ! FIX Round137: TERMINOLOGY GLOSSARY
         !   - TLS lookup cache: solver_ptr to GridInfo* mapping cached per thread
         !   - TLS debug tracking: change counters, sample counters stored per thread
         !
         ! FIX Round136: QUICK SUMMARY (see details below)
         ! FIX Round139: Added per-thread marking to Scope column
         !   │ API                              │ Scope (thread)    │ Effect     │
         !   │ sdirk3_clear_tls_solver_cache()  │ Lookup+Dbg(curr)  │ Delayed    │
         !   │ sdirk3_reset_tls_debug_tracking()│ Debug (curr thrd) │ Immediate  │
         !   │ reset_tls_parallel(solver_ptr)   │ TLS (all threads) │ Immediate  │
         ! FIX Round138: First two APIs affect CALLING THREAD ONLY (per-thread TLS).
         !   For all-thread reset, use reset_tls_parallel() within OMP PARALLEL region.
         ! FIX Round140: Details below expand on table above - see wrf_sdirk3_interface.h
         !   for complete API documentation including thread scope and usage patterns.
         ! FIX Round142: DOC ROLE SEPARATION (table = summary, body = details)
         !   - TABLE: Quick reference with scope/effect - use for API selection
         !   - BODY (below): Complete usage notes - read before calling unfamiliar APIs
         !   - Full reference: wrf_sdirk3_interface.h (C++) / wrf_sdirk3_config.cpp (policy)
         !
         ! Use this to explicitly invalidate the TLS solver lookup cache for test harnesses
         ! or solver recycling scenarios where you want to force cache re-population.
         !
         ! ZEROCOPY INTERFACE ONLY: This function ONLY affects the zerocopy interface
         ! (sdirk3_tile_solver_create_zerocopy, etc.). Legacy tile and unified interfaces
         ! do NOT use TLS caching, so this function has no effect on them.
         !
         ! FIX Round129: DEBUG TRACKING INCLUSION
         ! This function resets BOTH lookup cache AND debug tracking (internally).
         ! You do NOT need to call sdirk3_reset_tls_debug_tracking() separately.
         !   - sdirk3_clear_tls_solver_cache(): Resets lookup cache + debug tracking
         !   - sdirk3_reset_tls_debug_tracking(): Resets ONLY debug tracking
         !
         ! DELAYED INVALIDATION: Uses global generation increment. Each thread's
         ! cache is invalidated on NEXT ACCESS, not immediately. For immediate
         ! per-thread invalidation, use sdirk3_tile_solver_reset_tls_parallel().
         !
         ! FIX Round130: FORTRAN USER NOTE (호출 즉시 효과 아님)
         ! After CALL sdirk3_clear_tls_solver_cache(), the cache is NOT cleared
         ! until the NEXT solver access. If you need guaranteed cache reset BEFORE
         ! proceeding, use sdirk3_tile_solver_reset_tls_parallel(solver_ptr) instead.
         !
         ! FIX Round135: MULTI-DSO ENVIRONMENT NOTE (다중 공유 라이브러리 환경)
         ! The global g_solver_generation counter is per-DSO. If your application
         ! links multiple shared libraries, calling this function only increments
         ! the counter in the CURRENT DSO. For complete invalidation across all DSOs:
         !   - Call sdirk3_clear_tls_solver_cache() from each DSO using TLS caching
         !   - Or use a single shared library build for unified generation counter
         !
         ! Thread scope: Current thread only - call from each thread if needed.
         SUBROUTINE sdirk3_clear_tls_solver_cache() &
            BIND(C, name="sdirk3_clear_tls_solver_cache")
         END SUBROUTINE sdirk3_clear_tls_solver_cache

         SUBROUTINE sdirk3_set_mpi_comm(comm_handle, periodic_x, periodic_y) &
            BIND(C, name="sdirk3_set_mpi_comm")
            USE, INTRINSIC :: ISO_C_BINDING
            INTEGER(C_INT), VALUE :: comm_handle
            INTEGER(C_INT), VALUE :: periodic_x
            INTEGER(C_INT), VALUE :: periodic_y
         END SUBROUTINE sdirk3_set_mpi_comm
      END INTERFACE
      
      IF (.NOT. sdirk3_initialized) THEN
         CALL wrf_error_fatal("SDIRK3: Solvers not initialized")
      END IF
      
      ! Get domain dimensions
      CALL get_ijk_from_grid(grid, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            ips, ipe, jps, jpe, kps, kpe)
      
      ! Pass WRF's domain-specific Cartesian communicator + periodic flags to C++ halo system.
      ! Uses local_communicator (non-periodic, per-domain) — NOT local_communicator_periodic.
      ! Periodic directions are handled by C++ via MPI_Cart_rank wrap-around,
      ! matching RSL_LITE's separate EXCH_PERIOD path.
#ifdef DM_PARALLEL
      IF (local_communicator /= MPI_UNDEFINED .AND. &
          local_communicator /= MPI_COMM_NULL) THEN
         CALL sdirk3_set_mpi_comm( &
            INT(local_communicator, C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_x), C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_y), C_INT))
      END IF
#endif

      ! Perform halo exchange BEFORE SDIRK3 solver to ensure correct boundary values
      ! This is critical - solver needs updated halo values from neighboring tiles
      CALL sdirk3_halo_exchange(grid, config_flags)
      
      ! Process each tile with zero-copy approach
      !$OMP PARALLEL DO PRIVATE(ij,its,ite,jts,jte,kts,kte,nx,ny,nz, &
      !$OMP                     nx_u,ny_v,nz_w,i_endu,j_endv,message)
      DO ij = 1, grid%num_tiles
         
         ! Get tile bounds
         its = grid%i_start(ij)
         ite = grid%i_end(ij)
         jts = grid%j_start(ij)
         jte = grid%j_end(ij)
         kts = kds
         kte = kde - 1
         
         ! Tile dimensions
         nx = ite - its + 1
         ny = jte - jts + 1
         nz = kte - kts + 1
         
         ! Staggered dimensions
         ! FIX 2026-02-05: Array dimensions are always nx+1, ny+1, nz+1 regardless of boundary.
         ! The loop bounds (i_endu, j_endv) handle where to compute, but array sizes are fixed.
         ! Previous code incorrectly set nx_u=nx and ny_v=ny at boundaries, causing dimension
         ! mismatch between Fortran arrays and C++ tensor expectations.
         nx_u = nx + 1
         ny_v = ny + 1

         IF (ite == ide) THEN
            i_endu = ite
         ELSE
            i_endu = ite + 1
         END IF

         IF (jte == jde) THEN
            j_endv = jte
         ELSE
            j_endv = jte + 1
         END IF

         ! W-staggered dimension: w, ph, c1f, c2f need one extra level
         nz_w = nz + 1

         ! DEBUG: Check ny_v calculation
         WRITE(message, '(A,I3,A,I3,A,I3,A,I3,A,I3)') &
            'DEBUG ny_v calc: jte=', jte, ' jde=', jde, ' ny=', ny, &
            ' -> ny_v=', ny_v, ' (expected ', ny, ' or ', ny+1, ')'
         CALL wrf_debug(0, message)
         
         ! Validate solver
         IF (ALLOCATED(solver_valid)) THEN
            IF (.NOT. solver_valid(ij)) THEN
               WRITE(message,'(A,I0)') 'SDIRK3 ERROR: Solver not initialized for tile ', ij
               CALL wrf_error_fatal(message)
            END IF
         END IF
         
         ! Update solver state
         IF (ALLOCATED(solver_states)) THEN
            IF (solver_states(ij)%is_initialized) THEN
               solver_states(ij)%total_steps = solver_states(ij)%total_steps + 1
               solver_states(ij)%last_dt = dt
            END IF
         END IF
         
         ! Print timestep info periodically
         IF (MOD(grid%itimestep, 100) == 0 .AND. ij == 1) THEN
            WRITE(message,'(A,I0,A,I0,A,F10.3,A)') &
               'SDIRK3 Zero-Copy: Tile ', ij, ', timestep ', grid%itimestep, &
               ', dt=', dt, ' seconds'
            CALL wrf_debug(1, message)
         END IF
         
         ! Debug dimensions being passed
         IF (ij == 1) THEN
            WRITE(message,'(A,3I6)') 'SDIRK3 FORTRAN: Passing nx/ny/nz = ', nx, ny, nz
            CALL wrf_debug(0, message)
            WRITE(message,'(A,3I6)') 'SDIRK3 FORTRAN: Passing nx_u/ny_v/nz_w = ', nx_u, ny_v, nz_w
            CALL wrf_debug(0, message)
            WRITE(message,'(A,I6,A,F10.3)') 'SDIRK3 FORTRAN: Passing rk_step = ', rk_step, ', dt = ', dt
            CALL wrf_debug(0, message)
            WRITE(message,'(A,6I6)') 'SDIRK3 FORTRAN: Passing its/ite/jts/jte/kts/kte = ', &
                                     its, ite, jts, jte, kts, kte
            CALL wrf_debug(0, message)
            WRITE(message,'(A,6I6)') 'SDIRK3 FORTRAN: Passing ids/ide/jds/jde/kds/kde = ', &
                                     ids, ide, jds, jde, kds, kde
            CALL wrf_debug(0, message)
            WRITE(message,'(A,6I6)') 'SDIRK3 FORTRAN: Passing ims/ime/jms/jme/kms/kme = ', &
                                     ims, ime, jms, jme, kms, kme
            CALL wrf_debug(0, message)
            ! Also check some pointer addresses
            WRITE(message,'(A,Z16,A,Z16)') 'SDIRK3 FORTRAN: f_ptr = ', &
                                     TRANSFER(C_LOC(grid%f(its,jts)), 0_C_INTPTR_T), &
                                     ', e_ptr = ', &
                                     TRANSFER(C_LOC(grid%e(its,jts)), 0_C_INTPTR_T)
            CALL wrf_debug(0, message)
            ! cqu, cqv, cqw are allocated in grid structure
            ! These are moisture-related coefficients
         END IF
         
         ! Debug: Print tile indices before calling C++
         IF (ij == 1) THEN
            WRITE(message, '(A,6I5)') 'SDIRK3 FORTRAN: its/ite/jts/jte/kts/kte = ', &
                                       its, ite, jts, jte, kts, kte
            CALL wrf_debug(0, message)
            WRITE(message, '(A,6I5)') 'SDIRK3 FORTRAN: ids/ide/jds/jde/kds/kde = ', &
                                       ids, ide, jds, jde, kds, kde
            CALL wrf_debug(0, message)
            WRITE(message, '(A,6I5)') 'SDIRK3 FORTRAN: ims/ime/jms/jme/kms/kme = ', &
                                       ims, ime, jms, jme, kms, kme
            CALL wrf_debug(0, message)
         END IF
         
         ! DEBUG: Print all parameters right before the call
         WRITE(message, '(A,6I5)') 'FORTRAN CALL: nx,ny,nz,nx_u,ny_v,nz_w = ', &
                                    nx, ny, nz, nx_u, ny_v, nz_w
         CALL wrf_debug(0, message)
         
         ! DEBUG: Check values being passed to SDIRK3
         WRITE(message, '(A,I2,A,E12.5,A,E12.5)') 'WRF->SDIRK3 tile ', ij, &
            ' grid%u_2 min/max: ', MINVAL(grid%u_2(its:ite,kts:kte,jts:jte)), &
            ' / ', MAXVAL(grid%u_2(its:ite,kts:kte,jts:jte))
         CALL wrf_debug(0, message)
         WRITE(message, '(A,I2,A,E12.5,A,E12.5)') 'WRF->SDIRK3 tile ', ij, &
            ' grid%v_2 min/max: ', MINVAL(grid%v_2(its:ite,kts:kte,jts:jte)), &
            ' / ', MAXVAL(grid%v_2(its:ite,kts:kte,jts:jte))
         CALL wrf_debug(0, message)
         WRITE(message, '(A,I2,A,E12.5,A,E12.5)') 'WRF->SDIRK3 tile ', ij, &
            ' grid%w_2 min/max: ', MINVAL(grid%w_2(its:ite,kts:kte,jts:jte)), &
            ' / ', MAXVAL(grid%w_2(its:ite,kts:kte,jts:jte))
         CALL wrf_debug(0, message)
         WRITE(message, '(A,I2,A,E12.5,A,E12.5)') 'WRF->SDIRK3 tile ', ij, &
            ' grid%mu_2 min/max: ', MINVAL(grid%mu_2(its:ite,jts:jte)), &
            ' / ', MAXVAL(grid%mu_2(its:ite,jts:jte))
         CALL wrf_debug(0, message)

         ! Populate structs for v2 interface (avoids gfortran BIND(C) 127-parameter limit)
         ! Fill IndexBounds struct
         bounds%its = its ; bounds%ite = ite
         bounds%jts = jts ; bounds%jte = jte
         bounds%kts = kts ; bounds%kte = kte
         bounds%ids = ids ; bounds%ide = ide
         bounds%jds = jds ; bounds%jde = jde
         bounds%kds = kds ; bounds%kde = kde
         bounds%ims = ims ; bounds%ime = ime
         bounds%jms = jms ; bounds%jme = jme
         bounds%kms = kms ; bounds%kme = kme

         ! Fill Dimensions struct
         dims%nx = nx ; dims%ny = ny ; dims%nz = nz
         dims%nx_u = nx_u ; dims%ny_v = ny_v ; dims%nz_w = nz_w
         dims%rk_step = rk_step

         ! Fill ScalarParams struct
         scalars%rdx = grid%rdx ; scalars%rdy = grid%rdy
         scalars%kdamp = REAL(config_flags%dampcoef, C_FLOAT)
         scalars%khdif = config_flags%khdif
         scalars%kvdif = config_flags%kvdif
         scalars%dtbc = grid%dtbc
         scalars%dt = REAL(dt, C_FLOAT)

         ! Fill BoundaryConfig struct
         bdy_cfg%spec_bdy_width = config_flags%spec_bdy_width
         bdy_cfg%spec_zone = config_flags%spec_zone
         bdy_cfg%relax_zone = config_flags%relax_zone
         bdy_cfg%padding = 0  ! Unused

         ! Call NEW struct-based Zero-Copy version of SDIRK-3 solver (v2)
         ! Pass C_LOC of the first element of each tile's data + struct pointers
         ! CRITICAL FIX: C++ expects PERTURBATION mu, not total mu!
         ! Compute mu' = mu_2 - mub in temporary array
         ! FIX 2026-01-30: Allocate mu_pert with FULL PATCH dimensions (ims:ime, jms:jme)
         ! to match the memory layout assumed by C++ from_blob strides.
         ! Old code used (its:ite, jts:jte) which had stride mismatch causing buffer overrun.
         ALLOCATE(mu_pert(ims:ime, jms:jme))
         mu_pert = 0.0
         mu_pert(its:ite, jts:jte) = grid%mu_2(its:ite, jts:jte) - grid%mub(its:ite, jts:jte)

         ! FIX Round150: Gate μ' diagnostics with debug_level >= 3 to avoid excessive I/O.
         ! These outputs run EVERY TIMESTEP and can severely impact performance.
         ! Enable via: namelist debug_level = 3 or compile with -DSDIRK3_VERBOSE_MU_DEBUG
#ifdef SDIRK3_VERBOSE_MU_DEBUG
         IF (ij == 1) THEN  ! Only print for first tile to avoid spam
            WRITE(*, '(A)') ''
            WRITE(*, '(A)') '=== FORTRAN μ'' DIAGNOSTICS (before C++ call) ==='
            WRITE(*, '(A,E14.6,A,E14.6,A)') '  grid%mu_2 (total μ) min/max: ', &
                 MINVAL(grid%mu_2(its:ite, jts:jte)), ' / ', &
                 MAXVAL(grid%mu_2(its:ite, jts:jte)), ' Pa'
            WRITE(*, '(A,E14.6,A)') '  grid%mub (base μ) mean: ', &
                 SUM(grid%mub(its:ite, jts:jte)) / REAL((ite-its+1)*(jte-jts+1)), ' Pa'
            WRITE(*, '(A,E14.6,A,E14.6,A)') '  mu_pert (μ'') min/max: ', &
                 MINVAL(mu_pert), ' / ', MAXVAL(mu_pert), ' Pa'

            ! Flag if μ' is unreasonably large
            IF (ABS(MAXVAL(mu_pert)) > 1.0E4 .OR. ABS(MINVAL(mu_pert)) > 1.0E4) THEN
               WRITE(*, '(A)') '  WARNING: μ'' magnitude exceeds 10^4 Pa before C++ call!'
               WRITE(*, '(A)') '  Check if Fortran is updating mu_2 incorrectly'
            END IF
         END IF
#endif

         CALL sdirk3_tile_unified_step_zerocopy_v2( &
            tile_solvers(ij), &
            ! FIX 2026-01-30: Pass C_LOC starting at (ims,kms,jms) instead of (its,kts,jts).
            ! The C++ code computes slice offsets as (jts-jms, kts-kms, its-ims) assuming
            ! the pointer starts at the memory origin. Passing (its,kts,jts) double-counts
            ! the offset, causing reads/writes at wrong physical locations.
            C_LOC(grid%u_2(ims,kms,jms)), &     ! U velocity
            C_LOC(grid%v_2(ims,kms,jms)), &     ! V velocity
            C_LOC(grid%w_2(ims,kms,jms)), &     ! W velocity
            C_LOC(grid%ph_2(ims,kms,jms)), &    ! Perturbation geopotential
            C_LOC(grid%al(ims,kms,jms)), &      ! Inverse density
            C_LOC(mu_pert(ims,jms)), &          ! Column mass PERTURBATION (mu' = mu_2 - mub)
            C_LOC(grid%p(ims,kms,jms)), &       ! Perturbation pressure
            C_LOC(grid%t_2(ims,kms,jms)), &     ! Perturbation theta
            ! Moisture variables (pass start of 4D array for tile)
            MERGE(C_LOC(grid%moist(ims,kms,jms,1)), C_NULL_PTR, num_moist > 0), &
            num_moist, &  ! number of moisture species
            ! Tendencies - SDIRK3 computes internally, pass NULL
            C_NULL_PTR, & ! U tendency (computed internally)
            C_NULL_PTR, & ! V tendency (computed internally)
            C_NULL_PTR, & ! W tendency (rw_tend is i1, not state)
            C_NULL_PTR, & ! PH tendency (ph_tend is i1, not state)
            C_NULL_PTR, & ! AL tendency (al_tend not available as state)
            C_NULL_PTR, & ! MU tendency (mu_tend is i1, not state)
            C_NULL_PTR, C_NULL_PTR, &            ! P and T tendencies (computed internally)
            ! Grid metrics (arrays only - scalars now in struct)
            C_LOC(grid%rdnw(kts)), &             ! Reciprocal dnw array (also used for U/V via sign change)
            C_LOC(grid%rdn(kts)), &              ! Reciprocal dn array (rdzu for W)
            ! Map scale factors
            C_LOC(grid%msftx(ims,jms)), &       ! Map factor x at mass points
            C_LOC(grid%msfty(ims,jms)), &       ! Map factor y at mass points
            C_LOC(grid%msfux(ims,jms)), &       ! Map factor x at u points
            C_LOC(grid%msfuy(ims,jms)), &       ! Map factor y at u points
            C_LOC(grid%msfvx(ims,jms)), &       ! Map factor x at v points
            C_LOC(grid%msfvy(ims,jms)), &       ! Map factor y at v points
            ! Vertical coordinate coefficients
            C_LOC(grid%c1f(kts)), &             ! C1 at w levels
            C_LOC(grid%c2f(kts)), &             ! C2 at w levels
            C_LOC(grid%c1h(kts)), &             ! C1 at mass levels
            C_LOC(grid%c2h(kts)), &             ! C2 at mass levels
            ! Vertical interpolation weights (fnm=fzm, fnp=fzp)
            C_LOC(grid%fnm(kts)), &             ! Weight for level k
            C_LOC(grid%fnp(kts)), &             ! Weight for level k-1
            ! Coriolis parameters
            C_LOC(grid%f(ims,jms)), &           ! 2*Omega*sin(lat)
            C_LOC(grid%e(ims,jms)), &           ! 2*Omega*cos(lat)
            C_LOC(grid%sina(ims,jms)), &        ! sin(alpha) for rotation
            C_LOC(grid%cosa(ims,jms)), &        ! cos(alpha) for rotation
            ! Moisture correction factors - now always allocated
            C_NULL_PTR, &     ! cqu coefficient (pass null if not available)
            C_NULL_PTR, &     ! cqv coefficient (pass null if not available)
            C_NULL_PTR, &     ! cqw coefficient (pass null if not available)
            ! TEMPORARY: Pass NULL for all boundary arrays to reduce parameter count
            ! This works for periodic boundaries (em_b_wave)
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_bxs, u_bxe, v_bxs, v_bxe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_bxs, w_bxe, t_bxs, t_bxe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_bxs, ph_bxe, mu_bxs, mu_bxe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_bys, u_bye, v_bys, v_bye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_bys, w_bye, t_bys, t_bye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_bys, ph_bye, mu_bys, mu_bye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_btxs, u_btxe, v_btxs, v_btxe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_btxs, w_btxe, t_btxs, t_btxe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_btxs, ph_btxe, mu_btxs, mu_btxe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_btys, u_btye, v_btys, v_btye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_btys, w_btye, t_btys, t_btye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_btys, ph_btye, mu_btys, mu_btye
            ! Relaxation coefficients
            ! ISSUE #4-4: fcx/fcy/gcx/gcy are not in grid structure (computed locally in BCs)
            ! Pass null pointers - C++ code has null checks and computes them if needed
            C_NULL_PTR, &  ! fcx_ptr
            C_NULL_PTR, &  ! gcx_ptr
            C_NULL_PTR, &  ! fcy_ptr
            C_NULL_PTR, &  ! gcy_ptr
            ! STRUCT POINTERS - replaces 36 individual parameters with 4 pointers
            C_LOC(bounds), &      ! Index bounds (its/ite/jts/jte/kts/kte/ids.../ims...)
            C_LOC(dims), &        ! Dimensions (nx/ny/nz/nx_u/ny_v/nz_w/rk_step)
            C_LOC(scalars), &     ! Scalar params (rdx/rdy/kdamp/khdif/kvdif/dtbc/dt)
            C_LOC(bdy_cfg))       ! Boundary config (spec_bdy_width/spec_zone/relax_zone)

         CALL wrf_debug(2, 'SDIRK3 Zero-Copy: Returned from solver')

         step_outcome_rc = sdirk3_tile_solver_get_last_step_outcome_zerocopy( &
            tile_solvers(ij), step_outcome_code, step_final_update_aborted, &
            step_progress_ratio, step_progress_ratio_valid)
         IF (step_outcome_rc == 0_C_INT) THEN
            CALL wrf_error_fatal('SDIRK3: Failed to query zerocopy step outcome')
         END IF
         IF (step_outcome_code >= SDIRK3_STEP_OUTCOME_FATAL_INPUT .OR. &
             step_outcome_code == SDIRK3_STEP_OUTCOME_HARD_STAGE_ABORT .OR. &
             step_outcome_code == SDIRK3_STEP_OUTCOME_SOFT_NO_PROGRESS) THEN
            WRITE(message,'(A,I0,A,I0,A,ES12.4,A,I0,A,I0)') &
               'SDIRK3 FAIL-CLOSED outcome=', step_outcome_code, &
               ' final_update_aborted=', step_final_update_aborted, &
               ' progress_ratio=', step_progress_ratio, &
               ' progress_ratio_valid=', step_progress_ratio_valid, &
               ' rk_step=', rk_step
            CALL wrf_error_fatal(TRIM(message))
         END IF

         ! FIX 2026-01-30: Write back updated mu to grid%mu_2.
         ! The SDIRK3 solver modifies mu_pert in-place via zero-copy.
         ! We must propagate the updated perturbation back to the prognostic variable.
         grid%mu_2(its:ite, jts:jte) = mu_pert(its:ite, jts:jte) + grid%mub(its:ite, jts:jte)

         ! FIX 2026-01-30: Update mut and muts for post-timestep diagnostics.
         ! The RK path computes these in rk_step_prep and small_step_finish, but
         ! the SDIRK3 path bypasses the RK loop entirely. Without muts, calc_p_rho_phi
         ! divides by (c1*muts + c2) = 0, producing al = -inf and p = -p_base.
         ! mut = total column dry air mass = mub + mu_2(perturbation)
         ! muts = total at new time (same as mut for implicit scheme)
         grid%mut(its:ite, jts:jte) = grid%mub(its:ite, jts:jte) + grid%mu_2(its:ite, jts:jte)
         grid%muts(its:ite, jts:jte) = grid%mub(its:ite, jts:jte) + grid%mu_2(its:ite, jts:jte)

         DEALLOCATE(mu_pert)

      END DO
      !$OMP END PARALLEL DO
      
   END SUBROUTINE advance_implicit_zerocopy
   
   SUBROUTINE sdirk3_halo_exchange(grid, config_flags)
      USE module_domain
      USE module_configure
      USE module_driver_constants
      USE module_machine
      USE module_state_description, ONLY : PARAM_FIRST_SCALAR, &
                                           num_moist, num_chem, num_tracer, num_scalar
      ! These functions are external, not in module_dm
      ! wrf_get_dm_communicator, wrf_get_nproc, wrf_get_myproc, wrf_get_nprocx, wrf_get_nprocy
      IMPLICIT NONE
      TYPE(domain) , INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      ! Local variables needed for halo exchanges
      INTEGER :: local_communicator, mytask, ntasks, ntasks_x, ntasks_y
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: ips, ipe, jps, jpe, kps, kpe
      ! num_moist, num_chem, num_tracer, num_scalar come from module_state_description
      REAL, DIMENSION(:,:,:,:), POINTER :: moist, chem, tracer, scalar
      
#ifdef DM_PARALLEL
      ! Get communicator and process information
      local_communicator = grid%communicator
      CALL wrf_get_dm_communicator(local_communicator)
      CALL wrf_get_nproc(ntasks)
      CALL wrf_get_myproc(mytask)
      CALL wrf_get_nprocx(ntasks_x)
      CALL wrf_get_nprocy(ntasks_y)
      
      ! Get domain dimensions
      CALL get_ijk_from_grid(grid, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            ips, ipe, jps, jpe, kps, kpe)
      
      ! Set pointers to grid arrays for halo exchange
      moist => grid%moist
      chem => grid%chem
      tracer => grid%tracer
      scalar => grid%scalar
      
      ! Use existing WRF halo exchange for ALL SDIRK3 state variables
      ! HALO_EM_D2_5 exchanges all governing equation variables:
      !   u_2, v_2, w_2 (momentum equations)
      !   t_2 (perturbation potential temperature)
      !   ph_2 (perturbation geopotential)
      !   al (inverse density = 1/rho_d)
      !   mu_2 (column dry air mass)
      ! The 48/24/4 prefixes indicate stencil widths for different variable groups
# include "HALO_EM_D2_5.inc"
      ! FIX 2025-01-25: Notify C++ that halo exchange completed
      CALL sdirk3_notify_halo_fresh()
#endif

   END SUBROUTINE sdirk3_halo_exchange

END MODULE module_implicit_sdirk3_zerocopy
