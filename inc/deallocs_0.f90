





SUBROUTINE deallocs_0( grid )
  USE module_wrf_error
  USE module_domain_type
  IMPLICIT NONE
  TYPE( domain ), POINTER :: grid
  INTEGER :: ierr
IF ( ASSOCIATED( grid%xlat ) ) THEN 
  DEALLOCATE(grid%xlat,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",16,&
'frame/module_domain.f: Failed to deallocate grid%xlat. ')
 endif
  NULLIFY(grid%xlat)
ENDIF
IF ( ASSOCIATED( grid%traj_lat ) ) THEN 
  DEALLOCATE(grid%traj_lat,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",24,&
'frame/module_domain.f: Failed to deallocate grid%traj_lat. ')
 endif
  NULLIFY(grid%traj_lat)
ENDIF
IF ( ASSOCIATED( grid%lap_hgt ) ) THEN 
  DEALLOCATE(grid%lap_hgt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",32,&
'frame/module_domain.f: Failed to deallocate grid%lap_hgt. ')
 endif
  NULLIFY(grid%lap_hgt)
ENDIF
IF ( ASSOCIATED( grid%psfc_gc ) ) THEN 
  DEALLOCATE(grid%psfc_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",40,&
'frame/module_domain.f: Failed to deallocate grid%psfc_gc. ')
 endif
  NULLIFY(grid%psfc_gc)
ENDIF
IF ( ASSOCIATED( grid%qi_gc ) ) THEN 
  DEALLOCATE(grid%qi_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",48,&
'frame/module_domain.f: Failed to deallocate grid%qi_gc. ')
 endif
  NULLIFY(grid%qi_gc)
ENDIF
IF ( ASSOCIATED( grid%ght_max_p ) ) THEN 
  DEALLOCATE(grid%ght_max_p,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",56,&
'frame/module_domain.f: Failed to deallocate grid%ght_max_p. ')
 endif
  NULLIFY(grid%ght_max_p)
ENDIF
IF ( ASSOCIATED( grid%ttrop ) ) THEN 
  DEALLOCATE(grid%ttrop,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",64,&
'frame/module_domain.f: Failed to deallocate grid%ttrop. ')
 endif
  NULLIFY(grid%ttrop)
ENDIF
IF ( ASSOCIATED( grid%ru_m ) ) THEN 
  DEALLOCATE(grid%ru_m,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",72,&
'frame/module_domain.f: Failed to deallocate grid%ru_m. ')
 endif
  NULLIFY(grid%ru_m)
ENDIF
IF ( ASSOCIATED( grid%rv_m ) ) THEN 
  DEALLOCATE(grid%rv_m,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",80,&
'frame/module_domain.f: Failed to deallocate grid%rv_m. ')
 endif
  NULLIFY(grid%rv_m)
ENDIF
IF ( ASSOCIATED( grid%acoustic_grad_p ) ) THEN 
  DEALLOCATE(grid%acoustic_grad_p,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",88,&
'frame/module_domain.f: Failed to deallocate grid%acoustic_grad_p. ')
 endif
  NULLIFY(grid%acoustic_grad_p)
ENDIF
IF ( ASSOCIATED( grid%t_1 ) ) THEN 
  DEALLOCATE(grid%t_1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",96,&
'frame/module_domain.f: Failed to deallocate grid%t_1. ')
 endif
  NULLIFY(grid%t_1)
ENDIF
IF ( ASSOCIATED( grid%t_2 ) ) THEN 
  DEALLOCATE(grid%t_2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",104,&
'frame/module_domain.f: Failed to deallocate grid%t_2. ')
 endif
  NULLIFY(grid%t_2)
ENDIF
IF ( ASSOCIATED( grid%qv_upstream_x ) ) THEN 
  DEALLOCATE(grid%qv_upstream_x,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",112,&
'frame/module_domain.f: Failed to deallocate grid%qv_upstream_x. ')
 endif
  NULLIFY(grid%qv_upstream_x)
ENDIF
IF ( ASSOCIATED( grid%v_upstream_x ) ) THEN 
  DEALLOCATE(grid%v_upstream_x,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",120,&
'frame/module_domain.f: Failed to deallocate grid%v_upstream_x. ')
 endif
  NULLIFY(grid%v_upstream_x)
ENDIF
IF ( ASSOCIATED( grid%u_largescale ) ) THEN 
  DEALLOCATE(grid%u_largescale,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",128,&
'frame/module_domain.f: Failed to deallocate grid%u_largescale. ')
 endif
  NULLIFY(grid%u_largescale)
ENDIF
IF ( ASSOCIATED( grid%q_soil_forcing_val ) ) THEN 
  DEALLOCATE(grid%q_soil_forcing_val,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",136,&
'frame/module_domain.f: Failed to deallocate grid%q_soil_forcing_val. ')
 endif
  NULLIFY(grid%q_soil_forcing_val)
ENDIF
IF ( ASSOCIATED( grid%mu_btxs ) ) THEN 
  DEALLOCATE(grid%mu_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",144,&
'frame/module_domain.f: Failed to deallocate grid%mu_btxs. ')
 endif
  NULLIFY(grid%mu_btxs)
ENDIF
IF ( ASSOCIATED( grid%mu_btxe ) ) THEN 
  DEALLOCATE(grid%mu_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",152,&
'frame/module_domain.f: Failed to deallocate grid%mu_btxe. ')
 endif
  NULLIFY(grid%mu_btxe)
ENDIF
IF ( ASSOCIATED( grid%mu_btys ) ) THEN 
  DEALLOCATE(grid%mu_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",160,&
'frame/module_domain.f: Failed to deallocate grid%mu_btys. ')
 endif
  NULLIFY(grid%mu_btys)
ENDIF
IF ( ASSOCIATED( grid%mu_btye ) ) THEN 
  DEALLOCATE(grid%mu_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",168,&
'frame/module_domain.f: Failed to deallocate grid%mu_btye. ')
 endif
  NULLIFY(grid%mu_btye)
ENDIF
IF ( ASSOCIATED( grid%gamu ) ) THEN 
  DEALLOCATE(grid%gamu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",176,&
'frame/module_domain.f: Failed to deallocate grid%gamu. ')
 endif
  NULLIFY(grid%gamu)
ENDIF
IF ( ASSOCIATED( grid%rdz ) ) THEN 
  DEALLOCATE(grid%rdz,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",184,&
'frame/module_domain.f: Failed to deallocate grid%rdz. ')
 endif
  NULLIFY(grid%rdz)
ENDIF
IF ( ASSOCIATED( grid%p_hyd_w ) ) THEN 
  DEALLOCATE(grid%p_hyd_w,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",192,&
'frame/module_domain.f: Failed to deallocate grid%p_hyd_w. ')
 endif
  NULLIFY(grid%p_hyd_w)
