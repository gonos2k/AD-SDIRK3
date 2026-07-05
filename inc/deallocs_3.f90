





SUBROUTINE deallocs_3( grid )
  USE module_wrf_error
  USE module_domain_type
  IMPLICIT NONE
  TYPE( domain ), POINTER :: grid
  INTEGER :: ierr
IF ( ASSOCIATED( grid%lu_mask ) ) THEN 
  DEALLOCATE(grid%lu_mask,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",16,&
'frame/module_domain.f: Failed to deallocate grid%lu_mask. ')
 endif
  NULLIFY(grid%lu_mask)
ENDIF
IF ( ASSOCIATED( grid%t_gc ) ) THEN 
  DEALLOCATE(grid%t_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",24,&
'frame/module_domain.f: Failed to deallocate grid%t_gc. ')
 endif
  NULLIFY(grid%t_gc)
ENDIF
IF ( ASSOCIATED( grid%tmn_gc ) ) THEN 
  DEALLOCATE(grid%tmn_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",32,&
'frame/module_domain.f: Failed to deallocate grid%tmn_gc. ')
 endif
  NULLIFY(grid%tmn_gc)
ENDIF
IF ( ASSOCIATED( grid%qv_gc ) ) THEN 
  DEALLOCATE(grid%qv_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",40,&
'frame/module_domain.f: Failed to deallocate grid%qv_gc. ')
 endif
  NULLIFY(grid%qv_gc)
ENDIF
IF ( ASSOCIATED( grid%qni_gc ) ) THEN 
  DEALLOCATE(grid%qni_gc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",48,&
'frame/module_domain.f: Failed to deallocate grid%qni_gc. ')
 endif
  NULLIFY(grid%qni_gc)
ENDIF
IF ( ASSOCIATED( grid%ght_min_p ) ) THEN 
  DEALLOCATE(grid%ght_min_p,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",56,&
'frame/module_domain.f: Failed to deallocate grid%ght_min_p. ')
 endif
  NULLIFY(grid%ght_min_p)
ENDIF
IF ( ASSOCIATED( grid%vmaxw ) ) THEN 
  DEALLOCATE(grid%vmaxw,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",64,&
'frame/module_domain.f: Failed to deallocate grid%vmaxw. ')
 endif
  NULLIFY(grid%vmaxw)
ENDIF
IF ( ASSOCIATED( grid%u_save ) ) THEN 
  DEALLOCATE(grid%u_save,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",72,&
'frame/module_domain.f: Failed to deallocate grid%u_save. ')
 endif
  NULLIFY(grid%u_save)
ENDIF
IF ( ASSOCIATED( grid%v_save ) ) THEN 
  DEALLOCATE(grid%v_save,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",80,&
'frame/module_domain.f: Failed to deallocate grid%v_save. ')
 endif
  NULLIFY(grid%v_save)
ENDIF
IF ( ASSOCIATED( grid%ph_btxs ) ) THEN 
  DEALLOCATE(grid%ph_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",88,&
'frame/module_domain.f: Failed to deallocate grid%ph_btxs. ')
 endif
  NULLIFY(grid%ph_btxs)
ENDIF
IF ( ASSOCIATED( grid%ph_btxe ) ) THEN 
  DEALLOCATE(grid%ph_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",96,&
'frame/module_domain.f: Failed to deallocate grid%ph_btxe. ')
 endif
  NULLIFY(grid%ph_btxe)
ENDIF
IF ( ASSOCIATED( grid%ph_btys ) ) THEN 
  DEALLOCATE(grid%ph_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",104,&
'frame/module_domain.f: Failed to deallocate grid%ph_btys. ')
 endif
  NULLIFY(grid%ph_btys)
ENDIF
IF ( ASSOCIATED( grid%ph_btye ) ) THEN 
  DEALLOCATE(grid%ph_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",112,&
'frame/module_domain.f: Failed to deallocate grid%ph_btye. ')
 endif
  NULLIFY(grid%ph_btye)
ENDIF
IF ( ASSOCIATED( grid%t_init ) ) THEN 
  DEALLOCATE(grid%t_init,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",120,&
'frame/module_domain.f: Failed to deallocate grid%t_init. ')
 endif
  NULLIFY(grid%t_init)
ENDIF
IF ( ASSOCIATED( grid%qv_upstream_y_tend ) ) THEN 
  DEALLOCATE(grid%qv_upstream_y_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",128,&
'frame/module_domain.f: Failed to deallocate grid%qv_upstream_y_tend. ')
 endif
  NULLIFY(grid%qv_upstream_y_tend)
ENDIF
IF ( ASSOCIATED( grid%v_upstream_y_tend ) ) THEN 
  DEALLOCATE(grid%v_upstream_y_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",136,&
'frame/module_domain.f: Failed to deallocate grid%v_upstream_y_tend. ')
 endif
  NULLIFY(grid%v_upstream_y_tend)
ENDIF
IF ( ASSOCIATED( grid%v_largescale_tend ) ) THEN 
  DEALLOCATE(grid%v_largescale_tend,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",144,&
'frame/module_domain.f: Failed to deallocate grid%v_largescale_tend. ')
 endif
  NULLIFY(grid%v_largescale_tend)
ENDIF
IF ( ASSOCIATED( grid%soil_depth_force ) ) THEN 
  DEALLOCATE(grid%soil_depth_force,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",152,&
'frame/module_domain.f: Failed to deallocate grid%soil_depth_force. ')
 endif
  NULLIFY(grid%soil_depth_force)
ENDIF
IF ( ASSOCIATED( grid%mub_save ) ) THEN 
  DEALLOCATE(grid%mub_save,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",160,&
'frame/module_domain.f: Failed to deallocate grid%mub_save. ')
 endif
  NULLIFY(grid%mub_save)
ENDIF
IF ( ASSOCIATED( grid%l_diss ) ) THEN 
  DEALLOCATE(grid%l_diss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",168,&
'frame/module_domain.f: Failed to deallocate grid%l_diss. ')
 endif
  NULLIFY(grid%l_diss)
ENDIF
IF ( ASSOCIATED( grid%fnp ) ) THEN 
  DEALLOCATE(grid%fnp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",176,&
'frame/module_domain.f: Failed to deallocate grid%fnp. ')
 endif
  NULLIFY(grid%fnp)
ENDIF
IF ( ASSOCIATED( grid%th2 ) ) THEN 
  DEALLOCATE(grid%th2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",184,&
'frame/module_domain.f: Failed to deallocate grid%th2. ')
 endif
  NULLIFY(grid%th2)
ENDIF
IF ( ASSOCIATED( grid%dx2d ) ) THEN 
  DEALLOCATE(grid%dx2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",192,&
'frame/module_domain.f: Failed to deallocate grid%dx2d. ')
 endif
  NULLIFY(grid%dx2d)
ENDIF
IF ( ASSOCIATED( grid%imask_xystag ) ) THEN 
  DEALLOCATE(grid%imask_xystag,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",200,&
'frame/module_domain.f: Failed to deallocate grid%imask_xystag. ')
 endif
  NULLIFY(grid%imask_xystag)
ENDIF
IF ( ASSOCIATED( grid%rimi ) ) THEN 
  DEALLOCATE(grid%rimi,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",208,&
'frame/module_domain.f: Failed to deallocate grid%rimi. ')
 endif
  NULLIFY(grid%rimi)
ENDIF
IF ( ASSOCIATED( grid%re_snow_gsfc ) ) THEN 
  DEALLOCATE(grid%re_snow_gsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",216,&
'frame/module_domain.f: Failed to deallocate grid%re_snow_gsfc. ')
 endif
  NULLIFY(grid%re_snow_gsfc)
ENDIF
IF ( ASSOCIATED( grid%soil_layers ) ) THEN 
  DEALLOCATE(grid%soil_layers,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",224,&
'frame/module_domain.f: Failed to deallocate grid%soil_layers. ')
 endif
  NULLIFY(grid%soil_layers)
