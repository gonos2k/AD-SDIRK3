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
   PUBLIC :: thm_advect_diag_type, thm_advect_diag_arm, thm_advect_diag_disarm
   PUBLIC :: thm_advect_diag_begin, thm_advect_diag_horizontal_done, thm_advect_diag_finish
   PUBLIC :: ph_rhs_diag_type, ph_rhs_diag_arm, ph_rhs_diag_disarm
   PUBLIC :: ph_rhs_diag_begin, ph_rhs_diag_vertical_done, ph_rhs_diag_top_reset
   PUBLIC :: ph_rhs_diag_gw_done, ph_rhs_diag_finish
   PUBLIC :: write_ph_thm_omega_snapshot, write_thm_mix_parts, write_ph_thm_add_parts
   PUBLIC :: ph_thm_session_arm, ph_thm_session_disarm, ph_thm_session_is_armed

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
!  9F.D46 (review P1 section 8): the hooks must run in order, and the type now says so.
!  snap_v is ALLOCATED by begin() but first WRITTEN by horizontal_done(); finish() reads
!  it. A refactor that dropped the middle hook would compute the vertical field from
!  uninitialised memory -- a plausible-looking wrong artifact, not a crash. One integer
!  makes that unrepresentable; no state machine required.
   INTEGER, PARAMETER, PUBLIC :: DIAG_IDLE            = 0
   INTEGER, PARAMETER, PUBLIC :: DIAG_BEGUN           = 1
   INTEGER, PARAMETER, PUBLIC :: DIAG_HORIZONTAL_DONE = 2

   TYPE advect_u_diag_type
      LOGICAL :: trace = .FALSE.
      INTEGER :: dump_call = 2
      INTEGER :: call_this = 0
      INTEGER :: phase = DIAG_IDLE
      REAL, ALLOCATABLE, DIMENSION(:,:,:) :: snap_h, snap_v
   END TYPE advect_u_diag_type

   ! Narrow first-call component observers for the actual THM scalar and PH calls.
   ! module_em arms these only around the known t/PH call sites under the existing
   ! WRF_PARITY_RK_TEND_DUMP gate; no routine is re-evaluated.
   TYPE thm_advect_diag_type
      LOGICAL :: trace = .FALSE.
      REAL, ALLOCATABLE, DIMENSION(:,:,:) :: field, rom, before, after_h
   END TYPE thm_advect_diag_type

   TYPE ph_rhs_diag_type
      LOGICAL :: trace = .FALSE.
      REAL, ALLOCATABLE, DIMENSION(:,:,:) :: field, ww, before, after_v, after_reset, after_gw
   END TYPE ph_rhs_diag_type

   LOGICAL, SAVE :: parity_thm_advect_armed = .FALSE.
   LOGICAL, SAVE :: parity_ph_rhs_armed = .FALSE.
!$OMP THREADPRIVATE(parity_thm_advect_armed, parity_ph_rhs_armed)
   LOGICAL, SAVE :: parity_ph_thm_session = .FALSE.

CONTAINS

! The one-rank/one-tile parity capture spans rk_tendency through rk_addtend_dry,
! which are separate OpenMP regions. This process-local latch carries only that
! existing gated first-call context; it is never set when the parity env is off.
   SUBROUTINE ph_thm_session_arm()
      parity_ph_thm_session = .TRUE.
   END SUBROUTINE ph_thm_session_arm

   SUBROUTINE ph_thm_session_disarm()
      parity_ph_thm_session = .FALSE.
   END SUBROUTINE ph_thm_session_disarm

   LOGICAL FUNCTION ph_thm_session_is_armed()
      ph_thm_session_is_armed = parity_ph_thm_session
   END FUNCTION ph_thm_session_is_armed

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

!     9F.D46 (review P1 section 7): every non-zero STATUS is handled. The old test was
!     IF ( dst == 0 .AND. dlen > 0 ), so a TRUNCATION or any other read error fell through
!     the whole block and silently kept the default of 2 -- an operator asking for call 7
!     would get call 2 recorded, with nothing said. The TRACE variable directly above
!     already failed closed on exactly these statuses; that inconsistency is what hid it.
      cfg_dump_call_s = 2
      CALL get_environment_variable("WRF_ADVECT_U_SPLIT_DUMP_CALL", denv, &
                                    LENGTH=dlen, STATUS=dst)
      IF ( dst == 1 ) THEN
