





SUBROUTINE deallocs_1( grid )
  USE module_wrf_error
  USE module_domain_type
  IMPLICIT NONE
  TYPE( domain ), POINTER :: grid
  INTEGER :: ierr
IF ( ASSOCIATED( grid%xlong ) ) THEN 
  DEALLOCATE(grid%xlong,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",16,&
'frame/module_domain.f: Failed to deallocate grid%xlong. ')
 endif
  NULLIFY(grid%xlong)
ENDIF
IF ( ASSOCIATED( grid%u_gc ) ) THEN 
  DEALLOCATE(grid%u_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",24,&
'frame/module_domain.f: Failed to deallocate grid%u_gc. ')
 endif
  NULLIFY(grid%u_gc)
ENDIF
IF ( ASSOCIATED( grid%tsk_gc ) ) THEN 
  DEALLOCATE(grid%tsk_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",32,&
'frame/module_domain.f: Failed to deallocate grid%tsk_gc. ')
 endif
  NULLIFY(grid%tsk_gc)
ENDIF
IF ( ASSOCIATED( grid%intq_gc ) ) THEN 
  DEALLOCATE(grid%intq_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",40,&
'frame/module_domain.f: Failed to deallocate grid%intq_gc. ')
 endif
  NULLIFY(grid%intq_gc)
ENDIF
IF ( ASSOCIATED( grid%qg_gc ) ) THEN 
  DEALLOCATE(grid%qg_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",48,&
'frame/module_domain.f: Failed to deallocate grid%qg_gc. ')
 endif
  NULLIFY(grid%qg_gc)
ENDIF
IF ( ASSOCIATED( grid%max_p ) ) THEN 
  DEALLOCATE(grid%max_p,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",56,&
'frame/module_domain.f: Failed to deallocate grid%max_p. ')
 endif
  NULLIFY(grid%max_p)
ENDIF
IF ( ASSOCIATED( grid%umaxw ) ) THEN 
  DEALLOCATE(grid%umaxw,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",64,&
'frame/module_domain.f: Failed to deallocate grid%umaxw. ')
 endif
  NULLIFY(grid%umaxw)
ENDIF
IF ( ASSOCIATED( grid%ru_tend ) ) THEN 
  DEALLOCATE(grid%ru_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",72,&
'frame/module_domain.f: Failed to deallocate grid%ru_tend. ')
 endif
  NULLIFY(grid%ru_tend)
ENDIF
IF ( ASSOCIATED( grid%rv_tend ) ) THEN 
  DEALLOCATE(grid%rv_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",80,&
'frame/module_domain.f: Failed to deallocate grid%rv_tend. ')
 endif
  NULLIFY(grid%rv_tend)
ENDIF
IF ( ASSOCIATED( grid%ph_1 ) ) THEN 
  DEALLOCATE(grid%ph_1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",88,&
'frame/module_domain.f: Failed to deallocate grid%ph_1. ')
 endif
  NULLIFY(grid%ph_1)
ENDIF
IF ( ASSOCIATED( grid%ph_2 ) ) THEN 
  DEALLOCATE(grid%ph_2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",96,&
'frame/module_domain.f: Failed to deallocate grid%ph_2. ')
 endif
  NULLIFY(grid%ph_2)
ENDIF
IF ( ASSOCIATED( grid%t_bxs ) ) THEN 
  DEALLOCATE(grid%t_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",104,&
'frame/module_domain.f: Failed to deallocate grid%t_bxs. ')
 endif
  NULLIFY(grid%t_bxs)
ENDIF
IF ( ASSOCIATED( grid%t_bxe ) ) THEN 
  DEALLOCATE(grid%t_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",112,&
'frame/module_domain.f: Failed to deallocate grid%t_bxe. ')
 endif
  NULLIFY(grid%t_bxe)
ENDIF
IF ( ASSOCIATED( grid%t_bys ) ) THEN 
  DEALLOCATE(grid%t_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",120,&
'frame/module_domain.f: Failed to deallocate grid%t_bys. ')
 endif
  NULLIFY(grid%t_bys)
ENDIF
IF ( ASSOCIATED( grid%t_bye ) ) THEN 
  DEALLOCATE(grid%t_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",128,&
'frame/module_domain.f: Failed to deallocate grid%t_bye. ')
 endif
  NULLIFY(grid%t_bye)
ENDIF
IF ( ASSOCIATED( grid%qv_upstream_x_tend ) ) THEN 
  DEALLOCATE(grid%qv_upstream_x_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",136,&
'frame/module_domain.f: Failed to deallocate grid%qv_upstream_x_tend. ')
 endif
  NULLIFY(grid%qv_upstream_x_tend)
ENDIF
IF ( ASSOCIATED( grid%v_upstream_x_tend ) ) THEN 
  DEALLOCATE(grid%v_upstream_x_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",144,&
'frame/module_domain.f: Failed to deallocate grid%v_upstream_x_tend. ')
 endif
  NULLIFY(grid%v_upstream_x_tend)
ENDIF
IF ( ASSOCIATED( grid%u_largescale_tend ) ) THEN 
  DEALLOCATE(grid%u_largescale_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",152,&
'frame/module_domain.f: Failed to deallocate grid%u_largescale_tend. ')
 endif
  NULLIFY(grid%u_largescale_tend)
ENDIF
IF ( ASSOCIATED( grid%q_soil_forcing_tend ) ) THEN 
  DEALLOCATE(grid%q_soil_forcing_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",160,&
'frame/module_domain.f: Failed to deallocate grid%q_soil_forcing_tend. ')
 endif
  NULLIFY(grid%q_soil_forcing_tend)
ENDIF
IF ( ASSOCIATED( grid%mub ) ) THEN 
  DEALLOCATE(grid%mub,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",168,&
'frame/module_domain.f: Failed to deallocate grid%mub. ')
 endif
  NULLIFY(grid%mub)
ENDIF
IF ( ASSOCIATED( grid%gamv ) ) THEN 
  DEALLOCATE(grid%gamv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",176,&
'frame/module_domain.f: Failed to deallocate grid%gamv. ')
 endif
  NULLIFY(grid%gamv)
ENDIF
IF ( ASSOCIATED( grid%rdzw ) ) THEN 
  DEALLOCATE(grid%rdzw,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",184,&
'frame/module_domain.f: Failed to deallocate grid%rdzw. ')
 endif
  NULLIFY(grid%rdzw)
ENDIF
IF ( ASSOCIATED( grid%q2 ) ) THEN 
  DEALLOCATE(grid%q2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",192,&
'frame/module_domain.f: Failed to deallocate grid%q2. ')
 endif
  NULLIFY(grid%q2)
