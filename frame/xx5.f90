!STARTOFREGISTRYGENERATEDINCLUDE 'inc/nl_config.inc'
!
! WARNING This file is generated automatically by use_registry
! using the data base in the file named Registry.
! Do not edit.  Your changes to this file will be lost.
!

SUBROUTINE nl_set_frames_per_auxinput8 ( id_id , frames_per_auxinput8 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput8
  INTEGER id_id
  model_config_rec%frames_per_auxinput8(id_id) = frames_per_auxinput8
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput8
SUBROUTINE nl_set_auxinput9_inname ( id_id , auxinput9_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput9_inname
  INTEGER id_id
  model_config_rec%auxinput9_inname = trim(auxinput9_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput9_inname
SUBROUTINE nl_set_auxinput9_outname ( id_id , auxinput9_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput9_outname
  INTEGER id_id
  model_config_rec%auxinput9_outname = trim(auxinput9_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput9_outname
SUBROUTINE nl_set_auxinput9_interval_y ( id_id , auxinput9_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_interval_y
  INTEGER id_id
  model_config_rec%auxinput9_interval_y(id_id) = auxinput9_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput9_interval_y
SUBROUTINE nl_set_auxinput9_interval_d ( id_id , auxinput9_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_interval_d
  INTEGER id_id
  model_config_rec%auxinput9_interval_d(id_id) = auxinput9_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput9_interval_d
SUBROUTINE nl_set_auxinput9_interval_h ( id_id , auxinput9_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_interval_h
  INTEGER id_id
  model_config_rec%auxinput9_interval_h(id_id) = auxinput9_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput9_interval_h
SUBROUTINE nl_set_auxinput9_interval_m ( id_id , auxinput9_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_interval_m
  INTEGER id_id
  model_config_rec%auxinput9_interval_m(id_id) = auxinput9_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput9_interval_m
SUBROUTINE nl_set_auxinput9_interval_s ( id_id , auxinput9_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_interval_s
  INTEGER id_id
  model_config_rec%auxinput9_interval_s(id_id) = auxinput9_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput9_interval_s
SUBROUTINE nl_set_auxinput9_interval ( id_id , auxinput9_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_interval
  INTEGER id_id
  model_config_rec%auxinput9_interval(id_id) = auxinput9_interval
  RETURN
END SUBROUTINE nl_set_auxinput9_interval
SUBROUTINE nl_set_auxinput9_begin_y ( id_id , auxinput9_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_begin_y
  INTEGER id_id
  model_config_rec%auxinput9_begin_y(id_id) = auxinput9_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput9_begin_y
SUBROUTINE nl_set_auxinput9_begin_d ( id_id , auxinput9_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_begin_d
  INTEGER id_id
  model_config_rec%auxinput9_begin_d(id_id) = auxinput9_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput9_begin_d
SUBROUTINE nl_set_auxinput9_begin_h ( id_id , auxinput9_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_begin_h
  INTEGER id_id
  model_config_rec%auxinput9_begin_h(id_id) = auxinput9_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput9_begin_h
SUBROUTINE nl_set_auxinput9_begin_m ( id_id , auxinput9_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_begin_m
  INTEGER id_id
  model_config_rec%auxinput9_begin_m(id_id) = auxinput9_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput9_begin_m
SUBROUTINE nl_set_auxinput9_begin_s ( id_id , auxinput9_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_begin_s
  INTEGER id_id
  model_config_rec%auxinput9_begin_s(id_id) = auxinput9_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput9_begin_s
SUBROUTINE nl_set_auxinput9_begin ( id_id , auxinput9_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_begin
  INTEGER id_id
  model_config_rec%auxinput9_begin(id_id) = auxinput9_begin
  RETURN
END SUBROUTINE nl_set_auxinput9_begin
SUBROUTINE nl_set_auxinput9_end_y ( id_id , auxinput9_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_end_y
  INTEGER id_id
  model_config_rec%auxinput9_end_y(id_id) = auxinput9_end_y
  RETURN
END SUBROUTINE nl_set_auxinput9_end_y
SUBROUTINE nl_set_auxinput9_end_d ( id_id , auxinput9_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_end_d
  INTEGER id_id
  model_config_rec%auxinput9_end_d(id_id) = auxinput9_end_d
  RETURN
END SUBROUTINE nl_set_auxinput9_end_d
SUBROUTINE nl_set_auxinput9_end_h ( id_id , auxinput9_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_end_h
  INTEGER id_id
  model_config_rec%auxinput9_end_h(id_id) = auxinput9_end_h
  RETURN
END SUBROUTINE nl_set_auxinput9_end_h
SUBROUTINE nl_set_auxinput9_end_m ( id_id , auxinput9_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_end_m
  INTEGER id_id
  model_config_rec%auxinput9_end_m(id_id) = auxinput9_end_m
  RETURN
END SUBROUTINE nl_set_auxinput9_end_m
SUBROUTINE nl_set_auxinput9_end_s ( id_id , auxinput9_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_end_s
  INTEGER id_id
  model_config_rec%auxinput9_end_s(id_id) = auxinput9_end_s
  RETURN
END SUBROUTINE nl_set_auxinput9_end_s
SUBROUTINE nl_set_auxinput9_end ( id_id , auxinput9_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput9_end
  INTEGER id_id
  model_config_rec%auxinput9_end(id_id) = auxinput9_end
  RETURN
END SUBROUTINE nl_set_auxinput9_end
SUBROUTINE nl_set_io_form_auxinput9 ( id_id , io_form_auxinput9 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput9
  INTEGER id_id
  model_config_rec%io_form_auxinput9 = io_form_auxinput9 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput9
SUBROUTINE nl_set_frames_per_auxinput9 ( id_id , frames_per_auxinput9 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput9
  INTEGER id_id
  model_config_rec%frames_per_auxinput9(id_id) = frames_per_auxinput9
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput9
SUBROUTINE nl_set_auxinput10_inname ( id_id , auxinput10_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput10_inname
  INTEGER id_id
  model_config_rec%auxinput10_inname = trim(auxinput10_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput10_inname
SUBROUTINE nl_set_auxinput10_outname ( id_id , auxinput10_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput10_outname
  INTEGER id_id
  model_config_rec%auxinput10_outname = trim(auxinput10_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput10_outname
SUBROUTINE nl_set_auxinput10_interval_y ( id_id , auxinput10_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_interval_y
  INTEGER id_id
  model_config_rec%auxinput10_interval_y(id_id) = auxinput10_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput10_interval_y
SUBROUTINE nl_set_auxinput10_interval_d ( id_id , auxinput10_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_interval_d
  INTEGER id_id
  model_config_rec%auxinput10_interval_d(id_id) = auxinput10_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput10_interval_d
SUBROUTINE nl_set_auxinput10_interval_h ( id_id , auxinput10_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_interval_h
  INTEGER id_id
  model_config_rec%auxinput10_interval_h(id_id) = auxinput10_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput10_interval_h
SUBROUTINE nl_set_auxinput10_interval_m ( id_id , auxinput10_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_interval_m
  INTEGER id_id
  model_config_rec%auxinput10_interval_m(id_id) = auxinput10_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput10_interval_m
SUBROUTINE nl_set_auxinput10_interval_s ( id_id , auxinput10_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_interval_s
  INTEGER id_id
  model_config_rec%auxinput10_interval_s(id_id) = auxinput10_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput10_interval_s
SUBROUTINE nl_set_auxinput10_interval ( id_id , auxinput10_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_interval
  INTEGER id_id
  model_config_rec%auxinput10_interval(id_id) = auxinput10_interval
  RETURN
END SUBROUTINE nl_set_auxinput10_interval
SUBROUTINE nl_set_auxinput10_begin_y ( id_id , auxinput10_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_begin_y
  INTEGER id_id
  model_config_rec%auxinput10_begin_y(id_id) = auxinput10_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput10_begin_y
SUBROUTINE nl_set_auxinput10_begin_d ( id_id , auxinput10_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_begin_d
  INTEGER id_id
  model_config_rec%auxinput10_begin_d(id_id) = auxinput10_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput10_begin_d
SUBROUTINE nl_set_auxinput10_begin_h ( id_id , auxinput10_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_begin_h
  INTEGER id_id
  model_config_rec%auxinput10_begin_h(id_id) = auxinput10_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput10_begin_h
SUBROUTINE nl_set_auxinput10_begin_m ( id_id , auxinput10_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_begin_m
  INTEGER id_id
  model_config_rec%auxinput10_begin_m(id_id) = auxinput10_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput10_begin_m
SUBROUTINE nl_set_auxinput10_begin_s ( id_id , auxinput10_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_begin_s
  INTEGER id_id
  model_config_rec%auxinput10_begin_s(id_id) = auxinput10_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput10_begin_s
SUBROUTINE nl_set_auxinput10_begin ( id_id , auxinput10_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_begin
  INTEGER id_id
  model_config_rec%auxinput10_begin(id_id) = auxinput10_begin
  RETURN
END SUBROUTINE nl_set_auxinput10_begin
SUBROUTINE nl_set_auxinput10_end_y ( id_id , auxinput10_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_end_y
  INTEGER id_id
  model_config_rec%auxinput10_end_y(id_id) = auxinput10_end_y
  RETURN
END SUBROUTINE nl_set_auxinput10_end_y
SUBROUTINE nl_set_auxinput10_end_d ( id_id , auxinput10_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_end_d
  INTEGER id_id
  model_config_rec%auxinput10_end_d(id_id) = auxinput10_end_d
  RETURN
END SUBROUTINE nl_set_auxinput10_end_d
SUBROUTINE nl_set_auxinput10_end_h ( id_id , auxinput10_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_end_h
  INTEGER id_id
  model_config_rec%auxinput10_end_h(id_id) = auxinput10_end_h
  RETURN
END SUBROUTINE nl_set_auxinput10_end_h
SUBROUTINE nl_set_auxinput10_end_m ( id_id , auxinput10_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_end_m
  INTEGER id_id
  model_config_rec%auxinput10_end_m(id_id) = auxinput10_end_m
  RETURN
END SUBROUTINE nl_set_auxinput10_end_m
SUBROUTINE nl_set_auxinput10_end_s ( id_id , auxinput10_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_end_s
  INTEGER id_id
  model_config_rec%auxinput10_end_s(id_id) = auxinput10_end_s
  RETURN
END SUBROUTINE nl_set_auxinput10_end_s
SUBROUTINE nl_set_auxinput10_end ( id_id , auxinput10_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput10_end
  INTEGER id_id
  model_config_rec%auxinput10_end(id_id) = auxinput10_end
  RETURN
END SUBROUTINE nl_set_auxinput10_end
SUBROUTINE nl_set_io_form_auxinput10 ( id_id , io_form_auxinput10 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput10
  INTEGER id_id
  model_config_rec%io_form_auxinput10 = io_form_auxinput10 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput10
SUBROUTINE nl_set_frames_per_auxinput10 ( id_id , frames_per_auxinput10 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput10
  INTEGER id_id
  model_config_rec%frames_per_auxinput10(id_id) = frames_per_auxinput10
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput10
SUBROUTINE nl_set_auxinput11_inname ( id_id , auxinput11_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput11_inname
  INTEGER id_id
  model_config_rec%auxinput11_inname = trim(auxinput11_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput11_inname
SUBROUTINE nl_set_auxinput11_outname ( id_id , auxinput11_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput11_outname
  INTEGER id_id
  model_config_rec%auxinput11_outname = trim(auxinput11_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput11_outname
SUBROUTINE nl_set_auxinput11_interval_y ( id_id , auxinput11_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_interval_y
  INTEGER id_id
  model_config_rec%auxinput11_interval_y(id_id) = auxinput11_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput11_interval_y
SUBROUTINE nl_set_auxinput11_interval_d ( id_id , auxinput11_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_interval_d
  INTEGER id_id
  model_config_rec%auxinput11_interval_d(id_id) = auxinput11_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput11_interval_d
SUBROUTINE nl_set_auxinput11_interval_h ( id_id , auxinput11_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_interval_h
  INTEGER id_id
  model_config_rec%auxinput11_interval_h(id_id) = auxinput11_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput11_interval_h
SUBROUTINE nl_set_auxinput11_interval_m ( id_id , auxinput11_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_interval_m
  INTEGER id_id
  model_config_rec%auxinput11_interval_m(id_id) = auxinput11_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput11_interval_m
SUBROUTINE nl_set_auxinput11_interval_s ( id_id , auxinput11_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_interval_s
  INTEGER id_id
  model_config_rec%auxinput11_interval_s(id_id) = auxinput11_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput11_interval_s
SUBROUTINE nl_set_auxinput11_interval ( id_id , auxinput11_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_interval
  INTEGER id_id
  model_config_rec%auxinput11_interval(id_id) = auxinput11_interval
  RETURN
END SUBROUTINE nl_set_auxinput11_interval
SUBROUTINE nl_set_auxinput11_begin_y ( id_id , auxinput11_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_begin_y
  INTEGER id_id
  model_config_rec%auxinput11_begin_y(id_id) = auxinput11_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput11_begin_y
SUBROUTINE nl_set_auxinput11_begin_d ( id_id , auxinput11_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_begin_d
  INTEGER id_id
  model_config_rec%auxinput11_begin_d(id_id) = auxinput11_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput11_begin_d
SUBROUTINE nl_set_auxinput11_begin_h ( id_id , auxinput11_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_begin_h
  INTEGER id_id
  model_config_rec%auxinput11_begin_h(id_id) = auxinput11_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput11_begin_h
SUBROUTINE nl_set_auxinput11_begin_m ( id_id , auxinput11_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_begin_m
  INTEGER id_id
  model_config_rec%auxinput11_begin_m(id_id) = auxinput11_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput11_begin_m
SUBROUTINE nl_set_auxinput11_begin_s ( id_id , auxinput11_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_begin_s
  INTEGER id_id
  model_config_rec%auxinput11_begin_s(id_id) = auxinput11_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput11_begin_s
SUBROUTINE nl_set_auxinput11_begin ( id_id , auxinput11_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_begin
  INTEGER id_id
  model_config_rec%auxinput11_begin(id_id) = auxinput11_begin
  RETURN
END SUBROUTINE nl_set_auxinput11_begin
SUBROUTINE nl_set_auxinput11_end_y ( id_id , auxinput11_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_end_y
  INTEGER id_id
  model_config_rec%auxinput11_end_y(id_id) = auxinput11_end_y
  RETURN
END SUBROUTINE nl_set_auxinput11_end_y
SUBROUTINE nl_set_auxinput11_end_d ( id_id , auxinput11_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_end_d
  INTEGER id_id
  model_config_rec%auxinput11_end_d(id_id) = auxinput11_end_d
  RETURN
END SUBROUTINE nl_set_auxinput11_end_d
SUBROUTINE nl_set_auxinput11_end_h ( id_id , auxinput11_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_end_h
  INTEGER id_id
  model_config_rec%auxinput11_end_h(id_id) = auxinput11_end_h
  RETURN
END SUBROUTINE nl_set_auxinput11_end_h
SUBROUTINE nl_set_auxinput11_end_m ( id_id , auxinput11_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_end_m
  INTEGER id_id
  model_config_rec%auxinput11_end_m(id_id) = auxinput11_end_m
  RETURN
END SUBROUTINE nl_set_auxinput11_end_m
SUBROUTINE nl_set_auxinput11_end_s ( id_id , auxinput11_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_end_s
  INTEGER id_id
  model_config_rec%auxinput11_end_s(id_id) = auxinput11_end_s
  RETURN
END SUBROUTINE nl_set_auxinput11_end_s
SUBROUTINE nl_set_auxinput11_end ( id_id , auxinput11_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput11_end
  INTEGER id_id
  model_config_rec%auxinput11_end(id_id) = auxinput11_end
  RETURN
END SUBROUTINE nl_set_auxinput11_end
SUBROUTINE nl_set_io_form_auxinput11 ( id_id , io_form_auxinput11 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput11
  INTEGER id_id
  model_config_rec%io_form_auxinput11 = io_form_auxinput11 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput11
SUBROUTINE nl_set_frames_per_auxinput11 ( id_id , frames_per_auxinput11 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput11
  INTEGER id_id
  model_config_rec%frames_per_auxinput11(id_id) = frames_per_auxinput11
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput11
SUBROUTINE nl_set_auxinput12_inname ( id_id , auxinput12_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput12_inname
  INTEGER id_id
  model_config_rec%auxinput12_inname = trim(auxinput12_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput12_inname
SUBROUTINE nl_set_auxinput12_outname ( id_id , auxinput12_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput12_outname
  INTEGER id_id
  model_config_rec%auxinput12_outname = trim(auxinput12_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput12_outname
SUBROUTINE nl_set_auxinput12_interval_y ( id_id , auxinput12_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_interval_y
  INTEGER id_id
  model_config_rec%auxinput12_interval_y(id_id) = auxinput12_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput12_interval_y
SUBROUTINE nl_set_auxinput12_interval_d ( id_id , auxinput12_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_interval_d
  INTEGER id_id
  model_config_rec%auxinput12_interval_d(id_id) = auxinput12_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput12_interval_d
SUBROUTINE nl_set_auxinput12_interval_h ( id_id , auxinput12_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_interval_h
  INTEGER id_id
  model_config_rec%auxinput12_interval_h(id_id) = auxinput12_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput12_interval_h
SUBROUTINE nl_set_auxinput12_interval_m ( id_id , auxinput12_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_interval_m
  INTEGER id_id
  model_config_rec%auxinput12_interval_m(id_id) = auxinput12_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput12_interval_m
SUBROUTINE nl_set_auxinput12_interval_s ( id_id , auxinput12_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_interval_s
  INTEGER id_id
  model_config_rec%auxinput12_interval_s(id_id) = auxinput12_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput12_interval_s
SUBROUTINE nl_set_auxinput12_interval ( id_id , auxinput12_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_interval
  INTEGER id_id
  model_config_rec%auxinput12_interval(id_id) = auxinput12_interval
  RETURN
END SUBROUTINE nl_set_auxinput12_interval
SUBROUTINE nl_set_auxinput12_begin_y ( id_id , auxinput12_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_begin_y
  INTEGER id_id
  model_config_rec%auxinput12_begin_y(id_id) = auxinput12_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput12_begin_y
SUBROUTINE nl_set_auxinput12_begin_d ( id_id , auxinput12_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_begin_d
  INTEGER id_id
  model_config_rec%auxinput12_begin_d(id_id) = auxinput12_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput12_begin_d
SUBROUTINE nl_set_auxinput12_begin_h ( id_id , auxinput12_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_begin_h
  INTEGER id_id
  model_config_rec%auxinput12_begin_h(id_id) = auxinput12_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput12_begin_h
SUBROUTINE nl_set_auxinput12_begin_m ( id_id , auxinput12_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_begin_m
  INTEGER id_id
  model_config_rec%auxinput12_begin_m(id_id) = auxinput12_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput12_begin_m
SUBROUTINE nl_set_auxinput12_begin_s ( id_id , auxinput12_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_begin_s
  INTEGER id_id
  model_config_rec%auxinput12_begin_s(id_id) = auxinput12_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput12_begin_s
SUBROUTINE nl_set_auxinput12_begin ( id_id , auxinput12_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_begin
  INTEGER id_id
  model_config_rec%auxinput12_begin(id_id) = auxinput12_begin
  RETURN
END SUBROUTINE nl_set_auxinput12_begin
SUBROUTINE nl_set_auxinput12_end_y ( id_id , auxinput12_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_end_y
  INTEGER id_id
  model_config_rec%auxinput12_end_y(id_id) = auxinput12_end_y
  RETURN
END SUBROUTINE nl_set_auxinput12_end_y
SUBROUTINE nl_set_auxinput12_end_d ( id_id , auxinput12_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_end_d
  INTEGER id_id
  model_config_rec%auxinput12_end_d(id_id) = auxinput12_end_d
  RETURN
END SUBROUTINE nl_set_auxinput12_end_d
SUBROUTINE nl_set_auxinput12_end_h ( id_id , auxinput12_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_end_h
  INTEGER id_id
  model_config_rec%auxinput12_end_h(id_id) = auxinput12_end_h
  RETURN
END SUBROUTINE nl_set_auxinput12_end_h
SUBROUTINE nl_set_auxinput12_end_m ( id_id , auxinput12_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_end_m
  INTEGER id_id
  model_config_rec%auxinput12_end_m(id_id) = auxinput12_end_m
  RETURN
END SUBROUTINE nl_set_auxinput12_end_m
SUBROUTINE nl_set_auxinput12_end_s ( id_id , auxinput12_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_end_s
  INTEGER id_id
  model_config_rec%auxinput12_end_s(id_id) = auxinput12_end_s
  RETURN
END SUBROUTINE nl_set_auxinput12_end_s
SUBROUTINE nl_set_auxinput12_end ( id_id , auxinput12_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput12_end
  INTEGER id_id
  model_config_rec%auxinput12_end(id_id) = auxinput12_end
  RETURN
END SUBROUTINE nl_set_auxinput12_end
SUBROUTINE nl_set_io_form_auxinput12 ( id_id , io_form_auxinput12 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput12
  INTEGER id_id
  model_config_rec%io_form_auxinput12 = io_form_auxinput12 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput12
SUBROUTINE nl_set_frames_per_auxinput12 ( id_id , frames_per_auxinput12 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput12
  INTEGER id_id
  model_config_rec%frames_per_auxinput12(id_id) = frames_per_auxinput12
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput12
SUBROUTINE nl_set_auxinput13_inname ( id_id , auxinput13_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput13_inname
  INTEGER id_id
  model_config_rec%auxinput13_inname = trim(auxinput13_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput13_inname
SUBROUTINE nl_set_auxinput13_outname ( id_id , auxinput13_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput13_outname
  INTEGER id_id
  model_config_rec%auxinput13_outname = trim(auxinput13_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput13_outname
SUBROUTINE nl_set_auxinput13_interval_y ( id_id , auxinput13_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_interval_y
  INTEGER id_id
  model_config_rec%auxinput13_interval_y(id_id) = auxinput13_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput13_interval_y
SUBROUTINE nl_set_auxinput13_interval_d ( id_id , auxinput13_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_interval_d
  INTEGER id_id
  model_config_rec%auxinput13_interval_d(id_id) = auxinput13_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput13_interval_d
SUBROUTINE nl_set_auxinput13_interval_h ( id_id , auxinput13_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_interval_h
  INTEGER id_id
  model_config_rec%auxinput13_interval_h(id_id) = auxinput13_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput13_interval_h
SUBROUTINE nl_set_auxinput13_interval_m ( id_id , auxinput13_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_interval_m
  INTEGER id_id
  model_config_rec%auxinput13_interval_m(id_id) = auxinput13_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput13_interval_m
SUBROUTINE nl_set_auxinput13_interval_s ( id_id , auxinput13_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_interval_s
  INTEGER id_id
  model_config_rec%auxinput13_interval_s(id_id) = auxinput13_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput13_interval_s
SUBROUTINE nl_set_auxinput13_interval ( id_id , auxinput13_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_interval
  INTEGER id_id
  model_config_rec%auxinput13_interval(id_id) = auxinput13_interval
  RETURN
END SUBROUTINE nl_set_auxinput13_interval
SUBROUTINE nl_set_auxinput13_begin_y ( id_id , auxinput13_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_begin_y
  INTEGER id_id
  model_config_rec%auxinput13_begin_y(id_id) = auxinput13_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput13_begin_y
SUBROUTINE nl_set_auxinput13_begin_d ( id_id , auxinput13_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_begin_d
  INTEGER id_id
  model_config_rec%auxinput13_begin_d(id_id) = auxinput13_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput13_begin_d
SUBROUTINE nl_set_auxinput13_begin_h ( id_id , auxinput13_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_begin_h
  INTEGER id_id
  model_config_rec%auxinput13_begin_h(id_id) = auxinput13_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput13_begin_h
SUBROUTINE nl_set_auxinput13_begin_m ( id_id , auxinput13_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_begin_m
  INTEGER id_id
  model_config_rec%auxinput13_begin_m(id_id) = auxinput13_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput13_begin_m
SUBROUTINE nl_set_auxinput13_begin_s ( id_id , auxinput13_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_begin_s
  INTEGER id_id
  model_config_rec%auxinput13_begin_s(id_id) = auxinput13_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput13_begin_s
SUBROUTINE nl_set_auxinput13_begin ( id_id , auxinput13_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_begin
  INTEGER id_id
  model_config_rec%auxinput13_begin(id_id) = auxinput13_begin
  RETURN
END SUBROUTINE nl_set_auxinput13_begin
SUBROUTINE nl_set_auxinput13_end_y ( id_id , auxinput13_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_end_y
  INTEGER id_id
  model_config_rec%auxinput13_end_y(id_id) = auxinput13_end_y
  RETURN
END SUBROUTINE nl_set_auxinput13_end_y
SUBROUTINE nl_set_auxinput13_end_d ( id_id , auxinput13_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_end_d
  INTEGER id_id
  model_config_rec%auxinput13_end_d(id_id) = auxinput13_end_d
  RETURN
END SUBROUTINE nl_set_auxinput13_end_d
SUBROUTINE nl_set_auxinput13_end_h ( id_id , auxinput13_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_end_h
  INTEGER id_id
  model_config_rec%auxinput13_end_h(id_id) = auxinput13_end_h
  RETURN
END SUBROUTINE nl_set_auxinput13_end_h
SUBROUTINE nl_set_auxinput13_end_m ( id_id , auxinput13_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_end_m
  INTEGER id_id
  model_config_rec%auxinput13_end_m(id_id) = auxinput13_end_m
  RETURN
END SUBROUTINE nl_set_auxinput13_end_m
SUBROUTINE nl_set_auxinput13_end_s ( id_id , auxinput13_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_end_s
  INTEGER id_id
  model_config_rec%auxinput13_end_s(id_id) = auxinput13_end_s
  RETURN
END SUBROUTINE nl_set_auxinput13_end_s
SUBROUTINE nl_set_auxinput13_end ( id_id , auxinput13_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput13_end
  INTEGER id_id
  model_config_rec%auxinput13_end(id_id) = auxinput13_end
  RETURN
END SUBROUTINE nl_set_auxinput13_end
SUBROUTINE nl_set_io_form_auxinput13 ( id_id , io_form_auxinput13 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput13
  INTEGER id_id
  model_config_rec%io_form_auxinput13 = io_form_auxinput13 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput13
SUBROUTINE nl_set_frames_per_auxinput13 ( id_id , frames_per_auxinput13 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput13
  INTEGER id_id
  model_config_rec%frames_per_auxinput13(id_id) = frames_per_auxinput13
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput13
SUBROUTINE nl_set_auxinput14_inname ( id_id , auxinput14_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput14_inname
  INTEGER id_id
  model_config_rec%auxinput14_inname = trim(auxinput14_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput14_inname
SUBROUTINE nl_set_auxinput14_outname ( id_id , auxinput14_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput14_outname
  INTEGER id_id
  model_config_rec%auxinput14_outname = trim(auxinput14_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput14_outname
SUBROUTINE nl_set_auxinput14_interval_y ( id_id , auxinput14_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_interval_y
  INTEGER id_id
  model_config_rec%auxinput14_interval_y(id_id) = auxinput14_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput14_interval_y
SUBROUTINE nl_set_auxinput14_interval_d ( id_id , auxinput14_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_interval_d
  INTEGER id_id
  model_config_rec%auxinput14_interval_d(id_id) = auxinput14_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput14_interval_d
SUBROUTINE nl_set_auxinput14_interval_h ( id_id , auxinput14_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_interval_h
  INTEGER id_id
  model_config_rec%auxinput14_interval_h(id_id) = auxinput14_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput14_interval_h
SUBROUTINE nl_set_auxinput14_interval_m ( id_id , auxinput14_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_interval_m
  INTEGER id_id
  model_config_rec%auxinput14_interval_m(id_id) = auxinput14_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput14_interval_m
SUBROUTINE nl_set_auxinput14_interval_s ( id_id , auxinput14_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_interval_s
  INTEGER id_id
  model_config_rec%auxinput14_interval_s(id_id) = auxinput14_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput14_interval_s
SUBROUTINE nl_set_auxinput14_interval ( id_id , auxinput14_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_interval
  INTEGER id_id
  model_config_rec%auxinput14_interval(id_id) = auxinput14_interval
  RETURN
END SUBROUTINE nl_set_auxinput14_interval
SUBROUTINE nl_set_auxinput14_begin_y ( id_id , auxinput14_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_begin_y
  INTEGER id_id
  model_config_rec%auxinput14_begin_y(id_id) = auxinput14_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput14_begin_y
SUBROUTINE nl_set_auxinput14_begin_d ( id_id , auxinput14_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_begin_d
  INTEGER id_id
  model_config_rec%auxinput14_begin_d(id_id) = auxinput14_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput14_begin_d
SUBROUTINE nl_set_auxinput14_begin_h ( id_id , auxinput14_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_begin_h
  INTEGER id_id
  model_config_rec%auxinput14_begin_h(id_id) = auxinput14_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput14_begin_h
SUBROUTINE nl_set_auxinput14_begin_m ( id_id , auxinput14_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_begin_m
  INTEGER id_id
  model_config_rec%auxinput14_begin_m(id_id) = auxinput14_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput14_begin_m
SUBROUTINE nl_set_auxinput14_begin_s ( id_id , auxinput14_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_begin_s
  INTEGER id_id
  model_config_rec%auxinput14_begin_s(id_id) = auxinput14_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput14_begin_s
SUBROUTINE nl_set_auxinput14_begin ( id_id , auxinput14_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_begin
  INTEGER id_id
  model_config_rec%auxinput14_begin(id_id) = auxinput14_begin
  RETURN
END SUBROUTINE nl_set_auxinput14_begin
SUBROUTINE nl_set_auxinput14_end_y ( id_id , auxinput14_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_end_y
  INTEGER id_id
  model_config_rec%auxinput14_end_y(id_id) = auxinput14_end_y
  RETURN
END SUBROUTINE nl_set_auxinput14_end_y
SUBROUTINE nl_set_auxinput14_end_d ( id_id , auxinput14_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_end_d
  INTEGER id_id
  model_config_rec%auxinput14_end_d(id_id) = auxinput14_end_d
  RETURN
END SUBROUTINE nl_set_auxinput14_end_d
SUBROUTINE nl_set_auxinput14_end_h ( id_id , auxinput14_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_end_h
  INTEGER id_id
  model_config_rec%auxinput14_end_h(id_id) = auxinput14_end_h
  RETURN
END SUBROUTINE nl_set_auxinput14_end_h
SUBROUTINE nl_set_auxinput14_end_m ( id_id , auxinput14_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_end_m
  INTEGER id_id
  model_config_rec%auxinput14_end_m(id_id) = auxinput14_end_m
  RETURN
END SUBROUTINE nl_set_auxinput14_end_m
SUBROUTINE nl_set_auxinput14_end_s ( id_id , auxinput14_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_end_s
  INTEGER id_id
  model_config_rec%auxinput14_end_s(id_id) = auxinput14_end_s
  RETURN
END SUBROUTINE nl_set_auxinput14_end_s
SUBROUTINE nl_set_auxinput14_end ( id_id , auxinput14_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput14_end
  INTEGER id_id
  model_config_rec%auxinput14_end(id_id) = auxinput14_end
  RETURN
END SUBROUTINE nl_set_auxinput14_end
SUBROUTINE nl_set_io_form_auxinput14 ( id_id , io_form_auxinput14 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput14
  INTEGER id_id
  model_config_rec%io_form_auxinput14 = io_form_auxinput14 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput14
SUBROUTINE nl_set_frames_per_auxinput14 ( id_id , frames_per_auxinput14 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput14
  INTEGER id_id
  model_config_rec%frames_per_auxinput14(id_id) = frames_per_auxinput14
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput14
SUBROUTINE nl_set_auxinput15_inname ( id_id , auxinput15_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput15_inname
  INTEGER id_id
  model_config_rec%auxinput15_inname = trim(auxinput15_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput15_inname
SUBROUTINE nl_set_auxinput15_outname ( id_id , auxinput15_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput15_outname
  INTEGER id_id
  model_config_rec%auxinput15_outname = trim(auxinput15_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput15_outname
SUBROUTINE nl_set_auxinput15_interval_y ( id_id , auxinput15_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_interval_y
  INTEGER id_id
  model_config_rec%auxinput15_interval_y(id_id) = auxinput15_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput15_interval_y
SUBROUTINE nl_set_auxinput15_interval_d ( id_id , auxinput15_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_interval_d
  INTEGER id_id
  model_config_rec%auxinput15_interval_d(id_id) = auxinput15_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput15_interval_d
SUBROUTINE nl_set_auxinput15_interval_h ( id_id , auxinput15_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_interval_h
  INTEGER id_id
  model_config_rec%auxinput15_interval_h(id_id) = auxinput15_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput15_interval_h
SUBROUTINE nl_set_auxinput15_interval_m ( id_id , auxinput15_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_interval_m
  INTEGER id_id
  model_config_rec%auxinput15_interval_m(id_id) = auxinput15_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput15_interval_m
SUBROUTINE nl_set_auxinput15_interval_s ( id_id , auxinput15_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_interval_s
  INTEGER id_id
  model_config_rec%auxinput15_interval_s(id_id) = auxinput15_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput15_interval_s
SUBROUTINE nl_set_auxinput15_interval ( id_id , auxinput15_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_interval
  INTEGER id_id
  model_config_rec%auxinput15_interval(id_id) = auxinput15_interval
  RETURN
END SUBROUTINE nl_set_auxinput15_interval
SUBROUTINE nl_set_auxinput15_begin_y ( id_id , auxinput15_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_begin_y
  INTEGER id_id
  model_config_rec%auxinput15_begin_y(id_id) = auxinput15_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput15_begin_y
SUBROUTINE nl_set_auxinput15_begin_d ( id_id , auxinput15_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_begin_d
  INTEGER id_id
  model_config_rec%auxinput15_begin_d(id_id) = auxinput15_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput15_begin_d
SUBROUTINE nl_set_auxinput15_begin_h ( id_id , auxinput15_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_begin_h
  INTEGER id_id
  model_config_rec%auxinput15_begin_h(id_id) = auxinput15_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput15_begin_h
SUBROUTINE nl_set_auxinput15_begin_m ( id_id , auxinput15_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_begin_m
  INTEGER id_id
  model_config_rec%auxinput15_begin_m(id_id) = auxinput15_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput15_begin_m
SUBROUTINE nl_set_auxinput15_begin_s ( id_id , auxinput15_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_begin_s
  INTEGER id_id
  model_config_rec%auxinput15_begin_s(id_id) = auxinput15_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput15_begin_s
SUBROUTINE nl_set_auxinput15_begin ( id_id , auxinput15_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_begin
  INTEGER id_id
  model_config_rec%auxinput15_begin(id_id) = auxinput15_begin
  RETURN
END SUBROUTINE nl_set_auxinput15_begin
SUBROUTINE nl_set_auxinput15_end_y ( id_id , auxinput15_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_end_y
  INTEGER id_id
  model_config_rec%auxinput15_end_y(id_id) = auxinput15_end_y
  RETURN
END SUBROUTINE nl_set_auxinput15_end_y
SUBROUTINE nl_set_auxinput15_end_d ( id_id , auxinput15_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_end_d
  INTEGER id_id
  model_config_rec%auxinput15_end_d(id_id) = auxinput15_end_d
  RETURN
END SUBROUTINE nl_set_auxinput15_end_d
SUBROUTINE nl_set_auxinput15_end_h ( id_id , auxinput15_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_end_h
  INTEGER id_id
  model_config_rec%auxinput15_end_h(id_id) = auxinput15_end_h
  RETURN
END SUBROUTINE nl_set_auxinput15_end_h
SUBROUTINE nl_set_auxinput15_end_m ( id_id , auxinput15_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_end_m
  INTEGER id_id
  model_config_rec%auxinput15_end_m(id_id) = auxinput15_end_m
  RETURN
END SUBROUTINE nl_set_auxinput15_end_m
SUBROUTINE nl_set_auxinput15_end_s ( id_id , auxinput15_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_end_s
  INTEGER id_id
  model_config_rec%auxinput15_end_s(id_id) = auxinput15_end_s
  RETURN
END SUBROUTINE nl_set_auxinput15_end_s
SUBROUTINE nl_set_auxinput15_end ( id_id , auxinput15_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput15_end
  INTEGER id_id
  model_config_rec%auxinput15_end(id_id) = auxinput15_end
  RETURN
END SUBROUTINE nl_set_auxinput15_end
SUBROUTINE nl_set_io_form_auxinput15 ( id_id , io_form_auxinput15 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput15
  INTEGER id_id
  model_config_rec%io_form_auxinput15 = io_form_auxinput15 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput15
SUBROUTINE nl_set_frames_per_auxinput15 ( id_id , frames_per_auxinput15 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput15
  INTEGER id_id
  model_config_rec%frames_per_auxinput15(id_id) = frames_per_auxinput15
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput15
SUBROUTINE nl_set_auxinput16_inname ( id_id , auxinput16_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput16_inname
  INTEGER id_id
  model_config_rec%auxinput16_inname = trim(auxinput16_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput16_inname
SUBROUTINE nl_set_auxinput16_outname ( id_id , auxinput16_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput16_outname
  INTEGER id_id
  model_config_rec%auxinput16_outname = trim(auxinput16_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput16_outname
SUBROUTINE nl_set_auxinput16_interval_y ( id_id , auxinput16_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_interval_y
  INTEGER id_id
  model_config_rec%auxinput16_interval_y(id_id) = auxinput16_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput16_interval_y
SUBROUTINE nl_set_auxinput16_interval_d ( id_id , auxinput16_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_interval_d
  INTEGER id_id
  model_config_rec%auxinput16_interval_d(id_id) = auxinput16_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput16_interval_d
SUBROUTINE nl_set_auxinput16_interval_h ( id_id , auxinput16_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_interval_h
  INTEGER id_id
  model_config_rec%auxinput16_interval_h(id_id) = auxinput16_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput16_interval_h
SUBROUTINE nl_set_auxinput16_interval_m ( id_id , auxinput16_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_interval_m
  INTEGER id_id
  model_config_rec%auxinput16_interval_m(id_id) = auxinput16_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput16_interval_m
SUBROUTINE nl_set_auxinput16_interval_s ( id_id , auxinput16_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_interval_s
  INTEGER id_id
  model_config_rec%auxinput16_interval_s(id_id) = auxinput16_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput16_interval_s
SUBROUTINE nl_set_auxinput16_interval ( id_id , auxinput16_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_interval
  INTEGER id_id
  model_config_rec%auxinput16_interval(id_id) = auxinput16_interval
  RETURN
END SUBROUTINE nl_set_auxinput16_interval
SUBROUTINE nl_set_auxinput16_begin_y ( id_id , auxinput16_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_begin_y
  INTEGER id_id
  model_config_rec%auxinput16_begin_y(id_id) = auxinput16_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput16_begin_y
SUBROUTINE nl_set_auxinput16_begin_d ( id_id , auxinput16_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_begin_d
  INTEGER id_id
  model_config_rec%auxinput16_begin_d(id_id) = auxinput16_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput16_begin_d
SUBROUTINE nl_set_auxinput16_begin_h ( id_id , auxinput16_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_begin_h
  INTEGER id_id
  model_config_rec%auxinput16_begin_h(id_id) = auxinput16_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput16_begin_h
SUBROUTINE nl_set_auxinput16_begin_m ( id_id , auxinput16_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_begin_m
  INTEGER id_id
  model_config_rec%auxinput16_begin_m(id_id) = auxinput16_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput16_begin_m
SUBROUTINE nl_set_auxinput16_begin_s ( id_id , auxinput16_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_begin_s
  INTEGER id_id
  model_config_rec%auxinput16_begin_s(id_id) = auxinput16_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput16_begin_s
SUBROUTINE nl_set_auxinput16_begin ( id_id , auxinput16_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_begin
  INTEGER id_id
  model_config_rec%auxinput16_begin(id_id) = auxinput16_begin
  RETURN
END SUBROUTINE nl_set_auxinput16_begin
SUBROUTINE nl_set_auxinput16_end_y ( id_id , auxinput16_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_end_y
  INTEGER id_id
  model_config_rec%auxinput16_end_y(id_id) = auxinput16_end_y
  RETURN
END SUBROUTINE nl_set_auxinput16_end_y
SUBROUTINE nl_set_auxinput16_end_d ( id_id , auxinput16_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_end_d
  INTEGER id_id
  model_config_rec%auxinput16_end_d(id_id) = auxinput16_end_d
  RETURN
END SUBROUTINE nl_set_auxinput16_end_d
SUBROUTINE nl_set_auxinput16_end_h ( id_id , auxinput16_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_end_h
  INTEGER id_id
  model_config_rec%auxinput16_end_h(id_id) = auxinput16_end_h
  RETURN
END SUBROUTINE nl_set_auxinput16_end_h
SUBROUTINE nl_set_auxinput16_end_m ( id_id , auxinput16_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_end_m
  INTEGER id_id
  model_config_rec%auxinput16_end_m(id_id) = auxinput16_end_m
  RETURN
END SUBROUTINE nl_set_auxinput16_end_m
SUBROUTINE nl_set_auxinput16_end_s ( id_id , auxinput16_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_end_s
  INTEGER id_id
  model_config_rec%auxinput16_end_s(id_id) = auxinput16_end_s
  RETURN
END SUBROUTINE nl_set_auxinput16_end_s
SUBROUTINE nl_set_auxinput16_end ( id_id , auxinput16_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput16_end
  INTEGER id_id
  model_config_rec%auxinput16_end(id_id) = auxinput16_end
  RETURN
END SUBROUTINE nl_set_auxinput16_end
SUBROUTINE nl_set_io_form_auxinput16 ( id_id , io_form_auxinput16 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput16
  INTEGER id_id
  model_config_rec%io_form_auxinput16 = io_form_auxinput16 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput16
SUBROUTINE nl_set_frames_per_auxinput16 ( id_id , frames_per_auxinput16 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput16
  INTEGER id_id
  model_config_rec%frames_per_auxinput16(id_id) = frames_per_auxinput16
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput16
SUBROUTINE nl_set_auxinput17_inname ( id_id , auxinput17_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput17_inname
  INTEGER id_id
  model_config_rec%auxinput17_inname = trim(auxinput17_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput17_inname
SUBROUTINE nl_set_auxinput17_outname ( id_id , auxinput17_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput17_outname
  INTEGER id_id
  model_config_rec%auxinput17_outname = trim(auxinput17_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput17_outname
SUBROUTINE nl_set_auxinput17_interval_y ( id_id , auxinput17_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_interval_y
  INTEGER id_id
  model_config_rec%auxinput17_interval_y(id_id) = auxinput17_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput17_interval_y
SUBROUTINE nl_set_auxinput17_interval_d ( id_id , auxinput17_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_interval_d
  INTEGER id_id
  model_config_rec%auxinput17_interval_d(id_id) = auxinput17_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput17_interval_d
SUBROUTINE nl_set_auxinput17_interval_h ( id_id , auxinput17_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_interval_h
  INTEGER id_id
  model_config_rec%auxinput17_interval_h(id_id) = auxinput17_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput17_interval_h
SUBROUTINE nl_set_auxinput17_interval_m ( id_id , auxinput17_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_interval_m
  INTEGER id_id
  model_config_rec%auxinput17_interval_m(id_id) = auxinput17_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput17_interval_m
SUBROUTINE nl_set_auxinput17_interval_s ( id_id , auxinput17_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_interval_s
  INTEGER id_id
  model_config_rec%auxinput17_interval_s(id_id) = auxinput17_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput17_interval_s
SUBROUTINE nl_set_auxinput17_interval ( id_id , auxinput17_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_interval
  INTEGER id_id
  model_config_rec%auxinput17_interval(id_id) = auxinput17_interval
  RETURN
END SUBROUTINE nl_set_auxinput17_interval
SUBROUTINE nl_set_auxinput17_begin_y ( id_id , auxinput17_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_begin_y
  INTEGER id_id
  model_config_rec%auxinput17_begin_y(id_id) = auxinput17_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput17_begin_y
SUBROUTINE nl_set_auxinput17_begin_d ( id_id , auxinput17_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_begin_d
  INTEGER id_id
  model_config_rec%auxinput17_begin_d(id_id) = auxinput17_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput17_begin_d
SUBROUTINE nl_set_auxinput17_begin_h ( id_id , auxinput17_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_begin_h
  INTEGER id_id
  model_config_rec%auxinput17_begin_h(id_id) = auxinput17_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput17_begin_h
SUBROUTINE nl_set_auxinput17_begin_m ( id_id , auxinput17_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_begin_m
  INTEGER id_id
  model_config_rec%auxinput17_begin_m(id_id) = auxinput17_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput17_begin_m
SUBROUTINE nl_set_auxinput17_begin_s ( id_id , auxinput17_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_begin_s
  INTEGER id_id
  model_config_rec%auxinput17_begin_s(id_id) = auxinput17_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput17_begin_s
SUBROUTINE nl_set_auxinput17_begin ( id_id , auxinput17_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_begin
  INTEGER id_id
  model_config_rec%auxinput17_begin(id_id) = auxinput17_begin
  RETURN
END SUBROUTINE nl_set_auxinput17_begin
SUBROUTINE nl_set_auxinput17_end_y ( id_id , auxinput17_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_end_y
  INTEGER id_id
  model_config_rec%auxinput17_end_y(id_id) = auxinput17_end_y
  RETURN
END SUBROUTINE nl_set_auxinput17_end_y
SUBROUTINE nl_set_auxinput17_end_d ( id_id , auxinput17_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_end_d
  INTEGER id_id
  model_config_rec%auxinput17_end_d(id_id) = auxinput17_end_d
  RETURN
END SUBROUTINE nl_set_auxinput17_end_d
SUBROUTINE nl_set_auxinput17_end_h ( id_id , auxinput17_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_end_h
  INTEGER id_id
  model_config_rec%auxinput17_end_h(id_id) = auxinput17_end_h
  RETURN
END SUBROUTINE nl_set_auxinput17_end_h
SUBROUTINE nl_set_auxinput17_end_m ( id_id , auxinput17_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_end_m
  INTEGER id_id
  model_config_rec%auxinput17_end_m(id_id) = auxinput17_end_m
  RETURN
END SUBROUTINE nl_set_auxinput17_end_m
SUBROUTINE nl_set_auxinput17_end_s ( id_id , auxinput17_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_end_s
  INTEGER id_id
  model_config_rec%auxinput17_end_s(id_id) = auxinput17_end_s
  RETURN
END SUBROUTINE nl_set_auxinput17_end_s
SUBROUTINE nl_set_auxinput17_end ( id_id , auxinput17_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput17_end
  INTEGER id_id
  model_config_rec%auxinput17_end(id_id) = auxinput17_end
  RETURN
END SUBROUTINE nl_set_auxinput17_end
SUBROUTINE nl_set_io_form_auxinput17 ( id_id , io_form_auxinput17 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput17
  INTEGER id_id
  model_config_rec%io_form_auxinput17 = io_form_auxinput17 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput17
SUBROUTINE nl_set_frames_per_auxinput17 ( id_id , frames_per_auxinput17 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput17
  INTEGER id_id
  model_config_rec%frames_per_auxinput17(id_id) = frames_per_auxinput17
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput17
SUBROUTINE nl_set_auxinput18_inname ( id_id , auxinput18_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput18_inname
  INTEGER id_id
  model_config_rec%auxinput18_inname = trim(auxinput18_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput18_inname
SUBROUTINE nl_set_auxinput18_outname ( id_id , auxinput18_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput18_outname
  INTEGER id_id
  model_config_rec%auxinput18_outname = trim(auxinput18_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput18_outname
SUBROUTINE nl_set_auxinput18_interval_y ( id_id , auxinput18_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_interval_y
  INTEGER id_id
  model_config_rec%auxinput18_interval_y(id_id) = auxinput18_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput18_interval_y
SUBROUTINE nl_set_auxinput18_interval_d ( id_id , auxinput18_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_interval_d
  INTEGER id_id
  model_config_rec%auxinput18_interval_d(id_id) = auxinput18_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput18_interval_d
SUBROUTINE nl_set_auxinput18_interval_h ( id_id , auxinput18_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_interval_h
  INTEGER id_id
  model_config_rec%auxinput18_interval_h(id_id) = auxinput18_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput18_interval_h
SUBROUTINE nl_set_auxinput18_interval_m ( id_id , auxinput18_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_interval_m
  INTEGER id_id
  model_config_rec%auxinput18_interval_m(id_id) = auxinput18_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput18_interval_m
SUBROUTINE nl_set_auxinput18_interval_s ( id_id , auxinput18_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_interval_s
  INTEGER id_id
  model_config_rec%auxinput18_interval_s(id_id) = auxinput18_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput18_interval_s
SUBROUTINE nl_set_auxinput18_interval ( id_id , auxinput18_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_interval
  INTEGER id_id
  model_config_rec%auxinput18_interval(id_id) = auxinput18_interval
  RETURN
END SUBROUTINE nl_set_auxinput18_interval
SUBROUTINE nl_set_auxinput18_begin_y ( id_id , auxinput18_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_begin_y
  INTEGER id_id
  model_config_rec%auxinput18_begin_y(id_id) = auxinput18_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput18_begin_y
SUBROUTINE nl_set_auxinput18_begin_d ( id_id , auxinput18_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_begin_d
  INTEGER id_id
  model_config_rec%auxinput18_begin_d(id_id) = auxinput18_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput18_begin_d
SUBROUTINE nl_set_auxinput18_begin_h ( id_id , auxinput18_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_begin_h
  INTEGER id_id
  model_config_rec%auxinput18_begin_h(id_id) = auxinput18_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput18_begin_h
SUBROUTINE nl_set_auxinput18_begin_m ( id_id , auxinput18_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_begin_m
  INTEGER id_id
  model_config_rec%auxinput18_begin_m(id_id) = auxinput18_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput18_begin_m
SUBROUTINE nl_set_auxinput18_begin_s ( id_id , auxinput18_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_begin_s
  INTEGER id_id
  model_config_rec%auxinput18_begin_s(id_id) = auxinput18_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput18_begin_s
SUBROUTINE nl_set_auxinput18_begin ( id_id , auxinput18_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_begin
  INTEGER id_id
  model_config_rec%auxinput18_begin(id_id) = auxinput18_begin
  RETURN
END SUBROUTINE nl_set_auxinput18_begin
SUBROUTINE nl_set_auxinput18_end_y ( id_id , auxinput18_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_end_y
  INTEGER id_id
  model_config_rec%auxinput18_end_y(id_id) = auxinput18_end_y
  RETURN
END SUBROUTINE nl_set_auxinput18_end_y
SUBROUTINE nl_set_auxinput18_end_d ( id_id , auxinput18_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_end_d
  INTEGER id_id
  model_config_rec%auxinput18_end_d(id_id) = auxinput18_end_d
  RETURN
END SUBROUTINE nl_set_auxinput18_end_d
SUBROUTINE nl_set_auxinput18_end_h ( id_id , auxinput18_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_end_h
  INTEGER id_id
  model_config_rec%auxinput18_end_h(id_id) = auxinput18_end_h
  RETURN
END SUBROUTINE nl_set_auxinput18_end_h
SUBROUTINE nl_set_auxinput18_end_m ( id_id , auxinput18_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_end_m
  INTEGER id_id
  model_config_rec%auxinput18_end_m(id_id) = auxinput18_end_m
  RETURN
END SUBROUTINE nl_set_auxinput18_end_m
SUBROUTINE nl_set_auxinput18_end_s ( id_id , auxinput18_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_end_s
  INTEGER id_id
  model_config_rec%auxinput18_end_s(id_id) = auxinput18_end_s
  RETURN
END SUBROUTINE nl_set_auxinput18_end_s
SUBROUTINE nl_set_auxinput18_end ( id_id , auxinput18_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput18_end
  INTEGER id_id
  model_config_rec%auxinput18_end(id_id) = auxinput18_end
  RETURN
END SUBROUTINE nl_set_auxinput18_end
SUBROUTINE nl_set_io_form_auxinput18 ( id_id , io_form_auxinput18 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput18
  INTEGER id_id
  model_config_rec%io_form_auxinput18 = io_form_auxinput18 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput18
SUBROUTINE nl_set_frames_per_auxinput18 ( id_id , frames_per_auxinput18 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput18
  INTEGER id_id
  model_config_rec%frames_per_auxinput18(id_id) = frames_per_auxinput18
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput18
SUBROUTINE nl_set_auxinput19_inname ( id_id , auxinput19_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput19_inname
  INTEGER id_id
  model_config_rec%auxinput19_inname = trim(auxinput19_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput19_inname
SUBROUTINE nl_set_auxinput19_outname ( id_id , auxinput19_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput19_outname
  INTEGER id_id
  model_config_rec%auxinput19_outname = trim(auxinput19_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput19_outname
SUBROUTINE nl_set_auxinput19_interval_y ( id_id , auxinput19_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_interval_y
  INTEGER id_id
  model_config_rec%auxinput19_interval_y(id_id) = auxinput19_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput19_interval_y
SUBROUTINE nl_set_auxinput19_interval_d ( id_id , auxinput19_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_interval_d
  INTEGER id_id
  model_config_rec%auxinput19_interval_d(id_id) = auxinput19_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput19_interval_d
SUBROUTINE nl_set_auxinput19_interval_h ( id_id , auxinput19_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_interval_h
  INTEGER id_id
  model_config_rec%auxinput19_interval_h(id_id) = auxinput19_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput19_interval_h
SUBROUTINE nl_set_auxinput19_interval_m ( id_id , auxinput19_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_interval_m
  INTEGER id_id
  model_config_rec%auxinput19_interval_m(id_id) = auxinput19_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput19_interval_m
SUBROUTINE nl_set_auxinput19_interval_s ( id_id , auxinput19_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_interval_s
  INTEGER id_id
  model_config_rec%auxinput19_interval_s(id_id) = auxinput19_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput19_interval_s
SUBROUTINE nl_set_auxinput19_interval ( id_id , auxinput19_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_interval
  INTEGER id_id
  model_config_rec%auxinput19_interval(id_id) = auxinput19_interval
  RETURN
END SUBROUTINE nl_set_auxinput19_interval
SUBROUTINE nl_set_auxinput19_begin_y ( id_id , auxinput19_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_begin_y
  INTEGER id_id
  model_config_rec%auxinput19_begin_y(id_id) = auxinput19_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput19_begin_y
SUBROUTINE nl_set_auxinput19_begin_d ( id_id , auxinput19_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_begin_d
  INTEGER id_id
  model_config_rec%auxinput19_begin_d(id_id) = auxinput19_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput19_begin_d
SUBROUTINE nl_set_auxinput19_begin_h ( id_id , auxinput19_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_begin_h
  INTEGER id_id
  model_config_rec%auxinput19_begin_h(id_id) = auxinput19_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput19_begin_h
SUBROUTINE nl_set_auxinput19_begin_m ( id_id , auxinput19_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_begin_m
  INTEGER id_id
  model_config_rec%auxinput19_begin_m(id_id) = auxinput19_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput19_begin_m
SUBROUTINE nl_set_auxinput19_begin_s ( id_id , auxinput19_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_begin_s
  INTEGER id_id
  model_config_rec%auxinput19_begin_s(id_id) = auxinput19_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput19_begin_s
SUBROUTINE nl_set_auxinput19_begin ( id_id , auxinput19_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_begin
  INTEGER id_id
  model_config_rec%auxinput19_begin(id_id) = auxinput19_begin
  RETURN
END SUBROUTINE nl_set_auxinput19_begin
SUBROUTINE nl_set_auxinput19_end_y ( id_id , auxinput19_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_end_y
  INTEGER id_id
  model_config_rec%auxinput19_end_y(id_id) = auxinput19_end_y
  RETURN
END SUBROUTINE nl_set_auxinput19_end_y
SUBROUTINE nl_set_auxinput19_end_d ( id_id , auxinput19_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_end_d
  INTEGER id_id
  model_config_rec%auxinput19_end_d(id_id) = auxinput19_end_d
  RETURN
END SUBROUTINE nl_set_auxinput19_end_d
SUBROUTINE nl_set_auxinput19_end_h ( id_id , auxinput19_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_end_h
  INTEGER id_id
  model_config_rec%auxinput19_end_h(id_id) = auxinput19_end_h
  RETURN
END SUBROUTINE nl_set_auxinput19_end_h
SUBROUTINE nl_set_auxinput19_end_m ( id_id , auxinput19_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_end_m
  INTEGER id_id
  model_config_rec%auxinput19_end_m(id_id) = auxinput19_end_m
  RETURN
END SUBROUTINE nl_set_auxinput19_end_m
SUBROUTINE nl_set_auxinput19_end_s ( id_id , auxinput19_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_end_s
  INTEGER id_id
  model_config_rec%auxinput19_end_s(id_id) = auxinput19_end_s
  RETURN
END SUBROUTINE nl_set_auxinput19_end_s
SUBROUTINE nl_set_auxinput19_end ( id_id , auxinput19_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput19_end
  INTEGER id_id
  model_config_rec%auxinput19_end(id_id) = auxinput19_end
  RETURN
END SUBROUTINE nl_set_auxinput19_end
SUBROUTINE nl_set_io_form_auxinput19 ( id_id , io_form_auxinput19 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput19
  INTEGER id_id
  model_config_rec%io_form_auxinput19 = io_form_auxinput19 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput19
SUBROUTINE nl_set_frames_per_auxinput19 ( id_id , frames_per_auxinput19 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput19
  INTEGER id_id
  model_config_rec%frames_per_auxinput19(id_id) = frames_per_auxinput19
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput19
SUBROUTINE nl_set_auxinput20_inname ( id_id , auxinput20_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput20_inname
  INTEGER id_id
  model_config_rec%auxinput20_inname = trim(auxinput20_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput20_inname
SUBROUTINE nl_set_auxinput20_outname ( id_id , auxinput20_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput20_outname
  INTEGER id_id
  model_config_rec%auxinput20_outname = trim(auxinput20_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput20_outname
SUBROUTINE nl_set_auxinput20_interval_y ( id_id , auxinput20_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_interval_y
  INTEGER id_id
  model_config_rec%auxinput20_interval_y(id_id) = auxinput20_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput20_interval_y
SUBROUTINE nl_set_auxinput20_interval_d ( id_id , auxinput20_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_interval_d
  INTEGER id_id
  model_config_rec%auxinput20_interval_d(id_id) = auxinput20_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput20_interval_d
SUBROUTINE nl_set_auxinput20_interval_h ( id_id , auxinput20_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_interval_h
  INTEGER id_id
  model_config_rec%auxinput20_interval_h(id_id) = auxinput20_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput20_interval_h
SUBROUTINE nl_set_auxinput20_interval_m ( id_id , auxinput20_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_interval_m
  INTEGER id_id
  model_config_rec%auxinput20_interval_m(id_id) = auxinput20_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput20_interval_m
SUBROUTINE nl_set_auxinput20_interval_s ( id_id , auxinput20_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_interval_s
  INTEGER id_id
  model_config_rec%auxinput20_interval_s(id_id) = auxinput20_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput20_interval_s
SUBROUTINE nl_set_auxinput20_interval ( id_id , auxinput20_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_interval
  INTEGER id_id
  model_config_rec%auxinput20_interval(id_id) = auxinput20_interval
  RETURN
END SUBROUTINE nl_set_auxinput20_interval
SUBROUTINE nl_set_auxinput20_begin_y ( id_id , auxinput20_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_begin_y
  INTEGER id_id
  model_config_rec%auxinput20_begin_y(id_id) = auxinput20_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput20_begin_y
SUBROUTINE nl_set_auxinput20_begin_d ( id_id , auxinput20_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_begin_d
  INTEGER id_id
  model_config_rec%auxinput20_begin_d(id_id) = auxinput20_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput20_begin_d
SUBROUTINE nl_set_auxinput20_begin_h ( id_id , auxinput20_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_begin_h
  INTEGER id_id
  model_config_rec%auxinput20_begin_h(id_id) = auxinput20_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput20_begin_h
SUBROUTINE nl_set_auxinput20_begin_m ( id_id , auxinput20_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_begin_m
  INTEGER id_id
  model_config_rec%auxinput20_begin_m(id_id) = auxinput20_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput20_begin_m
SUBROUTINE nl_set_auxinput20_begin_s ( id_id , auxinput20_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_begin_s
  INTEGER id_id
  model_config_rec%auxinput20_begin_s(id_id) = auxinput20_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput20_begin_s
SUBROUTINE nl_set_auxinput20_begin ( id_id , auxinput20_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_begin
  INTEGER id_id
  model_config_rec%auxinput20_begin(id_id) = auxinput20_begin
  RETURN
END SUBROUTINE nl_set_auxinput20_begin
SUBROUTINE nl_set_auxinput20_end_y ( id_id , auxinput20_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_end_y
  INTEGER id_id
  model_config_rec%auxinput20_end_y(id_id) = auxinput20_end_y
  RETURN
END SUBROUTINE nl_set_auxinput20_end_y
SUBROUTINE nl_set_auxinput20_end_d ( id_id , auxinput20_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_end_d
  INTEGER id_id
  model_config_rec%auxinput20_end_d(id_id) = auxinput20_end_d
  RETURN
END SUBROUTINE nl_set_auxinput20_end_d
SUBROUTINE nl_set_auxinput20_end_h ( id_id , auxinput20_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_end_h
  INTEGER id_id
  model_config_rec%auxinput20_end_h(id_id) = auxinput20_end_h
  RETURN
END SUBROUTINE nl_set_auxinput20_end_h
SUBROUTINE nl_set_auxinput20_end_m ( id_id , auxinput20_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_end_m
  INTEGER id_id
  model_config_rec%auxinput20_end_m(id_id) = auxinput20_end_m
  RETURN
END SUBROUTINE nl_set_auxinput20_end_m
SUBROUTINE nl_set_auxinput20_end_s ( id_id , auxinput20_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_end_s
  INTEGER id_id
  model_config_rec%auxinput20_end_s(id_id) = auxinput20_end_s
  RETURN
END SUBROUTINE nl_set_auxinput20_end_s
SUBROUTINE nl_set_auxinput20_end ( id_id , auxinput20_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput20_end
  INTEGER id_id
  model_config_rec%auxinput20_end(id_id) = auxinput20_end
  RETURN
END SUBROUTINE nl_set_auxinput20_end
SUBROUTINE nl_set_io_form_auxinput20 ( id_id , io_form_auxinput20 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput20
  INTEGER id_id
  model_config_rec%io_form_auxinput20 = io_form_auxinput20 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput20
SUBROUTINE nl_set_frames_per_auxinput20 ( id_id , frames_per_auxinput20 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput20
  INTEGER id_id
  model_config_rec%frames_per_auxinput20(id_id) = frames_per_auxinput20
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput20
SUBROUTINE nl_set_auxinput21_inname ( id_id , auxinput21_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput21_inname
  INTEGER id_id
  model_config_rec%auxinput21_inname = trim(auxinput21_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput21_inname
SUBROUTINE nl_set_auxinput21_outname ( id_id , auxinput21_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput21_outname
  INTEGER id_id
  model_config_rec%auxinput21_outname = trim(auxinput21_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput21_outname
SUBROUTINE nl_set_auxinput21_interval_y ( id_id , auxinput21_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_interval_y
  INTEGER id_id
  model_config_rec%auxinput21_interval_y(id_id) = auxinput21_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput21_interval_y
SUBROUTINE nl_set_auxinput21_interval_d ( id_id , auxinput21_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_interval_d
  INTEGER id_id
  model_config_rec%auxinput21_interval_d(id_id) = auxinput21_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput21_interval_d
SUBROUTINE nl_set_auxinput21_interval_h ( id_id , auxinput21_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_interval_h
  INTEGER id_id
  model_config_rec%auxinput21_interval_h(id_id) = auxinput21_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput21_interval_h
SUBROUTINE nl_set_auxinput21_interval_m ( id_id , auxinput21_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_interval_m
  INTEGER id_id
  model_config_rec%auxinput21_interval_m(id_id) = auxinput21_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput21_interval_m
SUBROUTINE nl_set_auxinput21_interval_s ( id_id , auxinput21_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_interval_s
  INTEGER id_id
  model_config_rec%auxinput21_interval_s(id_id) = auxinput21_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput21_interval_s
SUBROUTINE nl_set_auxinput21_interval ( id_id , auxinput21_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_interval
  INTEGER id_id
  model_config_rec%auxinput21_interval(id_id) = auxinput21_interval
  RETURN
END SUBROUTINE nl_set_auxinput21_interval
SUBROUTINE nl_set_auxinput21_begin_y ( id_id , auxinput21_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_begin_y
  INTEGER id_id
  model_config_rec%auxinput21_begin_y(id_id) = auxinput21_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput21_begin_y
SUBROUTINE nl_set_auxinput21_begin_d ( id_id , auxinput21_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_begin_d
  INTEGER id_id
  model_config_rec%auxinput21_begin_d(id_id) = auxinput21_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput21_begin_d
SUBROUTINE nl_set_auxinput21_begin_h ( id_id , auxinput21_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_begin_h
  INTEGER id_id
  model_config_rec%auxinput21_begin_h(id_id) = auxinput21_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput21_begin_h
SUBROUTINE nl_set_auxinput21_begin_m ( id_id , auxinput21_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_begin_m
  INTEGER id_id
  model_config_rec%auxinput21_begin_m(id_id) = auxinput21_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput21_begin_m
SUBROUTINE nl_set_auxinput21_begin_s ( id_id , auxinput21_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_begin_s
  INTEGER id_id
  model_config_rec%auxinput21_begin_s(id_id) = auxinput21_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput21_begin_s
SUBROUTINE nl_set_auxinput21_begin ( id_id , auxinput21_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_begin
  INTEGER id_id
  model_config_rec%auxinput21_begin(id_id) = auxinput21_begin
  RETURN
END SUBROUTINE nl_set_auxinput21_begin
SUBROUTINE nl_set_auxinput21_end_y ( id_id , auxinput21_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_end_y
  INTEGER id_id
  model_config_rec%auxinput21_end_y(id_id) = auxinput21_end_y
  RETURN
END SUBROUTINE nl_set_auxinput21_end_y
SUBROUTINE nl_set_auxinput21_end_d ( id_id , auxinput21_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_end_d
  INTEGER id_id
  model_config_rec%auxinput21_end_d(id_id) = auxinput21_end_d
  RETURN
END SUBROUTINE nl_set_auxinput21_end_d
SUBROUTINE nl_set_auxinput21_end_h ( id_id , auxinput21_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_end_h
  INTEGER id_id
  model_config_rec%auxinput21_end_h(id_id) = auxinput21_end_h
  RETURN
END SUBROUTINE nl_set_auxinput21_end_h
SUBROUTINE nl_set_auxinput21_end_m ( id_id , auxinput21_end_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_end_m
  INTEGER id_id
  model_config_rec%auxinput21_end_m(id_id) = auxinput21_end_m
  RETURN
END SUBROUTINE nl_set_auxinput21_end_m
SUBROUTINE nl_set_auxinput21_end_s ( id_id , auxinput21_end_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_end_s
  INTEGER id_id
  model_config_rec%auxinput21_end_s(id_id) = auxinput21_end_s
  RETURN
END SUBROUTINE nl_set_auxinput21_end_s
SUBROUTINE nl_set_auxinput21_end ( id_id , auxinput21_end )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput21_end
  INTEGER id_id
  model_config_rec%auxinput21_end(id_id) = auxinput21_end
  RETURN
END SUBROUTINE nl_set_auxinput21_end
SUBROUTINE nl_set_io_form_auxinput21 ( id_id , io_form_auxinput21 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: io_form_auxinput21
  INTEGER id_id
  model_config_rec%io_form_auxinput21 = io_form_auxinput21 
  RETURN
END SUBROUTINE nl_set_io_form_auxinput21
SUBROUTINE nl_set_frames_per_auxinput21 ( id_id , frames_per_auxinput21 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: frames_per_auxinput21
  INTEGER id_id
  model_config_rec%frames_per_auxinput21(id_id) = frames_per_auxinput21
  RETURN
END SUBROUTINE nl_set_frames_per_auxinput21
SUBROUTINE nl_set_auxinput22_inname ( id_id , auxinput22_inname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput22_inname
  INTEGER id_id
  model_config_rec%auxinput22_inname = trim(auxinput22_inname) 
  RETURN
END SUBROUTINE nl_set_auxinput22_inname
SUBROUTINE nl_set_auxinput22_outname ( id_id , auxinput22_outname )
  USE module_configure, ONLY : model_config_rec 
  character*256 , INTENT(IN) :: auxinput22_outname
  INTEGER id_id
  model_config_rec%auxinput22_outname = trim(auxinput22_outname) 
  RETURN
END SUBROUTINE nl_set_auxinput22_outname
SUBROUTINE nl_set_auxinput22_interval_y ( id_id , auxinput22_interval_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_interval_y
  INTEGER id_id
  model_config_rec%auxinput22_interval_y(id_id) = auxinput22_interval_y
  RETURN
END SUBROUTINE nl_set_auxinput22_interval_y
SUBROUTINE nl_set_auxinput22_interval_d ( id_id , auxinput22_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_interval_d
  INTEGER id_id
  model_config_rec%auxinput22_interval_d(id_id) = auxinput22_interval_d
  RETURN
END SUBROUTINE nl_set_auxinput22_interval_d
SUBROUTINE nl_set_auxinput22_interval_h ( id_id , auxinput22_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_interval_h
  INTEGER id_id
  model_config_rec%auxinput22_interval_h(id_id) = auxinput22_interval_h
  RETURN
END SUBROUTINE nl_set_auxinput22_interval_h
SUBROUTINE nl_set_auxinput22_interval_m ( id_id , auxinput22_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_interval_m
  INTEGER id_id
  model_config_rec%auxinput22_interval_m(id_id) = auxinput22_interval_m
  RETURN
END SUBROUTINE nl_set_auxinput22_interval_m
SUBROUTINE nl_set_auxinput22_interval_s ( id_id , auxinput22_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_interval_s
  INTEGER id_id
  model_config_rec%auxinput22_interval_s(id_id) = auxinput22_interval_s
  RETURN
END SUBROUTINE nl_set_auxinput22_interval_s
SUBROUTINE nl_set_auxinput22_interval ( id_id , auxinput22_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_interval
  INTEGER id_id
  model_config_rec%auxinput22_interval(id_id) = auxinput22_interval
  RETURN
END SUBROUTINE nl_set_auxinput22_interval
SUBROUTINE nl_set_auxinput22_begin_y ( id_id , auxinput22_begin_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_begin_y
  INTEGER id_id
  model_config_rec%auxinput22_begin_y(id_id) = auxinput22_begin_y
  RETURN
END SUBROUTINE nl_set_auxinput22_begin_y
SUBROUTINE nl_set_auxinput22_begin_d ( id_id , auxinput22_begin_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_begin_d
  INTEGER id_id
  model_config_rec%auxinput22_begin_d(id_id) = auxinput22_begin_d
  RETURN
END SUBROUTINE nl_set_auxinput22_begin_d
SUBROUTINE nl_set_auxinput22_begin_h ( id_id , auxinput22_begin_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_begin_h
  INTEGER id_id
  model_config_rec%auxinput22_begin_h(id_id) = auxinput22_begin_h
  RETURN
END SUBROUTINE nl_set_auxinput22_begin_h
SUBROUTINE nl_set_auxinput22_begin_m ( id_id , auxinput22_begin_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_begin_m
  INTEGER id_id
  model_config_rec%auxinput22_begin_m(id_id) = auxinput22_begin_m
  RETURN
END SUBROUTINE nl_set_auxinput22_begin_m
SUBROUTINE nl_set_auxinput22_begin_s ( id_id , auxinput22_begin_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_begin_s
  INTEGER id_id
  model_config_rec%auxinput22_begin_s(id_id) = auxinput22_begin_s
  RETURN
END SUBROUTINE nl_set_auxinput22_begin_s
SUBROUTINE nl_set_auxinput22_begin ( id_id , auxinput22_begin )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_begin
  INTEGER id_id
  model_config_rec%auxinput22_begin(id_id) = auxinput22_begin
  RETURN
END SUBROUTINE nl_set_auxinput22_begin
SUBROUTINE nl_set_auxinput22_end_y ( id_id , auxinput22_end_y )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_end_y
  INTEGER id_id
  model_config_rec%auxinput22_end_y(id_id) = auxinput22_end_y
  RETURN
END SUBROUTINE nl_set_auxinput22_end_y
SUBROUTINE nl_set_auxinput22_end_d ( id_id , auxinput22_end_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_end_d
  INTEGER id_id
  model_config_rec%auxinput22_end_d(id_id) = auxinput22_end_d
  RETURN
END SUBROUTINE nl_set_auxinput22_end_d
SUBROUTINE nl_set_auxinput22_end_h ( id_id , auxinput22_end_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: auxinput22_end_h
  INTEGER id_id
  model_config_rec%auxinput22_end_h(id_id) = auxinput22_end_h
  RETURN
END SUBROUTINE nl_set_auxinput22_end_h

!ENDOFREGISTRYGENERATEDINCLUDE


