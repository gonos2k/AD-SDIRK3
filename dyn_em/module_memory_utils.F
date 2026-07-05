!WRF:MODEL_LAYER:UTIL
!
MODULE module_memory_utils
!
! Purpose: Memory management utilities for Fortran-C++ interoperability
!          Provides safe memory layout conversion and validation
!
! Author: WRF Development Team
! Date: 2025-06-15
!

USE module_wrf_error

IMPLICIT NONE

PRIVATE

! Public memory management procedures
PUBLIC :: memory_check_bounds
PUBLIC :: memory_check_size
PUBLIC :: memory_copy_3d_to_1d
PUBLIC :: memory_copy_1d_to_3d
PUBLIC :: memory_create_buffer
PUBLIC :: memory_destroy_buffer

CONTAINS

!----------------------------------------------------------------------------------------
! Check array bounds for safety
!----------------------------------------------------------------------------------------
LOGICAL FUNCTION memory_check_bounds(nx, ny, nz, &
                                   is, ie, ks, ke, js, je, &
                                   ims, ime, kms, kme, jms, jme) RESULT(valid)
    
    INTEGER, INTENT(IN) :: nx, ny, nz
    INTEGER, INTENT(IN) :: is, ie, ks, ke, js, je
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    
    CHARACTER(LEN=256) :: message
    
    valid = .TRUE.
    
    ! Check tile bounds within memory bounds
    IF (is < ims .OR. ie > ime) THEN
        WRITE(message,*) "X-bounds error: tile [", is, ",", ie, "] exceeds memory [", ims, ",", ime, "]"
        CALL wrf_error_fatal(TRIM(message))
        valid = .FALSE.
        RETURN
    END IF
    
    IF (ks < kms .OR. ke > kme) THEN
        WRITE(message,*) "Z-bounds error: tile [", ks, ",", ke, "] exceeds memory [", kms, ",", kme, "]"
        CALL wrf_error_fatal(TRIM(message))
        valid = .FALSE.
        RETURN
    END IF
    
    IF (js < jms .OR. je > jme) THEN
        WRITE(message,*) "Y-bounds error: tile [", js, ",", je, "] exceeds memory [", jms, ",", jme, "]"
        CALL wrf_error_fatal(TRIM(message))
        valid = .FALSE.
        RETURN
    END IF
    
    ! Check calculated dimensions
    IF (nx /= (ie - is + 1)) THEN
        WRITE(message,*) "X-dimension mismatch: nx=", nx, " but ie-is+1=", (ie-is+1)
        CALL wrf_error_fatal(TRIM(message))
        valid = .FALSE.
        RETURN
    END IF
    
    IF (ny /= (je - js + 1)) THEN
        WRITE(message,*) "Y-dimension mismatch: ny=", ny, " but je-js+1=", (je-js+1)
        CALL wrf_error_fatal(TRIM(message))
        valid = .FALSE.
        RETURN
    END IF
    
    IF (nz /= (ke - ks + 1)) THEN
        WRITE(message,*) "Z-dimension mismatch: nz=", nz, " but ke-ks+1=", (ke-ks+1)
        CALL wrf_error_fatal(TRIM(message))
        valid = .FALSE.
        RETURN
    END IF
    
END FUNCTION memory_check_bounds

!----------------------------------------------------------------------------------------
! Check array size validity
!----------------------------------------------------------------------------------------
LOGICAL FUNCTION memory_check_size(array_size, expected_size) RESULT(valid)
    
    INTEGER, INTENT(IN) :: array_size
    INTEGER, INTENT(IN) :: expected_size
    
    CHARACTER(LEN=256) :: message
    
    valid = .TRUE.
    
    IF (array_size /= expected_size) THEN
        WRITE(message,*) "Array size mismatch: ", array_size, " vs expected ", expected_size
        CALL wrf_error_fatal(TRIM(message))
        valid = .FALSE.
        RETURN
    END IF
    
    IF (expected_size <= 0) THEN
        CALL wrf_error_fatal("Invalid size in memory operation")
        valid = .FALSE.
        RETURN
    END IF
    
END FUNCTION memory_check_size

