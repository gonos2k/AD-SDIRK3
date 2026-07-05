!----------------------------------------------------------------
! SDIRK3 Integration Example for solve_em.F
! This code shows how to integrate SDIRK3 implicit solver with WRF
!----------------------------------------------------------------

SUBROUTINE sdirk3_time_step(grid, config_flags, dt_in)
   USE module_domain
   USE module_configure
   USE module_sdirk3_interface
   USE module_sdirk3_grid_map
   USE module_sdirk3_coefficients_data  ! Contains SDIRK3 coefficients
   
   IMPLICIT NONE
   
   TYPE(domain), INTENT(INOUT) :: grid
   TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
   REAL, INTENT(IN) :: dt_in
   
   ! Local variables
   INTEGER :: ids, ide, jds, jde, kds, kde
   INTEGER :: ims, ime, jms, jme, kms, kme
   INTEGER :: its, ite, jts, jte, kts, kte
   INTEGER :: i, j, k, stage, ierr
   INTEGER :: nx, ny, nz
   REAL :: dt
   
   ! SDIRK3 stage arrays
   REAL, ALLOCATABLE, DIMENSION(:,:,:,:) :: k1, k2, k3     ! Stage derivatives
   REAL, ALLOCATABLE, DIMENSION(:,:,:,:) :: U_n, U_np1    ! State at n and n+1
   REAL, ALLOCATABLE, DIMENSION(:,:,:,:,:) :: K_stages    ! All stages
   
   ! Get domain dimensions
   CALL get_ijk_from_grid(grid, &
                         ids, ide, jds, jde, kds, kde, &
                         ims, ime, jms, jme, kms, kme, &
                         its, ite, jts, jte, kts, kte)
   
   nx = ide - ids
   ny = jde - jds
   nz = kde - kds
   dt = dt_in
   
   ! Calculate packed array size considering staggering
   ! u: (nx+1)*ny*nz, v: nx*(ny+1)*nz, w: nx*ny*(nz+1)
   ! t: nx*ny*nz, ph: nx*ny*(nz+1), mu: nx*ny
   INTEGER :: packed_size
   packed_size = (nx+1)*ny*nz + nx*(ny+1)*nz + nx*ny*(nz+1) + &
                 nx*ny*nz + nx*ny*(nz+1) + nx*ny
   
   ! Allocate packed arrays for C++ interface
   REAL, ALLOCATABLE :: packed_u_n(:), packed_u_np1(:)
   REAL, ALLOCATABLE :: packed_k1(:), packed_k2(:), packed_k3(:)
   ALLOCATE(packed_u_n(packed_size))
   ALLOCATE(packed_u_np1(packed_size))
   ALLOCATE(packed_k1(packed_size))
   ALLOCATE(packed_k2(packed_size))
   ALLOCATE(packed_k3(packed_size))
   
   !----------------------------------------------------------------
   ! Step 1: Pack current state into unified array
   !----------------------------------------------------------------
   DO j = jms, jme
      DO k = kms, kme
         DO i = ims, ime
            U_n(i,k,j,1) = grid%u_2(i,k,j)
            U_n(i,k,j,2) = grid%v_2(i,k,j)
            U_n(i,k,j,3) = grid%w_2(i,k,j)
            U_n(i,k,j,4) = grid%t_2(i,k,j)
            U_n(i,k,j,5) = grid%ph_2(i,k,j)
         END DO
      END DO
   END DO
   
   ! 2D variable (mu)
   DO j = jms, jme
      DO i = ims, ime
         U_n(i,1,j,6) = grid%mu_2(i,j)
      END DO
   END DO
   
   !----------------------------------------------------------------
   ! Step 2: SDIRK3 Stage 1
   !----------------------------------------------------------------
   CALL sdirk3_solver_step(k1, U_n, stage_num=1, dt=dt, &
                          nx=nx, ny=ny, nz=nz, ierr=ierr)
   IF (ierr /= 0) THEN
      CALL wrf_error_fatal("SDIRK3: Stage 1 failed")
   END IF
   
   ! Store stage 1
   K_stages(1,:,:,:,:) = k1(:,:,:,:)
   
   !----------------------------------------------------------------
   ! Step 3: SDIRK3 Stage 2
   !----------------------------------------------------------------
   CALL sdirk3_solver_step(k2, U_n, K_stages, stage_num=2, dt=dt, &
                          nx=nx, ny=ny, nz=nz, ierr=ierr)
   IF (ierr /= 0) THEN
      CALL wrf_error_fatal("SDIRK3: Stage 2 failed")
   END IF
   
   ! Store stage 2
   K_stages(2,:,:,:,:) = k2(:,:,:,:)
   
   !----------------------------------------------------------------
   ! Step 4: SDIRK3 Stage 3
   !----------------------------------------------------------------
   CALL sdirk3_solver_step(k3, U_n, K_stages, stage_num=3, dt=dt, &
                          nx=nx, ny=ny, nz=nz, ierr=ierr)
   IF (ierr /= 0) THEN
      CALL wrf_error_fatal("SDIRK3: Stage 3 failed")
   END IF
   
   !----------------------------------------------------------------
   ! Step 5: Final update using SDIRK3 weights
   !----------------------------------------------------------------
   ! U_{n+1} = U_n + dt * (b1*k1 + b2*k2 + b3*k3)
   ! SDIRK3 weights: b = [1/6, 1/6, 2/3]
   
   DO j = jms, jme
      DO k = kms, kme
         DO i = ims, ime
            U_np1(i,k,j,:) = U_n(i,k,j,:) + dt * ( &
                             (1.0/6.0) * k1(i,k,j,:) + &
                             (1.0/6.0) * k2(i,k,j,:) + &
                             (2.0/3.0) * k3(i,k,j,:) )
         END DO
      END DO
   END DO
   
   !----------------------------------------------------------------
   ! Step 6: Unpack solution back to WRF arrays
   !----------------------------------------------------------------
   DO j = jms, jme
      DO k = kms, kme
         DO i = ims, ime
            grid%u_2(i,k,j) = U_np1(i,k,j,1)
            grid%v_2(i,k,j) = U_np1(i,k,j,2)
            grid%w_2(i,k,j) = U_np1(i,k,j,3)
            grid%t_2(i,k,j) = U_np1(i,k,j,4)
            grid%ph_2(i,k,j) = U_np1(i,k,j,5)
         END DO
      END DO
   END DO
   
   ! 2D variable
   DO j = jms, jme
      DO i = ims, ime
         grid%mu_2(i,j) = U_np1(i,1,j,6)
      END DO
   END DO
   
   !----------------------------------------------------------------
   ! Step 7: Get solver statistics
   !----------------------------------------------------------------
   IF (config_flags%sdirk3_print_stats) THEN
      INTEGER :: newton_iters, gmres_iters
      REAL :: final_residual
      
      CALL sdirk3_get_solver_stats(newton_iters, gmres_iters, final_residual)
      
      WRITE(wrf_err_message,*) 'SDIRK3 Stats: Newton=', newton_iters, &
                              ' GMRES=', gmres_iters, &
                              ' Residual=', final_residual
      CALL wrf_debug(100, wrf_err_message)
   END IF
   
   ! Deallocate arrays
   DEALLOCATE(U_n, U_np1, k1, k2, k3, K_stages)
   