ENDIF
IF ( ASSOCIATED( grid%st007028 ) ) THEN 
  DEALLOCATE(grid%st007028,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",232,&
'frame/module_domain.f: Failed to deallocate grid%st007028. ')
 endif
  NULLIFY(grid%st007028)
ENDIF
IF ( ASSOCIATED( grid%soilm160 ) ) THEN 
  DEALLOCATE(grid%soilm160,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",240,&
'frame/module_domain.f: Failed to deallocate grid%soilm160. ')
 endif
  NULLIFY(grid%soilm160)
ENDIF
IF ( ASSOCIATED( grid%soilw300 ) ) THEN 
  DEALLOCATE(grid%soilw300,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",248,&
'frame/module_domain.f: Failed to deallocate grid%soilw300. ')
 endif
  NULLIFY(grid%soilw300)
ENDIF
IF ( ASSOCIATED( grid%topostdv ) ) THEN 
  DEALLOCATE(grid%topostdv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",256,&
'frame/module_domain.f: Failed to deallocate grid%topostdv. ')
 endif
  NULLIFY(grid%topostdv)
ENDIF
IF ( ASSOCIATED( grid%soilcbot ) ) THEN 
  DEALLOCATE(grid%soilcbot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",264,&
'frame/module_domain.f: Failed to deallocate grid%soilcbot. ')
 endif
  NULLIFY(grid%soilcbot)
ENDIF
IF ( ASSOCIATED( grid%ts_psfc ) ) THEN 
  DEALLOCATE(grid%ts_psfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",272,&
'frame/module_domain.f: Failed to deallocate grid%ts_psfc. ')
 endif
  NULLIFY(grid%ts_psfc)
ENDIF
IF ( ASSOCIATED( grid%ts_w_profile ) ) THEN 
  DEALLOCATE(grid%ts_w_profile,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",280,&
'frame/module_domain.f: Failed to deallocate grid%ts_w_profile. ')
 endif
  NULLIFY(grid%ts_w_profile)
ENDIF
IF ( ASSOCIATED( grid%hgt_urb2d ) ) THEN 
  DEALLOCATE(grid%hgt_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",288,&
'frame/module_domain.f: Failed to deallocate grid%hgt_urb2d. ')
 endif
  NULLIFY(grid%hgt_urb2d)
ENDIF
IF ( ASSOCIATED( grid%icedepth ) ) THEN 
  DEALLOCATE(grid%icedepth,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",296,&
'frame/module_domain.f: Failed to deallocate grid%icedepth. ')
 endif
  NULLIFY(grid%icedepth)
ENDIF
IF ( ASSOCIATED( grid%udrunoff ) ) THEN 
  DEALLOCATE(grid%udrunoff,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",304,&
'frame/module_domain.f: Failed to deallocate grid%udrunoff. ')
 endif
  NULLIFY(grid%udrunoff)
ENDIF
IF ( ASSOCIATED( grid%snowh ) ) THEN 
  DEALLOCATE(grid%snowh,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",312,&
'frame/module_domain.f: Failed to deallocate grid%snowh. ')
 endif
  NULLIFY(grid%snowh)
ENDIF
IF ( ASSOCIATED( grid%uoce ) ) THEN 
  DEALLOCATE(grid%uoce,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",320,&
'frame/module_domain.f: Failed to deallocate grid%uoce. ')
 endif
  NULLIFY(grid%uoce)
ENDIF
IF ( ASSOCIATED( grid%dfi_w ) ) THEN 
  DEALLOCATE(grid%dfi_w,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",328,&
'frame/module_domain.f: Failed to deallocate grid%dfi_w. ')
 endif
  NULLIFY(grid%dfi_w)
ENDIF
IF ( ASSOCIATED( grid%dfi_snowh ) ) THEN 
  DEALLOCATE(grid%dfi_snowh,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",336,&
'frame/module_domain.f: Failed to deallocate grid%dfi_snowh. ')
 endif
  NULLIFY(grid%dfi_snowh)
ENDIF
IF ( ASSOCIATED( grid%xxxr_urb2d ) ) THEN 
  DEALLOCATE(grid%xxxr_urb2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",344,&
'frame/module_domain.f: Failed to deallocate grid%xxxr_urb2d. ')
 endif
  NULLIFY(grid%xxxr_urb2d)
ENDIF
IF ( ASSOCIATED( grid%smr_urb3d ) ) THEN 
  DEALLOCATE(grid%smr_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",352,&
'frame/module_domain.f: Failed to deallocate grid%smr_urb3d. ')
 endif
  NULLIFY(grid%smr_urb3d)
ENDIF
IF ( ASSOCIATED( grid%tw1_urb4d ) ) THEN 
  DEALLOCATE(grid%tw1_urb4d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",360,&
'frame/module_domain.f: Failed to deallocate grid%tw1_urb4d. ')
 endif
  NULLIFY(grid%tw1_urb4d)
ENDIF
IF ( ASSOCIATED( grid%sfvent_urb3d ) ) THEN 
  DEALLOCATE(grid%sfvent_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",368,&
'frame/module_domain.f: Failed to deallocate grid%sfvent_urb3d. ')
 endif
  NULLIFY(grid%sfvent_urb3d)
ENDIF
IF ( ASSOCIATED( grid%qgr_urb3d ) ) THEN 
  DEALLOCATE(grid%qgr_urb3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",376,&
'frame/module_domain.f: Failed to deallocate grid%qgr_urb3d. ')
 endif
  NULLIFY(grid%qgr_urb3d)
ENDIF
IF ( ASSOCIATED( grid%cmc_sfcdif ) ) THEN 
  DEALLOCATE(grid%cmc_sfcdif,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",384,&
'frame/module_domain.f: Failed to deallocate grid%cmc_sfcdif. ')
 endif
  NULLIFY(grid%cmc_sfcdif)
ENDIF
IF ( ASSOCIATED( grid%rhosnf ) ) THEN 
  DEALLOCATE(grid%rhosnf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",392,&
'frame/module_domain.f: Failed to deallocate grid%rhosnf. ')
 endif
  NULLIFY(grid%rhosnf)
ENDIF
IF ( ASSOCIATED( grid%alswnirdif ) ) THEN 
  DEALLOCATE(grid%alswnirdif,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",400,&
'frame/module_domain.f: Failed to deallocate grid%alswnirdif. ')
 endif
  NULLIFY(grid%alswnirdif)
ENDIF
IF ( ASSOCIATED( grid%wsat_px ) ) THEN 
  DEALLOCATE(grid%wsat_px,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",408,&
'frame/module_domain.f: Failed to deallocate grid%wsat_px. ')
 endif
  NULLIFY(grid%wsat_px)
ENDIF
IF ( ASSOCIATED( grid%wstar_ysu ) ) THEN 
  DEALLOCATE(grid%wstar_ysu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",416,&
'frame/module_domain.f: Failed to deallocate grid%wstar_ysu. ')
 endif
  NULLIFY(grid%wstar_ysu)
ENDIF
IF ( ASSOCIATED( grid%qsfc ) ) THEN 
  DEALLOCATE(grid%qsfc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",424,&
'frame/module_domain.f: Failed to deallocate grid%qsfc. ')
 endif
  NULLIFY(grid%qsfc)
ENDIF
IF ( ASSOCIATED( grid%rc_mf ) ) THEN 
  DEALLOCATE(grid%rc_mf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",432,&
'frame/module_domain.f: Failed to deallocate grid%rc_mf. ')
 endif
  NULLIFY(grid%rc_mf)
ENDIF
IF ( ASSOCIATED( grid%qlup_temf ) ) THEN 
  DEALLOCATE(grid%qlup_temf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",440,&
'frame/module_domain.f: Failed to deallocate grid%qlup_temf. ')
 endif
  NULLIFY(grid%qlup_temf)
ENDIF
IF ( ASSOCIATED( grid%dqke ) ) THEN 
  DEALLOCATE(grid%dqke,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",448,&
'frame/module_domain.f: Failed to deallocate grid%dqke. ')
 endif
  NULLIFY(grid%dqke)
