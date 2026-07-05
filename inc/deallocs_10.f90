





SUBROUTINE deallocs_10( grid )
  USE module_wrf_error
  USE module_domain_type
  IMPLICIT NONE
  TYPE( domain ), POINTER :: grid
  INTEGER :: ierr
IF ( ASSOCIATED( grid%traj_k ) ) THEN 
  DEALLOCATE(grid%traj_k,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",16,&
'frame/module_domain.f: Failed to deallocate grid%traj_k. ')
 endif
  NULLIFY(grid%traj_k)
ENDIF
IF ( ASSOCIATED( grid%ht_gc ) ) THEN 
  DEALLOCATE(grid%ht_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",24,&
'frame/module_domain.f: Failed to deallocate grid%ht_gc. ')
 endif
  NULLIFY(grid%ht_gc)
ENDIF
IF ( ASSOCIATED( grid%pd_gc ) ) THEN 
  DEALLOCATE(grid%pd_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",32,&
'frame/module_domain.f: Failed to deallocate grid%pd_gc. ')
 endif
  NULLIFY(grid%pd_gc)
ENDIF
IF ( ASSOCIATED( grid%qc_gc ) ) THEN 
  DEALLOCATE(grid%qc_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",40,&
'frame/module_domain.f: Failed to deallocate grid%qc_gc. ')
 endif
  NULLIFY(grid%qc_gc)
ENDIF
IF ( ASSOCIATED( grid%qntemp2 ) ) THEN 
  DEALLOCATE(grid%qntemp2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",48,&
'frame/module_domain.f: Failed to deallocate grid%qntemp2. ')
 endif
  NULLIFY(grid%qntemp2)
ENDIF
IF ( ASSOCIATED( grid%ptropnn ) ) THEN 
  DEALLOCATE(grid%ptropnn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",56,&
'frame/module_domain.f: Failed to deallocate grid%ptropnn. ')
 endif
  NULLIFY(grid%ptropnn)
ENDIF
IF ( ASSOCIATED( grid%u_btxs ) ) THEN 
  DEALLOCATE(grid%u_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",64,&
'frame/module_domain.f: Failed to deallocate grid%u_btxs. ')
 endif
  NULLIFY(grid%u_btxs)
ENDIF
IF ( ASSOCIATED( grid%u_btxe ) ) THEN 
  DEALLOCATE(grid%u_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",72,&
'frame/module_domain.f: Failed to deallocate grid%u_btxe. ')
 endif
  NULLIFY(grid%u_btxe)
ENDIF
IF ( ASSOCIATED( grid%u_btys ) ) THEN 
  DEALLOCATE(grid%u_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",80,&
'frame/module_domain.f: Failed to deallocate grid%u_btys. ')
 endif
  NULLIFY(grid%u_btys)
ENDIF
IF ( ASSOCIATED( grid%u_btye ) ) THEN 
  DEALLOCATE(grid%u_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",88,&
'frame/module_domain.f: Failed to deallocate grid%u_btye. ')
 endif
  NULLIFY(grid%u_btye)
ENDIF
IF ( ASSOCIATED( grid%v_btxs ) ) THEN 
  DEALLOCATE(grid%v_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",96,&
'frame/module_domain.f: Failed to deallocate grid%v_btxs. ')
 endif
  NULLIFY(grid%v_btxs)
ENDIF
IF ( ASSOCIATED( grid%v_btxe ) ) THEN 
  DEALLOCATE(grid%v_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",104,&
'frame/module_domain.f: Failed to deallocate grid%v_btxe. ')
 endif
  NULLIFY(grid%v_btxe)
ENDIF
IF ( ASSOCIATED( grid%v_btys ) ) THEN 
  DEALLOCATE(grid%v_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",112,&
'frame/module_domain.f: Failed to deallocate grid%v_btys. ')
 endif
  NULLIFY(grid%v_btys)
ENDIF
IF ( ASSOCIATED( grid%v_btye ) ) THEN 
  DEALLOCATE(grid%v_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",120,&
'frame/module_domain.f: Failed to deallocate grid%v_btye. ')
 endif
  NULLIFY(grid%v_btye)
ENDIF
IF ( ASSOCIATED( grid%rw ) ) THEN 
  DEALLOCATE(grid%rw,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",128,&
'frame/module_domain.f: Failed to deallocate grid%rw. ')
 endif
  NULLIFY(grid%rw)
ENDIF
IF ( ASSOCIATED( grid%acoustic_grad_v ) ) THEN 
  DEALLOCATE(grid%acoustic_grad_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",136,&
'frame/module_domain.f: Failed to deallocate grid%acoustic_grad_v. ')
 endif
  NULLIFY(grid%acoustic_grad_v)
ENDIF
IF ( ASSOCIATED( grid%th_upstream_y ) ) THEN 
  DEALLOCATE(grid%th_upstream_y,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",144,&
'frame/module_domain.f: Failed to deallocate grid%th_upstream_y. ')
 endif
  NULLIFY(grid%th_upstream_y)
ENDIF
IF ( ASSOCIATED( grid%u_upstream_y ) ) THEN 
  DEALLOCATE(grid%u_upstream_y,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",152,&
'frame/module_domain.f: Failed to deallocate grid%u_upstream_y. ')
 endif
  NULLIFY(grid%u_upstream_y)
ENDIF
IF ( ASSOCIATED( grid%ql_largescale ) ) THEN 
  DEALLOCATE(grid%ql_largescale,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",160,&
'frame/module_domain.f: Failed to deallocate grid%ql_largescale. ')
 endif
  NULLIFY(grid%ql_largescale)
ENDIF
IF ( ASSOCIATED( grid%t_soil_forcing_val ) ) THEN 
  DEALLOCATE(grid%t_soil_forcing_val,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",168,&
'frame/module_domain.f: Failed to deallocate grid%t_soil_forcing_val. ')
 endif
  NULLIFY(grid%t_soil_forcing_val)
ENDIF
IF ( ASSOCIATED( grid%mu_1 ) ) THEN 
  DEALLOCATE(grid%mu_1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",176,&
'frame/module_domain.f: Failed to deallocate grid%mu_1. ')
 endif
  NULLIFY(grid%mu_1)
ENDIF
IF ( ASSOCIATED( grid%mu_2 ) ) THEN 
  DEALLOCATE(grid%mu_2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",184,&
'frame/module_domain.f: Failed to deallocate grid%mu_2. ')
 endif
  NULLIFY(grid%mu_2)
ENDIF
IF ( ASSOCIATED( grid%mut ) ) THEN 
  DEALLOCATE(grid%mut,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",192,&
'frame/module_domain.f: Failed to deallocate grid%mut. ')
 endif
  NULLIFY(grid%mut)
ENDIF
IF ( ASSOCIATED( grid%zx ) ) THEN 
  DEALLOCATE(grid%zx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",200,&
'frame/module_domain.f: Failed to deallocate grid%zx. ')
 endif
  NULLIFY(grid%zx)
ENDIF
IF ( ASSOCIATED( grid%z_at_w ) ) THEN 
  DEALLOCATE(grid%z_at_w,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",208,&
'frame/module_domain.f: Failed to deallocate grid%z_at_w. ')
 endif
  NULLIFY(grid%z_at_w)
ENDIF
IF ( ASSOCIATED( grid%tratx ) ) THEN 
  DEALLOCATE(grid%tratx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",216,&
'frame/module_domain.f: Failed to deallocate grid%tratx. ')
 endif
  NULLIFY(grid%tratx)
ENDIF
IF ( ASSOCIATED( grid%moist_btxs ) ) THEN 
  DEALLOCATE(grid%moist_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",224,&
'frame/module_domain.f: Failed to deallocate grid%moist_btxs. ')
 endif
  NULLIFY(grid%moist_btxs)
ENDIF
IF ( ASSOCIATED( grid%moist_btxe ) ) THEN 
  DEALLOCATE(grid%moist_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",232,&
'frame/module_domain.f: Failed to deallocate grid%moist_btxe. ')
 endif
  NULLIFY(grid%moist_btxe)
ENDIF
IF ( ASSOCIATED( grid%moist_btys ) ) THEN 
  DEALLOCATE(grid%moist_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",240,&
'frame/module_domain.f: Failed to deallocate grid%moist_btys. ')
 endif
  NULLIFY(grid%moist_btys)
ENDIF
IF ( ASSOCIATED( grid%moist_btye ) ) THEN 
  DEALLOCATE(grid%moist_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",248,&
'frame/module_domain.f: Failed to deallocate grid%moist_btye. ')
 endif
  NULLIFY(grid%moist_btye)
