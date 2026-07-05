MODULE module_libtorch_acoustic_interface

! Interface module for LibTorch acoustic wave processing
! Connects Fortran routines with C++ LibTorch implementation
! Fixed version with correct memory layout and function names

USE ISO_C_BINDING
USE module_wrf_error

IMPLICIT NONE

PRIVATE
PUBLIC :: libtorch_acoustic_interface_init
PUBLIC :: libtorch_acoustic_interface_finalize
PUBLIC :: libtorch_acoustic_advance_uv_c
PUBLIC :: libtorch_acoustic_compute_gradients_c
PUBLIC :: libtorch_acoustic_optimize_timestep_c

! C interface declarations with correct function names
INTERFACE
    SUBROUTINE libtorch_acoustic_initialize_c(mode) BIND(C, name="libtorch_acoustic_initialize")
        USE ISO_C_BINDING
        INTEGER(C_INT), VALUE :: mode
    END SUBROUTINE libtorch_acoustic_initialize_c

    SUBROUTINE libtorch_acoustic_finalize_c() BIND(C, name="libtorch_acoustic_finalize")
        USE ISO_C_BINDING
    END SUBROUTINE libtorch_acoustic_finalize_c

    SUBROUTINE libtorch_acoustic_advance_uv_ad_c( &
        u_data, v_data, p_data, ru_tend_data, rv_tend_data, &
        nx, ny, nz, dt, rdx, rdy, mode) BIND(C, name="libtorch_acoustic_advance_uv_ad")
        USE ISO_C_BINDING
        TYPE(C_PTR), VALUE :: u_data, v_data, p_data, ru_tend_data, rv_tend_data
        INTEGER(C_INT), VALUE :: nx, ny, nz, mode
        REAL(C_FLOAT), VALUE :: dt, rdx, rdy
    END SUBROUTINE libtorch_acoustic_advance_uv_ad_c

    SUBROUTINE libtorch_compute_acoustic_gradients_c( &
        u_data, v_data, p_data, grad_u, grad_v, grad_p, nx, ny, nz) &
        BIND(C, name="libtorch_compute_acoustic_gradients")
        USE ISO_C_BINDING
        TYPE(C_PTR), VALUE :: u_data, v_data, p_data, grad_u, grad_v, grad_p
        INTEGER(C_INT), VALUE :: nx, ny, nz
    END SUBROUTINE libtorch_compute_acoustic_gradients_c

    REAL(C_FLOAT) FUNCTION libtorch_optimize_acoustic_timestep_c( &
        u_data, v_data, p_data, nx, ny, nz, current_dt, rdx, rdy) &
        BIND(C, name="libtorch_optimize_acoustic_timestep")
        USE ISO_C_BINDING
        TYPE(C_PTR), VALUE :: u_data, v_data, p_data
        INTEGER(C_INT), VALUE :: nx, ny, nz
        REAL(C_FLOAT), VALUE :: current_dt, rdx, rdy
    END FUNCTION libtorch_optimize_acoustic_timestep_c
END INTERFACE

CONTAINS

! Initialize LibTorch acoustic processing
SUBROUTINE libtorch_acoustic_interface_init(mode)
    IMPLICIT NONE
    INTEGER, INTENT(IN) :: mode
    
    CALL libtorch_acoustic_initialize_c(mode)
    
    CALL wrf_message("LibTorch acoustic interface initialized")
END SUBROUTINE libtorch_acoustic_interface_init

! Finalize LibTorch acoustic processing
SUBROUTINE libtorch_acoustic_interface_finalize()
    IMPLICIT NONE
    
    CALL libtorch_acoustic_finalize_c()
    
    CALL wrf_message("LibTorch acoustic interface finalized")
END SUBROUTINE libtorch_acoustic_interface_finalize

