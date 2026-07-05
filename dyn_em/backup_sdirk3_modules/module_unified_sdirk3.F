!------------------------------------------------------------------------
! module_unified_sdirk3.F
!
! Purpose: Unified SDIRK-3 solver for WRF - NO MODE SPLITTING
! L-stable SDIRK-3 handles all dynamics together
!
! Author: WRF-SDIRK3 Development Team
! Date: 2025-06-21
!------------------------------------------------------------------------

MODULE module_unified_sdirk3
   USE module_configure
   USE module_state_description
   USE module_model_constants
   USE module_domain, ONLY : domain, get_ijk_from_grid
   USE module_tiles, ONLY : set_tiles
   USE module_wrf_error
   USE module_dm, ONLY : wrf_dm_sum_real, wrf_dm_maxval
   
   USE ISO_C_BINDING
   
   IMPLICIT NONE
   PRIVATE
   
   ! Public procedures
   PUBLIC :: init_unified_sdirk3
   PUBLIC :: advance_unified_sdirk3
   PUBLIC :: finalize_unified_sdirk3
   
   ! C interface declarations for unified solver
   INTERFACE
      ! Create unified solver instance
      TYPE(C_PTR) FUNCTION sdirk3_unified_solver_create( &
         nx, ny, nz, dx, dy, dz, grid_coeffs, tile_id) BIND(C)
         USE ISO_C_BINDING
         INTEGER(C_INT), VALUE :: nx, ny, nz
         REAL(C_DOUBLE), VALUE :: dx, dy
         REAL(C_DOUBLE), DIMENSION(*) :: dz, grid_coeffs
         INTEGER(C_INT), VALUE :: tile_id
      END FUNCTION sdirk3_unified_solver_create
      
      ! Destroy unified solver instance
      SUBROUTINE sdirk3_unified_solver_destroy(solver_handle) BIND(C)
         USE ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_handle
      END SUBROUTINE sdirk3_unified_solver_destroy
      
      ! Advance solution with unified SDIRK-3
      SUBROUTINE sdirk3_unified_advance( &
         solver_handle, &
         u, v, w, theta, ph, mu, qv, qc, qr, &
         dt, nx, ny, nz) BIND(C)
         USE ISO_C_BINDING
         TYPE(C_PTR), VALUE :: solver_handle
         REAL(C_DOUBLE), DIMENSION(*) :: u, v, w, theta, ph, mu
         REAL(C_DOUBLE), DIMENSION(*) :: qv, qc, qr
         REAL(C_DOUBLE), VALUE :: dt
         INTEGER(C_INT), VALUE :: nx, ny, nz
      END SUBROUTINE sdirk3_unified_advance
   END INTERFACE
   
   ! Module variables - one solver per tile
   INTEGER, PARAMETER :: MAX_TILES = 1024
   TYPE(C_PTR), DIMENSION(MAX_TILES) :: tile_solvers
   LOGICAL :: sdirk3_initialized = .FALSE.
   INTEGER :: num_tiles_initialized = 0
   
