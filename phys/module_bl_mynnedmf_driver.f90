







 module module_bl_mynnedmf_driver

 use module_bl_mynnedmf_common

 contains




 subroutine mynnedmf_init (                        &
   &  RUBLTEN,RVBLTEN,RTHBLTEN,RQVBLTEN,RQCBLTEN,  &
   &  RQIBLTEN,QKE,                                &
   &  restart,allowed_to_read,                     &
   &  P_QC,P_QI,PARAM_FIRST_SCALAR,                &
   &  IDS,IDE,JDS,JDE,KDS,KDE,                     &
   &  IMS,IME,JMS,JME,KMS,KME,                     &
   &  ITS,ITE,JTS,JTE,KTS,KTE                      )

   implicit none
        
   LOGICAL,INTENT(IN) :: ALLOWED_TO_READ,RESTART

   INTEGER,INTENT(IN) :: IDS,IDE,JDS,JDE,KDS,KDE,  &
        &                IMS,IME,JMS,JME,KMS,KME,  &
        &                ITS,ITE,JTS,JTE,KTS,KTE

   REAL,DIMENSION(IMS:IME,KMS:KME,JMS:JME),INTENT(INOUT) :: &
        &RUBLTEN,RVBLTEN,RTHBLTEN,RQVBLTEN,                 &
        &RQCBLTEN,RQIBLTEN,QKE

   INTEGER,  intent(in) :: P_QC,P_QI,PARAM_FIRST_SCALAR

   INTEGER :: I,J,K,ITF,JTF,KTF

   JTF=MIN0(JTE,JDE-1)
   KTF=MIN0(KTE,KDE-1)
   ITF=MIN0(ITE,IDE-1)

   IF (.NOT.RESTART) THEN
      DO J=JTS,JTF
      DO K=KTS,KTF
      DO I=ITS,ITF
         RUBLTEN(i,k,j)=0.
         RVBLTEN(i,k,j)=0.
         RTHBLTEN(i,k,j)=0.
         RQVBLTEN(i,k,j)=0.
         if( p_qc >= param_first_scalar ) RQCBLTEN(i,k,j)=0.
         if( p_qi >= param_first_scalar ) RQIBLTEN(i,k,j)=0.
      ENDDO
      ENDDO
      ENDDO
   ENDIF

 end subroutine mynnedmf_init

 subroutine mynnedmf_finalize ()
 end subroutine mynnedmf_finalize





 SUBROUTINE mynnedmf_driver           &
                 (ids               , ide               , jds                , jde                , &
                  kds               , kde               , ims                , ime                , &
                  jms               , jme               , kms                , kme                , &
                  its               , ite               , jts                , jte                , &
                  kts               , kte               , flag_qc            , flag_qi            , &
                  flag_qs           , flag_qnc          , flag_qni           ,                      &
                  flag_qnifa        , flag_qnwfa        , flag_qnbca         , initflag           , &
                  restart           , cycling           , delt               ,                      &
                  dxc               , xland             , ps                 , ts                 , &
                  qsfc              , ust               , ch                 , hfx                , &
                  qfx               , wspd              , znt                ,                      &
                  uoce              , voce              , dz                 , u                  , &
                  v                 , w                 , th                 , t3d                , &
                  p                 , exner             , rho                , qv                 , &
                  qc                , qi                , qs                 , qnc                , &
                  qni               , qnifa             , qnwfa              , qnbca              , &

                  rthraten          , pblh              , kpbl               ,                      &
                  cldfra_bl         , qc_bl             , qi_bl              , maxwidth           , &
                  maxmf             , ztop_plume        , qke                , qke_adv            , &
                  tsq               , qsq               , cov                ,                      &
                  el_pbl            , rublten           , rvblten            , rthblten           , &
                  rqvblten          , rqcblten          , rqiblten           , rqsblten           , &
                  rqncblten         , rqniblten         , rqnifablten        , rqnwfablten        , &
                  rqnbcablten       ,                                                               &

                  edmf_a            , edmf_w            ,                                           &
                  edmf_qt           , edmf_thl          , edmf_ent           , edmf_qc            , &
                  sub_thl3d         , sub_sqv3d         , det_thl3d          , det_sqv3d          , &
                  exch_h            , exch_m            , dqke               , qwt                , &
                  qshear            , qbuoy             , qdiss              , sh3d               , &
                  sm3d              , spp_pbl           , pattern_spp_pbl    , icloud_bl          , &
                  bl_mynn_tkeadvect , tke_budget        , bl_mynn_cloudpdf   , bl_mynn_mixlength  , &
                  bl_mynn_closure   , bl_mynn_edmf      , bl_mynn_edmf_mom   , bl_mynn_edmf_tke   , &
                  bl_mynn_output    , bl_mynn_mixscalars, bl_mynn_cloudmix   , bl_mynn_mixqt        &

               )


 use module_bl_mynnedmf, only: mynnedmf


 implicit none






 logical, parameter ::                                  &
         mix_chem   =.false.,                           &
         enh_mix    =.false.,                           &
         rrfs_sd    =.false.,                           &
         smoke_dbg  =.false.
 integer, parameter :: nchem=2, ndvel=2, kdvel=1,       &
          num_vert_mix = 1



 logical, intent(in) ::                                 &
         bl_mynn_tkeadvect,                             &
         cycling
 integer, intent(in) ::                                 &
         bl_mynn_cloudpdf,                              &
         bl_mynn_mixlength,                             &
         icloud_bl,                                     &
         bl_mynn_edmf,                                  &
         bl_mynn_edmf_mom,                              &
         bl_mynn_edmf_tke,                              &
         bl_mynn_cloudmix,                              &
         bl_mynn_mixqt,                                 &
         bl_mynn_output,                                &
         bl_mynn_mixscalars,                            &
         spp_pbl,                                       &
         tke_budget
 real(kind_phys), intent(in) ::                         &
         bl_mynn_closure

 logical,intent(in):: &
    flag_qc,               &     
    flag_qi,               &     
    flag_qs,               &     
    flag_qnc,              &     
    flag_qni,              &     
    flag_qnifa,            &     
    flag_qnwfa,            &     
    flag_qnbca                   
 logical, parameter :: flag_ozone = .false.


 REAL(kind_phys),    intent(in) :: delt, dxc
 LOGICAL, intent(in) :: restart
 INTEGER :: i, j, k, itf, jtf, n
 INTEGER, intent(in) :: initflag,                      &
            IDS,IDE,JDS,JDE,KDS,KDE,                   &
            IMS,IME,JMS,JME,KMS,KME,                   &
            ITS,ITE,JTS,JTE,KTS,KTE


 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), intent(in) ::               &
       u,v,w,t3d,th,rho,exner,p,dz,rthraten
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), intent(inout) ::            &
       rublten,rvblten,rthblten
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), intent(out) ::              &
       qke, qke_adv, el_pbl, sh3d, sm3d, tsq, qsq, cov
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), intent(inout) ::            &
       exch_h, exch_m


 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), optional, intent(inout) ::  &
       rqvblten,rqcblten,rqiblten,rqsblten,rqncblten,rqniblten,                   &
       rqnwfablten,rqnifablten,rqnbcablten 
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), optional, intent(in)  ::    &
       pattern_spp_pbl
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), optional, intent(out) ::    &
       qc_bl, qi_bl, cldfra_bl
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), optional, intent(out) ::    &
       edmf_a,edmf_w,edmf_qt,                                                     &
       edmf_thl,edmf_ent,edmf_qc,                                                 &
       sub_thl3d,sub_sqv3d,det_thl3d,det_sqv3d
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), optional, intent(out) ::    &
       dqke,qWT,qSHEAR,qBUOY,qDISS
 real(kind_phys), dimension(ims:ime,kms:kme,jms:jme), optional, intent(inout) ::  &
       qv,qc,qi,qs,qnc,qni,qnwfa,qnifa,qnbca


 real(kind_phys), dimension(kts:kte) ::                                           &
       u1,v1,w1,th1,tk1,rho1,ex1,p1,dz1,rthraten1
 real(kind_phys), dimension(kts:kte) ::                                           &
       edmf_a1,edmf_w1,edmf_qt1,edmf_thl1,edmf_ent1,edmf_qc1,                     &
       sub_thl1,sub_sqv1,det_thl1,det_sqv1
 real(kind_phys), dimension(kts:kte) ::                                           &
       qc_bl1, qi_bl1, cldfra_bl1, pattern_spp_pbl1
 real(kind_phys), dimension(kts:kte) ::                                           &
       dqke1,qWT1,qSHEAR1,qBUOY1,qDISS1
 real(kind_phys), dimension(kts:kte) ::                                           &
       qke1, qke_adv1, el1, sh1, sm1, km1, kh1, tsq1, qsq1, cov1
 real(kind_phys), dimension(kts:kte) ::                                           &
       qv1,qc1,qi1,qs1,qnc1,qni1,qnwfa1,qnifa1,qnbca1,ozone1
 real(kind_phys), dimension(kts:kte) ::                                           &
       du1,dv1,dth1,dqv1,dqc1,dqi1,dqs1,                                          &
       dqni1,dqnc1,dqnwfa1,dqnifa1,dqnbca1,dozone1



 real(kind_phys), dimension(kms:kme,nchem)  :: chem
 real(kind_phys), dimension(ndvel)          :: vd
 real(kind_phys), dimension(ims:ime,jms:jme):: frp_mean, emis_ant_no



 real(kind_phys), dimension(ims:ime,jms:jme), intent(in) ::                       &
       xland,ts,qsfc,ps,ch,hfx,qfx,ust,wspd,znt,                                  &
       uoce,voce
 real(kind_phys), dimension(ims:ime,jms:jme), optional, intent(out) ::            &
       maxwidth,maxmf,ztop_plume
 real(kind_phys), dimension(ims:ime,jms:jme), intent(out) ::                      &
       pblh
 integer, dimension(ims:ime,jms:jme), intent(out) ::                              &
       kpbl


 real(kind_phys), dimension(kts:kte)                 :: delp,sqv1,sqc1,sqi1,sqs1,kzero
 logical, parameter                                  :: debug = .false.
 real(kind_phys), dimension(its:ite,kts:kte,jts:jte) :: ozone,rO3blten
 real(kind_phys):: xland1,ts1,qsfc1,ps1,ch1,hfx1,qfx1,ust1,wspd1,                 &
       znt1,uoce1,voce1,pblh1,maxwidth1,maxmf1,ztop_plume1,                       &
       frp1,emis1
 integer   :: kpbl1
      

 character :: errmsg   
 integer   :: errflg   

 if (debug) then
    write(0,*)"=============================================="
    write(0,*)"in mynn wrapper..."
    write(0,*)"initflag=",initflag," restart =",restart
 endif

 errmsg = " "
 errflg = 0
   
 jtf=MIN0(JTE,JDE-1)
 itf=MIN0(ITE,IDE-1)

 
 ozone            =0.0
 rO3blten         =0.0
 kzero            =0.0
 
 qc_bl1           =0.0
 qi_bl1           =0.0
 cldfra_bl1       =0.0
 
 pattern_spp_pbl1 =0.0
 
 qke1             =0.0
 qke_adv1         =0.0
 el1              =0.0
 sh1              =0.0
 sm1              =0.0
 kh1              =0.0
 km1              =0.0
 tsq1             =0.0
 qsq1             =0.0
 cov1             =0.0
 
 dqke1            =0.0
 qWT1             =0.0
 qSHEAR1          =0.0
 qBUOY1           =0.0
 qDISS1           =0.0
 
 edmf_a1          =0.0
 edmf_w1          =0.0
 edmf_qt1         =0.0
 edmf_thl1        =0.0
 edmf_ent1        =0.0
 edmf_qc1         =0.0
 sub_thl1         =0.0
 sub_sqv1         =0.0
 det_thl1         =0.0
 det_sqv1         =0.0
 
 qv1              =0.0
 qc1              =0.0
 qi1              =0.0
 qs1              =0.0
 qnc1             =0.0
 qni1             =0.0
 qnwfa1           =0.0
 qnifa1           =0.0
 qnbca1           =0.0
 ozone1           =0.0
 
 du1              =0.0
 dv1              =0.0
 dth1             =0.0
 dqv1             =0.0
 dqc1             =0.0
 dqi1             =0.0
 dqs1             =0.0
 dqni1            =0.0
 dqnc1            =0.0
 dqnwfa1          =0.0
 dqnifa1          =0.0
 dqnbca1          =0.0
 dozone1          =0.0

 
 
 
 do j = jts, jte 
   do i = its, ite 
      
      do k=kts,kte
         u1(k)       = u(i,k,j)
         v1(k)       = v(i,k,j)
         w1(k)       = w(i,k,j)
         th1(k)      = th(i,k,j)
         p1(k)       = p(i,k,j)
         ex1(k)      = exner(i,k,j)
         rho1(k)     = rho(i,k,j)
         tk1(k)      = t3d(i,k,j)
         dz1(k)      = dz(i,k,j)
         rthraten1(k)= rthraten(i,k,j)
      enddo
      
      xland1         = xland(i,j)
      ts1            = ts(i,j)
      qsfc1          = qsfc(i,j)
      ps1            = ps(i,j)
      ust1           = ust(i,j)
      ch1            = ch(i,j)
      wspd1          = wspd(i,j)
      uoce1          = uoce(i,j)
      voce1          = voce(i,j)
      znt1           = znt(i,j)
      
      pblh1          = pblh(i,j)
      kpbl1          = kpbl(i,j)
      if (bl_mynn_edmf > 0) then
         maxwidth1      = maxwidth(i,j)
         maxmf1         = maxmf(i,j)
         ztop_plume1    = ztop_plume(i,j)
      endif
      
      
      
      
      
      hfx1 = hfx(i,j)
      if (hfx1 > 1200.) then
         
         hfx1 = 1200.
      endif
      if (hfx1 < -600.) then
         
         hfx1 = -600.
      endif
      qfx1 = qfx(i,j)
      if (qfx1 > 9e-4) then
         
         qfx1 = 9e-4
      endif
      if (qfx1 < -3e-4) then
         
         qfx1 = -3e-4
      endif
      
      
      if (spp_pbl > 0) then
         do k=kts,kte
            pattern_spp_pbl1(k) = pattern_spp_pbl(i,k,j)
         enddo
      endif

      
      if (initflag .eq. 0 .or. restart) THEN
         
         if (icloud_bl > 0) then
            do k=kts,kte
               qc_bl1(k)     = qc_bl(i,k,j)
               qi_bl1(k)     = qi_bl(i,k,j)
               cldfra_bl1(k) = cldfra_bl(i,k,j)
            enddo
         endif

         
         do k=kts,kte
            qke1(k) = qke(i,k,j)
            qsq1(k) = qsq(i,k,j)
            tsq1(k) = tsq(i,k,j)
            cov1(k) = cov(i,k,j)
            sh1(k)  = sh3d(i,k,j)
            sm1(k)  = sm3d(i,k,j)
            kh1(k)  = exch_h(i,k,j)
            km1(k)  = exch_m(i,k,j)
            el1(k)  = el_pbl(i,k,j)
         enddo
         if (bl_mynn_tkeadvect) then
            qke_adv1(kts:kte) = qke_adv(i,kts:kte,j)
         else
            qke_adv1(kts:kte) = qke(i,kts:kte,j)
         endif
      endif
      
      
      do k=kts,kte
         qv1(k) = qv(i,k,j)
      enddo
      if (flag_qc) then
         do k=kts,kte
            qc1(k) = qc(i,k,j)
         enddo
      endif
      if (flag_qi) then
         do k=kts,kte
            qi1(k) = qi(i,k,j)
         enddo
      endif
      if (flag_qs) then
         do k=kts,kte
            qs1(k) = qs(i,k,j)
         enddo
      endif
      if (flag_qnc) then
         do k=kts,kte
            qnc1(k) = qnc(i,k,j)
         enddo
      endif
      if (flag_qni) then
         do k=kts,kte
            qni1(k) = qni(i,k,j)
         enddo
      endif
      if (flag_qnwfa) then
         do k=kts,kte
            qnwfa1(k) = qnwfa(i,k,j)
         enddo
      endif
      if (flag_qnifa) then
         do k=kts,kte
            qnifa1(k) = qnifa(i,k,j)
         enddo
      endif
      if (flag_qnbca) then
         do k=kts,kte
            qnbca1(k) = qnbca(i,k,j)
         enddo
      endif
      if (flag_ozone) then
         do k=kts,kte
            ozone1(k) = ozone(i,k,j)
         enddo
      endif

      chem        = 0.0
      vd          = 0.0
      frp_mean    = 0.0
      emis_ant_no = 0.0

      frp1        = frp_mean(i,j)
      emis1       = emis_ant_no(i,j)

      





      
      call mynnedmf_pre_run(kte    , flag_qc , flag_qi , flag_qs ,   &
                            qv1    , qc1     , qi1     , qs1     ,   &
                            sqv1   , sqc1    , sqi1    , sqs1    ,   &
                            errmsg , errflg                          )


      call mynnedmf( &
            i               = i             , j           = j             ,                              &
            initflag        = initflag      , restart     = restart       , cycling     = cycling      , &
            delt            = delt          , dz1         = dz1           , dx          = dxc          , &
            znt             = znt1          , u1          = u1            , v1          = v1           , &
            w1              = w1            , th1         = th1           , sqv1        = sqv1         , &
            sqc1            = sqc1          , sqi1        = sqi1          , sqs1        = sqs1         , &
            qnc1            = qnc1          , qni1        = qni1          , qnwfa1      = qnwfa1       , &
            qnifa1          = qnifa1        , qnbca1      = qnbca1        , ozone1      = ozone1       , &
            p1              = p1            , ex1         = ex1           , rho1        = rho1         , &
            tk1             = tk1           , xland       = xland1        , ts          = ts1          , &
            qsfc            = qsfc1         , ps          = ps1           , ust         = ust1         , &
            ch              = ch1           , hfx         = hfx1          , qfx         = qfx1         , &
            wspd            = wspd1         , uoce        = uoce1         , voce        = voce1        , &
            qke1            = qke1          , qke_adv1    = qke_adv1      ,                              &
            tsq1            = tsq1          , qsq1        = qsq1          , cov1        = cov1         , &
            rthraten1       = rthraten1     , du1         = du1           , dv1         = dv1          , &
            dth1            = dth1          , dqv1        = dqv1          , dqc1        = dqc1         , &
            dqi1            = dqi1          , dqs1        = kzero         , dqnc1       = dqnc1        , &
            dqni1           = dqni1         , dqnwfa1     = dqnwfa1       , dqnifa1     = dqnifa1      , &
            dqnbca1         = dqnbca1       , dozone1     = dozone1       , kh1         = kh1          , &
            km1             = km1           , pblh        = pblh1         , kpbl        = kpbl1        , &
            el1             = el1           , dqke1       = dqke1         , qwt1        = qwt1         , &
            qshear1         = qshear1       , qbuoy1      = qbuoy1        , qdiss1      = qdiss1       , &
            sh1             = sh1           , sm1         = sm1           , qc_bl1      = qc_bl1       , &
            qi_bl1          = qi_bl1        , cldfra_bl1  = cldfra_bl1    ,                              &
            edmf_a1         = edmf_a1       , edmf_w1     = edmf_w1       , edmf_qt1    = edmf_qt1     , &
            edmf_thl1       = edmf_thl1     , edmf_ent1   = edmf_ent1     , edmf_qc1    = edmf_qc1     , &
            sub_thl1        = sub_thl1      , sub_sqv1    = sub_sqv1      , det_thl1    = det_thl1     , &
            det_sqv1        = det_sqv1      ,                                                            &
            maxwidth        = maxwidth1     , maxmf       = maxmf1        , ztop_plume  = ztop_plume1  , &
            flag_qc         = flag_qc       , flag_qi     = flag_qi       , flag_qs     = flag_qs      , &
            flag_ozone      = flag_ozone    , flag_qnc    = flag_qnc      , flag_qni    = flag_qni     , &
            flag_qnwfa      = flag_qnwfa    , flag_qnifa  = flag_qnifa    , flag_qnbca  = flag_qnbca   , &
            pattern_spp_pbl1= pattern_spp_pbl1,                                                          &

            mix_chem        = mix_chem      , enh_mix     = enh_mix       , rrfs_sd     = rrfs_sd      , &
            smoke_dbg       = smoke_dbg     , nchem       = nchem         , kdvel       = kdvel        , &
            ndvel           = ndvel         , chem        = chem          , emis_ant_no = emis1        , &
            frp             = frp1          , vdep        = vd                                         , &

            bl_mynn_tkeadvect  = bl_mynn_tkeadvect    , &
            tke_budget         = tke_budget           , &
            bl_mynn_cloudpdf   = bl_mynn_cloudpdf     , &
            bl_mynn_mixlength  = bl_mynn_mixlength    , &
            closure            = bl_mynn_closure      , &
            bl_mynn_edmf       = bl_mynn_edmf         , &
            bl_mynn_edmf_mom   = bl_mynn_edmf_mom     , &
            bl_mynn_edmf_tke   = bl_mynn_edmf_tke     , &
            bl_mynn_mixscalars = bl_mynn_mixscalars   , &
            bl_mynn_output     = bl_mynn_output       , &
            bl_mynn_cloudmix   = bl_mynn_cloudmix     , &
            bl_mynn_mixqt      = bl_mynn_mixqt        , &
            icloud_bl          = icloud_bl            , &
            spp_pbl            = spp_pbl              , &
            kts = kts , kte = kte , errmsg = errmsg , errflg = errflg )

      
      call  mynnedmf_post_run(                                        &
                kte      , flag_qc  , flag_qi  , flag_qs , delt     , &
                qv1      , qc1      , qi1      , qs1     , dqv1     , &
                dqc1     , dqi1     , dqs1     , errmsg  , errflg     )

      if (debug) then
         print*,"In mynnedmf driver, after call to mynnedmf"
      endif

      
      do k=kts,kte
         qke(i,k,j)     = qke1(k)
         el_pbl(i,k,j)  = el1(k)
         sh3d(i,k,j)    = sh1(k)
         sm3d(i,k,j)    = sm1(k)
         exch_h(i,k,j)  = kh1(k)
         exch_m(i,k,j)  = km1(k)
         tsq(i,k,j)     = tsq1(k)
         qsq(i,k,j)     = qsq1(k)
         cov(i,k,j)     = cov1(k)
         qke_adv(i,k,j) = qke_adv1(k)
      enddo

      
      kpbl(i,j)        = kpbl1
      pblh(i,j)        = pblh1
      if (bl_mynn_edmf > 0) then
         maxwidth(i,j)    = maxwidth1
         maxmf(i,j)       = maxmf1
         ztop_plume(i,j)  = ztop_plume1
      endif

      
      do k=kts,kte
         RUBLTEN(i,k,j)  = du1(k)
         RVBLTEN(i,k,j)  = dv1(k)
         RTHBLTEN(i,k,j) = dth1(k)
      enddo
      if (present(RQVBLTEN)) then
         do k=kts,kte
            RQVBLTEN(i,k,j) = dqv1(k)
         enddo
      endif
      if (present(RQCBLTEN)) then
         do k=kts,kte
            RQCBLTEN(i,k,j) = dqc1(k)
         enddo
      endif
      if (present(RQIBLTEN)) then
         do k=kts,kte
            RQIBLTEN(i,k,j) = dqi1(k)
         enddo
      endif
      if (present(RQSBLTEN)) then 
        do k=kts,kte
           RQSBLTEN(i,k,j) = dqs1(k)
        enddo
      endif
      if (present(RQNCBLTEN)) then
         do k=kts,kte
            RQNCBLTEN(i,k,j) = dqnc1(k)
         enddo
      endif
      if (present(RQNIBLTEN)) then
         do k=kts,kte
            RQNIBLTEN(i,k,j) = dqni1(k)
         enddo
      endif
      if (present(RQNWFABLTEN)) then
         do k=kts,kte
            RQNWFABLTEN(i,k,j) = dqnwfa1(k)
         enddo
      endif
      if (present(RQNIFABLTEN)) then
         do k=kts,kte
            RQNIFABLTEN(i,k,j) = dqnifa1(k)
         enddo
      endif
      if (present(RQNBCABLTEN)) then
         do k=kts,kte
            RQNBCABLTEN(i,k,j) = dqnbca1(k)
         enddo
      endif

     
      if (icloud_bl > 0) then
         do k=kts,kte
            qc_bl(i,k,j)     = qc_bl1(k)/(1.0 - sqv1(k))
            qi_bl(i,k,j)     = qi_bl1(k)/(1.0 - sqv1(k))
            cldfra_bl(i,k,j) = cldfra_bl1(k)
         enddo
      endif

      if (tke_budget .eq. 1) then
         do k=kts,kte
            dqke(i,k,j)      = dqke1(k)
            qwt(i,k,j)       = qwt1(k)
            qshear(i,k,j)    = qshear1(k)
            qbuoy(i,k,j)     = qbuoy1(k)
            qdiss(i,k,j)     = qdiss1(k)
         enddo
      endif

      if (bl_mynn_output > 0) then
         do k=kts,kte
            edmf_a(i,k,j)    = edmf_a1(k)
            edmf_w(i,k,j)    = edmf_w1(k)
            edmf_qt(i,k,j)   = edmf_qt1(k)
            edmf_thl(i,k,j)  = edmf_thl1(k)
            edmf_ent(i,k,j)  = edmf_ent1(k)
            edmf_qc(i,k,j)   = edmf_qc1(k)
            sub_thl3d(i,k,j) = sub_thl1(k)
            sub_sqv3d(i,k,j) = sub_sqv1(k)
            det_thl3d(i,k,j) = det_thl1(k)
            det_sqv3d(i,k,j) = det_sqv1(k)
         enddo
      endif



   enddo  
   enddo  

   if (debug) then
      print*,"In mynnedmf_driver, at end"
   endif

 end subroutine mynnedmf_driver


 SUBROUTINE moisture_check2(kte, delt, dp, exner, &
                             qv, qc, qi, th        )
  
  
  
  
  
  
  
  
  
  

    implicit none
    integer,  intent(in)     :: kte
    real, intent(in)     :: delt
    real, dimension(kte), intent(in)     :: dp
    real, dimension(kte), intent(in)     :: exner
    real, dimension(kte), intent(inout)  :: qv, qc, qi, th
    integer   k
    real ::  dqc2, dqi2, dqv2, sum, aa, dum
    real, parameter :: qvmin1= 1e-8,    & 
                       qvmin = 1e-20,   & 
                       qcmin = 0.0,     &
                       qimin = 0.0

    do k = kte, 1, -1  
       dqc2 = max(0.0, qcmin-qc(k)) 
       dqi2 = max(0.0, qimin-qi(k)) 

       
       qc(k)  = qc(k)  +  dqc2
       qi(k)  = qi(k)  +  dqi2
       qv(k)  = qv(k)  -  dqc2 - dqi2
       
       
       
       
       th(k)  = th(k)  +  xlvcp*dqc2 + &
                          xlscp*dqi2

       
       if (k .eq. 1) then
          dqv2   = max(0.0, qvmin1-qv(k)) 
          qv(k)  = qv(k)  + dqv2
          qv(k)  = max(qv(k),qvmin1)
          dqv2   = 0.0
       else
          dqv2   = max(0.0, qvmin-qv(k)) 
          qv(k)  = qv(k)  + dqv2
          qv(k-1)= qv(k-1)  - dqv2*dp(k)/dp(k-1)
          qv(k)  = max(qv(k),qvmin)
       endif
       qc(k) = max(qc(k),qcmin)
       qi(k) = max(qi(k),qimin)
    end do



    
    if( dqv2 .gt. 1.e-20 ) then
        sum = 0.0
        do k = 1, kte
           if( qv(k) .gt. 2.0*qvmin ) sum = sum + qv(k)*dp(k)
        enddo
        aa = dqv2*dp(1)/max(1.e-20,sum)
        if( aa .lt. 0.5 ) then
            do k = 1, kte
               if( qv(k) .gt. 2.0*qvmin ) then
                   dum    = aa*qv(k)
                   qv(k)  = qv(k) - dum
               endif
            enddo
        else
        

        endif
    endif

    return

