! Torch-variant driver (probe 6 only).
program eh_fmain_torch
  use, intrinsic :: iso_c_binding
  implicit none
  interface
     integer(c_int) function eh_probe_torch_run() bind(C, name="eh_probe_torch_run")
       import :: c_int
     end function
  end interface
  integer :: rc
  rc = eh_probe_torch_run()
  if (rc == 1) then
     print *, "EH_PROBE_PASS 6"
  else
     print *, "EH_PROBE_FAIL 6 rc=", rc
  end if
end program
