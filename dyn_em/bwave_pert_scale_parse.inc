! bwave_pert_scale_parse.inc -- PURE parsing of WRF_BWAVE_PERT_SCALE.
!
! 9F.D21. Why this is an INCLUDE and not just a routine inside
! module_initialize_ideal.F: the causal results of this campaign (failure step,
! g_eff, ablation matrix) exist only in manual local runs, and the standing repo
! policy is that model EXECUTION is local-only -- so hosted CI can never re-run
! them. What hosted CI CAN do is gate the code-level contracts. That is only worth
! anything if the test exercises the REAL parser rather than a copy of it, because
! a copied test drifts and then certifies nothing.
!
! It lives in dyn_em/ beside its consumer -- NOT in inc/, which is Registry-generated
! and gitignored, so a source file there would be untracked and destroyed by
! `./clean -a`. dyn_em/ is the compile CWD for module_initialize_ideal.F, so a bare
! INCLUDE resolves with no Makefile change; the contract test passes -Idyn_em.
! Same source text both sides, no build-system risk.
!
! The routine is deliberately POLICY-FREE: no environment access, no wrf_error_fatal,
! no I/O. It takes the raw text and returns a value plus ok/msg. That is what makes
! it testable without WRF, and it keeps the fail-closed DECISION with the caller.
!
! Contract:
!   text       raw environment text (not trimmed by this routine)
!   text_len   significant length of text; <= 0 means "unset" -> scale = 1.0, ok
!   allow_zero .TRUE. permits scale = 0 (the zero-bubble control run, which is
!              needed to separate the base jet's own evolution from the
!              bubble-seeded mode via dU = U_eps - U_0). Default callers pass
!              .FALSE. so a stray 0 cannot silently disable the perturbation.
!   scale      parsed value; 1.0 when unset. Untouched-meaningful only if ok.
!   ok         .FALSE. => caller MUST fail closed.
!   msg        reason, ready to hand to wrf_error_fatal. Empty when ok.

  SUBROUTINE bwave_pert_scale_parse(text, text_len, allow_zero, scale, ok, msg)
    USE, INTRINSIC :: IEEE_ARITHMETIC, ONLY : IEEE_IS_FINITE
    IMPLICIT NONE
    CHARACTER(LEN=*), INTENT(IN)  :: text
    INTEGER,          INTENT(IN)  :: text_len
    LOGICAL,          INTENT(IN)  :: allow_zero
    REAL,             INTENT(OUT) :: scale
    LOGICAL,          INTENT(OUT) :: ok
    CHARACTER(LEN=*), INTENT(OUT) :: msg

    INTEGER         :: io_status
    REAL            :: parsed
    REAL, PARAMETER :: max_scale = 1.0E3

    scale = 1.0
    ok    = .TRUE.
    msg   = ''

    ! Unset -> baseline. Callers signal this with text_len <= 0.
    IF ( text_len <= 0 ) RETURN

    IF ( text_len > LEN(text) ) THEN
       ok  = .FALSE.
       msg = 'WRF_BWAVE_PERT_SCALE was truncated (value longer than buffer)'
       RETURN
    END IF

    READ(text(1:text_len), *, IOSTAT=io_status) parsed
    IF ( io_status /= 0 ) THEN
       ok  = .FALSE.
       msg = 'WRF_BWAVE_PERT_SCALE must parse as a real'
       RETURN
    END IF

    ! NaN fails EVERY comparison, so a bare `parsed <= 0.0` test lets NaN through --
    ! the same blind spot previously found in the EOS clamp and then reproduced here.
    ! IEEE_IS_FINITE is what actually rejects NaN and +/-Inf. Order matters: this
    ! must precede the range test, not follow it.
    IF ( .NOT. IEEE_IS_FINITE(parsed) ) THEN
       ok  = .FALSE.
       msg = 'WRF_BWAVE_PERT_SCALE must be finite (NaN/Inf rejected)'
       RETURN
    END IF

    IF ( parsed < 0.0 ) THEN
       ok  = .FALSE.
       msg = 'WRF_BWAVE_PERT_SCALE must not be negative'
       RETURN
    END IF

    IF ( parsed == 0.0 .AND. .NOT. allow_zero ) THEN
       ok  = .FALSE.
       msg = 'WRF_BWAVE_PERT_SCALE=0 needs WRF_BWAVE_ALLOW_ZERO=1 (zero-bubble control)'
       RETURN
    END IF

    IF ( parsed > max_scale ) THEN
       ok  = .FALSE.
       msg = 'WRF_BWAVE_PERT_SCALE must be <= 1.0E3'
       RETURN
    END IF

    scale = parsed

  END SUBROUTINE bwave_pert_scale_parse