!        Not set. Keep the default -- the only silent path here, and the correct one.
         CONTINUE
      ELSE IF ( dst /= 0 ) THEN
         CALL wrf_error_fatal( &
           'WRF_ADVECT_U_SPLIT_DUMP_CALL could not be read (get_environment_variable status /= 0/1)')
      ELSE IF ( dlen > LEN(denv) ) THEN
         CALL wrf_error_fatal('WRF_ADVECT_U_SPLIT_DUMP_CALL was truncated')
      ELSE IF ( dlen == 0 ) THEN
!        Present but empty: treated as unset, matching the TRACE variable's behaviour.
         CONTINUE
      ELSE
!        9F.D46: digits ONLY, whole string. A list-directed READ is not strict -- it stops
!        at the first non-numeric character, so "2junk" and "3.5" would be accepted as 2
!        and 3, silently dumping a different call than the one requested.
         DO i = 1, dlen
            c = IACHAR( denv(i:i) )
            IF ( c < 48 .OR. c > 57 ) THEN
               CALL wrf_error_fatal( &
                 'WRF_ADVECT_U_SPLIT_DUMP_CALL must be digits only, got "'// &
                 TRIM(denv(1:dlen))//'"')
            END IF
         END DO
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

!     9F.D46 (section 8): IDLE -> BEGUN. A second begin() without an intervening finish()
!     would leak the previous allocation and silently restart the measurement.
      IF ( diag%phase /= DIAG_IDLE ) THEN
         CALL wrf_error_fatal('advect_u_diag_begin called out of order (expected IDLE)')
      END IF
      diag%phase = DIAG_BEGUN

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
      IF ( diag%phase /= DIAG_BEGUN ) THEN
         CALL wrf_error_fatal( &
           'advect_u_diag_horizontal_done called out of order (expected BEGUN)')
      END IF
      diag%snap_h = tendency - diag%snap_h
      diag%snap_v = tendency
      diag%phase = DIAG_HORIZONTAL_DONE
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
      CHARACTER(LEN=72)  :: tmpname
      INTEGER            :: du, nproc, myproc, ios

      IF ( .NOT. diag%trace ) RETURN

!     9F.D46 (section 8): HORIZONTAL_DONE -> IDLE. Arriving straight from BEGUN means
!     snap_v was allocated but never written, so the vertical norm and the dumped vertical
!     field would both come from uninitialised memory.
      IF ( diag%phase /= DIAG_HORIZONTAL_DONE ) THEN
         CALL wrf_error_fatal( &
           'advect_u_diag_finish called out of order (expected HORIZONTAL_DONE -- was '// &
           'advect_u_diag_horizontal_done skipped?)')
      END IF

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
!        9F.D46 (review P1 section 9): write to a TEMPORARY name and publish by rename
!        only after a verified close. STATUS="replace" on the final name meant a process
!        killed mid-write left a truncated file called wrf_advect_u_split_rankNNNN.bin --
!        and a later run whose dump target was never reached would let the parity tool
!        read that fragment as the current result. The C++ side already does this; the two
!        artifacts of one comparison should not have different publication guarantees.
         WRITE(fname,'(A,I4.4,A)') 'wrf_advect_u_split_rank', myproc, '.bin'
         tmpname = TRIM(fname)//'.tmp'
         OPEN(NEWUNIT=du, FILE=TRIM(tmpname), FORM="unformatted", &
              ACCESS="stream", STATUS="replace", IOSTAT=ios)
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] cannot open '//TRIM(tmpname))

         WRITE(du, IOSTAT=ios) ims, ime, jms, jme, kms, kme
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] header write failed for '//TRIM(tmpname))
         WRITE(du, IOSTAT=ios) diag%snap_h
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] horizontal field write failed for '//TRIM(tmpname))
         WRITE(du, IOSTAT=ios) diag%snap_v
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] vertical field write failed for '//TRIM(tmpname))

         CLOSE(du, IOSTAT=ios)
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] close failed for '//TRIM(tmpname))

