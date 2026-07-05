MODULE module_zero_copy_interface
    !
    ! Zero-copy interface between WRF Fortran and C++ SDIRK3
    !
    USE module_configure, ONLY : grid_config_rec_type
    USE module_state_description
    
    IMPLICIT NONE
    PRIVATE
    
    PUBLIC :: sdirk3_zero_copy_timestep
    PUBLIC :: test_zero_copy_performance
    
    ! Interface to C++ zero-copy implementation
    INTERFACE
        SUBROUTINE c_sdirk3_zero_copy_timestep(              &
            u_tend, v_tend, w_tend, t_tend, ph_tend, mu_tend,&
            u, v, w, t, ph, mu,                              &
            ims, ime, kms, kme, jms, jme,                    &
            its, ite, kts, kte, jts, jte,                    &
            dt, dx, dy) BIND(C, name="sdirk3_zero_copy_timestep")
            
            USE ISO_C_BINDING
            INTEGER(C_INT), VALUE :: ims, ime, kms, kme, jms, jme
            INTEGER(C_INT), VALUE :: its, ite, kts, kte, jts, jte
            REAL(C_FLOAT), VALUE :: dt, dx, dy
            
            ! Output tendencies (modified in-place)
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme) :: u_tend
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme) :: v_tend
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme) :: w_tend
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme) :: t_tend
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme) :: ph_tend
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme) :: mu_tend
            
            ! Input state (read-only)
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: u
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: v
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: w
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: t
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: ph
            REAL(C_FLOAT), DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: mu
        END SUBROUTINE c_sdirk3_zero_copy_timestep
    END INTERFACE
    
CONTAINS
    
    SUBROUTINE sdirk3_zero_copy_timestep(grid, config_flags, &
                                         ru_tend, rv_tend, rw_tend, &
                                         t_tend, ph_tend, mu_tend, &
                                         ims, ime, kms, kme, jms, jme, &
                                         its, ite, kts, kte, jts, jte, &
                                         dt)
        USE module_domain, ONLY : domain
        TYPE(domain), INTENT(INOUT) :: grid
        TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
        INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
        INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
        REAL, INTENT(IN) :: dt
        
        ! Tendency arrays passed as arguments
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(INOUT) :: ru_tend
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(INOUT) :: rv_tend
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(INOUT) :: rw_tend
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(INOUT) :: t_tend
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(INOUT) :: ph_tend
        REAL, DIMENSION(ims:ime, jms:jme), INTENT(INOUT) :: mu_tend
        
        ! Local variables
        REAL :: dx, dy
        INTEGER :: i, j, k
        
        ! Get grid spacing
        dx = config_flags%dx
        dy = config_flags%dy
        
        ! Important: Ensure arrays are contiguous in memory
        ! WRF arrays are already allocated with proper bounds
        
        ! Call C++ implementation with zero-copy views
        ! The C++ code will create tensor views directly from these arrays
        ! without any data copying
        CALL c_sdirk3_zero_copy_timestep(                    &
            ru_tend, rv_tend, rw_tend,                      &
            t_tend, ph_tend, mu_tend,                       &
            grid%u_2, grid%v_2, grid%w_2,                   &
            grid%t_2, grid%ph_2, grid%mu_2,                 &
            ims, ime, kms, kme, jms, jme,                   &
            its, ite, kts, kte, jts, jte,                   &
            dt, dx, dy)
        
        ! No need to copy back - modifications were made in-place!
        
    END SUBROUTINE sdirk3_zero_copy_timestep
    
    SUBROUTINE test_zero_copy_performance(grid, config_flags, &
                                         ims, ime, kms, kme, jms, jme, &
                                         its, ite, kts, kte, jts, jte)
        USE module_domain, ONLY : domain
        USE module_timing
        
        TYPE(domain), INTENT(INOUT) :: grid
        TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
        INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
        INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
        
        ! Timing variables
        REAL :: t1, t2, t_zero_copy, t_traditional
        INTEGER :: n_iterations = 100
        INTEGER :: iter, i, j, k
        
        ! Test arrays
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme) :: test_array
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme) :: work_array
        
        PRINT*, "=== Zero-Copy Performance Test ==="
        PRINT*, "Array dimensions: ", ime-ims+1, "x", kme-kms+1, "x", jme-jms+1
        PRINT*, "Tile dimensions: ", ite-its+1, "x", kte-kts+1, "x", jte-jts+1
        
        ! Initialize test data
        DO j = jms, jme
        DO k = kms, kme
        DO i = ims, ime
            test_array(i,k,j) = REAL(i + k*100 + j*10000)
        END DO
        END DO
        END DO
        
        ! Test zero-copy approach
        CALL cpu_time(t1)
        DO iter = 1, n_iterations
            ! Direct operation on array - no copy
            CALL direct_operation_on_array(test_array,      &
                                          ims, ime, kms, kme, jms, jme, &
                                          its, ite, kts, kte, jts, jte)
        END DO
        CALL cpu_time(t2)
        t_zero_copy = t2 - t1
        
        ! Test traditional copy approach
        CALL cpu_time(t1)
        DO iter = 1, n_iterations
            ! Copy to work array
            work_array(its:ite, kts:kte, jts:jte) = &
                test_array(its:ite, kts:kte, jts:jte)
            
            ! Operation on work array
            CALL direct_operation_on_array(work_array,      &
                                          ims, ime, kms, kme, jms, jme, &
                                          its, ite, kts, kte, jts, jte)
            
            ! Copy back
            test_array(its:ite, kts:kte, jts:jte) = &
                work_array(its:ite, kts:kte, jts:jte)
        END DO
        CALL cpu_time(t2)
        t_traditional = t2 - t1
        
        PRINT*, "Zero-copy time: ", t_zero_copy, " seconds"
        PRINT*, "Traditional copy time: ", t_traditional, " seconds"
        PRINT*, "Speedup: ", t_traditional / t_zero_copy, "x"
        PRINT*, "Memory saved: ", &
                2 * (ite-its+1) * (kte-kts+1) * (jte-jts+1) * 4 / 1024.0 / 1024.0, " MB"
        
    END SUBROUTINE test_zero_copy_performance
    
    SUBROUTINE direct_operation_on_array(array,             &
                                        ims, ime, kms, kme, jms, jme, &
                                        its, ite, kts, kte, jts, jte)
        INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
        INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
        REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(INOUT) :: array
        
        INTEGER :: i, j, k
        
        ! Simple operation for testing
        DO j = jts, jte
        DO k = kts, kte
        DO i = its, ite
            array(i,k,j) = array(i,k,j) * 1.001 + 0.001
        END DO
        END DO
        END DO
        
    END SUBROUTINE direct_operation_on_array
    
END MODULE module_zero_copy_interface