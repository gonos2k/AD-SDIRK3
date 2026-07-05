! jvp_bridge.F90 - WRF-Fortran to libtorch JVP bridge
module jvp_bridge
  use iso_c_binding
  implicit none
  
  ! Bit flags for options
  integer(c_int), parameter :: JVP_FREEZE_LIMITERS = 1  ! 1 << 0
  integer(c_int), parameter :: JVP_FREEZE_SWITCHES = 2  ! 1 << 1
  integer(c_int), parameter :: JVP_USE_SMOOTHING   = 4  ! 1 << 2
  
  ! Module identifiers matching C++ enum
  integer(c_int), parameter :: MOD_DYCORE_ADV   = 0
  integer(c_int), parameter :: MOD_DYCORE_PG    = 1
  integer(c_int), parameter :: MOD_DYCORE_COR   = 2
  integer(c_int), parameter :: MOD_DYCORE_DIFF  = 3
  integer(c_int), parameter :: MOD_LATERAL_BC   = 4
  integer(c_int), parameter :: MOD_COUPLER      = 5
  integer(c_int), parameter :: MOD_SURFACE      = 6
  integer(c_int), parameter :: MOD_LAND_SURFACE = 7
  integer(c_int), parameter :: MOD_PBL          = 8
  integer(c_int), parameter :: MOD_MICROPHYSICS = 9
  integer(c_int), parameter :: MOD_CUMULUS      = 10
  integer(c_int), parameter :: MOD_RADIATION    = 11
  integer(c_int), parameter :: MOD_GWD          = 12
  integer(c_int), parameter :: MOD_CHEMISTRY    = 13

  interface
    ! Initialize libtorch
    subroutine lt_init(device, precision) bind(C, name="lt_init")
      import :: c_int
      integer(c_int), value :: device   ! 0:CPU, 1:GPU0, ...
      integer(c_int), value :: precision ! 32 or 64
    end subroutine lt_init

    ! Finalize libtorch
    subroutine lt_finalize() bind(C, name="lt_finalize")
    end subroutine lt_finalize

    ! AD-JVP computation via libtorch
    subroutine lt_jvp_residual_ad(nvar, nx, ny, nz,     &
         U, v, jrv, dx, dy, dz, mu, module_id, opts) &
         bind(C, name="lt_jvp_residual_ad")
      import :: c_int, c_float
      integer(c_int), value :: nvar, nx, ny, nz
      real(c_float) :: U(nvar, nx, ny, nz)
      real(c_float) :: v(nvar, nx, ny, nz)
      real(c_float) :: jrv(nvar, nx, ny, nz)
      real(c_float) :: dx(nx), dy(ny), dz(nz)
      real(c_float) :: mu(nx, ny)          ! dry air mass
      integer(c_int), value :: module_id   ! which WRF module
      integer(c_int), value :: opts        ! bit flags
    end subroutine lt_jvp_residual_ad

    ! Get recommended epsilon for FD-JVP
    function lt_get_fd_epsilon(U, v, nvar, nx, ny, nz, is_central) &
         bind(C, name="lt_get_fd_epsilon") result(eps)
      import :: c_int, c_float
      real(c_float) :: U(nvar, nx, ny, nz)
      real(c_float) :: v(nvar, nx, ny, nz)
      integer(c_int), value :: nvar, nx, ny, nz
      integer(c_int), value :: is_central  ! 0: forward, 1: central
      real(c_float) :: eps
    end function lt_get_fd_epsilon
  end interface

  ! Grid type for WRF
  type :: grid_t
    integer :: nx, ny, nz, nvar
    real(c_float), allocatable :: dx(:), dy(:), dz(:)
    real(c_float), allocatable :: mu(:,:)  ! dry air mass
    real(c_float), allocatable :: msfuy(:,:), msfvx(:,:)  ! map scale factors
  end type grid_t