END SUBROUTINE sdirk3_time_step


!----------------------------------------------------------------
! Tile-based SDIRK3 integration (for OpenMP parallelization)
!----------------------------------------------------------------
SUBROUTINE sdirk3_tile_step(grid, config_flags, &
                           its, ite, jts, jte, kts, kte, &
                           tile_id, dt_in)
   USE module_domain
   USE module_configure
   USE module_sdirk3_interface
   USE iso_c_binding
   
   IMPLICIT NONE
   
   TYPE(domain), INTENT(INOUT) :: grid
   TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
   INTEGER, INTENT(IN) :: its, ite, jts, jte, kts, kte
   INTEGER, INTENT(IN) :: tile_id
   REAL, INTENT(IN) :: dt_in
   
   ! Local variables
   TYPE(c_ptr) :: tile_solver
   INTEGER :: nx, ny, nz
   REAL :: dx, dy, dt
   
   ! Get grid parameters
   nx = grid%ed31 - grid%sd31 + 1
   ny = grid%ed33 - grid%sd33 + 1
   nz = grid%ed32 - grid%sd32 + 1
   dx = config_flags%dx
   dy = config_flags%dy
   dt = dt_in
   
   ! Create tile solver with map factors
   tile_solver = sdirk3_tile_create( &
                   nx, ny, nz, dx, dy, dt, &
                   its, ite, jts, jte, kts, kte, &
                   grid%msftx(its:ite,jts:jte), &
                   grid%msfty(its:ite,jts:jte), &
                   grid%msfux(its:ite,jts:jte), &
                   grid%msfuy(its:ite,jts:jte), &
                   grid%msfvx(its:ite,jts:jte), &
                   grid%msfvy(its:ite,jts:jte), &
                   unified_mode=1, tile_id=tile_id)
   
   ! Perform SDIRK3 time step on this tile
   ! ... (implementation details) ...
   
   ! Destroy tile solver
   CALL sdirk3_tile_destroy(tile_solver)
   
END SUBROUTINE sdirk3_tile_step