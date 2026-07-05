!===============================================================================
! Fortran Interface for Cross-Platform SDIRK3 Solver
! Connects WRF Fortran code to C++ PyTorch implementation
! Supports Apple MPS, NVIDIA CUDA, and CPU backends
!===============================================================================

module sdirk3_interface
    use iso_c_binding
    use module_domain, only : domain
    use module_configure, only : grid_config_rec_type
    use module_state_description
    implicit none
    
    private
    
    ! Public interface routines
    public :: sdirk3_init
    public :: sdirk3_solve_tile
    public :: sdirk3_cleanup
    public :: sdirk3_get_device_info
    public :: sdirk3_sync_device
    
    ! Device preference constants
    integer(c_int), parameter, public :: SDIRK3_DEVICE_AUTO = 0
    integer(c_int), parameter, public :: SDIRK3_DEVICE_MPS = 1
    integer(c_int), parameter, public :: SDIRK3_DEVICE_CUDA = 2
    integer(c_int), parameter, public :: SDIRK3_DEVICE_CPU = 3
    
    ! C++ interface declarations
    interface
        
        ! Initialize SDIRK3 solver with device selection
        subroutine c_sdirk3_init(device_preference, enable_diagnostics) &
            bind(C, name="sdirk3_init")
            import :: c_int, c_bool
            integer(c_int), value :: device_preference
            logical(c_bool), value :: enable_diagnostics
        end subroutine c_sdirk3_init
        
        ! Process single tile with SDIRK3
        subroutine c_sdirk3_solve_tile( &
            ! State variables (Fortran layout)
            u_3d, v_3d, w_3d, t_3d, ph_3d, mu_2d, p_3d, &
            qv_3d, qc_3d, qr_3d, &
            ! Tile bounds (memory)
            ims, ime, jms, jme, kms, kme, &
            ! Tile bounds (computation)
            its, ite, jts, jte, kts, kte, &
            ! Time and configuration
            dt, iteration, &
            ! Newton solver parameters
            newton_tol, max_newton_iter, &
            ! Success flag
            converged) &
            bind(C, name="sdirk3_solve_tile")
            
            import :: c_float, c_int, c_bool
            
            ! State arrays (Fortran ordering: i,k,j)
            real(c_float), dimension(*), intent(inout) :: u_3d
            real(c_float), dimension(*), intent(inout) :: v_3d
            real(c_float), dimension(*), intent(inout) :: w_3d
            real(c_float), dimension(*), intent(inout) :: t_3d
            real(c_float), dimension(*), intent(inout) :: ph_3d
            real(c_float), dimension(*), intent(inout) :: mu_2d
            real(c_float), dimension(*), intent(inout) :: p_3d
            real(c_float), dimension(*), intent(inout) :: qv_3d
            real(c_float), dimension(*), intent(inout) :: qc_3d
            real(c_float), dimension(*), intent(inout) :: qr_3d
            
            ! Bounds
            integer(c_int), value :: ims, ime, jms, jme, kms, kme
            integer(c_int), value :: its, ite, jts, jte, kts, kte
            
            ! Time and iteration
            real(c_float), value :: dt
            integer(c_int), value :: iteration
            
            ! Solver parameters
            real(c_float), value :: newton_tol
            integer(c_int), value :: max_newton_iter
            
            ! Output
            logical(c_bool), intent(out) :: converged
            
        end subroutine c_sdirk3_solve_tile
        
        ! Get device information
        subroutine c_sdirk3_get_device_info(device_type, device_name, memory_used, memory_total) &
            bind(C, name="sdirk3_get_device_info")
            import :: c_int, c_char, c_size_t
            integer(c_int), intent(out) :: device_type
            character(c_char), dimension(256), intent(out) :: device_name
            integer(c_size_t), intent(out) :: memory_used
            integer(c_size_t), intent(out) :: memory_total
        end subroutine c_sdirk3_get_device_info
        
        ! Synchronize device (wait for all operations to complete)
        subroutine c_sdirk3_sync_device() &
            bind(C, name="sdirk3_sync_device")
        end subroutine c_sdirk3_sync_device
        
        ! Clean up resources
        subroutine c_sdirk3_cleanup() &
            bind(C, name="sdirk3_cleanup")
        end subroutine c_sdirk3_cleanup
        
    end interface
    