!        Publish. RENAME is a GNU extension; the status is CHECKED rather than assumed so
!        a platform without it fails loudly instead of leaving only the .tmp behind.
         CALL RENAME( TRIM(tmpname), TRIM(fname), ios )
         IF ( ios /= 0 ) CALL wrf_error_fatal( &
              '[ADVECT_U_SPLIT] could not publish '//TRIM(tmpname)//' -> '//TRIM(fname))

!        Only now does the final name exist, and only with a complete artifact behind it.
         CALL wrf_message('[ADVECT_U_SPLIT] wrote '//TRIM(fname))
      END IF

      DEALLOCATE( diag%snap_h )
      DEALLOCATE( diag%snap_v )
      diag%phase = DIAG_IDLE
   END SUBROUTINE advect_u_diag_finish

!-----------------------------------------------------------------------
! The caller arms these only around rk_tendency's explicit THM scalar call and
! rhs_ph. THREADPRIVATE keeps the small capture context attached to its OpenMP
! call thread; default execution adds no allocations or I/O in these routines.
   SUBROUTINE thm_advect_diag_arm()
      parity_thm_advect_armed = .TRUE.
   END SUBROUTINE thm_advect_diag_arm

   SUBROUTINE thm_advect_diag_disarm()
      parity_thm_advect_armed = .FALSE.
   END SUBROUTINE thm_advect_diag_disarm

   SUBROUTINE thm_advect_diag_begin( diag, field, tendency, rom )
      TYPE(thm_advect_diag_type), INTENT(INOUT) :: diag
      REAL, DIMENSION(:,:,:), INTENT(IN) :: field, rom, tendency
      diag%trace = parity_thm_advect_armed
      IF ( .NOT. diag%trace ) RETURN
      ALLOCATE(diag%field(SIZE(field,1),SIZE(field,2),SIZE(field,3)))
      ALLOCATE(diag%rom(SIZE(rom,1),SIZE(rom,2),SIZE(rom,3)))
      ALLOCATE(diag%before(SIZE(tendency,1),SIZE(tendency,2),SIZE(tendency,3)))
      ALLOCATE(diag%after_h(SIZE(tendency,1),SIZE(tendency,2),SIZE(tendency,3)))
      diag%field=field
      diag%rom=rom
      diag%before=tendency
      diag%after_h=tendency
   END SUBROUTINE thm_advect_diag_begin

   SUBROUTINE thm_advect_diag_horizontal_done( diag, tendency )
      TYPE(thm_advect_diag_type), INTENT(INOUT) :: diag
      REAL, DIMENSION(:,:,:), INTENT(IN) :: tendency
      IF ( .NOT. diag%trace ) RETURN
      diag%after_h = tendency
   END SUBROUTINE thm_advect_diag_horizontal_done

   SUBROUTINE thm_advect_diag_finish( diag, tendency, &
                                      ids, ide, jds, jde, kds, kde, &
                                      ims, ime, jms, jme, kms, kme, &
                                      rdx, rdy, time_step, horz_order, vert_order, scalar_adv_opt, &
                                      fzm, fzp, rdzw )
      TYPE(thm_advect_diag_type), INTENT(INOUT) :: diag
      INTEGER, INTENT(IN) :: ids, ide, jds, jde, kds, kde
      INTEGER, INTENT(IN) :: ims, ime, jms, jme, kms, kme
      INTEGER, INTENT(IN) :: time_step, horz_order, vert_order, scalar_adv_opt
      REAL, INTENT(IN) :: rdx, rdy
      REAL, DIMENSION(kms:kme), INTENT(IN) :: fzm, fzp, rdzw
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN) :: tendency
      INTEGER :: du, ios, iclose
      REAL(KIND=4) :: fdx, fdy

      IF ( .NOT. diag%trace ) RETURN
      fdx = rdx
      fdy = rdy
      OPEN(NEWUNIT=du, FILE='wrf_thm_advect_parts.bin', FORM='unformatted', &
           ACCESS='stream', STATUS='replace', CONVERT='little_endian', IOSTAT=ios)
      IF ( ios /= 0 ) CALL wrf_error_fatal('[THM ADV DIAG] cannot open wrf_thm_advect_parts.bin')
      WRITE(du,IOSTAT=ios) 1_4, ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,fdx,fdy, &
                           1_4,1_4,time_step,horz_order,vert_order,scalar_adv_opt
      IF ( ios == 0 ) WRITE(du,IOSTAT=ios) diag%field,diag%rom,diag%before,diag%after_h,tendency
      IF ( ios == 0 ) WRITE(du,IOSTAT=ios) fzm,fzp,rdzw
      CLOSE(du,IOSTAT=iclose)
      IF ( ios /= 0 .OR. iclose /= 0 ) CALL wrf_error_fatal('[THM ADV DIAG] failed writing wrf_thm_advect_parts.bin')
      CALL wrf_message('[THM ADV DIAG] wrote wrf_thm_advect_parts.bin')
      DEALLOCATE(diag%field,diag%rom,diag%before,diag%after_h)
      diag%trace = .FALSE.
   END SUBROUTINE thm_advect_diag_finish

!-----------------------------------------------------------------------
   SUBROUTINE ph_rhs_diag_arm()
      parity_ph_rhs_armed = .TRUE.
   END SUBROUTINE ph_rhs_diag_arm

   SUBROUTINE ph_rhs_diag_disarm()
      parity_ph_rhs_armed = .FALSE.
   END SUBROUTINE ph_rhs_diag_disarm

   SUBROUTINE ph_rhs_diag_begin( diag, field, ww, tendency )
      TYPE(ph_rhs_diag_type), INTENT(INOUT) :: diag
      REAL, DIMENSION(:,:,:), INTENT(IN) :: field, ww, tendency
      diag%trace = parity_ph_rhs_armed
      IF ( .NOT. diag%trace ) RETURN
      ALLOCATE(diag%field(SIZE(field,1),SIZE(field,2),SIZE(field,3)))
      ALLOCATE(diag%ww(SIZE(ww,1),SIZE(ww,2),SIZE(ww,3)))
      ALLOCATE(diag%before(SIZE(tendency,1),SIZE(tendency,2),SIZE(tendency,3)))
      ALLOCATE(diag%after_v(SIZE(tendency,1),SIZE(tendency,2),SIZE(tendency,3)))
      ALLOCATE(diag%after_reset(SIZE(tendency,1),SIZE(tendency,2),SIZE(tendency,3)))
      ALLOCATE(diag%after_gw(SIZE(tendency,1),SIZE(tendency,2),SIZE(tendency,3)))
      diag%field=field
      diag%ww=ww
      diag%before=tendency
      diag%after_v=tendency
      diag%after_reset=tendency
      diag%after_gw=tendency
   END SUBROUTINE ph_rhs_diag_begin

   SUBROUTINE ph_rhs_diag_vertical_done( diag, tendency )
      TYPE(ph_rhs_diag_type), INTENT(INOUT) :: diag
      REAL, DIMENSION(:,:,:), INTENT(IN) :: tendency
      IF ( .NOT. diag%trace ) RETURN
      diag%after_v = tendency
      diag%after_reset = tendency
   END SUBROUTINE ph_rhs_diag_vertical_done

   SUBROUTINE ph_rhs_diag_top_reset( diag, tendency, j, kde, jms, kms )
      TYPE(ph_rhs_diag_type), INTENT(INOUT) :: diag
      REAL, DIMENSION(:,:,:), INTENT(IN) :: tendency
      INTEGER, INTENT(IN) :: j, kde, jms, kms
      INTEGER :: joff, koff
      IF ( .NOT. diag%trace ) RETURN
      ! rhs_ph resets one j-row and then adds GW on that row before advancing j.
      ! Save only the boundary row here so earlier rows' GW increments are not folded
      ! into the boundary-reset component.
      joff = j - jms + 1
      koff = kde - kms + 1
      IF (joff < 1 .OR. joff > SIZE(diag%after_reset,3) .OR. &
          koff < 1 .OR. koff > SIZE(diag%after_reset,2)) &
         CALL wrf_error_fatal('[PH RHS DIAG] top-reset index is outside captured memory bounds')
      diag%after_reset(:,koff,joff) = tendency(:,koff,joff)
   END SUBROUTINE ph_rhs_diag_top_reset

   SUBROUTINE ph_rhs_diag_gw_done( diag, tendency )
      TYPE(ph_rhs_diag_type), INTENT(INOUT) :: diag
      REAL, DIMENSION(:,:,:), INTENT(IN) :: tendency
      IF ( .NOT. diag%trace ) RETURN
      diag%after_gw = tendency
   END SUBROUTINE ph_rhs_diag_gw_done

   SUBROUTINE ph_rhs_diag_finish( diag, tendency, &
                                  ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
                                  rdx,rdy,advective_order,phi_adv_z,non_hydrostatic,top_lid, &
                                  fnm,fnp,rdnw )
      TYPE(ph_rhs_diag_type), INTENT(INOUT) :: diag
      INTEGER, INTENT(IN) :: ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme
      INTEGER, INTENT(IN) :: advective_order,phi_adv_z
      REAL, INTENT(IN) :: rdx,rdy
      REAL, DIMENSION(kms:kme), INTENT(IN) :: fnm,fnp,rdnw
      LOGICAL, INTENT(IN) :: non_hydrostatic,top_lid
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN) :: tendency
      INTEGER :: du,ios,iclose
      INTEGER(KIND=4) :: nh,tl
      REAL(KIND=4) :: fdx,fdy

      IF ( .NOT. diag%trace ) RETURN
      nh=MERGE(1_4,0_4,non_hydrostatic)
      tl=MERGE(1_4,0_4,top_lid)
      fdx=rdx
      fdy=rdy
      OPEN(NEWUNIT=du, FILE='wrf_ph_rhs_parts.bin', FORM='unformatted', &
           ACCESS='stream', STATUS='replace', CONVERT='little_endian', IOSTAT=ios)
      IF ( ios /= 0 ) CALL wrf_error_fatal('[PH RHS DIAG] cannot open wrf_ph_rhs_parts.bin')
      WRITE(du,IOSTAT=ios) 1_4,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,fdx,fdy, &
                           1_4,1_4,advective_order,phi_adv_z,nh,tl
      IF ( ios == 0 ) WRITE(du,IOSTAT=ios) diag%field,diag%ww,diag%before,diag%after_v, &
                                           diag%after_reset,diag%after_gw,tendency
      IF ( ios == 0 ) WRITE(du,IOSTAT=ios) fnm,fnp,rdnw
      CLOSE(du,IOSTAT=iclose)
      IF ( ios /= 0 .OR. iclose /= 0 ) CALL wrf_error_fatal('[PH RHS DIAG] failed writing wrf_ph_rhs_parts.bin')
      CALL wrf_message('[PH RHS DIAG] wrote wrf_ph_rhs_parts.bin')
      DEALLOCATE(diag%field,diag%ww,diag%before,diag%after_v,diag%after_reset,diag%after_gw)
      diag%trace=.FALSE.
   END SUBROUTINE ph_rhs_diag_finish