ENDIF
IF ( ASSOCIATED( grid%imask_nostag ) ) THEN 
  DEALLOCATE(grid%imask_nostag,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",200,&
'frame/module_domain.f: Failed to deallocate grid%imask_nostag. ')
 endif
  NULLIFY(grid%imask_nostag)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_bxs ) ) THEN 
  DEALLOCATE(grid%dfi_moist_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",208,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_bxs. ')
 endif
  NULLIFY(grid%dfi_moist_bxs)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_bxe ) ) THEN 
  DEALLOCATE(grid%dfi_moist_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",216,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_bxe. ')
 endif
  NULLIFY(grid%dfi_moist_bxe)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_bys ) ) THEN 
  DEALLOCATE(grid%dfi_moist_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",224,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_bys. ')
 endif
  NULLIFY(grid%dfi_moist_bys)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_bye ) ) THEN 
  DEALLOCATE(grid%dfi_moist_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",232,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_bye. ')
 endif
  NULLIFY(grid%dfi_moist_bye)
ENDIF
IF ( ASSOCIATED( grid%re_cloud_gsfc ) ) THEN 
  DEALLOCATE(grid%re_cloud_gsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",240,&
'frame/module_domain.f: Failed to deallocate grid%re_cloud_gsfc. ')
 endif
  NULLIFY(grid%re_cloud_gsfc)
ENDIF
IF ( ASSOCIATED( grid%dfi_re_snow_gsfc ) ) THEN 
  DEALLOCATE(grid%dfi_re_snow_gsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",248,&
'frame/module_domain.f: Failed to deallocate grid%dfi_re_snow_gsfc. ')
 endif
  NULLIFY(grid%dfi_re_snow_gsfc)
ENDIF
IF ( ASSOCIATED( grid%fcx ) ) THEN 
  DEALLOCATE(grid%fcx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",256,&
'frame/module_domain.f: Failed to deallocate grid%fcx. ')
 endif
  NULLIFY(grid%fcx)
ENDIF
IF ( ASSOCIATED( grid%sm028100 ) ) THEN 
  DEALLOCATE(grid%sm028100,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",264,&
'frame/module_domain.f: Failed to deallocate grid%sm028100. ')
 endif
  NULLIFY(grid%sm028100)
ENDIF
IF ( ASSOCIATED( grid%soilm005 ) ) THEN 
  DEALLOCATE(grid%soilm005,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",272,&
'frame/module_domain.f: Failed to deallocate grid%soilm005. ')
 endif
  NULLIFY(grid%soilm005)
ENDIF
IF ( ASSOCIATED( grid%soilw020 ) ) THEN 
  DEALLOCATE(grid%soilw020,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",280,&
'frame/module_domain.f: Failed to deallocate grid%soilw020. ')
 endif
  NULLIFY(grid%soilw020)
ENDIF
IF ( ASSOCIATED( grid%soilt040 ) ) THEN 
  DEALLOCATE(grid%soilt040,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",288,&
'frame/module_domain.f: Failed to deallocate grid%soilt040. ')
 endif
  NULLIFY(grid%soilt040)
ENDIF
IF ( ASSOCIATED( grid%toposoil ) ) THEN 
  DEALLOCATE(grid%toposoil,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",296,&
'frame/module_domain.f: Failed to deallocate grid%toposoil. ')
 endif
  NULLIFY(grid%toposoil)
ENDIF
IF ( ASSOCIATED( grid%ts_v ) ) THEN 
  DEALLOCATE(grid%ts_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",304,&
'frame/module_domain.f: Failed to deallocate grid%ts_v. ')
 endif
  NULLIFY(grid%ts_v)
ENDIF
IF ( ASSOCIATED( grid%ts_rainnc ) ) THEN 
  DEALLOCATE(grid%ts_rainnc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",312,&
'frame/module_domain.f: Failed to deallocate grid%ts_rainnc. ')
 endif
  NULLIFY(grid%ts_rainnc)
ENDIF
IF ( ASSOCIATED( grid%lp_urb2d ) ) THEN 
  DEALLOCATE(grid%lp_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",320,&
'frame/module_domain.f: Failed to deallocate grid%lp_urb2d. ')
 endif
  NULLIFY(grid%lp_urb2d)
ENDIF
IF ( ASSOCIATED( grid%sh2o ) ) THEN 
  DEALLOCATE(grid%sh2o,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",328,&
'frame/module_domain.f: Failed to deallocate grid%sh2o. ')
 endif
  NULLIFY(grid%sh2o)
ENDIF
IF ( ASSOCIATED( grid%qtiledrain ) ) THEN 
  DEALLOCATE(grid%qtiledrain,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",336,&
'frame/module_domain.f: Failed to deallocate grid%qtiledrain. ')
 endif
  NULLIFY(grid%qtiledrain)
ENDIF
IF ( ASSOCIATED( grid%acrunoff ) ) THEN 
  DEALLOCATE(grid%acrunoff,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",344,&
'frame/module_domain.f: Failed to deallocate grid%acrunoff. ')
 endif
  NULLIFY(grid%acrunoff)
ENDIF
IF ( ASSOCIATED( grid%lake_depth ) ) THEN 
  DEALLOCATE(grid%lake_depth,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",352,&
'frame/module_domain.f: Failed to deallocate grid%lake_depth. ')
 endif
  NULLIFY(grid%lake_depth)
ENDIF
IF ( ASSOCIATED( grid%dfi_php ) ) THEN 
  DEALLOCATE(grid%dfi_php,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",360,&
'frame/module_domain.f: Failed to deallocate grid%dfi_php. ')
 endif
  NULLIFY(grid%dfi_php)
ENDIF
IF ( ASSOCIATED( grid%dfi_tslb ) ) THEN 
  DEALLOCATE(grid%dfi_tslb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",368,&
'frame/module_domain.f: Failed to deallocate grid%dfi_tslb. ')
 endif
  NULLIFY(grid%dfi_tslb)
ENDIF
IF ( ASSOCIATED( grid%tc_urb2d ) ) THEN 
  DEALLOCATE(grid%tc_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",376,&
'frame/module_domain.f: Failed to deallocate grid%tc_urb2d. ')
 endif
  NULLIFY(grid%tc_urb2d)
ENDIF
IF ( ASSOCIATED( grid%flxhumb_urb2d ) ) THEN 
  DEALLOCATE(grid%flxhumb_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",384,&
'frame/module_domain.f: Failed to deallocate grid%flxhumb_urb2d. ')
 endif
  NULLIFY(grid%flxhumb_urb2d)
ENDIF
IF ( ASSOCIATED( grid%frc_urb2d ) ) THEN 
  DEALLOCATE(grid%frc_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",392,&
'frame/module_domain.f: Failed to deallocate grid%frc_urb2d. ')
 endif
  NULLIFY(grid%frc_urb2d)
ENDIF
IF ( ASSOCIATED( grid%sf_ac_urb3d ) ) THEN 
  DEALLOCATE(grid%sf_ac_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",400,&
'frame/module_domain.f: Failed to deallocate grid%sf_ac_urb3d. ')
 endif
  NULLIFY(grid%sf_ac_urb3d)
ENDIF
IF ( ASSOCIATED( grid%t_pv_urb3d ) ) THEN 
  DEALLOCATE(grid%t_pv_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",408,&
'frame/module_domain.f: Failed to deallocate grid%t_pv_urb3d. ')
 endif
  NULLIFY(grid%t_pv_urb3d)
ENDIF
IF ( ASSOCIATED( grid%lfg_urb3d ) ) THEN 
  DEALLOCATE(grid%lfg_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",416,&
'frame/module_domain.f: Failed to deallocate grid%lfg_urb3d. ')
 endif
  NULLIFY(grid%lfg_urb3d)
ENDIF
IF ( ASSOCIATED( grid%hrang ) ) THEN 
  DEALLOCATE(grid%hrang,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",424,&
'frame/module_domain.f: Failed to deallocate grid%hrang. ')
 endif
  NULLIFY(grid%hrang)
ENDIF
IF ( ASSOCIATED( grid%alswvisdir ) ) THEN 
  DEALLOCATE(grid%alswvisdir,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",432,&
'frame/module_domain.f: Failed to deallocate grid%alswvisdir. ')
 endif
  NULLIFY(grid%alswvisdir)
ENDIF
IF ( ASSOCIATED( grid%lai_px ) ) THEN 
  DEALLOCATE(grid%lai_px,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",440,&
'frame/module_domain.f: Failed to deallocate grid%lai_px. ')
 endif
  NULLIFY(grid%lai_px)
ENDIF
IF ( ASSOCIATED( grid%qz0 ) ) THEN 
  DEALLOCATE(grid%qz0,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",448,&
'frame/module_domain.f: Failed to deallocate grid%qz0. ')
 endif
  NULLIFY(grid%qz0)
ENDIF
IF ( ASSOCIATED( grid%pshltr ) ) THEN 
  DEALLOCATE(grid%pshltr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",456,&
'frame/module_domain.f: Failed to deallocate grid%pshltr. ')
 endif
  NULLIFY(grid%pshltr)
ENDIF
IF ( ASSOCIATED( grid%u_up ) ) THEN 
  DEALLOCATE(grid%u_up,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",464,&
'frame/module_domain.f: Failed to deallocate grid%u_up. ')
 endif
  NULLIFY(grid%u_up)
ENDIF
IF ( ASSOCIATED( grid%mf_temf ) ) THEN 
  DEALLOCATE(grid%mf_temf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",472,&
'frame/module_domain.f: Failed to deallocate grid%mf_temf. ')
 endif
  NULLIFY(grid%mf_temf)
ENDIF
IF ( ASSOCIATED( grid%qbuoy ) ) THEN 
  DEALLOCATE(grid%qbuoy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",480,&
'frame/module_domain.f: Failed to deallocate grid%qbuoy. ')
 endif
  NULLIFY(grid%qbuoy)
ENDIF
IF ( ASSOCIATED( grid%edmf_thl ) ) THEN 
  DEALLOCATE(grid%edmf_thl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",488,&
'frame/module_domain.f: Failed to deallocate grid%edmf_thl. ')
 endif
  NULLIFY(grid%edmf_thl)
ENDIF
IF ( ASSOCIATED( grid%dfgdp ) ) THEN 
  DEALLOCATE(grid%dfgdp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",496,&
'frame/module_domain.f: Failed to deallocate grid%dfgdp. ')
 endif
  NULLIFY(grid%dfgdp)
ENDIF
IF ( ASSOCIATED( grid%oa4 ) ) THEN 
  DEALLOCATE(grid%oa4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",504,&
'frame/module_domain.f: Failed to deallocate grid%oa4. ')
 endif
  NULLIFY(grid%oa4)
ENDIF
IF ( ASSOCIATED( grid%dtauy3d_fd ) ) THEN 
  DEALLOCATE(grid%dtauy3d_fd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",512,&
'frame/module_domain.f: Failed to deallocate grid%dtauy3d_fd. ')
 endif
  NULLIFY(grid%dtauy3d_fd)
ENDIF
IF ( ASSOCIATED( grid%oa2ls ) ) THEN 
  DEALLOCATE(grid%oa2ls,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",520,&
'frame/module_domain.f: Failed to deallocate grid%oa2ls. ')
 endif
  NULLIFY(grid%oa2ls)
ENDIF
IF ( ASSOCIATED( grid%oa4ss ) ) THEN 
  DEALLOCATE(grid%oa4ss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",528,&
'frame/module_domain.f: Failed to deallocate grid%oa4ss. ')
 endif
  NULLIFY(grid%oa4ss)
ENDIF
IF ( ASSOCIATED( grid%b_u_bep ) ) THEN 
  DEALLOCATE(grid%b_u_bep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",536,&
'frame/module_domain.f: Failed to deallocate grid%b_u_bep. ')
 endif
  NULLIFY(grid%b_u_bep)
ENDIF
IF ( ASSOCIATED( grid%tpe_pbl ) ) THEN 
  DEALLOCATE(grid%tpe_pbl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",544,&
'frame/module_domain.f: Failed to deallocate grid%tpe_pbl. ')
 endif
  NULLIFY(grid%tpe_pbl)
ENDIF
IF ( ASSOCIATED( grid%cuppt ) ) THEN 
  DEALLOCATE(grid%cuppt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",552,&
'frame/module_domain.f: Failed to deallocate grid%cuppt. ')
 endif
  NULLIFY(grid%cuppt)
ENDIF
IF ( ASSOCIATED( grid%o3_gfs_du ) ) THEN 
  DEALLOCATE(grid%o3_gfs_du,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",560,&
'frame/module_domain.f: Failed to deallocate grid%o3_gfs_du. ')
 endif
  NULLIFY(grid%o3_gfs_du)
ENDIF
IF ( ASSOCIATED( grid%aerosolc_1 ) ) THEN 
  DEALLOCATE(grid%aerosolc_1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",568,&
'frame/module_domain.f: Failed to deallocate grid%aerosolc_1. ')
 endif
  NULLIFY(grid%aerosolc_1)
ENDIF
IF ( ASSOCIATED( grid%aerosolc_2 ) ) THEN 
  DEALLOCATE(grid%aerosolc_2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",576,&
'frame/module_domain.f: Failed to deallocate grid%aerosolc_2. ')
 endif
  NULLIFY(grid%aerosolc_2)
ENDIF
IF ( ASSOCIATED( grid%om_lon ) ) THEN 
  DEALLOCATE(grid%om_lon,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",584,&
'frame/module_domain.f: Failed to deallocate grid%om_lon. ')
 endif
  NULLIFY(grid%om_lon)
ENDIF
IF ( ASSOCIATED( grid%tcloud_cup ) ) THEN 
  DEALLOCATE(grid%tcloud_cup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",592,&
'frame/module_domain.f: Failed to deallocate grid%tcloud_cup. ')
 endif
  NULLIFY(grid%tcloud_cup)
ENDIF
IF ( ASSOCIATED( grid%mfup_cup ) ) THEN 
  DEALLOCATE(grid%mfup_cup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",600,&
'frame/module_domain.f: Failed to deallocate grid%mfup_cup. ')
 endif
  NULLIFY(grid%mfup_cup)
ENDIF
IF ( ASSOCIATED( grid%qc_diabatic ) ) THEN 
  DEALLOCATE(grid%qc_diabatic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",608,&
'frame/module_domain.f: Failed to deallocate grid%qc_diabatic. ')
 endif
  NULLIFY(grid%qc_diabatic)
ENDIF
IF ( ASSOCIATED( grid%e ) ) THEN 
  DEALLOCATE(grid%e,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",616,&
'frame/module_domain.f: Failed to deallocate grid%e. ')
 endif
  NULLIFY(grid%e)
ENDIF
IF ( ASSOCIATED( grid%shadowmask ) ) THEN 
  DEALLOCATE(grid%shadowmask,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",624,&
'frame/module_domain.f: Failed to deallocate grid%shadowmask. ')
 endif
  NULLIFY(grid%shadowmask)
ENDIF
IF ( ASSOCIATED( grid%physd ) ) THEN 
  DEALLOCATE(grid%physd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",632,&
'frame/module_domain.f: Failed to deallocate grid%physd. ')
 endif
  NULLIFY(grid%physd)
ENDIF
IF ( ASSOCIATED( grid%precs3d ) ) THEN 
  DEALLOCATE(grid%precs3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",640,&
'frame/module_domain.f: Failed to deallocate grid%precs3d. ')
 endif
  NULLIFY(grid%precs3d)
ENDIF
IF ( ASSOCIATED( grid%cod2d_out ) ) THEN 
  DEALLOCATE(grid%cod2d_out,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",648,&
'frame/module_domain.f: Failed to deallocate grid%cod2d_out. ')
 endif
  NULLIFY(grid%cod2d_out)
ENDIF
IF ( ASSOCIATED( grid%rqinshten ) ) THEN 
  DEALLOCATE(grid%rqinshten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",656,&
'frame/module_domain.f: Failed to deallocate grid%rqinshten. ')
 endif
  NULLIFY(grid%rqinshten)
ENDIF
IF ( ASSOCIATED( grid%wub ) ) THEN 
  DEALLOCATE(grid%wub,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",664,&
'frame/module_domain.f: Failed to deallocate grid%wub. ')
 endif
  NULLIFY(grid%wub)
ENDIF
IF ( ASSOCIATED( grid%rucuten ) ) THEN 
  DEALLOCATE(grid%rucuten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",672,&
'frame/module_domain.f: Failed to deallocate grid%rucuten. ')
 endif
  NULLIFY(grid%rucuten)
ENDIF
IF ( ASSOCIATED( grid%qiconv ) ) THEN 
  DEALLOCATE(grid%qiconv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",680,&
'frame/module_domain.f: Failed to deallocate grid%qiconv. ')
 endif
  NULLIFY(grid%qiconv)
ENDIF
IF ( ASSOCIATED( grid%snownc ) ) THEN 
  DEALLOCATE(grid%snownc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",688,&
'frame/module_domain.f: Failed to deallocate grid%snownc. ')
 endif
  NULLIFY(grid%snownc)
ENDIF
IF ( ASSOCIATED( grid%rhopo3d ) ) THEN 
  DEALLOCATE(grid%rhopo3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",696,&
'frame/module_domain.f: Failed to deallocate grid%rhopo3d. ')
 endif
  NULLIFY(grid%rhopo3d)
ENDIF
IF ( ASSOCIATED( grid%itype_3 ) ) THEN 
  DEALLOCATE(grid%itype_3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",704,&
'frame/module_domain.f: Failed to deallocate grid%itype_3. ')
 endif
  NULLIFY(grid%itype_3)
ENDIF
IF ( ASSOCIATED( grid%timec_kf ) ) THEN 
  DEALLOCATE(grid%timec_kf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",712,&
'frame/module_domain.f: Failed to deallocate grid%timec_kf. ')
 endif
  NULLIFY(grid%timec_kf)
ENDIF
IF ( ASSOCIATED( grid%kbcon_shallow ) ) THEN 
  DEALLOCATE(grid%kbcon_shallow,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",720,&
'frame/module_domain.f: Failed to deallocate grid%kbcon_shallow. ')
 endif
  NULLIFY(grid%kbcon_shallow)
ENDIF
IF ( ASSOCIATED( grid%gd_cloud ) ) THEN 
  DEALLOCATE(grid%gd_cloud,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",728,&
'frame/module_domain.f: Failed to deallocate grid%gd_cloud. ')
 endif
  NULLIFY(grid%gd_cloud)
ENDIF
IF ( ASSOCIATED( grid%ni_cu ) ) THEN 
  DEALLOCATE(grid%ni_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",736,&
'frame/module_domain.f: Failed to deallocate grid%ni_cu. ')
 endif
  NULLIFY(grid%ni_cu)
ENDIF
IF ( ASSOCIATED( grid%ccn1_gs ) ) THEN 
  DEALLOCATE(grid%ccn1_gs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",744,&
'frame/module_domain.f: Failed to deallocate grid%ccn1_gs. ')
 endif
  NULLIFY(grid%ccn1_gs)
ENDIF
IF ( ASSOCIATED( grid%swdown ) ) THEN 
  DEALLOCATE(grid%swdown,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",752,&
'frame/module_domain.f: Failed to deallocate grid%swdown. ')
 endif
  NULLIFY(grid%swdown)
ENDIF
IF ( ASSOCIATED( grid%swddni2 ) ) THEN 
  DEALLOCATE(grid%swddni2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",760,&
'frame/module_domain.f: Failed to deallocate grid%swddni2. ')
 endif
  NULLIFY(grid%swddni2)
ENDIF
IF ( ASSOCIATED( grid%aod5502d ) ) THEN 
  DEALLOCATE(grid%aod5502d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",768,&
'frame/module_domain.f: Failed to deallocate grid%aod5502d. ')
 endif
  NULLIFY(grid%aod5502d)
ENDIF
IF ( ASSOCIATED( grid%t2std ) ) THEN 
  DEALLOCATE(grid%t2std,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",776,&
'frame/module_domain.f: Failed to deallocate grid%t2std. ')
 endif
  NULLIFY(grid%t2std)
ENDIF
IF ( ASSOCIATED( grid%skintempstd ) ) THEN 
  DEALLOCATE(grid%skintempstd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",784,&
'frame/module_domain.f: Failed to deallocate grid%skintempstd. ')
 endif
  NULLIFY(grid%skintempstd)
ENDIF
IF ( ASSOCIATED( grid%rainncvmax ) ) THEN 
  DEALLOCATE(grid%rainncvmax,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",792,&
'frame/module_domain.f: Failed to deallocate grid%rainncvmax. ')
 endif
  NULLIFY(grid%rainncvmax)
ENDIF
IF ( ASSOCIATED( grid%acswupb ) ) THEN 
  DEALLOCATE(grid%acswupb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",800,&
'frame/module_domain.f: Failed to deallocate grid%acswupb. ')
 endif
  NULLIFY(grid%acswupb)
ENDIF
IF ( ASSOCIATED( grid%i_acswupt ) ) THEN 
  DEALLOCATE(grid%i_acswupt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",808,&
'frame/module_domain.f: Failed to deallocate grid%i_acswupt. ')
 endif
  NULLIFY(grid%i_acswupt)
ENDIF
IF ( ASSOCIATED( grid%i_aclwupb ) ) THEN 
  DEALLOCATE(grid%i_aclwupb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",816,&
'frame/module_domain.f: Failed to deallocate grid%i_aclwupb. ')
 endif
  NULLIFY(grid%i_aclwupb)
ENDIF
IF ( ASSOCIATED( grid%swupbcln ) ) THEN 
  DEALLOCATE(grid%swupbcln,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",824,&
'frame/module_domain.f: Failed to deallocate grid%swupbcln. ')
 endif
  NULLIFY(grid%swupbcln)
ENDIF
IF ( ASSOCIATED( grid%lwupbcln ) ) THEN 
  DEALLOCATE(grid%lwupbcln,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",832,&
'frame/module_domain.f: Failed to deallocate grid%lwupbcln. ')
 endif
  NULLIFY(grid%lwupbcln)
ENDIF
IF ( ASSOCIATED( grid%clat ) ) THEN 
  DEALLOCATE(grid%clat,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",840,&
'frame/module_domain.f: Failed to deallocate grid%clat. ')
 endif
  NULLIFY(grid%clat)
ENDIF
IF ( ASSOCIATED( grid%rqcblten ) ) THEN 
  DEALLOCATE(grid%rqcblten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",848,&
'frame/module_domain.f: Failed to deallocate grid%rqcblten. ')
 endif
  NULLIFY(grid%rqcblten)
ENDIF
IF ( ASSOCIATED( grid%canwat_mosaic ) ) THEN 
  DEALLOCATE(grid%canwat_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",856,&
'frame/module_domain.f: Failed to deallocate grid%canwat_mosaic. ')
 endif
  NULLIFY(grid%canwat_mosaic)
ENDIF
IF ( ASSOCIATED( grid%hfx_mosaic ) ) THEN 
  DEALLOCATE(grid%hfx_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",864,&
'frame/module_domain.f: Failed to deallocate grid%hfx_mosaic. ')
 endif
  NULLIFY(grid%hfx_mosaic)
ENDIF
IF ( ASSOCIATED( grid%uc_urb2d_mosaic ) ) THEN 
  DEALLOCATE(grid%uc_urb2d_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",872,&
'frame/module_domain.f: Failed to deallocate grid%uc_urb2d_mosaic. ')
 endif
  NULLIFY(grid%uc_urb2d_mosaic)
ENDIF
IF ( ASSOCIATED( grid%tbpvs0_state ) ) THEN 
  DEALLOCATE(grid%tbpvs0_state,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",880,&
'frame/module_domain.f: Failed to deallocate grid%tbpvs0_state. ')
 endif
  NULLIFY(grid%tbpvs0_state)
ENDIF
IF ( ASSOCIATED( grid%cplmask ) ) THEN 
  DEALLOCATE(grid%cplmask,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",888,&
'frame/module_domain.f: Failed to deallocate grid%cplmask. ')
 endif
  NULLIFY(grid%cplmask)
ENDIF
IF ( ASSOCIATED( grid%capg ) ) THEN 
  DEALLOCATE(grid%capg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",896,&
'frame/module_domain.f: Failed to deallocate grid%capg. ')
 endif
  NULLIFY(grid%capg)
ENDIF
IF ( ASSOCIATED( grid%dfi_qvg ) ) THEN 
  DEALLOCATE(grid%dfi_qvg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",904,&
'frame/module_domain.f: Failed to deallocate grid%dfi_qvg. ')
 endif
  NULLIFY(grid%dfi_qvg)
ENDIF
IF ( ASSOCIATED( grid%sr ) ) THEN 
  DEALLOCATE(grid%sr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",912,&
'frame/module_domain.f: Failed to deallocate grid%sr. ')
 endif
  NULLIFY(grid%sr)
ENDIF
IF ( ASSOCIATED( grid%defor23 ) ) THEN 
  DEALLOCATE(grid%defor23,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",920,&
'frame/module_domain.f: Failed to deallocate grid%defor23. ')
 endif
  NULLIFY(grid%defor23)
ENDIF
IF ( ASSOCIATED( grid%rvndgdten ) ) THEN 
  DEALLOCATE(grid%rvndgdten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",928,&
'frame/module_domain.f: Failed to deallocate grid%rvndgdten. ')
 endif
  NULLIFY(grid%rvndgdten)
ENDIF
IF ( ASSOCIATED( grid%t2_ndg_new ) ) THEN 
  DEALLOCATE(grid%t2_ndg_new,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",936,&
'frame/module_domain.f: Failed to deallocate grid%t2_ndg_new. ')
 endif
  NULLIFY(grid%t2_ndg_new)
ENDIF
IF ( ASSOCIATED( grid%odis_ndg_old ) ) THEN 
  DEALLOCATE(grid%odis_ndg_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",944,&
'frame/module_domain.f: Failed to deallocate grid%odis_ndg_old. ')
 endif
  NULLIFY(grid%odis_ndg_old)
ENDIF
IF ( ASSOCIATED( grid%abstot ) ) THEN 
  DEALLOCATE(grid%abstot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",952,&
'frame/module_domain.f: Failed to deallocate grid%abstot. ')
 endif
  NULLIFY(grid%abstot)
ENDIF
IF ( ASSOCIATED( grid%w_mean ) ) THEN 
  DEALLOCATE(grid%w_mean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",960,&
'frame/module_domain.f: Failed to deallocate grid%w_mean. ')
 endif
  NULLIFY(grid%w_mean)
ENDIF
IF ( ASSOCIATED( grid%advh_t ) ) THEN 
  DEALLOCATE(grid%advh_t,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",968,&
'frame/module_domain.f: Failed to deallocate grid%advh_t. ')
 endif
  NULLIFY(grid%advh_t)
ENDIF
IF ( ASSOCIATED( grid%track_u ) ) THEN 
  DEALLOCATE(grid%track_u,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",976,&
'frame/module_domain.f: Failed to deallocate grid%track_u. ')
 endif
  NULLIFY(grid%track_u)
ENDIF
IF ( ASSOCIATED( grid%track_qvapor ) ) THEN 
  DEALLOCATE(grid%track_qvapor,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",984,&
'frame/module_domain.f: Failed to deallocate grid%track_qvapor. ')
 endif
  NULLIFY(grid%track_qvapor)
ENDIF
IF ( ASSOCIATED( grid%aqvshten ) ) THEN 
  DEALLOCATE(grid%aqvshten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",992,&
'frame/module_domain.f: Failed to deallocate grid%aqvshten. ')
 endif
  NULLIFY(grid%aqvshten)
ENDIF
IF ( ASSOCIATED( grid%hailcast_diam_std ) ) THEN 
  DEALLOCATE(grid%hailcast_diam_std,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1000,&
'frame/module_domain.f: Failed to deallocate grid%hailcast_diam_std. ')
 endif
  NULLIFY(grid%hailcast_diam_std)
ENDIF
IF ( ASSOCIATED( grid%u_xxx ) ) THEN 
  DEALLOCATE(grid%u_xxx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1008,&
'frame/module_domain.f: Failed to deallocate grid%u_xxx. ')
 endif
  NULLIFY(grid%u_xxx)
ENDIF
IF ( ASSOCIATED( grid%dif_analysis ) ) THEN 
  DEALLOCATE(grid%dif_analysis,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1016,&
'frame/module_domain.f: Failed to deallocate grid%dif_analysis. ')
 endif
  NULLIFY(grid%dif_analysis)
ENDIF
IF ( ASSOCIATED( grid%k1_v ) ) THEN 
  DEALLOCATE(grid%k1_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1024,&
'frame/module_domain.f: Failed to deallocate grid%k1_v. ')
 endif
  NULLIFY(grid%k1_v)
ENDIF
IF ( ASSOCIATED( grid%k1_mu ) ) THEN 
  DEALLOCATE(grid%k1_mu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1032,&
'frame/module_domain.f: Failed to deallocate grid%k1_mu. ')
 endif
  NULLIFY(grid%k1_mu)
ENDIF
IF ( ASSOCIATED( grid%k2_t ) ) THEN 
  DEALLOCATE(grid%k2_t,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1040,&
'frame/module_domain.f: Failed to deallocate grid%k2_t. ')
 endif
  NULLIFY(grid%k2_t)
ENDIF
IF ( ASSOCIATED( grid%k3_v ) ) THEN 
  DEALLOCATE(grid%k3_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1048,&
'frame/module_domain.f: Failed to deallocate grid%k3_v. ')
 endif
  NULLIFY(grid%k3_v)
ENDIF
IF ( ASSOCIATED( grid%k3_mu ) ) THEN 
  DEALLOCATE(grid%k3_mu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1056,&
'frame/module_domain.f: Failed to deallocate grid%k3_mu. ')
 endif
  NULLIFY(grid%k3_mu)
ENDIF
IF ( ASSOCIATED( grid%grnhfx ) ) THEN 
  DEALLOCATE(grid%grnhfx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1064,&
'frame/module_domain.f: Failed to deallocate grid%grnhfx. ')
 endif
  NULLIFY(grid%grnhfx)
ENDIF
IF ( ASSOCIATED( grid%lfn_s0 ) ) THEN 
  DEALLOCATE(grid%lfn_s0,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1072,&
'frame/module_domain.f: Failed to deallocate grid%lfn_s0. ')
 endif
  NULLIFY(grid%lfn_s0)
ENDIF
IF ( ASSOCIATED( grid%ros ) ) THEN 
  DEALLOCATE(grid%ros,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1080,&
'frame/module_domain.f: Failed to deallocate grid%ros. ')
 endif
  NULLIFY(grid%ros)
ENDIF
IF ( ASSOCIATED( grid%q2_old ) ) THEN 
  DEALLOCATE(grid%q2_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1088,&
'frame/module_domain.f: Failed to deallocate grid%q2_old. ')
 endif
  NULLIFY(grid%q2_old)
ENDIF
IF ( ASSOCIATED( grid%fs_landing_mask ) ) THEN 
  DEALLOCATE(grid%fs_landing_mask,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1096,&
'frame/module_domain.f: Failed to deallocate grid%fs_landing_mask. ')
 endif
  NULLIFY(grid%fs_landing_mask)
ENDIF
IF ( ASSOCIATED( grid%fs_p_effd ) ) THEN 
  DEALLOCATE(grid%fs_p_effd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1104,&
'frame/module_domain.f: Failed to deallocate grid%fs_p_effd. ')
 endif
  NULLIFY(grid%fs_p_effd)
ENDIF
IF ( ASSOCIATED( grid%avgflx_efd1 ) ) THEN 
  DEALLOCATE(grid%avgflx_efd1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1112,&
'frame/module_domain.f: Failed to deallocate grid%avgflx_efd1. ')
 endif
  NULLIFY(grid%avgflx_efd1)
ENDIF
IF ( ASSOCIATED( grid%field_pbl ) ) THEN 
  DEALLOCATE(grid%field_pbl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1120,&
'frame/module_domain.f: Failed to deallocate grid%field_pbl. ')
 endif
  NULLIFY(grid%field_pbl)
ENDIF
IF ( ASSOCIATED( grid%spstreamforcc ) ) THEN 
  DEALLOCATE(grid%spstreamforcc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1128,&
'frame/module_domain.f: Failed to deallocate grid%spstreamforcc. ')
 endif
  NULLIFY(grid%spstreamforcc)
ENDIF
IF ( ASSOCIATED( grid%spforcc3 ) ) THEN 
  DEALLOCATE(grid%spforcc3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1136,&
'frame/module_domain.f: Failed to deallocate grid%spforcc3. ')
 endif
  NULLIFY(grid%spforcc3)
ENDIF
IF ( ASSOCIATED( grid%vertampt ) ) THEN 
  DEALLOCATE(grid%vertampt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1144,&
'frame/module_domain.f: Failed to deallocate grid%vertampt. ')
 endif
  NULLIFY(grid%vertampt)
ENDIF
IF ( ASSOCIATED( grid%mult3d_vertstruc ) ) THEN 
  DEALLOCATE(grid%mult3d_vertstruc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1152,&
'frame/module_domain.f: Failed to deallocate grid%mult3d_vertstruc. ')
 endif
  NULLIFY(grid%mult3d_vertstruc)
ENDIF
IF ( ASSOCIATED( grid%tauresx2d ) ) THEN 
  DEALLOCATE(grid%tauresx2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1160,&
'frame/module_domain.f: Failed to deallocate grid%tauresx2d. ')
 endif
  NULLIFY(grid%tauresx2d)
ENDIF
IF ( ASSOCIATED( grid%zmdq ) ) THEN 
  DEALLOCATE(grid%zmdq,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1168,&
'frame/module_domain.f: Failed to deallocate grid%zmdq. ')
 endif
  NULLIFY(grid%zmdq)
ENDIF
IF ( ASSOCIATED( grid%cmfmcdzm ) ) THEN 
  DEALLOCATE(grid%cmfmcdzm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1176,&
'frame/module_domain.f: Failed to deallocate grid%cmfmcdzm. ')
 endif
  NULLIFY(grid%cmfmcdzm)
ENDIF
IF ( ASSOCIATED( grid%zmvpgd ) ) THEN 
  DEALLOCATE(grid%zmvpgd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1184,&
'frame/module_domain.f: Failed to deallocate grid%zmvpgd. ')
 endif
  NULLIFY(grid%zmvpgd)
ENDIF
IF ( ASSOCIATED( grid%md3d ) ) THEN 
  DEALLOCATE(grid%md3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1192,&
'frame/module_domain.f: Failed to deallocate grid%md3d. ')
 endif
  NULLIFY(grid%md3d)
ENDIF
IF ( ASSOCIATED( grid%cush ) ) THEN 
  DEALLOCATE(grid%cush,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1200,&
'frame/module_domain.f: Failed to deallocate grid%cush. ')
 endif
  NULLIFY(grid%cush)
ENDIF
IF ( ASSOCIATED( grid%qtten_cu ) ) THEN 
  DEALLOCATE(grid%qtten_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1208,&
'frame/module_domain.f: Failed to deallocate grid%qtten_cu. ')
 endif
  NULLIFY(grid%qtten_cu)
ENDIF
IF ( ASSOCIATED( grid%plcl_cu ) ) THEN 
  DEALLOCATE(grid%plcl_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1216,&
'frame/module_domain.f: Failed to deallocate grid%plcl_cu. ')
 endif
  NULLIFY(grid%plcl_cu)
ENDIF
IF ( ASSOCIATED( grid%tkeavg_cu ) ) THEN 
  DEALLOCATE(grid%tkeavg_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1224,&
'frame/module_domain.f: Failed to deallocate grid%tkeavg_cu. ')
 endif
  NULLIFY(grid%tkeavg_cu)
ENDIF
IF ( ASSOCIATED( grid%vu_cu ) ) THEN 
  DEALLOCATE(grid%vu_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1232,&
'frame/module_domain.f: Failed to deallocate grid%vu_cu. ')
 endif
  NULLIFY(grid%vu_cu)
ENDIF
IF ( ASSOCIATED( grid%fdr_cu ) ) THEN 
  DEALLOCATE(grid%fdr_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1240,&
'frame/module_domain.f: Failed to deallocate grid%fdr_cu. ')
 endif
  NULLIFY(grid%fdr_cu)
ENDIF
IF ( ASSOCIATED( grid%aquad_cu ) ) THEN 
  DEALLOCATE(grid%aquad_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1248,&
'frame/module_domain.f: Failed to deallocate grid%aquad_cu. ')
 endif
  NULLIFY(grid%aquad_cu)
ENDIF
IF ( ASSOCIATED( grid%exit_wu_cu ) ) THEN 
  DEALLOCATE(grid%exit_wu_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1256,&
'frame/module_domain.f: Failed to deallocate grid%exit_wu_cu. ')
 endif
  NULLIFY(grid%exit_wu_cu)
ENDIF
IF ( ASSOCIATED( grid%limit_rei_cu ) ) THEN 
  DEALLOCATE(grid%limit_rei_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1264,&
'frame/module_domain.f: Failed to deallocate grid%limit_rei_cu. ')
 endif
  NULLIFY(grid%limit_rei_cu)
ENDIF
IF ( ASSOCIATED( grid%wtp ) ) THEN 
  DEALLOCATE(grid%wtp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1272,&
'frame/module_domain.f: Failed to deallocate grid%wtp. ')
 endif
  NULLIFY(grid%wtp)
ENDIF
IF ( ASSOCIATED( grid%fsi240 ) ) THEN 
  DEALLOCATE(grid%fsi240,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1280,&
'frame/module_domain.f: Failed to deallocate grid%fsi240. ')
 endif
  NULLIFY(grid%fsi240)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq_s4 ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq_s4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1288,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq_s4. ')
 endif
  NULLIFY(grid%h2osoi_liq_s4)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice_s1 ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice_s1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1296,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice_s1. ')
 endif
  NULLIFY(grid%h2osoi_ice_s1)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice8 ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice8,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1304,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice8. ')
 endif
  NULLIFY(grid%h2osoi_ice8)
ENDIF
IF ( ASSOCIATED( grid%t_soisno5 ) ) THEN 
  DEALLOCATE(grid%t_soisno5,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1312,&
'frame/module_domain.f: Failed to deallocate grid%t_soisno5. ')
 endif
  NULLIFY(grid%t_soisno5)
ENDIF
IF ( ASSOCIATED( grid%snowrds2 ) ) THEN 
  DEALLOCATE(grid%snowrds2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1320,&
'frame/module_domain.f: Failed to deallocate grid%snowrds2. ')
 endif
  NULLIFY(grid%snowrds2)
ENDIF
IF ( ASSOCIATED( grid%t_lake9 ) ) THEN 
  DEALLOCATE(grid%t_lake9,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1328,&
'frame/module_domain.f: Failed to deallocate grid%t_lake9. ')
 endif
  NULLIFY(grid%t_lake9)
ENDIF
IF ( ASSOCIATED( grid%albedosubgrid ) ) THEN 
  DEALLOCATE(grid%albedosubgrid,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1336,&
'frame/module_domain.f: Failed to deallocate grid%albedosubgrid. ')
 endif
  NULLIFY(grid%albedosubgrid)
ENDIF
IF ( ASSOCIATED( grid%snowdp2d ) ) THEN 
  DEALLOCATE(grid%snowdp2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1344,&
'frame/module_domain.f: Failed to deallocate grid%snowdp2d. ')
 endif
  NULLIFY(grid%snowdp2d)
ENDIF
IF ( ASSOCIATED( grid%z3d ) ) THEN 
  DEALLOCATE(grid%z3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1352,&
'frame/module_domain.f: Failed to deallocate grid%z3d. ')
 endif
  NULLIFY(grid%z3d)
ENDIF
IF ( ASSOCIATED( grid%ssib_egi ) ) THEN 
  DEALLOCATE(grid%ssib_egi,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1360,&
'frame/module_domain.f: Failed to deallocate grid%ssib_egi. ')
 endif
  NULLIFY(grid%ssib_egi)
ENDIF
IF ( ASSOCIATED( grid%ssib_veg ) ) THEN 
  DEALLOCATE(grid%ssib_veg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1368,&
'frame/module_domain.f: Failed to deallocate grid%ssib_veg. ')
 endif
  NULLIFY(grid%ssib_veg)
ENDIF
IF ( ASSOCIATED( grid%cto1 ) ) THEN 
  DEALLOCATE(grid%cto1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1376,&
'frame/module_domain.f: Failed to deallocate grid%cto1. ')
 endif
  NULLIFY(grid%cto1)
ENDIF
IF ( ASSOCIATED( grid%cto2 ) ) THEN 
  DEALLOCATE(grid%cto2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1384,&
'frame/module_domain.f: Failed to deallocate grid%cto2. ')
 endif
  NULLIFY(grid%cto2)
ENDIF
IF ( ASSOCIATED( grid%cto3 ) ) THEN 
  DEALLOCATE(grid%cto3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1392,&
'frame/module_domain.f: Failed to deallocate grid%cto3. ')
 endif
  NULLIFY(grid%cto3)
ENDIF
IF ( ASSOCIATED( grid%cto4 ) ) THEN 
  DEALLOCATE(grid%cto4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1400,&
'frame/module_domain.f: Failed to deallocate grid%cto4. ')
 endif
  NULLIFY(grid%cto4)
ENDIF
IF ( ASSOCIATED( grid%tahxy ) ) THEN 
  DEALLOCATE(grid%tahxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1408,&
'frame/module_domain.f: Failed to deallocate grid%tahxy. ')
 endif
  NULLIFY(grid%tahxy)
ENDIF
IF ( ASSOCIATED( grid%tsnoxy ) ) THEN 
  DEALLOCATE(grid%tsnoxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1416,&
'frame/module_domain.f: Failed to deallocate grid%tsnoxy. ')
 endif
  NULLIFY(grid%tsnoxy)
ENDIF
IF ( ASSOCIATED( grid%t2mvxy ) ) THEN 
  DEALLOCATE(grid%t2mvxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1424,&
'frame/module_domain.f: Failed to deallocate grid%t2mvxy. ')
 endif
  NULLIFY(grid%t2mvxy)
ENDIF
IF ( ASSOCIATED( grid%ecanxy ) ) THEN 
  DEALLOCATE(grid%ecanxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1432,&
'frame/module_domain.f: Failed to deallocate grid%ecanxy. ')
 endif
  NULLIFY(grid%ecanxy)
ENDIF
IF ( ASSOCIATED( grid%wgapxy ) ) THEN 
  DEALLOCATE(grid%wgapxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1440,&
'frame/module_domain.f: Failed to deallocate grid%wgapxy. ')
 endif
  NULLIFY(grid%wgapxy)
ENDIF
IF ( ASSOCIATED( grid%irgxy ) ) THEN 
  DEALLOCATE(grid%irgxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1448,&
'frame/module_domain.f: Failed to deallocate grid%irgxy. ')
 endif
  NULLIFY(grid%irgxy)
ENDIF
IF ( ASSOCIATED( grid%rechxy ) ) THEN 
  DEALLOCATE(grid%rechxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1456,&
'frame/module_domain.f: Failed to deallocate grid%rechxy. ')
 endif
  NULLIFY(grid%rechxy)
ENDIF
IF ( ASSOCIATED( grid%pexpxy ) ) THEN 
  DEALLOCATE(grid%pexpxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1464,&
'frame/module_domain.f: Failed to deallocate grid%pexpxy. ')
 endif
  NULLIFY(grid%pexpxy)
ENDIF
IF ( ASSOCIATED( grid%qsnsubxy ) ) THEN 
  DEALLOCATE(grid%qsnsubxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1472,&
'frame/module_domain.f: Failed to deallocate grid%qsnsubxy. ')
 endif
  NULLIFY(grid%qsnsubxy)
ENDIF
IF ( ASSOCIATED( grid%pahgxy ) ) THEN 
  DEALLOCATE(grid%pahgxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1480,&
'frame/module_domain.f: Failed to deallocate grid%pahgxy. ')
 endif
  NULLIFY(grid%pahgxy)
ENDIF
IF ( ASSOCIATED( grid%acints ) ) THEN 
  DEALLOCATE(grid%acints,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1488,&
'frame/module_domain.f: Failed to deallocate grid%acints. ')
 endif
  NULLIFY(grid%acints)
ENDIF
IF ( ASSOCIATED( grid%acrunsb ) ) THEN 
  DEALLOCATE(grid%acrunsb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1496,&
'frame/module_domain.f: Failed to deallocate grid%acrunsb. ')
 endif
  NULLIFY(grid%acrunsb)
ENDIF
IF ( ASSOCIATED( grid%acmeltc ) ) THEN 
  DEALLOCATE(grid%acmeltc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1504,&
'frame/module_domain.f: Failed to deallocate grid%acmeltc. ')
 endif
  NULLIFY(grid%acmeltc)
ENDIF
IF ( ASSOCIATED( grid%acevb ) ) THEN 
  DEALLOCATE(grid%acevb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1512,&
'frame/module_domain.f: Failed to deallocate grid%acevb. ')
 endif
  NULLIFY(grid%acevb)
ENDIF
IF ( ASSOCIATED( grid%acevc ) ) THEN 
  DEALLOCATE(grid%acevc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1520,&
'frame/module_domain.f: Failed to deallocate grid%acevc. ')
 endif
  NULLIFY(grid%acevc)
ENDIF
IF ( ASSOCIATED( grid%soilenergy ) ) THEN 
  DEALLOCATE(grid%soilenergy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1528,&
'frame/module_domain.f: Failed to deallocate grid%soilenergy. ')
 endif
  NULLIFY(grid%soilenergy)
ENDIF
IF ( ASSOCIATED( grid%acc_edirxy ) ) THEN 
  DEALLOCATE(grid%acc_edirxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1536,&
'frame/module_domain.f: Failed to deallocate grid%acc_edirxy. ')
 endif
  NULLIFY(grid%acc_edirxy)
ENDIF
IF ( ASSOCIATED( grid%irfract ) ) THEN 
  DEALLOCATE(grid%irfract,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1544,&
'frame/module_domain.f: Failed to deallocate grid%irfract. ')
 endif
  NULLIFY(grid%irfract)
ENDIF
IF ( ASSOCIATED( grid%irfivol ) ) THEN 
  DEALLOCATE(grid%irfivol,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1552,&
'frame/module_domain.f: Failed to deallocate grid%irfivol. ')
 endif
  NULLIFY(grid%irfivol)
ENDIF
IF ( ASSOCIATED( grid%kext_ft_qip ) ) THEN 
  DEALLOCATE(grid%kext_ft_qip,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1560,&
'frame/module_domain.f: Failed to deallocate grid%kext_ft_qip. ')
 endif
  NULLIFY(grid%kext_ft_qip)
ENDIF
IF ( ASSOCIATED( grid%refd_com ) ) THEN 
  DEALLOCATE(grid%refd_com,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1568,&
'frame/module_domain.f: Failed to deallocate grid%refd_com. ')
 endif
  NULLIFY(grid%refd_com)
ENDIF
IF ( ASSOCIATED( grid%icing_lg ) ) THEN 
  DEALLOCATE(grid%icing_lg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1576,&
'frame/module_domain.f: Failed to deallocate grid%icing_lg. ')
 endif
  NULLIFY(grid%icing_lg)
ENDIF
IF ( ASSOCIATED( grid%afwa_llturbsvr ) ) THEN 
  DEALLOCATE(grid%afwa_llturbsvr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1584,&
'frame/module_domain.f: Failed to deallocate grid%afwa_llturbsvr. ')
 endif
  NULLIFY(grid%afwa_llturbsvr)
ENDIF
IF ( ASSOCIATED( grid%afwa_cloud_ceil ) ) THEN 
  DEALLOCATE(grid%afwa_cloud_ceil,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1592,&
'frame/module_domain.f: Failed to deallocate grid%afwa_cloud_ceil. ')
 endif
  NULLIFY(grid%afwa_cloud_ceil)
ENDIF
IF ( ASSOCIATED( grid%afwa_llws ) ) THEN 
  DEALLOCATE(grid%afwa_llws,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1600,&
'frame/module_domain.f: Failed to deallocate grid%afwa_llws. ')
 endif
  NULLIFY(grid%afwa_llws)
ENDIF
IF ( ASSOCIATED( grid%swdnb_mean ) ) THEN 
  DEALLOCATE(grid%swdnb_mean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1608,&
'frame/module_domain.f: Failed to deallocate grid%swdnb_mean. ')
 endif
  NULLIFY(grid%swdnb_mean)
ENDIF
IF ( ASSOCIATED( grid%t2_diurn ) ) THEN 
  DEALLOCATE(grid%t2_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1616,&
'frame/module_domain.f: Failed to deallocate grid%t2_diurn. ')
 endif
  NULLIFY(grid%t2_diurn)
ENDIF
IF ( ASSOCIATED( grid%swdnt_diurn ) ) THEN 
  DEALLOCATE(grid%swdnt_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1624,&
'frame/module_domain.f: Failed to deallocate grid%swdnt_diurn. ')
 endif
  NULLIFY(grid%swdnt_diurn)
ENDIF
IF ( ASSOCIATED( grid%hfx_dtmp ) ) THEN 
  DEALLOCATE(grid%hfx_dtmp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1632,&
'frame/module_domain.f: Failed to deallocate grid%hfx_dtmp. ')
 endif
  NULLIFY(grid%hfx_dtmp)
ENDIF
IF ( ASSOCIATED( grid%induc ) ) THEN 
  DEALLOCATE(grid%induc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1640,&
'frame/module_domain.f: Failed to deallocate grid%induc. ')
 endif
  NULLIFY(grid%induc)
ENDIF
IF ( ASSOCIATED( grid%flshn ) ) THEN 
  DEALLOCATE(grid%flshn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1648,&
'frame/module_domain.f: Failed to deallocate grid%flshn. ')
 endif
  NULLIFY(grid%flshn)
ENDIF
IF ( ASSOCIATED( grid%field_v_tend_perturb ) ) THEN 
  DEALLOCATE(grid%field_v_tend_perturb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1656,&
'frame/module_domain.f: Failed to deallocate grid%field_v_tend_perturb. ')
 endif
  NULLIFY(grid%field_v_tend_perturb)
ENDIF
IF ( ASSOCIATED( grid%pcb ) ) THEN 
  DEALLOCATE(grid%pcb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1664,&
'frame/module_domain.f: Failed to deallocate grid%pcb. ')
 endif
  NULLIFY(grid%pcb)
ENDIF
IF ( ASSOCIATED( grid%p_wif_now ) ) THEN 
  DEALLOCATE(grid%p_wif_now,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1672,&
'frame/module_domain.f: Failed to deallocate grid%p_wif_now. ')
 endif
  NULLIFY(grid%p_wif_now)
ENDIF
IF ( ASSOCIATED( grid%p_wif_dec ) ) THEN 
  DEALLOCATE(grid%p_wif_dec,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1680,&
'frame/module_domain.f: Failed to deallocate grid%p_wif_dec. ')
 endif
  NULLIFY(grid%p_wif_dec)
ENDIF
IF ( ASSOCIATED( grid%w_wif_nov ) ) THEN 
  DEALLOCATE(grid%w_wif_nov,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1688,&
'frame/module_domain.f: Failed to deallocate grid%w_wif_nov. ')
 endif
  NULLIFY(grid%w_wif_nov)
ENDIF
IF ( ASSOCIATED( grid%i_wif_oct ) ) THEN 
  DEALLOCATE(grid%i_wif_oct,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1696,&
'frame/module_domain.f: Failed to deallocate grid%i_wif_oct. ')
 endif
  NULLIFY(grid%i_wif_oct)
ENDIF
IF ( ASSOCIATED( grid%b_wif_sep ) ) THEN 
  DEALLOCATE(grid%b_wif_sep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1704,&
'frame/module_domain.f: Failed to deallocate grid%b_wif_sep. ')
 endif
  NULLIFY(grid%b_wif_sep)
ENDIF
IF ( ASSOCIATED( grid%dir ) ) THEN 
  DEALLOCATE(grid%dir,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1712,&
'frame/module_domain.f: Failed to deallocate grid%dir. ')
 endif
  NULLIFY(grid%dir)
ENDIF
IF ( ASSOCIATED( grid%iwp ) ) THEN 
  DEALLOCATE(grid%iwp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1720,&
'frame/module_domain.f: Failed to deallocate grid%iwp. ')
 endif
  NULLIFY(grid%iwp)
ENDIF
IF ( ASSOCIATED( grid%tau_qi ) ) THEN 
  DEALLOCATE(grid%tau_qi,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1728,&
'frame/module_domain.f: Failed to deallocate grid%tau_qi. ')
 endif
  NULLIFY(grid%tau_qi)
ENDIF
IF ( ASSOCIATED( grid%ts_wvp ) ) THEN 
  DEALLOCATE(grid%ts_wvp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1736,&
'frame/module_domain.f: Failed to deallocate grid%ts_wvp. ')
 endif
  NULLIFY(grid%ts_wvp)
ENDIF
IF ( ASSOCIATED( grid%ts_re_qi_tot ) ) THEN 
  DEALLOCATE(grid%ts_re_qi_tot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1744,&
'frame/module_domain.f: Failed to deallocate grid%ts_re_qi_tot. ')
 endif
  NULLIFY(grid%ts_re_qi_tot)
ENDIF
IF ( ASSOCIATED( grid%ts_ghi_accum ) ) THEN 
  DEALLOCATE(grid%ts_ghi_accum,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1752,&
'frame/module_domain.f: Failed to deallocate grid%ts_ghi_accum. ')
 endif
  NULLIFY(grid%ts_ghi_accum)
ENDIF
IF ( ASSOCIATED( grid%ght_pl ) ) THEN 
  DEALLOCATE(grid%ght_pl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1760,&
'frame/module_domain.f: Failed to deallocate grid%ght_pl. ')
 endif
  NULLIFY(grid%ght_pl)
ENDIF
IF ( ASSOCIATED( grid%v_zl ) ) THEN 
  DEALLOCATE(grid%v_zl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1768,&
'frame/module_domain.f: Failed to deallocate grid%v_zl. ')
 endif
  NULLIFY(grid%v_zl)
ENDIF
IF ( ASSOCIATED( grid%w_iau ) ) THEN 
  DEALLOCATE(grid%w_iau,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1776,&
'frame/module_domain.f: Failed to deallocate grid%w_iau. ')
 endif
  NULLIFY(grid%w_iau)
ENDIF
IF ( ASSOCIATED( grid%rthiauten ) ) THEN 
  DEALLOCATE(grid%rthiauten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1784,&
'frame/module_domain.f: Failed to deallocate grid%rthiauten. ')
 endif
  NULLIFY(grid%rthiauten)
ENDIF
IF ( ASSOCIATED( grid%sst ) ) THEN 
  DEALLOCATE(grid%sst,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1792,&
'frame/module_domain.f: Failed to deallocate grid%sst. ')
 endif
  NULLIFY(grid%sst)
ENDIF
END SUBROUTINE deallocs_0



