!WRF:MODEL_LAYER:DYNAMICS
!
MODULE module_dynamics_interface
!
! Purpose: Fortran-C++ interface for dynamics computations
!          Provides memory-safe data exchange between WRF Fortran and C++ libraries
!
! Author: WRF Development Team
! Date: 2025-06-15
!

USE ISO_C_BINDING
USE module_wrf_error
USE module_domain, ONLY : domain

IMPLICIT NONE

PRIVATE

! Public interface procedures
PUBLIC :: dynamics_interface_init
PUBLIC :: dynamics_interface_finalize  
PUBLIC :: dynamics_interface_acoustic_step
PUBLIC :: dynamics_interface_implicit_step

! Memory conversion utilities
PUBLIC :: convert_3d_to_linear
PUBLIC :: convert_linear_to_3d
PUBLIC :: validate_array_bounds

! C interface declarations
INTERFACE
    SUBROUTINE c_dynamics_init(nx, ny, nz, mode) &
        BIND(C, name="wrf_dynamics_init")
        USE ISO_C_BINDING
        INTEGER(C_INT), VALUE :: nx, ny, nz, mode
    END SUBROUTINE c_dynamics_init

    SUBROUTINE c_dynamics_finalize() &
        BIND(C, name="wrf_dynamics_finalize")
        USE ISO_C_BINDING
    END SUBROUTINE c_dynamics_finalize

    SUBROUTINE c_acoustic_advance(u_ptr, v_ptr, w_ptr, p_ptr, &
                                  tend_u_ptr, tend_v_ptr, tend_w_ptr, &
                                  c2_ptr, dt, dx, dy, dz, &
                                  nx, ny, nz, theta, converged) &
        BIND(C, name="wrf_acoustic_advance")
        USE ISO_C_BINDING
        TYPE(C_PTR), VALUE :: u_ptr, v_ptr, w_ptr, p_ptr
        TYPE(C_PTR), VALUE :: tend_u_ptr, tend_v_ptr, tend_w_ptr, c2_ptr
        REAL(C_FLOAT), VALUE :: dt, dx, dy, dz, theta
        INTEGER(C_INT), VALUE :: nx, ny, nz
        LOGICAL(C_BOOL) :: converged
    END SUBROUTINE c_acoustic_advance

    SUBROUTINE c_implicit_solve(state_ptr, tend_ptr, coeff_ptr, &
                               nx, ny, nz, dt, converged) &
        BIND(C, name="wrf_implicit_solve")
        USE ISO_C_BINDING
        TYPE(C_PTR), VALUE :: state_ptr, tend_ptr, coeff_ptr
        INTEGER(C_INT), VALUE :: nx, ny, nz
        REAL(C_FLOAT), VALUE :: dt
        LOGICAL(C_BOOL) :: converged
    END SUBROUTINE c_implicit_solve
END INTERFACE

CONTAINS

!----------------------------------------------------------------------------------------
! Initialize dynamics interface
!----------------------------------------------------------------------------------------
SUBROUTINE dynamics_interface_init(grid, config_flags)
    USE module_configure, ONLY : grid_config_rec_type
    
    TYPE(domain), INTENT(IN) :: grid
    TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
    
    INTEGER :: nx, ny, nz, mode
    
    ! Get grid dimensions
    nx = grid%ed31 - grid%sd31 + 1
    ny = grid%ed33 - grid%sd33 + 1  
    nz = grid%ed32 - grid%sd32 + 1
    
    ! Get computation mode from config
    mode = config_flags%dynamics_option
    
    ! Initialize C++ library
    CALL c_dynamics_init(nx, ny, nz, mode)
    
    CALL wrf_message("Dynamics interface initialized")
    
END SUBROUTINE dynamics_interface_init

!----------------------------------------------------------------------------------------
! Finalize dynamics interface
!----------------------------------------------------------------------------------------
SUBROUTINE dynamics_interface_finalize()
    
    CALL c_dynamics_finalize()
    CALL wrf_message("Dynamics interface finalized")
    
END SUBROUTINE dynamics_interface_finalize

