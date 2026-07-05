!===============================================================================
! module_namelist_kslab.F
!
! Purpose: Handle k-slab namelist parameters and interface with C++ SDIRK3 code
!
! Author: yhlee
! Date: 2025-08-05
! Principle: 현재 구조를 최대한 활용 (Maximum Current Structure Utilization)
!===============================================================================

MODULE module_namelist_kslab
  USE module_configure, ONLY : grid_config_rec_type
  USE module_wrf_error
  
  IMPLICIT NONE
  PRIVATE
  
  PUBLIC :: setup_kslab_config
  PUBLIC :: print_kslab_config
  PUBLIC :: validate_kslab_config
  
  ! Interface to C++ configuration function
  INTERFACE
    SUBROUTINE sdirk3_set_kslab_config( &
        use_advanced_kslab, kslab_base_size, kslab_min_size, kslab_max_size, &
        kslab_auto_tune, kslab_enable_avx2, kslab_enable_avx512, &
        kslab_vector_width, kslab_enable_fusion, kslab_fuse_advection_diffusion, &
        kslab_num_threads, kslab_thread_per_j, kslab_enable_prefetch, &
        kslab_prefetch_distance, kslab_collect_metrics, kslab_adaptive_tuning, &
        kslab_print_metrics, kslab_metric_interval) BIND(C)
      USE, INTRINSIC :: ISO_C_BINDING
      LOGICAL(C_BOOL) :: use_advanced_kslab
      INTEGER(C_INT) :: kslab_base_size, kslab_min_size, kslab_max_size
      LOGICAL(C_BOOL) :: kslab_auto_tune
      LOGICAL(C_BOOL) :: kslab_enable_avx2, kslab_enable_avx512
      INTEGER(C_INT) :: kslab_vector_width
      LOGICAL(C_BOOL) :: kslab_enable_fusion, kslab_fuse_advection_diffusion
      INTEGER(C_INT) :: kslab_num_threads
      LOGICAL(C_BOOL) :: kslab_thread_per_j
      LOGICAL(C_BOOL) :: kslab_enable_prefetch
      INTEGER(C_INT) :: kslab_prefetch_distance
      LOGICAL(C_BOOL) :: kslab_collect_metrics, kslab_adaptive_tuning
      LOGICAL(C_BOOL) :: kslab_print_metrics
      INTEGER(C_INT) :: kslab_metric_interval
    END SUBROUTINE sdirk3_set_kslab_config
  END INTERFACE
  
