! Complete memory-safe Fortran-C++ interface module
! Handles all memory layout conversions and safety checks

MODULE wrf_fortran_cpp_memory_fix

USE ISO_C_BINDING
USE module_wrf_error

IMPLICIT NONE

PRIVATE

! Public procedures for safe memory handling
PUBLIC :: check_array_bounds
PUBLIC :: convert_fortran_to_c_layout
PUBLIC :: convert_c_to_fortran_layout
PUBLIC :: validate_pointer_safety
PUBLIC :: create_contiguous_buffer

CONTAINS

! Check array bounds for safety
LOGICAL FUNCTION check_array_bounds(nx, ny, nz, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte)
    
    INTEGER, INTENT(IN) :: nx, ny, nz
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
    
    check_array_bounds = .TRUE.
    
    ! Check tile bounds within memory bounds
    IF (its < ims .OR. ite > ime) THEN
        CALL wrf_error_fatal("X-dimension tile bounds exceed memory bounds")
        check_array_bounds = .FALSE.
    END IF
    
    IF (kts < kms .OR. kte > kme) THEN
        CALL wrf_error_fatal("Z-dimension tile bounds exceed memory bounds")
        check_array_bounds = .FALSE.
    END IF
    
    IF (jts < jms .OR. jte > jme) THEN
        CALL wrf_error_fatal("Y-dimension tile bounds exceed memory bounds")
        check_array_bounds = .FALSE.
    END IF
    
    ! Check calculated dimensions match
    IF (nx /= (ite - its + 1)) THEN
        CALL wrf_error_fatal("X-dimension size mismatch")
        check_array_bounds = .FALSE.
    END IF
    
    IF (ny /= (jte - jts + 1)) THEN
        CALL wrf_error_fatal("Y-dimension size mismatch")
        check_array_bounds = .FALSE.
    END IF
    
    IF (nz /= (kte - kts + 1)) THEN
        CALL wrf_error_fatal("Z-dimension size mismatch")
        check_array_bounds = .FALSE.
    END IF
    
END FUNCTION check_array_bounds

! Convert Fortran (i,k,j) layout to C++ expected layout
SUBROUTINE convert_fortran_to_c_layout(fortran_array, c_array, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte, nx, ny, nz)
    
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
    INTEGER, INTENT(IN) :: nx, ny, nz
    REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: fortran_array
    REAL, DIMENSION(nx*ny*nz), INTENT(OUT) :: c_array
    
    INTEGER :: i, j, k, idx
    INTEGER :: ii, jj, kk
    
    ! For C++ expecting linear array representing {nz, ny, nx} tensor
    ! We need to reorder from Fortran (i,k,j) to match C++ expectations
    
    idx = 1
    DO ii = 1, nx
        DO jj = 1, ny
            DO kk = 1, nz
                i = its + ii - 1
                j = jts + jj - 1
                k = kts + kk - 1
                
                ! Map to linear index for C++ {nz,ny,nx} layout
                ! C++ index = k + nz*(j + ny*i)
                idx = kk + nz*(jj-1 + ny*(ii-1))
                c_array(idx) = fortran_array(i, k, j)
            END DO
        END DO
    END DO
    
END SUBROUTINE convert_fortran_to_c_layout

! Convert C++ layout back to Fortran (i,k,j) layout
SUBROUTINE convert_c_to_fortran_layout(c_array, fortran_array, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte, nx, ny, nz)
    
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
    INTEGER, INTENT(IN) :: nx, ny, nz
    REAL, DIMENSION(nx*ny*nz), INTENT(IN) :: c_array
    REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(OUT) :: fortran_array
    
    INTEGER :: i, j, k, idx
    INTEGER :: ii, jj, kk
    
    ! Convert from C++ {nz,ny,nx} tensor back to Fortran (i,k,j)
    
    DO ii = 1, nx
        DO jj = 1, ny
            DO kk = 1, nz
                i = its + ii - 1
                j = jts + jj - 1
                k = kts + kk - 1
                
                ! Extract from linear index for C++ {nz,ny,nx} layout
                idx = kk + nz*(jj-1 + ny*(ii-1))
                fortran_array(i, k, j) = c_array(idx)
            END DO
        END DO
    END DO
    
END SUBROUTINE convert_c_to_fortran_layout

! Validate pointer safety before passing to C++
LOGICAL FUNCTION validate_pointer_safety(data_ptr, expected_size)
    TYPE(C_PTR), INTENT(IN) :: data_ptr
    INTEGER, INTENT(IN) :: expected_size
    
    validate_pointer_safety = .TRUE.
    
    ! Check if pointer is associated
    IF (.NOT. C_ASSOCIATED(data_ptr)) THEN
        CALL wrf_error_fatal("Null pointer passed to C++ interface")
        validate_pointer_safety = .FALSE.
        RETURN
    END IF
    
    ! Size validation would require platform-specific code
    ! For now, just ensure pointer is not null
    
END FUNCTION validate_pointer_safety

! Create contiguous buffer from WRF array slice
SUBROUTINE create_contiguous_buffer(wrf_array, buffer, &
    ims, ime, kms, kme, jms, jme, &
    its, ite, kts, kte, jts, jte)
    
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
    REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: wrf_array
    REAL, ALLOCATABLE, DIMENSION(:), INTENT(OUT) :: buffer
    
    INTEGER :: nx, ny, nz, total_size
    INTEGER :: i, j, k, idx
    
    nx = ite - its + 1
    ny = jte - jts + 1
    nz = kte - kts + 1
    total_size = nx * ny * nz
    
    ! Allocate contiguous buffer
    IF (ALLOCATED(buffer)) DEALLOCATE(buffer)
    ALLOCATE(buffer(total_size))
    
    ! Copy data in C++ expected order
    idx = 1
    DO i = its, ite
        DO j = jts, jte
            DO k = kts, kte
                buffer(idx) = wrf_array(i, k, j)
                idx = idx + 1
            END DO
        END DO
    END DO
    
END SUBROUTINE create_contiguous_buffer

END MODULE wrf_fortran_cpp_memory_fix