!-----------------------------------------------------------------------
! Companion records use the same explicit, little-endian stream contract as the
! existing Full observer. They contain only operands/results from the one live call.
   SUBROUTINE write_ph_thm_omega_snapshot( t, wwE, wwI, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,rdx,rdy,dt,time_step, &
       rk_step,ieva,phi_adv_z,h_sca_order,v_sca_order,scalar_adv_opt,nonhydro,top_lid,zadvect_implicit )
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN) :: t,wwE,wwI
      INTEGER, INTENT(IN) :: ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme
      INTEGER, INTENT(IN) :: time_step,rk_step,phi_adv_z,h_sca_order,v_sca_order,scalar_adv_opt
      REAL, INTENT(IN) :: rdx,rdy,dt
      LOGICAL, INTENT(IN) :: ieva,nonhydro,top_lid
      INTEGER, INTENT(IN) :: zadvect_implicit
      INTEGER :: du,ios,iclose
      INTEGER(KIND=4) :: ieva_i,nh_i,top_i
      REAL(KIND=4) :: fdx,fdy,fdt
      ieva_i=MERGE(1_4,0_4,ieva); nh_i=MERGE(1_4,0_4,nonhydro)
      top_i=MERGE(1_4,0_4,top_lid)
      fdx=rdx; fdy=rdy; fdt=dt
      OPEN(NEWUNIT=du,FILE='wrf_ph_thm_omega.bin',FORM='unformatted',ACCESS='stream', &
           STATUS='replace',CONVERT='little_endian',IOSTAT=ios)
      IF (ios /= 0) CALL wrf_error_fatal('[PH/THM DIAG] cannot open wrf_ph_thm_omega.bin')
      WRITE(du,IOSTAT=ios) 1_4,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,fdx,fdy,fdt, &
                           1_4,time_step,rk_step,ieva_i,phi_adv_z,h_sca_order,v_sca_order,scalar_adv_opt,nh_i,top_i,zadvect_implicit
      IF (ios == 0) WRITE(du,IOSTAT=ios) t,wwE,wwI
      CLOSE(du,IOSTAT=iclose)
      IF (ios /= 0 .OR. iclose /= 0) CALL wrf_error_fatal('[PH/THM DIAG] failed writing wrf_ph_thm_omega.bin')
      CALL wrf_message('[PH/THM DIAG] wrote wrf_ph_thm_omega.bin')
   END SUBROUTINE write_ph_thm_omega_snapshot

   SUBROUTINE write_thm_mix_parts( before_v,after_v,before_h,after_h, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,rdx,rdy,itimestep,rk_step, &
       diff_opt,km_opt,pbl_opt,use_theta_m,mix_full_fields )
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN) :: before_v,after_v,before_h,after_h
      INTEGER, INTENT(IN) :: ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme
      INTEGER, INTENT(IN) :: itimestep,rk_step,diff_opt,km_opt,pbl_opt
      REAL, INTENT(IN) :: rdx,rdy
      INTEGER, INTENT(IN) :: use_theta_m
      LOGICAL, INTENT(IN) :: mix_full_fields
      INTEGER :: du,ios,iclose
      INTEGER(KIND=4) :: theta_i,mix_i
      REAL(KIND=4) :: fdx,fdy
      theta_i=use_theta_m; mix_i=MERGE(1_4,0_4,mix_full_fields)
      fdx=rdx; fdy=rdy
      OPEN(NEWUNIT=du,FILE='wrf_thm_mix_parts.bin',FORM='unformatted',ACCESS='stream', &
           STATUS='replace',CONVERT='little_endian',IOSTAT=ios)
      IF (ios /= 0) CALL wrf_error_fatal('[THM MIX DIAG] cannot open wrf_thm_mix_parts.bin')
      WRITE(du,IOSTAT=ios) 1_4,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,fdx,fdy, &
                           itimestep,rk_step,diff_opt,km_opt,pbl_opt,theta_i,mix_i
      IF (ios == 0) WRITE(du,IOSTAT=ios) before_v,after_v,before_h,after_h
      CLOSE(du,IOSTAT=iclose)
      IF (ios /= 0 .OR. iclose /= 0) CALL wrf_error_fatal('[THM MIX DIAG] failed writing wrf_thm_mix_parts.bin')
      CALL wrf_message('[THM MIX DIAG] wrote wrf_thm_mix_parts.bin')
   END SUBROUTINE write_thm_mix_parts

   SUBROUTINE write_ph_thm_add_parts( ph_before,t_before,ph_after,t_after, &
       ph_tendf_before,t_tendf_before,ph_save,t_save,h_diabatic, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,rk_step )
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN) :: ph_before,t_before,ph_after,t_after
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN) :: ph_tendf_before,t_tendf_before
      REAL, DIMENSION(ims:ime,kms:kme,jms:jme), INTENT(IN) :: ph_save,t_save,h_diabatic
      INTEGER, INTENT(IN) :: ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,rk_step
      INTEGER :: du,ios,iclose
      OPEN(NEWUNIT=du,FILE='wrf_ph_thm_add_parts.bin',FORM='unformatted',ACCESS='stream', &
           STATUS='replace',CONVERT='little_endian',IOSTAT=ios)
      IF (ios /= 0) CALL wrf_error_fatal('[PH/THM ADD DIAG] cannot open wrf_ph_thm_add_parts.bin')
      WRITE(du,IOSTAT=ios) 1_4,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,1_4,rk_step
      IF (ios == 0) WRITE(du,IOSTAT=ios) ph_before,t_before,ph_after,t_after, &
                                          ph_tendf_before,t_tendf_before,ph_save,t_save,h_diabatic
      CLOSE(du,IOSTAT=iclose)
      IF (ios /= 0 .OR. iclose /= 0) CALL wrf_error_fatal('[PH/THM ADD DIAG] failed writing wrf_ph_thm_add_parts.bin')
      CALL wrf_message('[PH/THM ADD DIAG] wrote wrf_ph_thm_add_parts.bin')
   END SUBROUTINE write_ph_thm_add_parts

END MODULE module_sdirk3_advect_u_diag
