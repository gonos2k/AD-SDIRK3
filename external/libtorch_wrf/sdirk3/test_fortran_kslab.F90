! test_fortran_kslab.F90
! Example of Fortran code with k-slab optimization directives
! Shows how existing WSM6 can be optimized with minimal changes

program test_fortran_kslab
    implicit none
    
    integer, parameter :: nx = 100, ny = 100, nz = 50
    real, dimension(nx,nz,ny) :: qc, qr, rho
    real, dimension(nx,nz,ny) :: dqcdt, dqrdt
    real :: k_auto, qc0, dt
    integer :: i, j, k
    real :: start_time, end_time
    
    ! Initialize test data
    k_auto = 1.0e-3
    qc0 = 1.0e-3
    dt = 1.0
    
    do j = 1, ny
        do k = 1, nz
            do i = 1, nx
                qc(i,k,j) = 0.002 * (1.0 + 0.1*sin(real(i+k+j)))
                rho(i,k,j) = 1.2
                dqcdt(i,k,j) = 0.0
                dqrdt(i,k,j) = 0.0
            end do
        end do
    end do
    
    ! Method 1: Original Fortran (j-slab)
    call cpu_time(start_time)
    call wsm6_original(qc, qr, rho, dqcdt, dqrdt, k_auto, qc0, dt, nx, ny, nz)
    call cpu_time(end_time)
    print *, "Original Fortran time:", end_time - start_time, "seconds"
    
    ! Method 2: Fortran with compiler vectorization hints
    call cpu_time(start_time)
    call wsm6_vectorized(qc, qr, rho, dqcdt, dqrdt, k_auto, qc0, dt, nx, ny, nz)
    call cpu_time(end_time)
    print *, "Vectorized Fortran time:", end_time - start_time, "seconds"
    
    ! Method 3: Fortran with k-slab blocking
    call cpu_time(start_time)
    call wsm6_kslab(qc, qr, rho, dqcdt, dqrdt, k_auto, qc0, dt, nx, ny, nz)
    call cpu_time(end_time)
    print *, "K-slab Fortran time:", end_time - start_time, "seconds"
    
contains

    ! Original implementation (typical WRF style)
    subroutine wsm6_original(qc, qr, rho, dqcdt, dqrdt, k_auto, qc0, dt, nx, ny, nz)
        integer, intent(in) :: nx, ny, nz
        real, intent(in) :: k_auto, qc0, dt
        real, dimension(nx,nz,ny), intent(in) :: qc, rho
        real, dimension(nx,nz,ny), intent(inout) :: qr, dqcdt, dqrdt
        
        integer :: i, j, k
        real :: excess, rate
        
        do j = 1, ny
            do k = 1, nz
                do i = 1, nx
                    if (qc(i,k,j) > qc0) then
                        excess = qc(i,k,j) - qc0
                        rate = k_auto * excess * rho(i,k,j)
                        dqcdt(i,k,j) = dqcdt(i,k,j) - rate
                        dqrdt(i,k,j) = dqrdt(i,k,j) + rate
                    end if
                end do
            end do
        end do
    end subroutine wsm6_original
    
    ! Vectorized version with compiler hints
    subroutine wsm6_vectorized(qc, qr, rho, dqcdt, dqrdt, k_auto, qc0, dt, nx, ny, nz)
        integer, intent(in) :: nx, ny, nz
        real, intent(in) :: k_auto, qc0, dt
        real, dimension(nx,nz,ny), intent(in) :: qc, rho
        real, dimension(nx,nz,ny), intent(inout) :: qr, dqcdt, dqrdt
        
        integer :: i, j, k
        real :: excess, rate
        
        ! Process j-slabs with vectorization hints
        do j = 1, ny
            !DIR$ IVDEP
            !DIR$ VECTOR ALWAYS
            !$OMP SIMD
            do k = 1, nz
                do i = 1, nx
                    excess = max(qc(i,k,j) - qc0, 0.0)
                    rate = k_auto * excess * rho(i,k,j)
                    dqcdt(i,k,j) = dqcdt(i,k,j) - rate
                    dqrdt(i,k,j) = dqrdt(i,k,j) + rate
                end do
            end do
        end do
    end subroutine wsm6_vectorized
    
    ! K-slab optimized version
    subroutine wsm6_kslab(qc, qr, rho, dqcdt, dqrdt, k_auto, qc0, dt, nx, ny, nz)
        integer, intent(in) :: nx, ny, nz
        real, intent(in) :: k_auto, qc0, dt
        real, dimension(nx,nz,ny), intent(in) :: qc, rho
        real, dimension(nx,nz,ny), intent(inout) :: qr, dqcdt, dqrdt
        
        integer :: i, j, k, kk, k_start, k_end
        integer, parameter :: K_SLAB_SIZE = 8
        real :: excess, rate
        
        ! Process in k-slabs for better cache usage
        do j = 1, ny
            do k_start = 1, nz, K_SLAB_SIZE
                k_end = min(k_start + K_SLAB_SIZE - 1, nz)
                
                ! Process k-slab with vectorization
                !DIR$ IVDEP
                !$OMP SIMD COLLAPSE(2)
                do kk = k_start, k_end
                    do i = 1, nx
                        excess = max(qc(i,kk,j) - qc0, 0.0)
                        rate = k_auto * excess * rho(i,kk,j)
                        dqcdt(i,kk,j) = dqcdt(i,kk,j) - rate
                        dqrdt(i,kk,j) = dqrdt(i,kk,j) + rate
                    end do
                end do
            end do
        end do
    end subroutine wsm6_kslab

end program test_fortran_kslab