END SUBROUTINE moisture_check2





 subroutine mynnedmf_pre_run(kte,f_qc,f_qi,f_qs,qv,qc,qi,qs,sqv,sqc,sqi,sqs,errmsg,errflg)



 logical,intent(in):: &
    f_qc,      &              
    f_qi,      &              
    f_qs                      

 integer,intent(in):: kte

 real(kind=kind_phys),intent(in),dimension(1:kte):: &
    qv,        &              
    qc,        &              
    qi,        &              
    qs                        


 character(len=*),intent(out):: &
    errmsg                    

 integer,intent(out):: &
    errflg                    

 real(kind=kind_phys),intent(out),dimension(1:kte):: &
    sqv,       &              
    sqc,       &              
    sqi,       &              
    sqs                       


 integer:: k
 integer,parameter::kts=1



 do k = kts,kte
    sqc(k) = 0._kind_phys
    sqi(k) = 0._kind_phys
 enddo


 do k = kts,kte
    sqv(k) = qv(k)/(1.+qv(k))
 enddo


 if(f_qc) then
    do k = kts,kte
       sqc(k) = qc(k)/(1.+qv(k))
    enddo
 endif
 if(f_qi) then
    do k = kts,kte
       sqi(k) = qi(k)/(1.+qv(k))
    enddo
 endif
 if(f_qs) then
    do k = kts,kte
       sqs(k) = qs(k)/(1.+qv(k))
    enddo
 endif


 errflg = 0
 errmsg = " "

 end subroutine mynnedmf_pre_run




 subroutine mynnedmf_post_run(kte,f_qc,f_qi,f_qs,delt,qv,qc,qi,qs,dqv,dqc,dqi,dqs,errmsg,errflg)



 logical,intent(in):: &
    f_qc, &                   
    f_qi, &                   
    f_qs                      

 integer,intent(in):: kte

 real(kind=kind_phys),intent(in):: &
    delt                      

 real(kind=kind_phys),intent(in),dimension(1:kte):: &
    qv,   &                   
    qc,   &                   
    qi,   &                   
    qs                        


 real(kind=kind_phys),intent(inout),dimension(1:kte):: &
    dqv,  &                   
    dqc,  &                   
    dqi,  &                   
    dqs                       


 character(len=*),intent(out):: errmsg
 integer,intent(out):: errflg



 integer:: k
 integer,parameter::kts=1
 real(kind=kind_phys):: rq,sq,tem
 real(kind=kind_phys),dimension(1:kte):: sqv,sqc,sqi,sqs


 do k = kts,kte
    sq = qv(k)/(1.+qv(k))      
    sqv(k) = sq + dqv(k)*delt  
    rq = sqv(k)/(1.-sqv(k))    
    dqv(k) = (rq - qv(k))/delt 
 enddo

 if (f_qc) then
    do k = kts,kte
       sq = qc(k)/(1.+qv(k))
       sqc(k) = sq + dqc(k)*delt
       rq  = sqc(k)*(1.+sqv(k))
       dqc(k) = (rq - qc(k))/delt
    enddo
 endif

 if (f_qi) then
    do k = kts,kte
       sq = qi(k)/(1.+qv(k))
       sqi(k) = sq + dqi(k)*delt
       rq = sqi(k)*(1.+sqv(k))
       dqi(k) = (rq - qi(k))/delt
    enddo
 endif

 if (f_qs) then
    do k = kts,kte
       sq = qs(k)/(1.+qv(k))
       sqs(k) = sq + dqs(k)*delt
       rq = sqs(k)*(1.+sqv(k))
       dqs(k) = (rq - qs(k))/delt
    enddo
 endif
      

 errmsg = " "
 errflg = 0

 end subroutine mynnedmf_post_run



END MODULE module_bl_mynnedmf_driver


