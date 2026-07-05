!===============================================================================
! solve_em_kslab_integration.F
!
! Purpose: Example integration of k-slab configuration into solve_em.F
!
! This file shows the modifications needed to add k-slab support to the
! main WRF dynamics solver routine. These changes should be integrated
! into the actual solve_em.F file.
!
! Author: yhlee
! Date: 2025-08-05
!===============================================================================

! Add to the USE statements section at the top of solve_em:
!-----------------------------------------------------------------------------
! USE module_namelist_kslab, ONLY : setup_kslab_config

! Add to the initialization section (after grid%itimestep == 1 check):
!-----------------------------------------------------------------------------
SUBROUTINE solve_em_kslab_init(grid, config_flags)
    USE module_domain, ONLY : domain
    USE module_configure, ONLY : grid_config_rec_type
    USE module_namelist_kslab, ONLY : setup_kslab_config
    USE module_wrf_error
    
    IMPLICIT NONE
    
    TYPE(domain), INTENT(INOUT) :: grid
    TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
    
    ! Initialize k-slab configuration if SDIRK3 is enabled
    IF (config_flags%time_integration_scheme == 1) THEN
        IF (grid%itimestep == 1) THEN
            CALL wrf_message('Initializing SDIRK3 k-slab configuration...')
            
            ! Setup k-slab configuration from namelist
            CALL setup_kslab_config(config_flags)
            
            ! Additional k-slab specific initialization could go here
            IF (config_flags%use_advanced_kslab) THEN
                CALL wrf_message('Advanced k-slab vertical operations enabled')
                
                ! Set OpenMP threads if specified
                IF (config_flags%kslab_num_threads > 0) THEN
#ifdef _OPENMP
                    CALL omp_set_num_threads(config_flags%kslab_num_threads)
                    WRITE(wrf_err_message,*) 'K-slab: Set OpenMP threads to ', &
                                           config_flags%kslab_num_threads
                    CALL wrf_message(TRIM(wrf_err_message))
#endif
                END IF
            END IF
        END IF
    END IF
    
END SUBROUTINE solve_em_kslab_init

! Add to the main time loop section (inside the time step loop):
!-----------------------------------------------------------------------------
SUBROUTINE solve_em_kslab_metrics(grid, config_flags)
    USE module_domain, ONLY : domain
    USE module_configure, ONLY : grid_config_rec_type
    USE module_wrf_error
    
    IMPLICIT NONE
    
    TYPE(domain), INTENT(IN) :: grid
    TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
    
    ! Interface to C++ metric printing function
    INTERFACE
        SUBROUTINE sdirk3_print_kslab_metrics() BIND(C)
        END SUBROUTINE sdirk3_print_kslab_metrics
    END INTERFACE
    
    REAL :: time_since_last_print
    SAVE :: last_metric_print_time = 0.0
    
    ! Print k-slab metrics at specified intervals
    IF (config_flags%time_integration_scheme == 1 .AND. &
        config_flags%use_advanced_kslab .AND. &
        config_flags%kslab_print_metrics) THEN
        
        time_since_last_print = grid%xtime - last_metric_print_time
        
        IF (time_since_last_print >= config_flags%kslab_metric_interval) THEN
            CALL wrf_message(' ')
            CALL wrf_message('=== K-Slab Performance Metrics ===')
            CALL wrf_message(' ')
            
            ! Call C++ function to print metrics
            CALL sdirk3_print_kslab_metrics()
            
            last_metric_print_time = grid%xtime
        END IF
    END IF
    
END SUBROUTINE solve_em_kslab_metrics

!===============================================================================
! Example of complete integration into solve_em
!
! The following shows where to add the k-slab calls in the actual solve_em.F:
!===============================================================================

! SUBROUTINE solve_em ( grid , config_flags                  &
! ...existing parameters...
!                     )
!
! ... existing USE statements ...
! USE module_namelist_kslab, ONLY : setup_kslab_config  ! ADD THIS
!
! ... existing code ...
!
! !<DESCRIPTION>
! !
! ! Solve_em advances the Eulerian mass model from the current time to the
! ! next time.
! !</DESCRIPTION>
!
! ... existing variable declarations ...
!
! ... existing code ...
!
! ! Initialize if this is the first time step
! IF (grid%itimestep == 1) THEN
!     ... existing initialization ...
!     
!     ! Initialize k-slab configuration for SDIRK3          ! ADD THIS BLOCK
!     IF (config_flags%time_integration_scheme == 1) THEN  ! ADD THIS BLOCK
!         CALL solve_em_kslab_init(grid, config_flags)     ! ADD THIS BLOCK
!     END IF                                                ! ADD THIS BLOCK
! END IF
!
! ... existing code ...
!
! ! Main time integration loop
! integration_loop : DO dynamics_step = 1, grid%time_step_sound
!     
!     ... existing integration code ...
!     
! END DO integration_loop
!
! ! Print k-slab metrics if needed                         ! ADD THIS BLOCK
! CALL solve_em_kslab_metrics(grid, config_flags)         ! ADD THIS BLOCK
!
! ... rest of solve_em ...

!===============================================================================
! Additional Notes:
!
! 1. The module_namelist_kslab.F file must be compiled and linked
! 2. The Registry.SDIRK3_INTEGRATED must be included in the build
! 3. The C++ interface functions must be available in the SDIRK3 library
! 4. Test with both use_advanced_kslab = .true. and .false. to ensure
!    backward compatibility
!
! Integration Checklist:
! [ ] Add USE statement for module_namelist_kslab
! [ ] Add initialization call after grid%itimestep == 1 check
! [ ] Add metric printing call in appropriate location
! [ ] Verify Registry entries are processed correctly
! [ ] Test with various k-slab configurations
!===============================================================================