contains
    
    !===========================================================================
    ! Initialize SDIRK3 solver
    !===========================================================================
    subroutine sdirk3_init(config, device_pref)
        type(grid_config_rec_type), intent(in) :: config
        integer, intent(in), optional :: device_pref
        
        integer(c_int) :: device_preference
        logical(c_bool) :: enable_diag
        character(len=256) :: device_name_str
        integer :: device_type
        integer(c_size_t) :: mem_used, mem_total
        
        ! Set device preference
        if (present(device_pref)) then
            device_preference = device_pref
        else
            device_preference = SDIRK3_DEVICE_AUTO
        end if
        
        ! Enable diagnostics if requested
        enable_diag = config%sdirk3_diagnostics
        
        ! Initialize C++ solver
        call c_sdirk3_init(device_preference, enable_diag)
        
        ! Get and print device information
        call sdirk3_get_device_info(device_type, device_name_str, mem_used, mem_total)
        
        if (device_type == SDIRK3_DEVICE_MPS) then
            write(0,*) 'SDIRK3: Initialized on Apple MPS device'
        else if (device_type == SDIRK3_DEVICE_CUDA) then
            write(0,*) 'SDIRK3: Initialized on NVIDIA CUDA device'
        else
            write(0,*) 'SDIRK3: Initialized on CPU'
        end if
        
        write(0,'(A,A)') ' Device: ', trim(device_name_str)
        write(0,'(A,I0,A,I0,A)') ' Memory: ', mem_used/1024/1024, ' MB / ', &
                                  mem_total/1024/1024, ' MB'
        
    end subroutine sdirk3_init
    
    !===========================================================================
    ! Solve single tile with SDIRK3
    !===========================================================================
    subroutine sdirk3_solve_tile(grid, &
                                 ims, ime, jms, jme, kms, kme, &
                                 its, ite, jts, jte, kts, kte, &
                                 dt, iteration)
        
        type(domain), intent(inout) :: grid
        integer, intent(in) :: ims, ime, jms, jme, kms, kme
        integer, intent(in) :: its, ite, jts, jte, kts, kte
        real, intent(in) :: dt
        integer, intent(in) :: iteration
        
        logical(c_bool) :: converged
        real(c_float) :: newton_tol
        integer(c_int) :: max_newton_iter
        
        ! Set solver parameters from namelist
        newton_tol = grid%sdirk3_newton_tol
        max_newton_iter = grid%sdirk3_max_newton_iter
        
        ! Call C++ solver
        ! Note: Arrays are passed directly as they're already in Fortran layout
        call c_sdirk3_solve_tile( &
            ! State variables - using WRF grid arrays directly
            grid%u_2(ims,kms,jms), &  ! U-momentum (staggered)
            grid%v_2(ims,kms,jms), &  ! V-momentum (staggered)
            grid%w_2(ims,kms,jms), &  ! W-momentum (staggered)
            grid%t_2(ims,kms,jms), &  ! Potential temperature perturbation
            grid%ph_2(ims,kms,jms), & ! Geopotential perturbation
            grid%mu_2(ims,jms), &     ! Dry air mass (2D)
            grid%p(ims,kms,jms), &    ! Pressure
            grid%moist(ims,kms,jms,P_QV), &  ! Water vapor
            grid%moist(ims,kms,jms,P_QC), &  ! Cloud water
            grid%moist(ims,kms,jms,P_QR), &  ! Rain water
            ! Bounds
            ims, ime, jms, jme, kms, kme, &
            its, ite, jts, jte, kts, kte, &
            ! Time and config
            dt, iteration, &
            newton_tol, max_newton_iter, &
            ! Output
            converged)
        
        ! Check convergence
        if (.not. converged) then
            write(0,*) 'WARNING: SDIRK3 Newton solver did not converge'
            write(0,*) '  Tile: ', its, '-', ite, ',', jts, '-', jte
            write(0,*) '  Iteration: ', iteration
        end if
        
    end subroutine sdirk3_solve_tile
    
    !===========================================================================
    ! Get device information
    !===========================================================================
    subroutine sdirk3_get_device_info(device_type, device_name, mem_used, mem_total)
        integer, intent(out) :: device_type
        character(len=*), intent(out) :: device_name
        integer(kind=8), intent(out) :: mem_used, mem_total
        
        character(c_char), dimension(256) :: c_name
        integer(c_int) :: c_type
        integer(c_size_t) :: c_mem_used, c_mem_total
        integer :: i
        
        ! Get info from C++
        call c_sdirk3_get_device_info(c_type, c_name, c_mem_used, c_mem_total)
        
        ! Convert C string to Fortran
        device_name = ''
        do i = 1, 256
            if (c_name(i) == c_null_char) exit
            device_name(i:i) = c_name(i)
        end do
        
        device_type = c_type
        mem_used = c_mem_used
        mem_total = c_mem_total
        
    end subroutine sdirk3_get_device_info
    
    !===========================================================================
    ! Synchronize device operations
    !===========================================================================
    subroutine sdirk3_sync_device()
        call c_sdirk3_sync_device()
    end subroutine sdirk3_sync_device
    
    !===========================================================================
    ! Clean up resources
    !===========================================================================
    subroutine sdirk3_cleanup()
        write(0,*) 'SDIRK3: Cleaning up device resources'
        call c_sdirk3_cleanup()
    end subroutine sdirk3_cleanup

