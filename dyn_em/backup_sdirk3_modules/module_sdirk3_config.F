MODULE module_sdirk3_config
   !
   ! Module: module_sdirk3_config
   ! Purpose: Configuration interface for SDIRK3 solver
   !
   USE iso_c_binding
   
   IMPLICIT NONE
   PRIVATE
   
   ! Public subroutines
   PUBLIC :: sdirk3_config_init
   PUBLIC :: sdirk3_config_set_from_namelist
   PUBLIC :: sdirk3_config_print
   
   ! C interface
   INTERFACE
      SUBROUTINE wrf_sdirk3_set_config_int(name, value) &
                 BIND(C, name="wrf_sdirk3_set_config_int")
         USE iso_c_binding
         CHARACTER(c_char), DIMENSION(*), INTENT(IN) :: name
         INTEGER(c_int), VALUE :: value
      END SUBROUTINE
      
      SUBROUTINE wrf_sdirk3_set_config_float(name, value) &
                 BIND(C, name="wrf_sdirk3_set_config_float")
         USE iso_c_binding
         CHARACTER(c_char), DIMENSION(*), INTENT(IN) :: name
         REAL(c_float), VALUE :: value
      END SUBROUTINE
      
      SUBROUTINE wrf_sdirk3_set_config_bool(name, value) &
                 BIND(C, name="wrf_sdirk3_set_config_bool")
         USE iso_c_binding
         CHARACTER(c_char), DIMENSION(*), INTENT(IN) :: name
         INTEGER(c_int), VALUE :: value
      END SUBROUTINE
      
      SUBROUTINE wrf_sdirk3_print_config() &
                 BIND(C, name="wrf_sdirk3_print_config")
         USE iso_c_binding
      END SUBROUTINE
   END INTERFACE
   
CONTAINS

   SUBROUTINE sdirk3_config_init()
      ! Initialize configuration with environment variables
      CHARACTER(LEN=32) :: env_value
      INTEGER :: int_val
      REAL :: real_val
      INTEGER :: ios
      
      ! Check environment variables
      CALL GET_ENVIRONMENT_VARIABLE("WRF_SDIRK3_MAX_NEWTON_ITER", env_value)
      IF (LEN_TRIM(env_value) > 0) THEN
         READ(env_value, *, IOSTAT=ios) int_val
         IF (ios == 0) CALL wrf_sdirk3_set_config_int("max_newton_iter"//C_NULL_CHAR, int_val)
      END IF
      
      CALL GET_ENVIRONMENT_VARIABLE("WRF_SDIRK3_NEWTON_TOL", env_value)
      IF (LEN_TRIM(env_value) > 0) THEN
         READ(env_value, *, IOSTAT=ios) real_val
         IF (ios == 0) CALL wrf_sdirk3_set_config_float("newton_tol"//C_NULL_CHAR, real_val)
      END IF
      
      CALL GET_ENVIRONMENT_VARIABLE("WRF_SDIRK3_DEBUG_LEVEL", env_value)
      IF (LEN_TRIM(env_value) > 0) THEN
         READ(env_value, *, IOSTAT=ios) int_val
         IF (ios == 0) CALL wrf_sdirk3_set_config_int("debug_level"//C_NULL_CHAR, int_val)
      END IF
      
   END SUBROUTINE sdirk3_config_init
   
   SUBROUTINE sdirk3_config_set_from_namelist(sdirk3_max_newton_iter, &
                                              sdirk3_newton_tol, &
                                              sdirk3_gmres_restart, &
                                              sdirk3_precond_type, &
                                              sdirk3_adaptive_timestep, &
                                              sdirk3_debug_level)
      INTEGER, OPTIONAL, INTENT(IN) :: sdirk3_max_newton_iter
      REAL, OPTIONAL, INTENT(IN) :: sdirk3_newton_tol
      INTEGER, OPTIONAL, INTENT(IN) :: sdirk3_gmres_restart
      INTEGER, OPTIONAL, INTENT(IN) :: sdirk3_precond_type
      LOGICAL, OPTIONAL, INTENT(IN) :: sdirk3_adaptive_timestep
      INTEGER, OPTIONAL, INTENT(IN) :: sdirk3_debug_level
      
      ! Set configuration from namelist values
      IF (PRESENT(sdirk3_max_newton_iter)) THEN
         CALL wrf_sdirk3_set_config_int("max_newton_iter"//C_NULL_CHAR, sdirk3_max_newton_iter)
      END IF
      
      IF (PRESENT(sdirk3_newton_tol)) THEN
         CALL wrf_sdirk3_set_config_float("newton_tol"//C_NULL_CHAR, sdirk3_newton_tol)
      END IF
      
      IF (PRESENT(sdirk3_gmres_restart)) THEN
         CALL wrf_sdirk3_set_config_int("gmres_restart"//C_NULL_CHAR, sdirk3_gmres_restart)
      END IF
      
      IF (PRESENT(sdirk3_precond_type)) THEN
         CALL wrf_sdirk3_set_config_int("precond_type"//C_NULL_CHAR, sdirk3_precond_type)
      END IF
      
      IF (PRESENT(sdirk3_adaptive_timestep)) THEN
         IF (sdirk3_adaptive_timestep) THEN
            CALL wrf_sdirk3_set_config_bool("adaptive_timestep"//C_NULL_CHAR, 1)
         ELSE
            CALL wrf_sdirk3_set_config_bool("adaptive_timestep"//C_NULL_CHAR, 0)
         END IF
      END IF
      
      IF (PRESENT(sdirk3_debug_level)) THEN
         CALL wrf_sdirk3_set_config_int("debug_level"//C_NULL_CHAR, sdirk3_debug_level)
      END IF
      
   END SUBROUTINE sdirk3_config_set_from_namelist
   
   SUBROUTINE sdirk3_config_print()
      CALL wrf_sdirk3_print_config()
   END SUBROUTINE sdirk3_config_print

END MODULE module_sdirk3_config