ENDIF
IF ( ASSOCIATED( grid%re_ice ) ) THEN 
  DEALLOCATE(grid%re_ice,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",256,&
'frame/module_domain.f: Failed to deallocate grid%re_ice. ')
 endif
  NULLIFY(grid%re_ice)
ENDIF
IF ( ASSOCIATED( grid%dfi_re_rain_gsfc ) ) THEN 
  DEALLOCATE(grid%dfi_re_rain_gsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",264,&
'frame/module_domain.f: Failed to deallocate grid%dfi_re_rain_gsfc. ')
 endif
  NULLIFY(grid%dfi_re_rain_gsfc)
ENDIF
IF ( ASSOCIATED( grid%dfi_scalar_bxs ) ) THEN 
  DEALLOCATE(grid%dfi_scalar_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",272,&
'frame/module_domain.f: Failed to deallocate grid%dfi_scalar_bxs. ')
 endif
  NULLIFY(grid%dfi_scalar_bxs)
ENDIF
IF ( ASSOCIATED( grid%dfi_scalar_bxe ) ) THEN 
  DEALLOCATE(grid%dfi_scalar_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",280,&
'frame/module_domain.f: Failed to deallocate grid%dfi_scalar_bxe. ')
 endif
  NULLIFY(grid%dfi_scalar_bxe)
ENDIF
IF ( ASSOCIATED( grid%dfi_scalar_bys ) ) THEN 
  DEALLOCATE(grid%dfi_scalar_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",288,&
'frame/module_domain.f: Failed to deallocate grid%dfi_scalar_bys. ')
 endif
  NULLIFY(grid%dfi_scalar_bys)
ENDIF
IF ( ASSOCIATED( grid%dfi_scalar_bye ) ) THEN 
  DEALLOCATE(grid%dfi_scalar_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",296,&
'frame/module_domain.f: Failed to deallocate grid%dfi_scalar_bye. ')
 endif
  NULLIFY(grid%dfi_scalar_bye)
ENDIF
IF ( ASSOCIATED( grid%sm000007 ) ) THEN 
  DEALLOCATE(grid%sm000007,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",304,&
'frame/module_domain.f: Failed to deallocate grid%sm000007. ')
 endif
  NULLIFY(grid%sm000007)
ENDIF
IF ( ASSOCIATED( grid%sm010200 ) ) THEN 
  DEALLOCATE(grid%sm010200,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",312,&
'frame/module_domain.f: Failed to deallocate grid%sm010200. ')
 endif
  NULLIFY(grid%sm010200)
ENDIF
IF ( ASSOCIATED( grid%soilw000 ) ) THEN 
  DEALLOCATE(grid%soilw000,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",320,&
'frame/module_domain.f: Failed to deallocate grid%soilw000. ')
 endif
  NULLIFY(grid%soilw000)
ENDIF
IF ( ASSOCIATED( grid%soilt005 ) ) THEN 
  DEALLOCATE(grid%soilt005,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",328,&
'frame/module_domain.f: Failed to deallocate grid%soilt005. ')
 endif
  NULLIFY(grid%soilt005)
ENDIF
IF ( ASSOCIATED( grid%shdavg ) ) THEN 
  DEALLOCATE(grid%shdavg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",336,&
'frame/module_domain.f: Failed to deallocate grid%shdavg. ')
 endif
  NULLIFY(grid%shdavg)
ENDIF
IF ( ASSOCIATED( grid%ts_hour ) ) THEN 
  DEALLOCATE(grid%ts_hour,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",344,&
'frame/module_domain.f: Failed to deallocate grid%ts_hour. ')
 endif
  NULLIFY(grid%ts_hour)
ENDIF
IF ( ASSOCIATED( grid%ts_clw ) ) THEN 
  DEALLOCATE(grid%ts_clw,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",352,&
'frame/module_domain.f: Failed to deallocate grid%ts_clw. ')
 endif
  NULLIFY(grid%ts_clw)
ENDIF
IF ( ASSOCIATED( grid%dzg ) ) THEN 
  DEALLOCATE(grid%dzg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",360,&
'frame/module_domain.f: Failed to deallocate grid%dzg. ')
 endif
  NULLIFY(grid%dzg)
ENDIF
IF ( ASSOCIATED( grid%ahe ) ) THEN 
  DEALLOCATE(grid%ahe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",368,&
'frame/module_domain.f: Failed to deallocate grid%ahe. ')
 endif
  NULLIFY(grid%ahe)
ENDIF
IF ( ASSOCIATED( grid%sfcheadrt ) ) THEN 
  DEALLOCATE(grid%sfcheadrt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",376,&
'frame/module_domain.f: Failed to deallocate grid%sfcheadrt. ')
 endif
  NULLIFY(grid%sfcheadrt)
ENDIF
IF ( ASSOCIATED( grid%sfcexc ) ) THEN 
  DEALLOCATE(grid%sfcexc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",384,&
'frame/module_domain.f: Failed to deallocate grid%sfcexc. ')
 endif
  NULLIFY(grid%sfcexc)
ENDIF
IF ( ASSOCIATED( grid%xlaidyn ) ) THEN 
  DEALLOCATE(grid%xlaidyn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",392,&
'frame/module_domain.f: Failed to deallocate grid%xlaidyn. ')
 endif
  NULLIFY(grid%xlaidyn)
ENDIF
IF ( ASSOCIATED( grid%dfi_phb ) ) THEN 
  DEALLOCATE(grid%dfi_phb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",400,&
'frame/module_domain.f: Failed to deallocate grid%dfi_phb. ')
 endif
  NULLIFY(grid%dfi_phb)
ENDIF
IF ( ASSOCIATED( grid%dfi_tke ) ) THEN 
  DEALLOCATE(grid%dfi_tke,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",408,&
'frame/module_domain.f: Failed to deallocate grid%dfi_tke. ')
 endif
  NULLIFY(grid%dfi_tke)
ENDIF
IF ( ASSOCIATED( grid%tb_urb2d ) ) THEN 
  DEALLOCATE(grid%tb_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",416,&
'frame/module_domain.f: Failed to deallocate grid%tb_urb2d. ')
 endif
  NULLIFY(grid%tb_urb2d)
ENDIF
IF ( ASSOCIATED( grid%drelg_urb2d ) ) THEN 
  DEALLOCATE(grid%drelg_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",424,&
'frame/module_domain.f: Failed to deallocate grid%drelg_urb2d. ')
 endif
  NULLIFY(grid%drelg_urb2d)
ENDIF
IF ( ASSOCIATED( grid%rn_urb2d ) ) THEN 
  DEALLOCATE(grid%rn_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",432,&
'frame/module_domain.f: Failed to deallocate grid%rn_urb2d. ')
 endif
  NULLIFY(grid%rn_urb2d)
ENDIF
IF ( ASSOCIATED( grid%tglev_urb3d ) ) THEN 
  DEALLOCATE(grid%tglev_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",440,&
'frame/module_domain.f: Failed to deallocate grid%tglev_urb3d. ')
 endif
  NULLIFY(grid%tglev_urb3d)
ENDIF
IF ( ASSOCIATED( grid%sfg_urb3d ) ) THEN 
  DEALLOCATE(grid%sfg_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",448,&
'frame/module_domain.f: Failed to deallocate grid%sfg_urb3d. ')
 endif
  NULLIFY(grid%sfg_urb3d)
ENDIF
IF ( ASSOCIATED( grid%dg_urb3d ) ) THEN 
  DEALLOCATE(grid%dg_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",456,&
'frame/module_domain.f: Failed to deallocate grid%dg_urb3d. ')
 endif
  NULLIFY(grid%dg_urb3d)
ENDIF
IF ( ASSOCIATED( grid%swnirdir ) ) THEN 
  DEALLOCATE(grid%swnirdir,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",464,&
'frame/module_domain.f: Failed to deallocate grid%swnirdir. ')
 endif
  NULLIFY(grid%swnirdir)
ENDIF
IF ( ASSOCIATED( grid%imperv ) ) THEN 
  DEALLOCATE(grid%imperv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",472,&
'frame/module_domain.f: Failed to deallocate grid%imperv. ')
 endif
  NULLIFY(grid%imperv)
ENDIF
IF ( ASSOCIATED( grid%fh ) ) THEN 
  DEALLOCATE(grid%fh,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",480,&
'frame/module_domain.f: Failed to deallocate grid%fh. ')
 endif
  NULLIFY(grid%fh)
ENDIF
IF ( ASSOCIATED( grid%thz0 ) ) THEN 
  DEALLOCATE(grid%thz0,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",488,&
'frame/module_domain.f: Failed to deallocate grid%thz0. ')
 endif
  NULLIFY(grid%thz0)