ENDIF
IF ( ASSOCIATED( grid%edmf_qc ) ) THEN 
  DEALLOCATE(grid%edmf_qc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",456,&
'frame/module_domain.f: Failed to deallocate grid%edmf_qc. ')
 endif
  NULLIFY(grid%edmf_qc)
ENDIF
IF ( ASSOCIATED( grid%dtaux3d ) ) THEN 
  DEALLOCATE(grid%dtaux3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",464,&
'frame/module_domain.f: Failed to deallocate grid%dtaux3d. ')
 endif
  NULLIFY(grid%dtaux3d)
ENDIF
IF ( ASSOCIATED( grid%ol3 ) ) THEN 
  DEALLOCATE(grid%ol3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",472,&
'frame/module_domain.f: Failed to deallocate grid%ol3. ')
 endif
  NULLIFY(grid%ol3)
ENDIF
IF ( ASSOCIATED( grid%dusfcg_bl ) ) THEN 
  DEALLOCATE(grid%dusfcg_bl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",480,&
'frame/module_domain.f: Failed to deallocate grid%dusfcg_bl. ')
 endif
  NULLIFY(grid%dusfcg_bl)
ENDIF
IF ( ASSOCIATED( grid%ol1ls ) ) THEN 
  DEALLOCATE(grid%ol1ls,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",488,&
'frame/module_domain.f: Failed to deallocate grid%ol1ls. ')
 endif
  NULLIFY(grid%ol1ls)
ENDIF
IF ( ASSOCIATED( grid%ol3ss ) ) THEN 
  DEALLOCATE(grid%ol3ss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",496,&
'frame/module_domain.f: Failed to deallocate grid%ol3ss. ')
 endif
  NULLIFY(grid%ol3ss)
ENDIF
IF ( ASSOCIATED( grid%b_q_bep ) ) THEN 
  DEALLOCATE(grid%b_q_bep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",504,&
'frame/module_domain.f: Failed to deallocate grid%b_q_bep. ')
 endif
  NULLIFY(grid%b_q_bep)
ENDIF
IF ( ASSOCIATED( grid%wv_tur ) ) THEN 
  DEALLOCATE(grid%wv_tur,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",512,&
'frame/module_domain.f: Failed to deallocate grid%wv_tur. ')
 endif
  NULLIFY(grid%wv_tur)
ENDIF
IF ( ASSOCIATED( grid%czmean ) ) THEN 
  DEALLOCATE(grid%czmean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",520,&
'frame/module_domain.f: Failed to deallocate grid%czmean. ')
 endif
  NULLIFY(grid%czmean)
ENDIF
IF ( ASSOCIATED( grid%aerod ) ) THEN 
  DEALLOCATE(grid%aerod,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",528,&
'frame/module_domain.f: Failed to deallocate grid%aerod. ')
 endif
  NULLIFY(grid%aerod)
ENDIF
IF ( ASSOCIATED( grid%f_rain_phy ) ) THEN 
  DEALLOCATE(grid%f_rain_phy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",536,&
'frame/module_domain.f: Failed to deallocate grid%f_rain_phy. ')
 endif
  NULLIFY(grid%f_rain_phy)
ENDIF
IF ( ASSOCIATED( grid%om_sini ) ) THEN 
  DEALLOCATE(grid%om_sini,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",544,&
'frame/module_domain.f: Failed to deallocate grid%om_sini. ')
 endif
  NULLIFY(grid%om_sini)
ENDIF
IF ( ASSOCIATED( grid%cldfratend_cup ) ) THEN 
  DEALLOCATE(grid%cldfratend_cup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",552,&
'frame/module_domain.f: Failed to deallocate grid%cldfratend_cup. ')
 endif
  NULLIFY(grid%cldfratend_cup)
ENDIF
IF ( ASSOCIATED( grid%mfdn_ent_cup ) ) THEN 
  DEALLOCATE(grid%mfdn_ent_cup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",560,&
'frame/module_domain.f: Failed to deallocate grid%mfdn_ent_cup. ')
 endif
  NULLIFY(grid%mfdn_ent_cup)
ENDIF
IF ( ASSOCIATED( grid%msfv ) ) THEN 
  DEALLOCATE(grid%msfv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",568,&
'frame/module_domain.f: Failed to deallocate grid%msfv. ')
 endif
  NULLIFY(grid%msfv)
ENDIF
IF ( ASSOCIATED( grid%ht ) ) THEN 
  DEALLOCATE(grid%ht,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",576,&
'frame/module_domain.f: Failed to deallocate grid%ht. ')
 endif
  NULLIFY(grid%ht)
ENDIF
IF ( ASSOCIATED( grid%dfi_tsk ) ) THEN 
  DEALLOCATE(grid%dfi_tsk,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",584,&
'frame/module_domain.f: Failed to deallocate grid%dfi_tsk. ')
 endif
  NULLIFY(grid%dfi_tsk)
ENDIF
IF ( ASSOCIATED( grid%physf ) ) THEN 
  DEALLOCATE(grid%physf,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",592,&
'frame/module_domain.f: Failed to deallocate grid%physf. ')
 endif
  NULLIFY(grid%physf)
ENDIF
IF ( ASSOCIATED( grid%precr3d ) ) THEN 
  DEALLOCATE(grid%precr3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",600,&
'frame/module_domain.f: Failed to deallocate grid%precr3d. ')
 endif
  NULLIFY(grid%precr3d)
ENDIF
IF ( ASSOCIATED( grid%rvshten ) ) THEN 
  DEALLOCATE(grid%rvshten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",608,&
'frame/module_domain.f: Failed to deallocate grid%rvshten. ')
 endif
  NULLIFY(grid%rvshten)
ENDIF
IF ( ASSOCIATED( grid%cldareaa ) ) THEN 
  DEALLOCATE(grid%cldareaa,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",616,&
'frame/module_domain.f: Failed to deallocate grid%cldareaa. ')
 endif
  NULLIFY(grid%cldareaa)
ENDIF
IF ( ASSOCIATED( grid%radsave ) ) THEN 
  DEALLOCATE(grid%radsave,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",624,&
'frame/module_domain.f: Failed to deallocate grid%radsave. ')
 endif
  NULLIFY(grid%radsave)
ENDIF
IF ( ASSOCIATED( grid%rqvcuten ) ) THEN 
  DEALLOCATE(grid%rqvcuten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",632,&
'frame/module_domain.f: Failed to deallocate grid%rqvcuten. ')
 endif
  NULLIFY(grid%rqvcuten)
ENDIF
IF ( ASSOCIATED( grid%rainnc ) ) THEN 
  DEALLOCATE(grid%rainnc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",640,&
'frame/module_domain.f: Failed to deallocate grid%rainnc. ')
 endif
  NULLIFY(grid%rainnc)
ENDIF
IF ( ASSOCIATED( grid%snowncv ) ) THEN 
  DEALLOCATE(grid%snowncv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",648,&
'frame/module_domain.f: Failed to deallocate grid%snowncv. ')
 endif
  NULLIFY(grid%snowncv)
ENDIF
IF ( ASSOCIATED( grid%di3d_2 ) ) THEN 
  DEALLOCATE(grid%di3d_2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",656,&
'frame/module_domain.f: Failed to deallocate grid%di3d_2. ')
 endif
  NULLIFY(grid%di3d_2)
ENDIF
IF ( ASSOCIATED( grid%nca ) ) THEN 
  DEALLOCATE(grid%nca,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",664,&
'frame/module_domain.f: Failed to deallocate grid%nca. ')
 endif
  NULLIFY(grid%nca)
ENDIF
IF ( ASSOCIATED( grid%apr_mc ) ) THEN 
  DEALLOCATE(grid%apr_mc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",672,&
'frame/module_domain.f: Failed to deallocate grid%apr_mc. ')
 endif
  NULLIFY(grid%apr_mc)
ENDIF
IF ( ASSOCIATED( grid%kbcon_deep ) ) THEN 
  DEALLOCATE(grid%kbcon_deep,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",680,&
'frame/module_domain.f: Failed to deallocate grid%kbcon_deep. ')
 endif
  NULLIFY(grid%kbcon_deep)
