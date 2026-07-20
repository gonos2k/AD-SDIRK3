! PR 9F.7 C4: Fortran <-> C++ RHS-lifecycle ABI contract.
!
! The core-linux CI builds the sdirk3 C++ library and its torch-free C++ contracts,
! but it NEVER compiles a line of Fortran -- so a drift between the Fortran integer
! constants / BIND(C) calling convention and the C++ enum / ABI escapes CI entirely
! and only surfaces in a full WRF build (this campaign hit exactly that class of bug).
!
! This tiny program links the REAL extern "C" symbols and drives the lifecycle the way
! Fortran advance_implicit does: begin -> already-running -> clean finalize -> re-init
! -> fatal close -> (rejected) re-open after fatal. It proves the constants agree and
! the calls link, without a full WRF build. Counting must be ENABLED
! (WRF_SDIRK3_RHS_COUNT=1) or begin reports DISABLED and the asserts below fail loudly.
program test_rhs_lifecycle_abi
   use iso_c_binding
   implicit none

   ! Must match wrf_sdirk3_stage_history_diag.h exactly (that agreement is the point).
   integer(c_int), parameter :: EXIT_CLEAN = 0, EXIT_FATAL = 1
   integer(c_int), parameter :: REASON_STEP_OUTCOME = 1, REASON_FINALIZE = 3, &
                                REASON_TOPOLOGY = 6
   integer(c_int), parameter :: BEGIN_OPENED = 1, BEGIN_ALREADY_RUNNING = 0, &
                                BEGIN_REJECTED_FATAL = -1

   interface
      function sdirk3_rhs_run_begin_checked() &
         bind(C, name="sdirk3_rhs_run_begin_checked") result(status)
         import :: c_int
         integer(c_int) :: status
      end function sdirk3_rhs_run_begin_checked
      function sdirk3_rhs_run_close_checked(exit_kind, reason) &
         bind(C, name="sdirk3_rhs_run_close_checked") result(status)
         import :: c_int
         integer(c_int), value :: exit_kind, reason
         integer(c_int) :: status
      end function sdirk3_rhs_run_close_checked
   end interface

   integer :: fails
   integer(c_int) :: s
   fails = 0

   ! generation 1: open, then idempotent re-call is ALREADY_RUNNING.
   s = sdirk3_rhs_run_begin_checked()
   call expect(s, BEGIN_OPENED, "begin gen1 -> OPENED")
   s = sdirk3_rhs_run_begin_checked()
   call expect(s, BEGIN_ALREADY_RUNNING, "begin again -> ALREADY_RUNNING")

   ! clean finalize closes generation 1.
   s = sdirk3_rhs_run_close_checked(EXIT_CLEAN, REASON_FINALIZE)
   call expect(s, 1_c_int, "clean finalize close -> 1")

   ! re-init opens generation 2 (P1-3: clean finalize permits re-open).
   s = sdirk3_rhs_run_begin_checked()
   call expect(s, BEGIN_OPENED, "re-init after clean finalize -> OPENED (gen2)")

   ! a fatal close ends generation 2.
   s = sdirk3_rhs_run_close_checked(EXIT_FATAL, REASON_STEP_OUTCOME)
   call expect(s, 1_c_int, "fatal close -> 1")

   ! P1-3: begin after a FATAL close must be REJECTED, not re-open a generation.
   s = sdirk3_rhs_run_begin_checked()
   call expect(s, BEGIN_REJECTED_FATAL, "re-open after fatal -> REJECTED_FATAL (-1)")

   ! P1-4: the ABI rejects an invalid (exit, reason) combination with status 0.
   s = sdirk3_rhs_run_close_checked(EXIT_CLEAN, REASON_TOPOLOGY)
   call expect(s, 0_c_int, "clean+topology is an invalid combo -> 0")

   if (fails == 0) then
      print '(A)', "FORTRAN_ABI_CONTRACT: ALL PASS"
      call exit(0)
   else
      print '(A,I0,A)', "FORTRAN_ABI_CONTRACT: ", fails, " FAILURE(S)"
      call exit(1)
   end if

contains
   subroutine expect(got, want, label)
      integer(c_int), intent(in) :: got, want
      character(len=*), intent(in) :: label
      if (got /= want) then
         print '(A,A,A,I0,A,I0)', "  FAIL: ", label, " -- got ", got, " want ", want
         fails = fails + 1
      else
         print '(A,A,A,I0,A)', "  PASS: ", label, " (", got, ")"
      end if
   end subroutine expect
end program test_rhs_lifecycle_abi
