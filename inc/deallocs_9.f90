





SUBROUTINE deallocs_9( grid )
  USE module_wrf_error
  USE module_domain_type
  IMPLICIT NONE
  TYPE( domain ), POINTER :: grid
  INTEGER :: ierr
IF ( ASSOCIATED( grid%traj_j ) ) THEN 
  DEALLOCATE(grid%traj_j,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",16,&
'frame/module_domain.f: Failed to deallocate grid%traj_j. ')
 endif
  NULLIFY(grid%traj_j)
ENDIF
IF ( ASSOCIATED( grid%xlong_gc ) ) THEN 
  DEALLOCATE(grid%xlong_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",24,&
'frame/module_domain.f: Failed to deallocate grid%xlong_gc. ')
 endif
  NULLIFY(grid%xlong_gc)
ENDIF
IF ( ASSOCIATED( grid%lai12m ) ) THEN 
  DEALLOCATE(grid%lai12m,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",32,&
'frame/module_domain.f: Failed to deallocate grid%lai12m. ')
 endif
  NULLIFY(grid%lai12m)
ENDIF
IF ( ASSOCIATED( grid%qr_gc ) ) THEN 
  DEALLOCATE(grid%qr_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",40,&
'frame/module_domain.f: Failed to deallocate grid%qr_gc. ')
 endif
  NULLIFY(grid%qr_gc)
ENDIF
IF ( ASSOCIATED( grid%qntemp ) ) THEN 
  DEALLOCATE(grid%qntemp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",48,&
'frame/module_domain.f: Failed to deallocate grid%qntemp. ')
 endif
  NULLIFY(grid%qntemp)
ENDIF
IF ( ASSOCIATED( grid%ptrop ) ) THEN 
  DEALLOCATE(grid%ptrop,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",56,&
'frame/module_domain.f: Failed to deallocate grid%ptrop. ')
 endif
  NULLIFY(grid%ptrop)
ENDIF
IF ( ASSOCIATED( grid%u_bxs ) ) THEN 
  DEALLOCATE(grid%u_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",64,&
'frame/module_domain.f: Failed to deallocate grid%u_bxs. ')
 endif
  NULLIFY(grid%u_bxs)
ENDIF
IF ( ASSOCIATED( grid%u_bxe ) ) THEN 
  DEALLOCATE(grid%u_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",72,&
'frame/module_domain.f: Failed to deallocate grid%u_bxe. ')
 endif
  NULLIFY(grid%u_bxe)
ENDIF
IF ( ASSOCIATED( grid%u_bys ) ) THEN 
  DEALLOCATE(grid%u_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",80,&
'frame/module_domain.f: Failed to deallocate grid%u_bys. ')
 endif
  NULLIFY(grid%u_bys)
ENDIF
IF ( ASSOCIATED( grid%u_bye ) ) THEN 
  DEALLOCATE(grid%u_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",88,&
'frame/module_domain.f: Failed to deallocate grid%u_bye. ')
 endif
  NULLIFY(grid%u_bye)
ENDIF
IF ( ASSOCIATED( grid%v_bxs ) ) THEN 
  DEALLOCATE(grid%v_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",96,&
'frame/module_domain.f: Failed to deallocate grid%v_bxs. ')
 endif
  NULLIFY(grid%v_bxs)
ENDIF
IF ( ASSOCIATED( grid%v_bxe ) ) THEN 
  DEALLOCATE(grid%v_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",104,&
'frame/module_domain.f: Failed to deallocate grid%v_bxe. ')
 endif
  NULLIFY(grid%v_bxe)
ENDIF
IF ( ASSOCIATED( grid%v_bys ) ) THEN 
  DEALLOCATE(grid%v_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",112,&
'frame/module_domain.f: Failed to deallocate grid%v_bys. ')
 endif
  NULLIFY(grid%v_bys)
ENDIF
IF ( ASSOCIATED( grid%v_bye ) ) THEN 
  DEALLOCATE(grid%v_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",120,&
'frame/module_domain.f: Failed to deallocate grid%v_bye. ')
 endif
  NULLIFY(grid%v_bye)
ENDIF
IF ( ASSOCIATED( grid%ww ) ) THEN 
  DEALLOCATE(grid%ww,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",128,&
'frame/module_domain.f: Failed to deallocate grid%ww. ')
 endif
  NULLIFY(grid%ww)
ENDIF
IF ( ASSOCIATED( grid%acoustic_grad_u ) ) THEN 
  DEALLOCATE(grid%acoustic_grad_u,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",136,&
'frame/module_domain.f: Failed to deallocate grid%acoustic_grad_u. ')
 endif
  NULLIFY(grid%acoustic_grad_u)
ENDIF
IF ( ASSOCIATED( grid%th_upstream_x_tend ) ) THEN 
  DEALLOCATE(grid%th_upstream_x_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",144,&
'frame/module_domain.f: Failed to deallocate grid%th_upstream_x_tend. ')
 endif
  NULLIFY(grid%th_upstream_x_tend)
ENDIF
IF ( ASSOCIATED( grid%u_upstream_x_tend ) ) THEN 
  DEALLOCATE(grid%u_upstream_x_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",152,&
'frame/module_domain.f: Failed to deallocate grid%u_upstream_x_tend. ')
 endif
  NULLIFY(grid%u_upstream_x_tend)
ENDIF
IF ( ASSOCIATED( grid%qv_largescale_tend ) ) THEN 
  DEALLOCATE(grid%qv_largescale_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",160,&
'frame/module_domain.f: Failed to deallocate grid%qv_largescale_tend. ')
 endif
  NULLIFY(grid%qv_largescale_tend)
ENDIF
IF ( ASSOCIATED( grid%tau_y_tend ) ) THEN 
  DEALLOCATE(grid%tau_y_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",168,&
'frame/module_domain.f: Failed to deallocate grid%tau_y_tend. ')
 endif
  NULLIFY(grid%tau_y_tend)
ENDIF
IF ( ASSOCIATED( grid%muvs ) ) THEN 
  DEALLOCATE(grid%muvs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",176,&
'frame/module_domain.f: Failed to deallocate grid%muvs. ')
 endif
  NULLIFY(grid%muvs)
ENDIF
IF ( ASSOCIATED( grid%tke_1 ) ) THEN 
  DEALLOCATE(grid%tke_1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",184,&
'frame/module_domain.f: Failed to deallocate grid%tke_1. ')
 endif
  NULLIFY(grid%tke_1)
ENDIF
IF ( ASSOCIATED( grid%tke_2 ) ) THEN 
  DEALLOCATE(grid%tke_2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",192,&
'frame/module_domain.f: Failed to deallocate grid%tke_2. ')
 endif
  NULLIFY(grid%tke_2)
ENDIF
IF ( ASSOCIATED( grid%alb ) ) THEN 
  DEALLOCATE(grid%alb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",200,&
'frame/module_domain.f: Failed to deallocate grid%alb. ')
 endif
  NULLIFY(grid%alb)
ENDIF
IF ( ASSOCIATED( grid%z ) ) THEN 
  DEALLOCATE(grid%z,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",208,&
'frame/module_domain.f: Failed to deallocate grid%z. ')
 endif
  NULLIFY(grid%z)
ENDIF
IF ( ASSOCIATED( grid%vratx ) ) THEN 
  DEALLOCATE(grid%vratx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",216,&
'frame/module_domain.f: Failed to deallocate grid%vratx. ')
 endif
  NULLIFY(grid%vratx)
ENDIF
IF ( ASSOCIATED( grid%moist_bxs ) ) THEN 
  DEALLOCATE(grid%moist_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",224,&
'frame/module_domain.f: Failed to deallocate grid%moist_bxs. ')
 endif
  NULLIFY(grid%moist_bxs)
ENDIF
IF ( ASSOCIATED( grid%moist_bxe ) ) THEN 
  DEALLOCATE(grid%moist_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",232,&
'frame/module_domain.f: Failed to deallocate grid%moist_bxe. ')
 endif
  NULLIFY(grid%moist_bxe)
ENDIF
IF ( ASSOCIATED( grid%moist_bys ) ) THEN 
  DEALLOCATE(grid%moist_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",240,&
'frame/module_domain.f: Failed to deallocate grid%moist_bys. ')
 endif
  NULLIFY(grid%moist_bys)
