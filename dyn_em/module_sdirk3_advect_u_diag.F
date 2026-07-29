!WRF:MODEL_LAYER:DIAGNOSTIC
!
! 9F.D42 (review P1-1, "Fortran S2-B"): the advect_u horizontal/vertical split
! diagnostic, lifted out of advect_u().
!
! WHY. advect_u() is a 1578-line advection routine that carried ~75 references to this
! probe: environment parsing with a hand-rolled lowercase loop, THREADPRIVATE SAVE
! state, dump-call parsing, two full 3-D allocatables, a shared ATOMIC counter, FP64
! reductions, norm formatting, an MPI topology query, and binary open/write/close --
! interleaved with the physics it is supposed to be measuring. The C++ side of this
! campaign (S2-A) already separated its equivalent; this is the Fortran half.
!
! The advection routine now says WHAT it wants observed at three points and nothing
! about HOW:
!     CALL advect_u_diag_begin           (diag, tendency, ...)
!     ! horizontal advection
!     CALL advect_u_diag_horizontal_done (diag, tendency)
!     ! vertical advection
!     CALL advect_u_diag_finish          (diag, tendency, ...)
!
! DEFAULT-OFF COST. With the probe disabled, a call performs: one logical test in each
! entry point. No environment access after the once-per-thread init, no allocation, no
! atomic, no reduction, no I/O. The state lives in a caller-declared derived type, so
! there is no hidden module-level buffer either.
!
! WHY A TYPE RATHER THAN MODULE SAVE VARIABLES. The snapshots and this call's ordinal
! are PER CALL; the parsed configuration is per thread. Keeping the two in one SAVE
! block is what made the original need THREADPRIVATE on some variables and not others,
! a distinction that was easy to get wrong and impossible to test. The type holds the
! per-call state; only the parsed config remains SAVE.

MODULE module_sdirk3_advect_u_diag

   IMPLICIT NONE
   PRIVATE

   PUBLIC :: advect_u_diag_type
   PUBLIC :: advect_u_diag_begin
   PUBLIC :: advect_u_diag_horizontal_done
   PUBLIC :: advect_u_diag_finish

   ! Self-contained: no USE, so this module cannot perturb compile order in dyn_em.
   INTEGER, PARAMETER :: dp = KIND(1.0D0)

   ! Parsed ONCE per thread. THREADPRIVATE because get_environment_variable and the
   ! fail-close it can trigger belong to the initialising thread, not to a lock.
   LOGICAL, SAVE :: cfg_trace_s     = .FALSE.
   LOGICAL, SAVE :: cfg_init_s      = .FALSE.
   INTEGER, SAVE :: cfg_dump_call_s = 2
!$OMP THREADPRIVATE(cfg_trace_s, cfg_init_s, cfg_dump_call_s)

   ! Shared ordinal. ATOMIC CAPTURE only -- see advect_u_diag_finish.
   INTEGER, SAVE :: call_counter_s = 0

   ! Per-call state. Caller-declared, so nothing is shared implicitly.
   TYPE advect_u_diag_type
      LOGICAL :: trace = .FALSE.
      INTEGER :: dump_call = 2
      INTEGER :: call_this = 0
      REAL, ALLOCATABLE, DIMENSION(:,:,:) :: snap_h, snap_v
   END TYPE advect_u_diag_type

CONTAINS

