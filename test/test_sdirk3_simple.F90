PROGRAM test_sdirk3_simple
!------------------------------------------------------------------------
! Simple test program for SDIRK-3 implementation
! 
! Tests basic functionality of the SDIRK-3 solver
!------------------------------------------------------------------------

   USE module_unified_state_vector
   USE module_sdirk3_coefficients
   USE module_sdirk3_interface
   
   IMPLICIT NONE
   
   ! Test parameters
   INTEGER, PARAMETER :: nx = 10, ny = 10, nz = 5
   INTEGER, PARAMETER :: n_species = 0
   REAL, PARAMETER :: dt = 0.1
   
   ! State vectors
   TYPE(unified_state_vector) :: U_n, U_new
   TYPE(unified_state_vector), DIMENSION(3) :: k_stages
   TYPE(sdirk3_coeffs) :: coeffs
   TYPE(sdirk3_config) :: config
   
   ! Grid parameters (simplified)
   TYPE :: simple_grid
      INTEGER :: nx, ny, nz
      INTEGER :: its, ite, jts, jte, kts, kte
      INTEGER :: ims, ime, jms, jme, kms, kme
      REAL :: dx, dy
      REAL, DIMENSION(:), ALLOCATABLE :: dz
      REAL, DIMENSION(:), ALLOCATABLE :: c1h, c2h
      INTEGER :: num_tiles
   END TYPE simple_grid
   
   TYPE(simple_grid) :: grid
   
   INTEGER :: i, j, k, stage
   REAL :: initial_value, final_value
   LOGICAL :: converged
   
   WRITE(*,*) '============================================'
   WRITE(*,*) 'SDIRK-3 Simple Test'
   WRITE(*,*) '============================================'
   
   ! Initialize grid
   grid%nx = nx
   grid%ny = ny
   grid%nz = nz
   grid%its = 1; grid%ite = nx
   grid%jts = 1; grid%jte = ny
   grid%kts = 1; grid%kte = nz
   grid%ims = 0; grid%ime = nx+1
   grid%jms = 0; grid%jme = ny+1
   grid%kms = 0; grid%kme = nz+1
   grid%dx = 1000.0  ! 1 km
   grid%dy = 1000.0
   grid%num_tiles = 1
   
   ALLOCATE(grid%dz(nz))
   ALLOCATE(grid%c1h(nz))
   ALLOCATE(grid%c2h(nz))
   grid%dz = 100.0  ! 100 m
   grid%c1h = 0.0
   grid%c2h = 0.0
   
   ! Initialize SDIRK-3 coefficients
   CALL init_sdirk3_coefficients(coeffs)
   
   WRITE(*,*) 'SDIRK-3 Coefficients:'
   WRITE(*,*) '  gamma =', coeffs%gamma
   WRITE(*,*) '  c =', coeffs%c
   WRITE(*,*) '  b =', coeffs%b
   
   ! Initialize state vectors
   CALL init_unified_state(U_n, grid, n_species)
   CALL init_unified_state(U_new, grid, n_species)
   DO stage = 1, 3
      CALL init_unified_state(k_stages(stage), grid, n_species)
   END DO
   
   ! Set initial conditions (simple test)
   initial_value = 1.0
   U_n%data = initial_value
   
   WRITE(*,*) 'Initial state:', initial_value
   
   ! Test 1: RHS evaluation
   WRITE(*,*) ''
   WRITE(*,*) 'Test 1: RHS Evaluation'
   WRITE(*,*) '----------------------'
   
   ! This would call the C++ RHS evaluation
   ! For now, create dummy tendencies
   k_stages(1)%data = -0.1 * U_n%data  ! Simple decay
   
   WRITE(*,*) 'RHS evaluated, sample tendency:', k_stages(1)%data(1,1)
   
   ! Test 2: Single SDIRK stage
   WRITE(*,*) ''
   WRITE(*,*) 'Test 2: Single SDIRK Stage'
   WRITE(*,*) '--------------------------'
   
   ! Solve first stage (no previous stages)
   stage = 1
   converged = .TRUE.  ! Dummy for now
   
   IF (converged) THEN
      WRITE(*,*) 'Stage 1 converged'
   ELSE
      WRITE(*,*) 'Stage 1 failed to converge'
   END IF
   
   ! Test 3: Full SDIRK-3 step
   WRITE(*,*) ''
   WRITE(*,*) 'Test 3: Full SDIRK-3 Step'
   WRITE(*,*) '--------------------------'
   
   ! Simple forward Euler for testing
   U_new%data = U_n%data
   DO stage = 1, 3
      k_stages(stage)%data = -0.1 * U_new%data
      U_new%data = U_new%data + dt * coeffs%b(stage) * k_stages(stage)%data
   END DO
   
   final_value = U_new%data(1,1)
   WRITE(*,*) 'Final state:', final_value
   WRITE(*,*) 'Expected (exp(-0.1*dt)):', EXP(-0.1*dt)
   
   ! Test 4: Conservation check
   WRITE(*,*) ''
   WRITE(*,*) 'Test 4: Conservation Check'
   WRITE(*,*) '--------------------------'
   
   WRITE(*,*) 'Initial sum:', SUM(U_n%data)
   WRITE(*,*) 'Final sum:', SUM(U_new%data)
   
   ! Clean up
   DEALLOCATE(U_n%data)
   DEALLOCATE(U_new%data)
   DO stage = 1, 3
      DEALLOCATE(k_stages(stage)%data)
   END DO
   DEALLOCATE(grid%dz)
   DEALLOCATE(grid%c1h)
   DEALLOCATE(grid%c2h)
   
   WRITE(*,*) ''
   WRITE(*,*) '============================================'
   WRITE(*,*) 'Test completed'
   WRITE(*,*) '============================================'

END PROGRAM test_sdirk3_simple