ENDIF
IF ( ASSOCIATED( grid%moist_bye ) ) THEN 
  DEALLOCATE(grid%moist_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",248,&
'frame/module_domain.f: Failed to deallocate grid%moist_bye. ')
 endif
  NULLIFY(grid%moist_bye)
ENDIF
IF ( ASSOCIATED( grid%re_cloud ) ) THEN 
  DEALLOCATE(grid%re_cloud,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",256,&
'frame/module_domain.f: Failed to deallocate grid%re_cloud. ')
 endif
  NULLIFY(grid%re_cloud)
ENDIF
IF ( ASSOCIATED( grid%dfi_re_cloud_gsfc ) ) THEN 
  DEALLOCATE(grid%dfi_re_cloud_gsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",264,&
'frame/module_domain.f: Failed to deallocate grid%dfi_re_cloud_gsfc. ')
 endif
  NULLIFY(grid%dfi_re_cloud_gsfc)
ENDIF
IF ( ASSOCIATED( grid%dfi_scalar ) ) THEN 
  DEALLOCATE(grid%dfi_scalar,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",272,&
'frame/module_domain.f: Failed to deallocate grid%dfi_scalar. ')
 endif
  NULLIFY(grid%dfi_scalar)
ENDIF
IF ( ASSOCIATED( grid%soilm ) ) THEN 
  DEALLOCATE(grid%soilm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",280,&
'frame/module_domain.f: Failed to deallocate grid%soilm. ')
 endif
  NULLIFY(grid%soilm)
ENDIF
IF ( ASSOCIATED( grid%sm100200 ) ) THEN 
  DEALLOCATE(grid%sm100200,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",288,&
'frame/module_domain.f: Failed to deallocate grid%sm100200. ')
 endif
  NULLIFY(grid%sm100200)
ENDIF
IF ( ASSOCIATED( grid%sw010200 ) ) THEN 
  DEALLOCATE(grid%sw010200,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",296,&
'frame/module_domain.f: Failed to deallocate grid%sw010200. ')
 endif
  NULLIFY(grid%sw010200)
ENDIF
IF ( ASSOCIATED( grid%soilt000 ) ) THEN 
  DEALLOCATE(grid%soilt000,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",304,&
'frame/module_domain.f: Failed to deallocate grid%soilt000. ')
 endif
  NULLIFY(grid%soilt000)
ENDIF
IF ( ASSOCIATED( grid%shdmin ) ) THEN 
  DEALLOCATE(grid%shdmin,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",312,&
'frame/module_domain.f: Failed to deallocate grid%shdmin. ')
 endif
  NULLIFY(grid%shdmin)
ENDIF
IF ( ASSOCIATED( grid%tslb ) ) THEN 
  DEALLOCATE(grid%tslb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",320,&
'frame/module_domain.f: Failed to deallocate grid%tslb. ')
 endif
  NULLIFY(grid%tslb)
ENDIF
IF ( ASSOCIATED( grid%ts_tslb ) ) THEN 
  DEALLOCATE(grid%ts_tslb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",328,&
'frame/module_domain.f: Failed to deallocate grid%ts_tslb. ')
 endif
  NULLIFY(grid%ts_tslb)
ENDIF
IF ( ASSOCIATED( grid%dzb ) ) THEN 
  DEALLOCATE(grid%dzb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",336,&
'frame/module_domain.f: Failed to deallocate grid%dzb. ')
 endif
  NULLIFY(grid%dzb)
ENDIF
IF ( ASSOCIATED( grid%lf_urb2d_s ) ) THEN 
  DEALLOCATE(grid%lf_urb2d_s,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",344,&
'frame/module_domain.f: Failed to deallocate grid%lf_urb2d_s. ')
 endif
  NULLIFY(grid%lf_urb2d_s)
ENDIF
IF ( ASSOCIATED( grid%soldrain ) ) THEN 
  DEALLOCATE(grid%soldrain,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",352,&
'frame/module_domain.f: Failed to deallocate grid%soldrain. ')
 endif
  NULLIFY(grid%soldrain)
ENDIF
IF ( ASSOCIATED( grid%acgrdflx ) ) THEN 
  DEALLOCATE(grid%acgrdflx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",360,&
'frame/module_domain.f: Failed to deallocate grid%acgrdflx. ')
 endif
  NULLIFY(grid%acgrdflx)
ENDIF
IF ( ASSOCIATED( grid%dfi_mu ) ) THEN 
  DEALLOCATE(grid%dfi_mu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",368,&
'frame/module_domain.f: Failed to deallocate grid%dfi_mu. ')
 endif
  NULLIFY(grid%dfi_mu)
ENDIF
IF ( ASSOCIATED( grid%dfi_alt ) ) THEN 
  DEALLOCATE(grid%dfi_alt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",376,&
'frame/module_domain.f: Failed to deallocate grid%dfi_alt. ')
 endif
  NULLIFY(grid%dfi_alt)
ENDIF
IF ( ASSOCIATED( grid%tgr_urb2d ) ) THEN 
  DEALLOCATE(grid%tgr_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",384,&
'frame/module_domain.f: Failed to deallocate grid%tgr_urb2d. ')
 endif
  NULLIFY(grid%tgr_urb2d)
ENDIF
IF ( ASSOCIATED( grid%drelb_urb2d ) ) THEN 
  DEALLOCATE(grid%drelb_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",392,&
'frame/module_domain.f: Failed to deallocate grid%drelb_urb2d. ')
 endif
  NULLIFY(grid%drelb_urb2d)
ENDIF
IF ( ASSOCIATED( grid%g_urb2d ) ) THEN 
  DEALLOCATE(grid%g_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",400,&
'frame/module_domain.f: Failed to deallocate grid%g_urb2d. ')
 endif
  NULLIFY(grid%g_urb2d)
ENDIF
IF ( ASSOCIATED( grid%tw2lev_urb3d ) ) THEN 
  DEALLOCATE(grid%tw2lev_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",408,&
'frame/module_domain.f: Failed to deallocate grid%tw2lev_urb3d. ')
 endif
  NULLIFY(grid%tw2lev_urb3d)
ENDIF
IF ( ASSOCIATED( grid%sfr_urb3d ) ) THEN 
  DEALLOCATE(grid%sfr_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",416,&
'frame/module_domain.f: Failed to deallocate grid%sfr_urb3d. ')
 endif
  NULLIFY(grid%sfr_urb3d)
ENDIF
IF ( ASSOCIATED( grid%dgr_urb3d ) ) THEN 
  DEALLOCATE(grid%dgr_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",424,&
'frame/module_domain.f: Failed to deallocate grid%dgr_urb3d. ')
 endif
  NULLIFY(grid%dgr_urb3d)
ENDIF
IF ( ASSOCIATED( grid%swvisdif ) ) THEN 
  DEALLOCATE(grid%swvisdif,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",432,&
'frame/module_domain.f: Failed to deallocate grid%swvisdif. ')
 endif
  NULLIFY(grid%swvisdif)
ENDIF
IF ( ASSOCIATED( grid%q2obs ) ) THEN 
  DEALLOCATE(grid%q2obs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",440,&
'frame/module_domain.f: Failed to deallocate grid%q2obs. ')
 endif
  NULLIFY(grid%q2obs)
ENDIF
IF ( ASSOCIATED( grid%fm ) ) THEN 
  DEALLOCATE(grid%fm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",448,&
'frame/module_domain.f: Failed to deallocate grid%fm. ')
 endif
  NULLIFY(grid%fm)
ENDIF
IF ( ASSOCIATED( grid%ct ) ) THEN 
  DEALLOCATE(grid%ct,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",456,&
'frame/module_domain.f: Failed to deallocate grid%ct. ')
 endif
  NULLIFY(grid%ct)
ENDIF
IF ( ASSOCIATED( grid%akpbl ) ) THEN 
  DEALLOCATE(grid%akpbl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",464,&
'frame/module_domain.f: Failed to deallocate grid%akpbl. ')
 endif
  NULLIFY(grid%akpbl)