ENDIF
IF ( ASSOCIATED( grid%raincv_a ) ) THEN 
  DEALLOCATE(grid%raincv_a,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",688,&
'frame/module_domain.f: Failed to deallocate grid%raincv_a. ')
 endif
  NULLIFY(grid%raincv_a)
ENDIF
IF ( ASSOCIATED( grid%ccn_cu ) ) THEN 
  DEALLOCATE(grid%ccn_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",696,&
'frame/module_domain.f: Failed to deallocate grid%ccn_cu. ')
 endif
  NULLIFY(grid%ccn_cu)
ENDIF
IF ( ASSOCIATED( grid%ccn4_gs ) ) THEN 
  DEALLOCATE(grid%ccn4_gs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",704,&
'frame/module_domain.f: Failed to deallocate grid%ccn4_gs. ')
 endif
  NULLIFY(grid%ccn4_gs)
ENDIF
IF ( ASSOCIATED( grid%rthratenlwc ) ) THEN 
  DEALLOCATE(grid%rthratenlwc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",712,&
'frame/module_domain.f: Failed to deallocate grid%rthratenlwc. ')
 endif
  NULLIFY(grid%rthratenlwc)
ENDIF
IF ( ASSOCIATED( grid%swdownc2 ) ) THEN 
  DEALLOCATE(grid%swdownc2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",720,&
'frame/module_domain.f: Failed to deallocate grid%swdownc2. ')
 endif
  NULLIFY(grid%swdownc2)
ENDIF
IF ( ASSOCIATED( grid%swddif ) ) THEN 
  DEALLOCATE(grid%swddif,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",728,&
'frame/module_domain.f: Failed to deallocate grid%swddif. ')
 endif
  NULLIFY(grid%swddif)
ENDIF
IF ( ASSOCIATED( grid%aerasy2d ) ) THEN 
  DEALLOCATE(grid%aerasy2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",736,&
'frame/module_domain.f: Failed to deallocate grid%aerasy2d. ')
 endif
  NULLIFY(grid%aerasy2d)
ENDIF
IF ( ASSOCIATED( grid%tq2min ) ) THEN 
  DEALLOCATE(grid%tq2min,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",744,&
'frame/module_domain.f: Failed to deallocate grid%tq2min. ')
 endif
  NULLIFY(grid%tq2min)
ENDIF
IF ( ASSOCIATED( grid%spduv10max ) ) THEN 
  DEALLOCATE(grid%spduv10max,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",752,&
'frame/module_domain.f: Failed to deallocate grid%spduv10max. ')
 endif
  NULLIFY(grid%spduv10max)
ENDIF
IF ( ASSOCIATED( grid%raincvmean ) ) THEN 
  DEALLOCATE(grid%raincvmean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",760,&
'frame/module_domain.f: Failed to deallocate grid%raincvmean. ')
 endif
  NULLIFY(grid%raincvmean)
ENDIF
IF ( ASSOCIATED( grid%acswdnbc ) ) THEN 
  DEALLOCATE(grid%acswdnbc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",768,&
'frame/module_domain.f: Failed to deallocate grid%acswdnbc. ')
 endif
  NULLIFY(grid%acswdnbc)
ENDIF
IF ( ASSOCIATED( grid%i_acswdntc ) ) THEN 
  DEALLOCATE(grid%i_acswdntc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",776,&
'frame/module_domain.f: Failed to deallocate grid%i_acswdntc. ')
 endif
  NULLIFY(grid%i_acswdntc)
ENDIF
IF ( ASSOCIATED( grid%i_aclwdnbc ) ) THEN 
  DEALLOCATE(grid%i_aclwdnbc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",784,&
'frame/module_domain.f: Failed to deallocate grid%i_aclwdnbc. ')
 endif
  NULLIFY(grid%i_aclwdnbc)
ENDIF
IF ( ASSOCIATED( grid%swdnbcln ) ) THEN 
  DEALLOCATE(grid%swdnbcln,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",792,&
'frame/module_domain.f: Failed to deallocate grid%swdnbcln. ')
 endif
  NULLIFY(grid%swdnbcln)
ENDIF
IF ( ASSOCIATED( grid%lwdnbcln ) ) THEN 
  DEALLOCATE(grid%lwdnbcln,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",800,&
'frame/module_domain.f: Failed to deallocate grid%lwdnbcln. ')
 endif
  NULLIFY(grid%lwdnbcln)
ENDIF
IF ( ASSOCIATED( grid%emiss ) ) THEN 
  DEALLOCATE(grid%emiss,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",808,&
'frame/module_domain.f: Failed to deallocate grid%emiss. ')
 endif
  NULLIFY(grid%emiss)
ENDIF
IF ( ASSOCIATED( grid%flx4 ) ) THEN 
  DEALLOCATE(grid%flx4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",816,&
'frame/module_domain.f: Failed to deallocate grid%flx4. ')
 endif
  NULLIFY(grid%flx4)
ENDIF
IF ( ASSOCIATED( grid%snowc_mosaic ) ) THEN 
  DEALLOCATE(grid%snowc_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",824,&
'frame/module_domain.f: Failed to deallocate grid%snowc_mosaic. ')
 endif
  NULLIFY(grid%snowc_mosaic)
ENDIF
IF ( ASSOCIATED( grid%grdflx_mosaic ) ) THEN 
  DEALLOCATE(grid%grdflx_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",832,&
'frame/module_domain.f: Failed to deallocate grid%grdflx_mosaic. ')
 endif
  NULLIFY(grid%grdflx_mosaic)
ENDIF
IF ( ASSOCIATED( grid%tgl_urb3d_mosaic ) ) THEN 
  DEALLOCATE(grid%tgl_urb3d_mosaic,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",840,&
'frame/module_domain.f: Failed to deallocate grid%tgl_urb3d_mosaic. ')
 endif
  NULLIFY(grid%tgl_urb3d_mosaic)
ENDIF
IF ( ASSOCIATED( grid%cka ) ) THEN 
  DEALLOCATE(grid%cka,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",848,&
'frame/module_domain.f: Failed to deallocate grid%cka. ')
 endif
  NULLIFY(grid%cka)
ENDIF
IF ( ASSOCIATED( grid%qfx ) ) THEN 
  DEALLOCATE(grid%qfx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",856,&
'frame/module_domain.f: Failed to deallocate grid%qfx. ')
 endif
  NULLIFY(grid%qfx)
ENDIF
IF ( ASSOCIATED( grid%soilt1 ) ) THEN 
  DEALLOCATE(grid%soilt1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",864,&
'frame/module_domain.f: Failed to deallocate grid%soilt1. ')
 endif
  NULLIFY(grid%soilt1)
ENDIF
IF ( ASSOCIATED( grid%soiltb ) ) THEN 
  DEALLOCATE(grid%soiltb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",872,&
'frame/module_domain.f: Failed to deallocate grid%soiltb. ')
 endif
  NULLIFY(grid%soiltb)
ENDIF
IF ( ASSOCIATED( grid%xkhv ) ) THEN 
  DEALLOCATE(grid%xkhv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",880,&
'frame/module_domain.f: Failed to deallocate grid%xkhv. ')
 endif
  NULLIFY(grid%xkhv)
ENDIF
IF ( ASSOCIATED( grid%rqvndgdten ) ) THEN 
  DEALLOCATE(grid%rqvndgdten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",888,&
'frame/module_domain.f: Failed to deallocate grid%rqvndgdten. ')
 endif
  NULLIFY(grid%rqvndgdten)
ENDIF
IF ( ASSOCIATED( grid%q2_ndg_old ) ) THEN 
  DEALLOCATE(grid%q2_ndg_old,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",896,&
'frame/module_domain.f: Failed to deallocate grid%q2_ndg_old. ')
 endif
  NULLIFY(grid%q2_ndg_old)
