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
   
   PUBLIC :: sdirk3_configure_from_namelist
   PUBLIC :: sdirk3_prepare_physics_coupling, sdirk3_apply_physics_tendencies
   PUBLIC :: sdirk3_initialize_solvers, sdirk3_finalize_solvers
   PUBLIC :: sdirk3_print_solver_statistics
   PUBLIC :: sdirk3_reset_on_restart  
   PUBLIC :: sdirk3_get_4dvar_trajectory_info, sdirk3_run_4dvar_adjoint_loop
   
   
   
   
   REAL, PARAMETER :: gamma_sdirk3 = 0.4358665

   
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_ADVANCED      = 0_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_SKIPPED       = 1_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_SOFT_NO_PROGRESS = 10_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_HARD_STAGE_ABORT = 20_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INPUT      = 100_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INTERNAL   = 101_C_INT

   
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

   
   INTERFACE
      
      FUNCTION sdirk3_tile_solver_create_zerocopy(nx, ny, nz, dx, dy, rdnw_ptr, tile_id, &
                                                   nx_u, ny_v, nz_w) &
         BIND(C, name="sdirk3_tile_solver_create_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         INTEGER(C_INT), VALUE :: nx, ny, nz, tile_id
         REAL(C_FLOAT), VALUE :: dx, dy
         TYPE(C_PTR), VALUE :: rdnw_ptr
         INTEGER(C_INT), VALUE :: nx_u, ny_v, nz_w  
         TYPE(C_PTR) :: sdirk3_tile_solver_create_zerocopy
      END FUNCTION sdirk3_tile_solver_create_zerocopy
      
      
      SUBROUTINE sdirk3_tile_solver_destroy_zerocopy(solver) &
         BIND(C, name="sdirk3_tile_solver_destroy_zerocopy")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END SUBROUTINE sdirk3_tile_solver_destroy_zerocopy

      
      SUBROUTINE sdirk3_mpi_safety_init() &
         BIND(C, name="sdirk3_mpi_safety_init")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE sdirk3_mpi_safety_init

      
      SUBROUTINE sdirk3_tile_solver_reset_full(solver) &
         BIND(C, name="sdirk3_tile_solver_reset_full")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
      END SUBROUTINE sdirk3_tile_solver_reset_full

      
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

      
      SUBROUTINE wrf_sdirk3_mark_workers_started() &
         BIND(C, name="wrf_sdirk3_mark_workers_started")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE wrf_sdirk3_mark_workers_started

      
      INTEGER(C_INT) FUNCTION wrf_sdirk3_is_workers_started() &
         BIND(C, name="wrf_sdirk3_is_workers_started")
         USE, INTRINSIC :: ISO_C_BINDING
      END FUNCTION wrf_sdirk3_is_workers_started

      
      SUBROUTINE wrf_sdirk3_reset_workers_started() &
         BIND(C, name="wrf_sdirk3_reset_workers_started")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE wrf_sdirk3_reset_workers_started

      
      SUBROUTINE sdirk3_debug_step(solver, tile_id, itimestep) &
         BIND(C, name="sdirk3_debug_step")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver
         INTEGER(C_INT), VALUE :: tile_id, itimestep
      END SUBROUTINE sdirk3_debug_step
      
      
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
      
      
      SUBROUTINE sdirk3_tile_set_map_projection(solver_ptr, map_proj) &
         BIND(C, name="sdirk3_tile_set_map_projection")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_ptr
         INTEGER(C_INT), VALUE :: map_proj
      END SUBROUTINE sdirk3_tile_set_map_projection

      
      SUBROUTINE sdirk3_tile_set_mpi_process_info(solver_ptr, nprocx, nprocy, mypx, mypy) &
         BIND(C, name="sdirk3_tile_set_mpi_process_info")
         USE, INTRINSIC :: ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_ptr
         INTEGER(C_INT), VALUE :: nprocx, nprocy, mypx, mypy
      END SUBROUTINE sdirk3_tile_set_mpi_process_info
      
      
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

      
      SUBROUTINE sdirk3_notify_halo_fresh() &
         BIND(C, name="sdirk3_notify_halo_fresh")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE sdirk3_notify_halo_fresh
   END INTERFACE
   
   
   LOGICAL :: sdirk3_initialized = .FALSE.
   LOGICAL :: base_state_initialized = .FALSE.
   TYPE(C_PTR), ALLOCATABLE, SAVE :: tile_solvers(:)
   INTEGER, SAVE :: num_tiles_initialized = 0
   INTEGER, SAVE :: max_tiles = 0
   
   
   LOGICAL, ALLOCATABLE, SAVE :: solver_valid(:)
   
   
   TYPE :: tile_info_type
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: nx, ny, nz
      INTEGER :: tile_id
   END TYPE tile_info_type
   
   
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
   
   
   SUBROUTINE init_implicit_sdirk3(grid, config_flags)
      IMPLICIT NONE
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      
      INTEGER :: ij
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: nx, ny, nz, tile_id
      INTEGER :: nx_u, ny_v, nz_w  
      REAL(C_FLOAT) :: dx, dy
      CHARACTER(LEN=256) :: message
      
      CALL wrf_message("SDIRK3: init_implicit_sdirk3 called")
      
      IF (sdirk3_initialized) THEN
         CALL wrf_debug(2, "UNIFIED SDIRK3: Already initialized, skipping")
         RETURN
      END IF
      
      
      
      
      ids = grid%sd31
      ide = grid%ed31
      jds = grid%sd33  
      jde = grid%ed33  
      kds = grid%sd32  
      kde = SIZE(grid%pb, 2) 

      
      WRITE(message, '(A,I0,A,I0)') "SDIRK3 init_implicit: kde from pb array = ", kde, ", kde-1 = ", kde-1
      CALL wrf_message(message)
      ims = grid%sm31
      ime = grid%em31
      jms = grid%sm33  
      jme = grid%em33  
      kms = grid%sm32  
      kme = grid%em32  
      
      
      max_tiles = grid%num_tiles
      ALLOCATE(tile_solvers(max_tiles))
      ALLOCATE(solver_valid(max_tiles))
      ALLOCATE(solver_states(max_tiles))
      solver_valid(:) = .FALSE.
      
      WRITE(message, '(A,I0,A)') "UNIFIED SDIRK3: Allocating space for ", max_tiles, " tile solvers"
      CALL wrf_message(message)

      
      
      CALL sdirk3_configure_from_namelist(config_flags)

      
      CALL sdirk3_mpi_safety_init()

      
      !$OMP PARALLEL DO PRIVATE(ij,its,ite,jts,jte,kts,kte,nx,ny,nz,nx_u,ny_v,nz_w,tile_id,dx,dy) &
      !$OMP SHARED(ims,ime,jms,jme,kms,kme)
      DO ij = 1, grid%num_tiles
         
         
         its = grid%i_start(ij)
         ite = grid%i_end(ij)
         jts = grid%j_start(ij)
         jte = grid%j_end(ij)
         kts = kds
         kte = kde - 1
         
         
         IF (ij == 1) THEN
            WRITE(message, '(A,6I5)') "SDIRK3 DEBUG: kds/kde/kms/kme/kts/kte = ", kds, kde, kms, kme, kts, kte
            CALL wrf_message(message)
         END IF
         
         
         nx = ite - its + 1
         ny = jte - jts + 1
         
         nz = kte - kts + 1
         
         
         WRITE(message, '(A,I0,A,I0,A,I0,A,I0,A)') &
              "SDIRK3 init: WRF-consistent nz=", nz, &
              " (kde=", kde, ", kte=", kte, ", kts=", kts, ")"
         CALL wrf_message(message)
         tile_id = ij
         
         
         dx = config_flags%dx
         dy = config_flags%dy
         
         
         
         
         
         
         nx_u = nx + 1
         ny_v = ny + 1

         
         WRITE(message,'(A,I5,A,I5,A,L1)') 'SDIRK3 INIT DEBUG: jte=', jte, &
              ', jde=', jde, ', jte==jde=', (jte==jde)
         CALL wrf_message(message)

         
         nz_w = nz + 1

         WRITE(message,'(A,3I5,A,3I5)') 'SDIRK3 DEBUG: Creating solver with nx/ny/nz=', &
              nx, ny, nz, ', nx_u/ny_v/nz_w=', nx_u, ny_v, nz_w
         CALL wrf_message(message)
         
         
         
         tile_solvers(ij) = sdirk3_tile_solver_create_zerocopy( &
            nx, ny, nz, dx, dy, C_LOC(grid%rdnw(kds)), tile_id, nx_u, ny_v, nz_w)
         
         IF (.NOT. C_ASSOCIATED(tile_solvers(ij))) THEN
            WRITE(message, '(A,I0)') "UNIFIED SDIRK3: Failed to create solver for tile ", ij
            CALL wrf_error_fatal3("<stdin>",507,&
message)
         ELSE
            solver_valid(ij) = .TRUE.
            
            solver_states(ij)%solver_ptr = tile_solvers(ij)
            solver_states(ij)%is_initialized = .TRUE.
            solver_states(ij)%tile_id = tile_id
            solver_states(ij)%last_dt = config_flags%dt
            solver_states(ij)%total_steps = 0
            solver_states(ij)%failed_steps = 0
            
            
            
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
            
            
            CALL sdirk3_tile_set_map_projection(tile_solvers(ij), &
                 config_flags%map_proj)
            

         END IF
         
         
         
      END DO
      !$OMP END PARALLEL DO
      
      num_tiles_initialized = grid%num_tiles
      sdirk3_initialized = .TRUE.

      WRITE(message, '(A,I0,A)') "UNIFIED SDIRK3: Initialized ", num_tiles_initialized, " tile solvers"
      CALL wrf_message(message)
      CALL wrf_message("UNIFIED SDIRK3: L-stability eliminates CFL restriction on sound waves")

      
      
      

   END SUBROUTINE init_implicit_sdirk3

   
   
   
   
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
      INTEGER :: kde_consistent  
      CHARACTER(LEN=256) :: message
      
      
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
               kte = kde_consistent-1  
               WRITE(0,*) '=== kte = ', kte
               CALL FLUSH(0)

               WRITE(0,*) '=== About to write kte message ==='
               CALL FLUSH(0)

               
               WRITE(message, '(A,I0,A,I0,A,I0)') "SDIRK3 DEBUG: kde_consistent=", kde_consistent, ", kte calculated as kde_consistent-1=", kte, &
                    ", actual pb levels=", SIZE(grid%pb,2)

               WRITE(0,*) '=== Wrote kte message ==='
               CALL FLUSH(0)
               WRITE(0,*) '=== About to call wrf_debug ==='
               CALL FLUSH(0)
               CALL wrf_debug(1, message)
               WRITE(0,*) '=== wrf_debug done ==='
               CALL FLUSH(0)

               
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
               
               
               CALL sdirk3_tile_set_base_state_zerocopy(tile_solvers(ij), &
                    C_LOC(grid%pb(ims,kms,jms)), &    
                    C_LOC(grid%t_init(ims,kms,jms)), &
                    C_LOC(grid%phb(ims,kms,jms)), &   
                    C_LOC(grid%mub(ims,jms)), &       
                    its, ite, jts, jte, kts, kte, &   
                    ims, ime, jms, jme, kms, kme, &   
                    C_NULL_PTR, C_NULL_PTR, &         
                    C_NULL_PTR, C_NULL_PTR, &         
                    config_flags%damp_opt, REAL(config_flags%dampcoef, C_FLOAT), &      
                    config_flags%diff_6th_opt, REAL(config_flags%diff_6th_factor, C_FLOAT), &  
                    config_flags%diff_6th_slopeopt, REAL(config_flags%diff_6th_thresh, C_FLOAT), &  
                    config_flags%km_opt, REAL(config_flags%c_s, C_FLOAT), REAL(config_flags%c_k, C_FLOAT))  
               
               
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
      INCLUDE 'mpif.h'
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags  
      INTEGER, INTENT(IN) :: rk_step
      REAL, INTENT(IN) :: dt
      
      
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN), TARGET :: cqu, cqv, cqw
      
      
      INTEGER, INTENT(IN) :: ids, ide, jds, jde, kds, kde
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      
      
      INTEGER :: ij
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: nx, ny, nz
      INTEGER :: nx_u, ny_v, nz_w  
      INTEGER :: i_endu, j_endv     
      INTEGER :: i, j, nzeros       
      INTEGER :: sdirk3_nprocx, sdirk3_nprocy
      INTEGER :: sdirk3_mypx, sdirk3_mypy
      CHARACTER(LEN=256) :: message
      INTEGER(C_INT) :: step_outcome_rc
      INTEGER(C_INT) :: step_outcome_code
      INTEGER(C_INT) :: step_final_update_aborted
      INTEGER(C_INT) :: step_progress_ratio_valid
      REAL(C_FLOAT) :: step_progress_ratio

      
      TYPE(SDIRK3_IndexBounds), TARGET :: bounds
      TYPE(SDIRK3_Dimensions), TARGET :: dims
      TYPE(SDIRK3_ScalarParams), TARGET :: scalars
      TYPE(SDIRK3_BoundaryConfig), TARGET :: bdy_cfg

      
      
      
      INTERFACE
         SUBROUTINE sdirk3_set_mpi_comm(comm_handle, periodic_x, periodic_y) &
            BIND(C, name="sdirk3_set_mpi_comm")
            USE, INTRINSIC :: ISO_C_BINDING
            INTEGER(C_INT), VALUE :: comm_handle
            INTEGER(C_INT), VALUE :: periodic_x
            INTEGER(C_INT), VALUE :: periodic_y
         END SUBROUTINE sdirk3_set_mpi_comm
      END INTERFACE

      IF (.NOT. sdirk3_initialized) THEN
         CALL wrf_error_fatal3("<stdin>",842,&
"SDIRK3: Solvers not initialized")
      END IF


      IF (local_communicator /= MPI_UNDEFINED .AND. &
          local_communicator /= MPI_COMM_NULL) THEN
         CALL sdirk3_set_mpi_comm( &
            INT(local_communicator, C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_x), C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_y), C_INT))
      END IF

      
      
      
      sdirk3_nprocx = 1
      sdirk3_nprocy = 1
      sdirk3_mypx = 0
      sdirk3_mypy = 0
      IF (ntasks_x > 0 .AND. ntasks_y > 0) THEN
         sdirk3_nprocx = ntasks_x
         sdirk3_nprocy = ntasks_y
      END IF
      IF (mytask_x >= 0 .AND. mytask_y >= 0) THEN
         sdirk3_mypx = mytask_x
         sdirk3_mypy = mytask_y
      END IF

      DO ij = 1, grid%num_tiles
         IF (C_ASSOCIATED(tile_solvers(ij))) THEN
            CALL sdirk3_tile_set_mpi_process_info(tile_solvers(ij), &
                 INT(sdirk3_nprocx, C_INT), INT(sdirk3_nprocy, C_INT), &
                 INT(sdirk3_mypx, C_INT), INT(sdirk3_mypy, C_INT))
         END IF
      END DO

      
      

      
      !$OMP PARALLEL DO PRIVATE(ij,its,ite,jts,jte,kts,kte,nx,ny,nz, &
      !$OMP                     nx_u,ny_v,nz_w,i_endu,j_endv,i,j,nzeros,message)
      DO ij = 1, grid%num_tiles
         
         
         its = grid%i_start(ij)
         ite = grid%i_end(ij)
         jts = grid%j_start(ij)
         jte = grid%j_end(ij)
         kts = kds
         kte = kde - 1
         
         
         nx = ite - its + 1
         ny = jte - jts + 1
         
         
         
         nz = kte - kts + 1
         
         
         IF (ij == 1) THEN
            WRITE(message,'(A,L1,A,6I5)') 'SDIRK3 DEBUG: periodic_x=', &
                 config_flags%periodic_x, ', ids/ide/ims/ime/its/ite=', &
                 ids, ide, ims, ime, its, ite
            CALL wrf_message(message)
         END IF
         
         
         
         
         IF (config_flags%periodic_x) THEN
            i_endu = ite + 1
         ELSE IF (ite == ide) THEN
            i_endu = ite
         ELSE
            i_endu = ite + 1
         END IF
         
         
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

         
         
         
         nx_u = nx + 1
         ny_v = ny + 1
         nz_w = nz + 1

         
         IF (ij == 1) THEN
            WRITE(message,'(A,3I5,A,3I5)') 'SDIRK3 solve_em: Using nx/ny/nz=', &
                 nx, ny, nz, ', nx_u/ny_v/nz_w=', nx_u, ny_v, nz_w
            CALL wrf_message(message)
         END IF
         
         
         
         
         
         IF (ij > max_tiles) THEN
            WRITE(message,'(A,I0,A,I0)') 'SDIRK3 ERROR: Tile index ', ij, &
                 ' exceeds max_tiles ', max_tiles
            CALL wrf_error_fatal3("<stdin>",958,&
message)
         END IF
         
         IF (.NOT. solver_valid(ij)) THEN
            WRITE(message,'(A,I0)') 'SDIRK3 ERROR: Solver not initialized for tile ', ij
            CALL wrf_error_fatal3("<stdin>",964,&
message)
         END IF
         
         IF (.NOT. C_ASSOCIATED(tile_solvers(ij))) THEN
            WRITE(message,'(A,I0)') 'SDIRK3 ERROR: NULL solver pointer for tile ', ij
            CALL wrf_error_fatal3("<stdin>",970,&
message)
         END IF
         
         
         IF (solver_states(ij)%is_initialized) THEN
            solver_states(ij)%total_steps = solver_states(ij)%total_steps + 1
            solver_states(ij)%last_dt = dt
         END IF
         
         
         IF (MOD(grid%itimestep, 100) == 0 .AND. ij == 1) THEN
            WRITE(message,'(A,I0,A,I0,A,F10.3,A)') &
               'SDIRK3 Zero-Copy: Tile ', ij, ', timestep ', grid%itimestep, &
               ', dt=', dt, ' seconds'
            CALL wrf_debug(1, message)
         END IF
         
         CALL wrf_debug(2, 'SDIRK3 Zero-Copy: Calling actual solver')
         
         
         WRITE(message, '(A,5F12.4)') 'SDIRK3: First 5 msfty values from Fortran (tile start): ', &
              grid%msfty(its,jts), grid%msfty(its+1,jts), grid%msfty(its+2,jts), &
              grid%msfty(its+3,jts), grid%msfty(its+4,jts)
         CALL wrf_debug(0, message)
         
         
         nzeros = 0
         DO j = jts, jte
            DO i = its, ite
               IF (grid%msfty(i,j) == 0.0) nzeros = nzeros + 1
            END DO
         END DO
         WRITE(message, '(A,I8,A,I8)') 'SDIRK3: msfty has ', nzeros, ' zeros out of ', (ite-its+1)*(jte-jts+1)
         CALL wrf_debug(0, message)
         
         
         IF (ij == 1) THEN  
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 rdnw values from Fortran: ', &
               grid%rdnw(kds), grid%rdnw(kds+1), grid%rdnw(kds+2), grid%rdnw(kds+3), grid%rdnw(kds+4)
            CALL wrf_debug(1, message)
            
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 rdn values from Fortran: ', &
               grid%rdn(kds+1), grid%rdn(kds+2), grid%rdn(kds+3), grid%rdn(kds+4), grid%rdn(kds+5)
            CALL wrf_debug(1, message)
            
            
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 znw values from Fortran: ', &
               grid%znw(kms), grid%znw(kms+1), grid%znw(kms+2), grid%znw(kms+3), grid%znw(kms+4)
            CALL wrf_debug(1, message)
            
            
            WRITE(message,'(A,5(F12.4))') &
               'SDIRK3: First 5 dnw values from Fortran: ', &
               grid%dnw(kms), grid%dnw(kms+1), grid%dnw(kms+2), grid%dnw(kms+3), grid%dnw(kms+4)
            CALL wrf_debug(1, message)
         END IF
         
         
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
         scalars%dtbc = 0.0  
         scalars%dt = REAL(dt, C_FLOAT)

         bdy_cfg%spec_bdy_width = 0  
         bdy_cfg%spec_zone = 0
         bdy_cfg%relax_zone = 0
         bdy_cfg%padding = 0

         
         CALL sdirk3_tile_unified_step_zerocopy_v2( &
            tile_solvers(ij), &
            
            C_LOC(grid%u_2(ims,kms,jms)), &     
            C_LOC(grid%v_2(ims,kms,jms)), &     
            C_LOC(grid%w_2(ims,kms,jms)), &     
            C_LOC(grid%ph_2(ims,kms,jms)), &    
            C_LOC(grid%al(ims,kms,jms)), &      
            C_LOC(grid%mu_2(ims,jms)), &        
            C_LOC(grid%p(ims,kms,jms)), &       
            C_LOC(grid%t_2(ims,kms,jms)), &     
            
            C_NULL_PTR, 0, &                     
            
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, & 
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, & 
            C_NULL_PTR, C_NULL_PTR, &           
            
            C_LOC(grid%rdnw(kds)), &             
            C_LOC(grid%rdn(kds)), &              
            
            C_LOC(grid%msftx(ims,jms)), &       
            C_LOC(grid%msfty(ims,jms)), &       
            C_LOC(grid%msfux(ims,jms)), &       
            C_LOC(grid%msfuy(ims,jms)), &       
            C_LOC(grid%msfvx(ims,jms)), &       
            C_LOC(grid%msfvy(ims,jms)), &       
            
            C_LOC(grid%c1f(kms)), &             
            C_LOC(grid%c2f(kms)), &             
            C_LOC(grid%c1h(kms)), &             
            C_LOC(grid%c2h(kms)), &             
            
            C_LOC(grid%fnm(kds)), &             
            C_LOC(grid%fnp(kds)), &             
            
            C_LOC(grid%f(ims,jms)), &           
            C_LOC(grid%e(ims,jms)), &           
            C_LOC(grid%sina(ims,jms)), &        
            C_LOC(grid%cosa(ims,jms)), &        
            
            C_LOC(cqu(ims,kms,jms)), &          
            C_LOC(cqv(ims,kms,jms)), &          
            C_LOC(cqw(ims,kms,jms)), &          
            
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &  
            
            C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, C_NULL_PTR, &
            
            C_LOC(bounds), C_LOC(dims), C_LOC(scalars), C_LOC(bdy_cfg))
            
         CALL wrf_debug(2, 'SDIRK3 Zero-Copy: Returned from actual solver')

         step_outcome_rc = sdirk3_tile_solver_get_last_step_outcome_zerocopy( &
            tile_solvers(ij), step_outcome_code, step_final_update_aborted, &
            step_progress_ratio, step_progress_ratio_valid)
         IF (step_outcome_rc == 0_C_INT) THEN
            CALL wrf_error_fatal3("<stdin>",1141,&
"SDIRK3: Failed to query zerocopy step outcome")
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
            CALL wrf_error_fatal3("<stdin>",1153,&
TRIM(message))
         END IF
         

         
      END DO
      !$OMP END PARALLEL DO
      
      
      
      
      
      
      
   END SUBROUTINE advance_implicit
   
   
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
         
         IF (ALLOCATED(tile_solvers)) DEALLOCATE(tile_solvers)
         IF (ALLOCATED(solver_valid)) DEALLOCATE(solver_valid)
         IF (ALLOCATED(solver_states)) DEALLOCATE(solver_states)
         
         sdirk3_initialized = .FALSE.
         num_tiles_initialized = 0

         
         CALL wrf_sdirk3_reset_workers_started()
         max_tiles = 0
         
         CALL wrf_message("UNIFIED SDIRK3: Solvers finalized and memory deallocated")
      END IF
      
   END SUBROUTINE finalize_implicit_sdirk3
   
   
   FUNCTION get_sdirk3_dt_recommendation(grid, config_flags) RESULT(dt_new)
      IMPLICIT NONE
      
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      REAL :: dt_new
      
      
      INTEGER :: ij
      REAL :: dt_min, dt_tile
      REAL :: error_norm, safety_factor
      REAL :: dt_current
      
      IF (.NOT. sdirk3_initialized) THEN
         CALL wrf_error_fatal3("<stdin>",1215,&
"SDIRK3: Cannot get dt recommendation - solvers not initialized")
      END IF
      
      
      dt_current = grid%dt
      
      
      safety_factor = config_flags%sdirk3_safety_factor
      IF (safety_factor <= 0.0 .OR. safety_factor > 1.0) THEN
         safety_factor = 0.9  
      END IF
      
      
      IF (.NOT. config_flags%sdirk3_adaptive_dt) THEN
         dt_new = dt_current
         RETURN
      END IF
      
      
      dt_min = HUGE(1.0)
      
      !$OMP PARALLEL DO REDUCTION(MIN:dt_min) PRIVATE(ij,dt_tile)
      DO ij = 1, grid%num_tiles
         IF (C_ASSOCIATED(tile_solvers(ij))) THEN
            
            
            dt_tile = dt_current * safety_factor
            
            
            dt_tile = MAX(dt_current * config_flags%sdirk3_dt_min_factor, &
                         MIN(dt_tile, dt_current * config_flags%sdirk3_dt_max_factor))
            
            dt_min = MIN(dt_min, dt_tile)
         END IF
      END DO
      !$OMP END PARALLEL DO
      
      
      dt_new = wrf_dm_min_real(dt_min)
      
      
      IF (dt_new <= 0.0 .OR. dt_new >= HUGE(1.0)) THEN
         dt_new = dt_current
      END IF
      
      
      IF (MOD(grid%itimestep, 10) == 0) THEN
         WRITE(wrf_err_message,'(A,F10.3,A,F10.3)') &
            'SDIRK3 Adaptive dt: current=', dt_current, ', recommended=', dt_new
         CALL wrf_debug(2, wrf_err_message)
      END IF
      
   END FUNCTION get_sdirk3_dt_recommendation
   
   
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
         CALL wrf_error_fatal3("<stdin>",1313,&
wrf_err_message)
      END IF
      
      
      IF (ABS(max_val) > 1.0E10 .OR. ABS(min_val) > 1.0E10) THEN
         WRITE(wrf_err_message,'(A,A,A,ES12.5,A,ES12.5)') &
            'SDIRK3 WARNING: Extreme values in ', name, &
            ': min=', min_val, ', max=', max_val
         CALL wrf_debug(1, wrf_err_message)
      END IF
      
   END SUBROUTINE check_state_validity
   
   
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
         CALL wrf_error_fatal3("<stdin>",1368,&
wrf_err_message)
      END IF
      
      
      IF (ABS(max_val) > 1.0E10 .OR. ABS(min_val) > 1.0E10) THEN
         WRITE(wrf_err_message,'(A,A,A,ES12.5,A,ES12.5)') &
            'SDIRK3 WARNING: Extreme values in ', name, &
            ': min=', min_val, ', max=', max_val
         CALL wrf_debug(1, wrf_err_message)
      END IF
      
   END SUBROUTINE check_2d_validity
   
   
   
   
   
   
   
   
   
   
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
      
      
      INTEGER :: str_len
      INTEGER, PARAMETER :: default_max_krylov_iter = 100
      INTEGER, PARAMETER :: default_max_gmres_iter  = 100
      REAL, PARAMETER :: default_krylov_tol = 1.0E-3
      REAL, PARAMETER :: default_gmres_tol  = 1.0E-7
      
      
      config_name = "max_newton_iter"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_max_newton_iter)
      
      config_name = "newton_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_newton_tol)

      config_name = "newton_rtol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_newton_rtol)
      
      config_name = "gmres_restart"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_gmres_restart)

      
      
      
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

      
      WRITE(6,*) '[FORTRAN DEBUG] config_flags%sdirk3_precond_type = ', config_flags%sdirk3_precond_type
      config_name = "precond_type"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_precond_type)

      
      config_name = "precond_mu_coupling_damping"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_precond_mu_coupling_damping)
      
      
      config_name = "jvp_method"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_jvp_method)
      
      config_name = "jvp_epsilon"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_jvp_epsilon)
      
      
      config_name = "use_autograd"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_use_autograd))

      
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
      
      
      config_name = "debug_level"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_debug_level)

      
      
      
      
      enable_stage_halo_exchange = .FALSE.
      IF (ntasks_x > 0 .AND. ntasks_y > 0) THEN
         enable_stage_halo_exchange = (ntasks_x * ntasks_y > 1)
      ELSE
         
         
         
         enable_stage_halo_exchange = .TRUE.
      END IF
      config_name = "enable_stage_halo_exchange"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, enable_stage_halo_exchange))

      config_name = "stage_boundary_sync"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_stage_boundary_sync))

      config_name = "check_nan"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_check_nan))

      config_name = "check_conservation"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_check_conservation))

      
      config_name = "n_threads"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_n_threads)

      
      config_name = "scaling_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, 1)

      
      config_name = "lateral_bc_option"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_lateral_bc_option)
      
      config_name = "top_bc_option"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, config_flags%sdirk3_top_bc_option)
      
      config_name = "rayleigh_damp_coef"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_rayleigh_coef)
      
      config_name = "rayleigh_damp_depth"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_rayleigh_depth)
      
      
      config_name = "check_cfl"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_check_cfl))
      
      config_name = "cfl_target"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_cfl_target)

      
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

      
      config_name = "use_jvp_cache"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_use_jvp_cache))

      
      config_name = "nk_adaptive_tol"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_nk_adaptive_tol))

      
      config_name = "nk_line_search"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_nk_line_search))

      
      config_name = "nk_trust_region"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, MERGE(1, 0, config_flags%sdirk3_nk_trust_region))

      config_name = "nk_trust_radius"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_float(config_name, config_flags%sdirk3_nk_trust_radius)

      
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
   
   
   
   
   
   SUBROUTINE sdirk3_initialize_solvers(grid, config_flags)
      IMPLICIT NONE
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags

      
      
      
      
      
      IF (wrf_sdirk3_is_workers_started() /= 0) THEN
         CALL wrf_debug(1, "SDIRK3: Already initialized and frozen, skipping re-init")
         RETURN
      END IF

      
      CALL init_implicit_sdirk3(grid, config_flags)

      
      CALL sdirk3_configure_physics_coupling(config_flags)

      CALL wrf_message("SDIRK3: Solvers initialized for solve_em")

      
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

      
      
      CALL wrf_sdirk3_mark_workers_started()
      CALL wrf_debug(1, "SDIRK3: Config freeze point reached - workers started")

   END SUBROUTINE sdirk3_initialize_solvers
   
   
   
   SUBROUTINE sdirk3_finalize_solvers()
      IMPLICIT NONE
      
      
      CALL finalize_implicit_sdirk3()
      
   END SUBROUTINE sdirk3_finalize_solvers
   
   
   
   
   
   SUBROUTINE sdirk3_configure_physics_coupling(config_flags)
      IMPLICIT NONE
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      CHARACTER(LEN=256) :: config_name
      INTEGER :: physics_mode
      
      
      physics_mode = 0  
      
      
      
      
      
      
      
      
      config_name = "physics_mode"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_int(config_name, physics_mode)
      
      
      config_name = "frozen_physics_jvp"//C_NULL_CHAR
      CALL wrf_sdirk3_set_config_bool(config_name, 1)  
      
      CALL wrf_debug(1, "SDIRK3: Physics coupling configured (dynamics-only mode)")
      
   END SUBROUTINE sdirk3_configure_physics_coupling
   
   
   
   SUBROUTINE sdirk3_prepare_physics_coupling(grid, config_flags)
      IMPLICIT NONE
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      
      
      
      CALL wrf_debug(2, "SDIRK3: Physics coupling prepared (placeholder)")
      
   END SUBROUTINE sdirk3_prepare_physics_coupling
   
   
   
   SUBROUTINE sdirk3_apply_physics_tendencies(grid, config_flags, dt)
      IMPLICIT NONE
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      REAL, INTENT(IN) :: dt
      
      
      
      
      CALL wrf_debug(2, "SDIRK3: Physics tendencies applied (placeholder)")
      
   END SUBROUTINE sdirk3_apply_physics_tendencies

   
   
   

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

      checkpoints_used = INT(c_used)
      IF (checkpoints_used < 0) RETURN

      ierr = 0

   END SUBROUTINE sdirk3_run_4dvar_adjoint_loop
   
   
   
   
   
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
   
   
   
   
   
   
   
   
   
   
   SUBROUTINE sdirk3_halo_exchange(grid, config_flags)
      USE module_domain
      USE module_configure
      USE module_driver_constants
      USE module_machine
      USE module_state_description, ONLY : PARAM_FIRST_SCALAR, &
                                           num_moist, num_chem, num_tracer, num_scalar
      USE module_comm_dm_1, ONLY: halo_em_d2_5_sub
      
      
      IMPLICIT NONE
      TYPE(domain) , INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      
      INTEGER :: local_communicator, mytask, ntasks, ntasks_x, ntasks_y
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: ips, ipe, jps, jpe, kps, kpe
      
      REAL, DIMENSION(:,:,:,:), POINTER :: moist, chem, tracer, scalar
      
      
      local_communicator = grid%communicator
      CALL wrf_get_dm_communicator(local_communicator)
      CALL wrf_get_nproc(ntasks)
      CALL wrf_get_myproc(mytask)
      CALL wrf_get_nprocx(ntasks_x)
      CALL wrf_get_nprocy(ntasks_y)
      
      
      CALL get_ijk_from_grid(grid, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            ips, ipe, jps, jpe, kps, kpe)
      
      
      moist => grid%moist
      chem => grid%chem
      tracer => grid%tracer
      scalar => grid%scalar
      
      
      
      
      
      
      
      






