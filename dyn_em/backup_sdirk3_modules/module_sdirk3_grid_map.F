MODULE module_sdirk3_grid_map
   !
   ! Module: module_sdirk3_grid_map
   ! Purpose: Interface between WRF grid mapping factors and SDIRK3 implicit solver
   ! Author: WRF-SDIRK3 Development Team
   !
   USE module_configure
   USE module_state_description
   USE module_domain_type, ONLY : domain
   USE module_domain, ONLY : get_ijk_from_grid2

   IMPLICIT NONE

   PRIVATE

   PUBLIC :: sdirk3_init_grid_mapping
   PUBLIC :: sdirk3_get_grid_params

   ! Interface to C++ functions
   INTERFACE
      FUNCTION wrf_sdirk3_init_solver_with_maps(nx, ny, nz, dx, dy, dt, &
                                                msftx, msfty, msfux, msfuy, msfvx, msfvy, &
                                                unified_mode) BIND(C, name="wrf_sdirk3_init_solver_with_maps")
         USE iso_c_binding
         INTEGER(c_int), VALUE :: nx, ny, nz
         REAL(c_float), VALUE :: dx, dy, dt
         REAL(c_float), DIMENSION(*) :: msftx, msfty, msfux, msfuy, msfvx, msfvy
         INTEGER(c_int), VALUE :: unified_mode
         INTEGER(c_int) :: wrf_sdirk3_init_solver_with_maps
      END FUNCTION wrf_sdirk3_init_solver_with_maps
      
      FUNCTION wrf_sdirk3_init_solver_with_grid(nx, ny, nz, dx, dy, dt, &
                                                dnw, rdnw, &
                                                msftx, msfty, msfux, msfuy, msfvx, msfvy, &
                                                unified_mode) BIND(C, name="wrf_sdirk3_init_solver_with_grid")
         USE iso_c_binding
         INTEGER(c_int), VALUE :: nx, ny, nz
         REAL(c_float), VALUE :: dx, dy, dt
         REAL(c_float), DIMENSION(*) :: dnw, rdnw
         REAL(c_float), DIMENSION(*) :: msftx, msfty, msfux, msfuy, msfvx, msfvy
         INTEGER(c_int), VALUE :: unified_mode
         INTEGER(c_int) :: wrf_sdirk3_init_solver_with_grid
      END FUNCTION wrf_sdirk3_init_solver_with_grid
   END INTERFACE

CONTAINS

   !-------------------------------------------------------------------------
   ! sdirk3_init_grid_mapping
   ! Initialize SDIRK3 solver with WRF grid parameters and map scale factors
   !-------------------------------------------------------------------------
   SUBROUTINE sdirk3_init_grid_mapping(grid, config_flags)
      USE iso_c_binding
      IMPLICIT NONE
      
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: i, j, k, nx, ny, nz
      REAL :: dx, dy, dt
      INTEGER :: unified_mode, ierr
      CHARACTER(LEN=256) :: message
      
      ! Get domain dimensions
      CALL get_ijk_from_grid2(grid, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            its, ite, jts, jte, kts, kte)
      
      ! Set grid parameters
      nx = ide - ids + 1
      ny = jde - jds + 1
      nz = kde - kds  ! Number of mass point levels (kde-1 - kds + 1)
      
      WRITE(message,'(A,I0,A,I0,A,I0)') 'sdirk3_init_grid_mapping: kde=', kde, ', kds=', kds, ', nz=', nz
      CALL wrf_message(message)
      dx = config_flags%dx
      dy = config_flags%dy
      dt = grid%dt
      
      ! Set unified physics mode based on configuration
      unified_mode = 0
      IF (config_flags%time_integration_scheme == 1) THEN
         unified_mode = 1  ! Full unified implicit mode
      END IF
      
      ! Initialize SDIRK3 solver with map factors and vertical grid
      ierr = wrf_sdirk3_init_solver_with_grid( &
               nx, ny, nz, REAL(dx,c_float), REAL(dy,c_float), REAL(dt,c_float), &
               grid%dnw(kms:kme), &
               grid%rdnw(kms:kme), &
               grid%msftx(ims:ime,jms:jme), &
               grid%msfty(ims:ime,jms:jme), &
               grid%msfux(ims:ime,jms:jme), &
               grid%msfuy(ims:ime,jms:jme), &
               grid%msfvx(ims:ime,jms:jme), &
               grid%msfvy(ims:ime,jms:jme), &
               unified_mode)
      
      IF (ierr /= 0) THEN
         CALL wrf_error_fatal("SDIRK3: Failed to initialize solver with grid mapping factors")
      END IF
      
   END SUBROUTINE sdirk3_init_grid_mapping

   !-------------------------------------------------------------------------
   ! sdirk3_get_grid_params
   ! Extract grid parameters needed by SDIRK3 solver
   !-------------------------------------------------------------------------
   SUBROUTINE sdirk3_get_grid_params(grid, config_flags, &
                                     nx, ny, nz, dx, dy, dt)
      IMPLICIT NONE
      
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      INTEGER, INTENT(OUT) :: nx, ny, nz
      REAL, INTENT(OUT) :: dx, dy, dt
      
      INTEGER :: ids, ide, jds, jde, kds, kde
      INTEGER :: ims, ime, jms, jme, kms, kme
      INTEGER :: its, ite, jts, jte, kts, kte
      
      ! Get domain dimensions
      CALL get_ijk_from_grid2(grid, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            its, ite, jts, jte, kts, kte)
      
      ! Set output parameters
      nx = ide - ids
      ny = jde - jds  
      nz = kde - kds
      dx = config_flags%dx
      dy = config_flags%dy
      dt = grid%dt
      
   END SUBROUTINE sdirk3_get_grid_params

END MODULE module_sdirk3_grid_map