ENDIF
IF ( ASSOCIATED( grid%sn_ndg_new ) ) THEN 
  DEALLOCATE(grid%sn_ndg_new,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",904,&
'frame/module_domain.f: Failed to deallocate grid%sn_ndg_new. ')
 endif
  NULLIFY(grid%sn_ndg_new)
ENDIF
IF ( ASSOCIATED( grid%dpsdt ) ) THEN 
  DEALLOCATE(grid%dpsdt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",912,&
'frame/module_domain.f: Failed to deallocate grid%dpsdt. ')
 endif
  NULLIFY(grid%dpsdt)
ENDIF
IF ( ASSOCIATED( grid%w_colmean ) ) THEN 
  DEALLOCATE(grid%w_colmean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",920,&
'frame/module_domain.f: Failed to deallocate grid%w_colmean. ')
 endif
  NULLIFY(grid%w_colmean)
ENDIF
IF ( ASSOCIATED( grid%t0ml ) ) THEN 
  DEALLOCATE(grid%t0ml,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",928,&
'frame/module_domain.f: Failed to deallocate grid%t0ml. ')
 endif
  NULLIFY(grid%t0ml)
ENDIF
IF ( ASSOCIATED( grid%track_rh ) ) THEN 
  DEALLOCATE(grid%track_rh,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",936,&
'frame/module_domain.f: Failed to deallocate grid%track_rh. ')
 endif
  NULLIFY(grid%track_rh)
ENDIF
IF ( ASSOCIATED( grid%cldtopz ) ) THEN 
  DEALLOCATE(grid%cldtopz,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",944,&
'frame/module_domain.f: Failed to deallocate grid%cldtopz. ')
 endif
  NULLIFY(grid%cldtopz)
ENDIF
IF ( ASSOCIATED( grid%athblten ) ) THEN 
  DEALLOCATE(grid%athblten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",952,&
'frame/module_domain.f: Failed to deallocate grid%athblten. ')
 endif
  NULLIFY(grid%athblten)
ENDIF
IF ( ASSOCIATED( grid%haildtacttime ) ) THEN 
  DEALLOCATE(grid%haildtacttime,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",960,&
'frame/module_domain.f: Failed to deallocate grid%haildtacttime. ')
 endif
  NULLIFY(grid%haildtacttime)
ENDIF
IF ( ASSOCIATED( grid%rv_xxx ) ) THEN 
  DEALLOCATE(grid%rv_xxx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",968,&
'frame/module_domain.f: Failed to deallocate grid%rv_xxx. ')
 endif
  NULLIFY(grid%rv_xxx)
ENDIF
IF ( ASSOCIATED( grid%k1_w ) ) THEN 
  DEALLOCATE(grid%k1_w,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",976,&
'frame/module_domain.f: Failed to deallocate grid%k1_w. ')
 endif
  NULLIFY(grid%k1_w)
ENDIF
IF ( ASSOCIATED( grid%k2_u ) ) THEN 
  DEALLOCATE(grid%k2_u,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",984,&
'frame/module_domain.f: Failed to deallocate grid%k2_u. ')
 endif
  NULLIFY(grid%k2_u)
ENDIF
IF ( ASSOCIATED( grid%k2_ph ) ) THEN 
  DEALLOCATE(grid%k2_ph,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",992,&
'frame/module_domain.f: Failed to deallocate grid%k2_ph. ')
 endif
  NULLIFY(grid%k2_ph)
ENDIF
IF ( ASSOCIATED( grid%k3_w ) ) THEN 
  DEALLOCATE(grid%k3_w,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1000,&
'frame/module_domain.f: Failed to deallocate grid%k3_w. ')
 endif
  NULLIFY(grid%k3_w)
ENDIF
IF ( ASSOCIATED( grid%lfn_time ) ) THEN 
  DEALLOCATE(grid%lfn_time,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1008,&
'frame/module_domain.f: Failed to deallocate grid%lfn_time. ')
 endif
  NULLIFY(grid%lfn_time)
ENDIF
IF ( ASSOCIATED( grid%canqfx ) ) THEN 
  DEALLOCATE(grid%canqfx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1016,&
'frame/module_domain.f: Failed to deallocate grid%canqfx. ')
 endif
  NULLIFY(grid%canqfx)
ENDIF
IF ( ASSOCIATED( grid%lfn_s3 ) ) THEN 
  DEALLOCATE(grid%lfn_s3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1024,&
'frame/module_domain.f: Failed to deallocate grid%lfn_s3. ')
 endif
  NULLIFY(grid%lfn_s3)
ENDIF
IF ( ASSOCIATED( grid%ros_front ) ) THEN 
  DEALLOCATE(grid%ros_front,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1032,&
'frame/module_domain.f: Failed to deallocate grid%ros_front. ')
 endif
  NULLIFY(grid%ros_front)
ENDIF
IF ( ASSOCIATED( grid%fuel_time ) ) THEN 
  DEALLOCATE(grid%fuel_time,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1040,&
'frame/module_domain.f: Failed to deallocate grid%fuel_time. ')
 endif
  NULLIFY(grid%fuel_time)
ENDIF
IF ( ASSOCIATED( grid%fs_spotting_lkhd ) ) THEN 
  DEALLOCATE(grid%fs_spotting_lkhd,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1048,&
'frame/module_domain.f: Failed to deallocate grid%fs_spotting_lkhd. ')
 endif
  NULLIFY(grid%fs_spotting_lkhd)
ENDIF
IF ( ASSOCIATED( grid%avgflx_rum ) ) THEN 
  DEALLOCATE(grid%avgflx_rum,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1056,&
'frame/module_domain.f: Failed to deallocate grid%avgflx_rum. ')
 endif
  NULLIFY(grid%avgflx_rum)
ENDIF
IF ( ASSOCIATED( grid%dfu1 ) ) THEN 
  DEALLOCATE(grid%dfu1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1064,&
'frame/module_domain.f: Failed to deallocate grid%dfu1. ')
 endif
  NULLIFY(grid%dfu1)
ENDIF
IF ( ASSOCIATED( grid%rv_tendf_stoch ) ) THEN 
  DEALLOCATE(grid%rv_tendf_stoch,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1072,&
'frame/module_domain.f: Failed to deallocate grid%rv_tendf_stoch. ')
 endif
  NULLIFY(grid%rv_tendf_stoch)
ENDIF
IF ( ASSOCIATED( grid%sptforcc ) ) THEN 
  DEALLOCATE(grid%sptforcc,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1080,&
'frame/module_domain.f: Failed to deallocate grid%sptforcc. ')
 endif
  NULLIFY(grid%sptforcc)
ENDIF
IF ( ASSOCIATED( grid%spforcc4 ) ) THEN 
  DEALLOCATE(grid%spforcc4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1088,&
'frame/module_domain.f: Failed to deallocate grid%spforcc4. ')
 endif
  NULLIFY(grid%spforcc4)
ENDIF
IF ( ASSOCIATED( grid%iseedarr_skebs ) ) THEN 
  DEALLOCATE(grid%iseedarr_skebs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1096,&
'frame/module_domain.f: Failed to deallocate grid%iseedarr_skebs. ')
 endif
  NULLIFY(grid%iseedarr_skebs)
ENDIF
IF ( ASSOCIATED( grid%spforcs3d ) ) THEN 
  DEALLOCATE(grid%spforcs3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1104,&
'frame/module_domain.f: Failed to deallocate grid%spforcs3d. ')
 endif
  NULLIFY(grid%spforcs3d)
ENDIF
IF ( ASSOCIATED( grid%qpert2d ) ) THEN 
  DEALLOCATE(grid%qpert2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1112,&
'frame/module_domain.f: Failed to deallocate grid%qpert2d. ')
 endif
  NULLIFY(grid%qpert2d)
ENDIF
IF ( ASSOCIATED( grid%evaptzm ) ) THEN 
  DEALLOCATE(grid%evaptzm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1120,&
'frame/module_domain.f: Failed to deallocate grid%evaptzm. ')
 endif
  NULLIFY(grid%evaptzm)