ENDIF
IF ( ASSOCIATED( grid%rv_up ) ) THEN 
  DEALLOCATE(grid%rv_up,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",472,&
'frame/module_domain.f: Failed to deallocate grid%rv_up. ')
 endif
  NULLIFY(grid%rv_up)
ENDIF
IF ( ASSOCIATED( grid%uw_temf ) ) THEN 
  DEALLOCATE(grid%uw_temf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",480,&
'frame/module_domain.f: Failed to deallocate grid%uw_temf. ')
 endif
  NULLIFY(grid%uw_temf)
ENDIF
IF ( ASSOCIATED( grid%wm_temf ) ) THEN 
  DEALLOCATE(grid%wm_temf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",488,&
'frame/module_domain.f: Failed to deallocate grid%wm_temf. ')
 endif
  NULLIFY(grid%wm_temf)
ENDIF
IF ( ASSOCIATED( grid%ch ) ) THEN 
  DEALLOCATE(grid%ch,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",496,&
'frame/module_domain.f: Failed to deallocate grid%ch. ')
 endif
  NULLIFY(grid%ch)
ENDIF
IF ( ASSOCIATED( grid%maxwidth ) ) THEN 
  DEALLOCATE(grid%maxwidth,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",504,&
'frame/module_domain.f: Failed to deallocate grid%maxwidth. ')
 endif
  NULLIFY(grid%maxwidth)
ENDIF
IF ( ASSOCIATED( grid%oa1 ) ) THEN 
  DEALLOCATE(grid%oa1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",512,&
'frame/module_domain.f: Failed to deallocate grid%oa1. ')
 endif
  NULLIFY(grid%oa1)
ENDIF
IF ( ASSOCIATED( grid%dtaux3d_ss ) ) THEN 
  DEALLOCATE(grid%dtaux3d_ss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",520,&
'frame/module_domain.f: Failed to deallocate grid%dtaux3d_ss. ')
 endif
  NULLIFY(grid%dtaux3d_ss)
ENDIF
IF ( ASSOCIATED( grid%var2dls ) ) THEN 
  DEALLOCATE(grid%var2dls,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",528,&
'frame/module_domain.f: Failed to deallocate grid%var2dls. ')
 endif
  NULLIFY(grid%var2dls)
ENDIF
IF ( ASSOCIATED( grid%oa1ss ) ) THEN 
  DEALLOCATE(grid%oa1ss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",536,&
'frame/module_domain.f: Failed to deallocate grid%oa1ss. ')
 endif
  NULLIFY(grid%oa1ss)
ENDIF
IF ( ASSOCIATED( grid%a_t_bep ) ) THEN 
  DEALLOCATE(grid%a_t_bep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",544,&
'frame/module_domain.f: Failed to deallocate grid%a_t_bep. ')
 endif
  NULLIFY(grid%a_t_bep)
ENDIF
IF ( ASSOCIATED( grid%tke_pbl ) ) THEN 
  DEALLOCATE(grid%tke_pbl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",552,&
'frame/module_domain.f: Failed to deallocate grid%tke_pbl. ')
 endif
  NULLIFY(grid%tke_pbl)
ENDIF
IF ( ASSOCIATED( grid%hbotr ) ) THEN 
  DEALLOCATE(grid%hbotr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",560,&
'frame/module_domain.f: Failed to deallocate grid%hbotr. ')
 endif
  NULLIFY(grid%hbotr)
ENDIF
IF ( ASSOCIATED( grid%acfrcv ) ) THEN 
  DEALLOCATE(grid%acfrcv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",568,&
'frame/module_domain.f: Failed to deallocate grid%acfrcv. ')
 endif
  NULLIFY(grid%acfrcv)
ENDIF
IF ( ASSOCIATED( grid%ozmixm ) ) THEN 
  DEALLOCATE(grid%ozmixm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",576,&
'frame/module_domain.f: Failed to deallocate grid%ozmixm. ')
 endif
  NULLIFY(grid%ozmixm)
ENDIF
IF ( ASSOCIATED( grid%om_u ) ) THEN 
  DEALLOCATE(grid%om_u,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",584,&
'frame/module_domain.f: Failed to deallocate grid%om_u. ')
 endif
  NULLIFY(grid%om_u)
ENDIF
IF ( ASSOCIATED( grid%shall ) ) THEN 
  DEALLOCATE(grid%shall,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",592,&
'frame/module_domain.f: Failed to deallocate grid%shall. ')
 endif
  NULLIFY(grid%shall)
ENDIF
IF ( ASSOCIATED( grid%wup_cup ) ) THEN 
  DEALLOCATE(grid%wup_cup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",600,&
'frame/module_domain.f: Failed to deallocate grid%wup_cup. ')
 endif
  NULLIFY(grid%wup_cup)
ENDIF
IF ( ASSOCIATED( grid%lnint ) ) THEN 
  DEALLOCATE(grid%lnint,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",608,&
'frame/module_domain.f: Failed to deallocate grid%lnint. ')
 endif
  NULLIFY(grid%lnint)
