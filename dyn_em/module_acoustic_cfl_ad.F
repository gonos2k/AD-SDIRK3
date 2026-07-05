!MODULE: module_acoustic_cfl_ad
!
! Description: CFL-based adaptive timestep control with AD
! This module provides CFL stability monitoring with gradients
!
! Author: WRF AD System
! Date: 2025-06-11

MODULE module_acoustic_cfl_ad

   USE module_configure, ONLY : grid_config_rec_type
   USE module_state_description
   USE module_model_constants
   USE module_wrf_error
   USE module_dm, ONLY : wrf_dm_maxval, wrf_dm_sum_real
   USE module_domain, ONLY : domain
   
   IMPLICIT NONE
   
   PRIVATE
   
   ! Public interfaces
   PUBLIC :: calc_cfl_ad
   PUBLIC :: get_adaptive_timestep_ad
   PUBLIC :: update_timestep_gradients
   PUBLIC :: cfl_info
   
   ! CFL criteria type
   TYPE cfl_info
      REAL :: max_cfl_hor
      REAL :: max_cfl_vert
      REAL :: max_cfl_sound
      INTEGER :: i_max, j_max, k_max
   END TYPE cfl_info
   
CONTAINS

   !-----------------------------------------------------------------------
   SUBROUTINE calc_cfl_ad(grid, config_flags, cfl, &
                          ids, ide, jds, jde, kds, kde, &
                          ims, ime, jms, jme, kms, kme, &
                          its, ite, jts, jte, kts, kte)
   
      IMPLICIT NONE
      
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      TYPE(cfl_info), INTENT(OUT) :: cfl
      
      INTEGER, INTENT(IN) :: ids, ide, jds, jde, kds, kde
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      INTEGER, INTENT(IN) :: its, ite, jts, jte, kts, kte
      
      ! Local variables
      INTEGER :: i, j, k
      INTEGER :: idex, jdex
      REAL :: dt, dx, dy
      REAL :: u_max, v_max, w_max, c_sound
      REAL :: cfl_hor, cfl_vert, cfl_sound
      REAL :: local_max_cfl
      
      dt = grid%dt
      dx = grid%dx
      dy = grid%dy
      
      ! Initialize CFL values
      cfl%max_cfl_hor = 0.0
      cfl%max_cfl_vert = 0.0
      cfl%max_cfl_sound = 0.0
      
      ! Calculate CFL numbers
      DO j = jts, MIN(jte, jde-1)
         DO k = kts, kte
            DO i = its, MIN(ite, ide-1)
               
               ! Get velocities
               u_max = 0.5 * (ABS(grid%ru(i,k,j)) + ABS(grid%ru(i+1,k,j)))
               v_max = 0.5 * (ABS(grid%rv(i,k,j)) + ABS(grid%rv(i,k,j+1)))
               w_max = 0.5 * (ABS(grid%ww(i,k,j)) + ABS(grid%ww(i,k+1,j)))
               
               ! Sound speed (simplified)
               c_sound = 340.0  ! m/s, simplified constant
               
               ! CFL calculations
               cfl_hor = dt * (u_max/dx + v_max/dy)
               cfl_vert = dt * w_max / (grid%dnw(k) * grid%rdz(i,k,j))
               cfl_sound = dt * c_sound * (1.0/dx + 1.0/dy)
               
               ! Track maximum values
               IF (cfl_hor > cfl%max_cfl_hor) THEN
                  cfl%max_cfl_hor = cfl_hor
                  cfl%i_max = i
                  cfl%j_max = j
               END IF
               
               IF (cfl_vert > cfl%max_cfl_vert) THEN
                  cfl%max_cfl_vert = cfl_vert
                  cfl%k_max = k
               END IF
               
               IF (cfl_sound > cfl%max_cfl_sound) THEN
                  cfl%max_cfl_sound = cfl_sound
               END IF
               
            END DO
         END DO
      END DO
      
      ! Global maximum across processors