ENDIF
IF ( ASSOCIATED( grid%pconvt ) ) THEN 
  DEALLOCATE(grid%pconvt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1128,&
'frame/module_domain.f: Failed to deallocate grid%pconvt. ')
 endif
  NULLIFY(grid%pconvt)
ENDIF
IF ( ASSOCIATED( grid%zmicvu ) ) THEN 
  DEALLOCATE(grid%zmicvu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1136,&
'frame/module_domain.f: Failed to deallocate grid%zmicvu. ')
 endif
  NULLIFY(grid%zmicvu)
ENDIF
IF ( ASSOCIATED( grid%ideep2d ) ) THEN 
  DEALLOCATE(grid%ideep2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1144,&
'frame/module_domain.f: Failed to deallocate grid%ideep2d. ')
 endif
  NULLIFY(grid%ideep2d)
ENDIF
IF ( ASSOCIATED( grid%snowsh ) ) THEN 
  DEALLOCATE(grid%snowsh,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1152,&
'frame/module_domain.f: Failed to deallocate grid%snowsh. ')
 endif
  NULLIFY(grid%snowsh)
ENDIF
IF ( ASSOCIATED( grid%vten_cu ) ) THEN 
  DEALLOCATE(grid%vten_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1160,&
'frame/module_domain.f: Failed to deallocate grid%vten_cu. ')
 endif
  NULLIFY(grid%vten_cu)
ENDIF
IF ( ASSOCIATED( grid%pbup_cu ) ) THEN 
  DEALLOCATE(grid%pbup_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1168,&
'frame/module_domain.f: Failed to deallocate grid%pbup_cu. ')
 endif
  NULLIFY(grid%pbup_cu)
ENDIF
IF ( ASSOCIATED( grid%rlwp_cu ) ) THEN 
  DEALLOCATE(grid%rlwp_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1176,&
'frame/module_domain.f: Failed to deallocate grid%rlwp_cu. ')
 endif
  NULLIFY(grid%rlwp_cu)
ENDIF
IF ( ASSOCIATED( grid%uu_emf_cu ) ) THEN 
  DEALLOCATE(grid%uu_emf_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1184,&
'frame/module_domain.f: Failed to deallocate grid%uu_emf_cu. ')
 endif
  NULLIFY(grid%uu_emf_cu)
ENDIF
IF ( ASSOCIATED( grid%qrten_cu ) ) THEN 
  DEALLOCATE(grid%qrten_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1192,&
'frame/module_domain.f: Failed to deallocate grid%qrten_cu. ')
 endif
  NULLIFY(grid%qrten_cu)
ENDIF
IF ( ASSOCIATED( grid%bogbot_cu ) ) THEN 
  DEALLOCATE(grid%bogbot_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1200,&
'frame/module_domain.f: Failed to deallocate grid%bogbot_cu. ')
 endif
  NULLIFY(grid%bogbot_cu)
ENDIF
IF ( ASSOCIATED( grid%exit_rei_cu ) ) THEN 
  DEALLOCATE(grid%exit_rei_cu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1208,&
'frame/module_domain.f: Failed to deallocate grid%exit_rei_cu. ')
 endif
  NULLIFY(grid%exit_rei_cu)
ENDIF
IF ( ASSOCIATED( grid%lcd_old_mp ) ) THEN 
  DEALLOCATE(grid%lcd_old_mp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1216,&
'frame/module_domain.f: Failed to deallocate grid%lcd_old_mp. ')
 endif
  NULLIFY(grid%lcd_old_mp)
ENDIF
IF ( ASSOCIATED( grid%sabv ) ) THEN 
  DEALLOCATE(grid%sabv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1224,&
'frame/module_domain.f: Failed to deallocate grid%sabv. ')
 endif
  NULLIFY(grid%sabv)
ENDIF
IF ( ASSOCIATED( grid%t_veg ) ) THEN 
  DEALLOCATE(grid%t_veg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1232,&
'frame/module_domain.f: Failed to deallocate grid%t_veg. ')
 endif
  NULLIFY(grid%t_veg)