! Main acoustic advance with automatic differentiation
SUBROUTINE libtorch_acoustic_advance_uv_c(u, v, p, ru_tend, rv_tend, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte, &
    dt, rdx, rdy, mode)

    IMPLICIT NONE
    
    ! Arguments
    INTEGER, INTENT(IN) :: ims,ime, kms,kme, jms,jme
    INTEGER, INTENT(IN) :: its,ite, kts,kte, jts,jte, mode
    REAL, INTENT(INOUT), DIMENSION(ims:ime, kms:kme, jms:jme) :: u, v, p
    REAL, INTENT(IN), DIMENSION(ims:ime, kms:kme, jms:jme) :: ru_tend, rv_tend
    REAL, INTENT(IN) :: dt, rdx, rdy
    
    ! Local variables
    INTEGER :: nx, ny, nz
    ! Fixed: Array dimensions must match C++ expectation: {nz, ny, nx}
    REAL, ALLOCATABLE, TARGET :: u_local(:,:,:), v_local(:,:,:), p_local(:,:,:)
    REAL, ALLOCATABLE, TARGET :: ru_tend_local(:,:,:), rv_tend_local(:,:,:)
    TYPE(C_PTR) :: u_ptr, v_ptr, p_ptr, ru_tend_ptr, rv_tend_ptr
    
    ! Calculate array dimensions
    nx = ite - its + 1
    ny = jte - jts + 1
    nz = kte - kts + 1
    
    ! Allocate contiguous arrays with C++ expected layout
    ! C++ expects: {nz, ny, nx} with row-major storage
    ! Fortran allocates as column-major, so we reverse: (nx, ny, nz)
    ALLOCATE(u_local(nx, ny, nz))
    ALLOCATE(v_local(nx, ny, nz))
    ALLOCATE(p_local(nx, ny, nz))
    ALLOCATE(ru_tend_local(nx, ny, nz))
    ALLOCATE(rv_tend_local(nx, ny, nz))
    
    ! Copy WRF arrays to contiguous format with correct layout
    CALL copy_wrf_to_contiguous_3d_fixed(u, u_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(v, v_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(p, p_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(ru_tend, ru_tend_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(rv_tend, rv_tend_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    
    ! Get C pointers
    u_ptr = C_LOC(u_local(1,1,1))
    v_ptr = C_LOC(v_local(1,1,1))
    p_ptr = C_LOC(p_local(1,1,1))
    ru_tend_ptr = C_LOC(ru_tend_local(1,1,1))
    rv_tend_ptr = C_LOC(rv_tend_local(1,1,1))
    
    ! Call LibTorch routine
    CALL libtorch_acoustic_advance_uv_ad_c(u_ptr, v_ptr, p_ptr, &
        ru_tend_ptr, rv_tend_ptr, nx, ny, nz, dt, rdx, rdy, mode)
    
    ! Copy results back to WRF arrays
    CALL copy_contiguous_to_wrf_3d_fixed(u_local, u, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_contiguous_to_wrf_3d_fixed(v_local, v, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_contiguous_to_wrf_3d_fixed(p_local, p, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    
    ! Cleanup
    DEALLOCATE(u_local, v_local, p_local, ru_tend_local, rv_tend_local)
    
END SUBROUTINE libtorch_acoustic_advance_uv_c

! Compute gradients for optimization
SUBROUTINE libtorch_acoustic_compute_gradients_c(u, v, p, grad_u, grad_v, grad_p, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte)

    IMPLICIT NONE
    
    ! Arguments
    INTEGER, INTENT(IN) :: ims,ime, kms,kme, jms,jme
    INTEGER, INTENT(IN) :: its,ite, kts,kte, jts,jte
    REAL, INTENT(IN), DIMENSION(ims:ime, kms:kme, jms:jme) :: u, v, p
    REAL, INTENT(OUT), DIMENSION(ims:ime, kms:kme, jms:jme) :: grad_u, grad_v, grad_p
    
    ! Local variables
    INTEGER :: nx, ny, nz
    REAL, ALLOCATABLE, TARGET :: u_local(:,:,:), v_local(:,:,:), p_local(:,:,:)
    REAL, ALLOCATABLE, TARGET :: grad_u_local(:,:,:), grad_v_local(:,:,:), grad_p_local(:,:,:)
    TYPE(C_PTR) :: u_ptr, v_ptr, p_ptr, grad_u_ptr, grad_v_ptr, grad_p_ptr
    
    ! Calculate array dimensions
    nx = ite - its + 1
    ny = jte - jts + 1
    nz = kte - kts + 1
    
    ! Allocate contiguous arrays with correct layout
    ALLOCATE(u_local(nx, ny, nz))
    ALLOCATE(v_local(nx, ny, nz))
    ALLOCATE(p_local(nx, ny, nz))
    ALLOCATE(grad_u_local(nx, ny, nz))
    ALLOCATE(grad_v_local(nx, ny, nz))
    ALLOCATE(grad_p_local(nx, ny, nz))
    
    ! Initialize gradient arrays
    grad_u_local = 0.0
    grad_v_local = 0.0
    grad_p_local = 0.0
    
    ! Copy WRF arrays to contiguous format
    CALL copy_wrf_to_contiguous_3d_fixed(u, u_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(v, v_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(p, p_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    
    ! Get C pointers
    u_ptr = C_LOC(u_local(1,1,1))
    v_ptr = C_LOC(v_local(1,1,1))
    p_ptr = C_LOC(p_local(1,1,1))
    grad_u_ptr = C_LOC(grad_u_local(1,1,1))
    grad_v_ptr = C_LOC(grad_v_local(1,1,1))
    grad_p_ptr = C_LOC(grad_p_local(1,1,1))
    
    ! Call LibTorch routine
    CALL libtorch_compute_acoustic_gradients_c(u_ptr, v_ptr, p_ptr, &
        grad_u_ptr, grad_v_ptr, grad_p_ptr, nx, ny, nz)
    
    ! Copy results back to WRF arrays
    CALL copy_contiguous_to_wrf_3d_fixed(grad_u_local, grad_u, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_contiguous_to_wrf_3d_fixed(grad_v_local, grad_v, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_contiguous_to_wrf_3d_fixed(grad_p_local, grad_p, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    
    ! Cleanup
    DEALLOCATE(u_local, v_local, p_local)
    DEALLOCATE(grad_u_local, grad_v_local, grad_p_local)
    
END SUBROUTINE libtorch_acoustic_compute_gradients_c

! Optimize acoustic timestep
FUNCTION libtorch_acoustic_optimize_timestep_c(u, v, p, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte, &
    current_dt, rdx, rdy) RESULT(optimal_dt)

    IMPLICIT NONE
    
    ! Arguments
    INTEGER, INTENT(IN) :: ims,ime, kms,kme, jms,jme
    INTEGER, INTENT(IN) :: its,ite, kts,kte, jts,jte
    REAL, INTENT(IN), DIMENSION(ims:ime, kms:kme, jms:jme) :: u, v, p
    REAL, INTENT(IN) :: current_dt, rdx, rdy
    REAL :: optimal_dt
    
    ! Local variables
    INTEGER :: nx, ny, nz
    REAL, ALLOCATABLE, TARGET :: u_local(:,:,:), v_local(:,:,:), p_local(:,:,:)
    TYPE(C_PTR) :: u_ptr, v_ptr, p_ptr
    
    ! Calculate array dimensions
    nx = ite - its + 1
    ny = jte - jts + 1
    nz = kte - kts + 1
    
    ! Allocate contiguous arrays
    ALLOCATE(u_local(nx, ny, nz))
    ALLOCATE(v_local(nx, ny, nz))
    ALLOCATE(p_local(nx, ny, nz))
    
    ! Copy WRF arrays to contiguous format
    CALL copy_wrf_to_contiguous_3d_fixed(u, u_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(v, v_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    CALL copy_wrf_to_contiguous_3d_fixed(p, p_local, &
        ims, ime, kms, kme, jms, jme, &
        its, ite, kts, kte, jts, jte, nx, ny, nz)
    
    ! Get C pointers
    u_ptr = C_LOC(u_local(1,1,1))
    v_ptr = C_LOC(v_local(1,1,1))
    p_ptr = C_LOC(p_local(1,1,1))
    
    ! Call LibTorch routine
    optimal_dt = libtorch_optimize_acoustic_timestep_c( &
        u_ptr, v_ptr, p_ptr, nx, ny, nz, current_dt, rdx, rdy)
    
    DEALLOCATE(u_local, v_local, p_local)
    
END FUNCTION libtorch_acoustic_optimize_timestep_c

! Helper routines for array copying with fixed memory layout

! Fixed version: Properly handle Fortran to C++ memory layout conversion
SUBROUTINE copy_wrf_to_contiguous_3d_fixed(wrf_array, cont_array, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte, nx, ny, nz)

    IMPLICIT NONE
    
    INTEGER, INTENT(IN) :: ims,ime, kms,kme, jms,jme
    INTEGER, INTENT(IN) :: its,ite, kts,kte, jts,jte, nx, ny, nz
    REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: wrf_array
    REAL, DIMENSION(nx, ny, nz), INTENT(OUT) :: cont_array
    
    INTEGER :: i, j, k, ii, jj, kk
    
    ! Copy data maintaining proper order for C++ interpretation
    ! WRF: (i,k,j) -> Contiguous: (x,y,z) for C++ {nz,ny,nx}
    DO j = jts, jte
        jj = j - jts + 1
        DO k = kts, kte
            kk = k - kts + 1
            DO i = its, ite
                ii = i - its + 1
                ! Map to match C++ expectation when interpreted as {nz,ny,nx}
                cont_array(ii, jj, kk) = wrf_array(i, k, j)
            END DO
        END DO
    END DO

END SUBROUTINE copy_wrf_to_contiguous_3d_fixed

! Copy data from contiguous array back to WRF array format
SUBROUTINE copy_contiguous_to_wrf_3d_fixed(cont_array, wrf_array, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte, nx, ny, nz)

    IMPLICIT NONE
    
    INTEGER, INTENT(IN) :: ims,ime, kms,kme, jms,jme
    INTEGER, INTENT(IN) :: its,ite, kts,kte, jts,jte, nx, ny, nz
    REAL, DIMENSION(nx, ny, nz), INTENT(IN) :: cont_array
    REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(OUT) :: wrf_array
    
    INTEGER :: i, j, k, ii, jj, kk
    
    ! Copy data back maintaining proper order
    DO j = jts, jte
        jj = j - jts + 1
        DO k = kts, kte
            kk = k - kts + 1
            DO i = its, ite
                ii = i - its + 1
                wrf_array(i, k, j) = cont_array(ii, jj, kk)
            END DO
        END DO
    END DO

END SUBROUTINE copy_contiguous_to_wrf_3d_fixed

END MODULE module_libtorch_acoustic_interface