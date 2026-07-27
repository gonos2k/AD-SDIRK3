! 9F.D21 hosted-CI contract for WRF_BWAVE_PERT_SCALE parsing.
!
! Compiles dyn_em/bwave_pert_scale_parse.inc -- the SAME source text the model
! compiles -- so this gates the real parser, not a copy. Needs only gfortran: no
! WRF, no libtorch, no netCDF, no MPI, sub-second. That matters because model
! EXECUTION is local-only under the standing repo policy, so hosted CI can never
! re-run the ablation/g_eff experiments; gating the code-level contract is the
! part it CAN do.
!
! Every case below is one the parser has actually been wrong about, or one whose
! silent failure would corrupt an experiment:
!   - NaN previously passed `parsed <= 0.0` (NaN fails every comparison). If the
!     IEEE_IS_FINITE guard is ever removed or reordered after the range test,
!     NaN_REJECTED fails.
!   - a truncated value would otherwise run BASELINE while the operator believes
!     a scaled run is in progress -- silent, and it invalidates the experiment.
!   - eps=0 must be reachable (zero-bubble control) but only on request.

PROGRAM test_bwave_pert_scale
  IMPLICIT NONE
  INTEGER :: failures, cases
! Expected number of cases, asserted BELOW by the test itself rather than by a grep
! in the workflow YAML. A count ratchet is the right contract -- it is what proves
! the gates ran instead of silently skipping -- but keeping it in the YAML has
! broken here four times, because adding a case then requires editing two files and
! the CI-side number rots. Holding it in the source means one edit, and forgetting
! it fails loudly next to the change that caused it.
  INTEGER, PARAMETER :: EXPECTED_CASES = 16
  failures = 0
  cases    = 0

  ! ---- accepted -------------------------------------------------------------
  CALL expect_ok(cases, 'unset',            '',        0, .FALSE., 1.0,  failures)
  CALL expect_ok(cases, 'one',              '1.0',     3, .FALSE., 1.0,  failures)
  CALL expect_ok(cases, 'half',             '0.5',     3, .FALSE., 0.5,  failures)
  CALL expect_ok(cases, 'quarter',          '0.25',    4, .FALSE., 0.25, failures)
  CALL expect_ok(cases, 'exponent form',    '1.0E-2',  6, .FALSE., 0.01, failures)
  CALL expect_ok(cases, 'at max',           '1000.0',  6, .FALSE., 1000.0, failures)
  ! zero ONLY with the explicit control flag
  CALL expect_ok(cases, 'zero w/ allow',    '0.0',     3, .TRUE.,  0.0,  failures)

  ! ---- rejected -------------------------------------------------------------
  CALL expect_bad(cases, 'NaN rejected',        'NaN',      3, .FALSE., failures)
  CALL expect_bad(cases, 'nan lowercase',       'nan',      3, .FALSE., failures)
  CALL expect_bad(cases, '+Inf rejected',       'Infinity', 8, .FALSE., failures)
  CALL expect_bad(cases, '-Inf rejected',       '-Infinity',9, .FALSE., failures)
  CALL expect_bad(cases, 'malformed',           'abc',      3, .FALSE., failures)
  CALL expect_bad(cases, 'negative',            '-1.0',     4, .FALSE., failures)
  CALL expect_bad(cases, 'above max',           '1.0E4',    5, .FALSE., failures)
  ! zero WITHOUT the control flag must not silently disable the perturbation
  CALL expect_bad(cases, 'zero w/o allow',      '0.0',      3, .FALSE., failures)
  ! len > buffer means the environment value was truncated
  CALL expect_bad(cases, 'truncated',           '0.5',     99, .FALSE., failures)

  IF ( cases /= EXPECTED_CASES ) THEN
     WRITE(*,'(A,I0,A,I0,A)') 'BWAVE_PERT_SCALE CONTRACT: FAIL (ran ', cases, &
        ' cases, expected ', EXPECTED_CASES, ' -- a case was added or silently dropped)'
     ERROR STOP 1
  END IF

  IF ( failures == 0 ) THEN
     WRITE(*,'(A,I0,A,I0,A)') 'BWAVE_PERT_SCALE CONTRACT: PASS (', cases, '/', &
        EXPECTED_CASES, ')'
  ELSE
     WRITE(*,'(A,I0,A)') 'BWAVE_PERT_SCALE CONTRACT: FAIL (', failures, ' case(s))'
     ERROR STOP 1
  END IF

CONTAINS

  SUBROUTINE expect_ok(cases, label, text, tlen, allow_zero, want, failures)
    CHARACTER(LEN=*), INTENT(IN)    :: label, text
    INTEGER,          INTENT(IN)    :: tlen
    LOGICAL,          INTENT(IN)    :: allow_zero
    REAL,             INTENT(IN)    :: want
    INTEGER,          INTENT(INOUT) :: failures
    INTEGER,          INTENT(INOUT) :: cases
    CHARACTER(LEN=64)  :: buf
    CHARACTER(LEN=160) :: msg
    REAL    :: got
    LOGICAL :: ok
    cases = cases + 1
    buf = text
    CALL bwave_pert_scale_parse(buf, tlen, allow_zero, got, ok, msg)
    IF ( .NOT. ok ) THEN
       WRITE(*,'(A,A,A,A)') '  FAIL ', label, ' : unexpectedly rejected -- ', TRIM(msg)
       failures = failures + 1
    ELSE IF ( ABS(got - want) > 1.0E-6 * MAX(1.0, ABS(want)) ) THEN
       WRITE(*,'(A,A,A,E14.6,A,E14.6)') '  FAIL ', label, ' : got ', got, ' want ', want
       failures = failures + 1
    ELSE
       WRITE(*,'(A,A)') '  ok   ', label
    END IF
  END SUBROUTINE expect_ok

  SUBROUTINE expect_bad(cases, label, text, tlen, allow_zero, failures)
    CHARACTER(LEN=*), INTENT(IN)    :: label, text
    INTEGER,          INTENT(IN)    :: tlen
    LOGICAL,          INTENT(IN)    :: allow_zero
    INTEGER,          INTENT(INOUT) :: failures
    INTEGER,          INTENT(INOUT) :: cases
    CHARACTER(LEN=64)  :: buf
    CHARACTER(LEN=160) :: msg
    REAL    :: got
    LOGICAL :: ok
    cases = cases + 1
    buf = text
    CALL bwave_pert_scale_parse(buf, tlen, allow_zero, got, ok, msg)
    IF ( ok ) THEN
       WRITE(*,'(A,A,A,E14.6)') '  FAIL ', label, ' : accepted, value ', got
       failures = failures + 1
    ELSE
       WRITE(*,'(A,A,A,A)') '  ok   ', label, ' : ', TRIM(msg)
    END IF
  END SUBROUTINE expect_bad

  INCLUDE 'bwave_pert_scale_parse.inc'

END PROGRAM test_bwave_pert_scale