ENDIF
IF ( ASSOCIATED( grid%tshltr ) ) THEN 
  DEALLOCATE(grid%tshltr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",496,&
'frame/module_domain.f: Failed to deallocate grid%tshltr. ')
 endif
  NULLIFY(grid%tshltr)
ENDIF
IF ( ASSOCIATED( grid%rt_up ) ) THEN 
  DEALLOCATE(grid%rt_up,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",504,&
'frame/module_domain.f: Failed to deallocate grid%rt_up. ')
 endif
  NULLIFY(grid%rt_up)
ENDIF
IF ( ASSOCIATED( grid%vw_temf ) ) THEN 
  DEALLOCATE(grid%vw_temf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",512,&
'frame/module_domain.f: Failed to deallocate grid%vw_temf. ')
 endif
  NULLIFY(grid%vw_temf)
ENDIF
IF ( ASSOCIATED( grid%qke ) ) THEN 
  DEALLOCATE(grid%qke,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",520,&
'frame/module_domain.f: Failed to deallocate grid%qke. ')
 endif
  NULLIFY(grid%qke)
ENDIF
IF ( ASSOCIATED( grid%edmf_a ) ) THEN 
  DEALLOCATE(grid%edmf_a,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",528,&
'frame/module_domain.f: Failed to deallocate grid%edmf_a. ')
 endif
  NULLIFY(grid%edmf_a)
ENDIF
IF ( ASSOCIATED( grid%ztop_plume ) ) THEN 
  DEALLOCATE(grid%ztop_plume,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",536,&
'frame/module_domain.f: Failed to deallocate grid%ztop_plume. ')
 endif
  NULLIFY(grid%ztop_plume)
ENDIF
IF ( ASSOCIATED( grid%oa2 ) ) THEN 
  DEALLOCATE(grid%oa2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",544,&
'frame/module_domain.f: Failed to deallocate grid%oa2. ')
 endif
  NULLIFY(grid%oa2)
ENDIF
IF ( ASSOCIATED( grid%dtauy3d_ss ) ) THEN 
  DEALLOCATE(grid%dtauy3d_ss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",552,&
'frame/module_domain.f: Failed to deallocate grid%dtauy3d_ss. ')
 endif
  NULLIFY(grid%dtauy3d_ss)
ENDIF
IF ( ASSOCIATED( grid%oc12dls ) ) THEN 
  DEALLOCATE(grid%oc12dls,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",560,&
'frame/module_domain.f: Failed to deallocate grid%oc12dls. ')
 endif
  NULLIFY(grid%oc12dls)
ENDIF
IF ( ASSOCIATED( grid%oa2ss ) ) THEN 
  DEALLOCATE(grid%oa2ss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",568,&
'frame/module_domain.f: Failed to deallocate grid%oa2ss. ')
 endif
  NULLIFY(grid%oa2ss)
ENDIF
IF ( ASSOCIATED( grid%a_q_bep ) ) THEN 
  DEALLOCATE(grid%a_q_bep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",576,&
'frame/module_domain.f: Failed to deallocate grid%a_q_bep. ')
 endif
  NULLIFY(grid%a_q_bep)
ENDIF
IF ( ASSOCIATED( grid%el_pbl ) ) THEN 
  DEALLOCATE(grid%el_pbl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",584,&
'frame/module_domain.f: Failed to deallocate grid%el_pbl. ')
 endif
  NULLIFY(grid%el_pbl)
ENDIF
IF ( ASSOCIATED( grid%cutop ) ) THEN 
  DEALLOCATE(grid%cutop,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",592,&
'frame/module_domain.f: Failed to deallocate grid%cutop. ')
 endif
  NULLIFY(grid%cutop)
ENDIF
IF ( ASSOCIATED( grid%ncfrcv ) ) THEN 
  DEALLOCATE(grid%ncfrcv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",600,&
'frame/module_domain.f: Failed to deallocate grid%ncfrcv. ')
 endif
  NULLIFY(grid%ncfrcv)
ENDIF
IF ( ASSOCIATED( grid%pin ) ) THEN 
  DEALLOCATE(grid%pin,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",608,&
'frame/module_domain.f: Failed to deallocate grid%pin. ')
 endif
  NULLIFY(grid%pin)
ENDIF
IF ( ASSOCIATED( grid%om_v ) ) THEN 
  DEALLOCATE(grid%om_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",616,&
'frame/module_domain.f: Failed to deallocate grid%om_v. ')
 endif
  NULLIFY(grid%om_v)
ENDIF
IF ( ASSOCIATED( grid%taucloud ) ) THEN 
  DEALLOCATE(grid%taucloud,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",624,&
'frame/module_domain.f: Failed to deallocate grid%taucloud. ')
 endif
  NULLIFY(grid%taucloud)
ENDIF
IF ( ASSOCIATED( grid%wact_cup ) ) THEN 
  DEALLOCATE(grid%wact_cup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",632,&
'frame/module_domain.f: Failed to deallocate grid%wact_cup. ')
 endif
  NULLIFY(grid%wact_cup)
ENDIF
IF ( ASSOCIATED( grid%h_diabatic ) ) THEN 
  DEALLOCATE(grid%h_diabatic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",640,&
'frame/module_domain.f: Failed to deallocate grid%h_diabatic. ')
 endif
  NULLIFY(grid%h_diabatic)