CALL HALO_EM_D2_5_sub ( grid, &
  num_moist, &
  moist, &
  num_chem, &
  chem, &
  num_tracer, &
  tracer, &
  num_scalar, &
  scalar, &
  local_communicator, &
  mytask, ntasks, ntasks_x, ntasks_y, &
  ids, ide, jds, jde, kds, kde,       &
  ims, ime, jms, jme, kms, kme,       &
  ips, ipe, jps, jpe, kps, kpe )

      
      CALL sdirk3_notify_halo_fresh()

   END SUBROUTINE sdirk3_halo_exchange

   
   
   
   
   
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
            
            CALL sdirk3_tile_solver_clear_saved_trajectory_zerocopy(tile_solvers(ij))

            
            
            
            
            
            CALL sdirk3_tile_solver_reset_full(tile_solvers(ij))

            IF (solver_valid(ij)) THEN
               solver_states(ij)%total_steps = 0
               solver_states(ij)%failed_steps = 0
            END IF
         END IF
      END DO

      
      base_state_initialized = .FALSE.

      WRITE(message, '(A,I0,A)') "SDIRK3: Reset ", num_tiles_initialized, &
                                  " tile solvers for restart"
      CALL wrf_message(TRIM(message))

   END SUBROUTINE sdirk3_reset_on_restart

END MODULE module_implicit_sdirk3
