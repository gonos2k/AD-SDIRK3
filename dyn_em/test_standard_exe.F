!TEST: Test standard.exe preprocessing
!
! This file tests standard.exe behavior with various code patterns
! to ensure no code loss during preprocessing

MODULE test_standard_exe

   IMPLICIT NONE
   
   ! Test 1: Conditional compilation
#ifdef USE_LIBTORCH
   LOGICAL :: libtorch_enabled = .TRUE.
#else
   LOGICAL :: libtorch_enabled = .FALSE.
#endif

   ! Test 2: String with quotes
   CHARACTER(LEN=100) :: test_string = "This is a 'test' string"
   
   ! Test 3: Comments with special characters
   ! This comment has 'quotes' and other $pecial @characters
   
CONTAINS

   SUBROUTINE test_preprocessing()
      ! Test 4: Multi-line statement
      REAL :: test_array(10,10,10) = &
              RESHAPE([(i, i=1,1000)], [10,10,10])
      
      ! Test 5: Preprocessor in subroutine
#ifdef DM_PARALLEL
      CALL wrf_dm_sum_real(test_value)
#endif
      
   END SUBROUTINE test_preprocessing

END MODULE test_standard_exe