! PR 9C.3: Fortran driver for the exception-link probes — the point is the
! FINAL LINK SHAPE, so the main program must be Fortran (as in wrf.exe).
program eh_fmain
  use, intrinsic :: iso_c_binding
  implicit none
  interface
     integer(c_int) function eh_probe_run(which) bind(C, name="eh_probe_run")
       import :: c_int
       integer(c_int), value :: which
     end function
  end interface
  character(len=8) :: arg
  integer :: which, rc
  call get_command_argument(1, arg)
  read(arg, *) which
  rc = eh_probe_run(int(which, c_int))
  if (rc == 1) then
     print *, "EH_PROBE_PASS", which
  else
     print *, "EH_PROBE_FAIL", which, "rc=", rc
  end if
end program