CONTAINS

  !-----------------------------------------------------------------------------
  ! setup_kslab_config
  !
  ! Purpose: Setup k-slab configuration from namelist and pass to C++
  !-----------------------------------------------------------------------------
  SUBROUTINE setup_kslab_config(config_flags)
    TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
    
    ! C-compatible types
    LOGICAL(C_BOOL) :: c_use_advanced_kslab
    INTEGER(C_INT) :: c_kslab_base_size, c_kslab_min_size, c_kslab_max_size
    LOGICAL(C_BOOL) :: c_kslab_auto_tune
    LOGICAL(C_BOOL) :: c_kslab_enable_avx2, c_kslab_enable_avx512
    INTEGER(C_INT) :: c_kslab_vector_width
    LOGICAL(C_BOOL) :: c_kslab_enable_fusion, c_kslab_fuse_advection_diffusion
    INTEGER(C_INT) :: c_kslab_num_threads
    LOGICAL(C_BOOL) :: c_kslab_thread_per_j
    LOGICAL(C_BOOL) :: c_kslab_enable_prefetch
    INTEGER(C_INT) :: c_kslab_prefetch_distance
    LOGICAL(C_BOOL) :: c_kslab_collect_metrics, c_kslab_adaptive_tuning
    LOGICAL(C_BOOL) :: c_kslab_print_metrics
    INTEGER(C_INT) :: c_kslab_metric_interval
    
    ! Only proceed if SDIRK3 is enabled and k-slab is requested
    IF (config_flags%time_integration_scheme /= 1) RETURN
    IF (.NOT. config_flags%use_advanced_kslab) RETURN
    
    ! Convert Fortran logical to C bool
    c_use_advanced_kslab = config_flags%use_advanced_kslab
    c_kslab_base_size = config_flags%kslab_base_size
    c_kslab_min_size = config_flags%kslab_min_size
    c_kslab_max_size = config_flags%kslab_max_size
    c_kslab_auto_tune = config_flags%kslab_auto_tune
    c_kslab_enable_avx2 = config_flags%kslab_enable_avx2
    c_kslab_enable_avx512 = config_flags%kslab_enable_avx512
    c_kslab_vector_width = config_flags%kslab_vector_width
    c_kslab_enable_fusion = config_flags%kslab_enable_fusion
    c_kslab_fuse_advection_diffusion = config_flags%kslab_fuse_advection_diffusion
    c_kslab_num_threads = config_flags%kslab_num_threads
    c_kslab_thread_per_j = config_flags%kslab_thread_per_j
    c_kslab_enable_prefetch = config_flags%kslab_enable_prefetch
    c_kslab_prefetch_distance = config_flags%kslab_prefetch_distance
    c_kslab_collect_metrics = config_flags%kslab_collect_metrics
    c_kslab_adaptive_tuning = config_flags%kslab_adaptive_tuning
    c_kslab_print_metrics = config_flags%kslab_print_metrics
    c_kslab_metric_interval = config_flags%kslab_metric_interval
    
    ! Validate configuration
    CALL validate_kslab_config(config_flags)
    
    ! Pass configuration to C++
    CALL sdirk3_set_kslab_config( &
        c_use_advanced_kslab, c_kslab_base_size, c_kslab_min_size, c_kslab_max_size, &
        c_kslab_auto_tune, c_kslab_enable_avx2, c_kslab_enable_avx512, &
        c_kslab_vector_width, c_kslab_enable_fusion, c_kslab_fuse_advection_diffusion, &
        c_kslab_num_threads, c_kslab_thread_per_j, c_kslab_enable_prefetch, &
        c_kslab_prefetch_distance, c_kslab_collect_metrics, c_kslab_adaptive_tuning, &
        c_kslab_print_metrics, c_kslab_metric_interval)
    
    ! Print configuration if requested
    IF (config_flags%kslab_print_config) THEN
      CALL print_kslab_config(config_flags)
    END IF
    
  END SUBROUTINE setup_kslab_config
  
  !-----------------------------------------------------------------------------
  ! validate_kslab_config
  !
  ! Purpose: Validate k-slab configuration parameters
  !-----------------------------------------------------------------------------
  SUBROUTINE validate_kslab_config(config_flags)
    TYPE(grid_config_rec_type), INTENT(INOUT) :: config_flags
    CHARACTER(LEN=256) :: message
    
    ! Validate slab sizes
    IF (config_flags%kslab_base_size < config_flags%kslab_min_size) THEN
      WRITE(message,*) 'WARNING: kslab_base_size (', config_flags%kslab_base_size, &
                      ') < kslab_min_size (', config_flags%kslab_min_size, '). Adjusting.'
      CALL wrf_message(TRIM(message))
      config_flags%kslab_base_size = config_flags%kslab_min_size
    END IF
    
    IF (config_flags%kslab_base_size > config_flags%kslab_max_size) THEN
      WRITE(message,*) 'WARNING: kslab_base_size (', config_flags%kslab_base_size, &
                      ') > kslab_max_size (', config_flags%kslab_max_size, '). Adjusting.'
      CALL wrf_message(TRIM(message))
      config_flags%kslab_base_size = config_flags%kslab_max_size
    END IF
    
    ! Validate vector width
    IF (config_flags%kslab_vector_width /= 1 .AND. &
        config_flags%kslab_vector_width /= 4 .AND. &
        config_flags%kslab_vector_width /= 8 .AND. &
        config_flags%kslab_vector_width /= 16) THEN
      WRITE(message,*) 'WARNING: Invalid kslab_vector_width (', &
                      config_flags%kslab_vector_width, '). Setting to 8.'
      CALL wrf_message(TRIM(message))
      config_flags%kslab_vector_width = 8
    END IF
    
    ! Check SIMD availability
    IF (config_flags%kslab_enable_avx512 .OR. config_flags%kslab_enable_avx2) THEN
      ! This check would be more sophisticated in practice
      ! For now, just issue a message
      CALL wrf_message('INFO: SIMD optimizations enabled for k-slab operations')
    END IF
    
    ! Validate thread count
    IF (config_flags%kslab_num_threads < 0) THEN
      WRITE(message,*) 'WARNING: Invalid kslab_num_threads (', &
                      config_flags%kslab_num_threads, '). Setting to 0 (auto).'
      CALL wrf_message(TRIM(message))
      config_flags%kslab_num_threads = 0
    END IF
    
    ! Check fusion compatibility
    IF (config_flags%kslab_enable_fusion .AND. &
        .NOT. config_flags%use_advanced_kslab) THEN
      CALL wrf_message('WARNING: Operation fusion requires advanced k-slab. Disabling fusion.')
      config_flags%kslab_enable_fusion = .FALSE.
    END IF
    
  END SUBROUTINE validate_kslab_config
  
  !-----------------------------------------------------------------------------
  ! print_kslab_config
  !
  ! Purpose: Print k-slab configuration summary
  !-----------------------------------------------------------------------------
  SUBROUTINE print_kslab_config(config_flags)
    TYPE(grid_config_rec_type), INTENT(IN) :: config_flags
    
    CALL wrf_message(' ')
    CALL wrf_message('=== K-Slab Configuration Summary ===')
    CALL wrf_message(' ')
    
    ! Core configuration
    WRITE(wrf_err_message,*) 'K-Slab Optimization:     ', &
        MERGE('ENABLED ', 'DISABLED', config_flags%use_advanced_kslab)
    CALL wrf_message(TRIM(wrf_err_message))
    
    IF (config_flags%use_advanced_kslab) THEN
      WRITE(wrf_err_message,*) 'Base slab size:          ', config_flags%kslab_base_size
      CALL wrf_message(TRIM(wrf_err_message))
      
      WRITE(wrf_err_message,*) 'Slab size range:         ', config_flags%kslab_min_size, &
                              ' - ', config_flags%kslab_max_size
      CALL wrf_message(TRIM(wrf_err_message))
      
      WRITE(wrf_err_message,*) 'Auto-tuning:             ', &
          MERGE('ON ', 'OFF', config_flags%kslab_auto_tune)
      CALL wrf_message(TRIM(wrf_err_message))
      
      ! Vectorization
      CALL wrf_message(' ')
      CALL wrf_message('Vectorization:')
      WRITE(wrf_err_message,*) '  AVX2:                  ', &
          MERGE('ENABLED ', 'DISABLED', config_flags%kslab_enable_avx2)
      CALL wrf_message(TRIM(wrf_err_message))
      
      WRITE(wrf_err_message,*) '  AVX512:                ', &
          MERGE('ENABLED ', 'DISABLED', config_flags%kslab_enable_avx512)
      CALL wrf_message(TRIM(wrf_err_message))
      
      WRITE(wrf_err_message,*) '  Vector width:          ', config_flags%kslab_vector_width, ' floats'
      CALL wrf_message(TRIM(wrf_err_message))
      
      ! Operation fusion
      CALL wrf_message(' ')
      CALL wrf_message('Operation Fusion:')
      WRITE(wrf_err_message,*) '  Fusion enabled:        ', &
          MERGE('YES', 'NO ', config_flags%kslab_enable_fusion)
      CALL wrf_message(TRIM(wrf_err_message))
      
      IF (config_flags%kslab_enable_fusion) THEN
        WRITE(wrf_err_message,*) '  Advection+Diffusion:   ', &
            MERGE('FUSED    ', 'SEPARATE ', config_flags%kslab_fuse_advection_diffusion)
        CALL wrf_message(TRIM(wrf_err_message))
      END IF
      
      ! Performance
      CALL wrf_message(' ')
      CALL wrf_message('Performance Settings:')
      IF (config_flags%kslab_num_threads == 0) THEN
        CALL wrf_message('  Threads:               AUTO')
      ELSE
        WRITE(wrf_err_message,*) '  Threads:               ', config_flags%kslab_num_threads
        CALL wrf_message(TRIM(wrf_err_message))
      END IF
      
      WRITE(wrf_err_message,*) '  Thread strategy:       ', &
          MERGE('per-J    ', 'per-slab ', config_flags%kslab_thread_per_j)
      CALL wrf_message(TRIM(wrf_err_message))
      
      WRITE(wrf_err_message,*) '  Prefetching:           ', &
          MERGE('ON ', 'OFF', config_flags%kslab_enable_prefetch)
      CALL wrf_message(TRIM(wrf_err_message))
      
      IF (config_flags%kslab_enable_prefetch) THEN
        WRITE(wrf_err_message,*) '  Prefetch distance:     ', &
            config_flags%kslab_prefetch_distance, ' slabs'
        CALL wrf_message(TRIM(wrf_err_message))
      END IF
      
      ! Monitoring
      CALL wrf_message(' ')
      CALL wrf_message('Monitoring:')
      WRITE(wrf_err_message,*) '  Collect metrics:       ', &
          MERGE('YES', 'NO ', config_flags%kslab_collect_metrics)
      CALL wrf_message(TRIM(wrf_err_message))
      
      WRITE(wrf_err_message,*) '  Adaptive tuning:       ', &
          MERGE('ON ', 'OFF', config_flags%kslab_adaptive_tuning)
      CALL wrf_message(TRIM(wrf_err_message))
      
      IF (config_flags%kslab_print_metrics) THEN
        WRITE(wrf_err_message,*) '  Metric interval:       ', &
            config_flags%kslab_metric_interval, ' seconds'
        CALL wrf_message(TRIM(wrf_err_message))
      END IF
    END IF
    
    CALL wrf_message(' ')
    CALL wrf_message('====================================')
    CALL wrf_message(' ')
    
  END SUBROUTINE print_kslab_config
  
END MODULE module_namelist_kslab