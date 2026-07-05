! Fortran interface for SDIRK3 using WRF Registry-generated variables
! This file shows how to access Registry variables in Fortran and pass to C++

module sdirk3_registry_interface
    use module_domain, only : domain
    use module_configure, only : grid_config_rec_type
    
    implicit none
    
contains
    
    subroutine sdirk3_init_from_registry(grid, config_flags)
        implicit none
        type(domain), intent(inout) :: grid
        type(grid_config_rec_type), intent(in) :: config_flags
        
        ! Interface to C++ initialization
        interface
            subroutine wrf_sdirk3_init_config_from_registry( &
                ! Newton-Krylov parameters
                max_newton_iter, newton_tol, &
                gmres_restart, max_gmres_iter, gmres_tol, &
                ! JVP options
                jvp_method, jvp_epsilon, &
                jvp_checkpointing, jvp_checkpoint_segments, &
                jvp_graph_caching, jvp_batched, jvp_batch_size, &
                jvp_mixed_precision, &
                ! Implicit/Explicit treatment
                implicit_acoustic, implicit_gravity, &
                implicit_rayleigh, implicit_wdamp, &
                implicit_vdiff, implicit_hdiff, implicit_divergence, &
                ! Preconditioner options
                precond_type, precond_diagonal_only, &
                precond_block_jacobi, precond_block_size, &
                precond_ilu, precond_ilu_level, &
                precond_multigrid, precond_mg_levels, &
                ! Newton-Krylov advanced
                nk_adaptive_tol, nk_forcing_eta_max, nk_forcing_eta_min, &
                nk_forcing_gamma, nk_forcing_alpha, &
                nk_line_search, nk_trust_region, nk_trust_radius, &
                ! Memory options
                memory_pool, memory_pool_size_mb, &
                tensor_checkpointing, gradient_checkpointing, &
                ! Physics parameters
                kdamp, khdif, kvdif, &
                rayleigh_coef, rayleigh_depth, &
                w_damp_alpha, w_crit_cfl, &
                ! Debug options
                validation_level, debug_mode, verbose &
            ) bind(C, name="wrf_sdirk3_init_config_from_registry")
                use iso_c_binding
                ! Newton-Krylov
                integer(c_int), value :: max_newton_iter
                real(c_float), value :: newton_tol
                integer(c_int), value :: gmres_restart, max_gmres_iter
                real(c_float), value :: gmres_tol
                ! JVP
                integer(c_int), value :: jvp_method
                real(c_float), value :: jvp_epsilon
                integer(c_int), value :: jvp_checkpointing  ! 0/1 for logical
                integer(c_int), value :: jvp_checkpoint_segments
                integer(c_int), value :: jvp_graph_caching, jvp_batched
                integer(c_int), value :: jvp_batch_size
                integer(c_int), value :: jvp_mixed_precision
                ! Implicit/Explicit
                integer(c_int), value :: implicit_acoustic, implicit_gravity
                integer(c_int), value :: implicit_rayleigh, implicit_wdamp
                integer(c_int), value :: implicit_vdiff, implicit_hdiff
                integer(c_int), value :: implicit_divergence
                ! Preconditioner
                integer(c_int), value :: precond_type
                integer(c_int), value :: precond_diagonal_only
                integer(c_int), value :: precond_block_jacobi
                integer(c_int), value :: precond_block_size
                integer(c_int), value :: precond_ilu
                integer(c_int), value :: precond_ilu_level
                integer(c_int), value :: precond_multigrid
                integer(c_int), value :: precond_mg_levels
                ! Newton-Krylov advanced
                integer(c_int), value :: nk_adaptive_tol
                real(c_float), value :: nk_forcing_eta_max, nk_forcing_eta_min
                real(c_float), value :: nk_forcing_gamma, nk_forcing_alpha
                integer(c_int), value :: nk_line_search, nk_trust_region
                real(c_float), value :: nk_trust_radius
                ! Memory
                integer(c_int), value :: memory_pool
                integer(c_int), value :: memory_pool_size_mb
                integer(c_int), value :: tensor_checkpointing
                integer(c_int), value :: gradient_checkpointing
                ! Physics
                real(c_float), value :: kdamp, khdif, kvdif
                real(c_float), value :: rayleigh_coef, rayleigh_depth
                real(c_float), value :: w_damp_alpha, w_crit_cfl
                ! Debug
                integer(c_int), value :: validation_level
                integer(c_int), value :: debug_mode, verbose
            end subroutine
        end interface
        
        ! Convert logical to integer for C interface
        integer :: jvp_checkpointing_int, jvp_graph_caching_int, jvp_batched_int
        integer :: jvp_mixed_precision_int
        integer :: implicit_acoustic_int, implicit_gravity_int
        integer :: implicit_rayleigh_int, implicit_wdamp_int
        integer :: implicit_vdiff_int, implicit_hdiff_int, implicit_divergence_int
        integer :: precond_diagonal_only_int, precond_block_jacobi_int
        integer :: precond_ilu_int, precond_multigrid_int
        integer :: nk_adaptive_tol_int, nk_line_search_int, nk_trust_region_int
        integer :: memory_pool_int, tensor_checkpointing_int
        integer :: gradient_checkpointing_int
        integer :: debug_mode_int, verbose_int
        
        ! Convert logicals to integers (0/1)
        jvp_checkpointing_int = merge(1, 0, config_flags%sdirk3_jvp_checkpointing)
        jvp_graph_caching_int = merge(1, 0, config_flags%sdirk3_jvp_graph_caching)
        jvp_batched_int = merge(1, 0, config_flags%sdirk3_jvp_batched)
        jvp_mixed_precision_int = merge(1, 0, config_flags%sdirk3_jvp_mixed_precision)
        
        implicit_acoustic_int = merge(1, 0, config_flags%sdirk3_implicit_acoustic)
        implicit_gravity_int = merge(1, 0, config_flags%sdirk3_implicit_gravity)
        implicit_rayleigh_int = merge(1, 0, config_flags%sdirk3_implicit_rayleigh)
        implicit_wdamp_int = merge(1, 0, config_flags%sdirk3_implicit_wdamp)
        implicit_vdiff_int = merge(1, 0, config_flags%sdirk3_implicit_vdiff)
        implicit_hdiff_int = merge(1, 0, config_flags%sdirk3_implicit_hdiff)
        implicit_divergence_int = merge(1, 0, config_flags%sdirk3_implicit_divergence)
        
        precond_diagonal_only_int = merge(1, 0, config_flags%sdirk3_precond_diagonal_only)
        precond_block_jacobi_int = merge(1, 0, config_flags%sdirk3_precond_block_jacobi)
        precond_ilu_int = merge(1, 0, config_flags%sdirk3_precond_ilu)
        precond_multigrid_int = merge(1, 0, config_flags%sdirk3_precond_multigrid)
        
        nk_adaptive_tol_int = merge(1, 0, config_flags%sdirk3_nk_adaptive_tol)
        nk_line_search_int = merge(1, 0, config_flags%sdirk3_nk_line_search)
        nk_trust_region_int = merge(1, 0, config_flags%sdirk3_nk_trust_region)
        
        memory_pool_int = merge(1, 0, config_flags%sdirk3_memory_pool)
        tensor_checkpointing_int = merge(1, 0, config_flags%sdirk3_tensor_checkpointing)
        gradient_checkpointing_int = merge(1, 0, config_flags%sdirk3_gradient_checkpointing)
        
        debug_mode_int = merge(1, 0, config_flags%sdirk3_debug_mode)
        verbose_int = merge(1, 0, config_flags%sdirk3_verbose)
        
        ! Call C++ initialization with Registry values
        call wrf_sdirk3_init_config_from_registry( &
            ! Newton-Krylov
            config_flags%sdirk3_max_newton_iter, &
            config_flags%sdirk3_newton_tol, &
            config_flags%sdirk3_gmres_restart, &
            config_flags%sdirk3_max_gmres_iter, &
            config_flags%sdirk3_gmres_tol, &
            ! JVP
            config_flags%sdirk3_jvp_method, &
            config_flags%sdirk3_jvp_epsilon, &
            jvp_checkpointing_int, &
            config_flags%sdirk3_jvp_checkpoint_segments, &
            jvp_graph_caching_int, &
            jvp_batched_int, &
            config_flags%sdirk3_jvp_batch_size, &
            jvp_mixed_precision_int, &
            ! Implicit/Explicit
            implicit_acoustic_int, &
            implicit_gravity_int, &
            implicit_rayleigh_int, &
            implicit_wdamp_int, &
            implicit_vdiff_int, &
            implicit_hdiff_int, &
            implicit_divergence_int, &
            ! Preconditioner
            config_flags%sdirk3_precond_type, &
            precond_diagonal_only_int, &
            precond_block_jacobi_int, &
            config_flags%sdirk3_precond_block_size, &
            precond_ilu_int, &
            config_flags%sdirk3_precond_ilu_level, &
            precond_multigrid_int, &
            config_flags%sdirk3_precond_mg_levels, &
            ! Newton-Krylov advanced
            nk_adaptive_tol_int, &
            config_flags%sdirk3_nk_forcing_eta_max, &
            config_flags%sdirk3_nk_forcing_eta_min, &
            config_flags%sdirk3_nk_forcing_gamma, &
            config_flags%sdirk3_nk_forcing_alpha, &
            nk_line_search_int, &
            nk_trust_region_int, &
            config_flags%sdirk3_nk_trust_radius, &
            ! Memory
            memory_pool_int, &
            config_flags%sdirk3_memory_pool_size_mb, &
            tensor_checkpointing_int, &
            gradient_checkpointing_int, &
            ! Physics
            config_flags%sdirk3_kdamp, &
            config_flags%sdirk3_khdif, &
            config_flags%sdirk3_kvdif, &
            config_flags%sdirk3_rayleigh_coef, &
            config_flags%sdirk3_rayleigh_depth, &
            config_flags%sdirk3_w_damp_alpha, &
            config_flags%sdirk3_w_crit_cfl, &
            ! Debug
            config_flags%sdirk3_validation_level, &
            debug_mode_int, &
            verbose_int &
        )
        
        write(*,*) 'SDIRK3: Configuration loaded from WRF Registry'
        write(*,*) '  Max Newton iterations:', config_flags%sdirk3_max_newton_iter
        write(*,*) '  JVP method:', config_flags%sdirk3_jvp_method
        write(*,*) '  Implicit acoustic:', config_flags%sdirk3_implicit_acoustic
        write(*,*) '  Preconditioner type:', config_flags%sdirk3_precond_type
        
    end subroutine sdirk3_init_from_registry
    
end module sdirk3_registry_interface