#ifdef DM_PARALLEL
      local_max_cfl = cfl%max_cfl_hor
      CALL wrf_dm_maxval(local_max_cfl, idex, jdex)
      cfl%max_cfl_hor = local_max_cfl
      
      local_max_cfl = cfl%max_cfl_vert
      CALL wrf_dm_maxval(local_max_cfl, idex, jdex)
      cfl%max_cfl_vert = local_max_cfl
      
      local_max_cfl = cfl%max_cfl_sound
      CALL wrf_dm_maxval(local_max_cfl, idex, jdex)
      cfl%max_cfl_sound = local_max_cfl
#endif
      
   END SUBROUTINE calc_cfl_ad

   !-----------------------------------------------------------------------
   FUNCTION get_adaptive_timestep_ad(grid, config_flags, cfl) RESULT(dt_new)
   
      IMPLICIT NONE
      
      TYPE(domain), INTENT(IN) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      TYPE(cfl_info), INTENT(IN) :: cfl
      REAL :: dt_new
      
      ! Local variables
      REAL :: max_cfl, target_cfl, safety_factor
      REAL :: dt_current, dt_min, dt_max
      
      ! Get current timestep and limits
      dt_current = grid%dt
      dt_min = config_flags%min_time_step
      dt_max = config_flags%max_time_step
      
      ! Safety parameters
      target_cfl = 0.8   ! Target CFL number
      safety_factor = 0.9 ! Safety margin
      
      ! Find maximum CFL
      max_cfl = MAX(cfl%max_cfl_hor, cfl%max_cfl_vert, cfl%max_cfl_sound)
      
      ! Calculate new timestep
      IF (max_cfl > 0.0) THEN
         dt_new = dt_current * (target_cfl / max_cfl) * safety_factor
      ELSE
         dt_new = dt_current
      END IF
      
      ! Apply limits
      dt_new = MAX(dt_new, dt_min)
      dt_new = MIN(dt_new, dt_max)
      
      ! Log if significant change
      IF (ABS(dt_new - dt_current) / dt_current > 0.1) THEN
         WRITE(wrf_err_message, '(A,F8.4,A,F8.4,A,F6.3)') &
            'Adaptive timestep: dt changed from ', dt_current, &
            ' to ', dt_new, ' (max CFL = ', max_cfl, ')'
         CALL wrf_debug(100, wrf_err_message)
      END IF
      
   END FUNCTION get_adaptive_timestep_ad

   !-----------------------------------------------------------------------
   SUBROUTINE update_timestep_gradients(grid, config_flags, grad_dt, &
                                       ids, ide, jds, jde, kds, kde, &
                                       ims, ime, jms, jme, kms, kme, &
                                       its, ite, jts, jte, kts, kte)
   
      IMPLICIT NONE
      
      TYPE(domain), INTENT(INOUT) :: grid
      TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
      REAL, INTENT(IN) :: grad_dt
      
      INTEGER, INTENT(IN) :: ids, ide, jds, jde, kds, kde
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      INTEGER, INTENT(IN) :: its, ite, jts, jte, kts, kte
      
      ! Local variables
      INTEGER :: i, j, k
      REAL :: factor
      
      ! Simple gradient propagation
      factor = grad_dt * 0.01  ! Scaling factor
      
      DO j = jts, jte
         DO k = kts, kte
            DO i = its, ite
               ! Update velocity gradients based on timestep gradient
               grid%acoustic_grad_u(i,k,j) = grid%acoustic_grad_u(i,k,j) + &
                                             factor * grid%ru(i,k,j)
               grid%acoustic_grad_v(i,k,j) = grid%acoustic_grad_v(i,k,j) + &
                                             factor * grid%rv(i,k,j)
               grid%acoustic_grad_w(i,k,j) = grid%acoustic_grad_w(i,k,j) + &
                                             factor * grid%ww(i,k,j)
            END DO
         END DO
      END DO
      
   END SUBROUTINE update_timestep_gradients

END MODULE module_acoustic_cfl_ad