ENDIF
IF ( ASSOCIATED( grid%imask_xstag ) ) THEN 
  DEALLOCATE(grid%imask_xstag,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",200,&
'frame/module_domain.f: Failed to deallocate grid%imask_xstag. ')
 endif
  NULLIFY(grid%imask_xstag)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_btxs ) ) THEN 
  DEALLOCATE(grid%dfi_moist_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",208,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_btxs. ')
 endif
  NULLIFY(grid%dfi_moist_btxs)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_btxe ) ) THEN 
  DEALLOCATE(grid%dfi_moist_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",216,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_btxe. ')
 endif
  NULLIFY(grid%dfi_moist_btxe)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_btys ) ) THEN 
  DEALLOCATE(grid%dfi_moist_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",224,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_btys. ')
 endif
  NULLIFY(grid%dfi_moist_btys)
ENDIF
IF ( ASSOCIATED( grid%dfi_moist_btye ) ) THEN 
  DEALLOCATE(grid%dfi_moist_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",232,&
'frame/module_domain.f: Failed to deallocate grid%dfi_moist_btye. ')
 endif
  NULLIFY(grid%dfi_moist_btye)
ENDIF
IF ( ASSOCIATED( grid%re_rain_gsfc ) ) THEN 
  DEALLOCATE(grid%re_rain_gsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",240,&
'frame/module_domain.f: Failed to deallocate grid%re_rain_gsfc. ')
 endif
  NULLIFY(grid%re_rain_gsfc)
ENDIF
IF ( ASSOCIATED( grid%dfi_re_graupel_gsfc ) ) THEN 
  DEALLOCATE(grid%dfi_re_graupel_gsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",248,&
'frame/module_domain.f: Failed to deallocate grid%dfi_re_graupel_gsfc. ')
 endif
  NULLIFY(grid%dfi_re_graupel_gsfc)
ENDIF
IF ( ASSOCIATED( grid%gcx ) ) THEN 
  DEALLOCATE(grid%gcx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",256,&
'frame/module_domain.f: Failed to deallocate grid%gcx. ')
 endif
  NULLIFY(grid%gcx)
ENDIF
IF ( ASSOCIATED( grid%sm100255 ) ) THEN 
  DEALLOCATE(grid%sm100255,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",264,&
'frame/module_domain.f: Failed to deallocate grid%sm100255. ')
 endif
  NULLIFY(grid%sm100255)
ENDIF
IF ( ASSOCIATED( grid%soilm020 ) ) THEN 
  DEALLOCATE(grid%soilm020,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",272,&
'frame/module_domain.f: Failed to deallocate grid%soilm020. ')
 endif
  NULLIFY(grid%soilm020)
ENDIF
IF ( ASSOCIATED( grid%soilw040 ) ) THEN 
  DEALLOCATE(grid%soilw040,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",280,&
'frame/module_domain.f: Failed to deallocate grid%soilw040. ')
 endif
  NULLIFY(grid%soilw040)
ENDIF
IF ( ASSOCIATED( grid%soilt160 ) ) THEN 
  DEALLOCATE(grid%soilt160,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",288,&
'frame/module_domain.f: Failed to deallocate grid%soilt160. ')
 endif
  NULLIFY(grid%soilt160)
ENDIF
IF ( ASSOCIATED( grid%landusef ) ) THEN 
  DEALLOCATE(grid%landusef,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",296,&
'frame/module_domain.f: Failed to deallocate grid%landusef. ')
 endif
  NULLIFY(grid%landusef)
ENDIF
IF ( ASSOCIATED( grid%ts_q ) ) THEN 
  DEALLOCATE(grid%ts_q,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",304,&
'frame/module_domain.f: Failed to deallocate grid%ts_q. ')
 endif
  NULLIFY(grid%ts_q)
ENDIF
IF ( ASSOCIATED( grid%ts_u_profile ) ) THEN 
  DEALLOCATE(grid%ts_u_profile,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",312,&
'frame/module_domain.f: Failed to deallocate grid%ts_u_profile. ')
 endif
  NULLIFY(grid%ts_u_profile)
ENDIF
IF ( ASSOCIATED( grid%hi_urb2d ) ) THEN 
  DEALLOCATE(grid%hi_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",320,&
'frame/module_domain.f: Failed to deallocate grid%hi_urb2d. ')
 endif
  NULLIFY(grid%hi_urb2d)
ENDIF
IF ( ASSOCIATED( grid%smcrel ) ) THEN 
  DEALLOCATE(grid%smcrel,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",328,&
'frame/module_domain.f: Failed to deallocate grid%smcrel. ')
 endif
  NULLIFY(grid%smcrel)
ENDIF
IF ( ASSOCIATED( grid%zwatble2d ) ) THEN 
  DEALLOCATE(grid%zwatble2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",336,&
'frame/module_domain.f: Failed to deallocate grid%zwatble2d. ')
 endif
  NULLIFY(grid%zwatble2d)
ENDIF
IF ( ASSOCIATED( grid%acsnom ) ) THEN 
  DEALLOCATE(grid%acsnom,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",344,&
'frame/module_domain.f: Failed to deallocate grid%acsnom. ')
 endif
  NULLIFY(grid%acsnom)
ENDIF
IF ( ASSOCIATED( grid%water_depth ) ) THEN 
  DEALLOCATE(grid%water_depth,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",352,&
'frame/module_domain.f: Failed to deallocate grid%water_depth. ')
 endif
  NULLIFY(grid%water_depth)
ENDIF
IF ( ASSOCIATED( grid%dfi_u ) ) THEN 
  DEALLOCATE(grid%dfi_u,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",360,&
'frame/module_domain.f: Failed to deallocate grid%dfi_u. ')
 endif
  NULLIFY(grid%dfi_u)
ENDIF
IF ( ASSOCIATED( grid%dfi_smois ) ) THEN 
  DEALLOCATE(grid%dfi_smois,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",368,&
'frame/module_domain.f: Failed to deallocate grid%dfi_smois. ')
 endif
  NULLIFY(grid%dfi_smois)
ENDIF
IF ( ASSOCIATED( grid%qc_urb2d ) ) THEN 
  DEALLOCATE(grid%qc_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",376,&
'frame/module_domain.f: Failed to deallocate grid%qc_urb2d. ')
 endif
  NULLIFY(grid%qc_urb2d)
ENDIF
IF ( ASSOCIATED( grid%flxhumg_urb2d ) ) THEN 
  DEALLOCATE(grid%flxhumg_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",384,&
'frame/module_domain.f: Failed to deallocate grid%flxhumg_urb2d. ')
 endif
  NULLIFY(grid%flxhumg_urb2d)
ENDIF
IF ( ASSOCIATED( grid%utype_urb2d ) ) THEN 
  DEALLOCATE(grid%utype_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",392,&
'frame/module_domain.f: Failed to deallocate grid%utype_urb2d. ')
 endif
  NULLIFY(grid%utype_urb2d)
ENDIF
IF ( ASSOCIATED( grid%lf_ac_urb3d ) ) THEN 
  DEALLOCATE(grid%lf_ac_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",400,&
'frame/module_domain.f: Failed to deallocate grid%lf_ac_urb3d. ')
 endif
  NULLIFY(grid%lf_ac_urb3d)
ENDIF
IF ( ASSOCIATED( grid%trv_urb4d ) ) THEN 
  DEALLOCATE(grid%trv_urb4d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",408,&
'frame/module_domain.f: Failed to deallocate grid%trv_urb4d. ')
 endif
  NULLIFY(grid%trv_urb4d)
ENDIF
IF ( ASSOCIATED( grid%cmr_sfcdif ) ) THEN 
  DEALLOCATE(grid%cmr_sfcdif,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",416,&
'frame/module_domain.f: Failed to deallocate grid%cmr_sfcdif. ')
 endif
  NULLIFY(grid%cmr_sfcdif)
ENDIF
IF ( ASSOCIATED( grid%alswvisdif ) ) THEN 
  DEALLOCATE(grid%alswvisdif,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",424,&
'frame/module_domain.f: Failed to deallocate grid%alswvisdif. ')
 endif
  NULLIFY(grid%alswvisdif)
ENDIF
IF ( ASSOCIATED( grid%wwlt_px ) ) THEN 
  DEALLOCATE(grid%wwlt_px,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",432,&
'frame/module_domain.f: Failed to deallocate grid%wwlt_px. ')
 endif
  NULLIFY(grid%wwlt_px)
ENDIF
IF ( ASSOCIATED( grid%br ) ) THEN 
  DEALLOCATE(grid%br,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",440,&
'frame/module_domain.f: Failed to deallocate grid%br. ')
 endif
  NULLIFY(grid%br)
ENDIF
IF ( ASSOCIATED( grid%uz0 ) ) THEN 
  DEALLOCATE(grid%uz0,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",448,&
'frame/module_domain.f: Failed to deallocate grid%uz0. ')
 endif
  NULLIFY(grid%uz0)
ENDIF
IF ( ASSOCIATED( grid%th10 ) ) THEN 
  DEALLOCATE(grid%th10,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",456,&
'frame/module_domain.f: Failed to deallocate grid%th10. ')
 endif
  NULLIFY(grid%th10)
ENDIF
IF ( ASSOCIATED( grid%v_up ) ) THEN 
  DEALLOCATE(grid%v_up,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",464,&
'frame/module_domain.f: Failed to deallocate grid%v_up. ')
 endif
  NULLIFY(grid%v_up)
ENDIF
IF ( ASSOCIATED( grid%thup_temf ) ) THEN 
  DEALLOCATE(grid%thup_temf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",472,&
'frame/module_domain.f: Failed to deallocate grid%thup_temf. ')
 endif
  NULLIFY(grid%thup_temf)
ENDIF
IF ( ASSOCIATED( grid%qdiss ) ) THEN 
  DEALLOCATE(grid%qdiss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",480,&
'frame/module_domain.f: Failed to deallocate grid%qdiss. ')
 endif
  NULLIFY(grid%qdiss)
ENDIF
IF ( ASSOCIATED( grid%edmf_qt ) ) THEN 
  DEALLOCATE(grid%edmf_qt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",488,&
'frame/module_domain.f: Failed to deallocate grid%edmf_qt. ')
 endif
  NULLIFY(grid%edmf_qt)
ENDIF
IF ( ASSOCIATED( grid%vdfg ) ) THEN 
  DEALLOCATE(grid%vdfg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",496,&
'frame/module_domain.f: Failed to deallocate grid%vdfg. ')
 endif
  NULLIFY(grid%vdfg)
ENDIF
IF ( ASSOCIATED( grid%ol1 ) ) THEN 
  DEALLOCATE(grid%ol1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",504,&
'frame/module_domain.f: Failed to deallocate grid%ol1. ')
 endif
  NULLIFY(grid%ol1)
ENDIF
IF ( ASSOCIATED( grid%dusfcg_ls ) ) THEN 
  DEALLOCATE(grid%dusfcg_ls,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",512,&
'frame/module_domain.f: Failed to deallocate grid%dusfcg_ls. ')
 endif
  NULLIFY(grid%dusfcg_ls)
ENDIF
IF ( ASSOCIATED( grid%oa3ls ) ) THEN 
  DEALLOCATE(grid%oa3ls,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",520,&
'frame/module_domain.f: Failed to deallocate grid%oa3ls. ')
 endif
  NULLIFY(grid%oa3ls)
ENDIF
IF ( ASSOCIATED( grid%ol1ss ) ) THEN 
  DEALLOCATE(grid%ol1ss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",528,&
'frame/module_domain.f: Failed to deallocate grid%ol1ss. ')
 endif
  NULLIFY(grid%ol1ss)
ENDIF
IF ( ASSOCIATED( grid%b_v_bep ) ) THEN 
  DEALLOCATE(grid%b_v_bep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",536,&
'frame/module_domain.f: Failed to deallocate grid%b_v_bep. ')
 endif
  NULLIFY(grid%b_v_bep)
ENDIF
IF ( ASSOCIATED( grid%pr_pbl ) ) THEN 
  DEALLOCATE(grid%pr_pbl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",544,&
'frame/module_domain.f: Failed to deallocate grid%pr_pbl. ')
 endif
  NULLIFY(grid%pr_pbl)
ENDIF
IF ( ASSOCIATED( grid%rswtoa ) ) THEN 
  DEALLOCATE(grid%rswtoa,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",552,&
'frame/module_domain.f: Failed to deallocate grid%rswtoa. ')
 endif
  NULLIFY(grid%rswtoa)
ENDIF
IF ( ASSOCIATED( grid%aerodm ) ) THEN 
  DEALLOCATE(grid%aerodm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",560,&
'frame/module_domain.f: Failed to deallocate grid%aerodm. ')
 endif
  NULLIFY(grid%aerodm)
ENDIF
IF ( ASSOCIATED( grid%m_hybi ) ) THEN 
  DEALLOCATE(grid%m_hybi,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",568,&
'frame/module_domain.f: Failed to deallocate grid%m_hybi. ')
 endif
  NULLIFY(grid%m_hybi)
ENDIF
IF ( ASSOCIATED( grid%om_ml ) ) THEN 
  DEALLOCATE(grid%om_ml,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",576,&
'frame/module_domain.f: Failed to deallocate grid%om_ml. ')
 endif
  NULLIFY(grid%om_ml)
ENDIF
IF ( ASSOCIATED( grid%wcloudbase ) ) THEN 
  DEALLOCATE(grid%wcloudbase,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",584,&
'frame/module_domain.f: Failed to deallocate grid%wcloudbase. ')
 endif
  NULLIFY(grid%wcloudbase)
ENDIF
IF ( ASSOCIATED( grid%mfup_ent_cup ) ) THEN 
  DEALLOCATE(grid%mfup_ent_cup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",592,&
'frame/module_domain.f: Failed to deallocate grid%mfup_ent_cup. ')
 endif
  NULLIFY(grid%mfup_ent_cup)
ENDIF
IF ( ASSOCIATED( grid%msft ) ) THEN 
  DEALLOCATE(grid%msft,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",600,&
'frame/module_domain.f: Failed to deallocate grid%msft. ')
 endif
  NULLIFY(grid%msft)
ENDIF
IF ( ASSOCIATED( grid%sina ) ) THEN 
  DEALLOCATE(grid%sina,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",608,&
'frame/module_domain.f: Failed to deallocate grid%sina. ')
 endif
  NULLIFY(grid%sina)
ENDIF
IF ( ASSOCIATED( grid%physs ) ) THEN 
  DEALLOCATE(grid%physs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",616,&
'frame/module_domain.f: Failed to deallocate grid%physs. ')
 endif
  NULLIFY(grid%physs)
ENDIF
IF ( ASSOCIATED( grid%precg3d ) ) THEN 
  DEALLOCATE(grid%precg3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",624,&
'frame/module_domain.f: Failed to deallocate grid%precg3d. ')
 endif
  NULLIFY(grid%precg3d)
ENDIF
IF ( ASSOCIATED( grid%ctop2d_out ) ) THEN 
  DEALLOCATE(grid%ctop2d_out,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",632,&
'frame/module_domain.f: Failed to deallocate grid%ctop2d_out. ')
 endif
  NULLIFY(grid%ctop2d_out)
ENDIF
IF ( ASSOCIATED( grid%rdcashten ) ) THEN 
  DEALLOCATE(grid%rdcashten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",640,&
'frame/module_domain.f: Failed to deallocate grid%rdcashten. ')
 endif
  NULLIFY(grid%rdcashten)
ENDIF
IF ( ASSOCIATED( grid%rainshvb ) ) THEN 
  DEALLOCATE(grid%rainshvb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",648,&
'frame/module_domain.f: Failed to deallocate grid%rainshvb. ')
 endif
  NULLIFY(grid%rainshvb)
ENDIF
IF ( ASSOCIATED( grid%rvcuten ) ) THEN 
  DEALLOCATE(grid%rvcuten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",656,&
'frame/module_domain.f: Failed to deallocate grid%rvcuten. ')
 endif
  NULLIFY(grid%rvcuten)
ENDIF
IF ( ASSOCIATED( grid%rainc ) ) THEN 
  DEALLOCATE(grid%rainc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",664,&
'frame/module_domain.f: Failed to deallocate grid%rainc. ')
 endif
  NULLIFY(grid%rainc)
ENDIF
IF ( ASSOCIATED( grid%graupelnc ) ) THEN 
  DEALLOCATE(grid%graupelnc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",672,&
'frame/module_domain.f: Failed to deallocate grid%graupelnc. ')
 endif
  NULLIFY(grid%graupelnc)
ENDIF
IF ( ASSOCIATED( grid%phii3d ) ) THEN 
  DEALLOCATE(grid%phii3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",680,&
'frame/module_domain.f: Failed to deallocate grid%phii3d. ')
 endif
  NULLIFY(grid%phii3d)
ENDIF
IF ( ASSOCIATED( grid%ssat ) ) THEN 
  DEALLOCATE(grid%ssat,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",688,&
'frame/module_domain.f: Failed to deallocate grid%ssat. ')
 endif
  NULLIFY(grid%ssat)
ENDIF
IF ( ASSOCIATED( grid%apr_gr ) ) THEN 
  DEALLOCATE(grid%apr_gr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",696,&
'frame/module_domain.f: Failed to deallocate grid%apr_gr. ')
 endif
  NULLIFY(grid%apr_gr)
ENDIF
IF ( ASSOCIATED( grid%ktop_shallow ) ) THEN 
  DEALLOCATE(grid%ktop_shallow,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",704,&
'frame/module_domain.f: Failed to deallocate grid%ktop_shallow. ')
 endif
  NULLIFY(grid%ktop_shallow)
ENDIF
IF ( ASSOCIATED( grid%gd_cloud2 ) ) THEN 
  DEALLOCATE(grid%gd_cloud2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",712,&
'frame/module_domain.f: Failed to deallocate grid%gd_cloud2. ')
 endif
  NULLIFY(grid%gd_cloud2)
ENDIF
IF ( ASSOCIATED( grid%nr_cu ) ) THEN 
  DEALLOCATE(grid%nr_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",720,&
'frame/module_domain.f: Failed to deallocate grid%nr_cu. ')
 endif
  NULLIFY(grid%nr_cu)
ENDIF
IF ( ASSOCIATED( grid%ccn2_gs ) ) THEN 
  DEALLOCATE(grid%ccn2_gs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",728,&
'frame/module_domain.f: Failed to deallocate grid%ccn2_gs. ')
 endif
  NULLIFY(grid%ccn2_gs)
ENDIF
IF ( ASSOCIATED( grid%rthraten ) ) THEN 
  DEALLOCATE(grid%rthraten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",736,&
'frame/module_domain.f: Failed to deallocate grid%rthraten. ')
 endif
  NULLIFY(grid%rthraten)
ENDIF
IF ( ASSOCIATED( grid%swdown2 ) ) THEN 
  DEALLOCATE(grid%swdown2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",744,&
'frame/module_domain.f: Failed to deallocate grid%swdown2. ')
 endif
  NULLIFY(grid%swdown2)
ENDIF
IF ( ASSOCIATED( grid%swddnic ) ) THEN 
  DEALLOCATE(grid%swddnic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",752,&
'frame/module_domain.f: Failed to deallocate grid%swddnic. ')
 endif
  NULLIFY(grid%swddnic)
ENDIF
IF ( ASSOCIATED( grid%angexp2d ) ) THEN 
  DEALLOCATE(grid%angexp2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",760,&
'frame/module_domain.f: Failed to deallocate grid%angexp2d. ')
 endif
  NULLIFY(grid%angexp2d)
ENDIF
IF ( ASSOCIATED( grid%q2min ) ) THEN 
  DEALLOCATE(grid%q2min,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",768,&
'frame/module_domain.f: Failed to deallocate grid%q2min. ')
 endif
  NULLIFY(grid%q2min)
ENDIF
IF ( ASSOCIATED( grid%u10max ) ) THEN 
  DEALLOCATE(grid%u10max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",776,&
'frame/module_domain.f: Failed to deallocate grid%u10max. ')
 endif
  NULLIFY(grid%u10max)
ENDIF
IF ( ASSOCIATED( grid%traincvmax ) ) THEN 
  DEALLOCATE(grid%traincvmax,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",784,&
'frame/module_domain.f: Failed to deallocate grid%traincvmax. ')
 endif
  NULLIFY(grid%traincvmax)
ENDIF
IF ( ASSOCIATED( grid%acswupbc ) ) THEN 
  DEALLOCATE(grid%acswupbc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",792,&
'frame/module_domain.f: Failed to deallocate grid%acswupbc. ')
 endif
  NULLIFY(grid%acswupbc)
ENDIF
IF ( ASSOCIATED( grid%i_acswuptc ) ) THEN 
  DEALLOCATE(grid%i_acswuptc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",800,&
'frame/module_domain.f: Failed to deallocate grid%i_acswuptc. ')
 endif
  NULLIFY(grid%i_acswuptc)
ENDIF
IF ( ASSOCIATED( grid%i_aclwupbc ) ) THEN 
  DEALLOCATE(grid%i_aclwupbc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",808,&
'frame/module_domain.f: Failed to deallocate grid%i_aclwupbc. ')
 endif
  NULLIFY(grid%i_aclwupbc)
ENDIF
IF ( ASSOCIATED( grid%swdnb ) ) THEN 
  DEALLOCATE(grid%swdnb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",816,&
'frame/module_domain.f: Failed to deallocate grid%swdnb. ')
 endif
  NULLIFY(grid%swdnb)
ENDIF
IF ( ASSOCIATED( grid%lwdnb ) ) THEN 
  DEALLOCATE(grid%lwdnb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",824,&
'frame/module_domain.f: Failed to deallocate grid%lwdnb. ')
 endif
  NULLIFY(grid%lwdnb)
ENDIF
IF ( ASSOCIATED( grid%albbck ) ) THEN 
  DEALLOCATE(grid%albbck,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",832,&
'frame/module_domain.f: Failed to deallocate grid%albbck. ')
 endif
  NULLIFY(grid%albbck)
ENDIF
IF ( ASSOCIATED( grid%rqiblten ) ) THEN 
  DEALLOCATE(grid%rqiblten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",840,&
'frame/module_domain.f: Failed to deallocate grid%rqiblten. ')
 endif
  NULLIFY(grid%rqiblten)
ENDIF
IF ( ASSOCIATED( grid%snow_mosaic ) ) THEN 
  DEALLOCATE(grid%snow_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",848,&
'frame/module_domain.f: Failed to deallocate grid%snow_mosaic. ')
 endif
  NULLIFY(grid%snow_mosaic)
ENDIF
IF ( ASSOCIATED( grid%qfx_mosaic ) ) THEN 
  DEALLOCATE(grid%qfx_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",856,&
'frame/module_domain.f: Failed to deallocate grid%qfx_mosaic. ')
 endif
  NULLIFY(grid%qfx_mosaic)
ENDIF
IF ( ASSOCIATED( grid%trl_urb3d_mosaic ) ) THEN 
  DEALLOCATE(grid%trl_urb3d_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",864,&
'frame/module_domain.f: Failed to deallocate grid%trl_urb3d_mosaic. ')
 endif
  NULLIFY(grid%trl_urb3d_mosaic)
ENDIF
IF ( ASSOCIATED( grid%znt ) ) THEN 
  DEALLOCATE(grid%znt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",872,&
'frame/module_domain.f: Failed to deallocate grid%znt. ')
 endif
  NULLIFY(grid%znt)
ENDIF
IF ( ASSOCIATED( grid%thc ) ) THEN 
  DEALLOCATE(grid%thc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",880,&
'frame/module_domain.f: Failed to deallocate grid%thc. ')
 endif
  NULLIFY(grid%thc)
ENDIF
IF ( ASSOCIATED( grid%qcg ) ) THEN 
  DEALLOCATE(grid%qcg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",888,&
'frame/module_domain.f: Failed to deallocate grid%qcg. ')
 endif
  NULLIFY(grid%qcg)
ENDIF
IF ( ASSOCIATED( grid%potevp ) ) THEN 
  DEALLOCATE(grid%potevp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",896,&
'frame/module_domain.f: Failed to deallocate grid%potevp. ')
 endif
  NULLIFY(grid%potevp)
ENDIF
IF ( ASSOCIATED( grid%xkmv ) ) THEN 
  DEALLOCATE(grid%xkmv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",904,&
'frame/module_domain.f: Failed to deallocate grid%xkmv. ')
 endif
  NULLIFY(grid%xkmv)
ENDIF
IF ( ASSOCIATED( grid%rthndgdten ) ) THEN 
  DEALLOCATE(grid%rthndgdten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",912,&
'frame/module_domain.f: Failed to deallocate grid%rthndgdten. ')
 endif
  NULLIFY(grid%rthndgdten)
ENDIF
IF ( ASSOCIATED( grid%th2_ndg_old ) ) THEN 
  DEALLOCATE(grid%th2_ndg_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",920,&
'frame/module_domain.f: Failed to deallocate grid%th2_ndg_old. ')
 endif
  NULLIFY(grid%th2_ndg_old)
ENDIF
IF ( ASSOCIATED( grid%tob_ndg_new ) ) THEN 
  DEALLOCATE(grid%tob_ndg_new,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",928,&
'frame/module_domain.f: Failed to deallocate grid%tob_ndg_new. ')
 endif
  NULLIFY(grid%tob_ndg_new)
ENDIF
IF ( ASSOCIATED( grid%absnxt ) ) THEN 
  DEALLOCATE(grid%absnxt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",936,&
'frame/module_domain.f: Failed to deallocate grid%absnxt. ')
 endif
  NULLIFY(grid%absnxt)
ENDIF
IF ( ASSOCIATED( grid%grpl_max ) ) THEN 
  DEALLOCATE(grid%grpl_max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",944,&
'frame/module_domain.f: Failed to deallocate grid%grpl_max. ')
 endif
  NULLIFY(grid%grpl_max)
ENDIF
IF ( ASSOCIATED( grid%advz_t ) ) THEN 
  DEALLOCATE(grid%advz_t,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",952,&
'frame/module_domain.f: Failed to deallocate grid%advz_t. ')
 endif
  NULLIFY(grid%advz_t)
ENDIF
IF ( ASSOCIATED( grid%track_v ) ) THEN 
  DEALLOCATE(grid%track_v,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",960,&
'frame/module_domain.f: Failed to deallocate grid%track_v. ')
 endif
  NULLIFY(grid%track_v)
ENDIF
IF ( ASSOCIATED( grid%brtemp ) ) THEN 
  DEALLOCATE(grid%brtemp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",968,&
'frame/module_domain.f: Failed to deallocate grid%brtemp. ')
 endif
  NULLIFY(grid%brtemp)
ENDIF
IF ( ASSOCIATED( grid%aushten ) ) THEN 
  DEALLOCATE(grid%aushten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",976,&
'frame/module_domain.f: Failed to deallocate grid%aushten. ')
 endif
  NULLIFY(grid%aushten)
ENDIF
IF ( ASSOCIATED( grid%hailcast_wup_mask ) ) THEN 
  DEALLOCATE(grid%hailcast_wup_mask,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",984,&
'frame/module_domain.f: Failed to deallocate grid%hailcast_wup_mask. ')
 endif
  NULLIFY(grid%hailcast_wup_mask)
ENDIF
IF ( ASSOCIATED( grid%ru_xxx ) ) THEN 
  DEALLOCATE(grid%ru_xxx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",992,&
'frame/module_domain.f: Failed to deallocate grid%ru_xxx. ')
 endif
  NULLIFY(grid%ru_xxx)
ENDIF
IF ( ASSOCIATED( grid%dif_xxx ) ) THEN 
  DEALLOCATE(grid%dif_xxx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1000,&
'frame/module_domain.f: Failed to deallocate grid%dif_xxx. ')
 endif
  NULLIFY(grid%dif_xxx)
ENDIF
IF ( ASSOCIATED( grid%k1_v_bxs ) ) THEN 
  DEALLOCATE(grid%k1_v_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1008,&
'frame/module_domain.f: Failed to deallocate grid%k1_v_bxs. ')
 endif
  NULLIFY(grid%k1_v_bxs)
ENDIF
IF ( ASSOCIATED( grid%k1_v_bxe ) ) THEN 
  DEALLOCATE(grid%k1_v_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1016,&
'frame/module_domain.f: Failed to deallocate grid%k1_v_bxe. ')
 endif
  NULLIFY(grid%k1_v_bxe)
ENDIF
IF ( ASSOCIATED( grid%k1_v_bys ) ) THEN 
  DEALLOCATE(grid%k1_v_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1024,&
'frame/module_domain.f: Failed to deallocate grid%k1_v_bys. ')
 endif
  NULLIFY(grid%k1_v_bys)
ENDIF
IF ( ASSOCIATED( grid%k1_v_bye ) ) THEN 
  DEALLOCATE(grid%k1_v_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1032,&
'frame/module_domain.f: Failed to deallocate grid%k1_v_bye. ')
 endif
  NULLIFY(grid%k1_v_bye)
ENDIF
IF ( ASSOCIATED( grid%k1_mu_bxs ) ) THEN 
  DEALLOCATE(grid%k1_mu_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1040,&
'frame/module_domain.f: Failed to deallocate grid%k1_mu_bxs. ')
 endif
  NULLIFY(grid%k1_mu_bxs)
ENDIF
IF ( ASSOCIATED( grid%k1_mu_bxe ) ) THEN 
  DEALLOCATE(grid%k1_mu_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1048,&
'frame/module_domain.f: Failed to deallocate grid%k1_mu_bxe. ')
 endif
  NULLIFY(grid%k1_mu_bxe)
ENDIF
IF ( ASSOCIATED( grid%k1_mu_bys ) ) THEN 
  DEALLOCATE(grid%k1_mu_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1056,&
'frame/module_domain.f: Failed to deallocate grid%k1_mu_bys. ')
 endif
  NULLIFY(grid%k1_mu_bys)
ENDIF
IF ( ASSOCIATED( grid%k1_mu_bye ) ) THEN 
  DEALLOCATE(grid%k1_mu_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1064,&
'frame/module_domain.f: Failed to deallocate grid%k1_mu_bye. ')
 endif
  NULLIFY(grid%k1_mu_bye)
ENDIF
IF ( ASSOCIATED( grid%k2_t_bxs ) ) THEN 
  DEALLOCATE(grid%k2_t_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1072,&
'frame/module_domain.f: Failed to deallocate grid%k2_t_bxs. ')
 endif
  NULLIFY(grid%k2_t_bxs)
ENDIF
IF ( ASSOCIATED( grid%k2_t_bxe ) ) THEN 
  DEALLOCATE(grid%k2_t_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1080,&
'frame/module_domain.f: Failed to deallocate grid%k2_t_bxe. ')
 endif
  NULLIFY(grid%k2_t_bxe)
ENDIF
IF ( ASSOCIATED( grid%k2_t_bys ) ) THEN 
  DEALLOCATE(grid%k2_t_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1088,&
'frame/module_domain.f: Failed to deallocate grid%k2_t_bys. ')
 endif
  NULLIFY(grid%k2_t_bys)
ENDIF
IF ( ASSOCIATED( grid%k2_t_bye ) ) THEN 
  DEALLOCATE(grid%k2_t_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1096,&
'frame/module_domain.f: Failed to deallocate grid%k2_t_bye. ')
 endif
  NULLIFY(grid%k2_t_bye)
ENDIF
IF ( ASSOCIATED( grid%k3_v_bxs ) ) THEN 
  DEALLOCATE(grid%k3_v_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1104,&
'frame/module_domain.f: Failed to deallocate grid%k3_v_bxs. ')
 endif
  NULLIFY(grid%k3_v_bxs)
ENDIF
IF ( ASSOCIATED( grid%k3_v_bxe ) ) THEN 
  DEALLOCATE(grid%k3_v_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1112,&
'frame/module_domain.f: Failed to deallocate grid%k3_v_bxe. ')
 endif
  NULLIFY(grid%k3_v_bxe)
ENDIF
IF ( ASSOCIATED( grid%k3_v_bys ) ) THEN 
  DEALLOCATE(grid%k3_v_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1120,&
'frame/module_domain.f: Failed to deallocate grid%k3_v_bys. ')
 endif
  NULLIFY(grid%k3_v_bys)
ENDIF
IF ( ASSOCIATED( grid%k3_v_bye ) ) THEN 
  DEALLOCATE(grid%k3_v_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1128,&
'frame/module_domain.f: Failed to deallocate grid%k3_v_bye. ')
 endif
  NULLIFY(grid%k3_v_bye)
ENDIF
IF ( ASSOCIATED( grid%k3_mu_bxs ) ) THEN 
  DEALLOCATE(grid%k3_mu_bxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1136,&
'frame/module_domain.f: Failed to deallocate grid%k3_mu_bxs. ')
 endif
  NULLIFY(grid%k3_mu_bxs)
ENDIF
IF ( ASSOCIATED( grid%k3_mu_bxe ) ) THEN 
  DEALLOCATE(grid%k3_mu_bxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1144,&
'frame/module_domain.f: Failed to deallocate grid%k3_mu_bxe. ')
 endif
  NULLIFY(grid%k3_mu_bxe)
ENDIF
IF ( ASSOCIATED( grid%k3_mu_bys ) ) THEN 
  DEALLOCATE(grid%k3_mu_bys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1152,&
'frame/module_domain.f: Failed to deallocate grid%k3_mu_bys. ')
 endif
  NULLIFY(grid%k3_mu_bys)
ENDIF
IF ( ASSOCIATED( grid%k3_mu_bye ) ) THEN 
  DEALLOCATE(grid%k3_mu_bye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1160,&
'frame/module_domain.f: Failed to deallocate grid%k3_mu_bye. ')
 endif
  NULLIFY(grid%k3_mu_bye)
ENDIF
IF ( ASSOCIATED( grid%grnqfx ) ) THEN 
  DEALLOCATE(grid%grnqfx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1168,&
'frame/module_domain.f: Failed to deallocate grid%grnqfx. ')
 endif
  NULLIFY(grid%grnqfx)
ENDIF
IF ( ASSOCIATED( grid%lfn_s1 ) ) THEN 
  DEALLOCATE(grid%lfn_s1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1176,&
'frame/module_domain.f: Failed to deallocate grid%lfn_s1. ')
 endif
  NULLIFY(grid%lfn_s1)
ENDIF
IF ( ASSOCIATED( grid%burnt_area_dt ) ) THEN 
  DEALLOCATE(grid%burnt_area_dt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1184,&
'frame/module_domain.f: Failed to deallocate grid%burnt_area_dt. ')
 endif
  NULLIFY(grid%burnt_area_dt)
ENDIF
IF ( ASSOCIATED( grid%psfc_old ) ) THEN 
  DEALLOCATE(grid%psfc_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1192,&
'frame/module_domain.f: Failed to deallocate grid%psfc_old. ')
 endif
  NULLIFY(grid%psfc_old)
ENDIF
IF ( ASSOCIATED( grid%fxlong ) ) THEN 
  DEALLOCATE(grid%fxlong,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1200,&
'frame/module_domain.f: Failed to deallocate grid%fxlong. ')
 endif
  NULLIFY(grid%fxlong)
ENDIF
IF ( ASSOCIATED( grid%fs_gen_inst ) ) THEN 
  DEALLOCATE(grid%fs_gen_inst,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1208,&
'frame/module_domain.f: Failed to deallocate grid%fs_gen_inst. ')
 endif
  NULLIFY(grid%fs_gen_inst)
ENDIF
IF ( ASSOCIATED( grid%fs_p_temp ) ) THEN 
  DEALLOCATE(grid%fs_p_temp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1216,&
'frame/module_domain.f: Failed to deallocate grid%fs_p_temp. ')
 endif
  NULLIFY(grid%fs_p_temp)
ENDIF
IF ( ASSOCIATED( grid%cfu1 ) ) THEN 
  DEALLOCATE(grid%cfu1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1224,&
'frame/module_domain.f: Failed to deallocate grid%cfu1. ')
 endif
  NULLIFY(grid%cfu1)
ENDIF
IF ( ASSOCIATED( grid%field_conv ) ) THEN 
  DEALLOCATE(grid%field_conv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1232,&
'frame/module_domain.f: Failed to deallocate grid%field_conv. ')
 endif
  NULLIFY(grid%field_conv)
ENDIF
IF ( ASSOCIATED( grid%spstreamforcs ) ) THEN 
  DEALLOCATE(grid%spstreamforcs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1240,&
'frame/module_domain.f: Failed to deallocate grid%spstreamforcs. ')
 endif
  NULLIFY(grid%spstreamforcs)
ENDIF
IF ( ASSOCIATED( grid%spforcs3 ) ) THEN 
  DEALLOCATE(grid%spforcs3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1248,&
'frame/module_domain.f: Failed to deallocate grid%spforcs3. ')
 endif
  NULLIFY(grid%spforcs3)
ENDIF
IF ( ASSOCIATED( grid%vertampuv ) ) THEN 
  DEALLOCATE(grid%vertampuv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1256,&
'frame/module_domain.f: Failed to deallocate grid%vertampuv. ')
 endif
  NULLIFY(grid%vertampuv)
ENDIF
IF ( ASSOCIATED( grid%iseed_mult3d ) ) THEN 
  DEALLOCATE(grid%iseed_mult3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1264,&
'frame/module_domain.f: Failed to deallocate grid%iseed_mult3d. ')
 endif
  NULLIFY(grid%iseed_mult3d)
ENDIF
IF ( ASSOCIATED( grid%tauresy2d ) ) THEN 
  DEALLOCATE(grid%tauresy2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1272,&
'frame/module_domain.f: Failed to deallocate grid%tauresy2d. ')
 endif
  NULLIFY(grid%tauresy2d)
ENDIF
IF ( ASSOCIATED( grid%zmdice ) ) THEN 
  DEALLOCATE(grid%zmdice,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1280,&
'frame/module_domain.f: Failed to deallocate grid%zmdice. ')
 endif
  NULLIFY(grid%zmdice)
ENDIF
IF ( ASSOCIATED( grid%preccdzm ) ) THEN 
  DEALLOCATE(grid%preccdzm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1288,&
'frame/module_domain.f: Failed to deallocate grid%preccdzm. ')
 endif
  NULLIFY(grid%preccdzm)
ENDIF
IF ( ASSOCIATED( grid%zmicuu ) ) THEN 
  DEALLOCATE(grid%zmicuu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1296,&
'frame/module_domain.f: Failed to deallocate grid%zmicuu. ')
 endif
  NULLIFY(grid%zmicuu)
ENDIF
IF ( ASSOCIATED( grid%mu3d ) ) THEN 
  DEALLOCATE(grid%mu3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1304,&
'frame/module_domain.f: Failed to deallocate grid%mu3d. ')
 endif
  NULLIFY(grid%mu3d)
ENDIF
IF ( ASSOCIATED( grid%evapcsh ) ) THEN 
  DEALLOCATE(grid%evapcsh,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1312,&
'frame/module_domain.f: Failed to deallocate grid%evapcsh. ')
 endif
  NULLIFY(grid%evapcsh)
ENDIF
IF ( ASSOCIATED( grid%slten_cu ) ) THEN 
  DEALLOCATE(grid%slten_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1320,&
'frame/module_domain.f: Failed to deallocate grid%slten_cu. ')
 endif
  NULLIFY(grid%slten_cu)
ENDIF
IF ( ASSOCIATED( grid%pinv_cu ) ) THEN 
  DEALLOCATE(grid%pinv_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1328,&
'frame/module_domain.f: Failed to deallocate grid%pinv_cu. ')
 endif
  NULLIFY(grid%pinv_cu)
ENDIF
IF ( ASSOCIATED( grid%zinv_cu ) ) THEN 
  DEALLOCATE(grid%zinv_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1336,&
'frame/module_domain.f: Failed to deallocate grid%zinv_cu. ')
 endif
  NULLIFY(grid%zinv_cu)
ENDIF
IF ( ASSOCIATED( grid%qtu_emf_cu ) ) THEN 
  DEALLOCATE(grid%qtu_emf_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1344,&
'frame/module_domain.f: Failed to deallocate grid%qtu_emf_cu. ')
 endif
  NULLIFY(grid%qtu_emf_cu)
ENDIF
IF ( ASSOCIATED( grid%dwten_cu ) ) THEN 
  DEALLOCATE(grid%dwten_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1352,&
'frame/module_domain.f: Failed to deallocate grid%dwten_cu. ')
 endif
  NULLIFY(grid%dwten_cu)
ENDIF
IF ( ASSOCIATED( grid%bquad_cu ) ) THEN 
  DEALLOCATE(grid%bquad_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1360,&
'frame/module_domain.f: Failed to deallocate grid%bquad_cu. ')
 endif
  NULLIFY(grid%bquad_cu)
ENDIF
IF ( ASSOCIATED( grid%exit_cufliter_cu ) ) THEN 
  DEALLOCATE(grid%exit_cufliter_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1368,&
'frame/module_domain.f: Failed to deallocate grid%exit_cufliter_cu. ')
 endif
  NULLIFY(grid%exit_cufliter_cu)
ENDIF
IF ( ASSOCIATED( grid%ind_delcin_cu ) ) THEN 
  DEALLOCATE(grid%ind_delcin_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1376,&
'frame/module_domain.f: Failed to deallocate grid%ind_delcin_cu. ')
 endif
  NULLIFY(grid%ind_delcin_cu)
ENDIF
IF ( ASSOCIATED( grid%numc ) ) THEN 
  DEALLOCATE(grid%numc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1384,&
'frame/module_domain.f: Failed to deallocate grid%numc. ')
 endif
  NULLIFY(grid%numc)
ENDIF
IF ( ASSOCIATED( grid%h2osno ) ) THEN 
  DEALLOCATE(grid%h2osno,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1392,&
'frame/module_domain.f: Failed to deallocate grid%h2osno. ')
 endif
  NULLIFY(grid%h2osno)
ENDIF
IF ( ASSOCIATED( grid%laip ) ) THEN 
  DEALLOCATE(grid%laip,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1400,&
'frame/module_domain.f: Failed to deallocate grid%laip. ')
 endif
  NULLIFY(grid%laip)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq_s5 ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq_s5,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1408,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq_s5. ')
 endif
  NULLIFY(grid%h2osoi_liq_s5)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice_s2 ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice_s2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1416,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice_s2. ')
 endif
  NULLIFY(grid%h2osoi_ice_s2)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice9 ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice9,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1424,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice9. ')
 endif
  NULLIFY(grid%h2osoi_ice9)
ENDIF
IF ( ASSOCIATED( grid%t_soisno6 ) ) THEN 
  DEALLOCATE(grid%t_soisno6,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1432,&
'frame/module_domain.f: Failed to deallocate grid%t_soisno6. ')
 endif
  NULLIFY(grid%t_soisno6)
ENDIF
IF ( ASSOCIATED( grid%snowrds3 ) ) THEN 
  DEALLOCATE(grid%snowrds3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1440,&
'frame/module_domain.f: Failed to deallocate grid%snowrds3. ')
 endif
  NULLIFY(grid%snowrds3)
ENDIF
IF ( ASSOCIATED( grid%t_lake10 ) ) THEN 
  DEALLOCATE(grid%t_lake10,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1448,&
'frame/module_domain.f: Failed to deallocate grid%t_lake10. ')
 endif
  NULLIFY(grid%t_lake10)
ENDIF
IF ( ASSOCIATED( grid%lhsubgrid ) ) THEN 
  DEALLOCATE(grid%lhsubgrid,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1456,&
'frame/module_domain.f: Failed to deallocate grid%lhsubgrid. ')
 endif
  NULLIFY(grid%lhsubgrid)
ENDIF
IF ( ASSOCIATED( grid%h2osno2d ) ) THEN 
  DEALLOCATE(grid%h2osno2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1464,&
'frame/module_domain.f: Failed to deallocate grid%h2osno2d. ')
 endif
  NULLIFY(grid%h2osno2d)
ENDIF
IF ( ASSOCIATED( grid%dz3d ) ) THEN 
  DEALLOCATE(grid%dz3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1472,&
'frame/module_domain.f: Failed to deallocate grid%dz3d. ')
 endif
  NULLIFY(grid%dz3d)
ENDIF
IF ( ASSOCIATED( grid%ssib_fm ) ) THEN 
  DEALLOCATE(grid%ssib_fm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1480,&
'frame/module_domain.f: Failed to deallocate grid%ssib_fm. ')
 endif
  NULLIFY(grid%ssib_fm)
ENDIF
IF ( ASSOCIATED( grid%ssib_egt ) ) THEN 
  DEALLOCATE(grid%ssib_egt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1488,&
'frame/module_domain.f: Failed to deallocate grid%ssib_egt. ')
 endif
  NULLIFY(grid%ssib_egt)
ENDIF
IF ( ASSOCIATED( grid%isnow ) ) THEN 
  DEALLOCATE(grid%isnow,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1496,&
'frame/module_domain.f: Failed to deallocate grid%isnow. ')
 endif
  NULLIFY(grid%isnow)
ENDIF
IF ( ASSOCIATED( grid%fio1 ) ) THEN 
  DEALLOCATE(grid%fio1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1504,&
'frame/module_domain.f: Failed to deallocate grid%fio1. ')
 endif
  NULLIFY(grid%fio1)
ENDIF
IF ( ASSOCIATED( grid%fio2 ) ) THEN 
  DEALLOCATE(grid%fio2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1512,&
'frame/module_domain.f: Failed to deallocate grid%fio2. ')
 endif
  NULLIFY(grid%fio2)
ENDIF
IF ( ASSOCIATED( grid%fio3 ) ) THEN 
  DEALLOCATE(grid%fio3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1520,&
'frame/module_domain.f: Failed to deallocate grid%fio3. ')
 endif
  NULLIFY(grid%fio3)
ENDIF
IF ( ASSOCIATED( grid%fio4 ) ) THEN 
  DEALLOCATE(grid%fio4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1528,&
'frame/module_domain.f: Failed to deallocate grid%fio4. ')
 endif
  NULLIFY(grid%fio4)
ENDIF
IF ( ASSOCIATED( grid%cmxy ) ) THEN 
  DEALLOCATE(grid%cmxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1536,&
'frame/module_domain.f: Failed to deallocate grid%cmxy. ')
 endif
  NULLIFY(grid%cmxy)
ENDIF
IF ( ASSOCIATED( grid%zsnsoxy ) ) THEN 
  DEALLOCATE(grid%zsnsoxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1544,&
'frame/module_domain.f: Failed to deallocate grid%zsnsoxy. ')
 endif
  NULLIFY(grid%zsnsoxy)
ENDIF
IF ( ASSOCIATED( grid%t2mbxy ) ) THEN 
  DEALLOCATE(grid%t2mbxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1552,&
'frame/module_domain.f: Failed to deallocate grid%t2mbxy. ')
 endif
  NULLIFY(grid%t2mbxy)
ENDIF
IF ( ASSOCIATED( grid%edirxy ) ) THEN 
  DEALLOCATE(grid%edirxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1560,&
'frame/module_domain.f: Failed to deallocate grid%edirxy. ')
 endif
  NULLIFY(grid%edirxy)
ENDIF
IF ( ASSOCIATED( grid%tgvxy ) ) THEN 
  DEALLOCATE(grid%tgvxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1568,&
'frame/module_domain.f: Failed to deallocate grid%tgvxy. ')
 endif
  NULLIFY(grid%tgvxy)
ENDIF
IF ( ASSOCIATED( grid%ircxy ) ) THEN 
  DEALLOCATE(grid%ircxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1576,&
'frame/module_domain.f: Failed to deallocate grid%ircxy. ')
 endif
  NULLIFY(grid%ircxy)
ENDIF
IF ( ASSOCIATED( grid%deeprechxy ) ) THEN 
  DEALLOCATE(grid%deeprechxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1584,&
'frame/module_domain.f: Failed to deallocate grid%deeprechxy. ')
 endif
  NULLIFY(grid%deeprechxy)
ENDIF
IF ( ASSOCIATED( grid%rivercondxy ) ) THEN 
  DEALLOCATE(grid%rivercondxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1592,&
'frame/module_domain.f: Failed to deallocate grid%rivercondxy. ')
 endif
  NULLIFY(grid%rivercondxy)
ENDIF
IF ( ASSOCIATED( grid%qsnfroxy ) ) THEN 
  DEALLOCATE(grid%qsnfroxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1600,&
'frame/module_domain.f: Failed to deallocate grid%qsnfroxy. ')
 endif
  NULLIFY(grid%qsnfroxy)
ENDIF
IF ( ASSOCIATED( grid%pahvxy ) ) THEN 
  DEALLOCATE(grid%pahvxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1608,&
'frame/module_domain.f: Failed to deallocate grid%pahvxy. ')
 endif
  NULLIFY(grid%pahvxy)
ENDIF
IF ( ASSOCIATED( grid%acintr ) ) THEN 
  DEALLOCATE(grid%acintr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1616,&
'frame/module_domain.f: Failed to deallocate grid%acintr. ')
 endif
  NULLIFY(grid%acintr)
ENDIF
IF ( ASSOCIATED( grid%acrunsf ) ) THEN 
  DEALLOCATE(grid%acrunsf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1624,&
'frame/module_domain.f: Failed to deallocate grid%acrunsf. ')
 endif
  NULLIFY(grid%acrunsf)
ENDIF
IF ( ASSOCIATED( grid%acsnbot ) ) THEN 
  DEALLOCATE(grid%acsnbot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1632,&
'frame/module_domain.f: Failed to deallocate grid%acsnbot. ')
 endif
  NULLIFY(grid%acsnbot)
ENDIF
IF ( ASSOCIATED( grid%acghb ) ) THEN 
  DEALLOCATE(grid%acghb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1640,&
'frame/module_domain.f: Failed to deallocate grid%acghb. ')
 endif
  NULLIFY(grid%acghb)
ENDIF
IF ( ASSOCIATED( grid%actr ) ) THEN 
  DEALLOCATE(grid%actr,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1648,&
'frame/module_domain.f: Failed to deallocate grid%actr. ')
 endif
  NULLIFY(grid%actr)
ENDIF
IF ( ASSOCIATED( grid%snowenergy ) ) THEN 
  DEALLOCATE(grid%snowenergy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1656,&
'frame/module_domain.f: Failed to deallocate grid%snowenergy. ')
 endif
  NULLIFY(grid%snowenergy)
ENDIF
IF ( ASSOCIATED( grid%grainxy ) ) THEN 
  DEALLOCATE(grid%grainxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1664,&
'frame/module_domain.f: Failed to deallocate grid%grainxy. ')
 endif
  NULLIFY(grid%grainxy)
ENDIF
IF ( ASSOCIATED( grid%sifract ) ) THEN 
  DEALLOCATE(grid%sifract,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1672,&
'frame/module_domain.f: Failed to deallocate grid%sifract. ')
 endif
  NULLIFY(grid%sifract)
ENDIF
IF ( ASSOCIATED( grid%ireloss ) ) THEN 
  DEALLOCATE(grid%ireloss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1680,&
'frame/module_domain.f: Failed to deallocate grid%ireloss. ')
 endif
  NULLIFY(grid%ireloss)
ENDIF
IF ( ASSOCIATED( grid%kext_ft_qid ) ) THEN 
  DEALLOCATE(grid%kext_ft_qid,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1688,&
'frame/module_domain.f: Failed to deallocate grid%kext_ft_qid. ')
 endif
  NULLIFY(grid%kext_ft_qid)
ENDIF
IF ( ASSOCIATED( grid%refd ) ) THEN 
  DEALLOCATE(grid%refd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1696,&
'frame/module_domain.f: Failed to deallocate grid%refd. ')
 endif
  NULLIFY(grid%refd)
ENDIF
IF ( ASSOCIATED( grid%icing_sm ) ) THEN 
  DEALLOCATE(grid%icing_sm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1704,&
'frame/module_domain.f: Failed to deallocate grid%icing_sm. ')
 endif
  NULLIFY(grid%icing_sm)
ENDIF
IF ( ASSOCIATED( grid%afwa_precip ) ) THEN 
  DEALLOCATE(grid%afwa_precip,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1712,&
'frame/module_domain.f: Failed to deallocate grid%afwa_precip. ')
 endif
  NULLIFY(grid%afwa_precip)
ENDIF
IF ( ASSOCIATED( grid%afwa_cape ) ) THEN 
  DEALLOCATE(grid%afwa_cape,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1720,&
'frame/module_domain.f: Failed to deallocate grid%afwa_cape. ')
 endif
  NULLIFY(grid%afwa_cape)
ENDIF
IF ( ASSOCIATED( grid%afwa_tornado ) ) THEN 
  DEALLOCATE(grid%afwa_tornado,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1728,&
'frame/module_domain.f: Failed to deallocate grid%afwa_tornado. ')
 endif
  NULLIFY(grid%afwa_tornado)
ENDIF
IF ( ASSOCIATED( grid%glw_mean ) ) THEN 
  DEALLOCATE(grid%glw_mean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1736,&
'frame/module_domain.f: Failed to deallocate grid%glw_mean. ')
 endif
  NULLIFY(grid%glw_mean)
ENDIF
IF ( ASSOCIATED( grid%th2_diurn ) ) THEN 
  DEALLOCATE(grid%th2_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1744,&
'frame/module_domain.f: Failed to deallocate grid%th2_diurn. ')
 endif
  NULLIFY(grid%th2_diurn)
ENDIF
IF ( ASSOCIATED( grid%lwupt_diurn ) ) THEN 
  DEALLOCATE(grid%lwupt_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1752,&
'frame/module_domain.f: Failed to deallocate grid%lwupt_diurn. ')
 endif
  NULLIFY(grid%lwupt_diurn)
ENDIF
IF ( ASSOCIATED( grid%lh_dtmp ) ) THEN 
  DEALLOCATE(grid%lh_dtmp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1760,&
'frame/module_domain.f: Failed to deallocate grid%lh_dtmp. ')
 endif
  NULLIFY(grid%lh_dtmp)
ENDIF
IF ( ASSOCIATED( grid%noninduc ) ) THEN 
  DEALLOCATE(grid%noninduc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1768,&
'frame/module_domain.f: Failed to deallocate grid%noninduc. ')
 endif
  NULLIFY(grid%noninduc)
ENDIF
IF ( ASSOCIATED( grid%flshp ) ) THEN 
  DEALLOCATE(grid%flshp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1776,&
'frame/module_domain.f: Failed to deallocate grid%flshp. ')
 endif
  NULLIFY(grid%flshp)
ENDIF
IF ( ASSOCIATED( grid%field_t_tend_perturb ) ) THEN 
  DEALLOCATE(grid%field_t_tend_perturb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1784,&
'frame/module_domain.f: Failed to deallocate grid%field_t_tend_perturb. ')
 endif
  NULLIFY(grid%field_t_tend_perturb)
ENDIF
IF ( ASSOCIATED( grid%pc_1 ) ) THEN 
  DEALLOCATE(grid%pc_1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1792,&
'frame/module_domain.f: Failed to deallocate grid%pc_1. ')
 endif
  NULLIFY(grid%pc_1)
ENDIF
IF ( ASSOCIATED( grid%pc_2 ) ) THEN 
  DEALLOCATE(grid%pc_2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1800,&
'frame/module_domain.f: Failed to deallocate grid%pc_2. ')
 endif
  NULLIFY(grid%pc_2)
ENDIF
IF ( ASSOCIATED( grid%p_wif_jan ) ) THEN 
  DEALLOCATE(grid%p_wif_jan,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1808,&
'frame/module_domain.f: Failed to deallocate grid%p_wif_jan. ')
 endif
  NULLIFY(grid%p_wif_jan)
ENDIF
IF ( ASSOCIATED( grid%w_wif_now ) ) THEN 
  DEALLOCATE(grid%w_wif_now,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1816,&
'frame/module_domain.f: Failed to deallocate grid%w_wif_now. ')
 endif
  NULLIFY(grid%w_wif_now)
ENDIF
IF ( ASSOCIATED( grid%w_wif_dec ) ) THEN 
  DEALLOCATE(grid%w_wif_dec,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1824,&
'frame/module_domain.f: Failed to deallocate grid%w_wif_dec. ')
 endif
  NULLIFY(grid%w_wif_dec)
ENDIF
IF ( ASSOCIATED( grid%i_wif_nov ) ) THEN 
  DEALLOCATE(grid%i_wif_nov,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1832,&
'frame/module_domain.f: Failed to deallocate grid%i_wif_nov. ')
 endif
  NULLIFY(grid%i_wif_nov)
ENDIF
IF ( ASSOCIATED( grid%b_wif_oct ) ) THEN 
  DEALLOCATE(grid%b_wif_oct,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1840,&
'frame/module_domain.f: Failed to deallocate grid%b_wif_oct. ')
 endif
  NULLIFY(grid%b_wif_oct)
ENDIF
IF ( ASSOCIATED( grid%rain ) ) THEN 
  DEALLOCATE(grid%rain,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1848,&
'frame/module_domain.f: Failed to deallocate grid%rain. ')
 endif
  NULLIFY(grid%rain)
ENDIF
IF ( ASSOCIATED( grid%swp ) ) THEN 
  DEALLOCATE(grid%swp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1856,&
'frame/module_domain.f: Failed to deallocate grid%swp. ')
 endif
  NULLIFY(grid%swp)
ENDIF
IF ( ASSOCIATED( grid%tau_qs ) ) THEN 
  DEALLOCATE(grid%tau_qs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1864,&
'frame/module_domain.f: Failed to deallocate grid%tau_qs. ')
 endif
  NULLIFY(grid%tau_qs)
ENDIF
IF ( ASSOCIATED( grid%ts_lwp ) ) THEN 
  DEALLOCATE(grid%ts_lwp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1872,&
'frame/module_domain.f: Failed to deallocate grid%ts_lwp. ')
 endif
  NULLIFY(grid%ts_lwp)
ENDIF
IF ( ASSOCIATED( grid%ts_tau_qc ) ) THEN 
  DEALLOCATE(grid%ts_tau_qc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1880,&
'frame/module_domain.f: Failed to deallocate grid%ts_tau_qc. ')
 endif
  NULLIFY(grid%ts_tau_qc)
ENDIF
IF ( ASSOCIATED( grid%ts_swdown ) ) THEN 
  DEALLOCATE(grid%ts_swdown,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1888,&
'frame/module_domain.f: Failed to deallocate grid%ts_swdown. ')
 endif
  NULLIFY(grid%ts_swdown)
ENDIF
IF ( ASSOCIATED( grid%s_pl ) ) THEN 
  DEALLOCATE(grid%s_pl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1896,&
'frame/module_domain.f: Failed to deallocate grid%s_pl. ')
 endif
  NULLIFY(grid%s_pl)
ENDIF
IF ( ASSOCIATED( grid%t_zl ) ) THEN 
  DEALLOCATE(grid%t_zl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1904,&
'frame/module_domain.f: Failed to deallocate grid%t_zl. ')
 endif
  NULLIFY(grid%t_zl)
ENDIF
IF ( ASSOCIATED( grid%p_iau ) ) THEN 
  DEALLOCATE(grid%p_iau,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1912,&
'frame/module_domain.f: Failed to deallocate grid%p_iau. ')
 endif
  NULLIFY(grid%p_iau)
ENDIF
IF ( ASSOCIATED( grid%rphiauten ) ) THEN 
  DEALLOCATE(grid%rphiauten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1920,&
'frame/module_domain.f: Failed to deallocate grid%rphiauten. ')
 endif
  NULLIFY(grid%rphiauten)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k1 ) ) THEN 
  DEALLOCATE(grid%sdirk3_k1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1928,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k1. ')
 endif
  NULLIFY(grid%sdirk3_k1)
ENDIF
IF ( ASSOCIATED( grid%sst_input ) ) THEN 
  DEALLOCATE(grid%sst_input,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1936,&
'frame/module_domain.f: Failed to deallocate grid%sst_input. ')
 endif
  NULLIFY(grid%sst_input)
ENDIF
END SUBROUTINE deallocs_1



