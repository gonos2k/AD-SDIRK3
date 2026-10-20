MODULE module_native_terminal_adjoint_test
  USE, INTRINSIC :: ISO_C_BINDING
  USE module_domain, ONLY : domain
  USE module_implicit_sdirk3, ONLY : sdirk3_begin_fixed_trajectory, &
       sdirk3_pullback_fixed_trajectory, sdirk3_close_fixed_trajectory
  IMPLICIT NONE
  EXTERNAL :: wrf_error_fatal
  PRIVATE
  PUBLIC :: native_terminal_test_prepare, native_terminal_test_before_step, &
       native_terminal_test_after_step

  INTEGER, PARAMETER :: max_steps = 256
  INTEGER, PARAMETER :: window_steps = 32
  INTEGER, PARAMETER :: packed_size = 24531
  REAL(C_FLOAT), ALLOCATABLE, SAVE :: direction(:), terminal(:), lambda_wrapper(:)
  INTEGER(C_INT), SAVE :: run_mode = -1
  INTEGER(C_INT), SAVE :: expected_steps = max_steps
  LOGICAL, SAVE :: active = .FALSE.

  INTERFACE
    INTEGER(C_INT) FUNCTION native_terminal_prepare_c(x0, n, nx, ny, nz, &
         nx_u, ny_v, nz_w, direction, terminal, lambda_wrapper, mode, expected_steps) BIND(C, &
         NAME="sdirk3_native_terminal_prepare")
      IMPORT :: C_INT, C_FLOAT
      INTEGER(C_INT), VALUE :: n, nx, ny, nz, nx_u, ny_v, nz_w
      REAL(C_FLOAT), INTENT(INOUT) :: x0(*)
      REAL(C_FLOAT), INTENT(OUT) :: direction(*), terminal(*), lambda_wrapper(*)
      INTEGER(C_INT), INTENT(OUT) :: mode, expected_steps
    END FUNCTION native_terminal_prepare_c

    INTEGER(C_INT) FUNCTION native_terminal_before_step_c(host_step) BIND(C, &
         NAME="sdirk3_native_terminal_before_step")
      IMPORT :: C_INT
      INTEGER(C_INT), VALUE :: host_step
    END FUNCTION native_terminal_before_step_c

    INTEGER(C_INT) FUNCTION native_terminal_after_step_c(host_step) BIND(C, &
         NAME="sdirk3_native_terminal_after_step")
      IMPORT :: C_INT
      INTEGER(C_INT), VALUE :: host_step
    END FUNCTION native_terminal_after_step_c

    INTEGER(C_INT) FUNCTION native_terminal_record_endpoint_c(terminal_state, n, host_step) BIND(C, &
         NAME="sdirk3_native_terminal_record_endpoint")
      IMPORT :: C_INT, C_FLOAT
      INTEGER(C_INT), VALUE :: n, host_step
      REAL(C_FLOAT), INTENT(IN) :: terminal_state(*)
    END FUNCTION native_terminal_record_endpoint_c

    INTEGER(C_INT) FUNCTION native_terminal_rearm_window_c() BIND(C, &
         NAME="sdirk3_native_terminal_rearm_window")
      IMPORT :: C_INT
    END FUNCTION native_terminal_rearm_window_c

    INTEGER(C_INT) FUNCTION native_terminal_pullback64_c(lambda_initial, n) BIND(C, &
         NAME="sdirk3_native_terminal_pullback64")
      IMPORT :: C_INT, C_DOUBLE
      INTEGER(C_INT), VALUE :: n
      REAL(C_DOUBLE), INTENT(OUT) :: lambda_initial(*)
    END FUNCTION native_terminal_pullback64_c

    INTEGER(C_INT) FUNCTION native_terminal_finish_c(direction, terminal, &
         initial_gradient, lambda_initial_f64, n, mode, have_gradient) BIND(C, &
         NAME="sdirk3_native_terminal_finish")
      IMPORT :: C_INT, C_FLOAT, C_DOUBLE
      REAL(C_FLOAT), INTENT(IN) :: direction(*), terminal(*), initial_gradient(*)
      REAL(C_DOUBLE), INTENT(IN) :: lambda_initial_f64(*)
      INTEGER(C_INT), VALUE :: n, mode, have_gradient
    END FUNCTION native_terminal_finish_c
  END INTERFACE