ENDIF
IF ( ASSOCIATED( grid%h2ocan_col ) ) THEN 
  DEALLOCATE(grid%h2ocan_col,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1240,&
'frame/module_domain.f: Failed to deallocate grid%h2ocan_col. ')
 endif
  NULLIFY(grid%h2ocan_col)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_liq2 ) ) THEN 
  DEALLOCATE(grid%h2osoi_liq2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1248,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_liq2. ')
 endif
  NULLIFY(grid%h2osoi_liq2)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_ice_s4 ) ) THEN 
  DEALLOCATE(grid%h2osoi_ice_s4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1256,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_ice_s4. ')
 endif
  NULLIFY(grid%h2osoi_ice_s4)
ENDIF
IF ( ASSOCIATED( grid%t_soisno_s1 ) ) THEN 
  DEALLOCATE(grid%t_soisno_s1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1264,&
'frame/module_domain.f: Failed to deallocate grid%t_soisno_s1. ')
 endif
  NULLIFY(grid%t_soisno_s1)
ENDIF
IF ( ASSOCIATED( grid%t_soisno8 ) ) THEN 
  DEALLOCATE(grid%t_soisno8,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1272,&
'frame/module_domain.f: Failed to deallocate grid%t_soisno8. ')
 endif
  NULLIFY(grid%t_soisno8)
ENDIF
IF ( ASSOCIATED( grid%snowrds5 ) ) THEN 
  DEALLOCATE(grid%snowrds5,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1280,&
'frame/module_domain.f: Failed to deallocate grid%snowrds5. ')
 endif
  NULLIFY(grid%snowrds5)
ENDIF
IF ( ASSOCIATED( grid%h2osoi_vol2 ) ) THEN 
  DEALLOCATE(grid%h2osoi_vol2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1288,&
'frame/module_domain.f: Failed to deallocate grid%h2osoi_vol2. ')
 endif
  NULLIFY(grid%h2osoi_vol2)
ENDIF
IF ( ASSOCIATED( grid%lwupsubgrid ) ) THEN 
  DEALLOCATE(grid%lwupsubgrid,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1296,&
'frame/module_domain.f: Failed to deallocate grid%lwupsubgrid. ')
 endif
  NULLIFY(grid%lwupsubgrid)
ENDIF
IF ( ASSOCIATED( grid%t_grnd2d ) ) THEN 
  DEALLOCATE(grid%t_grnd2d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1304,&
'frame/module_domain.f: Failed to deallocate grid%t_grnd2d. ')
 endif
  NULLIFY(grid%t_grnd2d)
ENDIF
IF ( ASSOCIATED( grid%watsat3d ) ) THEN 
  DEALLOCATE(grid%watsat3d,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1312,&
'frame/module_domain.f: Failed to deallocate grid%watsat3d. ')
 endif
  NULLIFY(grid%watsat3d)
ENDIF
IF ( ASSOCIATED( grid%ssib_cm ) ) THEN 
  DEALLOCATE(grid%ssib_cm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1320,&
'frame/module_domain.f: Failed to deallocate grid%ssib_cm. ')
 endif
  NULLIFY(grid%ssib_cm)
ENDIF
IF ( ASSOCIATED( grid%ssib_sup ) ) THEN 
  DEALLOCATE(grid%ssib_sup,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1328,&
'frame/module_domain.f: Failed to deallocate grid%ssib_sup. ')
 endif
  NULLIFY(grid%ssib_sup)
ENDIF
IF ( ASSOCIATED( grid%snowden ) ) THEN 
  DEALLOCATE(grid%snowden,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1336,&
'frame/module_domain.f: Failed to deallocate grid%snowden. ')
 endif
  NULLIFY(grid%snowden)
ENDIF
IF ( ASSOCIATED( grid%bio1 ) ) THEN 
  DEALLOCATE(grid%bio1,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1344,&
'frame/module_domain.f: Failed to deallocate grid%bio1. ')
 endif
  NULLIFY(grid%bio1)
ENDIF
IF ( ASSOCIATED( grid%bio2 ) ) THEN 
  DEALLOCATE(grid%bio2,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1352,&
'frame/module_domain.f: Failed to deallocate grid%bio2. ')
 endif
  NULLIFY(grid%bio2)
ENDIF
IF ( ASSOCIATED( grid%bio3 ) ) THEN 
  DEALLOCATE(grid%bio3,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1360,&
'frame/module_domain.f: Failed to deallocate grid%bio3. ')
 endif
  NULLIFY(grid%bio3)
ENDIF
IF ( ASSOCIATED( grid%bio4 ) ) THEN 
  DEALLOCATE(grid%bio4,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1368,&
'frame/module_domain.f: Failed to deallocate grid%bio4. ')
 endif
  NULLIFY(grid%bio4)
ENDIF
IF ( ASSOCIATED( grid%fwetxy ) ) THEN 
  DEALLOCATE(grid%fwetxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1376,&
'frame/module_domain.f: Failed to deallocate grid%fwetxy. ')
 endif
  NULLIFY(grid%fwetxy)
ENDIF
IF ( ASSOCIATED( grid%snliqxy ) ) THEN 
  DEALLOCATE(grid%snliqxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1384,&
'frame/module_domain.f: Failed to deallocate grid%snliqxy. ')
 endif
  NULLIFY(grid%snliqxy)
ENDIF
IF ( ASSOCIATED( grid%q2mbxy ) ) THEN 
  DEALLOCATE(grid%q2mbxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1392,&
'frame/module_domain.f: Failed to deallocate grid%q2mbxy. ')
 endif
  NULLIFY(grid%q2mbxy)
ENDIF
IF ( ASSOCIATED( grid%fsaxy ) ) THEN 
  DEALLOCATE(grid%fsaxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1400,&
'frame/module_domain.f: Failed to deallocate grid%fsaxy. ')
 endif
  NULLIFY(grid%fsaxy)
ENDIF
IF ( ASSOCIATED( grid%chvxy ) ) THEN 
  DEALLOCATE(grid%chvxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1408,&
'frame/module_domain.f: Failed to deallocate grid%chvxy. ')
 endif
  NULLIFY(grid%chvxy)
ENDIF
IF ( ASSOCIATED( grid%trxy ) ) THEN 
  DEALLOCATE(grid%trxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1416,&
'frame/module_domain.f: Failed to deallocate grid%trxy. ')
 endif
  NULLIFY(grid%trxy)
ENDIF
IF ( ASSOCIATED( grid%areaxy ) ) THEN 
  DEALLOCATE(grid%areaxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1424,&
'frame/module_domain.f: Failed to deallocate grid%areaxy. ')
 endif
  NULLIFY(grid%areaxy)
ENDIF
IF ( ASSOCIATED( grid%eqzwt ) ) THEN 
  DEALLOCATE(grid%eqzwt,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1432,&
'frame/module_domain.f: Failed to deallocate grid%eqzwt. ')
 endif
  NULLIFY(grid%eqzwt)
ENDIF
IF ( ASSOCIATED( grid%qfrocxy ) ) THEN 
  DEALLOCATE(grid%qfrocxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1440,&
'frame/module_domain.f: Failed to deallocate grid%qfrocxy. ')
 endif
  NULLIFY(grid%qfrocxy)
ENDIF
IF ( ASSOCIATED( grid%canhsxy ) ) THEN 
  DEALLOCATE(grid%canhsxy,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1448,&
'frame/module_domain.f: Failed to deallocate grid%canhsxy. ')
 endif
  NULLIFY(grid%canhsxy)
ENDIF
IF ( ASSOCIATED( grid%acthror ) ) THEN 
  DEALLOCATE(grid%acthror,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1456,&
'frame/module_domain.f: Failed to deallocate grid%acthror. ')
 endif
  NULLIFY(grid%acthror)
ENDIF
IF ( ASSOCIATED( grid%acetran ) ) THEN 
  DEALLOCATE(grid%acetran,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1464,&
'frame/module_domain.f: Failed to deallocate grid%acetran. ')
 endif
  NULLIFY(grid%acetran)
ENDIF
IF ( ASSOCIATED( grid%acponding ) ) THEN 
  DEALLOCATE(grid%acponding,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1472,&
'frame/module_domain.f: Failed to deallocate grid%acponding. ')
 endif
  NULLIFY(grid%acponding)
ENDIF
IF ( ASSOCIATED( grid%acsagv ) ) THEN 
  DEALLOCATE(grid%acsagv,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1480,&
'frame/module_domain.f: Failed to deallocate grid%acsagv. ')
 endif
  NULLIFY(grid%acsagv)
ENDIF
IF ( ASSOCIATED( grid%acswdnlsm ) ) THEN 
  DEALLOCATE(grid%acswdnlsm,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1488,&
'frame/module_domain.f: Failed to deallocate grid%acswdnlsm. ')
 endif
  NULLIFY(grid%acswdnlsm)
ENDIF
IF ( ASSOCIATED( grid%acc_qinsur ) ) THEN 
  DEALLOCATE(grid%acc_qinsur,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1496,&
'frame/module_domain.f: Failed to deallocate grid%acc_qinsur. ')
 endif
  NULLIFY(grid%acc_qinsur)
ENDIF
IF ( ASSOCIATED( grid%croptype ) ) THEN 
  DEALLOCATE(grid%croptype,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1504,&
'frame/module_domain.f: Failed to deallocate grid%croptype. ')
 endif
  NULLIFY(grid%croptype)
ENDIF
IF ( ASSOCIATED( grid%fifract ) ) THEN 
  DEALLOCATE(grid%fifract,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1512,&
'frame/module_domain.f: Failed to deallocate grid%fifract. ')
 endif
  NULLIFY(grid%fifract)
ENDIF
IF ( ASSOCIATED( grid%kext_ql ) ) THEN 
  DEALLOCATE(grid%kext_ql,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1520,&
'frame/module_domain.f: Failed to deallocate grid%kext_ql. ')
 endif
  NULLIFY(grid%kext_ql)
ENDIF
IF ( ASSOCIATED( grid%kext_ft_qg ) ) THEN 
  DEALLOCATE(grid%kext_ft_qg,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1528,&
'frame/module_domain.f: Failed to deallocate grid%kext_ft_qg. ')
 endif
  NULLIFY(grid%kext_ft_qg)
ENDIF
IF ( ASSOCIATED( grid%radarvil ) ) THEN 
  DEALLOCATE(grid%radarvil,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1536,&
'frame/module_domain.f: Failed to deallocate grid%radarvil. ')
 endif
  NULLIFY(grid%radarvil)
ENDIF
IF ( ASSOCIATED( grid%afwa_heatidx ) ) THEN 
  DEALLOCATE(grid%afwa_heatidx,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1544,&
'frame/module_domain.f: Failed to deallocate grid%afwa_heatidx. ')
 endif
  NULLIFY(grid%afwa_heatidx)
ENDIF
IF ( ASSOCIATED( grid%afwa_rain ) ) THEN 
  DEALLOCATE(grid%afwa_rain,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1552,&
'frame/module_domain.f: Failed to deallocate grid%afwa_rain. ')
 endif
  NULLIFY(grid%afwa_rain)
ENDIF
IF ( ASSOCIATED( grid%afwa_cape_mu ) ) THEN 
  DEALLOCATE(grid%afwa_cape_mu,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1560,&
'frame/module_domain.f: Failed to deallocate grid%afwa_cape_mu. ')
 endif
  NULLIFY(grid%afwa_cape_mu)
ENDIF
IF ( ASSOCIATED( grid%tornado_dur ) ) THEN 
  DEALLOCATE(grid%tornado_dur,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1568,&
'frame/module_domain.f: Failed to deallocate grid%tornado_dur. ')
 endif
  NULLIFY(grid%tornado_dur)
ENDIF
IF ( ASSOCIATED( grid%tsk_mean ) ) THEN 
  DEALLOCATE(grid%tsk_mean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1576,&
'frame/module_domain.f: Failed to deallocate grid%tsk_mean. ')
 endif
  NULLIFY(grid%tsk_mean)
ENDIF
IF ( ASSOCIATED( grid%swupb_mean ) ) THEN 
  DEALLOCATE(grid%swupb_mean,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1584,&
'frame/module_domain.f: Failed to deallocate grid%swupb_mean. ')
 endif
  NULLIFY(grid%swupb_mean)
ENDIF
IF ( ASSOCIATED( grid%u10_diurn ) ) THEN 
  DEALLOCATE(grid%u10_diurn,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1592,&
'frame/module_domain.f: Failed to deallocate grid%u10_diurn. ')
 endif
  NULLIFY(grid%u10_diurn)
ENDIF
IF ( ASSOCIATED( grid%glw_dtmp ) ) THEN 
  DEALLOCATE(grid%glw_dtmp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1600,&
'frame/module_domain.f: Failed to deallocate grid%glw_dtmp. ')
 endif
  NULLIFY(grid%glw_dtmp)
ENDIF
IF ( ASSOCIATED( grid%elecmag ) ) THEN 
  DEALLOCATE(grid%elecmag,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1608,&
'frame/module_domain.f: Failed to deallocate grid%elecmag. ')
 endif
  NULLIFY(grid%elecmag)
ENDIF
IF ( ASSOCIATED( grid%pc_btxs ) ) THEN 
  DEALLOCATE(grid%pc_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1616,&
'frame/module_domain.f: Failed to deallocate grid%pc_btxs. ')
 endif
  NULLIFY(grid%pc_btxs)
ENDIF
IF ( ASSOCIATED( grid%pc_btxe ) ) THEN 
  DEALLOCATE(grid%pc_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1624,&
'frame/module_domain.f: Failed to deallocate grid%pc_btxe. ')
 endif
  NULLIFY(grid%pc_btxe)
ENDIF
IF ( ASSOCIATED( grid%pc_btys ) ) THEN 
  DEALLOCATE(grid%pc_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1632,&
'frame/module_domain.f: Failed to deallocate grid%pc_btys. ')
 endif
  NULLIFY(grid%pc_btys)
ENDIF
IF ( ASSOCIATED( grid%pc_btye ) ) THEN 
  DEALLOCATE(grid%pc_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1640,&
'frame/module_domain.f: Failed to deallocate grid%pc_btye. ')
 endif
  NULLIFY(grid%pc_btye)
ENDIF
IF ( ASSOCIATED( grid%p_wif_mar ) ) THEN 
  DEALLOCATE(grid%p_wif_mar,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1648,&
'frame/module_domain.f: Failed to deallocate grid%p_wif_mar. ')
 endif
  NULLIFY(grid%p_wif_mar)
ENDIF
IF ( ASSOCIATED( grid%w_wif_feb ) ) THEN 
  DEALLOCATE(grid%w_wif_feb,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1656,&
'frame/module_domain.f: Failed to deallocate grid%w_wif_feb. ')
 endif
  NULLIFY(grid%w_wif_feb)
ENDIF
IF ( ASSOCIATED( grid%i_wif_jan ) ) THEN 
  DEALLOCATE(grid%i_wif_jan,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1664,&
'frame/module_domain.f: Failed to deallocate grid%i_wif_jan. ')
 endif
  NULLIFY(grid%i_wif_jan)
ENDIF
IF ( ASSOCIATED( grid%b_wif_now ) ) THEN 
  DEALLOCATE(grid%b_wif_now,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1672,&
'frame/module_domain.f: Failed to deallocate grid%b_wif_now. ')
 endif
  NULLIFY(grid%b_wif_now)
ENDIF
IF ( ASSOCIATED( grid%b_wif_dec ) ) THEN 
  DEALLOCATE(grid%b_wif_dec,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1680,&
'frame/module_domain.f: Failed to deallocate grid%b_wif_dec. ')
 endif
  NULLIFY(grid%b_wif_dec)
ENDIF
IF ( ASSOCIATED( grid%tpw ) ) THEN 
  DEALLOCATE(grid%tpw,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1688,&
'frame/module_domain.f: Failed to deallocate grid%tpw. ')
 endif
  NULLIFY(grid%tpw)
ENDIF
IF ( ASSOCIATED( grid%lwp_tot ) ) THEN 
  DEALLOCATE(grid%lwp_tot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1696,&
'frame/module_domain.f: Failed to deallocate grid%lwp_tot. ')
 endif
  NULLIFY(grid%lwp_tot)
ENDIF
IF ( ASSOCIATED( grid%tau_qi_tot ) ) THEN 
  DEALLOCATE(grid%tau_qi_tot,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1704,&
'frame/module_domain.f: Failed to deallocate grid%tau_qi_tot. ')
 endif
  NULLIFY(grid%tau_qi_tot)
ENDIF
IF ( ASSOCIATED( grid%ts_swp ) ) THEN 
  DEALLOCATE(grid%ts_swp,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1712,&
'frame/module_domain.f: Failed to deallocate grid%ts_swp. ')
 endif
  NULLIFY(grid%ts_swp)
ENDIF
IF ( ASSOCIATED( grid%ts_tau_qs ) ) THEN 
  DEALLOCATE(grid%ts_tau_qs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1720,&
'frame/module_domain.f: Failed to deallocate grid%ts_tau_qs. ')
 endif
  NULLIFY(grid%ts_tau_qs)
ENDIF
IF ( ASSOCIATED( grid%ts_swddif ) ) THEN 
  DEALLOCATE(grid%ts_swddif,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1728,&
'frame/module_domain.f: Failed to deallocate grid%ts_swddif. ')
 endif
  NULLIFY(grid%ts_swddif)
ENDIF
IF ( ASSOCIATED( grid%q_pl ) ) THEN 
  DEALLOCATE(grid%q_pl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1736,&
'frame/module_domain.f: Failed to deallocate grid%q_pl. ')
 endif
  NULLIFY(grid%q_pl)
ENDIF
IF ( ASSOCIATED( grid%ght_zl ) ) THEN 
  DEALLOCATE(grid%ght_zl,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1744,&
'frame/module_domain.f: Failed to deallocate grid%ght_zl. ')
 endif
  NULLIFY(grid%ght_zl)
ENDIF
IF ( ASSOCIATED( grid%ph_iau ) ) THEN 
  DEALLOCATE(grid%ph_iau,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1752,&
'frame/module_domain.f: Failed to deallocate grid%ph_iau. ')
 endif
  NULLIFY(grid%ph_iau)
ENDIF
IF ( ASSOCIATED( grid%rqciauten ) ) THEN 
  DEALLOCATE(grid%rqciauten,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1760,&
'frame/module_domain.f: Failed to deallocate grid%rqciauten. ')
 endif
  NULLIFY(grid%rqciauten)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k1_btxs ) ) THEN 
  DEALLOCATE(grid%sdirk3_k1_btxs,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1768,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k1_btxs. ')
 endif
  NULLIFY(grid%sdirk3_k1_btxs)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k1_btxe ) ) THEN 
  DEALLOCATE(grid%sdirk3_k1_btxe,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1776,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k1_btxe. ')
 endif
  NULLIFY(grid%sdirk3_k1_btxe)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k1_btys ) ) THEN 
  DEALLOCATE(grid%sdirk3_k1_btys,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1784,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k1_btys. ')
 endif
  NULLIFY(grid%sdirk3_k1_btys)
ENDIF
IF ( ASSOCIATED( grid%sdirk3_k1_btye ) ) THEN 
  DEALLOCATE(grid%sdirk3_k1_btye,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1792,&
'frame/module_domain.f: Failed to deallocate grid%sdirk3_k1_btye. ')
 endif
  NULLIFY(grid%sdirk3_k1_btye)
ENDIF
IF ( ASSOCIATED( grid%chem ) ) THEN 
  DEALLOCATE(grid%chem,STAT=ierr)
 if (ierr.ne.0) then
 CALL wrf_error_fatal3("<stdin>",1800,&
'frame/module_domain.f: Failed to deallocate grid%chem. ')
 endif
  NULLIFY(grid%chem)
ENDIF
END SUBROUTINE deallocs_3



