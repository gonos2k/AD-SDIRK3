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

   
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_ADVANCED    = 0_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_OK_SKIPPED     = 1_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_SOFT_NO_PROGRESS = 10_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_HARD_STAGE_ABORT = 20_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INPUT      = 100_C_INT
   INTEGER(C_INT), PARAMETER :: SDIRK3_STEP_OUTCOME_FATAL_INTERNAL   = 101_C_INT

   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   


   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   


   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   

   
   
   
   
   
   
   
   
   
   
   

   
   
   
   
   

   
   
   
   
   
   
   
   
   

   
   

   
   
   

   
   TYPE, BIND(C) :: SDIRK3_IndexBounds
      INTEGER(C_INT) :: its, ite  
      INTEGER(C_INT) :: jts, jte
      INTEGER(C_INT) :: kts, kte
      INTEGER(C_INT) :: ids, ide  
      INTEGER(C_INT) :: jds, jde
      INTEGER(C_INT) :: kds, kde
      INTEGER(C_INT) :: ims, ime  
      INTEGER(C_INT) :: jms, jme
      INTEGER(C_INT) :: kms, kme
   END TYPE SDIRK3_IndexBounds

   
   TYPE, BIND(C) :: SDIRK3_Dimensions
      INTEGER(C_INT) :: nx, ny, nz       
      INTEGER(C_INT) :: nx_u             
      INTEGER(C_INT) :: ny_v             
      INTEGER(C_INT) :: nz_w             
      INTEGER(C_INT) :: rk_step          
   END TYPE SDIRK3_Dimensions

   
   TYPE, BIND(C) :: SDIRK3_ScalarParams
      REAL(C_FLOAT) :: rdx, rdy          
      REAL(C_FLOAT) :: kdamp             
      REAL(C_FLOAT) :: khdif             
      REAL(C_FLOAT) :: kvdif             
      REAL(C_FLOAT) :: dtbc              
      REAL(C_FLOAT) :: dt                
   END TYPE SDIRK3_ScalarParams

   
   TYPE, BIND(C) :: SDIRK3_BoundaryConfig
      INTEGER(C_INT) :: spec_bdy_width   
      INTEGER(C_INT) :: spec_zone        
      INTEGER(C_INT) :: relax_zone       
      INTEGER(C_INT) :: padding          
   END TYPE SDIRK3_BoundaryConfig

   
   
   
   
   
   
   
   
   
   
   TYPE, BIND(C) :: SDIRK3_ScalarParams_FP64
      REAL(C_DOUBLE) :: rdx_fp64         
      REAL(C_DOUBLE) :: rdy_fp64         
      REAL(C_DOUBLE) :: dx_fp64          
      REAL(C_DOUBLE) :: dy_fp64          
   END TYPE SDIRK3_ScalarParams_FP64

   
   LOGICAL, SAVE :: sdirk3_initialized = .FALSE.
   TYPE(C_PTR), ALLOCATABLE, SAVE :: tile_solvers(:)
   LOGICAL, ALLOCATABLE, SAVE :: solver_valid(:)
   
   
   TYPE :: sdirk3_solver_state
      TYPE(C_PTR) :: solver_ptr = C_NULL_PTR
      LOGICAL :: is_initialized = .FALSE.
      INTEGER :: tile_id = -1
      REAL :: last_dt = -1.0
      INTEGER :: total_steps = 0
      INTEGER :: failed_steps = 0
   END TYPE sdirk3_solver_state
   
   TYPE(sdirk3_solver_state), DIMENSION(:), ALLOCATABLE, SAVE :: solver_states

   
   INTERFACE
      SUBROUTINE sdirk3_notify_halo_fresh() &
         BIND(C, name="sdirk3_notify_halo_fresh")
         USE, INTRINSIC :: ISO_C_BINDING
      END SUBROUTINE sdirk3_notify_halo_fresh
   END INTERFACE