ENDIF
IF ( ASSOCIATED( grid%msfvy ) ) THEN 
  DEALLOCATE(grid%msfvy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",648,&
'frame/module_domain.f: Failed to deallocate grid%msfvy. ')
 endif
  NULLIFY(grid%msfvy)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_btxs ) ) THEN 
  DEALLOCATE(grid%ht_shad_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",656,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_btxs. ')
 endif
  NULLIFY(grid%ht_shad_btxs)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_btxe ) ) THEN 
  DEALLOCATE(grid%ht_shad_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",664,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_btxe. ')
 endif
  NULLIFY(grid%ht_shad_btxe)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_btys ) ) THEN 
  DEALLOCATE(grid%ht_shad_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",672,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_btys. ')
 endif
  NULLIFY(grid%ht_shad_btys)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_btye ) ) THEN 
  DEALLOCATE(grid%ht_shad_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",680,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_btye. ')
 endif
  NULLIFY(grid%ht_shad_btye)
ENDIF
IF ( ASSOCIATED( grid%physc ) ) THEN 
  DEALLOCATE(grid%physc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",688,&
'frame/module_domain.f: Failed to deallocate grid%physc. ')
 endif
  NULLIFY(grid%physc)
ENDIF
IF ( ASSOCIATED( grid%acphysf ) ) THEN 
  DEALLOCATE(grid%acphysf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",696,&
'frame/module_domain.f: Failed to deallocate grid%acphysf. ')
 endif
  NULLIFY(grid%acphysf)
ENDIF
IF ( ASSOCIATED( grid%sswdn ) ) THEN 
  DEALLOCATE(grid%sswdn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",704,&
'frame/module_domain.f: Failed to deallocate grid%sswdn. ')
 endif
  NULLIFY(grid%sswdn)
ENDIF
IF ( ASSOCIATED( grid%rqgshten ) ) THEN 
  DEALLOCATE(grid%rqgshten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",712,&
'frame/module_domain.f: Failed to deallocate grid%rqgshten. ')
 endif
  NULLIFY(grid%rqgshten)
ENDIF
IF ( ASSOCIATED( grid%cldtopb ) ) THEN 
  DEALLOCATE(grid%cldtopb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",720,&
'frame/module_domain.f: Failed to deallocate grid%cldtopb. ')
 endif
  NULLIFY(grid%cldtopb)
ENDIF
IF ( ASSOCIATED( grid%tkeavg ) ) THEN 
  DEALLOCATE(grid%tkeavg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",728,&
'frame/module_domain.f: Failed to deallocate grid%tkeavg. ')
 endif
  NULLIFY(grid%tkeavg)
ENDIF
IF ( ASSOCIATED( grid%w0avg ) ) THEN 
  DEALLOCATE(grid%w0avg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",736,&
'frame/module_domain.f: Failed to deallocate grid%w0avg. ')
 endif
  NULLIFY(grid%w0avg)
ENDIF
IF ( ASSOCIATED( grid%rainncv ) ) THEN 
  DEALLOCATE(grid%rainncv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",744,&
'frame/module_domain.f: Failed to deallocate grid%rainncv. ')
 endif
  NULLIFY(grid%rainncv)
ENDIF
IF ( ASSOCIATED( grid%vmi3d ) ) THEN 
  DEALLOCATE(grid%vmi3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",752,&
'frame/module_domain.f: Failed to deallocate grid%vmi3d. ')
 endif
  NULLIFY(grid%vmi3d)
ENDIF
IF ( ASSOCIATED( grid%itype ) ) THEN 
  DEALLOCATE(grid%itype,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",760,&
'frame/module_domain.f: Failed to deallocate grid%itype. ')
 endif
  NULLIFY(grid%itype)
ENDIF
IF ( ASSOCIATED( grid%uer_kf ) ) THEN 
  DEALLOCATE(grid%uer_kf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",768,&
'frame/module_domain.f: Failed to deallocate grid%uer_kf. ')
 endif
  NULLIFY(grid%uer_kf)
ENDIF
IF ( ASSOCIATED( grid%xmb_shallow ) ) THEN 
  DEALLOCATE(grid%xmb_shallow,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",776,&
'frame/module_domain.f: Failed to deallocate grid%xmb_shallow. ')
 endif
  NULLIFY(grid%xmb_shallow)
ENDIF
IF ( ASSOCIATED( grid%cugd_qvtens ) ) THEN 
  DEALLOCATE(grid%cugd_qvtens,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",784,&
'frame/module_domain.f: Failed to deallocate grid%cugd_qvtens. ')
 endif
  NULLIFY(grid%cugd_qvtens)
ENDIF
IF ( ASSOCIATED( grid%qs_cu ) ) THEN 
  DEALLOCATE(grid%qs_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",792,&
'frame/module_domain.f: Failed to deallocate grid%qs_cu. ')
 endif
  NULLIFY(grid%qs_cu)
ENDIF
IF ( ASSOCIATED( grid%efss ) ) THEN 
  DEALLOCATE(grid%efss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",800,&
'frame/module_domain.f: Failed to deallocate grid%efss. ')
 endif
  NULLIFY(grid%efss)
ENDIF
IF ( ASSOCIATED( grid%rthften ) ) THEN 
  DEALLOCATE(grid%rthften,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",808,&
'frame/module_domain.f: Failed to deallocate grid%rthften. ')
 endif
  NULLIFY(grid%rthften)
ENDIF
IF ( ASSOCIATED( grid%cldfra_bl ) ) THEN 
  DEALLOCATE(grid%cldfra_bl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",816,&
'frame/module_domain.f: Failed to deallocate grid%cldfra_bl. ')
 endif
  NULLIFY(grid%cldfra_bl)
ENDIF
IF ( ASSOCIATED( grid%swddirc ) ) THEN 
  DEALLOCATE(grid%swddirc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",824,&
'frame/module_domain.f: Failed to deallocate grid%swddirc. ')
 endif
  NULLIFY(grid%swddirc)
ENDIF
IF ( ASSOCIATED( grid%swdown_ref ) ) THEN 
  DEALLOCATE(grid%swdown_ref,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",832,&
'frame/module_domain.f: Failed to deallocate grid%swdown_ref. ')
 endif
  NULLIFY(grid%swdown_ref)
ENDIF
IF ( ASSOCIATED( grid%tt2max ) ) THEN 
  DEALLOCATE(grid%tt2max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",840,&
'frame/module_domain.f: Failed to deallocate grid%tt2max. ')
 endif
  NULLIFY(grid%tt2max)
ENDIF
IF ( ASSOCIATED( grid%tskintempmax ) ) THEN 
  DEALLOCATE(grid%tskintempmax,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",848,&
'frame/module_domain.f: Failed to deallocate grid%tskintempmax. ')
 endif
  NULLIFY(grid%tskintempmax)
ENDIF
IF ( ASSOCIATED( grid%spduv10std ) ) THEN 
  DEALLOCATE(grid%spduv10std,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",856,&
'frame/module_domain.f: Failed to deallocate grid%spduv10std. ')
 endif
  NULLIFY(grid%spduv10std)
ENDIF
IF ( ASSOCIATED( grid%acswdnt ) ) THEN 
  DEALLOCATE(grid%acswdnt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",864,&
'frame/module_domain.f: Failed to deallocate grid%acswdnt. ')
 endif
  NULLIFY(grid%acswdnt)
ENDIF
IF ( ASSOCIATED( grid%aclwdnb ) ) THEN 
  DEALLOCATE(grid%aclwdnb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",872,&
'frame/module_domain.f: Failed to deallocate grid%aclwdnb. ')
 endif
  NULLIFY(grid%aclwdnb)
ENDIF
IF ( ASSOCIATED( grid%i_aclwdnt ) ) THEN 
  DEALLOCATE(grid%i_aclwdnt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",880,&
'frame/module_domain.f: Failed to deallocate grid%i_aclwdnt. ')
 endif
  NULLIFY(grid%i_aclwdnt)
ENDIF
IF ( ASSOCIATED( grid%swupb ) ) THEN 
  DEALLOCATE(grid%swupb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",888,&
'frame/module_domain.f: Failed to deallocate grid%swupb. ')
 endif
  NULLIFY(grid%swupb)
ENDIF
IF ( ASSOCIATED( grid%lwupb ) ) THEN 
  DEALLOCATE(grid%lwupb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",896,&
'frame/module_domain.f: Failed to deallocate grid%lwupb. ')
 endif
  NULLIFY(grid%lwupb)
ENDIF
IF ( ASSOCIATED( grid%xlong_v ) ) THEN 
  DEALLOCATE(grid%xlong_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",904,&
'frame/module_domain.f: Failed to deallocate grid%xlong_v. ')
 endif
  NULLIFY(grid%xlong_v)
ENDIF
IF ( ASSOCIATED( grid%rthblten ) ) THEN 
  DEALLOCATE(grid%rthblten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",912,&
'frame/module_domain.f: Failed to deallocate grid%rthblten. ')
 endif
  NULLIFY(grid%rthblten)
ENDIF
IF ( ASSOCIATED( grid%smois_mosaic ) ) THEN 
  DEALLOCATE(grid%smois_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",920,&
'frame/module_domain.f: Failed to deallocate grid%smois_mosaic. ')
 endif
  NULLIFY(grid%smois_mosaic)
ENDIF
IF ( ASSOCIATED( grid%lai_mosaic ) ) THEN 
  DEALLOCATE(grid%lai_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",928,&
'frame/module_domain.f: Failed to deallocate grid%lai_mosaic. ')
 endif
  NULLIFY(grid%lai_mosaic)
ENDIF
IF ( ASSOCIATED( grid%ts_rul2d_mosaic ) ) THEN 
  DEALLOCATE(grid%ts_rul2d_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",936,&
'frame/module_domain.f: Failed to deallocate grid%ts_rul2d_mosaic. ')
 endif
  NULLIFY(grid%ts_rul2d_mosaic)
ENDIF
IF ( ASSOCIATED( grid%mp_restart_state ) ) THEN 
  DEALLOCATE(grid%mp_restart_state,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",944,&
'frame/module_domain.f: Failed to deallocate grid%mp_restart_state. ')
 endif
  NULLIFY(grid%mp_restart_state)
ENDIF
IF ( ASSOCIATED( grid%u_phy ) ) THEN 
  DEALLOCATE(grid%u_phy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",952,&
'frame/module_domain.f: Failed to deallocate grid%u_phy. ')
 endif
  NULLIFY(grid%u_phy)
ENDIF
IF ( ASSOCIATED( grid%mol ) ) THEN 
  DEALLOCATE(grid%mol,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",960,&
'frame/module_domain.f: Failed to deallocate grid%mol. ')
 endif
  NULLIFY(grid%mol)
ENDIF
IF ( ASSOCIATED( grid%qsg ) ) THEN 
  DEALLOCATE(grid%qsg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",968,&
'frame/module_domain.f: Failed to deallocate grid%qsg. ')
 endif
  NULLIFY(grid%qsg)
ENDIF
IF ( ASSOCIATED( grid%mavail ) ) THEN 
  DEALLOCATE(grid%mavail,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",976,&
'frame/module_domain.f: Failed to deallocate grid%mavail. ')
 endif
  NULLIFY(grid%mavail)
ENDIF
IF ( ASSOCIATED( grid%defor33 ) ) THEN 
  DEALLOCATE(grid%defor33,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",984,&
'frame/module_domain.f: Failed to deallocate grid%defor33. ')
 endif
  NULLIFY(grid%defor33)
ENDIF
IF ( ASSOCIATED( grid%v10_ndg_new ) ) THEN 
  DEALLOCATE(grid%v10_ndg_new,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",992,&
'frame/module_domain.f: Failed to deallocate grid%v10_ndg_new. ')
 endif
  NULLIFY(grid%v10_ndg_new)
ENDIF
IF ( ASSOCIATED( grid%ps_ndg_new ) ) THEN 
  DEALLOCATE(grid%ps_ndg_new,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1000,&
'frame/module_domain.f: Failed to deallocate grid%ps_ndg_new. ')
 endif
  NULLIFY(grid%ps_ndg_new)
ENDIF
IF ( ASSOCIATED( grid%hfx_fdda ) ) THEN 
  DEALLOCATE(grid%hfx_fdda,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1008,&
'frame/module_domain.f: Failed to deallocate grid%hfx_fdda. ')
 endif
  NULLIFY(grid%hfx_fdda)
ENDIF
IF ( ASSOCIATED( grid%refd_max ) ) THEN 
  DEALLOCATE(grid%refd_max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1016,&
'frame/module_domain.f: Failed to deallocate grid%refd_max. ')
 endif
  NULLIFY(grid%refd_max)
ENDIF
IF ( ASSOCIATED( grid%prec_acc_nc ) ) THEN 
  DEALLOCATE(grid%prec_acc_nc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1024,&
'frame/module_domain.f: Failed to deallocate grid%prec_acc_nc. ')
 endif
  NULLIFY(grid%prec_acc_nc)
ENDIF
IF ( ASSOCIATED( grid%track_t ) ) THEN 
  DEALLOCATE(grid%track_t,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1032,&
'frame/module_domain.f: Failed to deallocate grid%track_t. ')
 endif
  NULLIFY(grid%track_t)
ENDIF
IF ( ASSOCIATED( grid%track_qsnow ) ) THEN 
  DEALLOCATE(grid%track_qsnow,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1040,&
'frame/module_domain.f: Failed to deallocate grid%track_qsnow. ')
 endif
  NULLIFY(grid%track_qsnow)
ENDIF
IF ( ASSOCIATED( grid%avcuten ) ) THEN 
  DEALLOCATE(grid%avcuten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1048,&
'frame/module_domain.f: Failed to deallocate grid%avcuten. ')
 endif
  NULLIFY(grid%avcuten)
ENDIF
IF ( ASSOCIATED( grid%hailcast_diam_max ) ) THEN 
  DEALLOCATE(grid%hailcast_diam_max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1056,&
'frame/module_domain.f: Failed to deallocate grid%hailcast_diam_max. ')
 endif
  NULLIFY(grid%hailcast_diam_max)
ENDIF
IF ( ASSOCIATED( grid%ht_xxx ) ) THEN 
  DEALLOCATE(grid%ht_xxx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1064,&
'frame/module_domain.f: Failed to deallocate grid%ht_xxx. ')
 endif
  NULLIFY(grid%ht_xxx)
ENDIF
IF ( ASSOCIATED( grid%k1_u_bxs ) ) THEN 
  DEALLOCATE(grid%k1_u_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1072,&
'frame/module_domain.f: Failed to deallocate grid%k1_u_bxs. ')
 endif
  NULLIFY(grid%k1_u_bxs)
ENDIF
IF ( ASSOCIATED( grid%k1_u_bxe ) ) THEN 
  DEALLOCATE(grid%k1_u_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1080,&
'frame/module_domain.f: Failed to deallocate grid%k1_u_bxe. ')
 endif
  NULLIFY(grid%k1_u_bxe)
ENDIF
IF ( ASSOCIATED( grid%k1_u_bys ) ) THEN 
  DEALLOCATE(grid%k1_u_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1088,&
'frame/module_domain.f: Failed to deallocate grid%k1_u_bys. ')
 endif
  NULLIFY(grid%k1_u_bys)
ENDIF
IF ( ASSOCIATED( grid%k1_u_bye ) ) THEN 
  DEALLOCATE(grid%k1_u_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1096,&
'frame/module_domain.f: Failed to deallocate grid%k1_u_bye. ')
 endif
  NULLIFY(grid%k1_u_bye)
ENDIF
IF ( ASSOCIATED( grid%k1_ph_bxs ) ) THEN 
  DEALLOCATE(grid%k1_ph_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1104,&
'frame/module_domain.f: Failed to deallocate grid%k1_ph_bxs. ')
 endif
  NULLIFY(grid%k1_ph_bxs)
ENDIF
IF ( ASSOCIATED( grid%k1_ph_bxe ) ) THEN 
  DEALLOCATE(grid%k1_ph_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1112,&
'frame/module_domain.f: Failed to deallocate grid%k1_ph_bxe. ')
 endif
  NULLIFY(grid%k1_ph_bxe)
ENDIF
IF ( ASSOCIATED( grid%k1_ph_bys ) ) THEN 
  DEALLOCATE(grid%k1_ph_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1120,&
'frame/module_domain.f: Failed to deallocate grid%k1_ph_bys. ')
 endif
  NULLIFY(grid%k1_ph_bys)
ENDIF
IF ( ASSOCIATED( grid%k1_ph_bye ) ) THEN 
  DEALLOCATE(grid%k1_ph_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1128,&
'frame/module_domain.f: Failed to deallocate grid%k1_ph_bye. ')
 endif
  NULLIFY(grid%k1_ph_bye)
ENDIF
IF ( ASSOCIATED( grid%k2_w_bxs ) ) THEN 
  DEALLOCATE(grid%k2_w_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1136,&
'frame/module_domain.f: Failed to deallocate grid%k2_w_bxs. ')
 endif
  NULLIFY(grid%k2_w_bxs)
ENDIF
IF ( ASSOCIATED( grid%k2_w_bxe ) ) THEN 
  DEALLOCATE(grid%k2_w_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1144,&
'frame/module_domain.f: Failed to deallocate grid%k2_w_bxe. ')
 endif
  NULLIFY(grid%k2_w_bxe)
ENDIF
IF ( ASSOCIATED( grid%k2_w_bys ) ) THEN 
  DEALLOCATE(grid%k2_w_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1152,&
'frame/module_domain.f: Failed to deallocate grid%k2_w_bys. ')
 endif
  NULLIFY(grid%k2_w_bys)
ENDIF
IF ( ASSOCIATED( grid%k2_w_bye ) ) THEN 
  DEALLOCATE(grid%k2_w_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1160,&
'frame/module_domain.f: Failed to deallocate grid%k2_w_bye. ')
 endif
  NULLIFY(grid%k2_w_bye)
ENDIF
IF ( ASSOCIATED( grid%k3_u_bxs ) ) THEN 
  DEALLOCATE(grid%k3_u_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1168,&
'frame/module_domain.f: Failed to deallocate grid%k3_u_bxs. ')
 endif
  NULLIFY(grid%k3_u_bxs)
ENDIF
IF ( ASSOCIATED( grid%k3_u_bxe ) ) THEN 
  DEALLOCATE(grid%k3_u_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1176,&
'frame/module_domain.f: Failed to deallocate grid%k3_u_bxe. ')
 endif
  NULLIFY(grid%k3_u_bxe)
ENDIF
IF ( ASSOCIATED( grid%k3_u_bys ) ) THEN 
  DEALLOCATE(grid%k3_u_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1184,&
'frame/module_domain.f: Failed to deallocate grid%k3_u_bys. ')
 endif
  NULLIFY(grid%k3_u_bys)
ENDIF
IF ( ASSOCIATED( grid%k3_u_bye ) ) THEN 
  DEALLOCATE(grid%k3_u_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1192,&
'frame/module_domain.f: Failed to deallocate grid%k3_u_bye. ')
 endif
  NULLIFY(grid%k3_u_bye)
ENDIF
IF ( ASSOCIATED( grid%k3_ph_bxs ) ) THEN 
  DEALLOCATE(grid%k3_ph_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1200,&
'frame/module_domain.f: Failed to deallocate grid%k3_ph_bxs. ')
 endif
  NULLIFY(grid%k3_ph_bxs)
ENDIF
IF ( ASSOCIATED( grid%k3_ph_bxe ) ) THEN 
  DEALLOCATE(grid%k3_ph_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1208,&
'frame/module_domain.f: Failed to deallocate grid%k3_ph_bxe. ')
 endif
  NULLIFY(grid%k3_ph_bxe)
ENDIF
IF ( ASSOCIATED( grid%k3_ph_bys ) ) THEN 
  DEALLOCATE(grid%k3_ph_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1216,&
'frame/module_domain.f: Failed to deallocate grid%k3_ph_bys. ')
 endif
  NULLIFY(grid%k3_ph_bys)
ENDIF
IF ( ASSOCIATED( grid%k3_ph_bye ) ) THEN 
  DEALLOCATE(grid%k3_ph_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1224,&
'frame/module_domain.f: Failed to deallocate grid%k3_ph_bye. ')
 endif
  NULLIFY(grid%k3_ph_bye)
ENDIF
IF ( ASSOCIATED( grid%rqvfrten ) ) THEN 
  DEALLOCATE(grid%rqvfrten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1232,&
'frame/module_domain.f: Failed to deallocate grid%rqvfrten. ')
 endif
  NULLIFY(grid%rqvfrten)
ENDIF
IF ( ASSOCIATED( grid%lfn_1 ) ) THEN 
  DEALLOCATE(grid%lfn_1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1240,&
'frame/module_domain.f: Failed to deallocate grid%lfn_1. ')
 endif
  NULLIFY(grid%lfn_1)
ENDIF
IF ( ASSOCIATED( grid%fcanhfx ) ) THEN 
  DEALLOCATE(grid%fcanhfx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1248,&
'frame/module_domain.f: Failed to deallocate grid%fcanhfx. ')
 endif
  NULLIFY(grid%fcanhfx)
ENDIF
IF ( ASSOCIATED( grid%rain_old ) ) THEN 
  DEALLOCATE(grid%rain_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1256,&
'frame/module_domain.f: Failed to deallocate grid%rain_old. ')
 endif
  NULLIFY(grid%rain_old)
ENDIF
IF ( ASSOCIATED( grid%fz0 ) ) THEN 
  DEALLOCATE(grid%fz0,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1264,&
'frame/module_domain.f: Failed to deallocate grid%fz0. ')
 endif
  NULLIFY(grid%fz0)
ENDIF
IF ( ASSOCIATED( grid%fs_count_landed_all ) ) THEN 
  DEALLOCATE(grid%fs_count_landed_all,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1272,&
'frame/module_domain.f: Failed to deallocate grid%fs_count_landed_all. ')
 endif
  NULLIFY(grid%fs_count_landed_all)
ENDIF
IF ( ASSOCIATED( grid%fs_p_mass ) ) THEN 
  DEALLOCATE(grid%fs_p_mass,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1280,&
'frame/module_domain.f: Failed to deallocate grid%fs_p_mass. ')
 endif
  NULLIFY(grid%fs_p_mass)
ENDIF
IF ( ASSOCIATED( grid%avgflx_efu1 ) ) THEN 
  DEALLOCATE(grid%avgflx_efu1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1288,&
'frame/module_domain.f: Failed to deallocate grid%avgflx_efu1. ')
 endif
  NULLIFY(grid%avgflx_efu1)
ENDIF
IF ( ASSOCIATED( grid%vertstrucs ) ) THEN 
  DEALLOCATE(grid%vertstrucs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1296,&
'frame/module_domain.f: Failed to deallocate grid%vertstrucs. ')
 endif
  NULLIFY(grid%vertstrucs)
ENDIF
IF ( ASSOCIATED( grid%rand_real ) ) THEN 
  DEALLOCATE(grid%rand_real,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1304,&
'frame/module_domain.f: Failed to deallocate grid%rand_real. ')
 endif
  NULLIFY(grid%rand_real)
ENDIF
IF ( ASSOCIATED( grid%spforcs2 ) ) THEN 
  DEALLOCATE(grid%spforcs2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1312,&
'frame/module_domain.f: Failed to deallocate grid%spforcs2. ')
 endif
  NULLIFY(grid%spforcs2)
ENDIF
IF ( ASSOCIATED( grid%spptforcs ) ) THEN 
  DEALLOCATE(grid%spptforcs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1320,&
'frame/module_domain.f: Failed to deallocate grid%spptforcs. ')
 endif
  NULLIFY(grid%spptforcs)
ENDIF
IF ( ASSOCIATED( grid%rand_imag_xxx ) ) THEN 
  DEALLOCATE(grid%rand_imag_xxx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1328,&
'frame/module_domain.f: Failed to deallocate grid%rand_imag_xxx. ')
 endif
  NULLIFY(grid%rand_imag_xxx)
ENDIF
IF ( ASSOCIATED( grid%lengthscale_mult3d ) ) THEN 
  DEALLOCATE(grid%lengthscale_mult3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1336,&
'frame/module_domain.f: Failed to deallocate grid%lengthscale_mult3d. ')
 endif
  NULLIFY(grid%lengthscale_mult3d)
ENDIF
IF ( ASSOCIATED( grid%precz ) ) THEN 
  DEALLOCATE(grid%precz,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1344,&
'frame/module_domain.f: Failed to deallocate grid%precz. ')
 endif
  NULLIFY(grid%precz)
ENDIF
IF ( ASSOCIATED( grid%zmntsnpd ) ) THEN 
  DEALLOCATE(grid%zmntsnpd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1352,&
'frame/module_domain.f: Failed to deallocate grid%zmntsnpd. ')
 endif
  NULLIFY(grid%zmntsnpd)
ENDIF
IF ( ASSOCIATED( grid%zmupgd ) ) THEN 
  DEALLOCATE(grid%zmupgd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1360,&
'frame/module_domain.f: Failed to deallocate grid%zmupgd. ')
 endif
  NULLIFY(grid%zmupgd)
ENDIF
IF ( ASSOCIATED( grid%ed3d ) ) THEN 
  DEALLOCATE(grid%ed3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1368,&
'frame/module_domain.f: Failed to deallocate grid%ed3d. ')
 endif
  NULLIFY(grid%ed3d)
ENDIF
IF ( ASSOCIATED( grid%cmfmc2 ) ) THEN 
  DEALLOCATE(grid%cmfmc2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1376,&
'frame/module_domain.f: Failed to deallocate grid%cmfmc2. ')
 endif
  NULLIFY(grid%cmfmc2)
ENDIF
IF ( ASSOCIATED( grid%uflx_cu ) ) THEN 
  DEALLOCATE(grid%uflx_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1384,&
'frame/module_domain.f: Failed to deallocate grid%uflx_cu. ')
 endif
  NULLIFY(grid%uflx_cu)
ENDIF
IF ( ASSOCIATED( grid%winvbase_cu ) ) THEN 
  DEALLOCATE(grid%winvbase_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1392,&
'frame/module_domain.f: Failed to deallocate grid%winvbase_cu. ')
 endif
  NULLIFY(grid%winvbase_cu)
ENDIF
IF ( ASSOCIATED( grid%cinlcl_cu ) ) THEN 
  DEALLOCATE(grid%cinlcl_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1400,&
'frame/module_domain.f: Failed to deallocate grid%cinlcl_cu. ')
 endif
  NULLIFY(grid%cinlcl_cu)
ENDIF
IF ( ASSOCIATED( grid%thvu_cu ) ) THEN 
  DEALLOCATE(grid%thvu_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1408,&
'frame/module_domain.f: Failed to deallocate grid%thvu_cu. ')
 endif
  NULLIFY(grid%thvu_cu)
ENDIF
IF ( ASSOCIATED( grid%cufrc_cu ) ) THEN 
  DEALLOCATE(grid%cufrc_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1416,&
'frame/module_domain.f: Failed to deallocate grid%cufrc_cu. ')
 endif
  NULLIFY(grid%cufrc_cu)
ENDIF
IF ( ASSOCIATED( grid%excessu0_cu ) ) THEN 
  DEALLOCATE(grid%excessu0_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1424,&
'frame/module_domain.f: Failed to deallocate grid%excessu0_cu. ')
 endif
  NULLIFY(grid%excessu0_cu)
ENDIF
IF ( ASSOCIATED( grid%exit_wtw_cu ) ) THEN 
  DEALLOCATE(grid%exit_wtw_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1432,&
'frame/module_domain.f: Failed to deallocate grid%exit_wtw_cu. ')
 endif
  NULLIFY(grid%exit_wtw_cu)
ENDIF
IF ( ASSOCIATED( grid%limit_cin_cu ) ) THEN 
  DEALLOCATE(grid%limit_cin_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1440,&
'frame/module_domain.f: Failed to deallocate grid%limit_cin_cu. ')
 endif
  NULLIFY(grid%limit_cin_cu)
ENDIF
IF ( ASSOCIATED( grid%cldfrai ) ) THEN 
  DEALLOCATE(grid%cldfrai,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1448,&
'frame/module_domain.f: Failed to deallocate grid%cldfrai. ')
 endif
  NULLIFY(grid%cldfrai)
ENDIF
IF ( ASSOCIATED( grid%snowdp ) ) THEN 
  DEALLOCATE(grid%snowdp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1456,&
'frame/module_domain.f: Failed to deallocate grid%snowdp. ')
 endif
  NULLIFY(grid%snowdp)
ENDIF
IF ( ASSOCIATED( grid%fsd240 ) ) THEN 
  DEALLOCATE(grid%fsd240,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1464,&
'frame/module_domain.f: Failed to deallocate grid%fsd240. ')
 endif
  NULLIFY(grid%fsd240)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq_s2 ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq_s2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1472,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq_s2. ')
 endif
  NULLIFY(grid%h2osoi_liq_s2)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq9 ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq9,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1480,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq9. ')
 endif
  NULLIFY(grid%h2osoi_liq9)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice6 ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice6,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1488,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice6. ')
 endif
  NULLIFY(grid%h2osoi_ice6)
ENDIF
IF ( ASSOCIATED( grid%t_soisno3 ) ) THEN 
  DEALLOCATE(grid%t_soisno3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1496,&
'frame/module_domain.f: Failed to deallocate grid%t_soisno3. ')
 endif
  NULLIFY(grid%t_soisno3)
ENDIF
IF ( ASSOCIATED( grid%dzsnow5 ) ) THEN 
  DEALLOCATE(grid%dzsnow5,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1504,&
'frame/module_domain.f: Failed to deallocate grid%dzsnow5. ')
 endif
  NULLIFY(grid%dzsnow5)
ENDIF
IF ( ASSOCIATED( grid%t_lake7 ) ) THEN 
  DEALLOCATE(grid%t_lake7,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1512,&
'frame/module_domain.f: Failed to deallocate grid%t_lake7. ')
 endif
  NULLIFY(grid%t_lake7)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_vol9 ) ) THEN 
  DEALLOCATE(grid%h2osoi_vol9,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1520,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_vol9. ')
 endif
  NULLIFY(grid%h2osoi_vol9)
ENDIF
IF ( ASSOCIATED( grid%lakedepth2d ) ) THEN 
  DEALLOCATE(grid%lakedepth2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1528,&
'frame/module_domain.f: Failed to deallocate grid%lakedepth2d. ')
 endif
  NULLIFY(grid%lakedepth2d)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq3d ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1536,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq3d. ')
 endif
  NULLIFY(grid%h2osoi_liq3d)
ENDIF
IF ( ASSOCIATED( grid%ssib_eci ) ) THEN 
  DEALLOCATE(grid%ssib_eci,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1544,&
'frame/module_domain.f: Failed to deallocate grid%ssib_eci. ')
 endif
  NULLIFY(grid%ssib_eci)
ENDIF
IF ( ASSOCIATED( grid%ssib_vcf ) ) THEN 
  DEALLOCATE(grid%ssib_vcf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1552,&
'frame/module_domain.f: Failed to deallocate grid%ssib_vcf. ')
 endif
  NULLIFY(grid%ssib_vcf)
ENDIF
IF ( ASSOCIATED( grid%bwo1 ) ) THEN 
  DEALLOCATE(grid%bwo1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1560,&
'frame/module_domain.f: Failed to deallocate grid%bwo1. ')
 endif
  NULLIFY(grid%bwo1)
ENDIF
IF ( ASSOCIATED( grid%bwo2 ) ) THEN 
  DEALLOCATE(grid%bwo2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1568,&
'frame/module_domain.f: Failed to deallocate grid%bwo2. ')
 endif
  NULLIFY(grid%bwo2)
ENDIF
IF ( ASSOCIATED( grid%bwo3 ) ) THEN 
  DEALLOCATE(grid%bwo3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1576,&
'frame/module_domain.f: Failed to deallocate grid%bwo3. ')
 endif
  NULLIFY(grid%bwo3)
ENDIF
IF ( ASSOCIATED( grid%bwo4 ) ) THEN 
  DEALLOCATE(grid%bwo4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1584,&
'frame/module_domain.f: Failed to deallocate grid%bwo4. ')
 endif
  NULLIFY(grid%bwo4)
ENDIF
IF ( ASSOCIATED( grid%canliqxy ) ) THEN 
  DEALLOCATE(grid%canliqxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1592,&
'frame/module_domain.f: Failed to deallocate grid%canliqxy. ')
 endif
  NULLIFY(grid%canliqxy)
ENDIF
IF ( ASSOCIATED( grid%waxy ) ) THEN 
  DEALLOCATE(grid%waxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1600,&
'frame/module_domain.f: Failed to deallocate grid%waxy. ')
 endif
  NULLIFY(grid%waxy)
ENDIF
IF ( ASSOCIATED( grid%xsaixy ) ) THEN 
  DEALLOCATE(grid%xsaixy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1608,&
'frame/module_domain.f: Failed to deallocate grid%xsaixy. ')
 endif
  NULLIFY(grid%xsaixy)
ENDIF
IF ( ASSOCIATED( grid%runsfxy ) ) THEN 
  DEALLOCATE(grid%runsfxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1616,&
'frame/module_domain.f: Failed to deallocate grid%runsfxy. ')
 endif
  NULLIFY(grid%runsfxy)
ENDIF
IF ( ASSOCIATED( grid%rsshaxy ) ) THEN 
  DEALLOCATE(grid%rsshaxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1624,&
'frame/module_domain.f: Failed to deallocate grid%rsshaxy. ')
 endif
  NULLIFY(grid%rsshaxy)
ENDIF
IF ( ASSOCIATED( grid%ghvxy ) ) THEN 
  DEALLOCATE(grid%ghvxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1632,&
'frame/module_domain.f: Failed to deallocate grid%ghvxy. ')
 endif
  NULLIFY(grid%ghvxy)
ENDIF
IF ( ASSOCIATED( grid%smoiseq ) ) THEN 
  DEALLOCATE(grid%smoiseq,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1640,&
'frame/module_domain.f: Failed to deallocate grid%smoiseq. ')
 endif
  NULLIFY(grid%smoiseq)
ENDIF
IF ( ASSOCIATED( grid%qlatxy ) ) THEN 
  DEALLOCATE(grid%qlatxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1648,&
'frame/module_domain.f: Failed to deallocate grid%qlatxy. ')
 endif
  NULLIFY(grid%qlatxy)
ENDIF
IF ( ASSOCIATED( grid%qthrosxy ) ) THEN 
  DEALLOCATE(grid%qthrosxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1656,&
'frame/module_domain.f: Failed to deallocate grid%qthrosxy. ')
 endif
  NULLIFY(grid%qthrosxy)
ENDIF
IF ( ASSOCIATED( grid%pondingxy ) ) THEN 
  DEALLOCATE(grid%pondingxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1664,&
'frame/module_domain.f: Failed to deallocate grid%pondingxy. ')
 endif
  NULLIFY(grid%pondingxy)
ENDIF
IF ( ASSOCIATED( grid%soilcl3 ) ) THEN 
  DEALLOCATE(grid%soilcl3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1672,&
'frame/module_domain.f: Failed to deallocate grid%soilcl3. ')
 endif
  NULLIFY(grid%soilcl3)
ENDIF
IF ( ASSOCIATED( grid%forcwlsm ) ) THEN 
  DEALLOCATE(grid%forcwlsm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1680,&
'frame/module_domain.f: Failed to deallocate grid%forcwlsm. ')
 endif
  NULLIFY(grid%forcwlsm)
ENDIF
IF ( ASSOCIATED( grid%acfroc ) ) THEN 
  DEALLOCATE(grid%acfroc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1688,&
'frame/module_domain.f: Failed to deallocate grid%acfroc. ')
 endif
  NULLIFY(grid%acfroc)
ENDIF
IF ( ASSOCIATED( grid%acirb ) ) THEN 
  DEALLOCATE(grid%acirb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1696,&
'frame/module_domain.f: Failed to deallocate grid%acirb. ')
 endif
  NULLIFY(grid%acirb)
ENDIF
IF ( ASSOCIATED( grid%acirc ) ) THEN 
  DEALLOCATE(grid%acirc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1704,&
'frame/module_domain.f: Failed to deallocate grid%acirc. ')
 endif
  NULLIFY(grid%acirc)
ENDIF
IF ( ASSOCIATED( grid%acpahlsm ) ) THEN 
  DEALLOCATE(grid%acpahlsm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1712,&
'frame/module_domain.f: Failed to deallocate grid%acpahlsm. ')
 endif
  NULLIFY(grid%acpahlsm)
ENDIF
IF ( ASSOCIATED( grid%acc_ecanxy ) ) THEN 
  DEALLOCATE(grid%acc_ecanxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1720,&
'frame/module_domain.f: Failed to deallocate grid%acc_ecanxy. ')
 endif
  NULLIFY(grid%acc_ecanxy)
ENDIF
IF ( ASSOCIATED( grid%td_fraction ) ) THEN 
  DEALLOCATE(grid%td_fraction,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1728,&
'frame/module_domain.f: Failed to deallocate grid%td_fraction. ')
 endif
  NULLIFY(grid%td_fraction)
ENDIF
IF ( ASSOCIATED( grid%irsivol ) ) THEN 
  DEALLOCATE(grid%irsivol,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1736,&
'frame/module_domain.f: Failed to deallocate grid%irsivol. ')
 endif
  NULLIFY(grid%irsivol)
ENDIF
IF ( ASSOCIATED( grid%kext_qa ) ) THEN 
  DEALLOCATE(grid%kext_qa,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1744,&
'frame/module_domain.f: Failed to deallocate grid%kext_qa. ')
 endif
  NULLIFY(grid%kext_qa)
ENDIF
IF ( ASSOCIATED( grid%tcoli_max ) ) THEN 
  DEALLOCATE(grid%tcoli_max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1752,&
'frame/module_domain.f: Failed to deallocate grid%tcoli_max. ')
 endif
  NULLIFY(grid%tcoli_max)
ENDIF
IF ( ASSOCIATED( grid%qicing_lg_max ) ) THEN 
  DEALLOCATE(grid%qicing_lg_max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1760,&
'frame/module_domain.f: Failed to deallocate grid%qicing_lg_max. ')
 endif
  NULLIFY(grid%qicing_lg_max)
ENDIF
IF ( ASSOCIATED( grid%afwa_llturblgt ) ) THEN 
  DEALLOCATE(grid%afwa_llturblgt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1768,&
'frame/module_domain.f: Failed to deallocate grid%afwa_llturblgt. ')
 endif
  NULLIFY(grid%afwa_llturblgt)
ENDIF
IF ( ASSOCIATED( grid%afwa_vis_dust ) ) THEN 
  DEALLOCATE(grid%afwa_vis_dust,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1776,&
'frame/module_domain.f: Failed to deallocate grid%afwa_vis_dust. ')
 endif
  NULLIFY(grid%afwa_vis_dust)
ENDIF
IF ( ASSOCIATED( grid%midrh_min_old ) ) THEN 
  DEALLOCATE(grid%midrh_min_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1784,&
'frame/module_domain.f: Failed to deallocate grid%midrh_min_old. ')
 endif
  NULLIFY(grid%midrh_min_old)
ENDIF
IF ( ASSOCIATED( grid%hfx_mean ) ) THEN 
  DEALLOCATE(grid%hfx_mean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1792,&
'frame/module_domain.f: Failed to deallocate grid%hfx_mean. ')
 endif
  NULLIFY(grid%hfx_mean)
ENDIF
IF ( ASSOCIATED( grid%psfc_diurn ) ) THEN 
  DEALLOCATE(grid%psfc_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1800,&
'frame/module_domain.f: Failed to deallocate grid%psfc_diurn. ')
 endif
  NULLIFY(grid%psfc_diurn)
ENDIF
IF ( ASSOCIATED( grid%swupb_diurn ) ) THEN 
  DEALLOCATE(grid%swupb_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1808,&
'frame/module_domain.f: Failed to deallocate grid%swupb_diurn. ')
 endif
  NULLIFY(grid%swupb_diurn)
ENDIF
IF ( ASSOCIATED( grid%u10_dtmp ) ) THEN 
  DEALLOCATE(grid%u10_dtmp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1816,&
'frame/module_domain.f: Failed to deallocate grid%u10_dtmp. ')
 endif
  NULLIFY(grid%u10_dtmp)
ENDIF
IF ( ASSOCIATED( grid%lightdis ) ) THEN 
  DEALLOCATE(grid%lightdis,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1824,&
'frame/module_domain.f: Failed to deallocate grid%lightdis. ')
 endif
  NULLIFY(grid%lightdis)
ENDIF
IF ( ASSOCIATED( grid%c3f ) ) THEN 
  DEALLOCATE(grid%c3f,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1832,&
'frame/module_domain.f: Failed to deallocate grid%c3f. ')
 endif
  NULLIFY(grid%c3f)
ENDIF
IF ( ASSOCIATED( grid%qnbca_gc ) ) THEN 
  DEALLOCATE(grid%qnbca_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1840,&
'frame/module_domain.f: Failed to deallocate grid%qnbca_gc. ')
 endif
  NULLIFY(grid%qnbca_gc)
ENDIF
IF ( ASSOCIATED( grid%p_wif_oct ) ) THEN 
  DEALLOCATE(grid%p_wif_oct,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1848,&
'frame/module_domain.f: Failed to deallocate grid%p_wif_oct. ')
 endif
  NULLIFY(grid%p_wif_oct)
ENDIF
IF ( ASSOCIATED( grid%w_wif_sep ) ) THEN 
  DEALLOCATE(grid%w_wif_sep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1856,&
'frame/module_domain.f: Failed to deallocate grid%w_wif_sep. ')
 endif
  NULLIFY(grid%w_wif_sep)
ENDIF
IF ( ASSOCIATED( grid%i_wif_aug ) ) THEN 
  DEALLOCATE(grid%i_wif_aug,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1864,&
'frame/module_domain.f: Failed to deallocate grid%i_wif_aug. ')
 endif
  NULLIFY(grid%i_wif_aug)
ENDIF
IF ( ASSOCIATED( grid%b_wif_jul ) ) THEN 
  DEALLOCATE(grid%b_wif_jul,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1872,&
'frame/module_domain.f: Failed to deallocate grid%b_wif_jul. ')
 endif
  NULLIFY(grid%b_wif_jul)
ENDIF
IF ( ASSOCIATED( grid%vmet ) ) THEN 
  DEALLOCATE(grid%vmet,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1880,&
'frame/module_domain.f: Failed to deallocate grid%vmet. ')
 endif
  NULLIFY(grid%vmet)
ENDIF
IF ( ASSOCIATED( grid%wvp ) ) THEN 
  DEALLOCATE(grid%wvp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1888,&
'frame/module_domain.f: Failed to deallocate grid%wvp. ')
 endif
  NULLIFY(grid%wvp)
ENDIF
IF ( ASSOCIATED( grid%re_qi_tot ) ) THEN 
  DEALLOCATE(grid%re_qi_tot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1896,&
'frame/module_domain.f: Failed to deallocate grid%re_qi_tot. ')
 endif
  NULLIFY(grid%re_qi_tot)
ENDIF
IF ( ASSOCIATED( grid%ghi_accum ) ) THEN 
  DEALLOCATE(grid%ghi_accum,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1904,&
'frame/module_domain.f: Failed to deallocate grid%ghi_accum. ')
 endif
  NULLIFY(grid%ghi_accum)
ENDIF
IF ( ASSOCIATED( grid%ts_re_qs ) ) THEN 
  DEALLOCATE(grid%ts_re_qs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1912,&
'frame/module_domain.f: Failed to deallocate grid%ts_re_qs. ')
 endif
  NULLIFY(grid%ts_re_qs)
ENDIF
IF ( ASSOCIATED( grid%ts_clrnidx ) ) THEN 
  DEALLOCATE(grid%ts_clrnidx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1920,&
'frame/module_domain.f: Failed to deallocate grid%ts_clrnidx. ')
 endif
  NULLIFY(grid%ts_clrnidx)
ENDIF
IF ( ASSOCIATED( grid%ts_swddnic2 ) ) THEN 
  DEALLOCATE(grid%ts_swddnic2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1928,&
'frame/module_domain.f: Failed to deallocate grid%ts_swddnic2. ')
 endif
  NULLIFY(grid%ts_swddnic2)
ENDIF
IF ( ASSOCIATED( grid%t_pl ) ) THEN 
  DEALLOCATE(grid%t_pl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1936,&
'frame/module_domain.f: Failed to deallocate grid%t_pl. ')
 endif
  NULLIFY(grid%t_pl)
ENDIF
IF ( ASSOCIATED( grid%z_zl ) ) THEN 
  DEALLOCATE(grid%z_zl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1944,&
'frame/module_domain.f: Failed to deallocate grid%z_zl. ')
 endif
  NULLIFY(grid%z_zl)
ENDIF
IF ( ASSOCIATED( grid%u_iau ) ) THEN 
  DEALLOCATE(grid%u_iau,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1952,&
'frame/module_domain.f: Failed to deallocate grid%u_iau. ')
 endif
  NULLIFY(grid%u_iau)
ENDIF
IF ( ASSOCIATED( grid%ruiauten ) ) THEN 
  DEALLOCATE(grid%ruiauten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1960,&
'frame/module_domain.f: Failed to deallocate grid%ruiauten. ')
 endif
  NULLIFY(grid%ruiauten)
ENDIF
IF ( ASSOCIATED( grid%landmask ) ) THEN 
  DEALLOCATE(grid%landmask,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1968,&
'frame/module_domain.f: Failed to deallocate grid%landmask. ')
 endif
  NULLIFY(grid%landmask)
ENDIF
END SUBROUTINE deallocs_10