ENDIF
IF ( ASSOCIATED( grid%msfvx_inv ) ) THEN 
  DEALLOCATE(grid%msfvx_inv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",616,&
'frame/module_domain.f: Failed to deallocate grid%msfvx_inv. ')
 endif
  NULLIFY(grid%msfvx_inv)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_bxs ) ) THEN 
  DEALLOCATE(grid%ht_shad_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",624,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_bxs. ')
 endif
  NULLIFY(grid%ht_shad_bxs)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_bxe ) ) THEN 
  DEALLOCATE(grid%ht_shad_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",632,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_bxe. ')
 endif
  NULLIFY(grid%ht_shad_bxe)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_bys ) ) THEN 
  DEALLOCATE(grid%ht_shad_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",640,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_bys. ')
 endif
  NULLIFY(grid%ht_shad_bys)
ENDIF
IF ( ASSOCIATED( grid%ht_shad_bye ) ) THEN 
  DEALLOCATE(grid%ht_shad_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",648,&
'frame/module_domain.f: Failed to deallocate grid%ht_shad_bye. ')
 endif
  NULLIFY(grid%ht_shad_bye)
ENDIF
IF ( ASSOCIATED( grid%phys_tot ) ) THEN 
  DEALLOCATE(grid%phys_tot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",656,&
'frame/module_domain.f: Failed to deallocate grid%phys_tot. ')
 endif
  NULLIFY(grid%phys_tot)
ENDIF
IF ( ASSOCIATED( grid%acphysm ) ) THEN 
  DEALLOCATE(grid%acphysm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",664,&
'frame/module_domain.f: Failed to deallocate grid%acphysm. ')
 endif
  NULLIFY(grid%acphysm)
ENDIF
IF ( ASSOCIATED( grid%tswup ) ) THEN 
  DEALLOCATE(grid%tswup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",672,&
'frame/module_domain.f: Failed to deallocate grid%tswup. ')
 endif
  NULLIFY(grid%tswup)
ENDIF
IF ( ASSOCIATED( grid%rqishten ) ) THEN 
  DEALLOCATE(grid%rqishten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",680,&
'frame/module_domain.f: Failed to deallocate grid%rqishten. ')
 endif
  NULLIFY(grid%rqishten)
ENDIF
IF ( ASSOCIATED( grid%clddpthb ) ) THEN 
  DEALLOCATE(grid%clddpthb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",688,&
'frame/module_domain.f: Failed to deallocate grid%clddpthb. ')
 endif
  NULLIFY(grid%clddpthb)
ENDIF
IF ( ASSOCIATED( grid%pblhavg ) ) THEN 
  DEALLOCATE(grid%pblhavg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",696,&
'frame/module_domain.f: Failed to deallocate grid%pblhavg. ')
 endif
  NULLIFY(grid%pblhavg)
ENDIF
IF ( ASSOCIATED( grid%rqincuten ) ) THEN 
  DEALLOCATE(grid%rqincuten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",704,&
'frame/module_domain.f: Failed to deallocate grid%rqincuten. ')
 endif
  NULLIFY(grid%rqincuten)
ENDIF
IF ( ASSOCIATED( grid%rainshv ) ) THEN 
  DEALLOCATE(grid%rainshv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",712,&
'frame/module_domain.f: Failed to deallocate grid%rainshv. ')
 endif
  NULLIFY(grid%rainshv)
ENDIF
IF ( ASSOCIATED( grid%qv_old ) ) THEN 
  DEALLOCATE(grid%qv_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",720,&
'frame/module_domain.f: Failed to deallocate grid%qv_old. ')
 endif
  NULLIFY(grid%qv_old)
ENDIF
IF ( ASSOCIATED( grid%phii3d_3 ) ) THEN 
  DEALLOCATE(grid%phii3d_3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",728,&
'frame/module_domain.f: Failed to deallocate grid%phii3d_3. ')
 endif
  NULLIFY(grid%phii3d_3)
ENDIF
IF ( ASSOCIATED( grid%ddr_kf ) ) THEN 
  DEALLOCATE(grid%ddr_kf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",736,&
'frame/module_domain.f: Failed to deallocate grid%ddr_kf. ')
 endif
  NULLIFY(grid%ddr_kf)
ENDIF
IF ( ASSOCIATED( grid%edt_out ) ) THEN 
  DEALLOCATE(grid%edt_out,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",744,&
'frame/module_domain.f: Failed to deallocate grid%edt_out. ')
 endif
  NULLIFY(grid%edt_out)
ENDIF
IF ( ASSOCIATED( grid%cugd_ttens ) ) THEN 
  DEALLOCATE(grid%cugd_ttens,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",752,&
'frame/module_domain.f: Failed to deallocate grid%cugd_ttens. ')
 endif
  NULLIFY(grid%cugd_ttens)
ENDIF
IF ( ASSOCIATED( grid%qr_cu ) ) THEN 
  DEALLOCATE(grid%qr_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",760,&
'frame/module_domain.f: Failed to deallocate grid%qr_cu. ')
 endif
  NULLIFY(grid%qr_cu)
ENDIF
IF ( ASSOCIATED( grid%efsg ) ) THEN 
  DEALLOCATE(grid%efsg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",768,&
'frame/module_domain.f: Failed to deallocate grid%efsg. ')
 endif
  NULLIFY(grid%efsg)
ENDIF
IF ( ASSOCIATED( grid%cldfra_old ) ) THEN 
  DEALLOCATE(grid%cldfra_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",776,&
'frame/module_domain.f: Failed to deallocate grid%cldfra_old. ')
 endif
  NULLIFY(grid%cldfra_old)
ENDIF
IF ( ASSOCIATED( grid%swddir2 ) ) THEN 
  DEALLOCATE(grid%swddir2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",784,&
'frame/module_domain.f: Failed to deallocate grid%swddir2. ')
 endif
  NULLIFY(grid%swddir2)
ENDIF
IF ( ASSOCIATED( grid%coszen_ref ) ) THEN 
  DEALLOCATE(grid%coszen_ref,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",792,&
'frame/module_domain.f: Failed to deallocate grid%coszen_ref. ')
 endif
  NULLIFY(grid%coszen_ref)
ENDIF
IF ( ASSOCIATED( grid%tt2min ) ) THEN 
  DEALLOCATE(grid%tt2min,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",800,&
'frame/module_domain.f: Failed to deallocate grid%tt2min. ')
 endif
  NULLIFY(grid%tt2min)
ENDIF
IF ( ASSOCIATED( grid%tskintempmin ) ) THEN 
  DEALLOCATE(grid%tskintempmin,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",808,&
'frame/module_domain.f: Failed to deallocate grid%tskintempmin. ')
 endif
  NULLIFY(grid%tskintempmin)
ENDIF
IF ( ASSOCIATED( grid%v10std ) ) THEN 
  DEALLOCATE(grid%v10std,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",816,&
'frame/module_domain.f: Failed to deallocate grid%v10std. ')
 endif
  NULLIFY(grid%v10std)
ENDIF
IF ( ASSOCIATED( grid%acswuptc ) ) THEN 
  DEALLOCATE(grid%acswuptc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",824,&
'frame/module_domain.f: Failed to deallocate grid%acswuptc. ')
 endif
  NULLIFY(grid%acswuptc)
ENDIF
IF ( ASSOCIATED( grid%aclwupbc ) ) THEN 
  DEALLOCATE(grid%aclwupbc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",832,&
'frame/module_domain.f: Failed to deallocate grid%aclwupbc. ')
 endif
  NULLIFY(grid%aclwupbc)
ENDIF
IF ( ASSOCIATED( grid%i_aclwuptc ) ) THEN 
  DEALLOCATE(grid%i_aclwuptc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",840,&
'frame/module_domain.f: Failed to deallocate grid%i_aclwuptc. ')
 endif
  NULLIFY(grid%i_aclwuptc)
ENDIF
IF ( ASSOCIATED( grid%swdntcln ) ) THEN 
  DEALLOCATE(grid%swdntcln,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",848,&
'frame/module_domain.f: Failed to deallocate grid%swdntcln. ')
 endif
  NULLIFY(grid%swdntcln)
ENDIF
IF ( ASSOCIATED( grid%lwdntcln ) ) THEN 
  DEALLOCATE(grid%lwdntcln,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",856,&
'frame/module_domain.f: Failed to deallocate grid%lwdntcln. ')
 endif
  NULLIFY(grid%lwdntcln)
ENDIF
IF ( ASSOCIATED( grid%xlat_v ) ) THEN 
  DEALLOCATE(grid%xlat_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",864,&
'frame/module_domain.f: Failed to deallocate grid%xlat_v. ')
 endif
  NULLIFY(grid%xlat_v)
ENDIF
IF ( ASSOCIATED( grid%rvblten ) ) THEN 
  DEALLOCATE(grid%rvblten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",872,&
'frame/module_domain.f: Failed to deallocate grid%rvblten. ')
 endif
  NULLIFY(grid%rvblten)
ENDIF
IF ( ASSOCIATED( grid%tslb_mosaic ) ) THEN 
  DEALLOCATE(grid%tslb_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",880,&
'frame/module_domain.f: Failed to deallocate grid%tslb_mosaic. ')
 endif
  NULLIFY(grid%tslb_mosaic)
ENDIF
IF ( ASSOCIATED( grid%z0_mosaic ) ) THEN 
  DEALLOCATE(grid%z0_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",888,&
'frame/module_domain.f: Failed to deallocate grid%z0_mosaic. ')
 endif
  NULLIFY(grid%z0_mosaic)
ENDIF
IF ( ASSOCIATED( grid%ts_urb2d_mosaic ) ) THEN 
  DEALLOCATE(grid%ts_urb2d_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",896,&
'frame/module_domain.f: Failed to deallocate grid%ts_urb2d_mosaic. ')
 endif
  NULLIFY(grid%ts_urb2d_mosaic)
ENDIF
IF ( ASSOCIATED( grid%landusef2 ) ) THEN 
  DEALLOCATE(grid%landusef2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",904,&
'frame/module_domain.f: Failed to deallocate grid%landusef2. ')
 endif
  NULLIFY(grid%landusef2)
ENDIF
IF ( ASSOCIATED( grid%t_phy ) ) THEN 
  DEALLOCATE(grid%t_phy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",912,&
'frame/module_domain.f: Failed to deallocate grid%t_phy. ')
 endif
  NULLIFY(grid%t_phy)
ENDIF
IF ( ASSOCIATED( grid%rmol ) ) THEN 
  DEALLOCATE(grid%rmol,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",920,&
'frame/module_domain.f: Failed to deallocate grid%rmol. ')
 endif
  NULLIFY(grid%rmol)
ENDIF
IF ( ASSOCIATED( grid%flqc ) ) THEN 
  DEALLOCATE(grid%flqc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",928,&
'frame/module_domain.f: Failed to deallocate grid%flqc. ')
 endif
  NULLIFY(grid%flqc)
ENDIF
IF ( ASSOCIATED( grid%dfi_snowc ) ) THEN 
  DEALLOCATE(grid%dfi_snowc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",936,&
'frame/module_domain.f: Failed to deallocate grid%dfi_snowc. ')
 endif
  NULLIFY(grid%dfi_snowc)
ENDIF
IF ( ASSOCIATED( grid%defor12 ) ) THEN 
  DEALLOCATE(grid%defor12,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",944,&
'frame/module_domain.f: Failed to deallocate grid%defor12. ')
 endif
  NULLIFY(grid%defor12)
ENDIF
IF ( ASSOCIATED( grid%v10_ndg_old ) ) THEN 
  DEALLOCATE(grid%v10_ndg_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",952,&
'frame/module_domain.f: Failed to deallocate grid%v10_ndg_old. ')
 endif
  NULLIFY(grid%v10_ndg_old)
ENDIF
IF ( ASSOCIATED( grid%ps_ndg_old ) ) THEN 
  DEALLOCATE(grid%ps_ndg_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",960,&
'frame/module_domain.f: Failed to deallocate grid%ps_ndg_old. ')
 endif
  NULLIFY(grid%ps_ndg_old)
ENDIF
IF ( ASSOCIATED( grid%qfx_both ) ) THEN 
  DEALLOCATE(grid%qfx_both,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",968,&
'frame/module_domain.f: Failed to deallocate grid%qfx_both. ')
 endif
  NULLIFY(grid%qfx_both)
ENDIF
IF ( ASSOCIATED( grid%w_dn_max ) ) THEN 
  DEALLOCATE(grid%w_dn_max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",976,&
'frame/module_domain.f: Failed to deallocate grid%w_dn_max. ')
 endif
  NULLIFY(grid%w_dn_max)
ENDIF
IF ( ASSOCIATED( grid%prec_acc_c ) ) THEN 
  DEALLOCATE(grid%prec_acc_c,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",984,&
'frame/module_domain.f: Failed to deallocate grid%prec_acc_c. ')
 endif
  NULLIFY(grid%prec_acc_c)
ENDIF
IF ( ASSOCIATED( grid%track_z ) ) THEN 
  DEALLOCATE(grid%track_z,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",992,&
'frame/module_domain.f: Failed to deallocate grid%track_z. ')
 endif
  NULLIFY(grid%track_z)
ENDIF
IF ( ASSOCIATED( grid%track_qice ) ) THEN 
  DEALLOCATE(grid%track_qice,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1000,&
'frame/module_domain.f: Failed to deallocate grid%track_qice. ')
 endif
  NULLIFY(grid%track_qice)
ENDIF
IF ( ASSOCIATED( grid%aucuten ) ) THEN 
  DEALLOCATE(grid%aucuten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1008,&
'frame/module_domain.f: Failed to deallocate grid%aucuten. ')
 endif
  NULLIFY(grid%aucuten)
ENDIF
IF ( ASSOCIATED( grid%hailcast_dhail5 ) ) THEN 
  DEALLOCATE(grid%hailcast_dhail5,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1016,&
'frame/module_domain.f: Failed to deallocate grid%hailcast_dhail5. ')
 endif
  NULLIFY(grid%hailcast_dhail5)
ENDIF
IF ( ASSOCIATED( grid%cg_flashrate ) ) THEN 
  DEALLOCATE(grid%cg_flashrate,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1024,&
'frame/module_domain.f: Failed to deallocate grid%cg_flashrate. ')
 endif
  NULLIFY(grid%cg_flashrate)
ENDIF
IF ( ASSOCIATED( grid%iccg_in_den ) ) THEN 
  DEALLOCATE(grid%iccg_in_den,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1032,&
'frame/module_domain.f: Failed to deallocate grid%iccg_in_den. ')
 endif
  NULLIFY(grid%iccg_in_den)
ENDIF
IF ( ASSOCIATED( grid%clat_xxx ) ) THEN 
  DEALLOCATE(grid%clat_xxx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1040,&
'frame/module_domain.f: Failed to deallocate grid%clat_xxx. ')
 endif
  NULLIFY(grid%clat_xxx)
ENDIF
IF ( ASSOCIATED( grid%k1_u ) ) THEN 
  DEALLOCATE(grid%k1_u,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1048,&
'frame/module_domain.f: Failed to deallocate grid%k1_u. ')
 endif
  NULLIFY(grid%k1_u)
ENDIF
IF ( ASSOCIATED( grid%k1_ph ) ) THEN 
  DEALLOCATE(grid%k1_ph,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1056,&
'frame/module_domain.f: Failed to deallocate grid%k1_ph. ')
 endif
  NULLIFY(grid%k1_ph)
ENDIF
IF ( ASSOCIATED( grid%k2_w ) ) THEN 
  DEALLOCATE(grid%k2_w,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1064,&
'frame/module_domain.f: Failed to deallocate grid%k2_w. ')
 endif
  NULLIFY(grid%k2_w)
ENDIF
IF ( ASSOCIATED( grid%k3_u ) ) THEN 
  DEALLOCATE(grid%k3_u,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1072,&
'frame/module_domain.f: Failed to deallocate grid%k3_u. ')
 endif
  NULLIFY(grid%k3_u)
ENDIF
IF ( ASSOCIATED( grid%k3_ph ) ) THEN 
  DEALLOCATE(grid%k3_ph,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1080,&
'frame/module_domain.f: Failed to deallocate grid%k3_ph. ')
 endif
  NULLIFY(grid%k3_ph)
ENDIF
IF ( ASSOCIATED( grid%rthfrten ) ) THEN 
  DEALLOCATE(grid%rthfrten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1088,&
'frame/module_domain.f: Failed to deallocate grid%rthfrten. ')
 endif
  NULLIFY(grid%rthfrten)
ENDIF
IF ( ASSOCIATED( grid%lfn_0 ) ) THEN 
  DEALLOCATE(grid%lfn_0,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1096,&
'frame/module_domain.f: Failed to deallocate grid%lfn_0. ')
 endif
  NULLIFY(grid%lfn_0)
ENDIF
IF ( ASSOCIATED( grid%fgrnqfx ) ) THEN 
  DEALLOCATE(grid%fgrnqfx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1104,&
'frame/module_domain.f: Failed to deallocate grid%fgrnqfx. ')
 endif
  NULLIFY(grid%fgrnqfx)
ENDIF
IF ( ASSOCIATED( grid%fmc_lag ) ) THEN 
  DEALLOCATE(grid%fmc_lag,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1112,&
'frame/module_domain.f: Failed to deallocate grid%fmc_lag. ')
 endif
  NULLIFY(grid%fmc_lag)
ENDIF
IF ( ASSOCIATED( grid%ischap ) ) THEN 
  DEALLOCATE(grid%ischap,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1120,&
'frame/module_domain.f: Failed to deallocate grid%ischap. ')
 endif
  NULLIFY(grid%ischap)
ENDIF
IF ( ASSOCIATED( grid%tracer_btxs ) ) THEN 
  DEALLOCATE(grid%tracer_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1128,&
'frame/module_domain.f: Failed to deallocate grid%tracer_btxs. ')
 endif
  NULLIFY(grid%tracer_btxs)
ENDIF
IF ( ASSOCIATED( grid%tracer_btxe ) ) THEN 
  DEALLOCATE(grid%tracer_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1136,&
'frame/module_domain.f: Failed to deallocate grid%tracer_btxe. ')
 endif
  NULLIFY(grid%tracer_btxe)
ENDIF
IF ( ASSOCIATED( grid%tracer_btys ) ) THEN 
  DEALLOCATE(grid%tracer_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1144,&
'frame/module_domain.f: Failed to deallocate grid%tracer_btys. ')
 endif
  NULLIFY(grid%tracer_btys)
ENDIF
IF ( ASSOCIATED( grid%tracer_btye ) ) THEN 
  DEALLOCATE(grid%tracer_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1152,&
'frame/module_domain.f: Failed to deallocate grid%tracer_btye. ')
 endif
  NULLIFY(grid%tracer_btye)
ENDIF
IF ( ASSOCIATED( grid%fs_fuel_spotting_risk ) ) THEN 
  DEALLOCATE(grid%fs_fuel_spotting_risk,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1160,&
'frame/module_domain.f: Failed to deallocate grid%fs_fuel_spotting_risk. ')
 endif
  NULLIFY(grid%fs_fuel_spotting_risk)
ENDIF
IF ( ASSOCIATED( grid%fs_p_z ) ) THEN 
  DEALLOCATE(grid%fs_p_z,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1168,&
'frame/module_domain.f: Failed to deallocate grid%fs_p_z. ')
 endif
  NULLIFY(grid%fs_p_z)
ENDIF
IF ( ASSOCIATED( grid%avgflx_dfu1 ) ) THEN 
  DEALLOCATE(grid%avgflx_dfu1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1176,&
'frame/module_domain.f: Failed to deallocate grid%avgflx_dfu1. ')
 endif
  NULLIFY(grid%avgflx_dfu1)
ENDIF
IF ( ASSOCIATED( grid%vertstrucc ) ) THEN 
  DEALLOCATE(grid%vertstrucc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1184,&
'frame/module_domain.f: Failed to deallocate grid%vertstrucc. ')
 endif
  NULLIFY(grid%vertstrucc)
ENDIF
IF ( ASSOCIATED( grid%rstoch ) ) THEN 
  DEALLOCATE(grid%rstoch,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1192,&
'frame/module_domain.f: Failed to deallocate grid%rstoch. ')
 endif
  NULLIFY(grid%rstoch)
ENDIF
IF ( ASSOCIATED( grid%spforcc2 ) ) THEN 
  DEALLOCATE(grid%spforcc2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1200,&
'frame/module_domain.f: Failed to deallocate grid%spforcc2. ')
 endif
  NULLIFY(grid%spforcc2)
ENDIF
IF ( ASSOCIATED( grid%spptforcc ) ) THEN 
  DEALLOCATE(grid%spptforcc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1208,&
'frame/module_domain.f: Failed to deallocate grid%spptforcc. ')
 endif
  NULLIFY(grid%spptforcc)
ENDIF
IF ( ASSOCIATED( grid%rand_real_yyy ) ) THEN 
  DEALLOCATE(grid%rand_real_yyy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1216,&
'frame/module_domain.f: Failed to deallocate grid%rand_real_yyy. ')
 endif
  NULLIFY(grid%rand_real_yyy)
ENDIF
IF ( ASSOCIATED( grid%stddev_cutoff_mult3d ) ) THEN 
  DEALLOCATE(grid%stddev_cutoff_mult3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1224,&
'frame/module_domain.f: Failed to deallocate grid%stddev_cutoff_mult3d. ')
 endif
  NULLIFY(grid%stddev_cutoff_mult3d)
ENDIF
IF ( ASSOCIATED( grid%pert3d ) ) THEN 
  DEALLOCATE(grid%pert3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1232,&
'frame/module_domain.f: Failed to deallocate grid%pert3d. ')
 endif
  NULLIFY(grid%pert3d)
ENDIF
IF ( ASSOCIATED( grid%nba_rij ) ) THEN 
  DEALLOCATE(grid%nba_rij,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1240,&
'frame/module_domain.f: Failed to deallocate grid%nba_rij. ')
 endif
  NULLIFY(grid%nba_rij)
ENDIF
IF ( ASSOCIATED( grid%dlf ) ) THEN 
  DEALLOCATE(grid%dlf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1248,&
'frame/module_domain.f: Failed to deallocate grid%dlf. ')
 endif
  NULLIFY(grid%dlf)
ENDIF
IF ( ASSOCIATED( grid%zmntprpd ) ) THEN 
  DEALLOCATE(grid%zmntprpd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1256,&
'frame/module_domain.f: Failed to deallocate grid%zmntprpd. ')
 endif
  NULLIFY(grid%zmntprpd)
ENDIF
IF ( ASSOCIATED( grid%zmupgu ) ) THEN 
  DEALLOCATE(grid%zmupgu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1264,&
'frame/module_domain.f: Failed to deallocate grid%zmupgu. ')
 endif
  NULLIFY(grid%zmupgu)
ENDIF
IF ( ASSOCIATED( grid%du3d ) ) THEN 
  DEALLOCATE(grid%du3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1272,&
'frame/module_domain.f: Failed to deallocate grid%du3d. ')
 endif
  NULLIFY(grid%du3d)
ENDIF
IF ( ASSOCIATED( grid%cmfmc ) ) THEN 
  DEALLOCATE(grid%cmfmc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1280,&
'frame/module_domain.f: Failed to deallocate grid%cmfmc. ')
 endif
  NULLIFY(grid%cmfmc)
ENDIF
IF ( ASSOCIATED( grid%slflx_cu ) ) THEN 
  DEALLOCATE(grid%slflx_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1288,&
'frame/module_domain.f: Failed to deallocate grid%slflx_cu. ')
 endif
  NULLIFY(grid%slflx_cu)
ENDIF
IF ( ASSOCIATED( grid%ufrclcl_cu ) ) THEN 
  DEALLOCATE(grid%ufrclcl_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1296,&
'frame/module_domain.f: Failed to deallocate grid%ufrclcl_cu. ')
 endif
  NULLIFY(grid%ufrclcl_cu)
ENDIF
IF ( ASSOCIATED( grid%cin_cu ) ) THEN 
  DEALLOCATE(grid%cin_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1304,&
'frame/module_domain.f: Failed to deallocate grid%cin_cu. ')
 endif
  NULLIFY(grid%cin_cu)
ENDIF
IF ( ASSOCIATED( grid%thlu_cu ) ) THEN 
  DEALLOCATE(grid%thlu_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1312,&
'frame/module_domain.f: Failed to deallocate grid%thlu_cu. ')
 endif
  NULLIFY(grid%thlu_cu)
ENDIF
IF ( ASSOCIATED( grid%qiu_cu ) ) THEN 
  DEALLOCATE(grid%qiu_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1320,&
'frame/module_domain.f: Failed to deallocate grid%qiu_cu. ')
 endif
  NULLIFY(grid%qiu_cu)
ENDIF
IF ( ASSOCIATED( grid%excessu_cu ) ) THEN 
  DEALLOCATE(grid%excessu_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1328,&
'frame/module_domain.f: Failed to deallocate grid%excessu_cu. ')
 endif
  NULLIFY(grid%excessu_cu)
ENDIF
IF ( ASSOCIATED( grid%exit_ufrc_cu ) ) THEN 
  DEALLOCATE(grid%exit_ufrc_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1336,&
'frame/module_domain.f: Failed to deallocate grid%exit_ufrc_cu. ')
 endif
  NULLIFY(grid%exit_ufrc_cu)
ENDIF
IF ( ASSOCIATED( grid%limit_cinlcl_cu ) ) THEN 
  DEALLOCATE(grid%limit_cinlcl_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1344,&
'frame/module_domain.f: Failed to deallocate grid%limit_cinlcl_cu. ')
 endif
  NULLIFY(grid%limit_cinlcl_cu)
ENDIF
IF ( ASSOCIATED( grid%cldfra_conv ) ) THEN 
  DEALLOCATE(grid%cldfra_conv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1352,&
'frame/module_domain.f: Failed to deallocate grid%cldfra_conv. ')
 endif
  NULLIFY(grid%cldfra_conv)
ENDIF
IF ( ASSOCIATED( grid%snl ) ) THEN 
  DEALLOCATE(grid%snl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1360,&
'frame/module_domain.f: Failed to deallocate grid%snl. ')
 endif
  NULLIFY(grid%snl)
ENDIF
IF ( ASSOCIATED( grid%fsd24 ) ) THEN 
  DEALLOCATE(grid%fsd24,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1368,&
'frame/module_domain.f: Failed to deallocate grid%fsd24. ')
 endif
  NULLIFY(grid%fsd24)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq_s1 ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq_s1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1376,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq_s1. ')
 endif
  NULLIFY(grid%h2osoi_liq_s1)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq8 ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq8,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1384,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq8. ')
 endif
  NULLIFY(grid%h2osoi_liq8)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice5 ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice5,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1392,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice5. ')
 endif
  NULLIFY(grid%h2osoi_ice5)
ENDIF
IF ( ASSOCIATED( grid%t_soisno2 ) ) THEN 
  DEALLOCATE(grid%t_soisno2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1400,&
'frame/module_domain.f: Failed to deallocate grid%t_soisno2. ')
 endif
  NULLIFY(grid%t_soisno2)
ENDIF
IF ( ASSOCIATED( grid%dzsnow4 ) ) THEN 
  DEALLOCATE(grid%dzsnow4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1408,&
'frame/module_domain.f: Failed to deallocate grid%dzsnow4. ')
 endif
  NULLIFY(grid%dzsnow4)
ENDIF
IF ( ASSOCIATED( grid%t_lake6 ) ) THEN 
  DEALLOCATE(grid%t_lake6,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1416,&
'frame/module_domain.f: Failed to deallocate grid%t_lake6. ')
 endif
  NULLIFY(grid%t_lake6)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_vol8 ) ) THEN 
  DEALLOCATE(grid%h2osoi_vol8,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1424,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_vol8. ')
 endif
  NULLIFY(grid%h2osoi_vol8)
ENDIF
IF ( ASSOCIATED( grid%lake2d ) ) THEN 
  DEALLOCATE(grid%lake2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1432,&
'frame/module_domain.f: Failed to deallocate grid%lake2d. ')
 endif
  NULLIFY(grid%lake2d)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice3d ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1440,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice3d. ')
 endif
  NULLIFY(grid%h2osoi_ice3d)
ENDIF
IF ( ASSOCIATED( grid%ssib_egs ) ) THEN 
  DEALLOCATE(grid%ssib_egs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1448,&
'frame/module_domain.f: Failed to deallocate grid%ssib_egs. ')
 endif
  NULLIFY(grid%ssib_egs)
ENDIF
IF ( ASSOCIATED( grid%ssib_lai ) ) THEN 
  DEALLOCATE(grid%ssib_lai,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1456,&
'frame/module_domain.f: Failed to deallocate grid%ssib_lai. ')
 endif
  NULLIFY(grid%ssib_lai)
ENDIF
IF ( ASSOCIATED( grid%tssno1 ) ) THEN 
  DEALLOCATE(grid%tssno1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1464,&
'frame/module_domain.f: Failed to deallocate grid%tssno1. ')
 endif
  NULLIFY(grid%tssno1)
ENDIF
IF ( ASSOCIATED( grid%tssno2 ) ) THEN 
  DEALLOCATE(grid%tssno2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1472,&
'frame/module_domain.f: Failed to deallocate grid%tssno2. ')
 endif
  NULLIFY(grid%tssno2)
ENDIF
IF ( ASSOCIATED( grid%tssno3 ) ) THEN 
  DEALLOCATE(grid%tssno3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1480,&
'frame/module_domain.f: Failed to deallocate grid%tssno3. ')
 endif
  NULLIFY(grid%tssno3)
ENDIF
IF ( ASSOCIATED( grid%tssno4 ) ) THEN 
  DEALLOCATE(grid%tssno4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1488,&
'frame/module_domain.f: Failed to deallocate grid%tssno4. ')
 endif
  NULLIFY(grid%tssno4)
ENDIF
IF ( ASSOCIATED( grid%canicexy ) ) THEN 
  DEALLOCATE(grid%canicexy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1496,&
'frame/module_domain.f: Failed to deallocate grid%canicexy. ')
 endif
  NULLIFY(grid%canicexy)
ENDIF
IF ( ASSOCIATED( grid%zwtxy ) ) THEN 
  DEALLOCATE(grid%zwtxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1504,&
'frame/module_domain.f: Failed to deallocate grid%zwtxy. ')
 endif
  NULLIFY(grid%zwtxy)
ENDIF
IF ( ASSOCIATED( grid%fastcpxy ) ) THEN 
  DEALLOCATE(grid%fastcpxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1512,&
'frame/module_domain.f: Failed to deallocate grid%fastcpxy. ')
 endif
  NULLIFY(grid%fastcpxy)
ENDIF
IF ( ASSOCIATED( grid%qinxy ) ) THEN 
  DEALLOCATE(grid%qinxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1520,&
'frame/module_domain.f: Failed to deallocate grid%qinxy. ')
 endif
  NULLIFY(grid%qinxy)
ENDIF
IF ( ASSOCIATED( grid%rssunxy ) ) THEN 
  DEALLOCATE(grid%rssunxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1528,&
'frame/module_domain.f: Failed to deallocate grid%rssunxy. ')
 endif
  NULLIFY(grid%rssunxy)
ENDIF
IF ( ASSOCIATED( grid%evbxy ) ) THEN 
  DEALLOCATE(grid%evbxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1536,&
'frame/module_domain.f: Failed to deallocate grid%evbxy. ')
 endif
  NULLIFY(grid%evbxy)
ENDIF
IF ( ASSOCIATED( grid%chstarxy ) ) THEN 
  DEALLOCATE(grid%chstarxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1544,&
'frame/module_domain.f: Failed to deallocate grid%chstarxy. ')
 endif
  NULLIFY(grid%chstarxy)
ENDIF
IF ( ASSOCIATED( grid%qslatxy ) ) THEN 
  DEALLOCATE(grid%qslatxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1552,&
'frame/module_domain.f: Failed to deallocate grid%qslatxy. ')
 endif
  NULLIFY(grid%qslatxy)
ENDIF
IF ( ASSOCIATED( grid%qdriprxy ) ) THEN 
  DEALLOCATE(grid%qdriprxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1560,&
'frame/module_domain.f: Failed to deallocate grid%qdriprxy. ')
 endif
  NULLIFY(grid%qdriprxy)
ENDIF
IF ( ASSOCIATED( grid%qmeltxy ) ) THEN 
  DEALLOCATE(grid%qmeltxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1568,&
'frame/module_domain.f: Failed to deallocate grid%qmeltxy. ')
 endif
  NULLIFY(grid%qmeltxy)
ENDIF
IF ( ASSOCIATED( grid%soilcl2 ) ) THEN 
  DEALLOCATE(grid%soilcl2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1576,&
'frame/module_domain.f: Failed to deallocate grid%soilcl2. ')
 endif
  NULLIFY(grid%soilcl2)
ENDIF
IF ( ASSOCIATED( grid%forczlsm ) ) THEN 
  DEALLOCATE(grid%forczlsm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1584,&
'frame/module_domain.f: Failed to deallocate grid%forczlsm. ')
 endif
  NULLIFY(grid%forczlsm)
ENDIF
IF ( ASSOCIATED( grid%acsubc ) ) THEN 
  DEALLOCATE(grid%acsubc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1592,&
'frame/module_domain.f: Failed to deallocate grid%acsubc. ')
 endif
  NULLIFY(grid%acsubc)
ENDIF
IF ( ASSOCIATED( grid%acsagb ) ) THEN 
  DEALLOCATE(grid%acsagb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1600,&
'frame/module_domain.f: Failed to deallocate grid%acsagb. ')
 endif
  NULLIFY(grid%acsagb)
ENDIF
IF ( ASSOCIATED( grid%acsav ) ) THEN 
  DEALLOCATE(grid%acsav,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1608,&
'frame/module_domain.f: Failed to deallocate grid%acsav. ')
 endif
  NULLIFY(grid%acsav)
ENDIF
IF ( ASSOCIATED( grid%acghflsm ) ) THEN 
  DEALLOCATE(grid%acghflsm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1616,&
'frame/module_domain.f: Failed to deallocate grid%acghflsm. ')
 endif
  NULLIFY(grid%acghflsm)
ENDIF
IF ( ASSOCIATED( grid%acc_prcpxy ) ) THEN 
  DEALLOCATE(grid%acc_prcpxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1624,&
'frame/module_domain.f: Failed to deallocate grid%acc_prcpxy. ')
 endif
  NULLIFY(grid%acc_prcpxy)
ENDIF
IF ( ASSOCIATED( grid%gecros_state ) ) THEN 
  DEALLOCATE(grid%gecros_state,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1632,&
'frame/module_domain.f: Failed to deallocate grid%gecros_state. ')
 endif
  NULLIFY(grid%gecros_state)
ENDIF
IF ( ASSOCIATED( grid%irwatfi ) ) THEN 
  DEALLOCATE(grid%irwatfi,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1640,&
'frame/module_domain.f: Failed to deallocate grid%irwatfi. ')
 endif
  NULLIFY(grid%irwatfi)
ENDIF
IF ( ASSOCIATED( grid%kext_qh ) ) THEN 
  DEALLOCATE(grid%kext_qh,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1648,&
'frame/module_domain.f: Failed to deallocate grid%kext_qh. ')
 endif
  NULLIFY(grid%kext_qh)
ENDIF
IF ( ASSOCIATED( grid%qicing_sm ) ) THEN 
  DEALLOCATE(grid%qicing_sm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1656,&
'frame/module_domain.f: Failed to deallocate grid%qicing_sm. ')
 endif
  NULLIFY(grid%qicing_sm)
ENDIF
IF ( ASSOCIATED( grid%afwa_llturb ) ) THEN 
  DEALLOCATE(grid%afwa_llturb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1664,&
'frame/module_domain.f: Failed to deallocate grid%afwa_llturb. ')
 endif
  NULLIFY(grid%afwa_llturb)
ENDIF
IF ( ASSOCIATED( grid%afwa_vis_alpha ) ) THEN 
  DEALLOCATE(grid%afwa_vis_alpha,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1672,&
'frame/module_domain.f: Failed to deallocate grid%afwa_vis_alpha. ')
 endif
  NULLIFY(grid%afwa_vis_alpha)
ENDIF
IF ( ASSOCIATED( grid%midrh_min ) ) THEN 
  DEALLOCATE(grid%midrh_min,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1680,&
'frame/module_domain.f: Failed to deallocate grid%midrh_min. ')
 endif
  NULLIFY(grid%midrh_min)
ENDIF
IF ( ASSOCIATED( grid%v10_mean ) ) THEN 
  DEALLOCATE(grid%v10_mean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1688,&
'frame/module_domain.f: Failed to deallocate grid%v10_mean. ')
 endif
  NULLIFY(grid%v10_mean)
ENDIF
IF ( ASSOCIATED( grid%lwupb_diurn ) ) THEN 
  DEALLOCATE(grid%lwupb_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1696,&
'frame/module_domain.f: Failed to deallocate grid%lwupb_diurn. ')
 endif
  NULLIFY(grid%lwupb_diurn)
ENDIF
IF ( ASSOCIATED( grid%q2_dtmp ) ) THEN 
  DEALLOCATE(grid%q2_dtmp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1704,&
'frame/module_domain.f: Failed to deallocate grid%q2_dtmp. ')
 endif
  NULLIFY(grid%q2_dtmp)
ENDIF
IF ( ASSOCIATED( grid%lwdnt_dtmp ) ) THEN 
  DEALLOCATE(grid%lwdnt_dtmp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1712,&
'frame/module_domain.f: Failed to deallocate grid%lwdnt_dtmp. ')
 endif
  NULLIFY(grid%lwdnt_dtmp)
ENDIF
IF ( ASSOCIATED( grid%lightdens ) ) THEN 
  DEALLOCATE(grid%lightdens,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1720,&
'frame/module_domain.f: Failed to deallocate grid%lightdens. ')
 endif
  NULLIFY(grid%lightdens)
ENDIF
IF ( ASSOCIATED( grid%c4h ) ) THEN 
  DEALLOCATE(grid%c4h,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1728,&
'frame/module_domain.f: Failed to deallocate grid%c4h. ')
 endif
  NULLIFY(grid%c4h)
ENDIF
IF ( ASSOCIATED( grid%qnifa_gc ) ) THEN 
  DEALLOCATE(grid%qnifa_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1736,&
'frame/module_domain.f: Failed to deallocate grid%qnifa_gc. ')
 endif
  NULLIFY(grid%qnifa_gc)
ENDIF
IF ( ASSOCIATED( grid%p_wif_sep ) ) THEN 
  DEALLOCATE(grid%p_wif_sep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1744,&
'frame/module_domain.f: Failed to deallocate grid%p_wif_sep. ')
 endif
  NULLIFY(grid%p_wif_sep)
ENDIF
IF ( ASSOCIATED( grid%w_wif_aug ) ) THEN 
  DEALLOCATE(grid%w_wif_aug,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1752,&
'frame/module_domain.f: Failed to deallocate grid%w_wif_aug. ')
 endif
  NULLIFY(grid%w_wif_aug)
ENDIF
IF ( ASSOCIATED( grid%i_wif_jul ) ) THEN 
  DEALLOCATE(grid%i_wif_jul,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1760,&
'frame/module_domain.f: Failed to deallocate grid%i_wif_jul. ')
 endif
  NULLIFY(grid%i_wif_jul)
ENDIF
IF ( ASSOCIATED( grid%b_wif_jun ) ) THEN 
  DEALLOCATE(grid%b_wif_jun,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1768,&
'frame/module_domain.f: Failed to deallocate grid%b_wif_jun. ')
 endif
  NULLIFY(grid%b_wif_jun)
ENDIF
IF ( ASSOCIATED( grid%umet ) ) THEN 
  DEALLOCATE(grid%umet,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1776,&
'frame/module_domain.f: Failed to deallocate grid%umet. ')
 endif
  NULLIFY(grid%umet)
ENDIF
IF ( ASSOCIATED( grid%cldfrac2d ) ) THEN 
  DEALLOCATE(grid%cldfrac2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1784,&
'frame/module_domain.f: Failed to deallocate grid%cldfrac2d. ')
 endif
  NULLIFY(grid%cldfrac2d)
ENDIF
IF ( ASSOCIATED( grid%re_qc_tot ) ) THEN 
  DEALLOCATE(grid%re_qc_tot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1792,&
'frame/module_domain.f: Failed to deallocate grid%re_qc_tot. ')
 endif
  NULLIFY(grid%re_qc_tot)
ENDIF
IF ( ASSOCIATED( grid%sza ) ) THEN 
  DEALLOCATE(grid%sza,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1800,&
'frame/module_domain.f: Failed to deallocate grid%sza. ')
 endif
  NULLIFY(grid%sza)
ENDIF
IF ( ASSOCIATED( grid%ts_re_qi ) ) THEN 
  DEALLOCATE(grid%ts_re_qi,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1808,&
'frame/module_domain.f: Failed to deallocate grid%ts_re_qi. ')
 endif
  NULLIFY(grid%ts_re_qi)
ENDIF
IF ( ASSOCIATED( grid%ts_ctopht_tot ) ) THEN 
  DEALLOCATE(grid%ts_ctopht_tot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1816,&
'frame/module_domain.f: Failed to deallocate grid%ts_ctopht_tot. ')
 endif
  NULLIFY(grid%ts_ctopht_tot)
ENDIF
IF ( ASSOCIATED( grid%ts_swdownc2 ) ) THEN 
  DEALLOCATE(grid%ts_swdownc2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1824,&
'frame/module_domain.f: Failed to deallocate grid%ts_swdownc2. ')
 endif
  NULLIFY(grid%ts_swdownc2)
ENDIF
IF ( ASSOCIATED( grid%v_pl ) ) THEN 
  DEALLOCATE(grid%v_pl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1832,&
'frame/module_domain.f: Failed to deallocate grid%v_pl. ')
 endif
  NULLIFY(grid%v_pl)
ENDIF
IF ( ASSOCIATED( grid%mu_iau ) ) THEN 
  DEALLOCATE(grid%mu_iau,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1840,&
'frame/module_domain.f: Failed to deallocate grid%mu_iau. ')
 endif
  NULLIFY(grid%mu_iau)
ENDIF
IF ( ASSOCIATED( grid%qg_iau ) ) THEN 
  DEALLOCATE(grid%qg_iau,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1848,&
'frame/module_domain.f: Failed to deallocate grid%qg_iau. ')
 endif
  NULLIFY(grid%qg_iau)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k3_btxs ) ) THEN 
  DEALLOCATE(grid%sdirk3_k3_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1856,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k3_btxs. ')
 endif
  NULLIFY(grid%sdirk3_k3_btxs)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k3_btxe ) ) THEN 
  DEALLOCATE(grid%sdirk3_k3_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1864,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k3_btxe. ')
 endif
  NULLIFY(grid%sdirk3_k3_btxe)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k3_btys ) ) THEN 
  DEALLOCATE(grid%sdirk3_k3_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1872,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k3_btys. ')
 endif
  NULLIFY(grid%sdirk3_k3_btys)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k3_btye ) ) THEN 
  DEALLOCATE(grid%sdirk3_k3_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1880,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k3_btye. ')
 endif
  NULLIFY(grid%sdirk3_k3_btye)
ENDIF
END SUBROUTINE deallocs_9



