







MODULE module_wrf_implicit

   USE module_configure, ONLY : grid_config_rec_type
   USE module_state_description
   USE module_model_constants
   USE module_wrf_error
   USE module_domain, ONLY : domain, get_ijk_from_grid
   USE module_wrf_implicit_interface
   USE module_timing

   USE module_dm, ONLY : local_communicator, mytask, ntasks, ntasks_x, ntasks_y, &
                         wrf_dm_sum_real, wrf_dm_max_real, wrf_dm_min_real, &
                         wrf_dm_sum_integer

   
   USE ISO_C_BINDING
   
   IMPLICIT NONE
   
   PRIVATE
   
   
   PUBLIC :: wrf_implicit_solver_type
   PUBLIC :: wrf_implicit_solver_init
   PUBLIC :: wrf_implicit_acoustic_step
   PUBLIC :: wrf_implicit_gravity_step  
   PUBLIC :: wrf_implicit_full_step
   PUBLIC :: wrf_implicit_solver_finalize
   
   
   TYPE :: wrf_implicit_solver_type
      
      TYPE(C_PTR) :: acoustic_graph = C_NULL_PTR
      TYPE(C_PTR) :: gravity_graph = C_NULL_PTR
      TYPE(C_PTR) :: full_graph = C_NULL_PTR
      
      
      LOGICAL :: initialized = .FALSE.
      LOGICAL :: acoustic_implicit = .TRUE.
      LOGICAL :: gravity_implicit = .FALSE.
      REAL :: theta_implicit = 0.6  
      
      
      INTEGER :: max_newton_iter = 10
      REAL :: newton_tolerance = 1.0E-6
      INTEGER :: linear_solver_type = 2  
      INTEGER :: gmres_restart = 30
      REAL :: linear_solver_tol = 1.0E-8
      
      
      INTEGER :: num_threads = 1
      INTEGER :: tile_size = 32
      LOGICAL :: use_cache_blocking = .TRUE.
      
      
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: its, ite, jts, jte, kts, kte
      
      
      LOGICAL :: is_distributed = .FALSE.
      INTEGER :: mpi_rank = 0
      INTEGER :: mpi_size = 1
      
   END TYPE wrf_implicit_solver_type
   