end module sdirk3_interface

!===============================================================================
! Integration with WRF solve_em
!===============================================================================

subroutine sdirk3_driver(grid, config_flags, &
                        ids, ide, jds, jde, kds, kde, &
                        ims, ime, jms, jme, kms, kme, &
                        its, ite, jts, jte, kts, kte, &
                        dt, iteration)
    
    use module_domain, only : domain
    use module_configure, only : grid_config_rec_type
    use sdirk3_interface
    use module_dm, only : wrf_dm_sum_real
    use module_comm_dm, only : halo_em_sdirk3_sub
    
    implicit none
    
    type(domain), intent(inout) :: grid
    type(grid_config_rec_type), intent(in) :: config_flags
    
    integer, intent(in) :: ids, ide, jds, jde, kds, kde
    integer, intent(in) :: ims, ime, jms, jme, kms, kme
    integer, intent(in) :: its, ite, jts, jte, kts, kte
    real, intent(in) :: dt
    integer, intent(in) :: iteration
    
    logical :: first_call = .true.
    integer :: j_start, j_end
    real :: time_start, time_end
    
    ! Initialize on first call
    if (first_call) then
        call sdirk3_init(config_flags)
        first_call = .false.
    end if
    
    ! Start timing
    call cpu_time(time_start)
    
    ! Exchange halos before SDIRK3 (RSL_LITE handles MPI communication)
#ifdef DM_PARALLEL
    call halo_em_sdirk3_sub(grid, &
                            grid%domdesc, grid%comms, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            its, ite, jts, jte, kts, kte)
#endif
    
    ! Process tile with SDIRK3 (only interior points)
    call sdirk3_solve_tile(grid, &
                          ims, ime, jms, jme, kms, kme, &
                          its, ite, jts, jte, kts, kte, &
                          dt, iteration)
    
    ! Synchronize device before halo exchange
    call sdirk3_sync_device()
    
    ! Exchange halos after SDIRK3
#ifdef DM_PARALLEL
    call halo_em_sdirk3_sub(grid, &
                            grid%domdesc, grid%comms, &
                            ids, ide, jds, jde, kds, kde, &
                            ims, ime, jms, jme, kms, kme, &
                            its, ite, jts, jte, kts, kte)
#endif
    
    ! End timing
    call cpu_time(time_end)
    
    ! Report performance every 10 steps
    if (mod(iteration, 10) == 0) then
        write(0,'(A,I5,A,F8.3,A)') 'SDIRK3 step ', iteration, &
                                   ' completed in ', (time_end - time_start)*1000, ' ms'
    end if
    
end subroutine sdirk3_driver