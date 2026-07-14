MODULE module_implicit_sdirk3
   USE module_configure
   USE module_domain
   USE module_state_description
   USE module_model_constants
   USE module_wrf_error
   USE module_dm, ONLY : wrf_dm_min_real
   USE, INTRINSIC :: ISO_C_BINDING
   
   IMPLICIT NONE
   
   LOGICAL, EXTERNAL :: wrf_dm_on_monitor
   
   PRIVATE
   PUBLIC :: init_implicit_sdirk3, advance_implicit, finalize_implicit_sdirk3
   PUBLIC :: get_sdirk3_dt_recommendation
   PUBLIC :: set_base_state_sdirk3
   PUBLIC :: sdirk3_halo_exchange
   ! PR 7B (3b-3 P1): solve_em consumes the checked notify status directly
   PUBLIC :: sdirk3_notify_halo_fresh_checked
   ! Removed unused wrapper functions: sdirk3_timestep_init, sdirk3_timestep_tile, sdirk3_timestep_finalize
   PUBLIC :: sdirk3_configure_from_namelist
   PUBLIC :: sdirk3_prepare_physics_coupling, sdirk3_apply_physics_tendencies
   PUBLIC :: sdirk3_initialize_solvers, sdirk3_finalize_solvers
   PUBLIC :: sdirk3_print_solver_statistics
   PUBLIC :: sdirk3_reset_on_restart  ! FIX 2025-01-25: Cache reset for restart path
   PUBLIC :: sdirk3_get_4dvar_trajectory_info, sdirk3_run_4dvar_adjoint_loop
   
   ! SDIRK-3 L-stable parameter
   ! Original double precision: 0.4358665215084590
   ! WRF REAL (4-byte float) precision: 0.4358665
   REAL, PARAMETER :: gamma_sdirk3 = 0.4358665

   ! Step outcome codes from wrf_sdirk3_interface.h
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_ADVANCED      = 0_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_SKIPPED       = 1_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_SOFT_NO_PROGRESS = 10_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_HARD_STAGE_ABORT = 20_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INPUT      = 100_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INTERNAL   = 101_C_INT

   ! Struct definitions for ABI-safe parameter passing (v2 interface)
   TYPE, BIND(C) :: SDIRK3_IndexBounds
      INTEGER(C_INT) :: its, ite, jts, jte, kts, kte
      INTEGER(C_INT) :: ids, ide, jds, jde, kds, kde
      INTEGER(C_INT) :: ims, ime, jms, jme, kms, kme
   END TYPE SDIRK3_IndexBounds

   TYPE, BIND(C) :: SDIRK3_Dimensions
      INTEGER(C_INT) :: nx, ny, nz
      INTEGER(C_INT) :: nx_u, ny_v, nz_w
      INTEGER(C_INT) :: rk_step
   END TYPE SDIRK3_Dimensions

   TYPE, BIND(C) :: SDIRK3_ScalarParams
      REAL(C_FLOAT) :: rdx, rdy
      REAL(C_FLOAT) :: kdamp, khdif, kvdif
      REAL(C_FLOAT) :: dtbc, dt
   END TYPE SDIRK3_ScalarParams

   TYPE, BIND(C) :: SDIRK3_BoundaryConfig
      INTEGER(C_INT) :: spec_bdy_width, spec_zone, relax_zone, padding
   END TYPE SDIRK3_BoundaryConfig

   ! C-style interfaces for zero-copy unified solver
   INTERFACE
      ! Create function from the zerocopy interface
      FUNCTION sdirk3_tile_solver_create_zerocopy(nx, ny, nz, dx, dy, rdnw_ptr, tile_id, &
                                                   nx_u, ny_v, nz_w) &
         BIND(C, name="sdirk3_tile_solver_create_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         INTEGER(C_INT), VALUE :: nx, ny, nz, tile_id
         REAL(C_FLOAT), VALUE :: dx, dy
         TYPE(C_PTR), VALUE :: rdnw_ptr
         INTEGER(C_INT), VALUE :: nx_u, ny_v, nz_w  ! Add staggered dimensions
         TYPE(C_PTR) :: sdirk3_tile_solver_create_zerocopy
      END FUNCTION sdirk3_tile_solver_create_zerocopy
      
      ! Destroy function from the zerocopy interface
      SUBROUTINE sdirk3_tile_solver_destroy_zerocopy(solver) &
         BIND(C, name="sdirk3_tile_solver_destroy_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END SUBROUTINE sdirk3_tile_solver_destroy_zerocopy

      ! Initialize MPI safety guards (halo freshness/thread-level checks).
      SUBROUTINE sdirk3_mpi_safety_init() &
         BIND(C, name="sdirk3_mpi_safety_init")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE sdirk3_mpi_safety_init

      ! FIX 2025-01-25: Full reset for restart path - invalidates all caches
      SUBROUTINE sdirk3_tile_solver_reset_full(solver) &
         BIND(C, name="sdirk3_tile_solver_reset_full")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END SUBROUTINE sdirk3_tile_solver_reset_full

      ! 4DVAR trajectory checkpoint helpers (zerocopy solver path)
      INTEGER(C_INT) FUNCTION sdirk3_tile_solver_get_saved_trajectory_count_zerocopy(solver) &
         BIND(C, name="sdirk3_tile_solver_get_saved_trajectory_count_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END FUNCTION sdirk3_tile_solver_get_saved_trajectory_count_zerocopy

      INTEGER(C_INT) FUNCTION sdirk3_tile_solver_get_saved_global_timestep_zerocopy(solver) &
         BIND(C, name="sdirk3_tile_solver_get_saved_global_timestep_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END FUNCTION sdirk3_tile_solver_get_saved_global_timestep_zerocopy

      SUBROUTINE sdirk3_tile_solver_clear_saved_trajectory_zerocopy(solver) &
         BIND(C, name="sdirk3_tile_solver_clear_saved_trajectory_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END SUBROUTINE sdirk3_tile_solver_clear_saved_trajectory_zerocopy

      INTEGER(C_INT) FUNCTION sdirk3_tile_solver_get_state_vector_size_zerocopy(solver) &
         BIND(C, name="sdirk3_tile_solver_get_state_vector_size_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END FUNCTION sdirk3_tile_solver_get_state_vector_size_zerocopy

      INTEGER(C_INT) FUNCTION sdirk3_tile_solver_run_adjoint_replay_zerocopy( &
         solver, lambda_terminal_ptr, lambda_size, lambda_initial_ptr, &
         dt, gamma, gmres_restart, gmres_max_iter, gmres_tol) &
         BIND(C, name="sdirk3_tile_solver_run_adjoint_replay_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
         TYPE(C_PTR), VALUE :: lambda_terminal_ptr
         INTEGER(C_INT), VALUE :: lambda_size
         TYPE(C_PTR), VALUE :: lambda_initial_ptr
         REAL(C_FLOAT), VALUE :: dt, gamma
         INTEGER(C_INT), VALUE :: gmres_restart, gmres_max_iter
         REAL(C_FLOAT), VALUE :: gmres_tol
      END FUNCTION sdirk3_tile_solver_run_adjoint_replay_zerocopy

      ! OLD interface removed - Use sdirk3_tile_unified_step_zerocopy_v2 instead
      ! The 127-parameter version causes gfortran BIND(C) ABI corruption

      ! NEW: Struct-based v2 interface (reduces parameter count from 127 to ~90)
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
         IMPORT :: SDIRK3_IndexBounds, SDIRK3_Dimensions, SDIRK3_ScalarParams, SDIRK3_BoundaryConfig
         TYPE(C_PTR), VALUE :: solver
         TYPE(C_PTR), VALUE :: u_ptr, v_ptr, w_ptr, ph_ptr, al_ptr, mu_ptr
         TYPE(C_PTR), VALUE :: p_ptr, t_ptr
         TYPE(C_PTR), VALUE :: moist_ptr
         INTEGER(C_INT), VALUE :: n_moist
         TYPE(C_PTR), VALUE :: ru_tend_ptr, rv_tend_ptr, rw_tend_ptr
         TYPE(C_PTR), VALUE :: ph_tend_ptr, al_tend_ptr, mu_tend_ptr
         TYPE(C_PTR), VALUE :: p_tend_ptr, t_tend_ptr
         TYPE(C_PTR), VALUE :: rdnw_ptr, rdn_ptr
         TYPE(C_PTR), VALUE :: msftx_ptr, msfty_ptr, msfux_ptr, msfuy_ptr
         TYPE(C_PTR), VALUE :: msfvx_ptr, msfvy_ptr
         TYPE(C_PTR), VALUE :: c1f_ptr, c2f_ptr, c1h_ptr, c2h_ptr
         TYPE(C_PTR), VALUE :: fnm_ptr, fnp_ptr
         TYPE(C_PTR), VALUE :: f_ptr, e_ptr, sina_ptr, cosa_ptr
         TYPE(C_PTR), VALUE :: cqu_ptr, cqv_ptr, cqw_ptr
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
         TYPE(C_PTR), VALUE :: fcx, gcx, fcy, gcy
         TYPE(C_PTR), VALUE :: bounds_ptr, dims_ptr, scalars_ptr, bdy_cfg_ptr
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
      
      ! Configuration interfaces
      SUBROUTINE wrf_sdirk3_set_config_int(name, value) &
         BIND(C, name="wrf_sdirk3_set_config_int")
         USE, INTRINSIC :: ISO_C_BINDING
         CHARACTER(C_CHAR), DIMENSION(*) :: name
         INTEGER(C_INT), VALUE :: value
      END SUBROUTINE wrf_sdirk3_set_config_int
      
      SUBROUTINE wrf_sdirk3_set_config_float(name, value) &
         BIND(C, name="wrf_sdirk3_set_config_float")
         USE, INTRINSIC :: ISO_C_BINDING
         CHARACTER(C_CHAR), DIMENSION(*) :: name
         REAL(C_FLOAT), VALUE :: value
      END SUBROUTINE wrf_sdirk3_set_config_float
      
      SUBROUTINE wrf_sdirk3_set_config_bool(name, value) &
         BIND(C, name="wrf_sdirk3_set_config_bool")
         USE, INTRINSIC :: ISO_C_BINDING
         CHARACTER(C_CHAR), DIMENSION(*) :: name
         INTEGER(C_INT), VALUE :: value
      END SUBROUTINE wrf_sdirk3_set_config_bool

      ! FIX 2025-01-25: Config freeze mechanism - call after all config is set
      SUBROUTINE wrf_sdirk3_mark_workers_started() &
         BIND(C, name="wrf_sdirk3_mark_workers_started")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE wrf_sdirk3_mark_workers_started

      ! v20.14r27n: Query freeze state for idempotent init guard
      INTEGER(C_INT) FUNCTION wrf_sdirk3_is_workers_started() &
         BIND(C, name="wrf_sdirk3_is_workers_started")
         USE, INTRINSIC :: ISO_C_BINDING
      END FUNCTION wrf_sdirk3_is_workers_started

      ! v20.14r27n: Reset freeze flag for reinit/restart
      SUBROUTINE wrf_sdirk3_reset_workers_started() &
         BIND(C, name="wrf_sdirk3_reset_workers_started")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE wrf_sdirk3_reset_workers_started

      ! Debug function to test basic Fortran-C++ interface
      SUBROUTINE sdirk3_debug_step(solver, tile_id, itimestep) &
         BIND(C, name="sdirk3_debug_step")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
         INTEGER(C_INT), VALUE :: tile_id, itimestep
      END SUBROUTINE sdirk3_debug_step
      
      ! Boundary conditions setter (v2 — with specified/nested)
      SUBROUTINE sdirk3_tile_set_boundary_conditions_v2(solver_ptr, &
                    periodic_x, periodic_y, &
                    symmetric_xs, symmetric_xe, &
                    symmetric_ys, symmetric_ye, &
                    open_xs, open_xe, &
                    open_ys, open_ye, &
                    specified, nested) &
         BIND(C, name="sdirk3_tile_set_boundary_conditions_v2")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_ptr
         INTEGER(C_INT), VALUE :: periodic_x, periodic_y
         INTEGER(C_INT), VALUE :: symmetric_xs, symmetric_xe
         INTEGER(C_INT), VALUE :: symmetric_ys, symmetric_ye
         INTEGER(C_INT), VALUE :: open_xs, open_xe
         INTEGER(C_INT), VALUE :: open_ys, open_ye
         INTEGER(C_INT), VALUE :: specified, nested
      END SUBROUTINE sdirk3_tile_set_boundary_conditions_v2
      
      ! Map projection setter for idealized case detection
      SUBROUTINE sdirk3_tile_set_map_projection(solver_ptr, map_proj) &
         BIND(C, name="sdirk3_tile_set_map_projection")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_ptr
         INTEGER(C_INT), VALUE :: map_proj
      END SUBROUTINE sdirk3_tile_set_map_projection

      ! Solver-local MPI process grid setter
      SUBROUTINE sdirk3_tile_set_mpi_process_info(solver_ptr, nprocx, nprocy, mypx, mypy) &
         BIND(C, name="sdirk3_tile_set_mpi_process_info")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_ptr
         INTEGER(C_INT), VALUE :: nprocx, nprocy, mypx, mypy
      END SUBROUTINE sdirk3_tile_set_mpi_process_info
      
      ! Base state setter (extended version with advanced dynamics parameters)
      SUBROUTINE sdirk3_tile_set_base_state(solver_ptr, &
                    pb, alb, phb, mub, &
                    nx, ny, nz, &
                    sin_alpha_x, sin_alpha_y, &
                    cos_alpha_x, cos_alpha_y, &
                    div_damp_opt, div_damp_coef, &
                    diff_6th_opt, diff_6th_factor, &
                    diff_6th_slopeopt, diff_6th_thresh, &
                    smagorinsky_opt, c_s, c_k) &
         BIND(C, name="sdirk3_tile_set_base_state")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_ptr
         TYPE(C_PTR), VALUE :: pb, alb, phb, mub
         INTEGER(C_INT), VALUE :: nx, ny, nz
         TYPE(C_PTR), VALUE :: sin_alpha_x, sin_alpha_y
         TYPE(C_PTR), VALUE :: cos_alpha_x, cos_alpha_y
         INTEGER(C_INT), VALUE :: div_damp_opt, diff_6th_opt
         INTEGER(C_INT), VALUE :: diff_6th_slopeopt, smagorinsky_opt
         REAL(C_FLOAT), VALUE :: div_damp_coef, diff_6th_factor
         REAL(C_FLOAT), VALUE :: diff_6th_thresh, c_s, c_k
      END SUBROUTINE sdirk3_tile_set_base_state
      
      ! Zero-copy base state setter (extended version with advanced dynamics parameters)
      SUBROUTINE sdirk3_tile_set_base_state_zerocopy(solver_ptr, &
                    pb_ptr, alb_ptr, phb_ptr, mub_ptr, &
                    its, ite, jts, jte, kts, kte, &
                    ims, ime, jms, jme, kms, kme, &
                    sin_alpha_x, sin_alpha_y, &
                    cos_alpha_x, cos_alpha_y, &
                    div_damp_opt, div_damp_coef, &
                    diff_6th_opt, diff_6th_factor, &
                    diff_6th_slopeopt, diff_6th_thresh, &
                    smagorinsky_opt, c_s, c_k) &
         BIND(C, name="sdirk3_tile_set_base_state_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_ptr
         TYPE(C_PTR), VALUE :: pb_ptr, alb_ptr, phb_ptr, mub_ptr
         INTEGER(C_INT), VALUE :: its, ite, jts, jte, kts, kte
         INTEGER(C_INT), VALUE :: ims, ime, jms, jme, kms, kme
         TYPE(C_PTR), VALUE :: sin_alpha_x, sin_alpha_y
         TYPE(C_PTR), VALUE :: cos_alpha_x, cos_alpha_y
         INTEGER(C_INT), VALUE :: div_damp_opt, diff_6th_opt
         INTEGER(C_INT), VALUE :: diff_6th_slopeopt, smagorinsky_opt
         REAL(C_FLOAT), VALUE :: div_damp_coef, diff_6th_factor
         REAL(C_FLOAT), VALUE :: diff_6th_thresh, c_s, c_k
      END SUBROUTINE sdirk3_tile_set_base_state_zerocopy

      ! PR 7B (3b-3 P1): checked halo freshness publication. Returns 1 on
      ! success (serial/no-exchange notify is a true no-op), 0 when the
      ! publication is rejected (off-baseline thread). Fortran MUST consume
      ! the status and stop on 0 — the legacy void form is no longer
      ! referenced by active code (it aborts on failure at the C ABI).
      INTEGER(C_INT) FUNCTION sdirk3_notify_halo_fresh_checked() &
            BIND(C, name="sdirk3_notify_halo_fresh_checked")
         USE, INTRINSIC :: ISO_C_BINDING
      END FUNCTION sdirk3_notify_halo_fresh_checked

      ! PR 7B (3b-1): checked communicator setup. Returns 1 on success, 0 on
      ! ANY validation failure (null/non-Cartesian/periodic/1-D communicator,
      ! MPI not initialized). Fortran MUST consume the status and stop on 0 —
      ! the legacy void form is no longer referenced by active code.
      INTEGER(C_INT) FUNCTION sdirk3_set_mpi_comm_checked( &
            comm_handle, periodic_x, periodic_y) &
            BIND(C, name="sdirk3_set_mpi_comm_checked")
         USE, INTRINSIC :: ISO_C_BINDING
         INTEGER(C_INT), VALUE :: comm_handle
         INTEGER(C_INT), VALUE :: periodic_x
         INTEGER(C_INT), VALUE :: periodic_y
      END FUNCTION sdirk3_set_mpi_comm_checked

      ! PR 7B (3b-2): rank-level checked halo preparation — THE sole geometry
      ! authority (C ABI in wrf_sdirk3_halo_c_api.h). 1=success (identical
      ! re-prepare is a complete no-op), 0=failure with a stable marker.
      INTEGER(C_INT) FUNCTION sdirk3_halo_prepare_checked( &
            ids, ide, jds, jde, kds, kde, &
            ims, ime, jms, jme, kms, kme, &
            ips, ipe, jps, jpe, kps, kpe, &
            nprocx, nprocy, mypx, mypy, halo_width) &
            BIND(C, name="sdirk3_halo_prepare_checked")
         USE, INTRINSIC :: ISO_C_BINDING
         INTEGER(C_INT), VALUE :: ids, ide, jds, jde, kds, kde
         INTEGER(C_INT), VALUE :: ims, ime, jms, jme, kms, kme
         INTEGER(C_INT), VALUE :: ips, ipe, jps, jpe, kps, kpe
         INTEGER(C_INT), VALUE :: nprocx, nprocy, mypx, mypy
         INTEGER(C_INT), VALUE :: halo_width
      END FUNCTION sdirk3_halo_prepare_checked

      ! PR 7B: explicit rank-halo lifecycle teardown (owned communicator
      ! freed exactly once; fingerprint invalidated; Policy A configuration
      ! lifetime ends). Failures stop with a coordinated abort (void ABI).
      SUBROUTINE sdirk3_halo_finalize() &
         BIND(C, name="sdirk3_halo_finalize")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE sdirk3_halo_finalize
   END INTERFACE
   
   ! Module state with improved solver management
   LOGICAL :: sdirk3_initialized = .FALSE.
   LOGICAL :: base_state_initialized = .FALSE.
   TYPE(C_PTR), ALLOCATABLE, SAVE :: tile_solvers(:)
   INTEGER, SAVE :: num_tiles_initialized = 0
   INTEGER, SAVE :: max_tiles = 0
   
   ! Add explicit solver validation
   LOGICAL, ALLOCATABLE, SAVE :: solver_valid(:)
   
   ! Tile information structure for better organization
   TYPE :: tile_info_type
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: nx, ny, nz
      INTEGER :: tile_id
   END TYPE tile_info_type
   
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
   
CONTAINS
   
   !-----------------------------------------------------------------------
   SUBROUTINE init_implicit_sdirk3(grid, config_flags)
      IMPLICIT NONE
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      ! Local variables
      INTEGER :: ij
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: nx, ny, nz, tile_id
      INTEGER :: nx_u, ny_v, nz_w  ! Staggered dimensions
      REAL(C_FLOAT) :: dx, dy
      CHARACTER(LEN=256) :: message
      
      CALL wrf_message("SDIRK3: init_implicit_sdirk3 called")
      
      IF (sdirk3_initialized) THEN
         CALL wrf_debug(2, "UNIFIED SDIRK3: Already initialized, skipping")
         RETURN
      END IF
      
      ! Get domain dimensions directly from grid structure
      ! FIX 2025-01-26: WRF dimension mapping is d31=i, d32=k, d33=j
      ! Previously had d32/d33 swapped for j/k
      ids = grid%sd31
      ide = grid%ed31
      jds = grid%sd33  ! FIX: j is d33, not d32
      jde = grid%ed33  ! FIX: j is d33, not d32
      kds = grid%sd32  ! FIX: k is d32, not d33
      kde = SIZE(grid%pb, 2) ! Use actual pb array dimension

      ! Debug: Print actual kde value
      WRITE(message, '(A,I0,A,I0)') "SDIRK3 init_implicit: kde from pb array = ", kde, ", kde-1 = ", kde-1
      CALL wrf_message(message)
      ims = grid%sm31
      ime = grid%em31
      jms = grid%sm33  ! FIX: j is d33, not d32
      jme = grid%em33  ! FIX: j is d33, not d32
      kms = grid%sm32  ! FIX: k is d32, not d33
      kme = grid%em32  ! FIX: k is d32, not d33
      
      ! Allocate solver array and validation array
      max_tiles = grid%num_tiles
      ALLOCATE(tile_solvers(max_tiles))
      ALLOCATE(solver_valid(max_tiles))
      ALLOCATE(solver_states(max_tiles))
      solver_valid(:) = .FALSE.
      
      WRITE(message, '(A,I0,A)') "UNIFIED SDIRK3: Allocating space for ", max_tiles, " tile solvers"
      CALL wrf_message(message)

      ! Configure SDIRK3 from namelist settings BEFORE initializing tiles
      ! This ensures tiles pick up correct config values during construction
      CALL sdirk3_configure_from_namelist(config_flags)

      ! Activate MPI safety framework once during SDIRK3 initialization.
      CALL sdirk3_mpi_safety_init()

      ! Initialize solvers for each tile
      !$OMP PARALLEL DO PRIVATE(ij,its,ite,jts,jte,kts,kte,nx,ny,nz,nx_u,ny_v,nz_w,tile_id,dx,dy, &
      !$OMP                     message) &
      !$OMP SHARED(ims,ime,jms,jme,kms,kme)
      DO ij = 1, grid%num_tiles
         
         ! Get tile bounds
         its = grid%i_start(ij)
         ite = grid%i_end(ij)
         jts = grid%j_start(ij)
         jte = grid%j_end(ij)
         kts = kds
         kte = kde - 1
         
         ! Debug: Print domain bounds
         IF (ij == 1) THEN
            WRITE(message, '(A,6I5)') "SDIRK3 DEBUG: kds/kde/kms/kme/kts/kte = ", kds, kde, kms, kme, kts, kte
            CALL wrf_message(message)
         END IF
         
         ! Tile dimensions
         nx = ite - its + 1
         ny = jte - jts + 1
         ! Use WRF-consistent approach: mass levels = kte - kts + 1
         nz = kte - kts + 1
         
         ! Debug output
         WRITE(message, '(A,I0,A,I0,A,I0,A,I0,A)') &
              "SDIRK3 init: WRF-consistent nz=", nz, &
              " (kde=", kde, ", kte=", kte, ", kts=", kts, ")"
         CALL wrf_message(message)
         tile_id = ij
         
         ! Grid spacing
         dx = config_flags%dx
         dy = config_flags%dy
         
         ! Calculate staggered dimensions BEFORE creating solver
         ! FIX 2026-02-05: Array dimensions are always nx+1, ny+1 regardless of boundary.
         ! The staggered arrays have fixed sizes; boundary handling is separate from dimensions.
         ! Previous code incorrectly set nx_u=nx and ny_v=ny at boundaries, causing dimension
         ! mismatch between Fortran arrays and C++ tensor expectations in solver creation.
         nx_u = nx + 1
         ny_v = ny + 1

         ! DEBUG: Print boundary/tile info
         WRITE(message,'(A,I5,A,I5,A,L1)') 'SDIRK3 INIT DEBUG: jte=', jte, &
              ', jde=', jde, ', jte==jde=', (jte==jde)
         CALL wrf_message(message)

         ! W-staggered dimension: w, ph, c1f, c2f need one extra level
         nz_w = nz + 1

         WRITE(message,'(A,3I5,A,3I5)') 'SDIRK3 DEBUG: Creating solver with nx/ny/nz=', &
              nx, ny, nz, ', nx_u/ny_v/nz_w=', nx_u, ny_v, nz_w
         CALL wrf_message(message)
         
         ! Create unified solver using zero-copy interface with staggered dimensions
         ! NOTE: Pass rdnw starting at kds (1), not kms (which can be negative)
         tile_solvers(ij) = sdirk3_tile_solver_create_zerocopy( &
            nx, ny, nz, dx, dy, C_LOC(grid%rdnw(kds)), tile_id, nx_u, ny_v, nz_w)
         
         IF (.NOT. C_ASSOCIATED(tile_solvers(ij))) THEN
            WRITE(message, '(A,I0)') "UNIFIED SDIRK3: Failed to create solver for tile ", ij
            CALL wrf_error_fatal(message)
         ELSE
            solver_valid(ij) = .TRUE.
            ! Initialize solver state tracking
            solver_states(ij)%solver_ptr = tile_solvers(ij)
            solver_states(ij)%is_initialized = .TRUE.
            solver_states(ij)%tile_id = tile_id
            solver_states(ij)%last_dt = config_flags%dt
            solver_states(ij)%total_steps = 0
            solver_states(ij)%failed_steps = 0
            
            ! CRITICAL: Set boundary conditions immediately after creating solver
            ! v20.14r22: Use v2 API to pass specified/nested flags.
            CALL sdirk3_tile_set_boundary_conditions_v2(tile_solvers(ij), &
                 MERGE(1, 0, config_flags%periodic_x), &
                 MERGE(1, 0, config_flags%periodic_y), &
                 MERGE(1, 0, config_flags%symmetric_xs), &
                 MERGE(1, 0, config_flags%symmetric_xe), &
                 MERGE(1, 0, config_flags%symmetric_ys), &
                 MERGE(1, 0, config_flags%symmetric_ye), &
                 MERGE(1, 0, config_flags%open_xs), &
                 MERGE(1, 0, config_flags%open_xe), &
                 MERGE(1, 0, config_flags%open_ys), &
                 MERGE(1, 0, config_flags%open_ye), &
                 MERGE(1, 0, config_flags%specified), &
                 MERGE(1, 0, config_flags%nested))

            WRITE(message, '(A,I0,A,I0,A)') "UNIFIED SDIRK3: Created solver for tile ", ij, &
                 " (ID=", tile_id, ") successfully"
            CALL wrf_debug(1, message)
            
            ! Set map projection for idealized case detection
            CALL sdirk3_tile_set_map_projection(tile_solvers(ij), &
                 config_flags%map_proj)
            
            ! Base state will be set later after it's computed in start_em
         END IF
         
         ! Clean up
         
      END DO
      !$OMP END PARALLEL DO
      
      num_tiles_initialized = grid%num_tiles
      sdirk3_initialized = .TRUE.

      WRITE(message, '(A,I0,A)') "UNIFIED SDIRK3: Initialized ", num_tiles_initialized, " tile solvers"
      CALL wrf_message(message)
      CALL wrf_message("UNIFIED SDIRK3: L-stability eliminates CFL restriction on sound waves")

      ! NOTE: Config freeze (mark_workers_started) is now called at the END of
      ! sdirk3_initialize_solvers, AFTER all config including physics coupling.
      ! This ensures no set_config calls happen after the freeze point.

   END SUBROUTINE init_implicit_sdirk3

   !-----------------------------------------------------------------------------
   ! Set base state for SDIRK3 solvers
   ! Called from solve_em after base state is computed in start_em
   !-----------------------------------------------------------------------------
   SUBROUTINE set_base_state_sdirk3(grid, config_flags, &
                                    ids, ide, jds, jde, kds, kde, &
                                    ims, ime, jms, jme, kms, kme)
      USE module_domain, ONLY : domain
      USE module_configure, ONLY : grid_config_rec_type
      USE module_dm, ONLY: wrf_dm_sum_real
      IMPLICIT NONE
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      INTEGER, INTENT(IN) :: ids, ide, jds, jde, kds, kde
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      
      INTEGER :: ij, i, j, k
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: zero_count
      INTEGER :: nx, ny, nz
      INTEGER :: kde_consistent  ! For WRF-consistent kde
      CHARACTER(LEN=256) :: message
      
      ! Contiguous arrays for tile data
      REAL, DIMENSION(:,:,:), ALLOCATABLE, TARGET :: pb_tile, alb_tile, phb_tile
      REAL, DIMENSION(:,:), ALLOCATABLE, TARGET :: mub_tile
      
      WRITE(0,*) '=== INSIDE set_base_state_sdirk3 entry ==='
      CALL FLUSH(0)

      IF (.NOT. sdirk3_initialized) THEN
         WRITE(0,*) '=== SDIRK3 NOT initialized, returning ==='
         CALL FLUSH(0)
         CALL wrf_debug(1, "SDIRK3: Solvers not initialized, skipping base state setup")
         RETURN
      END IF

      WRITE(0,*) '=== SDIRK3 IS initialized, checking base_state ==='
      CALL FLUSH(0)

      IF (.NOT. base_state_initialized) THEN
         WRITE(0,*) '=== Setting base state for all tiles ==='
         CALL FLUSH(0)
         CALL wrf_debug(1, "SDIRK3: Setting base state for all tiles")

         WRITE(0,*) '=== About to call SIZE(grid%pb, 2) ==='
         CALL FLUSH(0)

         ! Use actual pb array dimension for consistency
         kde_consistent = SIZE(grid%pb, 2)

         WRITE(0,*) '=== kde_consistent = ', kde_consistent
         CALL FLUSH(0)
         WRITE(message, '(A,I0,A,I0,A)') "SDIRK3 set_base_state: kde from pb array=", kde_consistent, &
              " (passed kde=", kde, ")"
         CALL wrf_debug(1, message)
         
         WRITE(0,*) '=== Checking solver arrays, num_tiles = ', grid%num_tiles
         CALL FLUSH(0)
         WRITE(0,*) '=== solver_valid allocated = ', ALLOCATED(solver_valid)
         CALL FLUSH(0)
         WRITE(0,*) '=== tile_solvers allocated = ', ALLOCATED(tile_solvers)
         CALL FLUSH(0)

         IF (ALLOCATED(solver_valid)) THEN
            WRITE(0,*) '=== solver_valid bounds: ', LBOUND(solver_valid,1), ' to ', UBOUND(solver_valid,1)
            CALL FLUSH(0)
         END IF
         IF (ALLOCATED(tile_solvers)) THEN
            WRITE(0,*) '=== tile_solvers bounds: ', LBOUND(tile_solvers,1), ' to ', UBOUND(tile_solvers,1)
            CALL FLUSH(0)
         END IF

         ! Debug: Check solver status
         DO ij = 1, grid%num_tiles
            WRITE(0,*) '=== Checking tile ', ij
            CALL FLUSH(0)
            IF (ij >= LBOUND(solver_valid,1) .AND. ij <= UBOUND(solver_valid,1)) THEN
               WRITE(0,*) '=== solver_valid(', ij, ') = ', solver_valid(ij)
               CALL FLUSH(0)
            ELSE
               WRITE(0,*) '=== ERROR: ij out of bounds!'
               CALL FLUSH(0)
            END IF
            WRITE(0,*) '=== About to check C_ASSOCIATED for tile_solvers(', ij, ')'
            CALL FLUSH(0)
            WRITE(0,*) '=== C_ASSOCIATED result = ', C_ASSOCIATED(tile_solvers(ij))
            CALL FLUSH(0)
            WRITE(0,*) '=== About to write message ==='
            CALL FLUSH(0)
            WRITE(message, '(A,I0,A,L1,A,L1)') "Tile ", ij, " solver_valid=", solver_valid(ij), &
                 " C_ASSOCIATED=", C_ASSOCIATED(tile_solvers(ij))
            WRITE(0,*) '=== Message written ==='
            CALL FLUSH(0)
            CALL wrf_debug(1, message)
            WRITE(0,*) '=== wrf_debug called ==='
            CALL FLUSH(0)
         END DO
         WRITE(0,*) '=== Exited first DO loop ==='
         CALL FLUSH(0)

         WRITE(0,*) '=== Starting second DO loop, num_tiles = ', grid%num_tiles
         CALL FLUSH(0)

         ! Remove OpenMP for debugging
         DO ij = 1, grid%num_tiles
            WRITE(0,*) '=== Second loop, ij = ', ij
            CALL FLUSH(0)
            WRITE(message, '(A,I0,A)') "SDIRK3: Processing tile ", ij, " for base state"
            CALL wrf_debug(1, message)

            WRITE(0,*) '=== Checking solver_valid(', ij, ') in second loop'
            CALL FLUSH(0)

            IF (solver_valid(ij)) THEN
               WRITE(0,*) '=== solver_valid is TRUE for tile ', ij
               CALL FLUSH(0)
               ! Get tile bounds
               WRITE(0,*) '=== About to get i_start for ij = ', ij
               CALL FLUSH(0)
               its = grid%i_start(ij)
               WRITE(0,*) '=== its = ', its
               CALL FLUSH(0)
               ite = grid%i_end(ij)
               WRITE(0,*) '=== ite = ', ite
               CALL FLUSH(0)
               jts = grid%j_start(ij)
               WRITE(0,*) '=== jts = ', jts
               CALL FLUSH(0)
               jte = grid%j_end(ij)
               WRITE(0,*) '=== jte = ', jte
               CALL FLUSH(0)
               kts = kds
               WRITE(0,*) '=== kts = ', kts
               CALL FLUSH(0)
               kte = kde_consistent-1  ! Use WRF-consistent kde
               WRITE(0,*) '=== kte = ', kte
               CALL FLUSH(0)

               WRITE(0,*) '=== About to write kte message ==='
               CALL FLUSH(0)

               ! Debug: Check if kte gets modified
               WRITE(message, '(A,I0,A,I0,A,I0)') "SDIRK3 DEBUG: kde_consistent=", kde_consistent, ", kte calculated as kde_consistent-1=", kte, &
                    ", actual pb levels=", SIZE(grid%pb,2)

               WRITE(0,*) '=== Wrote kte message ==='
               CALL FLUSH(0)
               WRITE(0,*) '=== About to call wrf_debug ==='
               CALL FLUSH(0)
               CALL wrf_debug(1, message)
               WRITE(0,*) '=== wrf_debug done ==='
               CALL FLUSH(0)

               ! Check actual data dimensions
               WRITE(0,*) '=== About to check pb bounds ==='
               CALL FLUSH(0)
               WRITE(0,*) '=== pb LBOUND(1) = ', LBOUND(grid%pb,1)
               CALL FLUSH(0)
               WRITE(0,*) '=== pb UBOUND(1) = ', UBOUND(grid%pb,1)
               CALL FLUSH(0)
               WRITE(0,*) '=== pb LBOUND(2) = ', LBOUND(grid%pb,2)
               CALL FLUSH(0)
               WRITE(0,*) '=== pb UBOUND(2) = ', UBOUND(grid%pb,2)
               CALL FLUSH(0)
               WRITE(0,*) '=== pb bounds: (', LBOUND(grid%pb,1), ':', UBOUND(grid%pb,1), ',', &
                    LBOUND(grid%pb,2), ':', UBOUND(grid%pb,2), ',', &
                    LBOUND(grid%pb,3), ':', UBOUND(grid%pb,3), ')'
               CALL FLUSH(0)
               WRITE(0,*) '=== tile: its/ite/jts/jte/kts/kte = ', its, ite, jts, jte, kts, kte
               CALL FLUSH(0)
               WRITE(0,*) '=== memory: ims/ime/jms/jme/kms/kme = ', ims, ime, jms, jme, kms, kme
               CALL FLUSH(0)
               
               WRITE(0,*) '=== About to call sdirk3_tile_set_base_state_zerocopy ==='
               CALL FLUSH(0)
               ! Zero-copy base state initialization with extended dynamics parameters
               ! Pass base addresses and indices - no allocation or copying needed
               CALL sdirk3_tile_set_base_state_zerocopy(tile_solvers(ij), &
                    C_LOC(grid%pb(ims,kms,jms)), &    ! Base state pressure (memory start)
                    C_LOC(grid%t_init(ims,kms,jms)), &! Base state theta perturbation (memory start)
                    C_LOC(grid%phb(ims,kms,jms)), &   ! Base state geopotential (memory start)
                    C_LOC(grid%mub(ims,jms)), &       ! Base state column mass (memory start)
                    its, ite, jts, jte, kts, kte, &   ! Tile bounds
                    ims, ime, jms, jme, kms, kme, &   ! Memory bounds
                    C_NULL_PTR, C_NULL_PTR, &         ! sin_alpha_x, sin_alpha_y (computed later if needed)
                    C_NULL_PTR, C_NULL_PTR, &         ! cos_alpha_x, cos_alpha_y (computed later if needed)
                    config_flags%damp_opt, REAL(config_flags%dampcoef, C_FLOAT), &      ! Divergence damping
                    config_flags%diff_6th_opt, REAL(config_flags%diff_6th_factor, C_FLOAT), &  ! 6th order diffusion
                    config_flags%diff_6th_slopeopt, REAL(config_flags%diff_6th_thresh, C_FLOAT), &  ! Slope damping
                    config_flags%km_opt, REAL(config_flags%c_s, C_FLOAT), REAL(config_flags%c_k, C_FLOAT))  ! Smagorinsky
               
               ! Optional: Add debug output to verify zero-copy operation
               IF (wrf_dm_on_monitor()) THEN
                  WRITE(message, '(A,I0,A)') "SDIRK3: Base state set using zero-copy for tile ", ij, &
                       " (saved ~5.4 MB memory traffic)"
                  CALL wrf_debug(1, message)
               END IF
                    
               WRITE(message, '(A,I0)') "SDIRK3: Returned from set_base_state for tile ", ij
               CALL wrf_debug(1, message)
            ELSE
               WRITE(message, '(A,I0)') "SDIRK3: WARNING - solver not valid for tile ", ij
               CALL wrf_debug(1, message)
            END IF
         END DO
         
         base_state_initialized = .TRUE.
         CALL wrf_debug(1, "SDIRK3: Base state set for all tiles")
      END IF
      
   END SUBROUTINE set_base_state_sdirk3
   
   !-----------------------------------------------------------------------
   ! Zero-Copy implementation of advance_implicit
   ! This version passes pointers directly to grid data without copying
   !-----------------------------------------------------------------------
   SUBROUTINE advance_implicit(grid, config_flags, rk_step, dt, &
                                cqu, cqv, cqw,                   &
                                ids, ide, jds, jde, kds, kde,   &
                                ims, ime, jms, jme, kms, kme)
      USE, INTRINSIC :: ISO_C_BINDING
      USE module_domain, ONLY : domain
      USE module_configure, ONLY : grid_config_rec_type
      USE module_dm, ONLY : wrf_dm_sum_real, local_communicator, &
                            ntasks_x, ntasks_y, mytask_x, mytask_y
      IMPLICIT NONE
#ifdef DM_PARALLEL
      INCLUDE 'mpif.h'
#endif
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags  
      INTEGER, INTENT(IN) :: rk_step
      REAL, INTENT(IN) :: dt
      
      ! Moisture correction factors (passed from solve_em)
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN), TARGET :: cqu, cqv, cqw
      
      ! Domain dimensions (passed from solve_em)
      INTEGER, INTENT(IN) :: ids, ide, jds, jde, kds, kde
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      
      ! Local variables
      INTEGER :: ij
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: nx, ny, nz
      INTEGER :: nx_u, ny_v, nz_w  ! Staggered dimensions
      INTEGER :: i_endu, j_endv     ! Staggered loop bounds
      INTEGER :: i, j, nzeros       ! Loop counters and zero counter
      INTEGER :: sdirk3_nprocx, sdirk3_nprocy
      INTEGER :: sdirk3_mypx, sdirk3_mypy
      INTEGER(C_INT) :: mpi_comm_status
      INTEGER(C_INT) :: halo_prepare_status
      INTEGER(C_INT) :: measured_halo_width
      INTEGER :: r_ids, r_ide, r_jds, r_jde, r_kds, r_kde
      INTEGER :: r_ims, r_ime, r_jms, r_jme, r_kms, r_kme
      INTEGER :: r_ips, r_ipe, r_jps, r_jpe, r_kps, r_kpe
      INTEGER(C_INT64_T) :: west_halo, east_halo, south_halo, north_halo
      CHARACTER(LEN=256) :: message
      INTEGER(C_INT) :: step_outcome_rc
      INTEGER(C_INT) :: step_outcome_code
      INTEGER(C_INT) :: step_final_update_aborted
      INTEGER(C_INT) :: step_progress_ratio_valid
      REAL(C_FLOAT) :: step_progress_ratio

      ! Struct variables for v2 interface (ABI-safe parameter passing)
      TYPE(SDIRK3_IndexBounds), TARGET :: bounds
      TYPE(SDIRK3_Dimensions), TARGET :: dims
      TYPE(SDIRK3_ScalarParams), TARGET :: scalars
      TYPE(SDIRK3_BoundaryConfig), TARGET :: bdy_cfg

      ! Zero-copy interface is now declared at module level - no need to repeat here
      
      IF (.NOT. sdirk3_initialized) THEN
         CALL wrf_error_fatal("SDIRK3: Solvers not initialized")
      END IF

      ! PR 7B (3b-1): checked communicator setup, status CONSUMED. A multi-
      ! rank run without a usable local_communicator is fatal BEFORE any
      ! tile process info or solver step; a failed validation is fatal too.
      ! Runs before the OpenMP tile loop on the main thread.
      ! PR 7B (review P1-2): the opt-in AD halo path with multiple tiles is
      ! UNSUPPORTED (rank halo geometry equivalence unproven). Refuse on the
      ! MAIN THREAD before any communicator or halo mutation — a worker-side
      ! throw after other tiles already mutated solver state is a defense
      ! line, not a rejection point. The C++ guard remains defense-in-depth. Deliberately
      ! OUTSIDE #ifdef DM_PARALLEL — the unsupported combination is not
      ! MPI-specific and must be refused in serial builds too.
      IF (config_flags%sdirk3_enable_ad_halo_exchange .AND. &
          grid%num_tiles > 1) THEN
         WRITE(message, '(A,I0,A,I0,A,I0)') &
            "SDIRK3_MPI_MULTI_TILE_UNSUPPORTED: enable_ad_halo_exchange=" // &
            "true, num_tiles=", grid%num_tiles, ", rk_step=", rk_step, &
            ", itimestep=", grid%itimestep
         CALL wrf_error_fatal(TRIM(message))
      END IF

#ifdef DM_PARALLEL
      ! PR 7B (7C guard): every internal SDIRK stage executes inside ONE
      ! tile-worker C++ call, so multi-rank stage-halo correctness is NOT
      ! established. Refuse BEFORE any communicator or halo state mutation —
      ! all ranks exit on the same condition with the same marker.
      IF (ntasks_x * ntasks_y > 1) THEN
         WRITE(message, '(A,I0,A,I0,A,I0,A,I0,A,I0,A,I0)') &
            "SDIRK3_MPI_STAGE_HALO_UNSUPPORTED: decomposition ", &
            ntasks_x, " x ", ntasks_y, ", mytask (", mytask_x, ",", &
            mytask_y, "), rk_step=", rk_step, ", num_tiles=", grid%num_tiles
         CALL wrf_error_fatal(TRIM(message))
      END IF

      IF (ntasks_x * ntasks_y > 1) THEN
         IF (local_communicator == MPI_UNDEFINED .OR. &
             local_communicator == MPI_COMM_NULL) THEN
            WRITE(message, '(A,I0,A,I0,A)') &
               "SDIRK3_MPI_COMM_REQUIRED: multi-rank run (ntasks ", &
               ntasks_x, " x ", ntasks_y, ") has no local_communicator"
            CALL wrf_error_fatal(TRIM(message))
         END IF
      END IF

      IF (local_communicator /= MPI_UNDEFINED .AND. &
          local_communicator /= MPI_COMM_NULL) THEN
         mpi_comm_status = sdirk3_set_mpi_comm_checked( &
            INT(local_communicator, C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_x), C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_y), C_INT))
         IF (mpi_comm_status /= 1_C_INT) THEN
            WRITE(message, '(A,I0,A,I0,A,I0,A,I0,A,L1,A,L1)') &
               "SDIRK3 MPI communicator validation failed: ntasks ", &
               ntasks_x, " x ", ntasks_y, ", mytask (", mytask_x, ",", &
               mytask_y, "), periodic_x=", config_flags%periodic_x, &
               ", periodic_y=", config_flags%periodic_y
            CALL wrf_error_fatal(TRIM(message))
         END IF
      END IF
#endif

      ! Keep solver-local MPI topology synchronized with WRF decomposition.
      ! This must happen before the C++ step call so setWRFIndices() sees
      ! correct nprocx/nprocy/mypx/mypy instead of serial defaults.
      sdirk3_nprocx = 1
      sdirk3_nprocy = 1
      sdirk3_mypx = 0
      sdirk3_mypy = 0
#ifdef DM_PARALLEL
      IF (ntasks_x > 0 .AND. ntasks_y > 0) THEN
         sdirk3_nprocx = ntasks_x
         sdirk3_nprocy = ntasks_y
      END IF
      IF (mytask_x >= 0 .AND. mytask_y >= 0) THEN
         sdirk3_mypx = mytask_x
         sdirk3_mypy = mytask_y
      END IF
#endif

      ! PR 7B (3b-2): rank-level halo preparation on the MAIN thread, before
      ! any tile process-info or the OpenMP loop. Authoritative rank bounds
      ! come from get_ijk_from_grid (the subroutine arguments carry the same
      ! domain/memory dims — verified below, never silently overwritten); the
      ! halo width is MEASURED from the actual four memory margins, never
      ! hard-coded (C++ re-validates it against the algorithmic width).
      CALL get_ijk_from_grid(grid, &
                            r_ids, r_ide, r_jds, r_jde, r_kds, r_kde, &
                            r_ims, r_ime, r_jms, r_jme, r_kms, r_kme, &
                            r_ips, r_ipe, r_jps, r_jpe, r_kps, r_kpe)
      IF (r_ids /= ids .OR. r_ide /= ide .OR. r_jds /= jds .OR. &
          r_jde /= jde .OR. r_kds /= kds .OR. r_kde /= kde .OR. &
          r_ims /= ims .OR. r_ime /= ime .OR. r_jms /= jms .OR. &
          r_jme /= jme .OR. r_kms /= kms .OR. r_kme /= kme) THEN
         CALL wrf_error_fatal( &
            "SDIRK3_MPI_HALO_GEOMETRY_UNSUPPORTED: get_ijk_from_grid " // &
            "disagrees with the subroutine domain/memory arguments")
      END IF

      west_halo  = INT(r_ips, C_INT64_T) - INT(r_ims, C_INT64_T)
      east_halo  = INT(r_ime, C_INT64_T) - INT(r_ipe, C_INT64_T)
      south_halo = INT(r_jps, C_INT64_T) - INT(r_jms, C_INT64_T)
      north_halo = INT(r_jme, C_INT64_T) - INT(r_jpe, C_INT64_T)
      IF (west_halo <= 0_C_INT64_T .OR. east_halo /= west_halo .OR. &
          south_halo /= west_halo .OR. north_halo /= west_halo) THEN
         WRITE(message, '(A,4(I0,1X))') &
            "SDIRK3_MPI_HALO_GEOMETRY_UNSUPPORTED: west/east/south/north=", &
            west_halo, east_halo, south_halo, north_halo
         CALL wrf_error_fatal(TRIM(message))
      END IF
      IF (west_halo > INT(HUGE(0_C_INT), C_INT64_T)) THEN
         CALL wrf_error_fatal( &
            "SDIRK3_MPI_HALO_GEOMETRY_UNSUPPORTED: halo width exceeds C_INT")
      END IF
      measured_halo_width = INT(west_halo, C_INT)

      halo_prepare_status = sdirk3_halo_prepare_checked( &
         INT(r_ids, C_INT), INT(r_ide, C_INT), &
         INT(r_jds, C_INT), INT(r_jde, C_INT), &
         INT(r_kds, C_INT), INT(r_kde, C_INT), &
         INT(r_ims, C_INT), INT(r_ime, C_INT), &
         INT(r_jms, C_INT), INT(r_jme, C_INT), &
         INT(r_kms, C_INT), INT(r_kme, C_INT), &
         INT(r_ips, C_INT), INT(r_ipe, C_INT), &
         INT(r_jps, C_INT), INT(r_jpe, C_INT), &
         INT(r_kps, C_INT), INT(r_kpe, C_INT), &
         INT(sdirk3_nprocx, C_INT), INT(sdirk3_nprocy, C_INT), &
         INT(sdirk3_mypx, C_INT), INT(sdirk3_mypy, C_INT), &
         measured_halo_width)
      IF (halo_prepare_status /= 1_C_INT) THEN
         WRITE(message, '(A,4(I0,1X),A,I0)') &
            "SDIRK3_MPI_HALO_PREPARE_FAILED: margins W/E/S/N=", &
            west_halo, east_halo, south_halo, north_halo, &
            ", num_tiles=", grid%num_tiles
         CALL wrf_error_fatal(TRIM(message))
      END IF

      DO ij = 1, grid%num_tiles
         IF (C_ASSOCIATED(tile_solvers(ij))) THEN
            CALL sdirk3_tile_set_mpi_process_info(tile_solvers(ij), &
                 INT(sdirk3_nprocx, C_INT), INT(sdirk3_nprocy, C_INT), &
                 INT(sdirk3_mypx, C_INT), INT(sdirk3_mypy, C_INT))
         END IF
      END DO

      ! Rank-level halo preparation above already fetched and cross-checked
      ! the authoritative rank bounds via get_ijk_from_grid.

      ! Process each tile with Zero-Copy approach
      ! P0 FIX (2026-07-12, external review #4): bounds/dims/scalars/bdy_cfg and the
      ! step_outcome_* locals were SHARED — every tile thread overwrote the same TARGET
      ! structs whose ADDRESSES are handed to C++ (wrong bounds/strides and cross-tile
      ! outcome misreads under num_tiles > 1). All per-tile call state is now PRIVATE.
      !$OMP PARALLEL DO PRIVATE(ij,its,ite,jts,jte,kts,kte,nx,ny,nz, &
      !$OMP                     nx_u,ny_v,nz_w,i_endu,j_endv,i,j,nzeros,message, &
      !$OMP                     bounds,dims,scalars,bdy_cfg, &
      !$OMP                     step_outcome_rc,step_outcome_code, &
      !$OMP                     step_final_update_aborted,step_progress_ratio_valid, &
      !$OMP                     step_progress_ratio)
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
         
         ! Must match what was used in init_implicit_sdirk3
         ! Use WRF-consistent approach: mass levels = kte - kts + 1
         nz = kte - kts + 1
         
         ! Debug output for dimension analysis
         IF (ij == 1) THEN
            WRITE(message,'(A,L1,A,6I5)') 'SDIRK3 DEBUG: periodic_x=', &
                 config_flags%periodic_x, ', ids/ide/ims/ime/its/ite=', &
                 ids, ide, ims, ime, its, ite
            CALL wrf_message(message)
         END IF
         
         ! Staggered dimensions based on boundary conditions
         ! Set up endpoint indices for staggered grids
         ! These were already calculated before solver creation
         IF (config_flags%periodic_x) THEN
            i_endu = ite + 1
         ELSE IF (ite == ide) THEN
            i_endu = ite
         ELSE
            i_endu = ite + 1
         END IF
         
         ! DEBUG: Print jte and jde values to diagnose dimension mismatch
         IF (ij == 1) THEN
            WRITE(message,'(A,I5,A,I5,A,L1)') 'SDIRK3 SOLVE DEBUG: jte=', jte, &
                 ', jde=', jde, ', jte==jde=', (jte==jde)
            CALL wrf_message(message)
         END IF

         IF (config_flags%periodic_y) THEN
            j_endv = jte + 1
         ELSE IF (jte == jde) THEN
            j_endv = jte
         ELSE
            j_endv = jte + 1
         END IF

         ! FIX 2026-02-05: Array dimensions are always nx+1, ny+1, nz+1 regardless of loop bounds.
         ! Loop bounds (i_endu, j_endv) determine where to compute, but array dimensions are fixed.
         ! Previous code computed from loop bounds, causing dimension mismatch at boundaries.
         nx_u = nx + 1
         ny_v = ny + 1
         nz_w = nz + 1

         ! Debug staggered dimensions (already calculated)
         IF (ij == 1) THEN
            WRITE(message,'(A,3I5,A,3I5)') 'SDIRK3 solve_em: Using nx/ny/nz=', &
                 nx, ny, nz, ', nx_u/ny_v/nz_w=', nx_u, ny_v, nz_w
            CALL wrf_message(message)
         END IF
         
         ! Zero-Copy: No allocations or data copying needed!
         ! Just validate solver and update state
         
         ! Validate solver
         IF (ij > max_tiles) THEN
            WRITE(message,'(A,I0,A,I0)') 'SDIRK3 ERROR: Tile index ', ij, &
                 ' exceeds max_tiles ', max_tiles
            CALL wrf_error_fatal(message)
         END IF
         
         IF (.NOT. solver_valid(ij)) THEN
            WRITE(message,'(A,I0)') 'SDIRK3 ERROR: Solver not initialized for tile ', ij
            CALL wrf_error_fatal(message)
         END IF
         
         IF (.NOT. C_ASSOCIATED(tile_solvers(ij))) THEN
            WRITE(message,'(A,I0)') 'SDIRK3 ERROR: NULL solver pointer for tile ', ij
            CALL wrf_error_fatal(message)
         END IF
         
         ! Update solver state
         IF (solver_states(ij)%is_initialized) THEN
            solver_states(ij)%total_steps = solver_states(ij)%total_steps + 1
            solver_states(ij)%last_dt = dt
         END IF
         
         ! Print timestep info periodically
         IF (MOD(grid%itimestep, 100) == 0 .AND. ij == 1) THEN
            WRITE(message,'(A,I0,A,I0,A,F10.3,A)') &
               'SDIRK3 Zero-Copy: Tile ', ij, ', timestep ', grid%itimestep, &
               ', dt=', dt, ' seconds'
            CALL wrf_debug(1, message)
         END IF
         
         CALL wrf_debug(2, 'SDIRK3 Zero-Copy: Calling actual solver')
         
         ! Debug msfty values - check within tile bounds, not halo region
         WRITE(message, '(A,5F12.4)') 'SDIRK3: First 5 msfty values from Fortran (tile start): ', &
              grid%msfty(its,jts), grid%msfty(its+1,jts), grid%msfty(its+2,jts), &
              grid%msfty(its+3,jts), grid%msfty(its+4,jts)
         CALL wrf_debug(0, message)
         
         ! Check for zeros in msfty
         nzeros = 0
         DO j = jts, jte
            DO i = its, ite
               IF (grid%msfty(i,j) == 0.0) nzeros = nzeros + 1
            END DO
         END DO
         WRITE(message, '(A,I8,A,I8)') 'SDIRK3: msfty has ', nzeros, ' zeros out of ', (ite-its+1)*(jte-jts+1)
         CALL wrf_debug(0, message)
         
         ! Debug output for rdnw and rdn values before passing to C++
         IF (ij == 1) THEN  ! Only print for first tile
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 rdnw values from Fortran: ', &
               grid%rdnw(kds), grid%rdnw(kds+1), grid%rdnw(kds+2), grid%rdnw(kds+3), grid%rdnw(kds+4)
            CALL wrf_debug(1, message)
            
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 rdn values from Fortran: ', &
               grid%rdn(kds+1), grid%rdn(kds+2), grid%rdn(kds+3), grid%rdn(kds+4), grid%rdn(kds+5)
            CALL wrf_debug(1, message)
            
            ! Also check znw values (vertical levels)
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 znw values from Fortran: ', &
               grid%znw(kms), grid%znw(kms+1), grid%znw(kms+2), grid%znw(kms+3), grid%znw(kms+4)
            CALL wrf_debug(1, message)
            
            ! Check dnw values too
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 dnw values from Fortran: ', &
               grid%dnw(kms), grid%dnw(kms+1), grid%dnw(kms+2), grid%dnw(kms+3), grid%dnw(kms+4)
            CALL wrf_debug(1, message)
         END IF
         
         ! Populate structs for v2 interface (ABI-safe)
         bounds%its = its
         bounds%ite = ite
         bounds%jts = jts
         bounds%jte = jte
         bounds%kts = kts
         bounds%kte = kte
         bounds%ids = ids
         bounds%ide = ide
         bounds%jds = jds
         bounds%jde = jde
         bounds%kds = kds
         bounds%kde = kde
         bounds%ims = ims
         bounds%ime = ime
         bounds%jms = jms
         bounds%jme = jme
         bounds%kms = kms
         bounds%kme = kme

         dims%nx = nx
         dims%ny = ny
         dims%nz = nz
         dims%nx_u = nx_u
         dims%ny_v = ny_v
         dims%nz_w = nz_w
         dims%rk_step = rk_step

         scalars%rdx = grid%rdx
         scalars%rdy = grid%rdy
         scalars%kdamp = REAL(config_flags%dampcoef, C_FLOAT)
         scalars%khdif = REAL(config_flags%khdif, C_FLOAT)
         scalars%kvdif = REAL(config_flags%kvdif, C_FLOAT)
         scalars%dtbc = 0.0  ! Not used in non-specified case
         scalars%dt = REAL(dt, C_FLOAT)

         bdy_cfg%spec_bdy_width = 0  ! Not used for non-specified boundaries
         bdy_cfg%spec_zone = 0
         bdy_cfg%relax_zone = 0
         bdy_cfg%padding = 0

         ! Call the v2 SDIRK3 solver (struct-based, ABI-safe)
         CALL sdirk3_tile_unified_step_zerocopy_v2( &
            tile_solvers(ij), &
            ! State variables - pass base addresses of the WHOLE array
            C_LOC(grid%u_2(ims,kms,jms)), &     ! u(ims,...) not u(its,...)
            C_LOC(grid%v_2(ims,kms,jms)), &     ! v(ims,...) not v(its,...)
            C_LOC(grid%w_2(ims,kms,jms)), &     ! w(ims,...) not w(its,...)
            C_LOC(grid%ph_2(ims,kms,jms)), &    ! ph(ims,...) not ph(its,...)
            C_LOC(grid%al(ims,kms,jms)), &      ! al(ims,...) not al(its,...)
            C_LOC(grid%mu_2(ims,jms)), &        ! mu(ims,...) not mu(its,...)
            C_LOC(grid%p(ims,kms,jms)), &       ! perturbation pressure
            C_LOC(grid%t_2(ims,kms,jms)), &     ! perturbation theta
            ! Moisture variables
            C_NULL_PTR, 0, &                     ! moist_ptr and n_moist (not used for now)
            ! Tendencies - create null pointers for now (will be computed internally)
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, & ! ru, rv, rw tendencies
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, & ! ph, al, mu tendencies
            C_NULL_PTR, C_NULL_PTR, &           ! p, t tendencies
            ! Grid metrics (scalars now in structs, only pointers remain)
            C_LOC(grid%rdnw(kds)), &             ! Reciprocal dnw array from kds (not kms)
            C_LOC(grid%rdn(kds)), &              ! Reciprocal dn array from kds (not kms)
            ! Map scale factors - pass memory start position (like state variables)
            C_LOC(grid%msftx(ims,jms)), &       ! Map factor x at mass points (memory start)
            C_LOC(grid%msfty(ims,jms)), &       ! Map factor y at mass points (memory start)
            C_LOC(grid%msfux(ims,jms)), &       ! Map factor x at u points (memory start)
            C_LOC(grid%msfuy(ims,jms)), &       ! Map factor y at u points (memory start)
            C_LOC(grid%msfvx(ims,jms)), &       ! Map factor x at v points (memory start)
            C_LOC(grid%msfvy(ims,jms)), &       ! Map factor y at v points (memory start)
            ! Vertical coordinate coefficients from memory start
            C_LOC(grid%c1f(kms)), &             ! C1 at w levels
            C_LOC(grid%c2f(kms)), &             ! C2 at w levels
            C_LOC(grid%c1h(kms)), &             ! C1 at mass levels
            C_LOC(grid%c2h(kms)), &             ! C2 at mass levels
            ! Vertical interpolation weights
            C_LOC(grid%fnm(kds)), &             ! fnm (fzm in WRF)
            C_LOC(grid%fnp(kds)), &             ! fnp (fzp in WRF)
            ! Coriolis parameters from memory start
            C_LOC(grid%f(ims,jms)), &           ! Coriolis parameter f
            C_LOC(grid%e(ims,jms)), &           ! Coriolis parameter e
            C_LOC(grid%sina(ims,jms)), &        ! sin(map rotation angle)
            C_LOC(grid%cosa(ims,jms)), &        ! cos(map rotation angle)
            ! Moisture correction factors from memory start
            C_LOC(cqu(ims,kms,jms)), &          ! U-momentum moisture correction
            C_LOC(cqv(ims,kms,jms)), &          ! V-momentum moisture correction
            C_LOC(cqw(ims,kms,jms)), &          ! W-momentum moisture correction
            ! Boundary arrays (NULL for non-specified case)
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_bdy_xs, xe, v_bdy_xs, xe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_bdy_xs, xe, t_bdy_xs, xe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_bdy_xs, xe, mu_bdy_xs, xe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_bdy_ys, ye, v_bdy_ys, ye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_bdy_ys, ye, t_bdy_ys, ye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_bdy_ys, ye, mu_bdy_ys, ye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_btend_xs, xe, v_btend_xs, xe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_btend_xs, xe, t_btend_xs, xe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_btend_xs, xe, mu_btend_xs, xe
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! u_btend_ys, ye, v_btend_ys, ye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! w_btend_ys, ye, t_btend_ys, ye
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  ! ph_btend_ys, ye, mu_btend_ys, ye
            ! fcx, gcx, fcy, gcy (NULL for non-specified case)
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &
            ! Struct pointers (ABI-safe parameter passing)
            C_LOC(bounds), C_LOC(dims), C_LOC(scalars), C_LOC(bdy_cfg))
            
         CALL wrf_debug(2, 'SDIRK3 Zero-Copy: Returned from actual solver')

         step_outcome_rc = sdirk3_tile_solver_get_last_step_outcome_zerocopy( &
            tile_solvers(ij), step_outcome_code, step_final_update_aborted, &
            step_progress_ratio, step_progress_ratio_valid)
         IF (step_outcome_rc == 0_C_INT) THEN
            CALL wrf_error_fatal("SDIRK3: Failed to query zerocopy step outcome")
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
         
         ! Zero-Copy: No need to copy data back - it's already updated in place!
         
      END DO
      !$OMP END PARALLEL DO
      
      ! NOTE (r56 BC consistency):
      ! Halo exchange is handled in solve_em.F immediately after the rk_step=1
      ! SDIRK3 call. Keeping another exchange here duplicates boundary updates
      ! and can over-apply expensive MPI halo work when rk_step=2,3 are no-op.
      ! Keep advance_implicit focused on tile solve only.
      
   END SUBROUTINE advance_implicit
   
   !-----------------------------------------------------------------------
   SUBROUTINE finalize_implicit_sdirk3()
      IMPLICIT NONE
      INTEGER :: ij
      
      IF (sdirk3_initialized) THEN
         DO ij = 1, max_tiles
            IF (solver_valid(ij) .AND. C_ASSOCIATED(tile_solvers(ij))) THEN
               CALL sdirk3_tile_solver_destroy_zerocopy(tile_solvers(ij))
               tile_solvers(ij) = C_NULL_PTR
               solver_valid(ij) = .FALSE.
            END IF
         END DO
         
         ! PR 7B (review P1-1): the rank halo lifecycle now creates an owned
         ! communicator, a geometry fingerprint and a published state — end
         ! them EXACTLY ONCE here, on the main thread, after every tile
         ! solver is destroyed. Without this a re-init stayed bound to the
         ! previous fingerprint/communicator.
         CALL sdirk3_halo_finalize()

         IF (ALLOCATED(tile_solvers)) DEALLOCATE(tile_solvers)
         IF (ALLOCATED(solver_valid)) DEALLOCATE(solver_valid)
         IF (ALLOCATED(solver_states)) DEALLOCATE(solver_states)
         
         sdirk3_initialized = .FALSE.
         ! PR 7B (review P1-1): without this reset a re-created solver set
         ! never receives its base state again.
         base_state_initialized = .FALSE.
         num_tiles_initialized = 0

         ! v20.14r27n: Reset config freeze so re-initialization can set config again
         CALL wrf_sdirk3_reset_workers_started()
         max_tiles = 0
         
         CALL wrf_message("UNIFIED SDIRK3: Solvers finalized and memory deallocated")
      END IF
      
   END SUBROUTINE finalize_implicit_sdirk3
   
   !-----------------------------------------------------------------------
   FUNCTION get_sdirk3_dt_recommendation(grid, config_flags) RESULT(dt_new)
      IMPLICIT NONE
      
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      REAL :: dt_new
      
      ! Local variables
      INTEGER :: ij
      REAL :: dt_min, dt_tile
      REAL :: error_norm, safety_factor
      REAL :: dt_current
      
      IF (.NOT. sdirk3_initialized) THEN
         CALL wrf_error_fatal("SDIRK3: Cannot get dt recommendation - solvers not initialized")
      END IF
      
      ! Current timestep
      dt_current = grid%dt
      
      ! Get safety factor from config
      safety_factor = config_flags%sdirk3_safety_factor
      IF (safety_factor <= 0.0 .OR. safety_factor > 1.0) THEN
         safety_factor = 0.9  ! Default
      END IF
      
      ! If adaptive timestep is disabled, return current dt
      IF (.NOT. config_flags%sdirk3_adaptive_dt) THEN
         dt_new = dt_current
         RETURN
      END IF
      
      ! Get minimum recommended timestep across all tiles
      dt_min = HUGE(1.0)
      
      !$OMP PARALLEL DO REDUCTION(MIN:dt_min) PRIVATE(ij,dt_tile)
      DO ij = 1, grid%num_tiles
         IF (C_ASSOCIATED(tile_solvers(ij))) THEN
            ! In real implementation, this would call C++ solver
            ! For now, use a simple CFL-based estimate
            dt_tile = dt_current * safety_factor
            
            ! Apply min/max factors
            dt_tile = MAX(dt_current * config_flags%sdirk3_dt_min_factor, &
                         MIN(dt_tile, dt_current * config_flags%sdirk3_dt_max_factor))
            
            dt_min = MIN(dt_min, dt_tile)
         END IF
      END DO
      !$OMP END PARALLEL DO
      
      ! Use global minimum across all processes
      dt_new = wrf_dm_min_real(dt_min)
      
      ! Fallback to current timestep if no recommendation
      IF (dt_new <= 0.0 .OR. dt_new >= HUGE(1.0)) THEN
         dt_new = dt_current
      END IF
      
      ! Log adaptive timestep info periodically
      IF (MOD(grid%itimestep, 10) == 0) THEN
         WRITE(wrf_err_message,'(A,F10.3,A,F10.3)') &
            'SDIRK3 Adaptive dt: current=', dt_current, ', recommended=', dt_new
         CALL wrf_debug(2, wrf_err_message)
      END IF
      
   END FUNCTION get_sdirk3_dt_recommendation
   
   !-----------------------------------------------------------------------
   SUBROUTINE check_state_validity(array, name, nx, ny, nz)
      USE module_configure, ONLY : wrf_err_message
      USE module_wrf_error
      USE, INTRINSIC :: IEEE_ARITHMETIC
      IMPLICIT NONE
      
      INTEGER, INTENT(IN) :: nx, ny, nz
      REAL, DIMENSION(nx,ny,nz), INTENT(IN) :: array
      CHARACTER(LEN=*), INTENT(IN) :: name
      
      INTEGER :: i, j, k
      INTEGER :: nan_count, inf_count
      REAL :: max_val, min_val
      LOGICAL :: has_issues
      
      nan_count = 0
      inf_count = 0
      max_val = -HUGE(1.0)
      min_val = HUGE(1.0)
      has_issues = .FALSE.
      
      DO k = 1, nz
         DO j = 1, ny
            DO i = 1, nx
               IF (IEEE_IS_NAN(array(i,j,k))) THEN
                  nan_count = nan_count + 1
                  has_issues = .TRUE.
               ELSE IF (.NOT. IEEE_IS_FINITE(array(i,j,k))) THEN
                  inf_count = inf_count + 1
                  has_issues = .TRUE.
               ELSE
                  max_val = MAX(max_val, array(i,j,k))
                  min_val = MIN(min_val, array(i,j,k))
               END IF
            END DO
         END DO
      END DO
      
      IF (has_issues) THEN
         WRITE(wrf_err_message,'(A,A,A,I0,A,I0)') &
            'SDIRK3 ERROR: Invalid values in ', name, &
            ': NaN count=', nan_count, ', Inf count=', inf_count
         CALL wrf_error_fatal(wrf_err_message)
      END IF
      
      ! Warn for extreme values
      IF (ABS(max_val) > 1.0E10 .OR. ABS(min_val) > 1.0E10) THEN
         WRITE(wrf_err_message,'(A,A,A,ES12.5,A,ES12.5)') &
            'SDIRK3 WARNING: Extreme values in ', name, &
            ': min=', min_val, ', max=', max_val
         CALL wrf_debug(1, wrf_err_message)
      END IF
      
   END SUBROUTINE check_state_validity
   
   !-----------------------------------------------------------------------
   SUBROUTINE check_2d_validity(array, name, nx, ny)
      USE module_configure, ONLY : wrf_err_message
      USE module_wrf_error
      USE, INTRINSIC :: IEEE_ARITHMETIC
      IMPLICIT NONE
      
      INTEGER, INTENT(IN) :: nx, ny
      REAL, DIMENSION(nx,ny), INTENT(IN) :: array
      CHARACTER(LEN=*), INTENT(IN) :: name
      
      INTEGER :: i, j
      INTEGER :: nan_count, inf_count
      REAL :: max_val, min_val
      LOGICAL :: has_issues
      
      nan_count = 0
      inf_count = 0
      max_val = -HUGE(1.0)
      min_val = HUGE(1.0)
      has_issues = .FALSE.
      
      DO j = 1, ny
         DO i = 1, nx
            IF (IEEE_IS_NAN(array(i,j))) THEN
               nan_count = nan_count + 1
               has_issues = .TRUE.
            ELSE IF (.NOT. IEEE_IS_FINITE(array(i,j))) THEN
               inf_count = inf_count + 1
               has_issues = .TRUE.
            ELSE
               max_val = MAX(max_val, array(i,j))
               min_val = MIN(min_val, array(i,j))
            END IF
         END DO
      END DO
      
      IF (has_issues) THEN
         WRITE(wrf_err_message,'(A,A,A,I0,A,I0)') &
            'SDIRK3 ERROR: Invalid values in ', name, &
            ': NaN count=', nan_count, ', Inf count=', inf_count
         CALL wrf_error_fatal(wrf_err_message)
      END IF
      
      ! Warn for extreme values
      IF (ABS(max_val) > 1.0E10 .OR. ABS(min_val) > 1.0E10) THEN
         WRITE(wrf_err_message,'(A,A,A,ES12.5,A,ES12.5)') &
            'SDIRK3 WARNING: Extreme values in ', name, &
            ': min=', min_val, ', max=', max_val
         CALL wrf_debug(1, wrf_err_message)
      END IF
      
   END SUBROUTINE check_2d_validity
   
   !-----------------------------------------------------------------------
   ! Removed unused wrapper functions that were duplicating direct calls
   ! The current integration uses direct calls to init_implicit_sdirk3
   ! and advance_implicit from solve_em.F
   !-----------------------------------------------------------------------
   
   
   
   !-----------------------------------------------------------------------
   SUBROUTINE sdirk3_configure_from_namelist(config_flags)
      USE module_dm, ONLY : ntasks_x, ntasks_y
      IMPLICIT NONE
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      CHARACTER(LEN=256) :: config_name
      CHARACTER(KIND=C_CHAR), DIMENSION(257) :: c_name
      INTEGER :: i
      INTEGER :: effective_max_krylov_iter
      REAL :: effective_krylov_tol
      LOGICAL :: enable_stage_halo_exchange
      
      ! Convert Fortran string to C string helper
      INTEGER :: str_len
      INTEGER, PARAMETER :: default_max_krylov_iter = 100
      INTEGER, PARAMETER :: default_max_gmres_iter  = 100
      REAL, PARAMETER :: default_krylov_tol = 1.0E-3
      REAL, PARAMETER :: default_gmres_tol  = 1.0E-7
      
      ! Newton solver settings
      config_name = "max_newton_iter"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_max_newton_iter)
      
      config_name = "newton_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_newton_tol)

      config_name = "newton_rtol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_newton_rtol)
      
      config_name = "gmres_restart"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_gmres_restart)

      ! Krylov solver parameters
      ! Keep max_krylov_iter/krylov_tol as canonical keys while accepting
      ! legacy max_gmres_iter/gmres_tol if only legacy knobs are customized.
      effective_max_krylov_iter = config_flags%sdirk3_max_krylov_iter
      IF (config_flags%sdirk3_max_krylov_iter == default_max_krylov_iter .AND. &
          config_flags%sdirk3_max_gmres_iter /= default_max_gmres_iter) THEN
         effective_max_krylov_iter = config_flags%sdirk3_max_gmres_iter
      ELSE IF (config_flags%sdirk3_max_krylov_iter /= default_max_krylov_iter .AND. &
               config_flags%sdirk3_max_gmres_iter /= default_max_gmres_iter .AND. &
               config_flags%sdirk3_max_krylov_iter /= config_flags%sdirk3_max_gmres_iter) THEN
         CALL wrf_debug(1, "SDIRK3: Both max_krylov_iter and max_gmres_iter set; using max_krylov_iter")
      END IF

      effective_krylov_tol = config_flags%sdirk3_krylov_tol
      IF (ABS(config_flags%sdirk3_krylov_tol - default_krylov_tol) <= 1.0E-12 .AND. &
          ABS(config_flags%sdirk3_gmres_tol  - default_gmres_tol ) >  1.0E-12) THEN
         effective_krylov_tol = config_flags%sdirk3_gmres_tol
      ELSE IF (ABS(config_flags%sdirk3_krylov_tol - default_krylov_tol) > 1.0E-12 .AND. &
               ABS(config_flags%sdirk3_gmres_tol  - default_gmres_tol ) > 1.0E-12 .AND. &
               ABS(config_flags%sdirk3_krylov_tol - config_flags%sdirk3_gmres_tol) > 1.0E-12) THEN
         CALL wrf_debug(1, "SDIRK3: Both krylov_tol and gmres_tol set; using krylov_tol")
      END IF

      config_name = "max_krylov_iter"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, effective_max_krylov_iter)

      config_name = "krylov_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, effective_krylov_tol)

      ! Preconditioner type configuration
      WRITE(6,*) '[FORTRAN DEBUG] config_flags%sdirk3_precond_type = ', config_flags%sdirk3_precond_type
      config_name = "precond_type"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_precond_type)

      ! Legacy-compatible damping knob from namelist/Registry
      config_name = "precond_mu_coupling_damping"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_precond_mu_coupling_damping)
      
      ! JVP method settings
      config_name = "jvp_method"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_jvp_method)
      
      config_name = "jvp_epsilon"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_jvp_epsilon)
      
      ! Pass use_autograd flag
      config_name = "use_autograd"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_use_autograd))

      ! Advanced JVP tuning knobs
      config_name = "jvp_checkpointing"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_jvp_checkpointing))

      config_name = "jvp_checkpoint_segments"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_jvp_checkpoint_segments)

      config_name = "jvp_graph_caching"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_jvp_graph_caching))

      config_name = "jvp_batched"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_jvp_batched))

      config_name = "jvp_batch_size"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_jvp_batch_size)

      config_name = "jvp_mixed_precision"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_jvp_mixed_precision))
      
      ! Numerical scheme options
      config_name = "advection_order"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_advection_order)
      
      config_name = "diffusion_option"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%diff_opt)
      
      config_name = "diff_coef_h"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_diff_coef_h)
      
      config_name = "diff_coef_v"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_diff_coef_v)
      
      config_name = "diff4_coef"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_diff4_coef)
      
      ! Pass WRF diffusion coefficients from namelist
      config_name = "khdif"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%khdif)
      
      config_name = "kvdif"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%kvdif)

      config_name = "kdamp"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_kdamp)

      config_name = "w_damp_alpha"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_w_damp_alpha)

      config_name = "w_crit_cfl"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_w_crit_cfl)
      
      ! Debug level configuration
      config_name = "debug_level"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_debug_level)

      ! Consistency-first policy:
      ! Enable C++ inter-stage halo exchange from runtime decomposition truth.
      ! Do not depend on namelist nproc_x/nproc_y here because auto
      ! decomposition may leave those unset while MPI is actually multi-rank.
      enable_stage_halo_exchange = .FALSE.