contains

  !-----------------------------------------------------------------
  ! Apply J*v using AD-JVP (via libtorch)
  !-----------------------------------------------------------------
  subroutine apply_J_times_v_ad(U, v, Jv, dt, gamma, grid, module_id, opts)
    real(c_float), intent(in)  :: U(:,:,:,:), v(:,:,:,:)
    real(c_float), intent(out) :: Jv(:,:,:,:)
    real(c_float), intent(in)  :: dt, gamma
    type(grid_t), intent(in)   :: grid
    integer(c_int), intent(in) :: module_id, opts
    
    real(c_float), allocatable :: jrv(:,:,:,:)
    
    allocate(jrv, mold=U)
    
    ! Call libtorch for JVP computation
    call lt_jvp_residual_ad(grid%nvar, grid%nx, grid%ny, grid%nz, &
         U, v, jrv, grid%dx, grid%dy, grid%dz, grid%mu, &
         module_id, opts)
    
    ! Apply Newton operator: J*v = v - gamma*dt*JVP_R
    Jv = v - gamma * dt * jrv
    
    deallocate(jrv)
  end subroutine apply_J_times_v_ad

  !-----------------------------------------------------------------
  ! Apply J*v using FD-JVP (central difference)
  !-----------------------------------------------------------------
  subroutine apply_J_times_v_fd_central(U, v, Jv, dt, gamma, grid, &
                                        module_id, freeze_limiters)
    real(c_float), intent(in)  :: U(:,:,:,:), v(:,:,:,:)
    real(c_float), intent(out) :: Jv(:,:,:,:)
    real(c_float), intent(in)  :: dt, gamma
    type(grid_t), intent(in)   :: grid
    integer(c_int), intent(in) :: module_id
    logical, intent(in) :: freeze_limiters
    
    real(c_float) :: eps
    real(c_float), allocatable :: Rp(:,:,:,:), Rm(:,:,:,:)
    real(c_float), allocatable :: Up(:,:,:,:), Um(:,:,:,:)
    
    ! Get optimal epsilon for central difference
    eps = lt_get_fd_epsilon(U, v, grid%nvar, grid%nx, grid%ny, grid%nz, 1_c_int)
    
    allocate(Rp, mold=U)
    allocate(Rm, mold=U)
    allocate(Up, mold=U)
    allocate(Um, mold=U)
    
    ! Perturb state
    Up = U + eps * v
    Um = U - eps * v
    
    ! Evaluate residual at perturbed states
    call wrf_residual_flux_form(Up, Rp, grid, module_id, freeze_limiters)
    call wrf_residual_flux_form(Um, Rm, grid, module_id, freeze_limiters)
    
    ! Central difference: JVP = (R(U+εv) - R(U-εv))/(2ε)
    Jv = v - gamma * dt * (Rp - Rm) / (2.0_c_float * eps)
    
    deallocate(Rp, Rm, Up, Um)
  end subroutine apply_J_times_v_fd_central

  !-----------------------------------------------------------------
  ! Apply J*v using FD-JVP (forward difference)
  !-----------------------------------------------------------------
  subroutine apply_J_times_v_fd_forward(U, v, Jv, dt, gamma, grid, &
                                        module_id, R_base, freeze_limiters)
    real(c_float), intent(in)  :: U(:,:,:,:), v(:,:,:,:)
    real(c_float), intent(out) :: Jv(:,:,:,:)
    real(c_float), intent(in)  :: dt, gamma
    type(grid_t), intent(in)   :: grid
    integer(c_int), intent(in) :: module_id
    real(c_float), intent(inout) :: R_base(:,:,:,:)  ! cached base residual
    logical, intent(in) :: freeze_limiters
    
    real(c_float) :: eps
    real(c_float), allocatable :: Rp(:,:,:,:), Up(:,:,:,:)
    logical :: need_base_eval
    
    ! Check if we need to compute base residual
    need_base_eval = (maxval(abs(R_base)) < 1.0e-30_c_float)
    
    ! Get optimal epsilon for forward difference
    eps = lt_get_fd_epsilon(U, v, grid%nvar, grid%nx, grid%ny, grid%nz, 0_c_int)
    
    allocate(Rp, mold=U)
    allocate(Up, mold=U)
    
    ! Compute base residual if needed
    if (need_base_eval) then
      call wrf_residual_flux_form(U, R_base, grid, module_id, freeze_limiters)
    endif
    
    ! Perturb state
    Up = U + eps * v
    
    ! Evaluate residual at perturbed state
    call wrf_residual_flux_form(Up, Rp, grid, module_id, freeze_limiters)
    
    ! Forward difference: JVP = (R(U+εv) - R(U))/ε
    Jv = v - gamma * dt * (Rp - R_base) / eps
    
    deallocate(Rp, Up)
  end subroutine apply_J_times_v_fd_forward

  !-----------------------------------------------------------------
  ! Main JVP dispatcher based on module
  !-----------------------------------------------------------------
  subroutine apply_jvp_module_aware(U, v, Jv, dt, gamma, grid, module_id)
    real(c_float), intent(in)  :: U(:,:,:,:), v(:,:,:,:)
    real(c_float), intent(out) :: Jv(:,:,:,:)
    real(c_float), intent(in)  :: dt, gamma
    type(grid_t), intent(in)   :: grid
    integer(c_int), intent(in) :: module_id
    
    integer(c_int) :: opts
    logical :: use_ad, freeze_limiters
    
    ! Determine JVP method based on module
    select case (module_id)
    case (MOD_DYCORE_ADV, MOD_DYCORE_PG, MOD_DYCORE_COR, MOD_DYCORE_DIFF)
      ! Dynamics core: use AD-JVP
      use_ad = .true.
      freeze_limiters = .false.
      opts = 0
      
    case (MOD_LATERAL_BC, MOD_COUPLER)
      ! Boundary/coupling: use FD with frozen switches
      use_ad = .false.
      freeze_limiters = .true.
      opts = ior(JVP_FREEZE_LIMITERS, JVP_FREEZE_SWITCHES)
      
    case (MOD_SURFACE, MOD_LAND_SURFACE, MOD_MICROPHYSICS, MOD_CUMULUS)
      ! Highly non-smooth physics: use FD with frozen limiters
      use_ad = .false.
      freeze_limiters = .true.
      opts = JVP_FREEZE_LIMITERS
      
    case (MOD_PBL, MOD_RADIATION)
      ! Mixed physics: use FD by default, AD if smoothed
      use_ad = .false.  ! Can be overridden if smoothing applied
      freeze_limiters = .true.
      opts = JVP_FREEZE_LIMITERS
      
    case default
      ! Default to safe FD-central with frozen limiters
      use_ad = .false.
      freeze_limiters = .true.
      opts = JVP_FREEZE_LIMITERS
    end select
    
    ! Apply appropriate JVP method
    if (use_ad) then
      call apply_J_times_v_ad(U, v, Jv, dt, gamma, grid, module_id, opts)
    else
      call apply_J_times_v_fd_central(U, v, Jv, dt, gamma, grid, &
                                      module_id, freeze_limiters)
    endif
    
  end subroutine apply_jvp_module_aware

  !-----------------------------------------------------------------
  ! Placeholder for WRF residual evaluation
  ! This should call actual WRF physics/dynamics routines
  !-----------------------------------------------------------------
  subroutine wrf_residual_flux_form(U, R, grid, module_id, freeze_limiters)
    real(c_float), intent(in)  :: U(:,:,:,:)
    real(c_float), intent(out) :: R(:,:,:,:)
    type(grid_t), intent(in)   :: grid
    integer(c_int), intent(in) :: module_id
    logical, intent(in) :: freeze_limiters
    
    ! This is a placeholder - actual implementation would call
    ! WRF dynamics and physics routines based on module_id
    
    ! For now, just set to a simple test residual
    R = -U  ! Placeholder
    
    ! In practice:
    ! select case (module_id)
    ! case (MOD_DYCORE_ADV)
    !   call compute_advection_tendency(U, R, grid)
    ! case (MOD_MICROPHYSICS)
    !   call mp_wsm6_tendency(U, R, grid, freeze_limiters)
    ! ... etc
    ! end select
    
  end subroutine wrf_residual_flux_form

end module jvp_bridge