CONTAINS

   !-----------------------------------------------------------------------
   SUBROUTINE init_unified_sdirk3(grid, config_flags)
      IMPLICIT NONE
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      INTEGER :: ij  ! tile index
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ips, ipe, jps, jpe, kps, kpe
      INTEGER :: nx, ny, nz
      REAL(KIND=8) :: dx, dy
      REAL(KIND=8), ALLOCATABLE :: dz(:), grid_coeffs(:)
      
      ! Get dimensions
      CALL get_ijk_from_grid(grid, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            ips, ipe, jps, jpe, kps, kpe)
      
      ! Grid spacing
      dx = config_flags%dx
      dy = config_flags%dy
      
      ! Allocate vertical grid arrays
      ALLOCATE(dz(kms:kme))
      ALLOCATE(grid_coeffs(100))  ! Placeholder for various grid coefficients
      
      ! Initialize solver for each tile
      WRITE(wrf_err_message,'(A)') 'UNIFIED SDIRK3: Initializing L-stable solver (NO mode splitting)'
      CALL wrf_message(wrf_err_message)
      
      DO ij = 1, grid%num_tiles
         ! Get tile bounds
         its = grid%i_start(ij)
         ite = grid%i_end(ij)
         jts = grid%j_start(ij)
         jte = grid%j_end(ij)
         kts = kms
         kte = kme-1
         
         ! Tile dimensions
         nx = ite - its + 1
         ny = jte - jts + 1
         nz = kte - kts + 1
         
         ! Create unified solver instance for this tile
         tile_solvers(ij) = sdirk3_unified_solver_create( &
            nx, ny, nz, dx, dy, dz, grid_coeffs, ij)
         
         IF (.NOT. C_ASSOCIATED(tile_solvers(ij))) THEN
            WRITE(wrf_err_message,'(A,I0)') 'UNIFIED SDIRK3: Failed to create solver for tile ', ij
            CALL wrf_error_fatal(wrf_err_message)
         END IF
      END DO
      
      num_tiles_initialized = grid%num_tiles
      sdirk3_initialized = .TRUE.
      
      WRITE(wrf_err_message,'(A,I0,A)') 'UNIFIED SDIRK3: Initialized ', &
                                        num_tiles_initialized, ' tile solvers'
      CALL wrf_message(wrf_err_message)
      
      WRITE(wrf_err_message,'(A)') 'UNIFIED SDIRK3: All dynamics (acoustic/gravity/advection) solved together'
      CALL wrf_message(wrf_err_message)
      
      WRITE(wrf_err_message,'(A)') 'UNIFIED SDIRK3: L-stability eliminates CFL restriction on sound waves'
      CALL wrf_message(wrf_err_message)
      
      DEALLOCATE(dz, grid_coeffs)
      
   END SUBROUTINE init_unified_sdirk3
   
   !-----------------------------------------------------------------------
   SUBROUTINE advance_unified_sdirk3(grid, config_flags, dt, rk_step)
      IMPLICIT NONE
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      REAL, INTENT(IN) :: dt
      INTEGER, INTENT(IN) :: rk_step
      
      ! Local variables
      INTEGER :: ij  ! tile index
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: nx, ny, nz
      INTEGER :: i, j, k
      
      ! Tile arrays for state variables
      REAL(KIND=8), ALLOCATABLE :: u_tile(:,:,:), v_tile(:,:,:), w_tile(:,:,:)
      REAL(KIND=8), ALLOCATABLE :: theta_tile(:,:,:), ph_tile(:,:,:)
      REAL(KIND=8), ALLOCATABLE :: mu_tile(:,:)
      REAL(KIND=8), ALLOCATABLE :: qv_tile(:,:,:), qc_tile(:,:,:), qr_tile(:,:,:)
      
      IF (.NOT. sdirk3_initialized) THEN
         CALL wrf_error_fatal("UNIFIED SDIRK3: Solvers not initialized")
      END IF
      
      ! Only process on RK step 1 (unified approach does all 3 SDIRK stages internally)
      IF (rk_step /= 1) THEN
         RETURN
      END IF
      
      WRITE(wrf_err_message,'(A,F8.2,A)') 'UNIFIED SDIRK3: Advancing with dt=', dt, ' (no sub-stepping)'
      CALL wrf_message(wrf_err_message)
      
      ! Get memory dimensions
      ims = grid%sm31 ; ime = grid%em31
      jms = grid%sm32 ; jme = grid%em32
      kms = grid%sm33 ; kme = grid%em33
      
      !$OMP PARALLEL DO   &
      !$OMP PRIVATE(ij, its, ite, jts, jte, kts, kte, nx, ny, nz, i, j, k) &
      !$OMP PRIVATE(u_tile, v_tile, w_tile, theta_tile, ph_tile, mu_tile) &
      !$OMP PRIVATE(qv_tile, qc_tile, qr_tile)
      
      DO ij = 1, grid%num_tiles
         ! Get tile bounds
         its = grid%i_start(ij)
         ite = grid%i_end(ij)
         jts = grid%j_start(ij)
         jte = grid%j_end(ij)
         kts = kms
         kte = kme-1
         
         ! Tile dimensions
         nx = ite - its + 1
         ny = jte - jts + 1
         nz = kte - kts + 1
         
         ! Allocate tile arrays
         ALLOCATE(u_tile(nx,ny,nz))
         ALLOCATE(v_tile(nx,ny,nz))
         ALLOCATE(w_tile(nx,ny,nz))
         ALLOCATE(theta_tile(nx,ny,nz))
         ALLOCATE(ph_tile(nx,ny,nz))
         ALLOCATE(mu_tile(nx,ny))
         ALLOCATE(qv_tile(nx,ny,nz))
         ALLOCATE(qc_tile(nx,ny,nz))
         ALLOCATE(qr_tile(nx,ny,nz))
         
         ! Copy data to tile arrays (with index transformation)
         DO j = jts, jte
            DO k = kts, kte
               DO i = its, ite
                  u_tile(i-its+1, j-jts+1, k-kts+1) = grid%u_2(i,k,j)
                  v_tile(i-its+1, j-jts+1, k-kts+1) = grid%v_2(i,k,j)
                  w_tile(i-its+1, j-jts+1, k-kts+1) = grid%w_2(i,k,j)
                  theta_tile(i-its+1, j-jts+1, k-kts+1) = grid%t_2(i,k,j)
                  ph_tile(i-its+1, j-jts+1, k-kts+1) = grid%ph_2(i,k,j)
                  ! Add moisture if available
                  IF (config_flags%mp_physics > 0) THEN
                     qv_tile(i-its+1, j-jts+1, k-kts+1) = grid%moist(i,k,j,P_QV)
                     qc_tile(i-its+1, j-jts+1, k-kts+1) = grid%moist(i,k,j,P_QC)
                     qr_tile(i-its+1, j-jts+1, k-kts+1) = grid%moist(i,k,j,P_QR)
                  ELSE
                     qv_tile = 0.0_8
                     qc_tile = 0.0_8
                     qr_tile = 0.0_8
                  END IF
               END DO
            END DO
         END DO
         
         DO j = jts, jte
            DO i = its, ite
               mu_tile(i-its+1, j-jts+1) = grid%mu_2(i,j)
            END DO
         END DO
         
         ! Call unified SDIRK-3 solver
         ! This does ALL 3 SDIRK stages internally with Newton-Krylov
         ! No mode splitting - L-stability handles everything!
         CALL sdirk3_unified_advance( &
            tile_solvers(ij), &
            u_tile, v_tile, w_tile, &
            theta_tile, ph_tile, mu_tile, &
            qv_tile, qc_tile, qr_tile, &
            REAL(dt, C_DOUBLE), &
            nx, ny, nz)
         
         ! Copy updated state back to grid
         DO j = jts, jte
            DO k = kts, kte
               DO i = its, ite
                  grid%u_2(i,k,j) = u_tile(i-its+1, j-jts+1, k-kts+1)
                  grid%v_2(i,k,j) = v_tile(i-its+1, j-jts+1, k-kts+1)
                  grid%w_2(i,k,j) = w_tile(i-its+1, j-jts+1, k-kts+1)
                  grid%t_2(i,k,j) = theta_tile(i-its+1, j-jts+1, k-kts+1)
                  grid%ph_2(i,k,j) = ph_tile(i-its+1, j-jts+1, k-kts+1)
                  IF (config_flags%mp_physics > 0) THEN
                     grid%moist(i,k,j,P_QV) = qv_tile(i-its+1, j-jts+1, k-kts+1)
                     grid%moist(i,k,j,P_QC) = qc_tile(i-its+1, j-jts+1, k-kts+1)
                     grid%moist(i,k,j,P_QR) = qr_tile(i-its+1, j-jts+1, k-kts+1)
                  END IF
               END DO
            END DO
         END DO
         
         DO j = jts, jte
            DO i = its, ite
               grid%mu_2(i,j) = mu_tile(i-its+1, j-jts+1)
            END DO
         END DO
         
         ! Deallocate
         DEALLOCATE(u_tile, v_tile, w_tile)
         DEALLOCATE(theta_tile, ph_tile, mu_tile)
         DEALLOCATE(qv_tile, qc_tile, qr_tile)
         
      END DO
      !$OMP END PARALLEL DO
      
   END SUBROUTINE advance_unified_sdirk3
   
   !-----------------------------------------------------------------------
   SUBROUTINE finalize_unified_sdirk3()
      IMPLICIT NONE
      INTEGER :: ij
      
      IF (sdirk3_initialized) THEN
         DO ij = 1, num_tiles_initialized
            IF (C_ASSOCIATED(tile_solvers(ij))) THEN
               CALL sdirk3_unified_solver_destroy(tile_solvers(ij))
               tile_solvers(ij) = C_NULL_PTR
            END IF
         END DO
         
         sdirk3_initialized = .FALSE.
         num_tiles_initialized = 0
         
         CALL wrf_message("UNIFIED SDIRK3: Solvers finalized")
      END IF
      
   END SUBROUTINE finalize_unified_sdirk3

END MODULE module_unified_sdirk3