!----------------------------------------------------------------------------------------
! Acoustic wave computation step
!----------------------------------------------------------------------------------------
SUBROUTINE dynamics_interface_acoustic_step(grid, dt_acoustic, config_flags)
    USE module_configure, ONLY : grid_config_rec_type
    
    TYPE(domain), INTENT(INOUT) :: grid
    REAL, INTENT(IN) :: dt_acoustic
    TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
    
    ! Local variables
    REAL, ALLOCATABLE, TARGET :: buffer_u(:), buffer_v(:), buffer_w(:), buffer_p(:)
    REAL, ALLOCATABLE, TARGET :: buffer_tend_u(:), buffer_tend_v(:), buffer_tend_w(:)
    REAL, ALLOCATABLE, TARGET :: buffer_c2(:)
    TYPE(C_PTR) :: u_ptr, v_ptr, w_ptr, p_ptr
    TYPE(C_PTR) :: tend_u_ptr, tend_v_ptr, tend_w_ptr, c2_ptr
    INTEGER :: nx, ny, nz, total_size
    LOGICAL(C_BOOL) :: converged
    REAL :: dx, dy, dz, theta
    
    ! Get dimensions
    nx = grid%ed31 - grid%sd31 + 1
    ny = grid%ed33 - grid%sd33 + 1
    nz = grid%ed32 - grid%sd32 + 1
    total_size = nx * ny * nz
    
    ! Get grid spacing
    dx = grid%dx
    dy = grid%dy
    dz = grid%znw(2) - grid%znw(1)  ! Simplified vertical spacing
    
    ! Get implicit parameter
    theta = config_flags%implicit_acoustic_theta
    
    ! Allocate buffers
    ALLOCATE(buffer_u(total_size))
    ALLOCATE(buffer_v(total_size))
    ALLOCATE(buffer_w(total_size))
    ALLOCATE(buffer_p(total_size))
    ALLOCATE(buffer_tend_u(total_size))
    ALLOCATE(buffer_tend_v(total_size))
    ALLOCATE(buffer_tend_w(total_size))
    ALLOCATE(buffer_c2(total_size))
    
    ! Convert 3D arrays to linear buffers
    CALL convert_3d_to_linear(grid%u_2, buffer_u, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    CALL convert_3d_to_linear(grid%v_2, buffer_v, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    CALL convert_3d_to_linear(grid%w_2, buffer_w, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    CALL convert_3d_to_linear(grid%p, buffer_p, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    ! Get C pointers
    u_ptr = C_LOC(buffer_u(1))
    v_ptr = C_LOC(buffer_v(1))
    w_ptr = C_LOC(buffer_w(1))
    p_ptr = C_LOC(buffer_p(1))
    tend_u_ptr = C_LOC(buffer_tend_u(1))
    tend_v_ptr = C_LOC(buffer_tend_v(1))
    tend_w_ptr = C_LOC(buffer_tend_w(1))
    c2_ptr = C_LOC(buffer_c2(1))
    
    ! Call C++ acoustic solver
    CALL c_acoustic_advance(u_ptr, v_ptr, w_ptr, p_ptr, &
                           tend_u_ptr, tend_v_ptr, tend_w_ptr, c2_ptr, &
                           dt_acoustic, dx, dy, dz, &
                           nx, ny, nz, theta, converged)
    
    IF (.NOT. converged) THEN
        CALL wrf_message("WARNING: Acoustic solver did not converge")
    END IF
    
    ! Convert results back to 3D
    CALL convert_linear_to_3d(buffer_u, grid%u_2, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    CALL convert_linear_to_3d(buffer_v, grid%v_2, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    CALL convert_linear_to_3d(buffer_w, grid%w_2, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    CALL convert_linear_to_3d(buffer_p, grid%p, &
                             grid%sd31, grid%ed31, &
                             grid%sd32, grid%ed32, &
                             grid%sd33, grid%ed33)
    
    ! Cleanup
    DEALLOCATE(buffer_u, buffer_v, buffer_w, buffer_p)
    DEALLOCATE(buffer_tend_u, buffer_tend_v, buffer_tend_w)
    DEALLOCATE(buffer_c2)
    
END SUBROUTINE dynamics_interface_acoustic_step

!----------------------------------------------------------------------------------------
! Implicit time integration step
!----------------------------------------------------------------------------------------
SUBROUTINE dynamics_interface_implicit_step(grid, dt, config_flags)
    USE module_configure, ONLY : grid_config_rec_type
    
    TYPE(domain), INTENT(INOUT) :: grid
    REAL, INTENT(IN) :: dt
    TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
    
    ! Implementation similar to acoustic step
    ! This is a placeholder for the implicit solver interface
    
    CALL wrf_message("Implicit step called - implementation pending")
    
END SUBROUTINE dynamics_interface_implicit_step

!----------------------------------------------------------------------------------------
! Convert 3D array to linear buffer (Fortran to C++ memory layout)
!----------------------------------------------------------------------------------------
SUBROUTINE convert_3d_to_linear(array_3d, buffer_linear, &
                                is, ie, ks, ke, js, je)
    
    INTEGER, INTENT(IN) :: is, ie, ks, ke, js, je
    REAL, DIMENSION(is:ie, ks:ke, js:je), INTENT(IN) :: array_3d
    REAL, DIMENSION(:), INTENT(OUT) :: buffer_linear
    
    INTEGER :: i, j, k, idx
    INTEGER :: nx, ny, nz
    
    nx = ie - is + 1
    ny = je - js + 1
    nz = ke - ks + 1
    
    ! Convert from Fortran (i,k,j) to C++ linear array for {nz,ny,nx}
    idx = 1
    DO i = is, ie
        DO j = js, je
            DO k = ks, ke
                buffer_linear(idx) = array_3d(i, k, j)
                idx = idx + 1
            END DO
        END DO
    END DO
    
END SUBROUTINE convert_3d_to_linear

!----------------------------------------------------------------------------------------
! Convert linear buffer to 3D array (C++ to Fortran memory layout)
!----------------------------------------------------------------------------------------
SUBROUTINE convert_linear_to_3d(buffer_linear, array_3d, &
                                is, ie, ks, ke, js, je)
    
    INTEGER, INTENT(IN) :: is, ie, ks, ke, js, je
    REAL, DIMENSION(:), INTENT(IN) :: buffer_linear
    REAL, DIMENSION(is:ie, ks:ke, js:je), INTENT(OUT) :: array_3d
    
    INTEGER :: i, j, k, idx
    INTEGER :: nx, ny, nz
    
    nx = ie - is + 1
    ny = je - js + 1
    nz = ke - ks + 1
    
    ! Convert from C++ linear array to Fortran (i,k,j)
    idx = 1
    DO i = is, ie
        DO j = js, je
            DO k = ks, ke
                array_3d(i, k, j) = buffer_linear(idx)
                idx = idx + 1
            END DO
        END DO
    END DO
    
END SUBROUTINE convert_linear_to_3d

!----------------------------------------------------------------------------------------
! Validate array bounds for safety
!----------------------------------------------------------------------------------------
LOGICAL FUNCTION validate_array_bounds(is, ie, ks, ke, js, je, &
                                     ims, ime, kms, kme, jms, jme)
    
    INTEGER, INTENT(IN) :: is, ie, ks, ke, js, je
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    
    validate_array_bounds = .TRUE.
    
    IF (is < ims .OR. ie > ime) THEN
        CALL wrf_error_fatal("X-dimension bounds error in dynamics interface")
        validate_array_bounds = .FALSE.
    END IF
    
    IF (ks < kms .OR. ke > kme) THEN
        CALL wrf_error_fatal("Z-dimension bounds error in dynamics interface")
        validate_array_bounds = .FALSE.
    END IF
    
    IF (js < jms .OR. je > jme) THEN
        CALL wrf_error_fatal("Y-dimension bounds error in dynamics interface")
        validate_array_bounds = .FALSE.
    END IF
    
END FUNCTION validate_array_bounds

END MODULE module_dynamics_interface