!-----------------------------------------------------------------------
! Parse the environment once per thread. Fails closed on a malformed value: a probe
! that silently reads as OFF for "true" or a typo produces a run that looks like the
! baseline and is not, which is the failure this campaign keeps re-encountering.
   SUBROUTINE diag_init_once()
      CHARACTER(LEN=32) :: env, val, denv
      INTEGER :: st, ln, i, c, dst, dlen

      IF ( cfg_init_s ) RETURN

      cfg_trace_s = .FALSE.
      CALL get_environment_variable("WRF_ADVECT_U_SPLIT_TRACE", env, &
                                    LENGTH=ln, STATUS=st)
      IF ( st == 0 .AND. ln > 0 ) THEN
         IF ( ln > LEN(env) ) THEN
            CALL wrf_error_fatal('WRF_ADVECT_U_SPLIT_TRACE was truncated')
         END IF
         val = env(1:ln)
         DO i = 1, ln                       ! inline lowercase; no helper to invent
            c = IACHAR( val(i:i) )
            IF ( c >= 65 .AND. c <= 90 ) val(i:i) = ACHAR( c + 32 )
         END DO
         SELECT CASE ( TRIM(val) )
            CASE ('1','true','yes','on')  ; cfg_trace_s = .TRUE.
            CASE ('0','false','no','off') ; cfg_trace_s = .FALSE.
            CASE DEFAULT
               CALL wrf_error_fatal( &
                 'WRF_ADVECT_U_SPLIT_TRACE must be one of 1/true/yes/on/0/false/no/off')
         END SELECT
      ELSE IF ( st /= 0 .AND. st /= 1 ) THEN
         CALL wrf_error_fatal('WRF_ADVECT_U_SPLIT_TRACE could not be read')
      END IF

      cfg_dump_call_s = 2
      CALL get_environment_variable("WRF_ADVECT_U_SPLIT_DUMP_CALL", denv, &
                                    LENGTH=dlen, STATUS=dst)
      IF ( dst == 0 .AND. dlen > 0 ) THEN
         READ(denv(1:dlen), *, IOSTAT=dst) cfg_dump_call_s
         IF ( dst /= 0 .OR. cfg_dump_call_s < 1 ) THEN
            CALL wrf_error_fatal('WRF_ADVECT_U_SPLIT_DUMP_CALL must be a positive integer')
         END IF
      END IF

      cfg_init_s = .TRUE.
   END SUBROUTINE diag_init_once

!-----------------------------------------------------------------------
! Snapshot the tendency before horizontal advection.
   SUBROUTINE advect_u_diag_begin( diag, tendency, &
                                   ims, ime, jms, jme, kms, kme )
      TYPE(advect_u_diag_type), INTENT(INOUT) :: diag
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: tendency

      CALL diag_init_once()
      diag%trace     = cfg_trace_s
      diag%dump_call = cfg_dump_call_s
      IF ( .NOT. diag%trace ) RETURN          ! default path stops here

      ALLOCATE( diag%snap_h(ims:ime, kms:kme, jms:jme) )
      ALLOCATE( diag%snap_v(ims:ime, kms:kme, jms:jme) )
      diag%snap_h = tendency
   END SUBROUTINE advect_u_diag_begin

!-----------------------------------------------------------------------
! Close the horizontal contribution and open the vertical one. Horizontal work
! includes the radiation-BC pickup, so this must be called after it.
   SUBROUTINE advect_u_diag_horizontal_done( diag, tendency, &
                                             ims, ime, jms, jme, kms, kme )
      TYPE(advect_u_diag_type), INTENT(INOUT) :: diag
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: tendency

      IF ( .NOT. diag%trace ) RETURN
      diag%snap_h = tendency - diag%snap_h
      diag%snap_v = tendency
   END SUBROUTINE advect_u_diag_horizontal_done

!-----------------------------------------------------------------------
! Complete the vertical contribution, report the two norms, and optionally dump the
! two FIELDS for one call.
   SUBROUTINE advect_u_diag_finish( diag, tendency, &
                                    ids, ide, jds, jde, &
                                    ims, ime, jms, jme, kms, kme, &
                                    its, ite, jts, jte, ktf )
      TYPE(advect_u_diag_type), INTENT(INOUT) :: diag
      INTEGER, INTENT(IN) :: ids, ide, jds, jde
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      INTEGER, INTENT(IN) :: its, ite, jts, jte, ktf
      REAL, DIMENSION(ims:ime, kms:kme, jms:jme), INTENT(IN) :: tendency

      REAL(dp)           :: nh, nv
      CHARACTER(LEN=160) :: line
      CHARACTER(LEN=64)  :: fname
      INTEGER            :: du, nproc, myproc, ios

      IF ( .NOT. diag%trace ) RETURN