#ifdef DM_PARALLEL
      IF (ntasks_x > 0 .AND. ntasks_y > 0) THEN
         enable_stage_halo_exchange = (ntasks_x * ntasks_y > 1)
      ELSE
         ! Runtime topology is not ready at configure-time.
         ! Keep the capability enabled; C++ stage gates still require
         ! runtime nprocx*nprocy > 1 before exchanging halos.
         enable_stage_halo_exchange = .TRUE.
      END IF
#endif
      config_name = "enable_stage_halo_exchange"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, enable_stage_halo_exchange))

      config_name = "stage_boundary_sync"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_stage_boundary_sync))

      config_name = "check_nan"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_check_nan))

      config_name = "check_conservation"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_check_conservation))

      ! Threading control for LibTorch execution backend
      config_name = "n_threads"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_n_threads)

      ! GMRES scaling mode: 0=RMS (default), 1=PHYSICS (per-element from base state)
      config_name = "scaling_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, 1)

      ! Boundary condition options
      config_name = "lateral_bc_option"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_lateral_bc_option)
      
      config_name = "top_bc_option"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_top_bc_option)
      
      config_name = "rayleigh_damp_coef"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_rayleigh_coef)
      
      config_name = "rayleigh_damp_depth"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_rayleigh_depth)
      
      ! CFL monitoring
      config_name = "check_cfl"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_check_cfl))
      
      config_name = "cfl_target"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_cfl_target)

      ! Adaptive timestep control (Gap #3)
      config_name = "adaptive_timestep"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_adaptive_timestep))

      config_name = "safety_factor"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_safety_factor)

      config_name = "dt_min_factor"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_dt_min_factor)

      config_name = "dt_max_factor"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_dt_max_factor)

      config_name = "error_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_error_tol)

      ! Performance options (Gap #3)
      config_name = "use_jvp_cache"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_use_jvp_cache))

      ! Newton-Krylov optimization (Gap #3)
      config_name = "nk_adaptive_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_nk_adaptive_tol))

      ! FIX v16: Pass nk_line_search from namelist to C++
      config_name = "nk_line_search"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_nk_line_search))

      ! Trust-region globalization controls
      config_name = "nk_trust_region"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_nk_trust_region))

      config_name = "nk_trust_radius"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_nk_trust_radius)

      ! v20.14 r49: Stage-aware GMRES + JVP auto-bench
      config_name = "stage2_gmres_restart"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_stage2_gmres_restart)

      config_name = "stage2_max_krylov_restarts"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_stage2_max_krylov_restarts)

      config_name = "stage2_krylov_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_stage2_krylov_tol)

      config_name = "stage2_ew_eta_min"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_stage2_ew_eta_min)

      config_name = "stage2_ew_eta_max"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_stage2_ew_eta_max)

      config_name = "stage3_gmres_restart"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_stage3_gmres_restart)

      config_name = "stage3_max_krylov_restarts"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_stage3_max_krylov_restarts)

      config_name = "stage3_krylov_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_stage3_krylov_tol)

      config_name = "stage3_ew_eta_min"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_stage3_ew_eta_min)

      config_name = "stage3_ew_eta_max"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_stage3_ew_eta_max)

      config_name = "imex_split_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_imex_split_mode)

      config_name = "imex_slow_in_tangent"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_imex_slow_in_tangent))

      config_name = "imex_phys_in_tangent"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_imex_phys_in_tangent))

      config_name = "stage1_explicit"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_stage1_explicit))

      config_name = "stage3_warmstart"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_stage3_warmstart))

      config_name = "stage_fail_action"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, MAX(0, MIN(2, config_flags%sdirk3_stage_fail_action)))

      config_name = "gate_metric_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, MAX(0, MIN(2, config_flags%sdirk3_gate_metric_mode)))

      config_name = "stage3_gate_rel_threshold"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, MAX(0.0, config_flags%sdirk3_stage3_gate_rel_threshold))

      config_name = "stagnation_gate_enable"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_stagnation_gate_enable))

      config_name = "stagnation_growth_floor"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, MAX(0.0, config_flags%sdirk3_stagnation_growth_floor))

      config_name = "stage_require_convergence"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_stage_require_convergence))

      config_name = "hevi_split"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_hevi_split))

      config_name = "split_explicit"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_split_explicit))

      ! Split-explicit acoustic-loop parameters: pass-throughs of EXISTING WRF namelist values
      ! (no new Registry entries). Consumed by the C++ split-explicit branch only when
      ! sdirk3_split_explicit is ON; C++ defaults match em_b_wave so unset = no behavior change.
      config_name = "split_explicit_time_step_sound"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, MAX(0, config_flags%time_step_sound))

      config_name = "split_explicit_epssm"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, MAX(0.0, config_flags%epssm))

      config_name = "split_explicit_smdiv"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, MAX(0.0, config_flags%smdiv))

      config_name = "split_explicit_emdiv"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, MAX(0.0, config_flags%emdiv))

      config_name = "split_explicit_top_lid"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%top_lid))

      config_name = "hopeless_relax"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_hopeless_relax))

      config_name = "precond_ratio_guard_warn_only"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_precond_ratio_guard_warn_only))

      config_name = "jvp_auto_bench_calls"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_jvp_auto_bench_calls)

      config_name = "jvp_auto_bench_quality_gate"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_jvp_auto_bench_quality_gate))

      config_name = "jvp_auto_bench_warmup"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_jvp_auto_bench_warmup)

      config_name = "jvp_auto_bench_seed"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_jvp_auto_bench_seed)

      config_name = "jvp_auto_bench_lock_reset_stage1"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_jvp_auto_bench_lock_reset_stage1))

      config_name = "solver_telemetry"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_solver_telemetry))

      config_name = "progress_invariant_enable"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_progress_invariant_enable))

      config_name = "progress_invariant_min_ratio"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_progress_invariant_min_ratio)

      config_name = "progress_invariant_streak_limit"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_progress_invariant_streak_limit)

      config_name = "variable_pc_event_log"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_variable_pc_event_log))

      config_name = "gmres_warmstart"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_gmres_warmstart))

      config_name = "gmres_warmstart_quality_gate"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_gmres_warmstart_quality_gate)

      config_name = "inn_warmstart_enable"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_inn_warmstart_enable))

      config_name = "inn_residual_gate_enable"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_inn_residual_gate_enable))

      config_name = "inn_q_min"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_inn_q_min)

      config_name = "inn_gate_rel_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_inn_gate_rel_tol)

      config_name = "inn_gate_q_noise"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_inn_gate_q_noise)

      config_name = "inn_beta_max"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_inn_beta_max)

      config_name = "inn_ood_guard_enable"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_inn_ood_guard_enable))

      config_name = "inn_tol_ramp_enable"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_inn_tol_ramp_enable))

      config_name = "inn_tol_ramp_gamma"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_inn_tol_ramp_gamma)

      config_name = "rhs_bc_parity"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_rhs_bc_parity))

      config_name = "mass_pgf_bc_guard"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_mass_pgf_bc_guard))

      ! v20.14 r49: Smoothing + block-aware trust + direct-u solve
      config_name = "precond_horizontal_smooth_alpha"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_precond_horizontal_smooth_alpha)

      config_name = "precond_horizontal_smooth_iters"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_precond_horizontal_smooth_iters)

      config_name = "precond_vertical_smooth_alpha"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_precond_vertical_smooth_alpha)

      config_name = "precond_smooth_boundary_guard"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_precond_smooth_boundary_guard))

      config_name = "trust_region_block_aware_thresh"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_trust_region_block_aware_thresh)

      config_name = "trust_fallback_relax"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_trust_fallback_relax))

      config_name = "trust_fallback_ratio"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_trust_fallback_ratio)

      config_name = "direct_u_solve_thresh"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_direct_u_solve_thresh)

      ! Selective implicit/explicit treatment (Gap #3)
      config_name = "implicit_acoustic"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_implicit_acoustic))

      config_name = "implicit_gravity"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_implicit_gravity))

      config_name = "implicit_rayleigh"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_implicit_rayleigh))

      config_name = "implicit_wdamp"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_implicit_wdamp))

      config_name = "implicit_vdiff"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_implicit_vdiff))

      config_name = "implicit_hdiff"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_implicit_hdiff))

      config_name = "implicit_divergence"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_implicit_divergence))

      ! Preconditioner optimization options (Gap #3)
      config_name = "precond_diagonal_only"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_precond_diagonal_only))

      config_name = "precond_block_jacobi"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_precond_block_jacobi))

      config_name = "precond_block_size"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_precond_block_size)

      config_name = "precond_ilu"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_precond_ilu))

      config_name = "precond_ilu_level"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_precond_ilu_level)

      config_name = "precond_multigrid"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_precond_multigrid))

      config_name = "precond_mg_levels"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_precond_mg_levels)

      config_name = "memory_pool"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_memory_pool))

      config_name = "memory_pool_size_mb"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_memory_pool_size_mb)

      config_name = "tensor_checkpointing"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_tensor_checkpointing))

      config_name = "gradient_checkpointing"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_gradient_checkpointing))

      ! 4DVAR replay knobs (checkpoint + optional graph retention)
      config_name = "save_trajectory"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_save_trajectory))

      config_name = "checkpoint_interval"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, MAX(1, config_flags%sdirk3_checkpoint_interval))

      config_name = "retain_graph_for_adjoint"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_retain_graph_for_adjoint))

      config_name = "enable_ad_halo_exchange"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_enable_ad_halo_exchange))

      config_name = "obs_aware_4dvar"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_obs_aware_4dvar))

      config_name = "obs_source_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, MAX(0, MIN(2, config_flags%sdirk3_obs_source_mode)))

      config_name = "obs_window_sync_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, MAX(0, MIN(2, config_flags%sdirk3_obs_window_sync_mode)))

      ! Validation/diagnostic knobs
      config_name = "validation_level"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_validation_level)

      config_name = "check_staggered_consistency"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_check_staggered))

      config_name = "collect_validation_stats"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_collect_stats))

      config_name = "conservation_tol_mass"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_conserv_tol_mass)

      config_name = "conservation_tol_energy"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_conserv_tol_energy)

      config_name = "conservation_tol_momentum"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_conserv_tol_momentum)

      config_name = "enable_benchmarking"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_enable_benchmarking))

      config_name = "benchmark_warmup_iter"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_benchmark_warmup)

      config_name = "benchmark_measure_iter"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_benchmark_measure)

      CALL wrf_message("SDIRK3: Configuration updated from namelist")

   END SUBROUTINE sdirk3_configure_from_namelist
   
   !-----------------------------------------------------------------------
   ! New interfaces for solve_em integration
   !-----------------------------------------------------------------------
   
   SUBROUTINE sdirk3_initialize_solvers(grid, config_flags)
      IMPLICIT NONE
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags

      ! v20.14r27n: Idempotent guard — if config is already frozen (workers started),
      ! skip physics coupling + freeze. init_implicit_sdirk3 has its own sdirk3_initialized
      ! guard, but the wrapper calls below (configure_physics_coupling, mark_workers_started)
      ! do not. Without this guard, the second call from solve_em.F:618 triggers
      ! [CONFIG REJECT] for physics_mode/frozen_physics_jvp.
      IF (wrf_sdirk3_is_workers_started() /= 0) THEN
         CALL wrf_debug(1, "SDIRK3: Already initialized and frozen, skipping re-init")
         RETURN
      END IF

      ! This is a wrapper for solve_em - calls init_implicit_sdirk3
      CALL init_implicit_sdirk3(grid, config_flags)

      ! Set up physics coupling configuration
      CALL sdirk3_configure_physics_coupling(config_flags)

      CALL wrf_message("SDIRK3: Solvers initialized for solve_em")

      ! Print JVP method being used
      SELECT CASE(config_flags%sdirk3_jvp_method)
      CASE(0)
         CALL wrf_message("SDIRK3: Using finite difference for JVP computation")
      CASE(1)
         CALL wrf_message("SDIRK3: Using automatic differentiation for JVP computation")
      CASE(2)
         CALL wrf_message("SDIRK3: Using dual number approach for JVP computation")
      CASE(3)
         CALL wrf_message("SDIRK3: Using optimized batched method for JVP computation")
      END SELECT

      ! FIX 2025-01-25: Config freeze point - MUST be AFTER all config including physics coupling
      ! This ensures no set_config calls happen after mark_workers_started
      CALL wrf_sdirk3_mark_workers_started()
      CALL wrf_debug(1, "SDIRK3: Config freeze point reached - workers started")

   END SUBROUTINE sdirk3_initialize_solvers
   
   !-----------------------------------------------------------------------
   
   SUBROUTINE sdirk3_finalize_solvers()
      IMPLICIT NONE
      
      ! This is a wrapper for solve_em - calls finalize_implicit_sdirk3
      CALL finalize_implicit_sdirk3()
      
   END SUBROUTINE sdirk3_finalize_solvers
   
   !-----------------------------------------------------------------------
   ! Physics coupling infrastructure (prepared for future integration)
   !-----------------------------------------------------------------------
   
   SUBROUTINE sdirk3_configure_physics_coupling(config_flags)
      IMPLICIT NONE
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      CHARACTER(LEN=256) :: config_name
      INTEGER :: physics_mode
      
      ! Determine physics mode based on WRF configuration
      physics_mode = 0  ! 0 = dynamics only (current implementation)
      
      ! Future: Set physics mode based on namelist options
      ! IF (config_flags%mp_physics > 0) physics_mode = IOR(physics_mode, 1)
      ! IF (config_flags%ra_lw_physics > 0) physics_mode = IOR(physics_mode, 2)
      ! IF (config_flags%ra_sw_physics > 0) physics_mode = IOR(physics_mode, 4)
      ! IF (config_flags%bl_pbl_physics > 0) physics_mode = IOR(physics_mode, 8)
      ! IF (config_flags%cu_physics > 0) physics_mode = IOR(physics_mode, 16)
      
      config_name = "physics_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, physics_mode)
      
      ! Configure physics coupling options
      config_name = "frozen_physics_jvp"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, 1)  ! Frozen JVP for physics
      
      CALL wrf_debug(1, "SDIRK3: Physics coupling configured (dynamics-only mode)")
      
   END SUBROUTINE sdirk3_configure_physics_coupling
   
   !-----------------------------------------------------------------------
   
   SUBROUTINE sdirk3_prepare_physics_coupling(grid, config_flags)
      IMPLICIT NONE
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      ! Future: This will prepare physics tendencies for SDIRK3
      ! Currently a placeholder for em_b_wave testing
      
      CALL wrf_debug(2, "SDIRK3: Physics coupling prepared (placeholder)")
      
   END SUBROUTINE sdirk3_prepare_physics_coupling
   
   !-----------------------------------------------------------------------
   
   SUBROUTINE sdirk3_apply_physics_tendencies(grid, config_flags, dt)
      IMPLICIT NONE
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      REAL, INTENT(IN) :: dt
      
      ! Future: This will apply physics tendencies after SDIRK3 dynamics
      ! Currently a placeholder for em_b_wave testing
      
      CALL wrf_debug(2, "SDIRK3: Physics tendencies applied (placeholder)")
      
   END SUBROUTINE sdirk3_apply_physics_tendencies

   !-----------------------------------------------------------------------
   ! 4DVAR checkpoint-replay helpers (retain_graph_for_adjoint OFF path)
   !-----------------------------------------------------------------------

   SUBROUTINE sdirk3_get_4dvar_trajectory_info(tile_idx, checkpoint_count, &
                                               global_timestep, state_vector_size, ierr)
      IMPLICIT NONE
      INTEGER, INTENT(IN) :: tile_idx
      INTEGER, INTENT(OUT) :: checkpoint_count, global_timestep
      INTEGER, INTENT(OUT) :: state_vector_size, ierr
      INTEGER(C_INT) :: c_count, c_gts, c_size

      checkpoint_count = 0
      global_timestep = 0
      state_vector_size = 0
      ierr = 1

      IF (.NOT. sdirk3_initialized) RETURN
      IF (tile_idx < 1 .OR. tile_idx > num_tiles_initialized) RETURN
      IF (.NOT. C_ASSOCIATED(tile_solvers(tile_idx))) RETURN

      c_count = sdirk3_tile_solver_get_saved_trajectory_count_zerocopy(tile_solvers(tile_idx))
      c_gts = sdirk3_tile_solver_get_saved_global_timestep_zerocopy(tile_solvers(tile_idx))
      c_size = sdirk3_tile_solver_get_state_vector_size_zerocopy(tile_solvers(tile_idx))

      checkpoint_count = INT(c_count)
      global_timestep = INT(c_gts)
      state_vector_size = INT(c_size)
      ierr = 0

   END SUBROUTINE sdirk3_get_4dvar_trajectory_info

   SUBROUTINE sdirk3_run_4dvar_adjoint_loop(tile_idx, lambda_terminal, lambda_initial, &
                                            lambda_size, dt, gamma, &
                                            gmres_restart, gmres_max_iter, gmres_tol, &
                                            checkpoints_used, ierr)
      IMPLICIT NONE
      INTEGER, INTENT(IN) :: tile_idx
      INTEGER, INTENT(IN) :: lambda_size
      REAL, DIMENSION(lambda_size), TARGET, INTENT(IN) :: lambda_terminal
      REAL, DIMENSION(lambda_size), TARGET, INTENT(OUT) :: lambda_initial
      REAL, INTENT(IN) :: dt, gamma
      INTEGER, INTENT(IN) :: gmres_restart, gmres_max_iter
      REAL, INTENT(IN) :: gmres_tol
      INTEGER, INTENT(OUT) :: checkpoints_used, ierr

      INTEGER(C_INT) :: c_size, c_used
      INTEGER(C_INT) :: c_lambda_size, c_restart, c_max_iter
      REAL(C_FLOAT) :: c_dt, c_gamma, c_tol

      checkpoints_used = -1
      ierr = 1

      IF (.NOT. sdirk3_initialized) RETURN
      IF (tile_idx < 1 .OR. tile_idx > num_tiles_initialized) RETURN
      IF (.NOT. C_ASSOCIATED(tile_solvers(tile_idx))) RETURN

      c_size = sdirk3_tile_solver_get_state_vector_size_zerocopy(tile_solvers(tile_idx))
      IF (INT(c_size) /= lambda_size) RETURN

      c_lambda_size = INT(lambda_size, C_INT)
      c_restart = INT(MAX(1, gmres_restart), C_INT)
      c_max_iter = INT(MAX(1, gmres_max_iter), C_INT)
      c_dt = REAL(dt, C_FLOAT)
      c_gamma = REAL(gamma, C_FLOAT)
      c_tol = REAL(MAX(gmres_tol, 1.0E-8), C_FLOAT)

      c_used = sdirk3_tile_solver_run_adjoint_replay_zerocopy( &
         tile_solvers(tile_idx), &
         C_LOC(lambda_terminal(1)), c_lambda_size, C_LOC(lambda_initial(1)), &
         c_dt, c_gamma, c_restart, c_max_iter, c_tol)

      ! Review round 3: 0 replayed checkpoints is an identity adjoint, not a
      ! success — treat <= 0 as error (ierr stays 1). The C wrapper also
      ! returns -1 for 0; this covers binary skew.
      checkpoints_used = INT(c_used)
      IF (checkpoints_used <= 0) RETURN

      ierr = 0

   END SUBROUTINE sdirk3_run_4dvar_adjoint_loop
   
   !-----------------------------------------------------------------------
   ! Performance monitoring utilities
   !-----------------------------------------------------------------------
   
   SUBROUTINE sdirk3_print_solver_statistics()
      IMPLICIT NONE
      INTEGER :: ij
      CHARACTER(LEN=256) :: message
      
      IF (.NOT. sdirk3_initialized) RETURN
      
      CALL wrf_message(" ")
      CALL wrf_message("========== SDIRK3 Solver Statistics ==========")
      
      DO ij = 1, max_tiles
         IF (solver_states(ij)%is_initialized) THEN
            WRITE(message, '(A,I3,A,I6,A,I4,A,F8.4)') &
               "Tile ", ij, &
               ": Steps=", solver_states(ij)%total_steps, &
               ", Failed=", solver_states(ij)%failed_steps, &
               ", Last dt=", solver_states(ij)%last_dt
            CALL wrf_message(message)
         END IF
      END DO
      
      CALL wrf_message("==============================================")
      CALL wrf_message(" ")
      
   END SUBROUTINE sdirk3_print_solver_statistics
   
   !-----------------------------------------------------------------------
   ! Removed sdirk3_advance_timestep_tile - redundant wrapper
   ! advance_implicit is called directly from solve_em.F
   !-----------------------------------------------------------------------
   
   !-----------------------------------------------------------------------
   ! Halo exchange subroutine for SDIRK3 state variables
   ! Uses existing WRF RSL_LITE infrastructure
   !-----------------------------------------------------------------------
   SUBROUTINE sdirk3_halo_exchange(grid, config_flags)
      USE module_domain
      USE module_configure
      USE module_driver_constants
      USE module_machine
      USE module_state_description, ONLY : PARAM_FIRST_SCALAR, &
                                           num_moist, num_chem, num_tracer, num_scalar
      USE module_comm_dm_1, ONLY: halo_em_d2_5_sub
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
      INTEGER(C_INT) :: notify_status

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
      ! HALO_EM_D2_5 exchanges all 6 governing equation variables:
      !   u_2, v_2, w_2 (momentum equations)
      !   ph_2 (geopotential equation)
      !   al (theta - thermodynamic equation)
      !   mu_2 (mass continuity equation)
      ! The _5 suffix indicates 5-point stencil for 5th/6th order advection