CONTAINS
   
   
   
   
   
   SUBROUTINE advance_implicit_zerocopy(grid, config_flags, rk_step, dt)
      USE, INTRINSIC :: ISO_C_BINDING
      USE module_domain
      USE module_configure
      USE module_driver_constants
      USE module_state_description, ONLY : P_QV, num_moist  

      USE module_dm, ONLY : local_communicator

      IMPLICIT NONE

      INCLUDE 'mpif.h'

      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      INTEGER, INTENT(IN) :: rk_step
      REAL, INTENT(IN) :: dt

      
      INTEGER :: ij
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: ips, ipe, jps, jpe, kps, kpe
      INTEGER :: nx, ny, nz
      INTEGER :: nx_u, ny_v, nz_w  
      INTEGER :: i_endu, j_endv     
      CHARACTER(LEN=256) :: message
      INTEGER(C_INT) :: step_outcome_rc
      INTEGER(C_INT) :: step_outcome_code
      INTEGER(C_INT) :: step_final_update_aborted
      INTEGER(C_INT) :: step_progress_ratio_valid
      REAL(C_FLOAT) :: step_progress_ratio
      REAL, ALLOCATABLE, DIMENSION(:,:), TARGET :: mu_pert  

      
      TYPE(SDIRK3_IndexBounds), TARGET :: bounds
      TYPE(SDIRK3_Dimensions), TARGET :: dims
      TYPE(SDIRK3_ScalarParams), TARGET :: scalars
      TYPE(SDIRK3_BoundaryConfig), TARGET :: bdy_cfg
      
      
      INTERFACE
         
         

         
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
            
            TYPE(C_PTR), VALUE :: bounds_ptr      
            TYPE(C_PTR), VALUE :: dims_ptr        
            TYPE(C_PTR), VALUE :: scalars_ptr     
            TYPE(C_PTR), VALUE :: bdy_cfg_ptr     
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

         
         
         
         
         
         
         
         
         
         SUBROUTINE sdirk3_set_fp64_scalars(solver, fp64_ptr) &
            BIND(C, name="sdirk3_set_fp64_scalars")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver      
            TYPE(C_PTR), VALUE :: fp64_ptr    
         END SUBROUTINE sdirk3_set_fp64_scalars

         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         

         
         
         
         
         
         
         SUBROUTINE sdirk3_tile_solver_reset_state(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_state")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  
         END SUBROUTINE sdirk3_tile_solver_reset_state

         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         

         
         
         SUBROUTINE sdirk3_tile_solver_reset_full(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_full")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  
         END SUBROUTINE sdirk3_tile_solver_reset_full

         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         SUBROUTINE sdirk3_tile_solver_reset_full_parallel(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_full_parallel")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  
         END SUBROUTINE sdirk3_tile_solver_reset_full_parallel

         
         
         
         
         
         
         
         
         !   !$OMP PARALLEL
         
         !   !$OMP END PARALLEL
         
         
         
         SUBROUTINE sdirk3_tile_solver_reset_tls_parallel(solver_ptr) &
            BIND(C, name="sdirk3_tile_solver_reset_tls_parallel")
            USE, INTRINSIC :: ISO_C_BINDING
            TYPE(C_PTR), VALUE :: solver_ptr  
         END SUBROUTINE sdirk3_tile_solver_reset_tls_parallel

         
         
         
         
         
         SUBROUTINE sdirk3_reset_tls_debug_tracking() &
            BIND(C, name="sdirk3_reset_tls_debug_tracking")
         END SUBROUTINE sdirk3_reset_tls_debug_tracking

         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         

         SUBROUTINE wrf_sdirk3_set_config_uint64(name, value) &
            BIND(C, name="wrf_sdirk3_set_config_uint64")
            USE, INTRINSIC :: ISO_C_BINDING
            CHARACTER(KIND=C_CHAR), DIMENSION(*), INTENT(IN) :: name
            INTEGER(C_INT64_T), VALUE :: value  
         END SUBROUTINE wrf_sdirk3_set_config_uint64

         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         

         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
         
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
         CALL wrf_error_fatal3("<stdin>",616,&
"SDIRK3: Solvers not initialized")
      END IF
      
      
      CALL get_ijk_from_grid(grid, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            ips, ipe, jps, jpe, kps, kpe)
      

      
      


      IF (local_communicator /= MPI_UNDEFINED .AND. &
          local_communicator /= MPI_COMM_NULL) THEN
         CALL sdirk3_set_mpi_comm( &
            INT(local_communicator, C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_x), C_INT), &
            INT(MERGE(1, 0, config_flags%periodic_y), C_INT))
      END IF


      
      
      CALL sdirk3_halo_exchange(grid, config_flags)
      
      
      !$OMP PARALLEL DO PRIVATE(ij,its,ite,jts,jte,kts,kte,nx,ny,nz, &
      !$OMP                     nx_u,ny_v,nz_w,i_endu,j_endv,message)
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

         
         nz_w = nz + 1

         
         WRITE(message, '(A,I3,A,I3,A,I3,A,I3,A,I3)') &
            'DEBUG ny_v calc: jte=', jte, ' jde=', jde, ' ny=', ny, &
            ' -> ny_v=', ny_v, ' (expected ', ny, ' or ', ny+1, ')'
         CALL wrf_debug(0, message)
         
         
         IF (ALLOCATED(solver_valid)) THEN
            IF (.NOT. solver_valid(ij)) THEN
               WRITE(message,'(A,I0)') 'SDIRK3 ERROR: Solver not initialized for tile ', ij
               CALL wrf_error_fatal3("<stdin>",695,&
message)
            END IF
         END IF
         
         
         IF (ALLOCATED(solver_states)) THEN
            IF (solver_states(ij)%is_initialized) THEN
               solver_states(ij)%total_steps = solver_states(ij)%total_steps + 1
               solver_states(ij)%last_dt = dt
            END IF
         END IF
         
         
         IF (MOD(grid%itimestep, 100) == 0 .AND. ij == 1) THEN
            WRITE(message,'(A,I0,A,I0,A,F10.3,A)') &
               'SDIRK3 Zero-Copy: Tile ', ij, ', timestep ', grid%itimestep, &
               ', dt=', dt, ' seconds'
            CALL wrf_debug(1, message)
         END IF
         
         
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
            
            WRITE(message,'(A,Z16,A,Z16)') 'SDIRK3 FORTRAN: f_ptr = ', &
                                     TRANSFER(C_LOC(grid%f(its,jts)), 0_C_INTPTR_T), &
                                     ', e_ptr = ', &
                                     TRANSFER(C_LOC(grid%e(its,jts)), 0_C_INTPTR_T)
            CALL wrf_debug(0, message)
            
            
         END IF
         
         
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
         
         
         WRITE(message, '(A,6I5)') 'FORTRAN CALL: nx,ny,nz,nx_u,ny_v,nz_w = ', &
                                    nx, ny, nz, nx_u, ny_v, nz_w
         CALL wrf_debug(0, message)
         
         
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

         
         
         bounds%its = its ; bounds%ite = ite
         bounds%jts = jts ; bounds%jte = jte
         bounds%kts = kts ; bounds%kte = kte
         bounds%ids = ids ; bounds%ide = ide
         bounds%jds = jds ; bounds%jde = jde
         bounds%kds = kds ; bounds%kde = kde
         bounds%ims = ims ; bounds%ime = ime
         bounds%jms = jms ; bounds%jme = jme
         bounds%kms = kms ; bounds%kme = kme

         
         dims%nx = nx ; dims%ny = ny ; dims%nz = nz
         dims%nx_u = nx_u ; dims%ny_v = ny_v ; dims%nz_w = nz_w
         dims%rk_step = rk_step

         
         scalars%rdx = grid%rdx ; scalars%rdy = grid%rdy
         scalars%kdamp = REAL(config_flags%dampcoef, C_FLOAT)
         scalars%khdif = config_flags%khdif
         scalars%kvdif = config_flags%kvdif
         scalars%dtbc = grid%dtbc
         scalars%dt = REAL(dt, C_FLOAT)

         
         bdy_cfg%spec_bdy_width = config_flags%spec_bdy_width
         bdy_cfg%spec_zone = config_flags%spec_zone
         bdy_cfg%relax_zone = config_flags%relax_zone
         bdy_cfg%padding = 0  

         

         

         
         
         
         ALLOCATE(mu_pert(ims:ime, jms:jme))
         mu_pert = 0.0
         mu_pert(its:ite, jts:jte) = grid%mu_2(its:ite, jts:jte) - grid%mub(its:ite, jts:jte)


         
         


         CALL sdirk3_tile_unified_step_zerocopy_v2( &
            tile_solvers(ij), &
            
            
            
            
            C_LOC(grid%u_2(ims,kms,jms)), &     
            C_LOC(grid%v_2(ims,kms,jms)), &     
            C_LOC(grid%w_2(ims,kms,jms)), &     
            C_LOC(grid%ph_2(ims,kms,jms)), &    
            C_LOC(grid%al(ims,kms,jms)), &      
            C_LOC(mu_pert(ims,jms)), &          
            C_LOC(grid%p(ims,kms,jms)), &       
            C_LOC(grid%t_2(ims,kms,jms)), &     
            
            MERGE(C_LOC(grid%moist(ims,kms,jms,1)), C_NULL_PTR, num_moist > 0), &
            num_moist, &  
            
            C_NULL_PTR, & 
            C_NULL_PTR, & 
            C_NULL_PTR, & 
            C_NULL_PTR, & 
            C_NULL_PTR, & 
            C_NULL_PTR, & 
            C_NULL_PTR, C_NULL_PTR, &            
            
            C_LOC(grid%rdnw(kts)), &             
            C_LOC(grid%rdn(kts)), &              
            
            C_LOC(grid%msftx(ims,jms)), &       
            C_LOC(grid%msfty(ims,jms)), &       
            C_LOC(grid%msfux(ims,jms)), &       
            C_LOC(grid%msfuy(ims,jms)), &       
            C_LOC(grid%msfvx(ims,jms)), &       
            C_LOC(grid%msfvy(ims,jms)), &       
            
            C_LOC(grid%c1f(kts)), &             
            C_LOC(grid%c2f(kts)), &             
            C_LOC(grid%c1h(kts)), &             
            C_LOC(grid%c2h(kts)), &             
            
            C_LOC(grid%fnm(kts)), &             
            C_LOC(grid%fnp(kts)), &             
            
            C_LOC(grid%f(ims,jms)), &           
            C_LOC(grid%e(ims,jms)), &           
            C_LOC(grid%sina(ims,jms)), &        
            C_LOC(grid%cosa(ims,jms)), &        
            
            C_NULL_PTR, &     
            C_NULL_PTR, &     
            C_NULL_PTR, &     
            
            
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
            
            
            
            C_NULL_PTR, &  
            C_NULL_PTR, &  
            C_NULL_PTR, &  
            C_NULL_PTR, &  
            
            C_LOC(bounds), &      
            C_LOC(dims), &        
            C_LOC(scalars), &     
            C_LOC(bdy_cfg))       

         CALL wrf_debug(2, 'SDIRK3 Zero-Copy: Returned from solver')

         step_outcome_rc = sdirk3_tile_solver_get_last_step_outcome_zerocopy( &
            tile_solvers(ij), step_outcome_code, step_final_update_aborted, &
            step_progress_ratio, step_progress_ratio_valid)
         IF (step_outcome_rc == 0_C_INT) THEN
            CALL wrf_error_fatal3("<stdin>",911,&
'SDIRK3: Failed to query zerocopy step outcome')
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
            CALL wrf_error_fatal3("<stdin>",923,&
TRIM(message))
         END IF

         
         
         
         grid%mu_2(its:ite, jts:jte) = mu_pert(its:ite, jts:jte) + grid%mub(its:ite, jts:jte)

         
         
         
         
         
         
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

END MODULE module_implicit_sdirk3_zerocopy