!----------------------------------------------------------------------------------------
! Copy 3D Fortran array to 1D C++ buffer
! Handles Fortran column-major (i,k,j) to C++ row-major layout
!----------------------------------------------------------------------------------------
SUBROUTINE memory_copy_3d_to_1d(array_3d, buffer_1d, &
                                ims, ime, kms, kme, jms, jme, &
                                its, ite, kts, kte, jts, jte)
    
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
    REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: array_3d
    REAL, DIMENSION(:), INTENT(OUT) :: buffer_1d
    
    INTEGER :: i, j, k, idx
    INTEGER :: nx, ny, nz
    INTEGER :: ii, jj, kk
    
    nx = ite - its + 1
    ny = jte - jts + 1
    nz = kte - kts + 1
    
    ! Copy with layout transformation
    ! Fortran (i,k,j) -> C++ linear for {nz,ny,nx}
    idx = 1
    DO ii = 1, nx
        DO jj = 1, ny
            DO kk = 1, nz
                i = its + ii - 1
                j = jts + jj - 1
                k = kts + kk - 1
                
                ! Map to C++ expected layout
                buffer_1d(idx) = array_3d(i, k, j)
                idx = idx + 1
            END DO
        END DO
    END DO
    
END SUBROUTINE memory_copy_3d_to_1d

!----------------------------------------------------------------------------------------
! Copy 1D C++ buffer to 3D Fortran array
! Handles C++ row-major to Fortran column-major (i,k,j) layout
!----------------------------------------------------------------------------------------
SUBROUTINE memory_copy_1d_to_3d(buffer_1d, array_3d, &
                                ims, ime, kms, kme, jms, jme, &
                                its, ite, kts, kte, jts, jte)
    
    INTEGER, INTENT(IN) :: ims, ime, kms, kme, jms, jme
    INTEGER, INTENT(IN) :: its, ite, kts, kte, jts, jte
    REAL, DIMENSION(:), INTENT(IN) :: buffer_1d
    REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(OUT) :: array_3d
    
    INTEGER :: i, j, k, idx
    INTEGER :: nx, ny, nz
    INTEGER :: ii, jj, kk
    
    nx = ite - its + 1
    ny = jte - jts + 1
    nz = kte - kts + 1
    
    ! Copy with layout transformation
    ! C++ linear {nz,ny,nx} -> Fortran (i,k,j)
    idx = 1
    DO ii = 1, nx
        DO jj = 1, ny
            DO kk = 1, nz
                i = its + ii - 1
                j = jts + jj - 1
                k = kts + kk - 1
                
                array_3d(i, k, j) = buffer_1d(idx)
                idx = idx + 1
            END DO
        END DO
    END DO
    
END SUBROUTINE memory_copy_1d_to_3d

!----------------------------------------------------------------------------------------
! Create aligned buffer for C++ interface
!----------------------------------------------------------------------------------------
SUBROUTINE memory_create_buffer(buffer, size, alignment)
    
    REAL, ALLOCATABLE, DIMENSION(:), INTENT(OUT) :: buffer
    INTEGER, INTENT(IN) :: size
    INTEGER, INTENT(IN), OPTIONAL :: alignment
    
    INTEGER :: align_size
    
    ! Default alignment for cache efficiency
    align_size = 64  ! 64-byte alignment for AVX-512
    IF (PRESENT(alignment)) align_size = alignment
    
    ! Allocate buffer
    IF (ALLOCATED(buffer)) DEALLOCATE(buffer)
    ALLOCATE(buffer(size))
    
    ! Initialize to zero for safety
    buffer = 0.0
    
END SUBROUTINE memory_create_buffer

!----------------------------------------------------------------------------------------
! Destroy buffer and free memory
!----------------------------------------------------------------------------------------
SUBROUTINE memory_destroy_buffer(buffer)
    
    REAL, ALLOCATABLE, DIMENSION(:), INTENT(INOUT) :: buffer
    
    IF (ALLOCATED(buffer)) THEN
        DEALLOCATE(buffer)
    END IF
    
END SUBROUTINE memory_destroy_buffer

END MODULE module_memory_utils