!     9F.D28 (review section 2): ATOMIC CAPTURE, not bare ATOMIC. Plain ATOMIC makes the
!     INCREMENT indivisible but says nothing about the later READ, so two threads could
!     each increment (->2, ->3) and then BOTH read 3 -- losing the call-2 dump and
!     writing call 3 twice, with the logged ordinal not matching the tensor.
!$OMP ATOMIC CAPTURE
      call_counter_s  = call_counter_s + 1
      diag%call_this  = call_counter_s
!$OMP END ATOMIC

      diag%snap_v = tendency - diag%snap_v

!     9F.D25 (review sec 11): accumulate in DOUBLE. A conclusion quoted to 2.4% must not
!     rest on a single-precision sum of squares over O(2e5) terms, where the running
!     total is orders of magnitude above the increments and absorbs them.
!     Interior only, so halo garbage cannot inflate either norm.
      nh = SQRT( SUM( REAL( &
           diag%snap_h(its:MIN(ite,ide-1), 1:ktf, jts:MIN(jte,jde-1)), KIND=dp )**2 ) )
      nv = SQRT( SUM( REAL( &
           diag%snap_v(its:MIN(ite,ide-1), 1:ktf, jts:MIN(jte,jde-1)), KIND=dp )**2 ) )
      WRITE(line,'(A,I0,A,E24.16,A,E24.16)') &
         '[ADVECT_U_SPLIT] call=', diag%call_this, ' |horiz|=', nh, ' |vert|=', nv
      CALL wrf_message( TRIM(line) )

!     9F.D26 (review section 3): norms are NOT field parity. ||PT||=||T|| for any
!     permutation P and ||-T||=||T||, so equal magnitudes are consistent with a
!     transposed, shifted or sign-flipped field. Dump the FIELDS so an offline diff can
!     compute e2 / e_inf / correlation / per-level error / sign-disagreement.
      IF ( diag%call_this == diag%dump_call ) THEN
!        9F.D29 (review section 4): the call ordinal is PROCESS-LOCAL, so under MPI every
!        rank would write its own second call. Fail closed rather than produce a file
!        whose provenance is unknowable; the rank is in the name so the fail-close and
!        the artifact agree about what was written.
         CALL wrf_get_nproc( nproc )
         CALL wrf_get_myproc( myproc )
         IF ( nproc /= 1 ) THEN
            CALL wrf_error_fatal( &
              'WRF_ADVECT_U_SPLIT_DUMP is single-rank only: the call ordinal is '// &
              'process-local, so multiple ranks would overwrite one dump file.')
         END IF

!        9F.D42 (review P1-2): every I/O operation is CHECKED. The original relied on
!        the Fortran runtime aborting on error, which is implementation-defined -- a
!        short or failed write could leave a truncated file that a later parity run
!        reads as current evidence, and the success message was emitted regardless.
!        The message now follows a verified CLOSE, so it reports a completed artifact
!        rather than an attempted one.
         WRITE(fname,'(A,I4.4,A)') 'wrf_advect_u_split_rank', myproc, '.bin'
         OPEN(NEWUNIT=du, FILE=TRIM(fname), FORM="unformatted", &
              ACCESS="stream", STATUS="replace", IOSTAT=ios)
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] cannot open '//TRIM(fname))

         WRITE(du, IOSTAT=ios) ims, ime, jms, jme, kms, kme
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] header write failed for '//TRIM(fname))
         WRITE(du, IOSTAT=ios) diag%snap_h
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] horizontal field write failed for '//TRIM(fname))
         WRITE(du, IOSTAT=ios) diag%snap_v
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] vertical field write failed for '//TRIM(fname))

         CLOSE(du, IOSTAT=ios)
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] close failed for '//TRIM(fname))

         CALL wrf_message('[ADVECT_U_SPLIT] wrote '//TRIM(fname))
      END IF

      DEALLOCATE( diag%snap_h )
      DEALLOCATE( diag%snap_v )
   END SUBROUTINE advect_u_diag_finish

END MODULE module_sdirk3_advect_u_diag
