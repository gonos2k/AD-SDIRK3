!
! MODULE: module_sdirk3_halo_interface
!
! DESCRIPTION: Fortran interface for SDIRK3 MPI halo exchange
!
! This module provides Fortran wrappers for the C++ halo exchange
! implementation, integrating with WRF's existing communication
! infrastructure.
!

MODULE module_sdirk3_halo_interface

   USE module_domain, ONLY : domain
   USE module_configure, ONLY : grid_config_rec_type
   USE module_dm, ONLY : local_communicator, mytask, ntasks, ntasks_x, ntasks_y
   USE module_comm_dm, ONLY : HALO_EM_SDIRK3_K1_sub, HALO_EM_SDIRK3_K2_sub, &
                              HALO_EM_SDIRK3_K3_sub, HALO_EM_SDIRK3_WORK_sub

   IMPLICIT NONE
   PRIVATE

   PUBLIC :: sdirk3_halo_init_f90
   PUBLIC :: sdirk3_halo_finalize_f90
   PUBLIC :: sdirk3_halo_exchange_f90
   PUBLIC :: sdirk3_halo_exchange_stage

   ! C interface declarations
   INTERFACE

      SUBROUTINE sdirk3_halo_init(ids, ide, jds, jde, kds, kde, &
                                  ims, ime, jms, jme, kms, kme, &
                                  ips, ipe, jps, jpe, kps, kpe, &
                                  nprocx, nprocy, mypx, mypy, &
                                  halo_width) BIND(C)
         USE ISO_C_BINDING
         INTEGER(C_INT), VALUE :: ids, ide, jds, jde, kds, kde
         INTEGER(C_INT), VALUE :: ims, ime, jms, jme, kms, kme
         INTEGER(C_INT), VALUE :: ips, ipe, jps, jpe, kps, kpe
         INTEGER(C_INT), VALUE :: nprocx, nprocy, mypx, mypy
         INTEGER(C_INT), VALUE :: halo_width
      END SUBROUTINE sdirk3_halo_init

      SUBROUTINE sdirk3_halo_finalize() BIND(C)
         USE ISO_C_BINDING
      END SUBROUTINE sdirk3_halo_finalize

      SUBROUTINE sdirk3_halo_exchange(state_array, nx, ny, nz, num_vars, &
                                      ids, ide, jds, jde, kds, kde, &
                                      ims, ime, jms, jme, kms, kme, &
                                      ips, ipe, jps, jpe, kps, kpe) BIND(C)
         USE ISO_C_BINDING
         REAL(C_FLOAT), DIMENSION(*) :: state_array
         INTEGER(C_INT), VALUE :: nx, ny, nz, num_vars
         INTEGER(C_INT), VALUE :: ids, ide, jds, jde, kds, kde
         INTEGER(C_INT), VALUE :: ims, ime, jms, jme, kms, kme
         INTEGER(C_INT), VALUE :: ips, ipe, jps, jpe, kps, kpe
      END SUBROUTINE sdirk3_halo_exchange

   END INTERFACE

CONTAINS

   !
   ! Initialize SDIRK3 halo exchange system
   !
   SUBROUTINE sdirk3_halo_init_f90(grid, config_flags)
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags

      INTEGER :: mypx, mypy
      INTEGER :: halo_width

      ! Get process grid position
      ! In WRF, mytask is the global MPI rank
      mypy = mytask / ntasks_x
      mypx = MOD(mytask, ntasks_x)

      ! Default halo width (can be made configurable)
      halo_width = 4

      ! Initialize C++ halo exchange system
      CALL sdirk3_halo_init( &
         grid%sd31, grid%ed31, grid%sd32, grid%ed32, grid%sd33, grid%ed33, &
         grid%sm31, grid%em31, grid%sm32, grid%em32, grid%sm33, grid%em33, &
         grid%sp31, grid%ep31, grid%sp32, grid%ep32, grid%sp33, grid%ep33, &
         ntasks_x, ntasks_y, mypx, mypy, halo_width)

   END SUBROUTINE sdirk3_halo_init_f90

   !
   ! Finalize SDIRK3 halo exchange system
   !
   SUBROUTINE sdirk3_halo_finalize_f90()
      CALL sdirk3_halo_finalize()
   END SUBROUTINE sdirk3_halo_finalize_f90

   !
   ! Generic halo exchange for SDIRK3 arrays
   !
   SUBROUTINE sdirk3_halo_exchange_f90(state_array, grid, num_vars)
      REAL, DIMENSION(:,:,:,:), INTENT(INOUT) :: state_array
      TYPE(domain), INTENT(IN) :: grid
      INTEGER, INTENT(IN) :: num_vars

      INTEGER :: nx, ny, nz

      ! Get actual grid dimensions
      nx = grid%ep31 - grid%sp31 + 1
      ny = grid%ep32 - grid%sp32 + 1
      nz = grid%ep33 - grid%sp33 + 1

      ! Call C++ halo exchange
      CALL sdirk3_halo_exchange( &
         state_array, nx, ny, nz, num_vars, &
         grid%sd31, grid%ed31, grid%sd32, grid%ed32, grid%sd33, grid%ed33, &
         grid%sm31, grid%em31, grid%sm32, grid%em32, grid%sm33, grid%em33, &
         grid%sp31, grid%ep31, grid%sp32, grid%ep32, grid%sp33, grid%ep33)

   END SUBROUTINE sdirk3_halo_exchange_f90

   !
   ! Exchange halos for specific SDIRK3 stage
   !
   SUBROUTINE sdirk3_halo_exchange_stage(grid, stage)
      TYPE(domain), INTENT(INOUT) :: grid
      INTEGER, INTENT(IN) :: stage

      ! Use WRF's existing halo exchange routines for SDIRK3 arrays
      ! These are defined in the Registry and generated automatically

      SELECT CASE(stage)
      CASE(1)
#ifdef DM_PARALLEL
# include "HALO_EM_SDIRK3_K1.inc"
#endif
      CASE(2)
#ifdef DM_PARALLEL
# include "HALO_EM_SDIRK3_K2.inc"
#endif
      CASE(3)
#ifdef DM_PARALLEL
# include "HALO_EM_SDIRK3_K3.inc"
#endif
      CASE DEFAULT
         ! Exchange work array
#ifdef DM_PARALLEL
# include "HALO_EM_SDIRK3_WORK.inc"
#endif
      END SELECT

   END SUBROUTINE sdirk3_halo_exchange_stage

END MODULE module_sdirk3_halo_interface