CONTAINS

   
   SUBROUTINE wrf_implicit_solver_init(solver, grid, config_flags)
   
      TYPE(wrf_implicit_solver_type), INTENT(INOUT) :: solver
      TYPE(domain), TARGET, INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      
      INTEGER :: nx, ny, nz
      CHARACTER(LEN=256) :: message
      
      
      CALL get_ijk_from_grid(grid, &
                            solver%ids, solver%ide, solver%jds, solver%jde, solver%kds, solver%kde, &
                            solver%ims, solver%ime, solver%jms, solver%jme, solver%kms, solver%kme, &
                            solver%its, solver%ite, solver%jts, solver%jte, solver%kts, solver%kte)
      
      
      nx = solver%ite - solver%its + 1
      ny = solver%jte - solver%jts + 1
      nz = solver%kte - solver%kts + 1
      
      

      solver%is_distributed = (ntasks > 1)
      solver%mpi_rank = mytask
      solver%mpi_size = ntasks
      
      IF (solver%is_distributed) THEN
         WRITE(message,*) '[WRF Implicit] Running on ', ntasks, ' MPI tasks'
         CALL wrf_message(TRIM(message))
      END IF

      
      
      solver%acoustic_implicit = config_flags%implicit_acoustic
      solver%gravity_implicit = config_flags%implicit_gravity
      solver%theta_implicit = config_flags%theta_implicit
      solver%max_newton_iter = config_flags%implicit_solver_max_iter
      solver%newton_tolerance = config_flags%implicit_solver_tol
      solver%linear_solver_type = config_flags%implicit_solver_type
      solver%gmres_restart = config_flags%gmres_restart
      
      

      solver%num_threads = 1

      
      
      CALL wrf_implicit_init(nx, ny, nz, solver%num_threads, solver%tile_size)
      
      
      IF (solver%acoustic_implicit) THEN
         CALL wrf_implicit_create_acoustic_graph(solver%acoustic_graph, nx, ny, nz)
         CALL wrf_message('[WRF Implicit] Created acoustic wave graph')
      END IF
      
      IF (solver%gravity_implicit) THEN
         
         CALL wrf_message('[WRF Implicit] Gravity wave graph not yet implemented')
      END IF
      
      solver%initialized = .TRUE.
      
      WRITE(message,*) '[WRF Implicit] Initialized: theta=', solver%theta_implicit, &
                       ', threads=', solver%num_threads, ', tile_size=', solver%tile_size
      CALL wrf_message(TRIM(message))
      
   END SUBROUTINE wrf_implicit_solver_init

   
   SUBROUTINE wrf_implicit_acoustic_step(solver, grid, &
                                         ru_tend, rv_tend, rw_tend, &
                                         p_tend, mu_tend, &
                                         dt, config_flags &

                                         , mpi_comm &

                                         )
   
      TYPE(wrf_implicit_solver_type), INTENT(INOUT) :: solver
      TYPE(domain), INTENT(INOUT) :: grid
      REAL, DIMENSION(solver%ims:solver%ime, solver%kms:solver%kme, &
                      solver%jms:solver%jme), INTENT(INOUT) :: &
                      ru_tend, rv_tend, rw_tend, p_tend
      REAL, DIMENSION(solver%ims:solver%ime, solver%jms:solver%jme), &
           INTENT(INOUT) :: mu_tend
      REAL, INTENT(IN) :: dt
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags

      INTEGER, INTENT(IN) :: mpi_comm

      
      
      INTEGER :: nx, ny, nz
      REAL :: dx, dy, dz
      LOGICAL :: converged
      LOGICAL(C_BOOL) :: c_converged
      REAL :: t_start, t_end
      REAL, DIMENSION(solver%ims:solver%ime, solver%kms:solver%kme, &
                      solver%jms:solver%jme) :: sound_speed_sq
      CHARACTER(LEN=256) :: message

      
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme  
      INTEGER :: ips, ipe, jps, jpe, kps, kpe
      INTEGER :: local_conv, global_conv

      
      IF (.NOT. solver%initialized) THEN
         CALL wrf_error_fatal3("<stdin>",188,&
'[WRF Implicit] Solver not initialized')
      END IF
      
      CALL cpu_time(t_start)
      
      
      dx = grid%dx
      dy = grid%dy
      

      
      
      
      nx = solver%ite - solver%its + 1
      ny = solver%jte - solver%jts + 1
      nz = solver%kte - solver%kts + 1
      
      WRITE(message,*) '[WRF Implicit] nx=', nx, ', ny=', ny, ', nz=', nz
      CALL wrf_message(TRIM(message))
      WRITE(message,*) '[WRF Implicit] Memory dims: ims:ime=', solver%ims, ':', solver%ime, &
                       ', kms:kme=', solver%kms, ':', solver%kme, &
                       ', jms:jme=', solver%jms, ':', solver%jme
      CALL wrf_message(TRIM(message))
      WRITE(message,*) '[WRF Implicit] Tile dims: its:ite=', solver%its, ':', solver%ite, &
                       ', kts:kte=', solver%kts, ':', solver%kte, &
                       ', jts:jte=', solver%jts, ':', solver%jte
      CALL wrf_message(TRIM(message))
      
      
      CALL compute_sound_speed_squared(grid, sound_speed_sq, &
                                      solver%ims, solver%ime, &
                                      solver%kms, solver%kme, &
                                      solver%jms, solver%jme, &
                                      solver%its, solver%ite, &
                                      solver%kts, solver%kte, &
                                      solver%jts, solver%jte)
      

      
      IF (solver%is_distributed) THEN
         CALL wrf_debug(200, '[WRF Implicit] Exchanging halos')
         
         
         ids = solver%ids; ide = solver%ide
         jds = solver%jds; jde = solver%jde
         kds = solver%kds; kde = solver%kde
         ims = solver%ims; ime = solver%ime
         jms = solver%jms; jme = solver%jme
         kms = solver%kms; kme = solver%kme
         ips = solver%its; ipe = solver%ite  
         jps = solver%jts; jpe = solver%jte
         kps = solver%kts; kpe = solver%kte
         

      END IF

      
      
      
      CALL wrf_implicit_solve_acoustic( &
           solver%acoustic_graph, &
           grid%u_2, &
           grid%v_2, &
           grid%w_2, &
           grid%p, &
           ru_tend, &
           rv_tend, &
           rw_tend, &
           sound_speed_sq, &
           dx, dy, &
           grid%dnw, grid%rdnw, &  
           dt, solver%theta_implicit, &
           nx, ny, nz, &
           solver%max_newton_iter, solver%newton_tolerance, &
           c_converged)
      
      converged = c_converged
      

      
      IF (solver%is_distributed) THEN
         
         local_conv = 0
         IF (converged) local_conv = 1
         global_conv = wrf_dm_sum_integer(local_conv)
         converged = (global_conv == ntasks)
      END IF

      
      IF (.NOT. converged) THEN
         CALL wrf_message('WARNING: [WRF Implicit] Acoustic solver did not converge')
      END IF
      
      CALL cpu_time(t_end)
      
      IF (MOD(grid%itimestep, 100) == 0) THEN
         WRITE(message,*) '[WRF Implicit] Acoustic step time: ', &
                          t_end - t_start, ' seconds'
         CALL wrf_debug(100, TRIM(message))
      END IF
      
   END SUBROUTINE wrf_implicit_acoustic_step

   
   SUBROUTINE wrf_implicit_gravity_step(solver, grid, &
                                        theta_tend, w_tend, p_tend, &
                                        dt, config_flags)
   
      TYPE(wrf_implicit_solver_type), INTENT(INOUT) :: solver
      TYPE(domain), INTENT(INOUT) :: grid
      REAL, DIMENSION(solver%ims:solver%ime, solver%kms:solver%kme, &
                      solver%jms:solver%jme), INTENT(INOUT) :: &
                      theta_tend, w_tend, p_tend
      REAL, INTENT(IN) :: dt
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      
      CALL wrf_message('[WRF Implicit] Gravity wave solver not yet implemented')
      
   END SUBROUTINE wrf_implicit_gravity_step

   
   SUBROUTINE wrf_implicit_full_step(solver, grid, dt, config_flags)
   
      TYPE(wrf_implicit_solver_type), INTENT(INOUT) :: solver
      TYPE(domain), INTENT(INOUT) :: grid
      REAL, INTENT(IN) :: dt
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      
      CALL wrf_message('[WRF Implicit] Full implicit solver not yet implemented')
      
   END SUBROUTINE wrf_implicit_full_step

   
   SUBROUTINE wrf_implicit_solver_finalize(solver)
   
      TYPE(wrf_implicit_solver_type), INTENT(INOUT) :: solver
      
      IF (solver%initialized) THEN
         
         IF (C_ASSOCIATED(solver%acoustic_graph)) THEN
            CALL wrf_implicit_delete_graph(solver%acoustic_graph)
            solver%acoustic_graph = C_NULL_PTR
         END IF
         
         IF (C_ASSOCIATED(solver%gravity_graph)) THEN
            CALL wrf_implicit_delete_graph(solver%gravity_graph)
            solver%gravity_graph = C_NULL_PTR
         END IF
         
         IF (C_ASSOCIATED(solver%full_graph)) THEN
            CALL wrf_implicit_delete_graph(solver%full_graph)
            solver%full_graph = C_NULL_PTR
         END IF
         
         
         CALL wrf_implicit_finalize()
         
         solver%initialized = .FALSE.
         
         CALL wrf_message('[WRF Implicit] Solver finalized')
      END IF
      
   END SUBROUTINE wrf_implicit_solver_finalize

   
   SUBROUTINE compute_sound_speed_squared(grid, c2, &
                                          ims, ime, kms, kme, jms, jme, &
                                          its, ite, kts, kte, jts, jte)
   
      TYPE(domain), INTENT(IN) :: grid
      REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(OUT) :: c2
      INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
      INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
      
      INTEGER :: i, j, k
      REAL :: gamma = 1.4  
      
      
      
      
      DO j = jts, jte
         DO k = kts, kte
            DO i = its, ite
               
               c2(i,k,j) = 340.0 * 340.0  
               
               
               
            END DO
         END DO
      END DO
      
   END SUBROUTINE compute_sound_speed_squared

END MODULE module_wrf_implicit

