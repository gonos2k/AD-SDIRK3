MODULE module_sdirk3_coefficients_data
   !
   ! Module: wrf_sdirk3_coefficients_data
   ! Purpose: SDIRK3 Butcher tableau coefficients
   !
   ! L-stable SDIRK3 with gamma = (3 + sqrt(3))/6
   !
   
   USE module_configure, ONLY : grid_config_rec_type
   
   IMPLICIT NONE
   
   ! SDIRK3 coefficients (L-stable)
   REAL, PARAMETER :: sdirk3_gamma = 0.4358665215084590  ! (3 + sqrt(3))/6
   
   ! Butcher tableau A matrix (lower triangular)
   REAL, PARAMETER :: sdirk3_a21 = 0.4358665215084590   ! gamma
   REAL, PARAMETER :: sdirk3_a31 = 0.2820876568026110   ! (3*gamma - 2)/4
   REAL, PARAMETER :: sdirk3_a32 = 0.2820876568026110   ! (3*gamma - 2)/4
   
   ! Butcher tableau b weights
   REAL, PARAMETER, DIMENSION(3) :: sdirk3_b = (/ &
      0.1666666666666667,  &  ! 1/6
      0.1666666666666667,  &  ! 1/6
      0.6666666666666667   &  ! 2/3
   /)
   
   ! Butcher tableau c nodes
   REAL, PARAMETER, DIMENSION(3) :: sdirk3_c = (/ &
      0.4358665215084590,  &  ! gamma
      0.8717330430169180,  &  ! 2*gamma
      1.0000000000000000   &  ! 1
   /)
   
   ! Error estimation weights (if using embedded method)
   REAL, PARAMETER, DIMENSION(3) :: sdirk3_b_hat = (/ &
      0.2222222222222222,  &  ! 2/9
      0.5555555555555556,  &  ! 5/9
      0.2222222222222222   &  ! 2/9
   /)
   
CONTAINS

   !-------------------------------------------------------------------------
   ! print_sdirk3_coefficients
   ! Print SDIRK3 coefficients for verification
   !-------------------------------------------------------------------------
   SUBROUTINE print_sdirk3_coefficients()
      
      WRITE(*,'(A)') "SDIRK3 Coefficients (L-stable):"
      WRITE(*,'(A,F20.16)') "gamma = ", sdirk3_gamma
      WRITE(*,'(A)') ""
      WRITE(*,'(A)') "Butcher tableau A matrix:"
      WRITE(*,'(A,F20.16)') "a21 = ", sdirk3_a21
      WRITE(*,'(A,F20.16)') "a31 = ", sdirk3_a31
      WRITE(*,'(A,F20.16)') "a32 = ", sdirk3_a32
      WRITE(*,'(A)') ""
      WRITE(*,'(A)') "Weights b:"
      WRITE(*,'(A,3F20.16)') "b = ", sdirk3_b
      WRITE(*,'(A)') ""
      WRITE(*,'(A)') "Nodes c:"
      WRITE(*,'(A,3F20.16)') "c = ", sdirk3_c
      
   END SUBROUTINE print_sdirk3_coefficients
   
   !-------------------------------------------------------------------------
   ! check_order_conditions
   ! Verify that coefficients satisfy order conditions
   !-------------------------------------------------------------------------
   LOGICAL FUNCTION check_order_conditions()
      REAL :: sum_b, sum_bc, sum_bc2
      REAL, PARAMETER :: tol = 1.0E-14
      
      ! Order 1: sum(b_i) = 1
      sum_b = SUM(sdirk3_b)
      
      ! Order 2: sum(b_i * c_i) = 1/2
      sum_bc = sdirk3_b(1)*sdirk3_c(1) + &
               sdirk3_b(2)*sdirk3_c(2) + &
               sdirk3_b(3)*sdirk3_c(3)
      
      ! Order 3: sum(b_i * c_i^2) = 1/3
      sum_bc2 = sdirk3_b(1)*sdirk3_c(1)**2 + &
                sdirk3_b(2)*sdirk3_c(2)**2 + &
                sdirk3_b(3)*sdirk3_c(3)**2
      
      check_order_conditions = &
         (ABS(sum_b - 1.0) < tol) .AND. &
         (ABS(sum_bc - 0.5) < tol) .AND. &
         (ABS(sum_bc2 - 1.0/3.0) < tol)
      
      IF (.NOT. check_order_conditions) THEN
         WRITE(*,'(A)') "SDIRK3 order condition check failed!"
         WRITE(*,'(A,E20.12)') "sum(b) - 1 = ", sum_b - 1.0
         WRITE(*,'(A,E20.12)') "sum(b*c) - 1/2 = ", sum_bc - 0.5
         WRITE(*,'(A,E20.12)') "sum(b*c^2) - 1/3 = ", sum_bc2 - 1.0/3.0
      END IF
      
   END FUNCTION check_order_conditions

END MODULE module_sdirk3_coefficients_data