CONTAINS

  SUBROUTINE native_terminal_test_prepare(grid, kds, kde)
    TYPE(domain), INTENT(INOUT) :: grid
    INTEGER, INTENT(IN) :: kds, kde
    INTEGER :: its, ite, jts, jte, kts, kte
    INTEGER :: nx, ny, nz, nx_u, ny_v, nz_w, n, i, j, k, q, ierr
    INTEGER(C_INT) :: c_status
    REAL(C_FLOAT), TARGET :: schedule(window_steps)
    REAL(C_FLOAT), ALLOCATABLE :: x0(:)

    IF (active) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: trajectory already active")
    IF (grid%num_tiles /= 1) &
         CALL wrf_error_fatal("NATIVE_TERMINAL_TEST requires exactly one tile")
    its = grid%i_start(1); ite = grid%i_end(1)
    jts = grid%j_start(1); jte = grid%j_end(1)
    kts = kds; kte = kde - 1
    nx = ite - its + 1; ny = jte - jts + 1; nz = kte - kts + 1
    nx_u = nx + 1; ny_v = ny + 1; nz_w = nz + 1
    n = nx_u*ny*nz + nx*ny_v*nz + 2*nx*ny*nz_w + nx*ny*nz + nx*ny
    IF (nx /= 17 .OR. ny /= 17 .OR. nz /= 16 .OR. nx_u /= 18 .OR. &
        ny_v /= 18 .OR. nz_w /= 17 .OR. n /= packed_size) &
         CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: unexpected native WRF tile layout")

    ALLOCATE(x0(n), direction(n), terminal(n), lambda_wrapper(n))
    q = 0
    DO j = jts, jte
      DO k = kts, kte
        DO i = its, ite + 1
          q = q + 1; x0(q) = grid%u_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte + 1
      DO k = kts, kte
        DO i = its, ite
          q = q + 1; x0(q) = grid%v_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte + 1
        DO i = its, ite
          q = q + 1; x0(q) = grid%w_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte + 1
        DO i = its, ite
          q = q + 1; x0(q) = grid%ph_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte
        DO i = its, ite
          q = q + 1; x0(q) = grid%t_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO i = its, ite
        q = q + 1; x0(q) = grid%mu_2(i,j)
      END DO
    END DO
    IF (q /= n) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: pack size mismatch")

    schedule = 0.9375_C_FLOAT
    CALL sdirk3_begin_fixed_trajectory(1, window_steps, schedule, ierr)
    IF (ierr /= 0) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: fixed trajectory begin failed")
    c_status = native_terminal_prepare_c(x0, INT(n,C_INT), INT(nx,C_INT), &
         INT(ny,C_INT), INT(nz,C_INT), INT(nx_u,C_INT), INT(ny_v,C_INT), &
         INT(nz_w,C_INT), direction, terminal, lambda_wrapper, run_mode, expected_steps)
    IF (c_status /= 1_C_INT) THEN
      CALL sdirk3_close_fixed_trajectory(1, ierr)
      CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: C++ prepare failed")
    END IF

    q = 0
    DO j = jts, jte
      DO k = kts, kte
        DO i = its, ite + 1
          q = q + 1; grid%u_2(i,k,j) = x0(q)
        END DO
      END DO
    END DO
    DO j = jts, jte + 1
      DO k = kts, kte
        DO i = its, ite
          q = q + 1; grid%v_2(i,k,j) = x0(q)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte + 1
        DO i = its, ite
          q = q + 1; grid%w_2(i,k,j) = x0(q)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte + 1
        DO i = its, ite
          q = q + 1; grid%ph_2(i,k,j) = x0(q)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte
        DO i = its, ite
          q = q + 1; grid%t_2(i,k,j) = x0(q)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO i = its, ite
        q = q + 1; grid%mu_2(i,j) = x0(q)
      END DO
    END DO
    active = .TRUE.
    DEALLOCATE(x0)
  END SUBROUTINE native_terminal_test_prepare

  SUBROUTINE native_terminal_test_before_step(host_step)
    INTEGER, INTENT(IN) :: host_step
    INTEGER(C_INT) :: c_status
    IF (.NOT. active) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: inactive before_step")
    c_status = native_terminal_before_step_c(INT(host_step,C_INT))
    IF (c_status /= 1_C_INT) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: before_step failed")
  END SUBROUTINE native_terminal_test_before_step

  SUBROUTINE native_terminal_test_after_step(grid, host_step, kds, kde)
    TYPE(domain), INTENT(INOUT) :: grid
    INTEGER, INTENT(IN) :: host_step
    INTEGER, INTENT(IN) :: kds, kde
    INTEGER(C_INT) :: c_status
    INTEGER :: ierr, its, ite, jts, jte, kts, kte, i, j, k, q
    REAL(C_FLOAT), TARGET :: schedule(window_steps)
    REAL(C_FLOAT), ALLOCATABLE :: gradient(:), endpoint(:)
    REAL(C_DOUBLE), ALLOCATABLE :: gradient64(:)
    IF (.NOT. active) RETURN
    c_status = native_terminal_after_step_c(INT(host_step,C_INT))
    IF (c_status /= 1_C_INT) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: after_step failed")
    IF (host_step > expected_steps .OR. MOD(host_step,window_steps) /= 0) RETURN
    ALLOCATE(endpoint(packed_size))
    its = grid%i_start(1); ite = grid%i_end(1)
    jts = grid%j_start(1); jte = grid%j_end(1)
    kts = kds; kte = kde - 1
    q = 0
    DO j = jts, jte
      DO k = kts, kte
        DO i = its, ite + 1
          q = q + 1; endpoint(q) = grid%u_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte + 1
      DO k = kts, kte
        DO i = its, ite
          q = q + 1; endpoint(q) = grid%v_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte + 1
        DO i = its, ite
          q = q + 1; endpoint(q) = grid%w_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte + 1
        DO i = its, ite
          q = q + 1; endpoint(q) = grid%ph_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO k = kts, kte
        DO i = its, ite
          q = q + 1; endpoint(q) = grid%t_2(i,k,j)
        END DO
      END DO
    END DO
    DO j = jts, jte
      DO i = its, ite
        q = q + 1; endpoint(q) = grid%mu_2(i,j)
      END DO
    END DO
    IF (q /= packed_size) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: endpoint pack size mismatch")
    c_status = native_terminal_record_endpoint_c(endpoint, INT(packed_size,C_INT), &
         INT(host_step,C_INT))
    IF (c_status /= 1_C_INT) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: endpoint capture failed")

    IF (host_step < expected_steps) THEN
      CALL sdirk3_close_fixed_trajectory(1, ierr)
      IF (ierr /= 0) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: fixed trajectory close failed")
      schedule = 0.9375_C_FLOAT
      CALL sdirk3_begin_fixed_trajectory(1, window_steps, schedule, ierr)
      IF (ierr /= 0) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: next window begin failed")
      c_status = native_terminal_rearm_window_c()
      IF (c_status /= 1_C_INT) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: next window rearm failed")
      RETURN
    END IF

    ALLOCATE(gradient(packed_size),gradient64(packed_size))
    gradient = 0.0_C_FLOAT; gradient64 = 0.0_C_DOUBLE
    IF (run_mode /= 2_C_INT) THEN
      c_status = native_terminal_pullback64_c(gradient64, INT(packed_size,C_INT))
      IF (c_status /= 1_C_INT) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: FP64 pullback failed")
      CALL sdirk3_pullback_fixed_trajectory(1, lambda_wrapper, gradient, ierr)
      IF (ierr /= 0) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: checked Fortran pullback failed")
      c_status = native_terminal_finish_c(direction, terminal, gradient, gradient64, &
           INT(packed_size,C_INT), run_mode, 1_C_INT)
    ELSE
      c_status = native_terminal_finish_c(direction, terminal, gradient, gradient64, &
           INT(packed_size,C_INT), run_mode, 0_C_INT)
    END IF
    IF (c_status /= 1_C_INT) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: finish failed")
    CALL sdirk3_close_fixed_trajectory(1, ierr)
    IF (ierr /= 0) CALL wrf_error_fatal("NATIVE_TERMINAL_TEST: fixed trajectory close failed")
    DEALLOCATE(endpoint, gradient, gradient64, direction, terminal, lambda_wrapper)
    active = .FALSE.
  END SUBROUTINE native_terminal_test_after_step

END MODULE module_native_terminal_adjoint_test