# include "HALO_EM_D2_5.inc"
      ! PR 7B (3b-3 P1): checked freshness publication — status consumed
      notify_status = sdirk3_notify_halo_fresh_checked()
      IF (notify_status /= 1_C_INT) THEN
         CALL wrf_error_fatal( &
            "SDIRK3_MPI_HALO_FRESH_NOTIFY_FAILED: sdirk3_halo_exchange")
      END IF
#endif

   END SUBROUTINE sdirk3_halo_exchange

   !-----------------------------------------------------------------------
   ! FIX 2025-01-25: Reset all solver caches on restart
   ! This ensures base_state/metric caches are invalidated after restart
   ! when Fortran arrays may have been reallocated/reloaded from disk.
   !-----------------------------------------------------------------------
   SUBROUTINE sdirk3_reset_on_restart()
      IMPLICIT NONE

      INTEGER :: ij
      CHARACTER(LEN=256) :: message

      IF (.NOT. sdirk3_initialized) THEN
         CALL wrf_debug(1, "SDIRK3: sdirk3_reset_on_restart called but not initialized")
         RETURN
      END IF

      CALL wrf_message("SDIRK3: Resetting solver caches for restart")

      DO ij = 1, num_tiles_initialized
         IF (C_ASSOCIATED(tile_solvers(ij))) THEN
            ! Clear any saved 4DVAR trajectory to avoid stale replay across restart windows
            CALL sdirk3_tile_solver_clear_saved_trajectory_zerocopy(tile_solvers(ij))

            ! Call reset_full to invalidate all caches including:
            ! - Vertical metric caches (dnw, c1h, c2h, etc.)
            ! - MSF caches (map scale factors)
            ! - Pressure gradient caches
            ! - Divergence caches
            CALL sdirk3_tile_solver_reset_full(tile_solvers(ij))

            IF (solver_valid(ij)) THEN
               solver_states(ij)%total_steps = 0
               solver_states(ij)%failed_steps = 0
            END IF
         END IF
      END DO

      ! Reset base state flag to force re-initialization
      base_state_initialized = .FALSE.

      WRITE(message, '(A,I0,A)') "SDIRK3: Reset ", num_tiles_initialized, &
                                  " tile solvers for restart"
      CALL wrf_message(TRIM(message))

   END SUBROUTINE sdirk3_reset_on_restart

END MODULE module_implicit_sdirk3
