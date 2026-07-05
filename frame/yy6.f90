!STARTOFREGISTRYGENERATEDINCLUDE 'inc/nl_config.inc'
!
! WARNING This file is generated automatically by use_registry
! using the data base in the file named Registry.
! Do not edit.  Your changes to this file will be lost.
!

SUBROUTINE nl_get_auxinput22_end_m ( id_id , auxinput22_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput22_end_m
  INTEGER id_id
  auxinput22_end_m = model_config_rec%auxinput22_end_m(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput22_end_m
SUBROUTINE nl_get_auxinput22_end_s ( id_id , auxinput22_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput22_end_s
  INTEGER id_id
  auxinput22_end_s = model_config_rec%auxinput22_end_s(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput22_end_s
SUBROUTINE nl_get_auxinput22_end ( id_id , auxinput22_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput22_end
  INTEGER id_id
  auxinput22_end = model_config_rec%auxinput22_end(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput22_end
SUBROUTINE nl_get_io_form_auxinput22 ( id_id , io_form_auxinput22 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_auxinput22
  INTEGER id_id
  io_form_auxinput22 = model_config_rec%io_form_auxinput22
  RETURN
END SUBROUTINE nl_get_io_form_auxinput22
SUBROUTINE nl_get_frames_per_auxinput22 ( id_id , frames_per_auxinput22 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: frames_per_auxinput22
  INTEGER id_id
  frames_per_auxinput22 = model_config_rec%frames_per_auxinput22(id_id)
  RETURN
END SUBROUTINE nl_get_frames_per_auxinput22
SUBROUTINE nl_get_auxinput23_inname ( id_id , auxinput23_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: auxinput23_inname
  INTEGER id_id
  auxinput23_inname = trim(model_config_rec%auxinput23_inname)
  RETURN
END SUBROUTINE nl_get_auxinput23_inname
SUBROUTINE nl_get_auxinput23_outname ( id_id , auxinput23_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: auxinput23_outname
  INTEGER id_id
  auxinput23_outname = trim(model_config_rec%auxinput23_outname)
  RETURN
END SUBROUTINE nl_get_auxinput23_outname
SUBROUTINE nl_get_auxinput23_interval_y ( id_id , auxinput23_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_interval_y
  INTEGER id_id
  auxinput23_interval_y = model_config_rec%auxinput23_interval_y(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_interval_y
SUBROUTINE nl_get_auxinput23_interval_d ( id_id , auxinput23_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_interval_d
  INTEGER id_id
  auxinput23_interval_d = model_config_rec%auxinput23_interval_d(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_interval_d
SUBROUTINE nl_get_auxinput23_interval_h ( id_id , auxinput23_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_interval_h
  INTEGER id_id
  auxinput23_interval_h = model_config_rec%auxinput23_interval_h(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_interval_h
SUBROUTINE nl_get_auxinput23_interval_m ( id_id , auxinput23_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_interval_m
  INTEGER id_id
  auxinput23_interval_m = model_config_rec%auxinput23_interval_m(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_interval_m
SUBROUTINE nl_get_auxinput23_interval_s ( id_id , auxinput23_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_interval_s
  INTEGER id_id
  auxinput23_interval_s = model_config_rec%auxinput23_interval_s(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_interval_s
SUBROUTINE nl_get_auxinput23_interval ( id_id , auxinput23_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_interval
  INTEGER id_id
  auxinput23_interval = model_config_rec%auxinput23_interval(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_interval
SUBROUTINE nl_get_auxinput23_begin_y ( id_id , auxinput23_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_begin_y
  INTEGER id_id
  auxinput23_begin_y = model_config_rec%auxinput23_begin_y(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_begin_y
SUBROUTINE nl_get_auxinput23_begin_d ( id_id , auxinput23_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_begin_d
  INTEGER id_id
  auxinput23_begin_d = model_config_rec%auxinput23_begin_d(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_begin_d
SUBROUTINE nl_get_auxinput23_begin_h ( id_id , auxinput23_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_begin_h
  INTEGER id_id
  auxinput23_begin_h = model_config_rec%auxinput23_begin_h(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_begin_h
SUBROUTINE nl_get_auxinput23_begin_m ( id_id , auxinput23_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_begin_m
  INTEGER id_id
  auxinput23_begin_m = model_config_rec%auxinput23_begin_m(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_begin_m
SUBROUTINE nl_get_auxinput23_begin_s ( id_id , auxinput23_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_begin_s
  INTEGER id_id
  auxinput23_begin_s = model_config_rec%auxinput23_begin_s(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_begin_s
SUBROUTINE nl_get_auxinput23_begin ( id_id , auxinput23_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_begin
  INTEGER id_id
  auxinput23_begin = model_config_rec%auxinput23_begin(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_begin
SUBROUTINE nl_get_auxinput23_end_y ( id_id , auxinput23_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_end_y
  INTEGER id_id
  auxinput23_end_y = model_config_rec%auxinput23_end_y(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_end_y
SUBROUTINE nl_get_auxinput23_end_d ( id_id , auxinput23_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_end_d
  INTEGER id_id
  auxinput23_end_d = model_config_rec%auxinput23_end_d(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_end_d
SUBROUTINE nl_get_auxinput23_end_h ( id_id , auxinput23_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_end_h
  INTEGER id_id
  auxinput23_end_h = model_config_rec%auxinput23_end_h(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_end_h
SUBROUTINE nl_get_auxinput23_end_m ( id_id , auxinput23_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_end_m
  INTEGER id_id
  auxinput23_end_m = model_config_rec%auxinput23_end_m(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_end_m
SUBROUTINE nl_get_auxinput23_end_s ( id_id , auxinput23_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_end_s
  INTEGER id_id
  auxinput23_end_s = model_config_rec%auxinput23_end_s(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_end_s
SUBROUTINE nl_get_auxinput23_end ( id_id , auxinput23_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput23_end
  INTEGER id_id
  auxinput23_end = model_config_rec%auxinput23_end(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput23_end
SUBROUTINE nl_get_io_form_auxinput23 ( id_id , io_form_auxinput23 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_auxinput23
  INTEGER id_id
  io_form_auxinput23 = model_config_rec%io_form_auxinput23
  RETURN
END SUBROUTINE nl_get_io_form_auxinput23
SUBROUTINE nl_get_frames_per_auxinput23 ( id_id , frames_per_auxinput23 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: frames_per_auxinput23
  INTEGER id_id
  frames_per_auxinput23 = model_config_rec%frames_per_auxinput23(id_id)
  RETURN
END SUBROUTINE nl_get_frames_per_auxinput23
SUBROUTINE nl_get_auxinput24_inname ( id_id , auxinput24_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: auxinput24_inname
  INTEGER id_id
  auxinput24_inname = trim(model_config_rec%auxinput24_inname)
  RETURN
END SUBROUTINE nl_get_auxinput24_inname
SUBROUTINE nl_get_auxinput24_outname ( id_id , auxinput24_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: auxinput24_outname
  INTEGER id_id
  auxinput24_outname = trim(model_config_rec%auxinput24_outname)
  RETURN
END SUBROUTINE nl_get_auxinput24_outname
SUBROUTINE nl_get_auxinput24_interval_y ( id_id , auxinput24_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_interval_y
  INTEGER id_id
  auxinput24_interval_y = model_config_rec%auxinput24_interval_y(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_interval_y
SUBROUTINE nl_get_auxinput24_interval_d ( id_id , auxinput24_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_interval_d
  INTEGER id_id
  auxinput24_interval_d = model_config_rec%auxinput24_interval_d(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_interval_d
SUBROUTINE nl_get_auxinput24_interval_h ( id_id , auxinput24_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_interval_h
  INTEGER id_id
  auxinput24_interval_h = model_config_rec%auxinput24_interval_h(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_interval_h
SUBROUTINE nl_get_auxinput24_interval_m ( id_id , auxinput24_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_interval_m
  INTEGER id_id
  auxinput24_interval_m = model_config_rec%auxinput24_interval_m(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_interval_m
SUBROUTINE nl_get_auxinput24_interval_s ( id_id , auxinput24_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_interval_s
  INTEGER id_id
  auxinput24_interval_s = model_config_rec%auxinput24_interval_s(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_interval_s
SUBROUTINE nl_get_auxinput24_interval ( id_id , auxinput24_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_interval
  INTEGER id_id
  auxinput24_interval = model_config_rec%auxinput24_interval(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_interval
SUBROUTINE nl_get_auxinput24_begin_y ( id_id , auxinput24_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_begin_y
  INTEGER id_id
  auxinput24_begin_y = model_config_rec%auxinput24_begin_y(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_begin_y
SUBROUTINE nl_get_auxinput24_begin_d ( id_id , auxinput24_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_begin_d
  INTEGER id_id
  auxinput24_begin_d = model_config_rec%auxinput24_begin_d(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_begin_d
SUBROUTINE nl_get_auxinput24_begin_h ( id_id , auxinput24_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_begin_h
  INTEGER id_id
  auxinput24_begin_h = model_config_rec%auxinput24_begin_h(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_begin_h
SUBROUTINE nl_get_auxinput24_begin_m ( id_id , auxinput24_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_begin_m
  INTEGER id_id
  auxinput24_begin_m = model_config_rec%auxinput24_begin_m(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_begin_m
SUBROUTINE nl_get_auxinput24_begin_s ( id_id , auxinput24_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_begin_s
  INTEGER id_id
  auxinput24_begin_s = model_config_rec%auxinput24_begin_s(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_begin_s
SUBROUTINE nl_get_auxinput24_begin ( id_id , auxinput24_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_begin
  INTEGER id_id
  auxinput24_begin = model_config_rec%auxinput24_begin(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_begin
SUBROUTINE nl_get_auxinput24_end_y ( id_id , auxinput24_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_end_y
  INTEGER id_id
  auxinput24_end_y = model_config_rec%auxinput24_end_y(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_end_y
SUBROUTINE nl_get_auxinput24_end_d ( id_id , auxinput24_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_end_d
  INTEGER id_id
  auxinput24_end_d = model_config_rec%auxinput24_end_d(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_end_d
SUBROUTINE nl_get_auxinput24_end_h ( id_id , auxinput24_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_end_h
  INTEGER id_id
  auxinput24_end_h = model_config_rec%auxinput24_end_h(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_end_h
SUBROUTINE nl_get_auxinput24_end_m ( id_id , auxinput24_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_end_m
  INTEGER id_id
  auxinput24_end_m = model_config_rec%auxinput24_end_m(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_end_m
SUBROUTINE nl_get_auxinput24_end_s ( id_id , auxinput24_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_end_s
  INTEGER id_id
  auxinput24_end_s = model_config_rec%auxinput24_end_s(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_end_s
SUBROUTINE nl_get_auxinput24_end ( id_id , auxinput24_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: auxinput24_end
  INTEGER id_id
  auxinput24_end = model_config_rec%auxinput24_end(id_id)
  RETURN
END SUBROUTINE nl_get_auxinput24_end
SUBROUTINE nl_get_io_form_auxinput24 ( id_id , io_form_auxinput24 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_auxinput24
  INTEGER id_id
  io_form_auxinput24 = model_config_rec%io_form_auxinput24
  RETURN
END SUBROUTINE nl_get_io_form_auxinput24
SUBROUTINE nl_get_frames_per_auxinput24 ( id_id , frames_per_auxinput24 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: frames_per_auxinput24
  INTEGER id_id
  frames_per_auxinput24 = model_config_rec%frames_per_auxinput24(id_id)
  RETURN
END SUBROUTINE nl_get_frames_per_auxinput24
SUBROUTINE nl_get_history_interval ( id_id , history_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_interval
  INTEGER id_id
  history_interval = model_config_rec%history_interval(id_id)
  RETURN
END SUBROUTINE nl_get_history_interval
SUBROUTINE nl_get_frames_per_outfile ( id_id , frames_per_outfile )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: frames_per_outfile
  INTEGER id_id
  frames_per_outfile = model_config_rec%frames_per_outfile(id_id)
  RETURN
END SUBROUTINE nl_get_frames_per_outfile
SUBROUTINE nl_get_restart ( id_id , restart )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: restart
  INTEGER id_id
  restart = model_config_rec%restart
  RETURN
END SUBROUTINE nl_get_restart
SUBROUTINE nl_get_restart_interval ( id_id , restart_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_interval
  INTEGER id_id
  restart_interval = model_config_rec%restart_interval
  RETURN
END SUBROUTINE nl_get_restart_interval
SUBROUTINE nl_get_io_form_input ( id_id , io_form_input )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_input
  INTEGER id_id
  io_form_input = model_config_rec%io_form_input
  RETURN
END SUBROUTINE nl_get_io_form_input
SUBROUTINE nl_get_io_form_history ( id_id , io_form_history )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_history
  INTEGER id_id
  io_form_history = model_config_rec%io_form_history
  RETURN
END SUBROUTINE nl_get_io_form_history
SUBROUTINE nl_get_io_form_restart ( id_id , io_form_restart )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_restart
  INTEGER id_id
  io_form_restart = model_config_rec%io_form_restart
  RETURN
END SUBROUTINE nl_get_io_form_restart
SUBROUTINE nl_get_io_form_boundary ( id_id , io_form_boundary )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_boundary
  INTEGER id_id
  io_form_boundary = model_config_rec%io_form_boundary
  RETURN
END SUBROUTINE nl_get_io_form_boundary
SUBROUTINE nl_get_debug_level ( id_id , debug_level )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: debug_level
  INTEGER id_id
  debug_level = model_config_rec%debug_level
  RETURN
END SUBROUTINE nl_get_debug_level
SUBROUTINE nl_get_self_test_domain ( id_id , self_test_domain )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: self_test_domain
  INTEGER id_id
  self_test_domain = model_config_rec%self_test_domain
  RETURN
END SUBROUTINE nl_get_self_test_domain
SUBROUTINE nl_get_history_outname ( id_id , history_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: history_outname
  INTEGER id_id
  history_outname = trim(model_config_rec%history_outname)
  RETURN
END SUBROUTINE nl_get_history_outname
SUBROUTINE nl_get_history_inname ( id_id , history_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: history_inname
  INTEGER id_id
  history_inname = trim(model_config_rec%history_inname)
  RETURN
END SUBROUTINE nl_get_history_inname
SUBROUTINE nl_get_use_netcdf_classic ( id_id , use_netcdf_classic )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: use_netcdf_classic
  INTEGER id_id
  use_netcdf_classic = model_config_rec%use_netcdf_classic
  RETURN
END SUBROUTINE nl_get_use_netcdf_classic
SUBROUTINE nl_get_history_interval_d ( id_id , history_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_interval_d
  INTEGER id_id
  history_interval_d = model_config_rec%history_interval_d(id_id)
  RETURN
END SUBROUTINE nl_get_history_interval_d
SUBROUTINE nl_get_history_interval_h ( id_id , history_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_interval_h
  INTEGER id_id
  history_interval_h = model_config_rec%history_interval_h(id_id)
  RETURN
END SUBROUTINE nl_get_history_interval_h
SUBROUTINE nl_get_history_interval_m ( id_id , history_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_interval_m
  INTEGER id_id
  history_interval_m = model_config_rec%history_interval_m(id_id)
  RETURN
END SUBROUTINE nl_get_history_interval_m
SUBROUTINE nl_get_history_interval_s ( id_id , history_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_interval_s
  INTEGER id_id
  history_interval_s = model_config_rec%history_interval_s(id_id)
  RETURN
END SUBROUTINE nl_get_history_interval_s
SUBROUTINE nl_get_inputout_interval_d ( id_id , inputout_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_interval_d
  INTEGER id_id
  inputout_interval_d = model_config_rec%inputout_interval_d(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_interval_d
SUBROUTINE nl_get_inputout_interval_h ( id_id , inputout_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_interval_h
  INTEGER id_id
  inputout_interval_h = model_config_rec%inputout_interval_h(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_interval_h
SUBROUTINE nl_get_inputout_interval_m ( id_id , inputout_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_interval_m
  INTEGER id_id
  inputout_interval_m = model_config_rec%inputout_interval_m(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_interval_m
SUBROUTINE nl_get_inputout_interval_s ( id_id , inputout_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_interval_s
  INTEGER id_id
  inputout_interval_s = model_config_rec%inputout_interval_s(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_interval_s
SUBROUTINE nl_get_inputout_interval ( id_id , inputout_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_interval
  INTEGER id_id
  inputout_interval = model_config_rec%inputout_interval(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_interval
SUBROUTINE nl_get_restart_interval_d ( id_id , restart_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_interval_d
  INTEGER id_id
  restart_interval_d = model_config_rec%restart_interval_d
  RETURN
END SUBROUTINE nl_get_restart_interval_d
SUBROUTINE nl_get_restart_interval_h ( id_id , restart_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_interval_h
  INTEGER id_id
  restart_interval_h = model_config_rec%restart_interval_h
  RETURN
END SUBROUTINE nl_get_restart_interval_h
SUBROUTINE nl_get_restart_interval_m ( id_id , restart_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_interval_m
  INTEGER id_id
  restart_interval_m = model_config_rec%restart_interval_m
  RETURN
END SUBROUTINE nl_get_restart_interval_m
SUBROUTINE nl_get_restart_interval_s ( id_id , restart_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_interval_s
  INTEGER id_id
  restart_interval_s = model_config_rec%restart_interval_s
  RETURN
END SUBROUTINE nl_get_restart_interval_s
SUBROUTINE nl_get_history_begin_y ( id_id , history_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_begin_y
  INTEGER id_id
  history_begin_y = model_config_rec%history_begin_y(id_id)
  RETURN
END SUBROUTINE nl_get_history_begin_y
SUBROUTINE nl_get_history_begin_d ( id_id , history_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_begin_d
  INTEGER id_id
  history_begin_d = model_config_rec%history_begin_d(id_id)
  RETURN
END SUBROUTINE nl_get_history_begin_d
SUBROUTINE nl_get_history_begin_h ( id_id , history_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_begin_h
  INTEGER id_id
  history_begin_h = model_config_rec%history_begin_h(id_id)
  RETURN
END SUBROUTINE nl_get_history_begin_h
SUBROUTINE nl_get_history_begin_m ( id_id , history_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_begin_m
  INTEGER id_id
  history_begin_m = model_config_rec%history_begin_m(id_id)
  RETURN
END SUBROUTINE nl_get_history_begin_m
SUBROUTINE nl_get_history_begin_s ( id_id , history_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_begin_s
  INTEGER id_id
  history_begin_s = model_config_rec%history_begin_s(id_id)
  RETURN
END SUBROUTINE nl_get_history_begin_s
SUBROUTINE nl_get_history_begin ( id_id , history_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_begin
  INTEGER id_id
  history_begin = model_config_rec%history_begin(id_id)
  RETURN
END SUBROUTINE nl_get_history_begin
SUBROUTINE nl_get_inputout_begin_y ( id_id , inputout_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_begin_y
  INTEGER id_id
  inputout_begin_y = model_config_rec%inputout_begin_y(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_begin_y
SUBROUTINE nl_get_inputout_begin_d ( id_id , inputout_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_begin_d
  INTEGER id_id
  inputout_begin_d = model_config_rec%inputout_begin_d(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_begin_d
SUBROUTINE nl_get_inputout_begin_h ( id_id , inputout_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_begin_h
  INTEGER id_id
  inputout_begin_h = model_config_rec%inputout_begin_h(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_begin_h
SUBROUTINE nl_get_inputout_begin_m ( id_id , inputout_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_begin_m
  INTEGER id_id
  inputout_begin_m = model_config_rec%inputout_begin_m(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_begin_m
SUBROUTINE nl_get_inputout_begin_s ( id_id , inputout_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_begin_s
  INTEGER id_id
  inputout_begin_s = model_config_rec%inputout_begin_s(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_begin_s
SUBROUTINE nl_get_restart_begin_y ( id_id , restart_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_begin_y
  INTEGER id_id
  restart_begin_y = model_config_rec%restart_begin_y
  RETURN
END SUBROUTINE nl_get_restart_begin_y
SUBROUTINE nl_get_restart_begin_d ( id_id , restart_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_begin_d
  INTEGER id_id
  restart_begin_d = model_config_rec%restart_begin_d
  RETURN
END SUBROUTINE nl_get_restart_begin_d
SUBROUTINE nl_get_restart_begin_h ( id_id , restart_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_begin_h
  INTEGER id_id
  restart_begin_h = model_config_rec%restart_begin_h
  RETURN
END SUBROUTINE nl_get_restart_begin_h
SUBROUTINE nl_get_restart_begin_m ( id_id , restart_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_begin_m
  INTEGER id_id
  restart_begin_m = model_config_rec%restart_begin_m
  RETURN
END SUBROUTINE nl_get_restart_begin_m
SUBROUTINE nl_get_restart_begin_s ( id_id , restart_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_begin_s
  INTEGER id_id
  restart_begin_s = model_config_rec%restart_begin_s
  RETURN
END SUBROUTINE nl_get_restart_begin_s
SUBROUTINE nl_get_restart_begin ( id_id , restart_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: restart_begin
  INTEGER id_id
  restart_begin = model_config_rec%restart_begin
  RETURN
END SUBROUTINE nl_get_restart_begin
SUBROUTINE nl_get_history_end_y ( id_id , history_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_end_y
  INTEGER id_id
  history_end_y = model_config_rec%history_end_y(id_id)
  RETURN
END SUBROUTINE nl_get_history_end_y
SUBROUTINE nl_get_history_end_d ( id_id , history_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_end_d
  INTEGER id_id
  history_end_d = model_config_rec%history_end_d(id_id)
  RETURN
END SUBROUTINE nl_get_history_end_d
SUBROUTINE nl_get_history_end_h ( id_id , history_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_end_h
  INTEGER id_id
  history_end_h = model_config_rec%history_end_h(id_id)
  RETURN
END SUBROUTINE nl_get_history_end_h
SUBROUTINE nl_get_history_end_m ( id_id , history_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_end_m
  INTEGER id_id
  history_end_m = model_config_rec%history_end_m(id_id)
  RETURN
END SUBROUTINE nl_get_history_end_m
SUBROUTINE nl_get_history_end_s ( id_id , history_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_end_s
  INTEGER id_id
  history_end_s = model_config_rec%history_end_s(id_id)
  RETURN
END SUBROUTINE nl_get_history_end_s
SUBROUTINE nl_get_history_end ( id_id , history_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: history_end
  INTEGER id_id
  history_end = model_config_rec%history_end(id_id)
  RETURN
END SUBROUTINE nl_get_history_end
SUBROUTINE nl_get_inputout_end_y ( id_id , inputout_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_end_y
  INTEGER id_id
  inputout_end_y = model_config_rec%inputout_end_y(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_end_y
SUBROUTINE nl_get_inputout_end_d ( id_id , inputout_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_end_d
  INTEGER id_id
  inputout_end_d = model_config_rec%inputout_end_d(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_end_d
SUBROUTINE nl_get_inputout_end_h ( id_id , inputout_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_end_h
  INTEGER id_id
  inputout_end_h = model_config_rec%inputout_end_h(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_end_h
SUBROUTINE nl_get_inputout_end_m ( id_id , inputout_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_end_m
  INTEGER id_id
  inputout_end_m = model_config_rec%inputout_end_m(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_end_m
SUBROUTINE nl_get_inputout_end_s ( id_id , inputout_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: inputout_end_s
  INTEGER id_id
  inputout_end_s = model_config_rec%inputout_end_s(id_id)
  RETURN
END SUBROUTINE nl_get_inputout_end_s
SUBROUTINE nl_get_simulation_start_year ( id_id , simulation_start_year )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: simulation_start_year
  INTEGER id_id
  simulation_start_year = model_config_rec%simulation_start_year
  RETURN
END SUBROUTINE nl_get_simulation_start_year
SUBROUTINE nl_get_simulation_start_month ( id_id , simulation_start_month )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: simulation_start_month
  INTEGER id_id
  simulation_start_month = model_config_rec%simulation_start_month
  RETURN
END SUBROUTINE nl_get_simulation_start_month
SUBROUTINE nl_get_simulation_start_day ( id_id , simulation_start_day )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: simulation_start_day
  INTEGER id_id
  simulation_start_day = model_config_rec%simulation_start_day
  RETURN
END SUBROUTINE nl_get_simulation_start_day
SUBROUTINE nl_get_simulation_start_hour ( id_id , simulation_start_hour )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: simulation_start_hour
  INTEGER id_id
  simulation_start_hour = model_config_rec%simulation_start_hour
  RETURN
END SUBROUTINE nl_get_simulation_start_hour
SUBROUTINE nl_get_simulation_start_minute ( id_id , simulation_start_minute )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: simulation_start_minute
  INTEGER id_id
  simulation_start_minute = model_config_rec%simulation_start_minute
  RETURN
END SUBROUTINE nl_get_simulation_start_minute
SUBROUTINE nl_get_simulation_start_second ( id_id , simulation_start_second )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: simulation_start_second
  INTEGER id_id
  simulation_start_second = model_config_rec%simulation_start_second
  RETURN
END SUBROUTINE nl_get_simulation_start_second
SUBROUTINE nl_get_reset_simulation_start ( id_id , reset_simulation_start )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: reset_simulation_start
  INTEGER id_id
  reset_simulation_start = model_config_rec%reset_simulation_start
  RETURN
END SUBROUTINE nl_get_reset_simulation_start
SUBROUTINE nl_get_sr_x ( id_id , sr_x )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sr_x
  INTEGER id_id
  sr_x = model_config_rec%sr_x(id_id)
  RETURN
END SUBROUTINE nl_get_sr_x
SUBROUTINE nl_get_sr_y ( id_id , sr_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sr_y
  INTEGER id_id
  sr_y = model_config_rec%sr_y(id_id)
  RETURN
END SUBROUTINE nl_get_sr_y
SUBROUTINE nl_get_sgfdda_inname ( id_id , sgfdda_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: sgfdda_inname
  INTEGER id_id
  sgfdda_inname = trim(model_config_rec%sgfdda_inname)
  RETURN
END SUBROUTINE nl_get_sgfdda_inname
SUBROUTINE nl_get_gfdda_inname ( id_id , gfdda_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: gfdda_inname
  INTEGER id_id
  gfdda_inname = trim(model_config_rec%gfdda_inname)
  RETURN
END SUBROUTINE nl_get_gfdda_inname
SUBROUTINE nl_get_sgfdda_interval_d ( id_id , sgfdda_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_interval_d
  INTEGER id_id
  sgfdda_interval_d = model_config_rec%sgfdda_interval_d(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_interval_d
SUBROUTINE nl_get_sgfdda_interval_h ( id_id , sgfdda_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_interval_h
  INTEGER id_id
  sgfdda_interval_h = model_config_rec%sgfdda_interval_h(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_interval_h
SUBROUTINE nl_get_sgfdda_interval_m ( id_id , sgfdda_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_interval_m
  INTEGER id_id
  sgfdda_interval_m = model_config_rec%sgfdda_interval_m(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_interval_m
SUBROUTINE nl_get_sgfdda_interval_s ( id_id , sgfdda_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_interval_s
  INTEGER id_id
  sgfdda_interval_s = model_config_rec%sgfdda_interval_s(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_interval_s
SUBROUTINE nl_get_sgfdda_interval_y ( id_id , sgfdda_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_interval_y
  INTEGER id_id
  sgfdda_interval_y = model_config_rec%sgfdda_interval_y(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_interval_y
SUBROUTINE nl_get_sgfdda_interval ( id_id , sgfdda_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_interval
  INTEGER id_id
  sgfdda_interval = model_config_rec%sgfdda_interval(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_interval
SUBROUTINE nl_get_gfdda_interval_d ( id_id , gfdda_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_interval_d
  INTEGER id_id
  gfdda_interval_d = model_config_rec%gfdda_interval_d(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_interval_d
SUBROUTINE nl_get_gfdda_interval_h ( id_id , gfdda_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_interval_h
  INTEGER id_id
  gfdda_interval_h = model_config_rec%gfdda_interval_h(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_interval_h
SUBROUTINE nl_get_gfdda_interval_m ( id_id , gfdda_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_interval_m
  INTEGER id_id
  gfdda_interval_m = model_config_rec%gfdda_interval_m(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_interval_m
SUBROUTINE nl_get_gfdda_interval_s ( id_id , gfdda_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_interval_s
  INTEGER id_id
  gfdda_interval_s = model_config_rec%gfdda_interval_s(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_interval_s
SUBROUTINE nl_get_gfdda_interval_y ( id_id , gfdda_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_interval_y
  INTEGER id_id
  gfdda_interval_y = model_config_rec%gfdda_interval_y(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_interval_y
SUBROUTINE nl_get_gfdda_interval ( id_id , gfdda_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_interval
  INTEGER id_id
  gfdda_interval = model_config_rec%gfdda_interval(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_interval
SUBROUTINE nl_get_sgfdda_begin_y ( id_id , sgfdda_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_begin_y
  INTEGER id_id
  sgfdda_begin_y = model_config_rec%sgfdda_begin_y(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_begin_y
SUBROUTINE nl_get_sgfdda_begin_d ( id_id , sgfdda_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_begin_d
  INTEGER id_id
  sgfdda_begin_d = model_config_rec%sgfdda_begin_d(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_begin_d
SUBROUTINE nl_get_sgfdda_begin_h ( id_id , sgfdda_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_begin_h
  INTEGER id_id
  sgfdda_begin_h = model_config_rec%sgfdda_begin_h(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_begin_h
SUBROUTINE nl_get_sgfdda_begin_m ( id_id , sgfdda_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_begin_m
  INTEGER id_id
  sgfdda_begin_m = model_config_rec%sgfdda_begin_m(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_begin_m
SUBROUTINE nl_get_sgfdda_begin_s ( id_id , sgfdda_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_begin_s
  INTEGER id_id
  sgfdda_begin_s = model_config_rec%sgfdda_begin_s(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_begin_s
SUBROUTINE nl_get_gfdda_begin_y ( id_id , gfdda_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_begin_y
  INTEGER id_id
  gfdda_begin_y = model_config_rec%gfdda_begin_y(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_begin_y
SUBROUTINE nl_get_gfdda_begin_d ( id_id , gfdda_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_begin_d
  INTEGER id_id
  gfdda_begin_d = model_config_rec%gfdda_begin_d(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_begin_d
SUBROUTINE nl_get_gfdda_begin_h ( id_id , gfdda_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_begin_h
  INTEGER id_id
  gfdda_begin_h = model_config_rec%gfdda_begin_h(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_begin_h
SUBROUTINE nl_get_gfdda_begin_m ( id_id , gfdda_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_begin_m
  INTEGER id_id
  gfdda_begin_m = model_config_rec%gfdda_begin_m(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_begin_m
SUBROUTINE nl_get_gfdda_begin_s ( id_id , gfdda_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_begin_s
  INTEGER id_id
  gfdda_begin_s = model_config_rec%gfdda_begin_s(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_begin_s
SUBROUTINE nl_get_sgfdda_end_y ( id_id , sgfdda_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_end_y
  INTEGER id_id
  sgfdda_end_y = model_config_rec%sgfdda_end_y(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_end_y
SUBROUTINE nl_get_sgfdda_end_d ( id_id , sgfdda_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_end_d
  INTEGER id_id
  sgfdda_end_d = model_config_rec%sgfdda_end_d(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_end_d
SUBROUTINE nl_get_sgfdda_end_h ( id_id , sgfdda_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_end_h
  INTEGER id_id
  sgfdda_end_h = model_config_rec%sgfdda_end_h(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_end_h
SUBROUTINE nl_get_sgfdda_end_m ( id_id , sgfdda_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_end_m
  INTEGER id_id
  sgfdda_end_m = model_config_rec%sgfdda_end_m(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_end_m
SUBROUTINE nl_get_sgfdda_end_s ( id_id , sgfdda_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sgfdda_end_s
  INTEGER id_id
  sgfdda_end_s = model_config_rec%sgfdda_end_s(id_id)
  RETURN
END SUBROUTINE nl_get_sgfdda_end_s
SUBROUTINE nl_get_gfdda_end_y ( id_id , gfdda_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_end_y
  INTEGER id_id
  gfdda_end_y = model_config_rec%gfdda_end_y(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_end_y
SUBROUTINE nl_get_gfdda_end_d ( id_id , gfdda_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_end_d
  INTEGER id_id
  gfdda_end_d = model_config_rec%gfdda_end_d(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_end_d
SUBROUTINE nl_get_gfdda_end_h ( id_id , gfdda_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_end_h
  INTEGER id_id
  gfdda_end_h = model_config_rec%gfdda_end_h(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_end_h
SUBROUTINE nl_get_gfdda_end_m ( id_id , gfdda_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_end_m
  INTEGER id_id
  gfdda_end_m = model_config_rec%gfdda_end_m(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_end_m
SUBROUTINE nl_get_gfdda_end_s ( id_id , gfdda_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: gfdda_end_s
  INTEGER id_id
  gfdda_end_s = model_config_rec%gfdda_end_s(id_id)
  RETURN
END SUBROUTINE nl_get_gfdda_end_s
SUBROUTINE nl_get_io_form_sgfdda ( id_id , io_form_sgfdda )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_sgfdda
  INTEGER id_id
  io_form_sgfdda = model_config_rec%io_form_sgfdda
  RETURN
END SUBROUTINE nl_get_io_form_sgfdda
SUBROUTINE nl_get_io_form_gfdda ( id_id , io_form_gfdda )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: io_form_gfdda
  INTEGER id_id
  io_form_gfdda = model_config_rec%io_form_gfdda
  RETURN
END SUBROUTINE nl_get_io_form_gfdda
SUBROUTINE nl_get_iofields_filename ( id_id , iofields_filename )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: iofields_filename
  INTEGER id_id
  iofields_filename = model_config_rec%iofields_filename(id_id)
  RETURN
END SUBROUTINE nl_get_iofields_filename
SUBROUTINE nl_get_ignore_iofields_warning ( id_id , ignore_iofields_warning )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: ignore_iofields_warning
  INTEGER id_id
  ignore_iofields_warning = model_config_rec%ignore_iofields_warning
  RETURN
END SUBROUTINE nl_get_ignore_iofields_warning
SUBROUTINE nl_get_ncd_nofill ( id_id , ncd_nofill )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: ncd_nofill
  INTEGER id_id
  ncd_nofill = model_config_rec%ncd_nofill
  RETURN
END SUBROUTINE nl_get_ncd_nofill
SUBROUTINE nl_get_adios2_compression_enable ( id_id , adios2_compression_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: adios2_compression_enable
  INTEGER id_id
  adios2_compression_enable = model_config_rec%adios2_compression_enable
  RETURN
END SUBROUTINE nl_get_adios2_compression_enable
SUBROUTINE nl_get_adios2_blosc_compressor ( id_id , adios2_blosc_compressor )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(OUT) :: adios2_blosc_compressor
  INTEGER id_id
  adios2_blosc_compressor = trim(model_config_rec%adios2_blosc_compressor)
  RETURN
END SUBROUTINE nl_get_adios2_blosc_compressor
SUBROUTINE nl_get_adios2_numaggregators ( id_id , adios2_numaggregators )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: adios2_numaggregators
  INTEGER id_id
  adios2_numaggregators = model_config_rec%adios2_numaggregators
  RETURN
END SUBROUTINE nl_get_adios2_numaggregators
SUBROUTINE nl_get_nfmc ( id_id , nfmc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: nfmc
  INTEGER id_id
  nfmc = model_config_rec%nfmc
  RETURN
END SUBROUTINE nl_get_nfmc
SUBROUTINE nl_get_fmoist_run ( id_id , fmoist_run )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fmoist_run
  INTEGER id_id
  fmoist_run = model_config_rec%fmoist_run(id_id)
  RETURN
END SUBROUTINE nl_get_fmoist_run
SUBROUTINE nl_get_fmoist_interp ( id_id , fmoist_interp )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fmoist_interp
  INTEGER id_id
  fmoist_interp = model_config_rec%fmoist_interp(id_id)
  RETURN
END SUBROUTINE nl_get_fmoist_interp
SUBROUTINE nl_get_fmoisti_run ( id_id , fmoisti_run )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fmoisti_run
  INTEGER id_id
  fmoisti_run = model_config_rec%fmoisti_run(id_id)
  RETURN
END SUBROUTINE nl_get_fmoisti_run
SUBROUTINE nl_get_fmoisti_interp ( id_id , fmoisti_interp )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fmoisti_interp
  INTEGER id_id
  fmoisti_interp = model_config_rec%fmoisti_interp(id_id)
  RETURN
END SUBROUTINE nl_get_fmoisti_interp
SUBROUTINE nl_get_fmoist_only ( id_id , fmoist_only )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fmoist_only
  INTEGER id_id
  fmoist_only = model_config_rec%fmoist_only(id_id)
  RETURN
END SUBROUTINE nl_get_fmoist_only
SUBROUTINE nl_get_fmoist_freq ( id_id , fmoist_freq )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fmoist_freq
  INTEGER id_id
  fmoist_freq = model_config_rec%fmoist_freq(id_id)
  RETURN
END SUBROUTINE nl_get_fmoist_freq
SUBROUTINE nl_get_fmoist_dt ( id_id , fmoist_dt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fmoist_dt
  INTEGER id_id
  fmoist_dt = model_config_rec%fmoist_dt(id_id)
  RETURN
END SUBROUTINE nl_get_fmoist_dt
SUBROUTINE nl_get_fmep_decay_tlag ( id_id , fmep_decay_tlag )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fmep_decay_tlag
  INTEGER id_id
  fmep_decay_tlag = model_config_rec%fmep_decay_tlag
  RETURN
END SUBROUTINE nl_get_fmep_decay_tlag
SUBROUTINE nl_get_ifire ( id_id , ifire )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: ifire
  INTEGER id_id
  ifire = model_config_rec%ifire(id_id)
  RETURN
END SUBROUTINE nl_get_ifire
SUBROUTINE nl_get_fire_boundary_guard ( id_id , fire_boundary_guard )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_boundary_guard
  INTEGER id_id
  fire_boundary_guard = model_config_rec%fire_boundary_guard(id_id)
  RETURN
END SUBROUTINE nl_get_fire_boundary_guard
SUBROUTINE nl_get_fire_num_ignitions ( id_id , fire_num_ignitions )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_num_ignitions
  INTEGER id_id
  fire_num_ignitions = model_config_rec%fire_num_ignitions(id_id)
  RETURN
END SUBROUTINE nl_get_fire_num_ignitions
SUBROUTINE nl_get_fire_ignition_ros1 ( id_id , fire_ignition_ros1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_ros1
  INTEGER id_id
  fire_ignition_ros1 = model_config_rec%fire_ignition_ros1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_ros1
SUBROUTINE nl_get_fire_ignition_start_lon1 ( id_id , fire_ignition_start_lon1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lon1
  INTEGER id_id
  fire_ignition_start_lon1 = model_config_rec%fire_ignition_start_lon1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lon1
SUBROUTINE nl_get_fire_ignition_start_lat1 ( id_id , fire_ignition_start_lat1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lat1
  INTEGER id_id
  fire_ignition_start_lat1 = model_config_rec%fire_ignition_start_lat1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lat1
SUBROUTINE nl_get_fire_ignition_end_lon1 ( id_id , fire_ignition_end_lon1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lon1
  INTEGER id_id
  fire_ignition_end_lon1 = model_config_rec%fire_ignition_end_lon1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lon1
SUBROUTINE nl_get_fire_ignition_end_lat1 ( id_id , fire_ignition_end_lat1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lat1
  INTEGER id_id
  fire_ignition_end_lat1 = model_config_rec%fire_ignition_end_lat1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lat1
SUBROUTINE nl_get_fire_ignition_radius1 ( id_id , fire_ignition_radius1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_radius1
  INTEGER id_id
  fire_ignition_radius1 = model_config_rec%fire_ignition_radius1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_radius1
SUBROUTINE nl_get_fire_ignition_start_time1 ( id_id , fire_ignition_start_time1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_time1
  INTEGER id_id
  fire_ignition_start_time1 = model_config_rec%fire_ignition_start_time1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_time1
SUBROUTINE nl_get_fire_ignition_end_time1 ( id_id , fire_ignition_end_time1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_time1
  INTEGER id_id
  fire_ignition_end_time1 = model_config_rec%fire_ignition_end_time1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_time1
SUBROUTINE nl_get_fire_ignition_ros2 ( id_id , fire_ignition_ros2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_ros2
  INTEGER id_id
  fire_ignition_ros2 = model_config_rec%fire_ignition_ros2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_ros2
SUBROUTINE nl_get_fire_ignition_start_lon2 ( id_id , fire_ignition_start_lon2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lon2
  INTEGER id_id
  fire_ignition_start_lon2 = model_config_rec%fire_ignition_start_lon2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lon2
SUBROUTINE nl_get_fire_ignition_start_lat2 ( id_id , fire_ignition_start_lat2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lat2
  INTEGER id_id
  fire_ignition_start_lat2 = model_config_rec%fire_ignition_start_lat2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lat2
SUBROUTINE nl_get_fire_ignition_end_lon2 ( id_id , fire_ignition_end_lon2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lon2
  INTEGER id_id
  fire_ignition_end_lon2 = model_config_rec%fire_ignition_end_lon2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lon2
SUBROUTINE nl_get_fire_ignition_end_lat2 ( id_id , fire_ignition_end_lat2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lat2
  INTEGER id_id
  fire_ignition_end_lat2 = model_config_rec%fire_ignition_end_lat2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lat2
SUBROUTINE nl_get_fire_ignition_radius2 ( id_id , fire_ignition_radius2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_radius2
  INTEGER id_id
  fire_ignition_radius2 = model_config_rec%fire_ignition_radius2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_radius2
SUBROUTINE nl_get_fire_ignition_start_time2 ( id_id , fire_ignition_start_time2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_time2
  INTEGER id_id
  fire_ignition_start_time2 = model_config_rec%fire_ignition_start_time2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_time2
SUBROUTINE nl_get_fire_ignition_end_time2 ( id_id , fire_ignition_end_time2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_time2
  INTEGER id_id
  fire_ignition_end_time2 = model_config_rec%fire_ignition_end_time2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_time2
SUBROUTINE nl_get_fire_ignition_ros3 ( id_id , fire_ignition_ros3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_ros3
  INTEGER id_id
  fire_ignition_ros3 = model_config_rec%fire_ignition_ros3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_ros3
SUBROUTINE nl_get_fire_ignition_start_lon3 ( id_id , fire_ignition_start_lon3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lon3
  INTEGER id_id
  fire_ignition_start_lon3 = model_config_rec%fire_ignition_start_lon3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lon3
SUBROUTINE nl_get_fire_ignition_start_lat3 ( id_id , fire_ignition_start_lat3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lat3
  INTEGER id_id
  fire_ignition_start_lat3 = model_config_rec%fire_ignition_start_lat3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lat3
SUBROUTINE nl_get_fire_ignition_end_lon3 ( id_id , fire_ignition_end_lon3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lon3
  INTEGER id_id
  fire_ignition_end_lon3 = model_config_rec%fire_ignition_end_lon3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lon3
SUBROUTINE nl_get_fire_ignition_end_lat3 ( id_id , fire_ignition_end_lat3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lat3
  INTEGER id_id
  fire_ignition_end_lat3 = model_config_rec%fire_ignition_end_lat3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lat3
SUBROUTINE nl_get_fire_ignition_radius3 ( id_id , fire_ignition_radius3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_radius3
  INTEGER id_id
  fire_ignition_radius3 = model_config_rec%fire_ignition_radius3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_radius3
SUBROUTINE nl_get_fire_ignition_start_time3 ( id_id , fire_ignition_start_time3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_time3
  INTEGER id_id
  fire_ignition_start_time3 = model_config_rec%fire_ignition_start_time3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_time3
SUBROUTINE nl_get_fire_ignition_end_time3 ( id_id , fire_ignition_end_time3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_time3
  INTEGER id_id
  fire_ignition_end_time3 = model_config_rec%fire_ignition_end_time3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_time3
SUBROUTINE nl_get_fire_ignition_ros4 ( id_id , fire_ignition_ros4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_ros4
  INTEGER id_id
  fire_ignition_ros4 = model_config_rec%fire_ignition_ros4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_ros4
SUBROUTINE nl_get_fire_ignition_start_lon4 ( id_id , fire_ignition_start_lon4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lon4
  INTEGER id_id
  fire_ignition_start_lon4 = model_config_rec%fire_ignition_start_lon4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lon4
SUBROUTINE nl_get_fire_ignition_start_lat4 ( id_id , fire_ignition_start_lat4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lat4
  INTEGER id_id
  fire_ignition_start_lat4 = model_config_rec%fire_ignition_start_lat4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lat4
SUBROUTINE nl_get_fire_ignition_end_lon4 ( id_id , fire_ignition_end_lon4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lon4
  INTEGER id_id
  fire_ignition_end_lon4 = model_config_rec%fire_ignition_end_lon4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lon4
SUBROUTINE nl_get_fire_ignition_end_lat4 ( id_id , fire_ignition_end_lat4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lat4
  INTEGER id_id
  fire_ignition_end_lat4 = model_config_rec%fire_ignition_end_lat4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lat4
SUBROUTINE nl_get_fire_ignition_radius4 ( id_id , fire_ignition_radius4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_radius4
  INTEGER id_id
  fire_ignition_radius4 = model_config_rec%fire_ignition_radius4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_radius4
SUBROUTINE nl_get_fire_ignition_start_time4 ( id_id , fire_ignition_start_time4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_time4
  INTEGER id_id
  fire_ignition_start_time4 = model_config_rec%fire_ignition_start_time4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_time4
SUBROUTINE nl_get_fire_ignition_end_time4 ( id_id , fire_ignition_end_time4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_time4
  INTEGER id_id
  fire_ignition_end_time4 = model_config_rec%fire_ignition_end_time4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_time4
SUBROUTINE nl_get_fire_ignition_ros5 ( id_id , fire_ignition_ros5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_ros5
  INTEGER id_id
  fire_ignition_ros5 = model_config_rec%fire_ignition_ros5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_ros5
SUBROUTINE nl_get_fire_ignition_start_lon5 ( id_id , fire_ignition_start_lon5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lon5
  INTEGER id_id
  fire_ignition_start_lon5 = model_config_rec%fire_ignition_start_lon5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lon5
SUBROUTINE nl_get_fire_ignition_start_lat5 ( id_id , fire_ignition_start_lat5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_lat5
  INTEGER id_id
  fire_ignition_start_lat5 = model_config_rec%fire_ignition_start_lat5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_lat5
SUBROUTINE nl_get_fire_ignition_end_lon5 ( id_id , fire_ignition_end_lon5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lon5
  INTEGER id_id
  fire_ignition_end_lon5 = model_config_rec%fire_ignition_end_lon5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lon5
SUBROUTINE nl_get_fire_ignition_end_lat5 ( id_id , fire_ignition_end_lat5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_lat5
  INTEGER id_id
  fire_ignition_end_lat5 = model_config_rec%fire_ignition_end_lat5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_lat5
SUBROUTINE nl_get_fire_ignition_radius5 ( id_id , fire_ignition_radius5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_radius5
  INTEGER id_id
  fire_ignition_radius5 = model_config_rec%fire_ignition_radius5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_radius5
SUBROUTINE nl_get_fire_ignition_start_time5 ( id_id , fire_ignition_start_time5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_time5
  INTEGER id_id
  fire_ignition_start_time5 = model_config_rec%fire_ignition_start_time5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_time5
SUBROUTINE nl_get_fire_ignition_end_time5 ( id_id , fire_ignition_end_time5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_time5
  INTEGER id_id
  fire_ignition_end_time5 = model_config_rec%fire_ignition_end_time5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_time5
SUBROUTINE nl_get_fire_ignition_start_x1 ( id_id , fire_ignition_start_x1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_x1
  INTEGER id_id
  fire_ignition_start_x1 = model_config_rec%fire_ignition_start_x1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_x1
SUBROUTINE nl_get_fire_ignition_start_y1 ( id_id , fire_ignition_start_y1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_y1
  INTEGER id_id
  fire_ignition_start_y1 = model_config_rec%fire_ignition_start_y1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_y1
SUBROUTINE nl_get_fire_ignition_end_x1 ( id_id , fire_ignition_end_x1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_x1
  INTEGER id_id
  fire_ignition_end_x1 = model_config_rec%fire_ignition_end_x1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_x1
SUBROUTINE nl_get_fire_ignition_end_y1 ( id_id , fire_ignition_end_y1 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_y1
  INTEGER id_id
  fire_ignition_end_y1 = model_config_rec%fire_ignition_end_y1(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_y1
SUBROUTINE nl_get_fire_ignition_start_x2 ( id_id , fire_ignition_start_x2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_x2
  INTEGER id_id
  fire_ignition_start_x2 = model_config_rec%fire_ignition_start_x2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_x2
SUBROUTINE nl_get_fire_ignition_start_y2 ( id_id , fire_ignition_start_y2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_y2
  INTEGER id_id
  fire_ignition_start_y2 = model_config_rec%fire_ignition_start_y2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_y2
SUBROUTINE nl_get_fire_ignition_end_x2 ( id_id , fire_ignition_end_x2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_x2
  INTEGER id_id
  fire_ignition_end_x2 = model_config_rec%fire_ignition_end_x2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_x2
SUBROUTINE nl_get_fire_ignition_end_y2 ( id_id , fire_ignition_end_y2 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_y2
  INTEGER id_id
  fire_ignition_end_y2 = model_config_rec%fire_ignition_end_y2(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_y2
SUBROUTINE nl_get_fire_ignition_start_x3 ( id_id , fire_ignition_start_x3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_x3
  INTEGER id_id
  fire_ignition_start_x3 = model_config_rec%fire_ignition_start_x3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_x3
SUBROUTINE nl_get_fire_ignition_start_y3 ( id_id , fire_ignition_start_y3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_y3
  INTEGER id_id
  fire_ignition_start_y3 = model_config_rec%fire_ignition_start_y3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_y3
SUBROUTINE nl_get_fire_ignition_end_x3 ( id_id , fire_ignition_end_x3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_x3
  INTEGER id_id
  fire_ignition_end_x3 = model_config_rec%fire_ignition_end_x3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_x3
SUBROUTINE nl_get_fire_ignition_end_y3 ( id_id , fire_ignition_end_y3 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_y3
  INTEGER id_id
  fire_ignition_end_y3 = model_config_rec%fire_ignition_end_y3(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_y3
SUBROUTINE nl_get_fire_ignition_start_x4 ( id_id , fire_ignition_start_x4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_x4
  INTEGER id_id
  fire_ignition_start_x4 = model_config_rec%fire_ignition_start_x4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_x4
SUBROUTINE nl_get_fire_ignition_start_y4 ( id_id , fire_ignition_start_y4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_y4
  INTEGER id_id
  fire_ignition_start_y4 = model_config_rec%fire_ignition_start_y4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_y4
SUBROUTINE nl_get_fire_ignition_end_x4 ( id_id , fire_ignition_end_x4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_x4
  INTEGER id_id
  fire_ignition_end_x4 = model_config_rec%fire_ignition_end_x4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_x4
SUBROUTINE nl_get_fire_ignition_end_y4 ( id_id , fire_ignition_end_y4 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_y4
  INTEGER id_id
  fire_ignition_end_y4 = model_config_rec%fire_ignition_end_y4(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_y4
SUBROUTINE nl_get_fire_ignition_start_x5 ( id_id , fire_ignition_start_x5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_x5
  INTEGER id_id
  fire_ignition_start_x5 = model_config_rec%fire_ignition_start_x5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_x5
SUBROUTINE nl_get_fire_ignition_start_y5 ( id_id , fire_ignition_start_y5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_start_y5
  INTEGER id_id
  fire_ignition_start_y5 = model_config_rec%fire_ignition_start_y5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_start_y5
SUBROUTINE nl_get_fire_ignition_end_x5 ( id_id , fire_ignition_end_x5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_x5
  INTEGER id_id
  fire_ignition_end_x5 = model_config_rec%fire_ignition_end_x5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_x5
SUBROUTINE nl_get_fire_ignition_end_y5 ( id_id , fire_ignition_end_y5 )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ignition_end_y5
  INTEGER id_id
  fire_ignition_end_y5 = model_config_rec%fire_ignition_end_y5(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ignition_end_y5
SUBROUTINE nl_get_fire_lat_init ( id_id , fire_lat_init )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_lat_init
  INTEGER id_id
  fire_lat_init = model_config_rec%fire_lat_init(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lat_init
SUBROUTINE nl_get_fire_lon_init ( id_id , fire_lon_init )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_lon_init
  INTEGER id_id
  fire_lon_init = model_config_rec%fire_lon_init(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lon_init
SUBROUTINE nl_get_fire_ign_time ( id_id , fire_ign_time )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ign_time
  INTEGER id_id
  fire_ign_time = model_config_rec%fire_ign_time(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ign_time
SUBROUTINE nl_get_fire_shape ( id_id , fire_shape )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_shape
  INTEGER id_id
  fire_shape = model_config_rec%fire_shape(id_id)
  RETURN
END SUBROUTINE nl_get_fire_shape
SUBROUTINE nl_get_fire_sprd_mdl ( id_id , fire_sprd_mdl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_sprd_mdl
  INTEGER id_id
  fire_sprd_mdl = model_config_rec%fire_sprd_mdl(id_id)
  RETURN
END SUBROUTINE nl_get_fire_sprd_mdl
SUBROUTINE nl_get_fire_crwn_hgt ( id_id , fire_crwn_hgt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_crwn_hgt
  INTEGER id_id
  fire_crwn_hgt = model_config_rec%fire_crwn_hgt(id_id)
  RETURN
END SUBROUTINE nl_get_fire_crwn_hgt
SUBROUTINE nl_get_fire_ext_grnd ( id_id , fire_ext_grnd )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ext_grnd
  INTEGER id_id
  fire_ext_grnd = model_config_rec%fire_ext_grnd(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ext_grnd
SUBROUTINE nl_get_fire_ext_crwn ( id_id , fire_ext_crwn )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_ext_crwn
  INTEGER id_id
  fire_ext_crwn = model_config_rec%fire_ext_crwn(id_id)
  RETURN
END SUBROUTINE nl_get_fire_ext_crwn
SUBROUTINE nl_get_fire_sfc_flx ( id_id , fire_sfc_flx )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_sfc_flx
  INTEGER id_id
  fire_sfc_flx = model_config_rec%fire_sfc_flx(id_id)
  RETURN
END SUBROUTINE nl_get_fire_sfc_flx
SUBROUTINE nl_get_fire_heat_peak ( id_id , fire_heat_peak )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_heat_peak
  INTEGER id_id
  fire_heat_peak = model_config_rec%fire_heat_peak(id_id)
  RETURN
END SUBROUTINE nl_get_fire_heat_peak
SUBROUTINE nl_get_fire_tg_ub ( id_id , fire_tg_ub )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_tg_ub
  INTEGER id_id
  fire_tg_ub = model_config_rec%fire_tg_ub(id_id)
  RETURN
END SUBROUTINE nl_get_fire_tg_ub
SUBROUTINE nl_get_fire_smk_scheme ( id_id , fire_smk_scheme )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_smk_scheme
  INTEGER id_id
  fire_smk_scheme = model_config_rec%fire_smk_scheme(id_id)
  RETURN
END SUBROUTINE nl_get_fire_smk_scheme
SUBROUTINE nl_get_fire_smk_peak ( id_id , fire_smk_peak )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_smk_peak
  INTEGER id_id
  fire_smk_peak = model_config_rec%fire_smk_peak(id_id)
  RETURN
END SUBROUTINE nl_get_fire_smk_peak
SUBROUTINE nl_get_fire_smk_ext ( id_id , fire_smk_ext )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_smk_ext
  INTEGER id_id
  fire_smk_ext = model_config_rec%fire_smk_ext(id_id)
  RETURN
END SUBROUTINE nl_get_fire_smk_ext
SUBROUTINE nl_get_fire_wind_height ( id_id , fire_wind_height )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_wind_height
  INTEGER id_id
  fire_wind_height = model_config_rec%fire_wind_height(id_id)
  RETURN
END SUBROUTINE nl_get_fire_wind_height
SUBROUTINE nl_get_fire_fuel_read ( id_id , fire_fuel_read )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_fuel_read
  INTEGER id_id
  fire_fuel_read = model_config_rec%fire_fuel_read(id_id)
  RETURN
END SUBROUTINE nl_get_fire_fuel_read
SUBROUTINE nl_get_fire_fuel_cat ( id_id , fire_fuel_cat )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_fuel_cat
  INTEGER id_id
  fire_fuel_cat = model_config_rec%fire_fuel_cat(id_id)
  RETURN
END SUBROUTINE nl_get_fire_fuel_cat
SUBROUTINE nl_get_fire_fmc_read ( id_id , fire_fmc_read )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_fmc_read
  INTEGER id_id
  fire_fmc_read = model_config_rec%fire_fmc_read(id_id)
  RETURN
END SUBROUTINE nl_get_fire_fmc_read
SUBROUTINE nl_get_fire_print_msg ( id_id , fire_print_msg )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_print_msg
  INTEGER id_id
  fire_print_msg = model_config_rec%fire_print_msg(id_id)
  RETURN
END SUBROUTINE nl_get_fire_print_msg
SUBROUTINE nl_get_fire_print_file ( id_id , fire_print_file )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_print_file
  INTEGER id_id
  fire_print_file = model_config_rec%fire_print_file(id_id)
  RETURN
END SUBROUTINE nl_get_fire_print_file
SUBROUTINE nl_get_fire_fuel_left_method ( id_id , fire_fuel_left_method )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_fuel_left_method
  INTEGER id_id
  fire_fuel_left_method = model_config_rec%fire_fuel_left_method(id_id)
  RETURN
END SUBROUTINE nl_get_fire_fuel_left_method
SUBROUTINE nl_get_fire_fuel_left_irl ( id_id , fire_fuel_left_irl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_fuel_left_irl
  INTEGER id_id
  fire_fuel_left_irl = model_config_rec%fire_fuel_left_irl(id_id)
  RETURN
END SUBROUTINE nl_get_fire_fuel_left_irl
SUBROUTINE nl_get_fire_fuel_left_jrl ( id_id , fire_fuel_left_jrl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_fuel_left_jrl
  INTEGER id_id
  fire_fuel_left_jrl = model_config_rec%fire_fuel_left_jrl(id_id)
  RETURN
END SUBROUTINE nl_get_fire_fuel_left_jrl
SUBROUTINE nl_get_fire_grows_only ( id_id , fire_grows_only )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_grows_only
  INTEGER id_id
  fire_grows_only = model_config_rec%fire_grows_only(id_id)
  RETURN
END SUBROUTINE nl_get_fire_grows_only
SUBROUTINE nl_get_fire_upwinding ( id_id , fire_upwinding )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_upwinding
  INTEGER id_id
  fire_upwinding = model_config_rec%fire_upwinding(id_id)
  RETURN
END SUBROUTINE nl_get_fire_upwinding
SUBROUTINE nl_get_fire_upwind_split ( id_id , fire_upwind_split )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_upwind_split
  INTEGER id_id
  fire_upwind_split = model_config_rec%fire_upwind_split(id_id)
  RETURN
END SUBROUTINE nl_get_fire_upwind_split
SUBROUTINE nl_get_fire_viscosity ( id_id , fire_viscosity )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_viscosity
  INTEGER id_id
  fire_viscosity = model_config_rec%fire_viscosity(id_id)
  RETURN
END SUBROUTINE nl_get_fire_viscosity
SUBROUTINE nl_get_fire_lfn_ext_up ( id_id , fire_lfn_ext_up )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_lfn_ext_up
  INTEGER id_id
  fire_lfn_ext_up = model_config_rec%fire_lfn_ext_up(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lfn_ext_up
SUBROUTINE nl_get_fire_topo_from_atm ( id_id , fire_topo_from_atm )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_topo_from_atm
  INTEGER id_id
  fire_topo_from_atm = model_config_rec%fire_topo_from_atm(id_id)
  RETURN
END SUBROUTINE nl_get_fire_topo_from_atm
SUBROUTINE nl_get_fire_advection ( id_id , fire_advection )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_advection
  INTEGER id_id
  fire_advection = model_config_rec%fire_advection(id_id)
  RETURN
END SUBROUTINE nl_get_fire_advection
SUBROUTINE nl_get_fire_test_steps ( id_id , fire_test_steps )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_test_steps
  INTEGER id_id
  fire_test_steps = model_config_rec%fire_test_steps(id_id)
  RETURN
END SUBROUTINE nl_get_fire_test_steps
SUBROUTINE nl_get_fire_const_time ( id_id , fire_const_time )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_const_time
  INTEGER id_id
  fire_const_time = model_config_rec%fire_const_time(id_id)
  RETURN
END SUBROUTINE nl_get_fire_const_time
SUBROUTINE nl_get_fire_const_grnhfx ( id_id , fire_const_grnhfx )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_const_grnhfx
  INTEGER id_id
  fire_const_grnhfx = model_config_rec%fire_const_grnhfx(id_id)
  RETURN
END SUBROUTINE nl_get_fire_const_grnhfx
SUBROUTINE nl_get_fire_const_grnqfx ( id_id , fire_const_grnqfx )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_const_grnqfx
  INTEGER id_id
  fire_const_grnqfx = model_config_rec%fire_const_grnqfx(id_id)
  RETURN
END SUBROUTINE nl_get_fire_const_grnqfx
SUBROUTINE nl_get_fire_atm_feedback ( id_id , fire_atm_feedback )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_atm_feedback
  INTEGER id_id
  fire_atm_feedback = model_config_rec%fire_atm_feedback(id_id)
  RETURN
END SUBROUTINE nl_get_fire_atm_feedback
SUBROUTINE nl_get_fire_mountain_type ( id_id , fire_mountain_type )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_mountain_type
  INTEGER id_id
  fire_mountain_type = model_config_rec%fire_mountain_type(id_id)
  RETURN
END SUBROUTINE nl_get_fire_mountain_type
SUBROUTINE nl_get_fire_mountain_height ( id_id , fire_mountain_height )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_mountain_height
  INTEGER id_id
  fire_mountain_height = model_config_rec%fire_mountain_height(id_id)
  RETURN
END SUBROUTINE nl_get_fire_mountain_height
SUBROUTINE nl_get_fire_mountain_start_x ( id_id , fire_mountain_start_x )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_mountain_start_x
  INTEGER id_id
  fire_mountain_start_x = model_config_rec%fire_mountain_start_x(id_id)
  RETURN
END SUBROUTINE nl_get_fire_mountain_start_x
SUBROUTINE nl_get_fire_mountain_start_y ( id_id , fire_mountain_start_y )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_mountain_start_y
  INTEGER id_id
  fire_mountain_start_y = model_config_rec%fire_mountain_start_y(id_id)
  RETURN
END SUBROUTINE nl_get_fire_mountain_start_y
SUBROUTINE nl_get_fire_mountain_end_x ( id_id , fire_mountain_end_x )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_mountain_end_x
  INTEGER id_id
  fire_mountain_end_x = model_config_rec%fire_mountain_end_x(id_id)
  RETURN
END SUBROUTINE nl_get_fire_mountain_end_x
SUBROUTINE nl_get_fire_mountain_end_y ( id_id , fire_mountain_end_y )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_mountain_end_y
  INTEGER id_id
  fire_mountain_end_y = model_config_rec%fire_mountain_end_y(id_id)
  RETURN
END SUBROUTINE nl_get_fire_mountain_end_y
SUBROUTINE nl_get_delt_perturbation ( id_id , delt_perturbation )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: delt_perturbation
  INTEGER id_id
  delt_perturbation = model_config_rec%delt_perturbation(id_id)
  RETURN
END SUBROUTINE nl_get_delt_perturbation
SUBROUTINE nl_get_xrad_perturbation ( id_id , xrad_perturbation )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: xrad_perturbation
  INTEGER id_id
  xrad_perturbation = model_config_rec%xrad_perturbation(id_id)
  RETURN
END SUBROUTINE nl_get_xrad_perturbation
SUBROUTINE nl_get_yrad_perturbation ( id_id , yrad_perturbation )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: yrad_perturbation
  INTEGER id_id
  yrad_perturbation = model_config_rec%yrad_perturbation(id_id)
  RETURN
END SUBROUTINE nl_get_yrad_perturbation
SUBROUTINE nl_get_zrad_perturbation ( id_id , zrad_perturbation )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: zrad_perturbation
  INTEGER id_id
  zrad_perturbation = model_config_rec%zrad_perturbation(id_id)
  RETURN
END SUBROUTINE nl_get_zrad_perturbation
SUBROUTINE nl_get_hght_perturbation ( id_id , hght_perturbation )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: hght_perturbation
  INTEGER id_id
  hght_perturbation = model_config_rec%hght_perturbation(id_id)
  RETURN
END SUBROUTINE nl_get_hght_perturbation
SUBROUTINE nl_get_stretch_grd ( id_id , stretch_grd )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: stretch_grd
  INTEGER id_id
  stretch_grd = model_config_rec%stretch_grd(id_id)
  RETURN
END SUBROUTINE nl_get_stretch_grd
SUBROUTINE nl_get_stretch_hyp ( id_id , stretch_hyp )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: stretch_hyp
  INTEGER id_id
  stretch_hyp = model_config_rec%stretch_hyp(id_id)
  RETURN
END SUBROUTINE nl_get_stretch_hyp
SUBROUTINE nl_get_z_grd_scale ( id_id , z_grd_scale )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: z_grd_scale
  INTEGER id_id
  z_grd_scale = model_config_rec%z_grd_scale(id_id)
  RETURN
END SUBROUTINE nl_get_z_grd_scale
SUBROUTINE nl_get_sfc_full_init ( id_id , sfc_full_init )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sfc_full_init
  INTEGER id_id
  sfc_full_init = model_config_rec%sfc_full_init(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_full_init
SUBROUTINE nl_get_sfc_lu_index ( id_id , sfc_lu_index )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sfc_lu_index
  INTEGER id_id
  sfc_lu_index = model_config_rec%sfc_lu_index(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_lu_index
SUBROUTINE nl_get_sfc_tsk ( id_id , sfc_tsk )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sfc_tsk
  INTEGER id_id
  sfc_tsk = model_config_rec%sfc_tsk(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_tsk
SUBROUTINE nl_get_sfc_tmn ( id_id , sfc_tmn )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sfc_tmn
  INTEGER id_id
  sfc_tmn = model_config_rec%sfc_tmn(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_tmn
SUBROUTINE nl_get_fire_read_lu ( id_id , fire_read_lu )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_read_lu
  INTEGER id_id
  fire_read_lu = model_config_rec%fire_read_lu(id_id)
  RETURN
END SUBROUTINE nl_get_fire_read_lu
SUBROUTINE nl_get_fire_read_tsk ( id_id , fire_read_tsk )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_read_tsk
  INTEGER id_id
  fire_read_tsk = model_config_rec%fire_read_tsk(id_id)
  RETURN
END SUBROUTINE nl_get_fire_read_tsk
SUBROUTINE nl_get_fire_read_tmn ( id_id , fire_read_tmn )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_read_tmn
  INTEGER id_id
  fire_read_tmn = model_config_rec%fire_read_tmn(id_id)
  RETURN
END SUBROUTINE nl_get_fire_read_tmn
SUBROUTINE nl_get_fire_read_atm_ht ( id_id , fire_read_atm_ht )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_read_atm_ht
  INTEGER id_id
  fire_read_atm_ht = model_config_rec%fire_read_atm_ht(id_id)
  RETURN
END SUBROUTINE nl_get_fire_read_atm_ht
SUBROUTINE nl_get_fire_read_fire_ht ( id_id , fire_read_fire_ht )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_read_fire_ht
  INTEGER id_id
  fire_read_fire_ht = model_config_rec%fire_read_fire_ht(id_id)
  RETURN
END SUBROUTINE nl_get_fire_read_fire_ht
SUBROUTINE nl_get_fire_read_atm_grad ( id_id , fire_read_atm_grad )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_read_atm_grad
  INTEGER id_id
  fire_read_atm_grad = model_config_rec%fire_read_atm_grad(id_id)
  RETURN
END SUBROUTINE nl_get_fire_read_atm_grad
SUBROUTINE nl_get_fire_read_fire_grad ( id_id , fire_read_fire_grad )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_read_fire_grad
  INTEGER id_id
  fire_read_fire_grad = model_config_rec%fire_read_fire_grad(id_id)
  RETURN
END SUBROUTINE nl_get_fire_read_fire_grad
SUBROUTINE nl_get_sfc_vegfra ( id_id , sfc_vegfra )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sfc_vegfra
  INTEGER id_id
  sfc_vegfra = model_config_rec%sfc_vegfra(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_vegfra
SUBROUTINE nl_get_sfc_canwat ( id_id , sfc_canwat )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sfc_canwat
  INTEGER id_id
  sfc_canwat = model_config_rec%sfc_canwat(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_canwat
SUBROUTINE nl_get_sfc_ivgtyp ( id_id , sfc_ivgtyp )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sfc_ivgtyp
  INTEGER id_id
  sfc_ivgtyp = model_config_rec%sfc_ivgtyp(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_ivgtyp
SUBROUTINE nl_get_sfc_isltyp ( id_id , sfc_isltyp )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sfc_isltyp
  INTEGER id_id
  sfc_isltyp = model_config_rec%sfc_isltyp(id_id)
  RETURN
END SUBROUTINE nl_get_sfc_isltyp
SUBROUTINE nl_get_fire_lsm_reinit ( id_id , fire_lsm_reinit )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_lsm_reinit
  INTEGER id_id
  fire_lsm_reinit = model_config_rec%fire_lsm_reinit(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lsm_reinit
SUBROUTINE nl_get_fire_lsm_reinit_iter ( id_id , fire_lsm_reinit_iter )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_lsm_reinit_iter
  INTEGER id_id
  fire_lsm_reinit_iter = model_config_rec%fire_lsm_reinit_iter(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lsm_reinit_iter
SUBROUTINE nl_get_fire_upwinding_reinit ( id_id , fire_upwinding_reinit )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_upwinding_reinit
  INTEGER id_id
  fire_upwinding_reinit = model_config_rec%fire_upwinding_reinit(id_id)
  RETURN
END SUBROUTINE nl_get_fire_upwinding_reinit
SUBROUTINE nl_get_fire_is_real_perim ( id_id , fire_is_real_perim )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_is_real_perim
  INTEGER id_id
  fire_is_real_perim = model_config_rec%fire_is_real_perim(id_id)
  RETURN
END SUBROUTINE nl_get_fire_is_real_perim
SUBROUTINE nl_get_fire_lsm_band_ngp ( id_id , fire_lsm_band_ngp )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_lsm_band_ngp
  INTEGER id_id
  fire_lsm_band_ngp = model_config_rec%fire_lsm_band_ngp(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lsm_band_ngp
SUBROUTINE nl_get_fire_lsm_zcoupling ( id_id , fire_lsm_zcoupling )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fire_lsm_zcoupling
  INTEGER id_id
  fire_lsm_zcoupling = model_config_rec%fire_lsm_zcoupling(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lsm_zcoupling
SUBROUTINE nl_get_fire_lsm_zcoupling_ref ( id_id , fire_lsm_zcoupling_ref )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_lsm_zcoupling_ref
  INTEGER id_id
  fire_lsm_zcoupling_ref = model_config_rec%fire_lsm_zcoupling_ref(id_id)
  RETURN
END SUBROUTINE nl_get_fire_lsm_zcoupling_ref
SUBROUTINE nl_get_fire_tracer_smoke ( id_id , fire_tracer_smoke )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_tracer_smoke
  INTEGER id_id
  fire_tracer_smoke = model_config_rec%fire_tracer_smoke(id_id)
  RETURN
END SUBROUTINE nl_get_fire_tracer_smoke
SUBROUTINE nl_get_fire_viscosity_bg ( id_id , fire_viscosity_bg )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_viscosity_bg
  INTEGER id_id
  fire_viscosity_bg = model_config_rec%fire_viscosity_bg(id_id)
  RETURN
END SUBROUTINE nl_get_fire_viscosity_bg
SUBROUTINE nl_get_fire_viscosity_band ( id_id , fire_viscosity_band )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_viscosity_band
  INTEGER id_id
  fire_viscosity_band = model_config_rec%fire_viscosity_band(id_id)
  RETURN
END SUBROUTINE nl_get_fire_viscosity_band
SUBROUTINE nl_get_fire_viscosity_ngp ( id_id , fire_viscosity_ngp )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fire_viscosity_ngp
  INTEGER id_id
  fire_viscosity_ngp = model_config_rec%fire_viscosity_ngp(id_id)
  RETURN
END SUBROUTINE nl_get_fire_viscosity_ngp
SUBROUTINE nl_get_fire_slope_factor ( id_id , fire_slope_factor )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fire_slope_factor
  INTEGER id_id
  fire_slope_factor = model_config_rec%fire_slope_factor(id_id)
  RETURN
END SUBROUTINE nl_get_fire_slope_factor
SUBROUTINE nl_get_fs_array_maxsize ( id_id , fs_array_maxsize )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_array_maxsize
  INTEGER id_id
  fs_array_maxsize = model_config_rec%fs_array_maxsize
  RETURN
END SUBROUTINE nl_get_fs_array_maxsize
SUBROUTINE nl_get_fs_firebrand_gen_lim ( id_id , fs_firebrand_gen_lim )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_firebrand_gen_lim
  INTEGER id_id
  fs_firebrand_gen_lim = model_config_rec%fs_firebrand_gen_lim
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_lim

!ENDOFREGISTRYGENERATEDINCLUDE


