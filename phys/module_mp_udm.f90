





  module module_mp_udm
   use module_mp_radar
   use module_gfs_machine , only : kind_phys




   real, parameter, private :: &
          dtcldcr  = 180.  ,&    
          n0r    = 8.e6    ,&    
          n0s    = 2.e6    ,&    
          n0g    = 4.e6    ,&    
          n0h    = 4.e4    ,&    
          n0smax = 1.e11   ,&    
          alpha  = .12     ,&    
          avti   = 1.49e4  ,&    
          bvti   = 1.31    ,&    
          cxmi   = (1./11.9)**2,&
          dxmi   = 2.0     ,&    
          pi_    = 3.141593,&    
          deni   = 500.    ,&    
          avtis  = 2.71e3  ,&    
          bvtis  = 1.0     ,&    
          cxmis  = pi_*deni/6.,& 
          dxmis  = 3.0     ,&    
          avtr   = 841.9   ,&    
          bvtr   = 0.8     ,&    
          avts   = 11.72   ,&    
          bvts   = .41     ,&    
          avtg   = 553.1   ,&    
          bvtg   = 0.97    ,&    
          deng   = 500.    ,&    
          avth   = 168.0   ,&    
          bvth   = 0.72    ,&    
          denh   = 912.    ,&    
          lamdasmax = 1.e5 ,&    
          lamdagmax = 2.e4 ,&    
          lamdahmax = 2.e4 ,&    
          peaut  = .55     ,&    
          r0     = .8e-5   ,&    
          xncr   = 3.e8    ,&    
          xmyu   = 1.718e-5,&    
          dimax  = 500.e-6 ,&    
          ni0max = 500.e3  ,&    
          pfrz1  = 100.    ,&    
          pfrz2  = 0.66    ,&    
          qcmin  = 1.e-12  ,&    
          qrmin  = 1.e-9   ,&    
          eacrc  = 1.0     ,&    
          eachs  = 1.0     ,&    
          eachg  = 0.5     ,&    
          cd     = 0.6     ,&    
          dens   = 100.    ,&    
          qs0    = 0.6e-3        

   real, parameter, private :: &
          drmin   = 50.e-6    ,&  
          drmax   = 5000.e-6  ,&  
          drcoeff = (24.)**(.3333333),& 
          lamdarmax = drcoeff/(drmin*.1)  ,& 
          lamdarmin = drcoeff/(drmax*10.)  ,& 
          nrmin     = 1.e-6   ,&  
          nrmax     = 300.e6  ,&  
          dcmin     = 1.e-6   ,&  
          dcmax     = 100.e-6 ,&  
          lamdacmax = 1./(dcmin*.1),&  
          lamdacmin = 1./(dcmax*10.),& 
          ncmin     = 1.e-3   ,&  
          ncmax     = 30000.e6,&  
          satmax  = 0.0048   ,&  
          actk    = 0.6      ,&  
          actr    = 1.5e-6   ,&  
          ncrk1   = 3.03e3   ,&  
          ncrk2   = 2.59e15  ,&  
          di5000  = 5000.e-6 ,&  
          di2000  = 2000.e-6 ,&  
          di1000  = 1000.e-6 ,&  
          di600   = 600.e-6  ,&  
          di100   = 100.e-6  ,&  
          di82    = 82.e-6   ,&  
          di50    = 50.e-6   ,&  
          di15    = 15.e-6   ,&  
          di5     = 5.e-6    ,&  
          di2     = 2.e-6    ,&  
          di1     = 1.e-6        
   real, parameter, private :: &
          ccn_0   = 50.e+6,   &  
          ccnmax  = 20000.e+6,&  
          ccnmin  = 50.e+6       



   real, parameter, private :: &
          recmin  = 2.51e-6  ,&    
          recmax  = 50.e-6   ,&    
          reimin  = 5.01e-6  ,&    
          reimax  = 125.e-6  ,&    
          resmin  = 25.e-6   ,&    
          resmax  = 999.e-6        



   real, parameter, private :: &
          sedi_semi_cfl = 10000.0





   integer, parameter                  ::  nxsvp = 7501
   real                     ::  c1xsvp,c2xsvp
   real,  dimension(nxsvp)  ::  tbsvp
   integer, parameter                  ::  nxsvpw = nxsvp
   real                     ::  c1xsvpw,c2xsvpw
   real,  dimension(nxsvpw) ::  tbsvpw

   real, parameter, private :: &
         cliq_ = 4.1900e+3     ,&    
         cice_ = 2.1060e+3     ,&    
         cvap_ = 1.8700e+3     ,&    
         cpd_  = 1.00464e+3    ,&    
         hvap_ = 2.5010e+6     ,&    
         hsub_ = 2.8340e+6     ,&    
         psat_ = 6.1078e+2     ,&    
         rd_   = 2.8704e+2     ,&    
         rv_   = 4.6150e+2     ,&    
         ttp_  = 2.7316e+2           
  real, parameter, private :: &
         psatk = psat_,                         &
         dldt  = cvap_-cliq_,                   &
         dldti = cvap_-cice_,                   &
         xa    = -dldt/rv_,                     &
         xb    = xa+hvap_/(rv_*ttp_),           &
         xai   = -dldti/rv_,                    &
         xbi   = xai+hsub_/(rv_*ttp_)



   integer, parameter    ::  nxshape = 9991 
   real                            ::  c1xshape,c2xshape
   real, dimension(nxshape)        ::  tbshape



   integer, parameter    ::  nxaut = 9991 
   integer, parameter    ::  numax = 15   
   real       ::  c1xaut,c2xaut
   double precision, dimension(nxaut,numax)        ::  tbaut_qc_tot, tbaut_qc_sub
   double precision, dimension(nxaut,numax)        ::  tbaut_nc_tot, tbaut_nc_sub




   real, parameter, private :: &
         t1_sphere = -10.    ,&     
         t2_sphere = -33.           



   real, parameter, private :: &
          zero_0 = 0.0                   ,&
          one_1 = 1.0
   logical, parameter, private :: flgzero = .true.



   real, save ::                                                    &
             qc_ocean,qc_land,qck1,pidnc,bvtr1,bvtr2,bvtr3,bvtr4,bvtr5,        &
             bvtr6,bvtr7, bvtr2o5,bvtr3o5,                                     &
             g1pbr,g2pbr,g3pbr,g4pbr,g5pbr,g6pbr,g7pbr,                        &
             g5pbro2,g7pbro2,pi,pisq,                                          &
             pvtr,pvtrn,eacrr,pacrr,pidn0r,pidnr,                              &
             precr1,precr2,roqimax,bvts1,bvts2,                                &
             bvts3,bvts4,g1pbs,g3pbs,g4pbs,g5pbso2,                            &
             pvts,pacrs,precs1,precs2,pidn0s,xlv1,pacrc,                       &
             bvtg1,bvtg2,bvtg3,bvtg4,g1pbg,g3pbg,g4pbg,                        &
             g5pbgo2,pvtg,pacrg,precg1,precg2,pidn0g,                          &
             g6pbgh,precg3,bvth2,bvth3,bvth4,g3pbh,g4pbh,g5pbho2,pacrh,pvth,   &
             prech1,prech2,prech3,pidn0h,                                      &
             rslopehmax,rslopehbmax,rslopeh2max,rslopeh3max,                   &
             rslopecmax,rslopec2max,rslopec3max,                               &
             rslopermax,rslopesmax,rslopegmax,                                 &
             rsloperbmax,rslopesbmax,rslopegbmax,                              &
             rsloper2max,rslopes2max,rslopeg2max,                              &
             rsloper3max,rslopes3max,rslopeg3max
   real, dimension(numax), private, save :: ccg, ocg1
   real, parameter, private :: bm_r1 = 3.0
   real, parameter, private :: obmr1 = 1./bm_r1
   real, save :: mrmin


contains


subroutine udm(th, q, qc, qr, qi, qs, qg, qh                                  &
                 ,nn, nc, nr                                                   &
                 ,den, pii, p, delz                                            &
                 ,delt,g, cpd, cpv, ccn0, rd, rv, t0c                          &
                 ,ep1, ep2, qmin                                               &
                 ,xls, xlv0, xlf0, den0, denr                                  &
                 ,cliq,cice,psat                                               &
                 ,xland                                                        &
                 ,xice                                                         &
                 ,rain, rainncv                                                &
                 ,snow, snowncv                                                &
                 ,hail, hailncv                                                &
                 ,sr                                                           &
                 ,refl_10cm, diagflag, do_radar_ref                            &
                 ,graupel, graupelncv                                          &
                 ,itimestep                                                    &
                 ,has_reqc, has_reqi, has_reqs                                 &  
                 ,re_cloud, re_ice,   re_snow                                  &  
                 ,ids,ide, jds,jde, kds,kde                                    &
                 ,ims,ime, jms,jme, kms,kme                                    &
                 ,its,ite, jts,jte, kts,kte                                    &
                                                                               )

  implicit none


  integer,           intent(in   ) :: itimestep
  integer,           intent(in   ) :: ids,ide, jds,jde, kds,kde ,     &
                                      ims,ime, jms,jme, kms,kme ,     &
                                      its,ite, jts,jte, kts,kte
  real,              intent(in   ) :: delt, g, ccn0, rd, rv, t0c,     &
                                      den0, cpd, cpv, ep1, ep2,       &
                                      qmin, xls, xlv0, xlf0, cliq,    &
                                      cice, psat, denr
  real, dimension( ims:ime , jms:jme ), intent(in) ::      xland
  real, dimension( ims:ime , jms:jme ), intent(in) ::      xice
  real, dimension( ims:ime , kms:kme , jms:jme ),                 &
        intent(in   ) ::                                          &
                                                             den, &
                                                             pii, &
                                                               p, &
                                                            delz
  real, dimension( ims:ime , kms:kme , jms:jme ),                 &
        intent(inout) ::                                          &
                                                             th,  &
                                                              q,  &
                                                              qc, &
                                                              qi, &
                                                              qr, &
                                                              qs, &
                                                              qg, &
                                                              qh
  real, dimension( ims:ime , kms:kme , jms:jme ),                 &
        intent(inout) ::                                          &
                                                             nn,  &
                                                             nc,  &
                                                             nr
  real, dimension( ims:ime , jms:jme ),                           &
        intent(inout) ::                                    rain, &
                                                         rainncv, &
                                                              sr
  real, dimension( ims:ime , jms:jme ), optional,                 &
        intent(inout) ::                                    snow, &
                                                         snowncv, &
                                                         graupel, &
                                                      graupelncv, &
                                                            hail, &
                                                         hailncv



  integer, intent(in)::                                           &
                                                        has_reqc, &
                                                        has_reqi, &
                                                        has_reqs
  real, dimension( ims:ime, kms:kme, jms:jme ),                   &
        intent(inout)::                                           &
                                                        re_cloud, &
                                                          re_ice, &
                                                         re_snow




  integer, dimension( its:ite , jts:jte ) ::   slimsk
  real, dimension( its:ite , kts:kte )    ::   t
  real, dimension( its:ite , kts:kte, 2 ) ::   qci
  real, dimension( its:ite , kts:kte, 4 ) ::   qrs, ncr
  integer                                 ::   i,j,k



  real, dimension( kts:kte) :: qv1d, t1d, p1d, qr1d, nr1d, qs1d, qg1d, dbz
  real, dimension( kts:kte) :: qh1d
  real, dimension( kts:kte ) :: den1d, qc1d, nc1d, qi1d
  real, dimension( kts:kte ) :: re_qc, re_qi, re_qs
  integer,                optional, intent(in)    :: do_radar_ref
  logical,                optional, intent(in)    ::     diagflag
  real, dimension( ims:ime, kms:kme, jms:jme ),                   &
                          optional, intent(inout) ::    refl_10cm



      if (itimestep==1) then
        do j = jms,jme
          do k = kms,kme
            do i = ims,ime
              nn(i,k,j) = ccn0
            enddo
          enddo
        enddo
      endif

      do j=jts,jte
        do i=its,ite
          if(xland(i,j) > 1.) then 
            slimsk(i,j) = 0
          else 
            slimsk(i,j) = 1
          endif  
          if(xice(i,j) > 0.5) slimsk(i,j) = 2
        enddo
      enddo

      do j=jts,jte
        do k=kts,kte
          do i=its,ite
            t(i,k)=th(i,k,j)*pii(i,k,j)
            qci(i,k,1) = qc(i,k,j)
            qci(i,k,2) = qi(i,k,j)
            qrs(i,k,1) = qr(i,k,j)
            qrs(i,k,2) = qs(i,k,j)
            qrs(i,k,3) = qg(i,k,j)
            qrs(i,k,4) = qh(i,k,j)
            ncr(i,k,1) = nn(i,k,j)
            ncr(i,k,2) = nc(i,k,j)
            ncr(i,k,3) = nr(i,k,j)
          enddo
        enddo

         call udm2d(t, q(ims,kms,j), qci, qrs                     &
                    ,ncr                                           &
                    ,den(ims,kms,j)                                &
                    ,p(ims,kms,j), delz(ims,kms,j)                 &
                    ,delt,g, cpd, cpv, rd, rv, t0c                 &
                    ,ep1, ep2, qmin                                &
                    ,xls, xlv0, xlf0, den0, denr                   &
                    ,cliq,cice,psat                                &
                    ,j                                             &
                    ,slimsk(its,j)                                 &
                    ,rain(ims,j),rainncv(ims,j)                    &
                    ,sr(ims,j)                                     &
                    ,ids,ide, jds,jde, kds,kde                     &
                    ,ims,ime, jms,jme, kms,kme                     &
                    ,its,ite, jts,jte, kts,kte                     &
                    ,snow(ims,j),snowncv(ims,j)                    &
                    ,graupel(ims,j),graupelncv(ims,j)              &
                    ,hail(ims,j),hailncv(ims,j)                    &
                                                                   )
        do k=kts,kte
          do i=its,ite
            th(i,k,j)=t(i,k)/pii(i,k,j)
            qc(i,k,j) = qci(i,k,1)
            qi(i,k,j) = qci(i,k,2)
            qr(i,k,j) = qrs(i,k,1)
            qs(i,k,j) = qrs(i,k,2)
            qg(i,k,j) = qrs(i,k,3)
            qh(i,k,j) = qrs(i,k,4)
            nn(i,k,j) = ncr(i,k,1)
            nc(i,k,j) = ncr(i,k,2)
            nr(i,k,j) = ncr(i,k,3)
          enddo
        enddo

        if ( present (diagflag) ) then
          if (diagflag .and. do_radar_ref==1) then
            do i=its,ite
               do k=kts,kte
                  t1d(k)=th(i,k,j)*pii(i,k,j)
                  p1d(k)=p(i,k,j)
                  qv1d(k)=q(i,k,j)
                  qr1d(k)=qr(i,k,j)
                  qs1d(k)=qs(i,k,j)
                  qg1d(k)=qg(i,k,j)
                  qh1d(k)=qh(i,k,j)
                  nr1d(k) = nr(i,k,j)
               enddo
               call udm_mp_reflectivity (qv1d, qr1d, qs1d, qg1d,       &
                       nr1d,                                           &
                       qh1d,                                           &
                       t1d, p1d, dbz, kts, kte, i, j)
               do k = kts, kte
                  refl_10cm(i,k,j) = max(-35., dbz(k))
               enddo
            enddo
          endif
        endif

        if (has_reqc.ne.0 .and. has_reqi.ne.0 .and. has_reqs.ne.0) then
          do i=its,ite
            do k=kts,kte
              re_qc(k) = recmin
              re_qi(k) = reimin
              re_qs(k) = resmin
              t1d(k)  = th(i,k,j)*pii(i,k,j)
              qc1d(k) = qc(i,k,j)
              qi1d(k) = qi(i,k,j)
              qs1d(k) = qs(i,k,j)
              nc1d(k) = nc(i,k,j)
            enddo
            call udm_mp_effective_radius(t1d,qc1d,qi1d,qs1d,den1d,qmin, t0c    &
                               ,nc1d                                           &
                               ,re_qc, re_qi, re_qs, kts, kte, i, j)
            do k=kts,kte
              re_cloud(i,k,j) = max(recmin, min(re_qc(k), recmax))
              re_ice(i,k,j)   = max(reimin, min(re_qi(k), reimax))
              re_snow(i,k,j)  = max(resmin, min(re_qs(k), resmax))
            enddo
          enddo
        endif     

      enddo
end subroutine udm




subroutine udm2d(t1, q1                                          &
                   ,qci1, qrs1                                    &
                   ,ncr1                                          &
                   ,den1, p1, delz1                               &
                   ,delt,g, cpd, cpv, rd, rv, t0c                 &
                   ,ep1, ep2, qmin                                &
                   ,xls, xlv0, xlf0, den0, denr                   &
                   ,cliq,cice,psat                                &
                   ,lat                                           &
                   ,slimsk                                        &
                   ,rain,rainncv                                  &
                   ,sr                                            &
                   ,ids,ide, jds,jde, kds,kde                     &
                   ,ims,ime, jms,jme, kms,kme                     &
                   ,its,ite, jts,jte, kts,kte                     &
                   ,snow,snowncv                                  &
                   ,graupel,graupelncv                            &
                   ,hail,hailncv                                  &
                                                                  )





























































































   implicit none




   integer                                , intent(in   ) ::                   &
                                                    ids,ide, jds,jde, kds,kde, &
                                                    ims,ime, jms,jme, kms,kme, &
                                                    its,ite, jts,jte, kts,kte
   real                        , intent(in   ) :: delt
   real                        , intent(in   ) :: g, cpd, cpv, t0c,&
                                                             rd, rv, ep1, ep2
   integer, dimension( its:ite )           , intent(in   ) :: slimsk
   real                        , intent(in   ) ::                   &
                                                             qmin, xls,        &
                                                             xlv0, xlf0,       &
                                                             den0, denr,       &
                                                             cliq, cice, psat
   integer                                , intent(in   ) :: lat
   real, dimension( ims:ime,kms:kme ), intent(in   ) :: p1
   real, dimension( ims:ime,kms:kme ), intent(in   ) :: delz1
   real, dimension( ims:ime,kms:kme ), intent(inout) :: q1
   real, dimension( its:ite,kts:kte ), intent(inout) :: t1
   real, dimension( ims:ime,kms:kme )  , intent(in   ) :: den1
   real, dimension( its:ite,kts:kte,2 ), intent(inout) :: qci1
   real, dimension( its:ite,kts:kte,4 ), intent(inout) :: qrs1
   real, dimension( its:ite,kts:kte,3 ), intent(inout) :: ncr1
   real, dimension( ims:ime )               , intent(inout) :: rain
   real, dimension( ims:ime ), optional     , intent(inout) :: snow
   real, dimension( ims:ime ), optional     , intent(inout) :: graupel
   real, dimension( ims:ime ), optional     , intent(inout) :: hail
   real, dimension( ims:ime )               , intent(inout) :: rainncv
   real, dimension( ims:ime )               , intent(inout) :: sr
   real, dimension( ims:ime ), optional     , intent(inout) :: snowncv
   real, dimension( ims:ime ), optional     , intent(inout) :: graupelncv
   real, dimension( ims:ime ), optional     , intent(inout) :: hailncv



   real, dimension( its:ite )                           :: dxmeter
   real, dimension( kts:kte , its:ite ) ::                &
                                                                q, &
                                                              den, &
                                                                t, &
                                                                p, &
                                                             delz, &
                                                             dend, &
                                                           denfac, &
                                                              xlv, &
                                                              xlf, &
                                                              cpm
   real, dimension( kts:kte , its:ite , 2) ::             &
                                                              qci
   real, dimension( kts:kte , its:ite , 4) ::             &
                                                              qrs
   real, dimension( kts:kte, 4)  ::                      &
                                                           rslope, &
                                                          rslope2, &
                                                          rslope3, &
                                                          rslopeb
   real, dimension( kts:kte , its:ite , 3) ::         ncr
   real, dimension( kts:kte ) ::                         &
                                                           rh_mul, &
                                                           rh_ice, &
                                                         qsat_mul, &
                                                         qsat_ice, &
                                                              vtr, &
                                                              vts, &
                                                              vtg, &
                                                              vth, &
                                                           vtmean, &
                                                           sumice, &
                                                              vti, &
                                                             vtis, &
                                                               ni, &
                                                              ni0, &
                                                               mi, &
                                                               di, &
                                                              dis, &
                                                            denqr, &
                                                            denqs, &
                                                            denqg, &
                                                            denqh, &
                                                            denqi, &
                                                           n0sfac, &
                                                           diffus, &
                                                           viscok, &
                                                           viscod, &
                                                           ab_mul, &
                                                           ab_ice, &
                                                           venfac
   real, dimension( kts:kte ) ::                         &
                                                          rslopec, &
                                                         rslopec2, &
                                                         rslopec3, &
                                                               dc, &
                                                               dr, &
                                                            denqn
   real, dimension( kts:kte ) ::                         &
                                                            pigen, &
                                                            pcond, &
                                                            prevp, &
                                                            psevp, &
                                                            pgevp, &
                                                            pidep, &
                                                            psdep, &
                                                            pgdep, &
                                                            phdep, &
                                                            praut, &
                                                            psaut, &
                                                            pgaut, &
                                                            piacr, &
                                                            pracw, &
                                                            praci, &
                                                            pracs, &
                                                            psacw, &
                                                            psaci, &
                                                            psacr, &
                                                            phaut, &
                                                            phacr, &
                                                            phacs, &
                                                            phacg, &
                                                            phaci, &
                                                            phmlt, &
                                                            pheml, &
                                                            phevp, &
                                                            primh, &
                                                            pvapg, &
                                                            pvaph, &
                                                            pgwet, &
                                                            phwet, &
                                                          pgaci_w, &
                                                          phaci_w, &
                                                            nhacw, &
                                                            nhacr, &
                                                            nheml, &
                                                            pgacw, &
                                                            phacw, &
                                                            pracg, &
                                                            pgaci, &
                                                            pgacr, &
                                                            pgacs, &
                                                            paacw, &
                                                            psmlt, &
                                                            pgmlt, &
                                                            pseml, &
                                                            pgeml
   real, dimension( kts:kte ) ::                         &
                                                            pcact, &
                                                            nraut, &
                                                            nracw, &
                                                            ncevp, &
                                                            nccol, &
                                                            nrcol, &
                                                            nsacw, &
                                                            ngacw, &
                                                            niacr, &
                                                            nsacr, &
                                                            ngacr, &
                                                            naacw, &
                                                            nseml, &
                                                            ngeml, &
                                                            ncact
   real, dimension( kts:kte ) ::                         &
                                                           satrdt, &
                                                           supice, &
                                                           supsat, &
                                                           tcelci, &
                                                             cldf
   real ::                                              &
                                                           qrpath, &
                                                           qspath, &
                                                           qgpath, &
                                                           qipath, &
                                                         precip_r, &
                                                         precip_s, &
                                                         precip_g, &
                                                         precip_i
   logical, dimension( kts:kte ) ::                                 &
                                                            ifsat, &
                                                            ifice



   real, dimension( kts:kte )   :: ncaut
   integer :: nu_c
   logical :: L_qc
   real :: L_c, n_c
   double precision :: n0c, n0csq, lamc
   double precision, parameter :: vt0_c = 1.09734d8
   double precision, parameter :: kc_aut = 1.35429d14
   double precision, parameter :: alpha_aut = 0.88
   double precision, dimension(0:42) :: gamma1
   double precision ::                                             &
                   qc_tot, qc_sub, qc_aut,                         &
                   nc_tot, nc_sub, nc_aut, nr_aut



   real  ::                                             &
             rdtcld,conden, x, y, z, a, b, c, d, e,                &
             coeres, dtcld, eacrs, acrfac, egi,                    &
             ehi, rs0, ghw1, ghw2, ghw3, ghw4, precip_h, qhpath,   &
             qimax, roqi0, lamdar, diameter,                       &
             precip_sum, precip_ice, factor, source, htotal,       &
             hvalue, pfrzdtc, pfrzdtr,                             &
             tstepsnow, tstepgraupel, tstephail,                   &
             alpha2, delta2, delta3, dtcfl, dr_embryo,temp
   real  :: gfac, sfac, nfrzdtr, nfrzdtc, qnpath
   real, dimension( kts:kte ) :: qrconcr, qrcon, taucon
   real, dimension( kts:kte ) :: vtn
   integer :: nstep, niter, n, lond, latd,                         &
              i, j, k, loop, loops, idim, kdim
   integer :: ktopini, ktopmax, ktop
   integer :: ktopqc, ktopqi, ktopqr, ktopqs, ktopqg, ktopqh, ktoprh
   real, dimension( kts:kte ) :: tmp1d
   logical :: flgcld, lqc, lqi, lqr, lqs, lqg, lqh



   idim = ite-its+1
   kdim = kte-kts+1
   ktopini = kte - 1
   lond = (ite-its)/2 + 1
   latd = 1
   dxmeter(:) = 10000.
   dr_embryo =  1./(pi*denr/6.*di82**3)



   t(:,:) = 0.; q(:,:) = 0.; p(:,:) =0.
   delz(:,:) = 0.; den(:,:) = 0.; dend(:,:) =0.; denfac(:,:) = 0.
   do k = kts, kte
     do i = its, ite
       t(k,i) = t1(i,k)
       q(k,i) = q1(i,k)
       p(k,i) = p1(i,k)
       delz(k,i) = delz1(i,k)
       den(k,i) = den1(i,k)
       dend(k,i) = (p(k,i)/t(k,i)-den(k,i)*rv)/(rd-rv)  
       denfac(k,i) = sqrt(den0/den(k,i))
     enddo
   enddo



   qci(:,:,:) = qmin
   qrs(:,:,:) = qmin
   ncr(:,:,:) = qmin

   ktop = ktopini
   do k = kts, kte
     do i = its, ite
       if(flgzero) then
         qci(k,i,1) = max(qci1(i,k,1),0.0)
         qci(k,i,2) = max(qci1(i,k,2),0.0)
         qrs(k,i,1) = max(qrs1(i,k,1),0.0)
         qrs(k,i,2) = max(qrs1(i,k,2),0.0)
         qrs(k,i,3) = max(qrs1(i,k,3),0.0)
         qrs(k,i,4) = max(qrs1(i,k,4),0.0)
         ncr(k,i,1) = min(max(ncr1(i,k,1),ccnmin),ccnmax)
         ncr(k,i,2) = max(ncr1(i,k,2),0.0)
         ncr(k,i,3) = max(ncr1(i,k,3),0.0)
       else
         qci(k,i,1) = qci1(i,k,1)
         qci(k,i,2) = qci1(i,k,2)
         qrs(k,i,1) = qrs1(i,k,1)
         qrs(k,i,2) = qrs1(i,k,2)
         qrs(k,i,3) = qrs1(i,k,3)
         qrs(k,i,4) = qrs1(i,k,4)
         ncr(k,i,1) = min(max(ncr1(i,k,1),ccnmin),ccnmax)
         ncr(k,i,2) = ncr1(i,k,2)
         ncr(k,i,3) = ncr1(i,k,3)
       endif
     enddo
   enddo



   do i = its, ite
     if(present (snowncv) .and. present (snow)) snowncv(i) = 0.
     if(present (graupelncv) .and. present (graupel)) graupelncv(i) = 0.
     if(present (hailncv) .and. present (hail)) hailncv(i) = 0.
   enddo
   rainncv(:) = 0.;  sr(:) = 0.
   cpm(:,:) = 0.; xlv(:,:) = 0.; xlf(:,:) = 0.



   do k = kts, ktop
     do i = its, ite
       cpm(k,i) = cpd*(1.-max(q(k,i),qmin)) + max(q(k,i),qmin)*cpv
       xlv(k,i) = xlv0 - xlv1*(t(k,i)-t0c)
       xlf(k,i) = xls - xlv(k,i)
     enddo
   enddo







   loops = max(ceiling(delt/dtcldcr),1)
   dtcld = delt/loops
   if(delt<=dtcldcr) dtcld = delt
   rdtcld = 1./dtcld



 inner_loop : do loop = 1, loops 



 i_loop : do i = its, ite 



   qsat_mul(:) = 0.; rh_mul(:)   = 0.
   qsat_ice(:) = 0.; rh_ice(:)   = 0.
   ktopqc = 0; ktopqi = 0; ktopqr = 0; ktopqs = 0; ktopqg = 0; ktoprh = 0
   ktopqh = 0

   ktop = ktopini
   do k = kts, ktop
     qsat_mul(k) = fsvp_water(t(k,i),p(k,i))      
     qsat_mul(k) = ep2 * qsat_mul(k) / (p(k,i) - qsat_mul(k))
     qsat_mul(k) = max(qsat_mul(k),qmin)
     rh_mul(k)   = max(q(k,i) / qsat_mul(k),qmin)
     qsat_ice(k) = fsvp(t(k,i),p(k,i))
     qsat_ice(k) = ep2 * qsat_ice(k) / (p(k,i) - qsat_ice(k))
     qsat_ice(k) = max(qsat_ice(k),qmin)
     rh_ice(k)   = max(q(k,i) / qsat_ice(k),qmin)
   enddo
















   do k = ktop, kts, -1
     diffus(k) = (8.794e-5*exp(log(t(k,i))*(1.81)))/p(k,i)
     viscok(k) = (1.496e-6*(t(k,i)*sqrt(t(k,i))))/(t(k,i)+120.)/den(k,i)
     viscod(k) = 1.414e3*viscok(k)*den(k,i)
     ab_mul(k) = ((den(k,i)*xlv(k,i)*xlv(k,i)))/(viscod(k)*(rv*t(k,i)*t(k,i)))  &
               + 1./(qsat_mul(k)*diffus(k))
     ab_ice(k) = ((den(k,i)*xls*xls))/(viscod(k)*(rv*t(k,i)*t(k,i)))            &
               + 1./(qsat_ice(k)*diffus(k))
     venfac(k) = (exp(.3333333*log(viscok(k)/(diffus(k))))                      &
               * sqrt(sqrt(den0/(den(k,i)))))/sqrt(viscok(k))
   enddo



   lqc = .false.
   lqi = .false.
   lqr = .false.
   lqs = .false.
   lqg = .false.
   lqh = .false.
   flgcld = .false.
   call find_cloud_top(1,kdim,ktopini,qci(:,i,1),zero_0,ktopqc)
   call find_cloud_top(1,kdim,ktopini,qci(:,i,2),zero_0,ktopqi)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,1),zero_0,ktopqr)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,2),zero_0,ktopqs)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,3),zero_0,ktopqg)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,4),zero_0,ktopqh)
   call find_cloud_top(1,kdim,ktopini,rh_ice(:), one_1,ktoprh)
   if(ktopqc>0.0) lqc = .true.
   if(ktopqi>0.0) lqi = .true.
   if(ktopqr>0.0) lqr = .true.
   if(ktopqs>0.0) lqs = .true.
   if(ktopqg>0.0) lqg = .true.
   if(ktopqh>0.0) lqh = .true.
   ktopmax = max(ktopqc,ktopqi,ktopqr,ktopqs,ktopqg,ktopqh,ktoprh)



   if(lqc .or. lqi .or. lqr .or. lqs .or. lqg .or. lqh) flgcld = .true.
   if((.not.flgcld) .and. ktoprh>0.0) flgcld = .true.
   if(.not.flgcld) then
     cycle i_loop
   endif



   temp = 0.         ; hvalue = 0.       ; htotal = 0. ; source = 0.
   pcond(:) = 0.     ; pigen(:) = 0.
   praut(:) = 0.     ; psaut(:) = 0.     ; pgaut(:) = 0.
   pidep(:) = 0.     ; psdep(:) = 0.     ; pgdep(:) = 0.
   pracw(:) = 0.     ; praci(:) = 0.     ; pracs(:) = 0.
   psacw(:) = 0.     ; psaci(:) = 0.     ; psacr(:) = 0.
   pgacw(:) = 0.     ; pgaci(:) = 0.     ; pgacr(:) = 0. ; pgacs(:) = 0.
   paacw(:) = 0.     ; piacr(:) = 0.
   psmlt(:) = 0.     ; pgmlt(:) = 0.     ; pseml(:) = 0. ; pgeml(:) = 0.
   prevp(:) = 0.     ; psevp(:) = 0.     ; pgevp(:) = 0.
   denqr(:) = 0.     ; denqs(:) = 0.     ; denqi(:) = 0.
   vtr(:)   = 0.     ; vts(:)   = 0.     ; vtg(:)   = 0. ; vti(:)   = 0.
   qrpath   = 0.     ; qspath   = 0.     ; qgpath   = 0. ; qipath   = 0.
   precip_r = 0.     ; precip_s = 0.     ; precip_g = 0. ; precip_i = 0.
   satrdt(:)= 0.     ; supsat(:)= 0.     ; tcelci(:)= 0. ; supice(:)= 0.
   ifsat(:) = .false.; ifice(:) =.false.
   ni(:)   = 1.e3    ; mi(:)    = 0.     ; cldf(:) = 1.
   sumice(:) = 0.    ; vtmean(:) = 0.0   ; n0sfac(:) = 0.
   tstepsnow = 0.    ; tstepgraupel = 0. ; tstephail = 0.
   lamdar = 0.       ; diameter = 0.     ; eacrs = 0.  ; egi =0.
   roqi0 = 0.        ; acrfac = 0.       ; ni0(:) = 0.
   qimax = 0.        ; alpha2 = 0.       ; coeres = 0.
   pfrzdtc = 0.      ; pfrzdtr = 0.
   pcact(:) = 0.     ; nsacw(:) = 0.     ;  ngacw(:) = 0.  ; naacw(:) = 0.
   niacr(:) = 0.     ; nsacr(:) = 0.     ;  ngacr(:) = 0.  ; nseml(:) = 0.
   ngeml(:) = 0.     ; nracw(:) = 0.     ;  nccol(:) = 0.  ; nrcol(:) = 0.
   ncact(:) = 0.     ; nraut(:) = 0.     ;  ncevp(:) = 0.  ; denqn(:) = 0.
   vtn(:)   = 0.     ; dc(:)  = 0.       ;  dr(:) = 0.     ; di(:)  = 0.
   qrcon(:)= 0.      ; qrconcr(:) = 0.   ;  taucon(:) = 0. ; tmp1d(:) = 0.
   qnpath = 0.       ; sfac = 0.         ;  gfac = 0.
   nfrzdtc = 0.      ; nfrzdtr = 0.
   phdep(:)= 0.      ; phaut(:) = 0.   ; pracg(:) = 0. ; phacw(:) = 0.
   phaci(:)= 0.      ; phacr(:) = 0.   ; phacs(:) = 0. ; phacg(:) = 0.
   phmlt(:)= 0.      ; pheml(:) = 0.   ; phevp(:) = 0. ; primh(:) = 0.
   pvapg(:)= 0.      ; pvaph(:) = 0.   ; pgwet(:) = 0. ; phwet(:) = 0.
   pgaci_w(:)= 0.    ; phaci_w(:) = 0. ; qhpath = 0.   ; vth(:) = 0.
   nhacw(:) = 0.     ; nhacr(:) = 0.   ; nheml(:)= 0.  ; precip_h = 0.
   ncaut(:) = 0.     ; L_qc = .true.   ; n0c = 0.        ; n0csq = 0. 
   L_c = 0.          ; n_c = 0.        ; lamc = 0.       ; nu_c = 0
   gamma1(:) = 0.    ; qc_tot = 0.     ; qc_sub = 0.     ; qc_aut = 0.
   nc_tot = 0.       ; nc_sub = 0.     ; nc_aut = 0.     ; nr_aut = 0.
   dis(:)   = 0.     ; vtis(:)   = 0.  
   ktop = ktopmax
   do k = ktop, kts, -1
     if(qci(k,i,1) < qcmin) ncr(k,i,2) =0.
     if(qrs(k,i,1) < qrmin) ncr(k,i,3) =0.
   enddo





   call adjust_number_concent(ktopqr,kdim,qrs(:,i,1),ncr(:,i,3),den(:,i),      &
        pidnr,drcoeff,qrmin,nrmin,nrmax,di1000,drmin,drmax)
   call adjust_number_concent(ktopqc,kdim,qci(:,i,1),ncr(:,i,2),den(:,i),      &
        pidnc,one_1,qcmin,ncmin,ncmax,di15,  dcmin, dcmax)



   ktop = max(ktopqc,ktopqi)
   call cldf_diag(1,kdim,t(:,i),p(:,i),q(:,i),qci(:,i,1),qci(:,i,2),           &
        dxmeter(i),cldf(:),ktop)

   do k = ktop, kts, -1

       if(cldf(k)>0.) then
         qci(k,i,1) = qci(k,i,1) / cldf(k)
         qci(k,i,2) = qci(k,i,2) / cldf(k)
         qrs(k,i,1) = qrs(k,i,1) / cldf(k)
         qrs(k,i,2) = qrs(k,i,2) / cldf(k)
         qrs(k,i,3) = qrs(k,i,3) / cldf(k)
         qrs(k,i,4) = qrs(k,i,4) / cldf(k)
       endif
   enddo



   ktop = ktopqr
   call slope_rain(1,kdim,ktop,qrs(:,i,1),den(:,i),denfac(:,i),t(:,i),         &
        ncr(:,i,3),                                                            &
        rslope(:,1),rslopeb(:,1),rslope2(:,1),rslope3(:,1),vtr(:))
   ktop = ktopqs
   call slope_snow(1,kdim,ktop,qrs(:,i,2),den(:,i),denfac(:,i),t(:,i),         &
        rslope(:,2),rslopeb(:,2),rslope2(:,2),rslope3(:,2),vts(:))
   ktop = ktopqg
   call slope_graupel(1,kdim,ktop,qrs(:,i,3),den(:,i),denfac(:,i),t(:,i),      &
        rslope(:,3),rslopeb(:,3),rslope2(:,3),rslope3(:,3),vtg(:))
   ktop = ktopqh
   call slope_hail(1,kdim,ktop,qrs(:,i,4),den(:,i),denfac(:,i),t(:,i),         &
        rslope(:,4),rslopeb(:,4),rslope2(:,4),rslope3(:,4),vth(:))
   ktop = ktopqc
   call slope_cloud(1,kdim,ktop,qci(:,i,1),ncr(:,i,2),den(:,i),denfac(:,i),    &
        t(:,i),qcmin,rslopec(:),rslopec2(:),rslopec3(:))





   ktop = ktopmax
   do k = kts, ktop
     tcelci(k) = t(k,i) - t0c
     if(t(k,i)<t0c) ifice(k) = .true.
     n0sfac(k) = max(min(exp(-alpha*tcelci(k)),n0smax/n0s),1.)
   enddo



   if(lqs) then
     ktop = ktopqs
     do k = ktop, kts, -1
       if(.not.ifice(k) .and. qrs(k,i,2)>0.) then
         coeres = rslope2(k,2)*sqrt(rslope(k,2)*rslopeb(k,2))
         psmlt(k) = viscod(k)/xlf0*(t(k,i)-t0c)*pi/2.*n0sfac(k)                &
                  *(precs1*rslope2(k,2)+precs2*venfac(k)*coeres)/den(k,i)
         psmlt(k) = max(min(psmlt(k)*dtcld,qrs(k,i,2)),0.)



         if(qrs(k,i,2)>qrmin) then
           sfac = rslope(k,2)*n0s*n0sfac(k)/qrs(k,i,2)
           ncr(k,i,3) = min(ncr(k,i,3) + sfac*psmlt(k), nrmax)
         endif
         qrs(k,i,2) = qrs(k,i,2) - psmlt(k)
         qrs(k,i,1) = qrs(k,i,1) + psmlt(k)
         t(k,i) = t(k,i) - xlf0/cpm(k,i)*psmlt(k)*cldf(k)
       endif
     enddo
   endif



   if(lqg) then
     ktop = ktopqg
     do k = ktop, kts, -1
       if(.not.ifice(k) .and. qrs(k,i,3)>0.) then
         coeres = rslope2(k,3)*sqrt(rslope(k,3)*rslopeb(k,3))
         pgmlt(k) = viscod(k)/xlf0*(t(k,i)-t0c)*(precg1*rslope2(k,3)           &
                  + precg2*venfac(k)*coeres)/den(k,i)
         pgmlt(k) = max(min(pgmlt(k)*dtcld,qrs(k,i,3)),0.)



         if(qrs(k,i,3)>qrmin) then
           gfac = rslope(k,3)*n0g/qrs(k,i,3)
           ncr(k,i,3) = min(ncr(k,i,3) + gfac*pgmlt(k), nrmax)
         endif
         qrs(k,i,3) = qrs(k,i,3) - pgmlt(k)
         qrs(k,i,1) = qrs(k,i,1) + pgmlt(k)
         t(k,i) = t(k,i) - xlf0/cpm(k,i)*pgmlt(k)*cldf(k)
       endif
     enddo
   endif



   if(lqh) then
     ktop = ktopqh
     do k = ktop, kts, -1
       if(.not.ifice(k) .and. qrs(k,i,4)>0.) then
         coeres = rslope2(k,4)*sqrt(rslope(k,4)*rslopeb(k,4))
         phmlt(k) = viscod(k)/xlf0*(t(k,i)-t0c)*(prech1*rslope2(k,4)           &
                  + prech2*venfac(k)*coeres)/den(k,i)
         phmlt(k) = max(min(phmlt(k)*dtcld,qrs(k,i,4)),0.)



         if(qrs(k,i,4)>qrmin) then
           gfac = rslope(k,4)*n0h/qrs(k,i,4)
           ncr(k,i,3) = min(ncr(k,i,3) + gfac*phmlt(k), nrmax)
         endif
         qrs(k,i,4) = qrs(k,i,4) - phmlt(k)
         qrs(k,i,1) = qrs(k,i,1) + phmlt(k)
         t(k,i) = t(k,i) - xlf0/cpm(k,i)*phmlt(k)*cldf(k)
       endif
     enddo
   endif



   if(lqi) then
     ktop = ktopqi
     do k = ktop, kts, -1
       if(.not.ifice(k) .and. qci(k,i,2)>0.) then
         qci(k,i,1) = qci(k,i,1) + qci(k,i,2)
         t(k,i) = t(k,i) - xlf0/cpm(k,i)*qci(k,i,2)*cldf(k)
         qci(k,i,2) = 0.



         temp = (den(k,i)*max(qci(k,i,2),qcmin))
         temp = sqrt(sqrt(temp*temp*temp))
         ni(k) = min(max(5.38e7*temp,1.e3),1.e6)
         ncr(k,i,2) = min(ncr(k,i,2) + ni(k),ncmax)
       endif
     enddo
   endif



   if(lqc) then
     ktop = ktopqc
     do k = ktop, kts, -1
       if(tcelci(k)<-40. .and. qci(k,i,1)>0.) then
         qci(k,i,2) = qci(k,i,2) + qci(k,i,1)
         t(k,i) = t(k,i) + xlf(k,i)/cpm(k,i)*qci(k,i,1)*cldf(k)
         qci(k,i,1) = 0.



         if(ncr(k,i,2)>0.) ncr(k,i,2) = 0.
       endif



       if(ifice(k) .and. qci(k,i,1)>qcmin) then
         hvalue = max(tcelci(k),-70.)
         pfrzdtc = min(pisq*pfrz1*(exp(-pfrz2*hvalue)-1.)*denr/den(k,i)        &
                   *ncr(k,i,2)*rslopec3(k)*rslopec3(k)/18.*dtcld               &
                   ,qci(k,i,1))



         if(ncr(k,i,2)>ncmin) then
           nfrzdtc = min(pi*pfrz1*(exp(-pfrz2*hvalue)-1.)*ncr(k,i,2)           &
                     *rslopec3(k)/6.*dtcld,ncr(k,i,2))
           ncr(k,i,2) = max(ncr(k,i,2) - nfrzdtc,0.)
         endif
         qci(k,i,1) = qci(k,i,1) - pfrzdtc
         qci(k,i,2) = qci(k,i,2) + pfrzdtc
         t(k,i) = t(k,i) + xlf(k,i)/cpm(k,i)*pfrzdtc*cldf(k)
       endif
     enddo
   endif



   if(lqr) then
     ktop = ktopqr
     do k = ktop, kts, -1
       if(ifice(k) .and. qrs(k,i,1)>0.) then
         hvalue = max(tcelci(k), -70.)
         pfrzdtr = min(140.*pisq*pfrz1*ncr(k,i,3)*denr/den(k,i)                &
                  *(exp(-pfrz2*hvalue)-1.)*rslope3(k,1)*rslope3(k,1)           &
                  *dtcld,qrs(k,i,1))



         if(ncr(k,i,3)>nrmin) then
           nfrzdtr = min(4.*pi*pfrz1*ncr(k,i,3)*(exp(-pfrz2*hvalue)-1.)        &
                    *rslope3(k,1)*dtcld, ncr(k,i,3))
           ncr(k,i,3) = max(ncr(k,i,3) - nfrzdtr, 0.)
         endif
         qrs(k,i,1) = qrs(k,i,1) - pfrzdtr
         qrs(k,i,3) = qrs(k,i,3) + pfrzdtr
         t(k,i) = t(k,i) + xlf(k,i)/cpm(k,i)*pfrzdtr*cldf(k)
       endif
     enddo
   endif



   ktop = max(ktopqc,ktopqi)
   do k = ktop, kts, -1

     if(cldf(k)>0.) then
       qci(k,i,1) = qci(k,i,1) * cldf(k)
       qci(k,i,2) = qci(k,i,2) * cldf(k)
       qrs(k,i,1) = qrs(k,i,1) * cldf(k)
       qrs(k,i,2) = qrs(k,i,2) * cldf(k)
       qrs(k,i,3) = qrs(k,i,3) * cldf(k)
       qrs(k,i,4) = qrs(k,i,4) * cldf(k)
     endif
   enddo



   ktop = ktopini
   do k = kts, ktop
     qsat_mul(k) = fsvp_water(t(k,i),p(k,i))
     qsat_mul(k) = ep2 * qsat_mul(k) / (p(k,i) - qsat_mul(k))
     qsat_mul(k) = max(qsat_mul(k),qmin)
     rh_mul(k)   = max(q(k,i) / qsat_mul(k),qmin)
     qsat_ice(k) = fsvp(t(k,i),p(k,i))
     qsat_ice(k) = ep2 * qsat_ice(k) / (p(k,i) - qsat_ice(k))
     qsat_ice(k) = max(qsat_ice(k),qmin)
     rh_ice(k)   = max(q(k,i) / qsat_ice(k),qmin)
   enddo

   lqc = .false.
   lqi = .false.
   lqr = .false.
   lqs = .false.
   lqg = .false.
   lqh = .false.
   call find_cloud_top(1,kdim,ktopini,qci(:,i,1),zero_0,ktopqc)
   call find_cloud_top(1,kdim,ktopini,qci(:,i,2),zero_0,ktopqi)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,1),zero_0,ktopqr)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,2),zero_0,ktopqs)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,3),zero_0,ktopqg)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,4),zero_0,ktopqh)
   call find_cloud_top(1,kdim,ktopini,rh_ice(:), one_1,ktoprh)
   if(ktopqc>0.0) lqc = .true.
   if(ktopqi>0.0) lqi = .true.
   if(ktopqr>0.0) lqr = .true.
   if(ktopqs>0.0) lqs = .true.
   if(ktopqg>0.0) lqg = .true.
   if(ktopqh>0.0) lqh = .true.
   ktopmax = max(ktopqc,ktopqi,ktopqr,ktopqs,ktopqg,ktopqh,ktoprh)



   ktop = ktopqr
   call slope_rain(1,kdim,ktop,qrs(:,i,1),den(:,i),denfac(:,i),t(:,i),         &
        ncr(:,i,3),                                                            &
        rslope(:,1),rslopeb(:,1),rslope2(:,1),rslope3(:,1),vtr(:))
   ktop = ktopqs
   call slope_snow(1,kdim,ktop,qrs(:,i,2),den(:,i),denfac(:,i),t(:,i),         &
        rslope(:,2),rslopeb(:,2),rslope2(:,2),rslope3(:,2),vts(:))
   ktop = ktopqg
   call slope_graupel(1,kdim,ktop,qrs(:,i,3),den(:,i),denfac(:,i),t(:,i),      &
        rslope(:,3),rslopeb(:,3),rslope2(:,3),rslope3(:,3),vtg(:))
   ktop = ktopqh
   call slope_hail(1,kdim,ktop,qrs(:,i,4),den(:,i),denfac(:,i),t(:,i),         &
        rslope(:,4),rslopeb(:,4),rslope2(:,4),rslope3(:,4),vth(:))
   ktop = ktopqi
   do k = ktop, kts, -1
     if(qci(k,i,2)>qcmin) then
       temp   = (den(k,i)*max(qci(k,i,2),qcmin))
       temp   = sqrt(sqrt(temp*temp*temp))
       ni(k)  = min(max(5.38e7*temp,1.e3),1.e6)
       mi(k)     = den(k,i)*qci(k,i,2)/ni(k)
       di(k)  = max(min(exp(log((mi(k)/cxmi))*(1./dxmi)),dimax), qmin)
       vti(k) = avti*exp(log(di(k))*(bvti))
       hvalue = min(max((tcelci(k) - t2_sphere)/(t1_sphere-t2_sphere),0.),1.0)
       dis(k) = max(min(exp(log((mi(k)/cxmis))*(1./dxmis)),dimax), qmin)
       vtis(k)= avtis*exp(log(dis(k))*(bvtis))
       di(k)  = di(k)  * (1.-hvalue) + dis(k)  * hvalue
       vti(k) = vti(k) * (1.-hvalue) + vtis(k) * hvalue
     endif
   enddo

   ktop = max(ktopqs,ktopqg,ktopqh)
   do k = ktop, kts, -1
     sumice(k) = max( (qrs(k,i,2)+qrs(k,i,3)+qrs(k,i,4)), qmin)
     if(sumice(k)>qmin) then
       vtmean(k) = (vts(k)*qrs(k,i,2)+vtg(k)*qrs(k,i,3)                        &
                 +vth(k)*qrs(k,i,4))/sumice(k)
     else
       vtmean(k) = 0.
     endif
   enddo



   ktop = ktopqr
   do k = ktop, kts, -1
     dr(k) = rslope(k,1)*drcoeff
   enddo



   ktop = ktopqc
   call slope_cloud(1,kdim,ktop,qci(:,i,1),ncr(:,i,2),den(:,i),denfac(:,i),    &
        t(:,i),qcmin,rslopec(:),rslopec2(:),rslopec3(:))
   do k = ktop, kts, -1
     dc(k) = rslopec(k)
   enddo





   ktop = max(ktopqc,ktopqr)
   do k = ktop, kts, -1
     qrcon(k)  = 2.7e-2*den(k,i)*qci(k,i,1)*(1.e20/16.*rslopec2(k)             &
               *rslopec2(k)-0.4)
     qrconcr(k) = max(1.2*qrcon(k), qrmin)
   enddo
   if(lqc) then
     ktop = ktopqc
     do k = ktop, kts, -1



       if(qci(k,i,1) > qcmin) then
         L_qc = .true.
         L_c = qci(k,i,1)*den(k,i)
         n_c = max(min(ncr(k,i,2),1.e12),2.)
         nu_c = min(nint(1.e9/n_c)+2,15) 
         lamc = max(min((n_c*pidnc*8.d0*ccg(nu_c)*ocg1(nu_c)/L_c)**obmr1,      &
                lamdacmax),lamdacmin)
       else
         L_qc = .false.


         L_c = qcmin
         n_c = 2.
       endif
       if(L_qc.and.L_c>1.e-5) then



         gamma1(0) = 1.0d0/( lamc)
         do n=1, nu_c
           gamma1(n) = gamma1(n-1)*(dble(n)/( lamc))
         enddo
         n0c = n_c/gamma1(nu_c)
         n0csq = n0c * n0c
         qc_tot = funct_aut_qc_tot(lamc,nu_c)
         qc_sub = funct_aut_qc_sub(lamc,nu_c)

         qc_aut =(qc_tot-alpha_aut*qc_sub)*(pidnc*8.0)*pi*vt0_c*kc_aut*n0csq



         nc_tot = funct_aut_nc_tot(lamc,nu_c)
         nc_sub = funct_aut_nc_sub(lamc,nu_c)

         nc_aut = (2.0*nc_tot-alpha_aut*nc_sub)*pi*vt0_c*kc_aut*n0csq
         nr_aut = (    nc_tot-alpha_aut*nc_sub)*pi*vt0_c*kc_aut*n0csq

         praut(k) = max(min(real(qc_aut/den(k,i)),qci(k,i,1)*rdtcld),0.)
         ncaut(k) = max(min(real(nc_aut),ncr(k,i,2)*rdtcld),0.)
         nraut(k) = min(real(nr_aut),praut(k)/mrmin)
       end if
     enddo
   endif

   if(lqc) then
     ktop = ktopqc
     do k = ktop, kts, -1



       if(dc(k)>=di100) then
         nccol(k) = ncrk1*ncr(k,i,2)*ncr(k,i,2)*rslopec3(k)
       else
         nccol(k) = 2.*ncrk2*ncr(k,i,2)*ncr(k,i,2)*rslopec3(k)*rslopec3(k)
       endif
     enddo
   endif

   if(lqr) then
     ktop = ktopqr
     do k = ktop, kts, -1
       supsat(k) = max(q(k,i),qmin)-qsat_mul(k)
       satrdt(k) = supsat(k)*rdtcld





       if(qrs(k,i,1)>=qrconcr(k)) then
         if(dr(k)>=di100) then
           nracw(k) = min(ncrk1*ncr(k,i,2)*ncr(k,i,3)*(rslopec3(k)             &
                      + 24.*rslope3(k,1)),ncr(k,i,2)*rdtcld)
           pracw(k) = min(pi/6.*(denr/den(k,i))*ncrk1*ncr(k,i,2)               &
                      *ncr(k,i,3)*rslopec3(k)*(2.*rslopec3(k)                  &
                      + 24.*rslope3(k,1)),qci(k,i,1)*rdtcld)
         else
           nracw(k) = min(ncrk2*ncr(k,i,2)*ncr(k,i,3)*(2.*rslopec3(k)          &
                      *rslopec3(k)+5040.*rslope3(k,1)                          &
                      *rslope3(k,1)),ncr(k,i,2)*rdtcld)
           pracw(k) = min(pi/6.*(denr/den(k,i))*ncrk2*ncr(k,i,2)               &
                      *ncr(k,i,3)*rslopec3(k)*(6.*rslopec3(k)                  &
                      *rslopec3(k)+5040.*rslope3(k,1)*rslope3(k,1))            &
                      ,qci(k,i,1)*rdtcld)
         endif
       endif



       if(qrs(k,i,1)>=qrconcr(k)) then
         if(dr(k)<di100) then
           nrcol(k) = 5040.*ncrk2*ncr(k,i,3)*ncr(k,i,3)*rslope3(k,1)           &
                     *rslope3(k,1)
         elseif(dr(k)>=di100 .and. dr(k)<di600) then
           nrcol(k) = 24.*ncrk1*ncr(k,i,3)*ncr(k,i,3)*rslope3(k,1)
         elseif(dr(k)>=di600 .and. dr(k)<di2000) then
           hvalue = -2.5e3*(dr(k)-di600)
           nrcol(k) = 24.*exp(hvalue)*ncrk1*ncr(k,i,3)*ncr(k,i,3)              &
                      *rslope3(k,1)
         else
           nrcol(k) = 0.
         endif
       endif



       if(qrs(k,i,1)>0.) then
         coeres = rslope(k,1)*sqrt(rslope(k,1)*rslopeb(k,1))
         prevp(k) = (rh_mul(k)-1.)*ncr(k,i,3)*(precr1*rslope(k,1)              &
                   +precr2*venfac(k)*coeres)/ab_mul(k)
         if(prevp(k)<=0.) then
           prevp(k) = max(prevp(k),-qrs(k,i,1)*rdtcld)
           prevp(k) = max(prevp(k),satrdt(k)*.5)



           if(prevp(k)==-qrs(k,i,1)*rdtcld) then
             ncr(k,i,1) = min(ncr(k,i,1) + ncr(k,i,3), ccnmax)
             ncr(k,i,3) = 0.
           endif
         else
           prevp(k) = min(prevp(k),satrdt(k)*.5)
         endif
       endif
     enddo
   endif





   ktop = ktopmax
   ifice(:) = .false.
   do k = ktop, kts, -1
     tcelci(k) = t(k,i) - t0c
     if(t(k,i)<t0c) ifice(k) = .true.
     n0sfac(k) = max(min(exp(-alpha*tcelci(k)),n0smax/n0s),1.)
     supsat(k) = max(q(k,i),qmin)-qsat_ice(k)
     satrdt(k) = supsat(k)*rdtcld
   enddo

   if(lqr .and. lqi) then
     ktop = min(ktopqr,ktopqi)
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qrs(k,i,1)>qrmin .and. qci(k,i,2)>qcmin) then



           acrfac = 6.*rslope2(k,1)+4.*di(k)*rslope(k,1) + di(k)**2
           praci(k) = pi*qci(k,i,2)*ncr(k,i,3)*abs(vtr(k)-vti(k))*acrfac/4.

           praci(k) = praci(k)*min(max(0.0,qrs(k,i,1)/qci(k,i,2)),1.)**2
           praci(k) = min(praci(k),qci(k,i,2)*rdtcld)



           piacr(k) = pisq*avtr*ncr(k,i,3)*denr*ni(k)*denfac(k,i)              &
                       *g7pbr*rslope3(k,1)*rslope2(k,1)*rslopeb(k,1)           &
                       /24./den(k,i)

           piacr(k) = piacr(k)*min(max(0.0,qci(k,i,2)/qrs(k,i,1)),1.)**2
           piacr(k) = min(piacr(k),qrs(k,i,1)*rdtcld)



           if(ncr(k,i,3)>nrmin) then
             niacr(k) = pi*avtr*ncr(k,i,3)*ni(k)*denfac(k,i)*g4pbr             &
                         *rslope2(k,1)*rslopeb(k,1)/4.

             niacr(k) = niacr(k)*min(max(0.0,qci(k,i,2)/qrs(k,i,1)),1.)**2
             niacr(k) = min(niacr(k),ncr(k,i,3)*rdtcld)
           endif
         endif
       endif
     enddo
   endif

   if(lqs) then
     ktop = ktopqs
     do k = ktop, kts, -1

       if(ifice(k)) then
         if(qrs(k,i,2)>qrmin .and. qci(k,i,2)>qcmin) then
           eacrs = exp(0.09*(tcelci(k)))



           acrfac = 2.*rslope3(k,2) + 2.*di(k)*rslope2(k,2)                    &
                   + di(k)**2*rslope(k,2)
           psaci(k) = pi*qci(k,i,2)*eacrs*n0s*n0sfac(k)                        &
                        *abs(vtmean(k)-vti(k))*acrfac/4.
           psaci(k) = psaci(k)*min(max(0.0,qrs(k,i,2)/qci(k,i,2)),1.)**2
           psaci(k) = min(psaci(k),qci(k,i,2)*rdtcld)
         endif
       endif



       if(qrs(k,i,2)>qrmin .and. qci(k,i,1)>qcmin) then
         psacw(k) = min(pacrc*n0sfac(k)*rslope3(k,2)*rslopeb(k,2)              &
                  *qci(k,i,1)*denfac(k,i),qci(k,i,1)*rdtcld)

         psacw(k) = psacw(k)*min(max(0.0,qrs(k,i,2)/qci(k,i,1)),1.)**2
         psacw(k) = min(psacw(k),qci(k,i,1)*rdtcld)
       endif



       if(qrs(k,i,2)>qrmin .and. qci(k,i,1)>qcmin .and. ncr(k,i,2)>ncmin) then
         nsacw(k) = min(pacrc*n0sfac(k)*rslope3(k,2)*rslopeb(k,2)              &

                     *min(max(0.0,qrs(k,i,2)/qci(k,i,1)),1.)**2                &
                     *ncr(k,i,2)*denfac(k,i),ncr(k,i,2)*rdtcld)
       endif
     enddo
   endif

   if(lqg) then
     ktop = ktopqg
     do k = ktop, kts, -1
       if(ifice(k)) then



         if(qrs(k,i,3)>qrmin .and. qci(k,i,2)>qcmin) then
           egi = exp(0.09*(tcelci(k)))
           acrfac = 2.*rslope3(k,3) + 2.*di(k)*rslope2(k,3)                    &
                    +  di(k)**2*rslope(k,3)
           pgaci(k) = pi*egi*qci(k,i,2)*n0g*abs(vtmean(k)-vti(k))*acrfac/4.
           pgaci(k) = min(pgaci(k),qci(k,i,2)*rdtcld)
         endif
       endif



       if(qrs(k,i,3)>qrmin .and. qci(k,i,1)>qcmin) then
          pgacw(k) = min(pacrg*rslope3(k,3)*rslopeb(k,3)                       &

                    *min(max(0.0,qrs(k,i,3)/qci(k,i,1)),1.)**2                 &
                    *qci(k,i,1)*denfac(k,i),qci(k,i,1)*rdtcld)
       endif



       if(qrs(k,i,3)>qrmin .and. qci(k,i,1)>qcmin .and. ncr(k,i,2)>ncmin) then
         ngacw(k) = min(pacrg*rslope3(k,3)*rslopeb(k,3)*ncr(k,i,2)             &

                     *min(max(0.0,qrs(k,i,3)/qci(k,i,1)),1.)**2                &
                     *denfac(k,i),ncr(k,i,2)*rdtcld)
       endif
     enddo
   endif



   ktop = max(ktopqs,ktopqg)
   do k = ktop, kts, -1
     if(sumice(k)>qmin) then
       paacw(k) = (qrs(k,i,2)*psacw(k) + qrs(k,i,3)*pgacw(k))/sumice(k)



       naacw(k) = (qrs(k,i,2)*nsacw(k) + qrs(k,i,3)*ngacw(k))/sumice(k)
     endif
   enddo

   if(lqh) then
     ktop = ktopqh
     do k = ktop, kts, -1
       if(ifice(k)) then



         if(qrs(k,i,4)>qrmin .and. qci(k,i,2)>qcmin) then
           ehi = exp(0.09*(tcelci(k)))
           acrfac = 2.*rslope3(k,4) + 2.*di(k)*rslope2(k,4)                    &
                    +  di(k)**2*rslope(k,4)
           phaci(k) = pi*ehi*qci(k,i,2)*n0h*abs(vtmean(k)-vti(k))*acrfac/4.
           phaci(k) = min(phaci(k),qci(k,i,2)*rdtcld)
         endif
       endif



       if(qrs(k,i,4)>qrmin .and. qci(k,i,1)>qcmin) then
         phacw(k) = min(pacrh*rslope3(k,4)*rslopeb(k,4)*qci(k,i,1)             &
                        *min(max(0.0,qrs(k,i,4)/qci(k,i,1)),1.)**2             &
                        *denfac(k,i),qci(k,i,1)*rdtcld)
       endif



       if(qrs(k,i,4)>qrmin .and. qci(k,i,1)>qcmin .and. ncr(k,i,2)>ncmin) then
         nhacw(k) = min(pacrh*rslope3(k,4)*rslopeb(k,4)*ncr(k,i,2)             &
                     *min(max(0.0,qrs(k,i,4)/qci(k,i,1)),1.)**2                &
                     *denfac(k,i),ncr(k,i,2)*rdtcld)
       endif
     enddo
   endif

   if(lqr) then
     ktop = ktopqr
     do k = kts, ktop



       if(qrs(k,i,2)>qrmin .and. qrs(k,i,1)>qrmin) then
         if(ifice(k)) then
           acrfac = 5.*rslope3(k,2)*rslope3(k,2)                               &
                  + 4.*rslope3(k,2)*rslope2(k,2)*rslope(k,1)                   &
                  + 1.5*rslope2(k,2)*rslope2(k,2)*rslope2(k,1)
           pracs(k) = pisq*ncr(k,i,3)*n0s*n0sfac(k)*abs(vtr(k)-vtmean(k))      &
                      *(dens/den(k,i))*acrfac

           pracs(k) = pracs(k)*min(max(0.0,qrs(k,i,1)/qrs(k,i,2)),1.)**2
           pracs(k) = min(pracs(k),qrs(k,i,2)*rdtcld)
         endif



         acrfac = 30.*rslope3(k,1)*rslope2(k,1)*rslope(k,2)                    &
                 +10.*rslope2(k,1)*rslope2(k,1)*rslope2(k,2)                   &
                 + 2.*rslope3(k,1)*rslope3(k,2 )
         psacr(k) = pisq*ncr(k,i,3)*n0s*n0sfac(k)*abs(vtmean(k)-vtr(k))        &
                        *(denr/den(k,i))*acrfac

         psacr(k) = psacr(k)*min(max(0.0,qrs(k,i,2)/qrs(k,i,1)),1.)**2
         psacr(k) = min(psacr(k),qrs(k,i,1)*rdtcld)
       endif



       if(qrs(k,i,2)>qrmin .and. qrs(k,i,1)>qrmin .and. ncr(k,i,3)>nrmin) then
           acrfac = 1.5*rslope2(k,1)*rslope(k,2)                               &
                  + 1.0*rslope(k,1)*rslope2(k,2) + .5*rslope3(k,2)
           nsacr(k) = pi*ncr(k,i,3)*n0s*n0sfac(k)*abs(vtmean(k)-vtr(k))        &
                        *acrfac

           nsacr(k) = nsacr(k)*min(max(0.0,qrs(k,i,2)/qrs(k,i,1)),1.)**2
           nsacr(k) = min(nsacr(k),ncr(k,i,3)*rdtcld)
       endif



       if(qrs(k,i,3)>qrmin .and. qrs(k,i,1)>qrmin) then
         if(ifice(k)) then
           acrfac = 5.*rslope3(k,3)*rslope3(k,3)                               &
                   +4.*rslope3(k,3)*rslope2(k,3)*rslope(k,1)                   &
                   +1.5*rslope2(k,3)*rslope2(k,3)*rslope2(k,1)
           pracg(k) = pisq*ncr(k,i,3)*n0g*abs(vtr(k)-vtmean(k))                &
                       *(deng/den(k,i))*acrfac
           pracg(k) = pracg(k)*min(max(0.0,qrs(k,i,1)/qrs(k,i,3)),1.)**2
           pracg(k) = min(pracg(k),qrs(k,i,3)*rdtcld)
         endif
       endif



       if(qrs(k,i,3)>qrmin .and. qrs(k,i,1)>qrmin) then



         acrfac =  30.*rslope3(k,1)*rslope2(k,1)*rslope(k,3)                   &
                 + 10.*rslope2(k,1)*rslope2(k,1)*rslope2(k,3)                  &
                 +  2.*rslope3(k,1)*rslope3(k,3)
         pgacr(k) = pisq*ncr(k,i,3)*n0g*abs(vtmean(k)-vtr(k))*(denr/den(k,i))  &
                       *acrfac

         pgacr(k) = pgacr(k)*min(max(0.0,qrs(k,i,3)/qrs(k,i,1)),1.)**2
         pgacr(k) = min(pgacr(k),qrs(k,i,1)*rdtcld)
       endif



       if(qrs(k,i,3)>qrmin .and. qrs(k,i,1)>qrmin .and. ncr(k,i,3)>nrmin) then
         acrfac = 1.5*rslope2(k,1)*rslope(k,3)                                 &
                 +1.0*rslope(k,1)*rslope2(k,3) + .5*rslope3(k,3)
         ngacr(k) = pi*ncr(k,i,3)*n0g*abs(vtmean(k)-vtr(k))*acrfac
         ngacr(k) = ngacr(k)*min(max(0.0,qrs(k,i,3)/qrs(k,i,1)),1.)**2
         ngacr(k) = min(ngacr(k),ncr(k,i,3)*rdtcld)
       endif
     enddo
   endif

   if(lqh) then
     ktop = ktopqh
     do k = kts, ktop



       if(qrs(k,i,4)>qrmin.and.qrs(k,i,1)>qrmin) then
         acrfac = 30.*rslope3(k,1)*rslope2(k,1)*rslope(k,4)                    &
                 +10.*rslope3(k,1)*rslope(k,1)*rslope2(k,4)                    &
                 + 2.*rslope3(k,1)*rslope3(k,4)
         phacr(k) = pisq*ncr(k,i,3)*n0h*abs(vtmean(k)-vtr(k))*(denr/den(k,i))  &
                     *acrfac
         phacr(k) = phacr(k)*min(max(0.0,qrs(k,i,4)/qrs(k,i,1)),1.)**2
         phacr(k) = min(phacr(k),qrs(k,i,1)*rdtcld)
       endif



       if(qrs(k,i,4)>qrmin .and. qrs(k,i,1)>qrmin .and. ncr(k,i,3)>nrmin) then
         acrfac = 1.5*rslope2(k,1)*rslope(k,4)                                 &
                 + 1.0*rslope(k,1)*rslope2(k,4) + .5*rslope3(k,4)
         nhacr(k) = pi*ncr(k,i,3)*n0h*abs(vtmean(k)-vtr(k))*acrfac
         nhacr(k) = nhacr(k)*min(max(0.0,qrs(k,i,4)/qrs(k,i,1)),1.)**2
         nhacr(k) = min(nhacr(k),ncr(k,i,3)/dtcld)
       endif



       if(qrs(k,i,4)>qrmin.and.qrs(k,i,2)>qrmin) then
         acrfac = 5.*rslope3(k,2)*rslope3(k,2)*rslope(k,4)                     &
                 +2.*rslope3(k,2)*rslope2(k,2)*rslope2(k,4)                    &
                 +.5*rslope2(k,2)*rslope2(k,2)*rslope3(k,4)
         phacs(k) = pisq*eachs*n0s*n0sfac(k)*n0h*abs(vtmean(k)-vtmean(k))      &
                     *(dens/den(k,i))*acrfac
         phacs(k) = min(phacs(k),qrs(k,i,2)*rdtcld)
       endif



       if(qrs(k,i,4)>qrmin.and.qrs(k,i,3)>qrmin) then
         acrfac = 5.*rslope3(k,3)*rslope3(k,3)*rslope(k,4)                     &
                 +2.*rslope3(k,3)*rslope2(k,3)*rslope2(k,4)                    &
                 +.5*rslope2(k,3)*rslope2(k,3)*rslope3(k,4)
         phacg(k) = pisq*eachg*n0g*n0h*abs(vtmean(k)-vtmean(k))                &
                     *(deng/den(k,i))*acrfac
         phacg(k) = min(phacg(k),qrs(k,i,3)*rdtcld)
       endif



       if(qrs(k,i,4)>qrmin.or.qrs(k,i,3)>qrmin) then
         rs0 = fsvp(t0c,p(k,i))
         rs0 = ep2*rs0/(p(k,i)-rs0)
         rs0 = max(rs0,qmin)
         ghw1 = den(k,i)*xlv(k,i)*diffus(k)*(rs0-q(k,i)) - viscod(k)*(tcelci(k))
         ghw2 = den(k,i)*(xlf0+cliq*(tcelci(k)))
         ghw3 = venfac(k)
         ghw4 = den(k,i)*(xlf0-cliq*(-tcelci(k))+cice*(-tcelci(k)))
       endif
       if(qrs(k,i,3)>qrmin) then
         if(pgaci(k)>0.0) then
           egi = exp(0.09*(tcelci(k)))
           pgaci_w(k) = pgaci(k)/egi
         else
           pgaci_w(k) = 0.0
         endif
         pgwet(k) = ghw1/ghw2*(precg1*rslope2(k,3)                             &
                   +precg3*ghw3*exp(log(rslope(k,4))*2.75)                     &
                   +ghw4*(pgaci_w(k)+pgacs(k)))
         pgwet(k) = max(pgwet(k), 0.0)
         if(pgacw(k)+pgacr(k)<0.95*pgwet(k)) then
           pgaci(k) = 0.0
           pgacs(k) = 0.0
         endif
       endif



       if(qrs(k,i,4)>qrmin) then
         if(phaci(k)>0.0) then
           ehi = exp(0.09*(tcelci(k)))
           phaci_w(k) = phaci(k)/ehi
         else
           phaci_w(k) = 0.0
         endif
         phwet(k) = ghw1/ghw2*(prech1*rslope2(k,4)                             &
                   +prech3*ghw3*exp(log(rslope(k,4))*2.75)                     &
                   +ghw4*(phaci_w(k)+phacs(k)))
         phwet(k) = max(phwet(k), 0.0)
         if(phacw(k)+phacr(k)<0.95*phwet(k)) then
           phaci(k) = 0.0
           phacs(k) = 0.0
           phacg(k) = 0.0
         endif
       endif
     enddo
   endif



   if(lqs) then
     ktop = ktopqs
     do k = ktop, kts, -1
       if(.not.ifice(k) .and. qrs(k,i,2)>0.) then
         pseml(k) = min(max(-cliq*tcelci(k)*(paacw(k)+psacr(k))/xlf0,          &
                    -qrs(k,i,2)*rdtcld),0.)



         if  (qrs(k,i,2)>qrmin) then
           sfac = rslope(k,2)*n0s*n0sfac(k)/qrs(k,i,2)
           nseml(k) = -sfac*pseml(k)
         endif
       endif
     enddo
   endif



   if(lqg) then
     ktop = ktopqg
     do k = ktop, kts, -1
       if(.not.ifice(k) .and. qrs(k,i,3)>0.) then
         pgeml(k) = min(max(-cliq*tcelci(k)*(paacw(k)+pgacr(k))/xlf0,          &
                   -qrs(k,i,3)*rdtcld),0.)



         if  (qrs(k,i,3)>qrmin) then
           gfac = rslope(k,3)*n0g/qrs(k,i,3)
           ngeml(k) = -gfac*pgeml(k)
         endif
       endif
     enddo
   endif



   if(lqh) then
     ktop = ktopqh
     do k = ktop, kts, -1
       if(.not.ifice(k) .and. qrs(k,i,4)>0.) then
         pheml(k) = min(max(-cliq*tcelci(k)*(phacw(k)+phacr(k))/xlf0,          &
                   -qrs(k,i,4)*rdtcld),0.)



         if  (qrs(k,i,4)>qrmin) then
           gfac = rslope(k,4)*n0h/qrs(k,i,4)
           nheml(k) = -gfac*pheml(k)
         endif
       endif
     enddo
   endif



   if(lqi) then
     ktop = ktopqi
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qci(k,i,2)>0 .and. (.not.ifsat(k))) then
           pidep(k) = 4.*di(k)*ni(k)*(rh_ice(k)-1.)/ab_ice(k)
           supice(k) = satrdt(k) - prevp(k)
           if(pidep(k)<0.) then
             pidep(k) = max(max(pidep(k),satrdt(k)*.5),supice(k))
             pidep(k) = max(pidep(k),-qci(k,i,2)*rdtcld)
           else
             pidep(k) = min(min(pidep(k),satrdt(k)*.5),supice(k))
           endif
           if(abs(prevp(k)+pidep(k))>=abs(satrdt(k))) ifsat(k) = .true.
         endif
       endif
     enddo
   endif



   if(lqs) then
     ktop = ktopqs
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qrs(k,i,2)>0. .and. (.not.ifsat(k))) then
           coeres = rslope2(k,2)*sqrt(rslope(k,2)*rslopeb(k,2))
           psdep(k) = (rh_ice(k)-1.)*n0sfac(k)*(precs1*rslope2(k,2)            &
                      + precs2*venfac(k)*coeres)/ab_ice(k)
           supice(k) = satrdt(k)-prevp(k)-pidep(k)
           if(psdep(k)<0.) then
             psdep(k) = max(psdep(k),-qrs(k,i,2)*rdtcld)
             psdep(k) = max(max(psdep(k),satrdt(k)*.5),supice(k))
           else
             psdep(k) = min(min(psdep(k),satrdt(k)*.5),supice(k))
           endif
           if(abs(prevp(k)+pidep(k)+psdep(k))>=abs(satrdt(k)))                 &
             ifsat(k) = .true.
         endif
       endif
     enddo
   endif



   if(lqg) then
     ktop = ktopqg
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qrs(k,i,3)>0. .and. (.not.ifsat(k))) then
           coeres = rslope2(k,3)*sqrt(rslope(k,3)*rslopeb(k,3))
           pgdep(k) = (rh_ice(k)-1.)*(precg1*rslope2(k,3)                      &
                     + precg2*venfac(k)*coeres)/ab_ice(k)
           supice(k) = satrdt(k)-prevp(k)-pidep(k)-psdep(k)
           if(pgdep(k)<0.) then
             pgdep(k) = max(pgdep(k),-qrs(k,i,3)*rdtcld)
             pgdep(k) = max(max(pgdep(k),satrdt(k)*.5),supice(k))
           else
             pgdep(k) = min(min(pgdep(k),satrdt(k)*.5),supice(k))
           endif
           if(abs(prevp(k)+pidep(k)+psdep(k)+pgdep(k))>=abs(satrdt(k)))        &
             ifsat(k) = .true.
         endif
       endif
     enddo
   endif



   if(lqh) then
     ktop = ktopqh
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qrs(k,i,4)>0. .and. (.not.ifsat(k))) then
           coeres = rslope2(k,4)*sqrt(rslope(k,4)*rslopeb(k,4))
           phdep(k) = (rh_ice(k)-1.)*(prech1*rslope2(k,4)                      &
                     + prech2*venfac(k)*coeres)/ab_ice(k)
           supice(k) = satrdt(k)-prevp(k)-pidep(k)-psdep(k)-pgdep(k)
           if(phdep(k)<0.) then
             phdep(k) = max(phdep(k),-qrs(k,i,4)*rdtcld)
             phdep(k) = max(max(phdep(k),satrdt(k)*.5),supice(k))
           else
             phdep(k) = min(min(phdep(k),satrdt(k)*.5),supice(k))
           endif
           if(abs(prevp(k)+pidep(k)+psdep(k)+pgdep(k)+phdep(k))                &
               >=abs(satrdt(k))) ifsat(k) = .true.
         endif
       endif
     enddo
   endif



   ktop = ktoprh
   do k = ktop, kts, -1
     if(ifice(k)) then
       if(supsat(k)>0 .and. (.not.ifsat(k))) then
         supice(k) = satrdt(k)-prevp(k)-pidep(k)-psdep(k)-pgdep(k)
         if(slimsk(i)==0) then
           ni0(k) = 1000.*exp(-0.2*tcelci(k)-5.)
         else if(slimsk(i)==1) then
           ni0(k) = 1000.*exp(-0.15*tcelci(k)-2.5)
         else if(slimsk(i)==2) then
           ni0(k) = 1000.*exp(-0.35*tcelci(k)-10.)
         endif
         ni0(k) = min(ni0(k),ni0max)
         roqi0 = 4.92e-11*exp(log(ni0(k))*(1.33))
         pigen(k) = max(0.,(roqi0/den(k,i)-max(qci(k,i,2),0.))*rdtcld)
         pigen(k) = min(min(pigen(k),satrdt(k)),supice(k))
       endif
     endif
   enddo



   if(lqi) then
     ktop = ktopqi
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qci(k,i,2)>0.) then
           qimax = roqimax/den(k,i)
           psaut(k) = max(0.,(qci(k,i,2)-qimax)*rdtcld)
         endif
       endif
     enddo
   endif



   if(lqs) then
     ktop = ktopqs
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qrs(k,i,2)>0.) then
           alpha2 = 1.e-3*exp(0.09*(tcelci(k)))
           pgaut(k) = min(max(0.,alpha2*(qrs(k,i,2)-qs0)),qrs(k,i,2)*rdtcld)
         endif
       endif
     enddo
   endif



   if(lqh) then
     ktop = ktopqh
     do k = ktop, kts, -1
       if(ifice(k)) then
         if(qrs(k,i,3)>0.) then
           alpha2 = 1.e-3*exp(0.09*(tcelci(k)))
           phaut(k) = min(max(0.,alpha2*(qrs(k,i,3)-qs0)),qrs(k,i,3)*rdtcld)
         endif
       endif
     enddo
   endif



   if(lqs) then
     ktop = ktopqs
     do k = ktop, kts, -1
       if(.not.ifice(k)) then
         if(qrs(k,i,2)>0. .and. rh_mul(k)<1.) then
           coeres = rslope2(k,2)*sqrt(rslope(k,2)*rslopeb(k,2))
           psevp(k) = (rh_mul(k)-1.)*n0sfac(k)*(precs1*rslope2(k,2)            &
                       + precs2*venfac(k)*coeres)/ab_mul(k)
           psevp(k) = min(max(psevp(k),-qrs(k,i,2)*rdtcld),0.)
         endif
       endif
     enddo
   endif



   if(lqg) then
     ktop = ktopqg
     do k = ktop, kts, -1
       if(.not.ifice(k)) then
         if(qrs(k,i,3)>0. .and. rh_mul(k)<1.) then
           coeres = rslope2(k,3)*sqrt(rslope(k,3)*rslopeb(k,3))
           pgevp(k) = (rh_mul(k)-1.)*(precg1*rslope2(k,3)                      &
                     + precg2*venfac(k)*coeres)/ab_mul(k)
           pgevp(k) = min(max(pgevp(k),-qrs(k,i,3)*rdtcld),0.)
         endif
       endif
     enddo
   endif



   if(lqh) then
     ktop = ktopqh
     do k = ktop, kts, -1
       if(.not.ifice(k)) then
         if(qrs(k,i,4)>0. .and. rh_mul(k)<1.) then
           coeres = rslope2(k,4)*sqrt(rslope(k,4)*rslopeb(k,4))
           phevp(k) = (rh_mul(k)-1.)*(prech1*rslope2(k,4)                      &
                     + prech2*venfac(k)*coeres)/ab_mul(k)
           phevp(k) = min(max(phevp(k),-qrs(k,i,4)*rdtcld),0.)
         endif
       endif
     enddo
   endif





   ktop = ktopmax
   do k = ktop, kts, -1

     delta2 = 0.
     delta3 = 0.
     if(qrs(k,i,1)<1.e-4 .and. qrs(k,i,2)<1.e-4) delta2 = 1.
     if(qrs(k,i,1)<1.e-4) delta3 = 1.

     if(ifice(k)) then



       hvalue = max(qmin,qci(k,i,1))
       source = (praut(k)+pracw(k)+paacw(k)+paacw(k)+phacw(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         praut(k) = praut(k) * factor
         pracw(k) = pracw(k) * factor
         paacw(k) = paacw(k) * factor
         phacw(k) = phacw(k) * factor
       endif



       hvalue = max(qmin,qci(k,i,2))
       source = (psaut(k)-pigen(k)-pidep(k)+praci(k)+psaci(k)+pgaci(k)         &
                + phaci(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         psaut(k) = psaut(k) * factor
         pigen(k) = pigen(k) * factor
         pidep(k) = pidep(k) * factor
         praci(k) = praci(k) * factor
         psaci(k) = psaci(k) * factor
         pgaci(k) = pgaci(k) * factor
         phaci(k) = phaci(k) * factor
       endif



       hvalue = max(qmin,qrs(k,i,1))
       source = (-praut(k)-prevp(k)-pracw(k)+piacr(k)+psacr(k)+pgacr(k)       &
                + phacr(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         praut(k) = praut(k) * factor
         prevp(k) = prevp(k) * factor
         pracw(k) = pracw(k) * factor
         piacr(k) = piacr(k) * factor
         psacr(k) = psacr(k) * factor
         pgacr(k) = pgacr(k) * factor
         phacr(k) = phacr(k) * factor
       endif



       hvalue = max(qmin,qrs(k,i,2))
       source = - (psdep(k)+psaut(k)-pgaut(k)+paacw(k)+piacr(k)*delta3         &
                + pvapg(k)+pvaph(k)                                            &
                + praci(k)*delta3 - pracs(k)*(1.-delta2)                       &
                + psacr(k)*delta2 + psaci(k)-pgacs(k)-phacs(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         psdep(k) = psdep(k) * factor
         psaut(k) = psaut(k) * factor
         pgaut(k) = pgaut(k) * factor
         paacw(k) = paacw(k) * factor
         piacr(k) = piacr(k) * factor
         praci(k) = praci(k) * factor
         psaci(k) = psaci(k) * factor
         pracs(k) = praCs(k) * factor
         psacr(k) = psacr(k) * factor
         pgacs(k) = pgacs(k) * factor
         pvapg(k) = pvapg(k) * factor
         pvaph(k) = pvaph(k) * factor
         phacs(k) = phacs(k) * factor
       endif



       hvalue = max(qmin,qrs(k,i,3))
       source = - (pgdep(k)+pgaut(k)                                           &
                + piacr(k)*(1.-delta3) + praci(k)*(1.-delta3)                  &
                + psacr(k)*(1.-delta2) + pracs(k)*(1.-delta2)                  &
                + pgaci(k)+paacw(k)+pgacr(k)*delta2+pgacs(k)                   &
                - pracg(k)*(1.-delta2)-phacg(k)-phaut(k)                       &
                - pvapg(k)+primh(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         pgdep(k) = pgdep(k) * factor
         pgaut(k) = pgaut(k) * factor
         piacr(k) = piacr(k) * factor
         praci(k) = praci(k) * factor
         psacr(k) = psacr(k) * factor
         pracs(k) = pracs(k) * factor
         paacw(k) = paacw(k) * factor
         pgaci(k) = pgaci(k) * factor
         pgacr(k) = pgacr(k) * factor
         pgacs(k) = pgacs(k) * factor
         phaut(k) = phaut(k) * factor
         pracg(k) = pracg(k) * factor
         phacg(k) = phacg(k) * factor
         pvapg(k) = pvapg(k) * factor
         primh(k) = primh(k) * factor
       endif



       hvalue = max(qmin,qrs(k,i,4))
       source = -(phdep(k)+phaut(k)                                            &
                +pgacr(k)*(1.-delta2)+pracg(k)*(1.-delta2)                     &
                +phacw(k)+phacr(k)+phaci(k)+phacs(k)                           &
                +phacg(k)-pvaph(k)-primh(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         phdep(k) = phdep(k) * factor
         phaut(k) = phaut(k) * factor
         pracg(k) = pracg(k) * factor
         pgacr(k) = pgacr(k) * factor
         phacw(k) = phacw(k) * factor
         phaci(k) = phaci(k) * factor
         phacr(k) = phacr(k) * factor
         phacs(k) = phacs(k) * factor
         phacg(k) = phacg(k) * factor
         pvaph(k) = pvaph(k) * factor
         primh(k) = primh(k) * factor
       endif



       hvalue = max(qmin,ncr(k,i,2))
       source = (nraut(k)+nccol(k)+nracw(k)+naacw(k)+naacw(k)                  &
               +nhacw(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         nraut(k) = nraut(k) * factor
         nccol(k) = nccol(k) * factor
         nracw(k) = nracw(k) * factor
         naacw(k) = naacw(k) * factor
         nhacw(k) = nhacw(k) * factor
       endif



       hvalue = max(qmin,ncr(k,i,3))
       source = (-nraut(k)+nrcol(k)+niacr(k)+nsacr(k)+ngacr(k)                 &
                 +nhacr(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         nraut(k) = nraut(k) * factor
         nrcol(k) = nrcol(k) * factor
         niacr(k) = niacr(k) * factor
         nsacr(k) = nsacr(k) * factor
         ngacr(k) = ngacr(k) * factor
         nhacr(k) = nhacr(k) * factor
       endif



       htotal = -(prevp(k)+psdep(k)+pgdep(k)+pigen(k)+pidep(k)+phdep(k))
       q(k,i) = q(k,i)+htotal*dtcld
       qci(k,i,1) = max(qci(k,i,1) - (praut(k)+pracw(k)                        &
                   +paacw(k)+paacw(k)+phacw(k))*dtcld,0.)
       qrs(k,i,1) = max(qrs(k,i,1) + (praut(k)+pracw(k)                        &
                   +prevp(k)-piacr(k)-pgacr(k)-psacr(k)-phacr(k))*dtcld,0.)
       qci(k,i,2) = max(qci(k,i,2) - (psaut(k)+praci(k)                        &
                   +phaci(k)                                                   &
                   +psaci(k)+pgaci(k)-pigen(k)-pidep(k))*dtcld,0.)
       qrs(k,i,2) = max(qrs(k,i,2) + (psdep(k)+psaut(k)+paacw(k)               &
                   -pgaut(k)+piacr(k)*delta3 + praci(k)*delta3                 &
                   +pvapg(k)+pvaph(k)-phacs(k)                                 &
                   +psaci(k)-pgacs(k)-pracs(k)*(1.-delta2) + psacr(k)*delta2)  &
                   *dtcld,0.)
       qrs(k,i,3) = max(qrs(k,i,3) + (pgdep(k)+pgaut(k)+piacr(k)*(1.-delta3)   &
                   +praci(k)*(1.-delta3) + psacr(k)*(1.-delta2)                &
                   +pracs(k)*(1.-delta2) + pgaci(k)+paacw(k)                   &
                   +pgacr(k)*delta2+pgacs(k)+primh(k)                          &
                   -pracg(k)*(1.-delta2)-phacg(k)-phaut(k)                     &
                   -pvapg(k))*dtcld,0.)
       qrs(k,i,4) = max(qrs(k,i,4)+(phdep(k)+phaut(k)                          &
                   +pgacr(k)*(1.-delta2)+pracg(k)*(1.-delta2)                  &
                   +phacw(k)+phacr(k)+phaci(k)+phacs(k)                        &
                   +phacg(k)-pvaph(k)-primh(k))                                &
                   *dtcld,0.)
       ncr(k,i,2) = max(ncr(k,i,2) + (-nraut(k)-nccol(k)-nracw(k)              &
                   -naacw(k)-naacw(k)-nhacw(k))*dtcld,0.)
       ncr(k,i,3) = max(ncr(k,i,3) + (nraut(k)-nrcol(k)-niacr(k)               &
                   -nsacr(k)-ngacr(k)-nhacr(k))*dtcld,0.)
       hvalue = - xls*(psdep(k)+pgdep(k)+phdep(k)+pidep(k)+pigen(k))           &
               - xlv(k,i)*prevp(k) - xlf(k,i)*(piacr(k)+paacw(k)+phacw(k)      &
               + paacw(k)+pgacr(k)+psacr(k)+phacr(k))
       t(k,i) = t(k,i) - hvalue/cpm(k,i)*dtcld

     else    



       hvalue = max(qmin,qci(k,i,1))
       source=(praut(k)+pracw(k)+paacw(k)+paacw(k)-phacw(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         praut(k) = praut(k) * factor
         pracw(k) = pracw(k) * factor
         paacw(k) = paacw(k) * factor
         phacw(k) = phacw(k) * factor
       endif



       hvalue = max(qmin,qrs(k,i,1))
       source = (-prevp(k)-praut(k)+pseml(k)+pgeml(k)+pheml(k)                 &
                 -pracw(k)-paacw(k)-paacw(k)-phacw(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         praut(k) = praut(k) * factor
         prevp(k) = prevp(k) * factor
         pracw(k) = pracw(k) * factor
         paacw(k) = paacw(k) * factor
         pseml(k) = pseml(k) * factor
         pgeml(k) = pgeml(k) * factor
         phacw(k) = phacw(k) * factor
         pheml(k) = pheml(k) * factor
       endif



       hvalue = max(qmin,qrs(k,i,2))
       source=(pgacs(k)+phacs(k)-pseml(k)-psevp(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         pgacs(k) = pgacs(k) * factor
         pseml(k) = pseml(k) * factor
         psevp(k) = psevp(k) * factor
         phacs(k) = phacs(k) * factor
       endif



       hvalue = max(qmin,qrs(k,i,3))
       source=-(pgacs(k)+pgevp(k)+pgeml(k)-phacg(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         pgacs(k) = pgacs(k) * factor
         pgevp(k) = pgevp(k) * factor
         pgeml(k) = pgeml(k) * factor
         phacg(k) = phacg(k) * factor
       endif



       hvalue = max(qrmin,qrs(k,i,4))
       source=-(phacs(k)+phacg(k)+phevp(k)+pheml(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         phacs(k) = phacs(k)*factor
         phacg(k) = phacg(k)*factor
         phevp(k) = phevp(k)*factor
         pheml(k) = pheml(k)*factor
       endif



       hvalue = max(qmin,ncr(k,i,2))
       source = (+nraut(k)+nccol(k)+nracw(k)+naacw(k)+naacw(k)+nhacw(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         nraut(k) = nraut(k) * factor
         nccol(k) = nccol(k) * factor
         nracw(k) = nracw(k) * factor
         naacw(k) = naacw(k) * factor
         nhacw(k) = nhacw(k) * factor
       endif



       hvalue = max(qmin,ncr(k,i,3))
       source = (-nraut(k)+nrcol(k)-nseml(k)-ngeml(k)-nheml(k))*dtcld
       if (source>hvalue) then
         factor = hvalue/source
         nraut(k) = nraut(k) * factor
         nrcol(k) = nrcol(k) * factor
         nseml(k) = nseml(k) * factor
         ngeml(k) = ngeml(k) * factor
         nheml(k) = nheml(k) * factor
       endif



       htotal = -(prevp(k)+psevp(k)+pgevp(k)+phevp(k))
       q(k,i) = q(k,i) + htotal*dtcld
       qci(k,i,1) = max(qci(k,i,1) - (praut(k)+pracw(k)                        &
                   +paacw(k)+paacw(k)+phacw(k))*dtcld,0.)
       qrs(k,i,1) = max(qrs(k,i,1) + (praut(k)+pracw(k)                        &
                   +prevp(k)+paacw(k)+paacw(k)+phacw(k)                        &
                   -pseml(k)-pgeml(k)-pheml(k))*dtcld,0.)
       qrs(k,i,2) = max(qrs(k,i,2) + (psevp(k)-pgacs(k)-phacs(k)               &
                   +pseml(k))*dtcld,0.)
       qrs(k,i,3) = max(qrs(k,i,3) + (pgacs(k)+pgevp(k)                        &
                   +pgeml(k)-phacg(k))*dtcld,0.)
       qrs(k,i,4) = max(qrs(k,i,4)+(phacs(k)+phacg(k)+phevp(k)                 &
                   +pheml(k))*dtcld,0.)
       ncr(k,i,2) = min(max(ncr(k,i,2) + (-nraut(k)-nccol(k)-nracw(k)          &
                   -naacw(k)-naacw(k)-nhacw(k))*dtcld,0.),ncmax)
       ncr(k,i,3) = min(max(ncr(k,i,3) + (nraut(k)-nrcol(k)+nseml(k)           &
                   +ngeml(k)+nheml(k))*dtcld,0.),nrmax)
       hvalue = -xlv(k,i)*(prevp(k)+psevp(k)+pgevp(k)+phevp(k))                &
                -xlf(k,i)*(pseml(k)+pgeml(k)+pheml(k))
       t(k,i) = t(k,i) - hvalue/cpm(k,i)*dtcld
     endif
   enddo





   call find_cloud_top(1,kdim,ktopini,qrs(:,i,1),zero_0,ktopqr)
   if(ktopqr>0.0) lqr = .true.
   if(lqr) then
     ktop = ktopqr
     call slope_rain(1,kdim,ktop,qrs(:,i,1),den(:,i),denfac(:,i),t(:,i),       &
        ncr(:,i,3),                                                            &
        rslope(:,1),rslopeb(:,1),rslope2(:,1),rslope3(:,1),vtr(:))
     do k = ktop, kts, -1
       if(qrs(k,i,1)>0.) then
         dr(k) = rslope(k,1)*drcoeff



         if(dr(k)<=di50) then
           ncr(k,i,2) = min(ncr(k,i,2) + ncr(k,i,3), ncmax)
           ncr(k,i,3) = 0.



           qci(k,i,1) = qci(k,i,1) + qrs(k,i,1)
           qrs(k,i,1) = 0.
         endif
       endif
     enddo
   endif




   ktop = ktopini
   do k = ktop, kts, -1
     qsat_mul(k) = fsvp_water(t(k,i),p(k,i))
     qsat_mul(k) = ep2 * qsat_mul(k) / (p(k,i) - qsat_mul(k))
     qsat_mul(k) = max(qsat_mul(k),qmin)
     rh_mul(k)   = max(q(k,i) / qsat_mul(k),qmin)
   enddo

   call find_cloud_top(1,kdim,ktopini,rh_mul(:), one_1,ktoprh)

   ktop = ktoprh
   do k = ktop, kts, -1
     if(rh_mul(k)>1.) then
       temp = ((rh_mul(k)-1.)/satmax)
       temp = min(1.,exp(log(temp)*actk))
       ncact(k) = max(0.,((ncr(k,i,1)+ncr(k,i,2))*temp - ncr(k,i,2)))*rdtcld
       ncact(k) =min(ncact(k),max(ncr(k,i,1),0.)*rdtcld)
       pcact(k) = min(4.*pi*denr*exp(log(actr)*3)*ncact(k)/                    &
                     (3.*den(k,i)),max(q(k,i),0.)*rdtcld)
       q(k,i) = max(q(k,i) - pcact(k)*dtcld,0.)
       qci(k,i,1) = max(qci(k,i,1) + pcact(k)*dtcld,0.)
       ncr(k,i,1) = max(ncr(k,i,1) - ncact(k)*dtcld, ccnmin)
       ncr(k,i,2) = max(ncr(k,i,2) + ncact(k)*dtcld, ncmin)
       t(k,i) = t(k,i) + pcact(k)*xlv(k,i)/cpm(k,i)*dtcld
     endif
   enddo



   ktop = ktopini
   do k = ktop, kts, -1
     qsat_mul(k) = fsvp_water(t(k,i),p(k,i))
     qsat_mul(k) = ep2 * qsat_mul(k) / (p(k,i) - qsat_mul(k))
     qsat_mul(k) = max(qsat_mul(k),qmin)
   enddo
   call find_cloud_top(1,kdim,ktopini,qci(:,i,1),zero_0,ktopqc)
   call find_cloud_top(1,kdim,ktopini,rh_mul(:), one_1,ktoprh)
   ktop = max(ktopqc,ktoprh)
   do k = ktop, kts, -1
     hvalue = ((max(q(k,i),qmin)-(qsat_mul(k))) /(1.+(xlv(k,i))*(xlv(k,i))     &
              /(rv*(cpm(k,i)))*(qsat_mul(k)) /((t(k,i))*(t(k,i)))))
     pcond(k) = min(max(hvalue*rdtcld,0.),max(q(k,i),0.)*rdtcld)
     if(qci(k,i,1)>0. .and. hvalue<0.) then
       pcond(k) = max(hvalue,-qci(k,i,1))*rdtcld



       if(pcond(k)==-qci(k,i,1)*rdtcld) then
         ncr(k,i,1) = ncr(k,i,1) + ncr(k,i,2)
         ncr(k,i,2) = 0.
       endif
     endif
     q(k,i) = q(k,i) - pcond(k)*dtcld
     qci(k,i,1) = max(qci(k,i,1)+pcond(k)*dtcld,0.)
     t(k,i) = t(k,i) + pcond(k)*xlv(k,i)/cpm(k,i)*dtcld
   enddo







   lqc = .false.
   lqi = .false.
   lqr = .false.
   lqs = .false.
   lqg = .false.
   lqh = .false.
   flgcld = .false.
   call find_cloud_top(1,kdim,ktopini,qci(:,i,1),zero_0,ktopqc)
   call find_cloud_top(1,kdim,ktopini,qci(:,i,2),zero_0,ktopqi)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,1),zero_0,ktopqr)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,2),zero_0,ktopqs)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,3),zero_0,ktopqg)
   call find_cloud_top(1,kdim,ktopini,qrs(:,i,4),zero_0,ktopqh)
   call find_cloud_top(1,kdim,ktopini,rh_ice(:), one_1,ktoprh)
   if(ktopqc>0.0) lqc = .true.
   if(ktopqi>0.0) lqi = .true.
   if(ktopqr>0.0) lqr = .true.
   if(ktopqs>0.0) lqs = .true.
   if(ktopqg>0.0) lqg = .true.
   if(ktopqh>0.0) lqh = .true.
   ktopmax = max(ktopqc,ktopqi,ktopqr,ktopqs,ktopqg,ktopqh,ktoprh)



   if(lqr) then
     ktop = ktopqr
     call adjust_number_concent(ktopqr,kdim,qrs(:,i,1),ncr(:,i,3),den(:,i),    &
          pidnr,drcoeff,qrmin,nrmin,nrmax,di1000,drmin,drmax)
     call slope_rain(1,kdim,ktop,qrs(:,i,1),den(:,i),denfac(:,i),t(:,i),       &
        ncr(:,i,3),                                                            &
        rslope(:,1),rslopeb(:,1),rslope2(:,1),rslope3(:,1),vtr(:))
     nstep = 1
     do k = ktop, kts, -1
       vtn(k) = pvtrn*rslopeb(k,1)*denfac(k,i)
       hvalue = max(vtr(k),vtn(k))
       nstep = max(nstep,ceiling(dtcld/delz(k,i)*hvalue))
     enddo

     if(ktop>2) then
       do k = ktop-1, kts+1, -1
         temp = (2.*vtr(k)+vtr(k+1)+vtr(k-1))*0.25
         vtr(k) = temp
       enddo
     endif

     do k = ktop, kts, -1
       if(qrs(k,i,1)>qrmin) then
         diameter = drcoeff*rslope(k,1)
         hvalue = fshape(diameter)
         vtn(k) = vtr(k)/hvalue
       else
         vtn(k) = 0.
       endif
     enddo

     niter = ceiling(nstep/sedi_semi_cfl)
     dtcfl = dtcld/niter

     do n = 1, niter
       do k = ktop, kts, -1
         if(qrs(k,i,1)>qrmin) then
           denqr(k) = dend(k,i)*qrs(k,i,1)
           denqn(k) = dend(k,i)*ncr(k,i,3)
         else
           denqr(k) = 0.0
           denqn(k) = 0.0
           vtr(k) = 0.0
           vtn(k) = 0.0
         endif
       enddo

       call semi_lagrangian(1,kdim,max(ktop,2),dend(:,i),denfac(:,i),t(:,i),   &
            delz(:,i),vtr(:),denqr(:),qrpath,dtcfl,lat,i)
       call semi_lagrangian(1,kdim,max(ktop,2),dend(:,i),denfac(:,i),t(:,i),   &
            delz(:,i),vtn(:),denqn(:),qnpath,dtcfl,lat,i)
       do k = ktop, kts, -1
         if(denqr(k)>qrmin) then
           qrs(k,i,1) = max(denqr(k)/dend(k,i),0.)
           ncr(k,i,3) = max(denqn(k)/dend(k,i),0.)
         else
           qrs(k,i,1) = 0.0
           ncr(k,i,3) = 0.0
         endif
       enddo

       precip_r = qrpath/dtcld + precip_r   

     enddo
   endif



   if(lqs .or. lqg .or. lqh) then
     ktop =ktopqs
     call slope_snow(1,kdim,ktop,qrs(:,i,2),den(:,i),denfac(:,i),t(:,i),       &
          rslope(:,2),rslopeb(:,2),rslope2(:,2),rslope3(:,2),vts(:))
     ktop =ktopqg
     call slope_graupel(1,kdim,ktop,qrs(:,i,3),den(:,i),denfac(:,i),t(:,i),    &
          rslope(:,3),rslopeb(:,3),rslope2(:,3),rslope3(:,3),vtg(:))
     ktop =ktopqh
     call slope_hail(1,kdim,ktop,qrs(:,i,4),den(:,i),denfac(:,i),t(:,i),       &
          rslope(:,4),rslopeb(:,4),rslope2(:,4),rslope3(:,4),vth(:))

     ktop = max(ktopqs,ktopqg,ktopqh)
     do k = ktop, kts, -1
       sumice(k) = max( (qrs(k,i,2) + qrs(k,i,3) + qrs(k,i,4)), qmin)
       if(sumice(k)>qmin) then
         vtmean(k) = (vts(k)*qrs(k,i,2) + vtg(k)*qrs(k,i,3)                    &
                   + vth(k)*qrs(k,i,4))/sumice(k)
       else
         vtmean(k) = 0.
       endif
     enddo
     if(ktop>2) then
       do k = ktop-1, kts+1, -1
         vtmean(k) = (2*vtmean(k)+vtmean(k+1)+vtmean(k-1))/4.
       enddo
     endif

     nstep = max(1,ceiling(maxval(dtcld/delz(:,i)*vtmean(:))))
     niter = ceiling(nstep/sedi_semi_cfl)
     dtcfl = dtcld/niter

     do n = 1, niter

       ktop =ktopqs
       do k = ktop, kts, -1
         denqs(k) = dend(k,i)*qrs(k,i,2)
       enddo
       call semi_lagrangian(1,kdim,max(ktop,2),dend(:,i),denfac(:,i),t(:,i),   &
            delz(:,i),vtmean(:),denqs(:),qspath,dtcfl,lat,i)
       do k = kts, ktop
         qrs(k,i,2) = max(denqs(k)/dend(k,i),0.)
       enddo
       ktop =ktopqg
       do k = ktop, kts, -1
         denqg(k) = dend(k,i)*qrs(k,i,3)
       enddo
       call semi_lagrangian(1,kdim,max(ktop,2),dend(:,i),denfac(:,i),t(:,i),   &
            delz(:,i),vtmean(:),denqg(:),qgpath,dtcfl,lat,i)
       do k = kts, ktop
         qrs(k,i,3) = max(denqg(k)/dend(k,i),0.)
       enddo
       ktop =ktopqh
       do k = ktop, kts, -1
         denqh(k) = dend(k,i)*qrs(k,i,4)
       enddo
       call semi_lagrangian(1,kdim,max(ktop,2),dend(:,i),denfac(:,i),t(:,i),   &
            delz(:,i),vtmean(:),denqh(:),qhpath,dtcfl,lat,i)
       do k = kts, ktop
         qrs(k,i,4) = max(denqh(k)/dend(k,i),0.)
       enddo

       precip_s = qspath/dtcld + precip_s   
       precip_g = qgpath/dtcld + precip_g   
       precip_h = qhpath/dtcld + precip_h   
     enddo
   endif



   if(lqi) then
     ktop = ktopqi
     do k = ktop, kts, -1
       temp = (den(k,i)*max(qci(k,i,2),qcmin))
       temp = exp(log(temp)*0.75)
       ni(k) = min(max(5.38e7*temp,1.e3),1.e6)
       if(qci(k,i,2)<=0.0) then
         vti(k) = 0.
       else
         mi(k)  = den(k,i)*qci(k,i,2)/ni(k)
         di(k)  = max(min(exp(log((mi(k)/cxmi))*(1./dxmi)),dimax), qmin)
         vti(k) = avti*exp(log(di(k))*(bvti))
         hvalue = min(max((tcelci(k) - t2_sphere)/(t1_sphere-t2_sphere),0.),1.0)
         dis(k) = max(min(exp(log((mi(k)/cxmis))*(1./dxmis)),dimax), qmin)
         vtis(k)= avtis*exp(log(dis(k))*(bvtis))
         di(k)  = di(k)  * (1.-hvalue) + dis(k)  * hvalue
         vti(k) = vti(k) * (1.-hvalue) + vtis(k) * hvalue
       endif
     enddo
     if(ktop>2) then
       do k = ktop-1, kts+1, -1
         vti(k) = (2*vti(k)+vti(k+1)+vti(k-1))/4.
       enddo
     endif

     nstep = max(1,ceiling(maxval(dtcld/delz(:,i)*vti(:))))
     niter = ceiling(nstep/sedi_semi_cfl)
     dtcfl = dtcld/niter

     do n = 1, niter
       do k = ktop, kts, -1
         denqi(k) = dend(k,i)*qci(k,i,2)
       enddo

       call semi_lagrangian(1,kdim,max(ktop,2),dend(:,i),denfac(:,i),t(:,i),   &
            delz(:,i),vti(:),denqi(:),qipath,dtcfl,lat,i)
       do k = kts, ktop
         qci(k,i,2) = max(denqi(k)/dend(k,i),0.)
       enddo

       precip_i = qipath/dtcld + precip_i   
     enddo
   endif






   temp = 1000.
   precip_sum = precip_r + precip_s + precip_i + precip_g + precip_h
   precip_ice = precip_s + precip_i + precip_h

   if(precip_sum>0.) then
     hvalue =  precip_sum/denr*dtcld*temp
     rainncv(i) = hvalue + rainncv(i)
     rain(i)    = hvalue + rain(i)
   endif

   if(precip_ice>0.) then
     hvalue = precip_ice/denr*dtcld*temp
     tstepsnow   = hvalue + tstepsnow
     if ( present (snowncv) .and. present (snow)) then
       snowncv(i) = hvalue + snowncv(i)
       snow(i)    = hvalue + snow(i)
      endif
   endif

   if(precip_g>0.) then
     hvalue = precip_g/denr*dtcld*temp
     tstepgraupel   = hvalue + tstepgraupel
     if ( present (graupelncv) .and. present (graupel)) then
       graupelncv(i) = hvalue + graupelncv(i)
       graupel(i)    = hvalue + graupel(i)
      endif
   endif

   if(precip_h>0.) then
     hvalue = precip_h/denr*dtcld*temp
     tstephail   = hvalue + tstephail
     if ( present (hailncv) .and. present (hail)) then
       hailncv(i) = hvalue + hailncv(i)
       hail(i)    = hvalue + hail(i)
      endif
   endif

   if ( present (snow) ) then
     if(precip_sum>0.) sr(i) = snowncv(i)/(rainncv(i) + qmin)
   else
     if(precip_sum>0.) sr(i) = tstepsnow/(rainncv(i) + qmin)
   endif


   enddo i_loop                  



   enddo inner_loop                    






   ktop = kte
   do k = kts, ktop
     do i = its, ite
       t1(i,k) = t(k,i)
       q1(i,k) = q(k,i)
       ncr1(i,k,1) = ncr(k,i,1)
       ncr1(i,k,2) = ncr(k,i,2)
       ncr1(i,k,3) = ncr(k,i,3)
     enddo
   enddo

   do k = kts, ktop
     do i = its, ite
       qci1(i,k,1) = qci(k,i,1)
       qrs1(i,k,1) = qrs(k,i,1)
       qci1(i,k,2) = qci(k,i,2)
       qrs1(i,k,2) = qrs(k,i,2)
       qrs1(i,k,3) = qrs(k,i,3)
       qrs1(i,k,4) = qrs(k,i,4)
     enddo
   enddo


end subroutine udm2d




   subroutine slope_rain(idim, kdim, ktop, qrs, den, denfac, t,                &
                         ncr,                                                  &
                         rslope, rslopeb, rslope2, rslope3, vt)

   implicit none

   integer       ::               idim, kdim, ktop
   real, dimension( idim , kdim),                                   &
        intent(in   ) ::                                                       &
                                                                          qrs, &
                                                                          ncr, &
                                                                          den, &
                                                                       denfac, &
                                                                            t
  real, dimension( idim, kdim),                                     &
        intent(inout   ) ::                                                    &
                                                                       rslope, &
                                                                      rslopeb, &
                                                                      rslope2, &
                                                                      rslope3, &
                                                                           vt
  real       ::  lamda, lamdar1m, lamdar2m, x, y, z, hvalue
  integer :: i, j, k


   lamdar2m(x,y,z)= exp(log(((pidnr*z)/(x*y)))*((.33333333)))
   lamdar1m(x,y)=   exp(log(pidn0r/(x*y))*(0.25))      

   do i = 1,idim
     do k = 1,ktop
       if(qrs(i,k)<=qrmin .or. ncr(i,k)<=nrmin ) then
         rslope(i,k) = rslopermax
         rslopeb(i,k) = rsloperbmax
         rslope2(i,k) = rsloper2max
         rslope3(i,k) = rsloper3max
       else
         lamda = min(max(lamdar2m(qrs(i,k),den(i,k),ncr(i,k)),lamdarmin),      &
                 lamdarmax)
         if(qrs(i,k)<=0.1e-3)then
           hvalue = lamdar1m(qrs(i,k),den(i,k))
           lamda = max(lamda,hvalue)
         endif
         rslope(i,k) = 1./lamda
         rslopeb(i,k) = exp(log(rslope(i,k))*(bvtr))
         rslope2(i,k) = rslope(i,k)*rslope(i,k)
         rslope3(i,k) = rslope2(i,k)*rslope(i,k)
       endif
       vt(i,k) = pvtr*rslopeb(i,k)*denfac(i,k)
       if(qrs(i,k)<=0.0) vt(i,k) = 0.0
     enddo
   enddo
end subroutine slope_rain




   subroutine slope_graupel(idim, kdim, ktop, qrs, den, denfac, t, rslope,     &
                         rslopeb, rslope2, rslope3, vt)

   implicit none


   integer       ::               idim, kdim, ktop
   real, dimension( idim , kdim),                                   &
        intent(in   ) ::                                                       &
                                                                          qrs, &
                                                                          den, &
                                                                       denfac, &
                                                                            t
  real, dimension( idim, kdim),                                     &
        intent(inout   ) ::                                                    &
                                                                       rslope, &
                                                                      rslopeb, &
                                                                      rslope2, &
                                                                      rslope3, &
                                                                           vt
  real       ::  lamda, lamdag, x, y, z, supcol
  integer :: i, j, k


   lamdag(x,y)=   exp(log(pidn0g/(x*y))*(0.25))      

   do i = 1,idim
     do k = 1,ktop
       if(qrs(i,k)<=qrmin)then
         rslope(i,k) = rslopegmax
         rslopeb(i,k) = rslopegbmax
         rslope2(i,k) = rslopeg2max
         rslope3(i,k) = rslopeg3max
       else
         lamda = min(lamdag(qrs(i,k),den(i,k)),lamdagmax)
         rslope(i,k) = 1./lamda
         rslopeb(i,k) = exp(log(rslope(i,k))*(bvtg))
         rslope2(i,k) = rslope(i,k)*rslope(i,k)
         rslope3(i,k) = rslope2(i,k)*rslope(i,k)
       endif
       vt(i,k) = pvtg*rslopeb(i,k)*denfac(i,k)
       if(qrs(i,k)<=0.0) vt(i,k) = 0.0
     enddo
   enddo
end subroutine slope_graupel




   subroutine slope_hail(idim, kdim, ktop, qrs, den, denfac, t, rslope,        &
                         rslopeb, rslope2, rslope3, vt)

   implicit none


   integer       ::               idim, kdim, ktop
   real, dimension( idim , kdim),                                   &
        intent(in   ) ::                                                       &
                                                                          qrs, &
                                                                          den, &
                                                                       denfac, &
                                                                            t
  real, dimension( idim, kdim),                                     &
        intent(inout   ) ::                                                    &
                                                                       rslope, &
                                                                      rslopeb, &
                                                                      rslope2, &
                                                                      rslope3, &
                                                                           vt
  real       ::  lamda, lamdah, x, y, z, supcol
  integer :: i, j, k


   lamdah(x,y)=   exp(log(pidn0h/(x*y))*(0.25))      

   do i = 1,idim
     do k = 1,ktop
       if(qrs(i,k)<=qrmin)then
         rslope(i,k) = rslopehmax
         rslopeb(i,k) = rslopehbmax
         rslope2(i,k) = rslopeh2max
         rslope3(i,k) = rslopeh3max
       else
         lamda = min(lamdah(qrs(i,k),den(i,k)),lamdahmax)
         rslope(i,k) = 1./lamda
         rslopeb(i,k) = exp(log(rslope(i,k))*(bvth))
         rslope2(i,k) = rslope(i,k)*rslope(i,k)
         rslope3(i,k) = rslope2(i,k)*rslope(i,k)
       endif
       vt(i,k) = pvth*rslopeb(i,k)*denfac(i,k)
       if(qrs(i,k)<=0.0) vt(i,k) = 0.0
     enddo
   enddo
end subroutine slope_hail




   subroutine slope_cloud(idim, kdim, ktop, qrs, ncr, den, denfac, t, qmin,    &
                          rslope, rslope2, rslope3)

   implicit none

   integer       ::               idim, kdim, ktop
   real, dimension( idim , kdim),                                   &
        intent(in   ) ::                                                       &
                                                                          qrs, &
                                                                          ncr, &
                                                                          den, &
                                                                       denfac, &
                                                                            t
  real, dimension( idim, kdim),                                     &
        intent(inout   ) ::                                                    &
                                                                       rslope, &
                                                                      rslope2, &
                                                                      rslope3
  real       ::  lamda, lamdac, x, y, z, supcol, qmin
  integer :: i, j, k


   lamdac(x,y,z)= exp(log(((pidnc*z)/(x*y)))*((.33333333)))

   do i = 1,idim
     do k = 1,ktop
       if(qrs(i,k)<=qmin .or. ncr(i,k)<=ncmin )then
         rslope(i,k) = rslopecmax
         rslope2(i,k) = rslopec2max
         rslope3(i,k) = rslopec3max
       else
         lamda = min(max(lamdac(qrs(i,k),den(i,k),ncr(i,k)),lamdacmin),        &
                 lamdacmax)
         rslope(i,k) = 1./lamda
         rslope2(i,k) = rslope(i,k)*rslope(i,k)
         rslope3(i,k) = rslope2(i,k)*rslope(i,k)
       endif
     enddo
   enddo
end subroutine slope_cloud




subroutine slope_snow(idim, kdim, ktop, qrs, den, denfac, t, rslope, rslopeb,  &
                            rslope2, rslope3, vt)

   implicit none

   integer       ::               idim, kdim, ktop
   real, dimension( idim , kdim),                                   &
         intent(in   ) ::                                                      &
                                                                          qrs, &
                                                                          den, &
                                                                       denfac, &
                                                                            t
   real, dimension( idim , kdim),                                   &
         intent(inout   ) ::                                                   &
                                                                       rslope, &
                                                                      rslopeb, &
                                                                      rslope2, &
                                                                      rslope3, &
                                                                           vt
   real, parameter  :: t0c = 273.15
   real, dimension( idim , kdim ) ::                                &
                                                                       n0sfac
   real       ::  lamda, lamdas, x, y, z, supcol
   integer :: i, j, k


   lamdas(x,y,z)= exp(log((pidn0s*z)/(x*y))*(0.25))  

   do i = 1,idim
     do k = 1, ktop
       supcol = t0c-t(i,k)



       n0sfac(i,k) = max(min(exp(alpha*supcol),n0smax/n0s),1.)
       if(qrs(i,k)<=qrmin)then
         rslope(i,k) = rslopesmax
         rslopeb(i,k) = rslopesbmax
         rslope2(i,k) = rslopes2max
         rslope3(i,k) = rslopes3max
       else
         lamda = min(lamdas(qrs(i,k),den(i,k),n0sfac(i,k)),lamdasmax)
         rslope(i,k) = 1./lamda
         rslopeb(i,k) = exp(log(rslope(i,k))*(bvts))
         rslope2(i,k) = rslope(i,k)*rslope(i,k)
         rslope3(i,k) = rslope2(i,k)*rslope(i,k)
       endif
       vt(i,k) = pvts*rslopeb(i,k)*denfac(i,k)
       if(qrs(i,k)<=0.0) vt(i,k) = 0.0
     enddo
   enddo
end subroutine slope_snow




   subroutine semi_lagrangian(im, km, ktop, dendl, denfacl, tkl, dzl,          &
                                  wwl, rql, precip, dt, lat, lon)


























   implicit none

   integer               , intent(in   ) :: im, km, ktop
   integer               , intent(in   ) :: lat, lon
   real                  , intent(in   ) :: dt
   real, dimension(im,km), intent(in   ) :: dzl
   real, dimension(im,km), intent(in   ) :: dendl
   real, dimension(im,km), intent(in   ) :: denfacl
   real, dimension(im,km), intent(in   ) :: tkl
   real, dimension(km), intent(in   )    :: wwl
   real, dimension(km), intent(inout)    :: rql
   real, intent(inout)                   :: precip



   real, dimension(km)   ::                      &
                                                        dz, &
                                                        ww, &
                                                        qq, &
                                                        wd, &
                                                        wa, &
                                                       den, &
                                                    denfac, &
                                                        tk, &
                                                        qn
   real, dimension(km+1) ::                  wi, &
                                                        zi, &
                                                       qmi, &
                                                       qpi, &
                                                       dza, &
                                                        qa
   real, dimension(km+2) ::                  za

   integer               :: i, k, n, m, kk, kb, kt, lond, latd
   real  ::                                                &
                            tl, tl2, qql, dql, qqd                   ,&
                            th, th2, qqh, dqh                        ,&
                            zsum, qsum, dim, dip                     ,&
                            zsumt, qsumt, zsumb, qsumb               ,&
                            allold, allnew, zz, dzamin, cflmax, decfl
   real  ::      tmp

   real, parameter       ::                                &
                            cfac = 0.05, fa1 = 9./16., fa2 = 1./16.

   lond = 101
   latd = 1

   zi(:) = 0.    ; wi(:) = 0.   ; qa(:) = 0.
   wa(:) = 0.    ; wd(:) = 0.   ; dza(:) = 0.
   precip = 0.0  ; tmp = 0.0



   semi_loop : do i = 1,im


     dz(:) = dzl(i,:)
     den(:) = dendl(i,:)
     denfac(:) = denfacl(i,:)
     tk(:) = tkl(i,:)
     qq(:) = rql(:)
     ww(:) = wwl(:)


     allold = 0.0
     do k = 1,ktop
       allold = allold + qq(k)
     enddo
     if(allold<=0.0) then
       cycle semi_loop
     endif


     zi(1)=0.0
     do k = 1,ktop
       zi(k+1) = zi(k)+dz(k)
     enddo


     wd(:) = ww(:)
     n=1
     wi(1) = ww(1)
     wi(ktop+1) = ww(ktop)
     do k=2,ktop
       wi(k) = (ww(k)*dz(k-1)+ww(k-1)*dz(k))/(dz(k-1)+dz(k))
     enddo


     wi(1) = ww(1)
     wi(2) = 0.5*(ww(2)+ww(1))
     do k=3,ktop-1
       wi(k) = fa1*(ww(k)+ww(k-1))-fa2*(ww(k+1)+ww(k-2))
     enddo
     wi(ktop) = 0.5*(ww(ktop)+ww(ktop-1))
     wi(ktop+1) = ww(ktop)


     do k=2,ktop
       if( ww(k)==0.0 ) wi(k)=ww(k-1)
     enddo


     do k=ktop,1,-1
       decfl = (wi(k+1)-wi(k))*dt/dz(k)
       if( decfl > cfac ) then
         wi(k) = wi(k+1) - cfac*dz(k)/dt
       endif
     enddo


     do k=1,ktop+1
       za(k) = zi(k) - wi(k)*dt
     enddo
     za(ktop+2) = zi(ktop+1)   

     do k=1,ktop+1  
       dza(k) = za(k+1)-za(k)
     enddo


     do k=1,ktop
       tmp = qq(k)*dz(k)/dza(k)
       qa(k) = tmp
     enddo
     qa(ktop+1) = 0.0


     do k=2,ktop
       dip=(qa(k+1)-qa(k))/(dza(k+1)+dza(k))
       dim=(qa(k)-qa(k-1))/(dza(k-1)+dza(k))
       if( dip*dim<=0.0 ) then
         qmi(k)=qa(k)
         qpi(k)=qa(k)
       else
         qpi(k)=qa(k)+0.5*(dip+dim)*dza(k)
         qmi(k)=2.0*qa(k)-qpi(k)
         if( qpi(k)<0.0 .or. qmi(k)<0.0 ) then
           qpi(k) = qa(k)
           qmi(k) = qa(k)
         endif
       endif
     enddo
     qpi(1)=qa(1)
     qmi(1)=qa(1)
     qmi(ktop+1)=qa(ktop+1)
     qpi(ktop+1)=qa(ktop+1)


     qn = 0.0
     kb=1
     kt=1
     intp : do k=1,ktop
            kb=max(kb-1,1)
            kt=max(kt-1,1)

            if( zi(k)>=za(ktop+1) ) then
              exit intp
            else
              find_kb : do kk=kb,ktop
                        if( zi(k)<=za(kk+1) ) then
                          kb = kk
                          exit find_kb
                        else
                          cycle find_kb
                        endif
              enddo find_kb
              find_kt : do kk=kt,ktop+2    
                        if( zi(k+1)<=za(kk) ) then
                          kt = kk
                          exit find_kt
                        else
                          cycle find_kt
                        endif
              enddo find_kt
              kt = kt - 1


              if( kt==kb ) then
                tl=(zi(k)-za(kb))/dza(kb)
                th=(zi(k+1)-za(kb))/dza(kb)
                tl2=tl*tl
                th2=th*th
                qqd=0.5*(qpi(kb)-qmi(kb))
                qqh=qqd*th2+qmi(kb)*th
                qql=qqd*tl2+qmi(kb)*tl
               qn(k) = (qqh-qql)/(th-tl)
              else if( kt>kb ) then
                tl=(zi(k)-za(kb))/dza(kb)
                tl2=tl*tl
                qqd=0.5*(qpi(kb)-qmi(kb))
                qql=qqd*tl2+qmi(kb)*tl
                dql = qa(kb)-qql
                zsum  = (1.-tl)*dza(kb)
                qsum  = dql*dza(kb)
                if( kt-kb>1 ) then
                do m=kb+1,kt-1
                  zsum = zsum + dza(m)
                  qsum = qsum + qa(m) * dza(m)
                enddo
                endif
                th=(zi(k+1)-za(kt))/dza(kt)
                th2=th*th
                qqd=0.5*(qpi(kt)-qmi(kt))
                dqh=qqd*th2+qmi(kt)*th
                zsum  = zsum + th*dza(kt)
                qsum  = qsum + dqh*dza(kt)
                qn(k) = qsum/zsum
              endif
               cycle intp
             endif

     enddo intp


     sum_precip: do k=1,ktop
                   if( za(k)<0.0 .and. za(k+1)<=0.0 ) then          
                      precip = precip + qa(k)*dza(k)
                      cycle sum_precip
                   else if ( za(k)<0.0 .and. za(k+1)>0.0 ) then    
                      th = (0.0-za(k))/dza(k)               
                      th2 = th*th                           
                      qqd = 0.5*(qpi(k)-qmi(k))             
                      qqh = qqd*th2+qmi(k)*th               
                      precip = precip + qqh*dza(k)    
                      exit sum_precip
                   endif
                   exit sum_precip
     enddo sum_precip


     rql(:) = qn(:)


   enddo semi_loop

end subroutine semi_lagrangian




subroutine cldf_diag(idim, kdim, t, p, q, qc, qi, dx,cldf, ktop)

   implicit none






   integer,                     intent(in   ) :: idim, kdim, ktop
   real, dimension(idim),       intent(in   ) :: dx
   real, dimension(idim, kdim), intent(in   ) :: t, p, q
   real, dimension(idim, kdim), intent(in   ) :: qc
   real, dimension(idim, kdim), intent(in   ) :: qi
   real, dimension(idim, kdim), intent(out  ) :: cldf

   integer :: i, k, kk
   real, parameter :: cldmin = 50., cldmax = 100.
   real, parameter :: clddiff = cldmax - cldmin
   real, parameter :: cldf_min = 0.5
   real :: cv_w_min,cv_i_min,cvf_min
   real :: cv_w_max,cv_i_max,cvf_max
   real :: dxkm

   do k = 1,ktop
     do i = 1,idim
       cldf(i,k) = 0.
     enddo
   enddo

   do k = 1,ktop
     do i = 1, idim
       cv_w_min = 4.82*(max(0.,qc(i,k))*1000.)**0.94/1.04
       cv_w_max = 5.77*(max(0.,qc(i,k))*1000.)**1.07/1.04
       cv_i_min = 4.82*(max(0.,qi(i,k))*1000.)**0.94/0.96
       cv_i_max = 5.77*(max(0.,qi(i,k))*1000.)**1.07/0.96
       cvf_min = cv_i_min+cv_w_min
       cvf_max = cv_i_max+cv_w_max
       dxkm    = dx(i)/1000.
       cldf(i,k)= ((dxkm-cldmin)*(cvf_max-cvf_min)+clddiff*cvf_min)/clddiff
     enddo
   enddo
   do k = 1,ktop
     do i = 1,idim
       cldf(i,k)=min(1.,max(0.,cldf(i,k)))
       if(qc(i,k)+qi(i,k)<1.e-6) then
         cldf(i,k) = 0.
       endif
       if(cldf(i,k)<0.01) cldf(i,k) = 0.
       if(cldf(i,k)>0.99) cldf(i,k) = 1.
       if(cldf(i,k)>=0.01 .and. cldf(i,k)<cldf_min) cldf(i,k) = cldf_min 
     enddo
   enddo

end subroutine cldf_diag




subroutine udminit(den0,denr,dens,cl,cv,ccn0,allowed_to_read)

  implicit none

   real, intent(in) :: den0,denr,dens,cl,cv,ccn0
   logical, intent(in) :: allowed_to_read
   integer :: n

   pi = 4.*atan(1.)
   pisq = pi * pi
   xlv1 = cl-cv

   pidnc = pi*denr/6.

   bvtr1 = 1.+bvtr
   bvtr2 = 2.+bvtr
   bvtr3 = 3.+bvtr
   bvtr4 = 4.+bvtr
   bvtr6 = 6.+bvtr
   g1pbr = rgmma(bvtr1)
   g3pbr = rgmma(bvtr3)
   g4pbr = rgmma(bvtr4)            
   g6pbr = rgmma(bvtr6)
   bvtr5 = 5.+bvtr
   bvtr7 = 7.+bvtr
   bvtr2o5 = 2.5+.5*bvtr
   bvtr3o5 = 3.5+.5*bvtr
   g2pbr = rgmma(bvtr2)
   g5pbr = rgmma(bvtr5)
   g7pbr = rgmma(bvtr7)
   g5pbro2 = rgmma(bvtr2o5)
   g7pbro2 = rgmma(bvtr3o5)
   pvtr = avtr*g5pbr/24.
   pvtrn = avtr*g2pbr
   eacrr = 1.0
   pacrr = pi*n0r*avtr*g3pbr*.25*eacrr
   pidn0r =  pi*denr*n0r
   pidnr  = 4.*pi*denr
   precr1 = 2.*pi*1.56
   precr2 = 2.*pi*.31*avtr**.5*g7pbro2
   roqimax = 2.08e22*dimax**8

   bvts1 = 1.+bvts
   bvts2 = 2.5+.5*bvts
   bvts3 = 3.+bvts
   bvts4 = 4.+bvts
   g1pbs = rgmma(bvts1)    
   g3pbs = rgmma(bvts3)
   g4pbs = rgmma(bvts4)    
   g5pbso2 = rgmma(bvts2)
   pvts = avts*g4pbs/6.
   pacrs = pi*n0s*avts*g3pbs*.25
   precs1 = 4.*n0s*.65
   precs2 = 4.*n0s*.44*avts**.5*g5pbso2
   pidn0r =  pi*denr*n0r
   pidn0s =  pi*dens*n0s
   pacrc = pi*n0s*avts*g3pbs*.25*eacrc

   bvtg1 = 1.+bvtg
   bvtg2 = 2.5+.5*bvtg
   bvtg3 = 3.+bvtg
   bvtg4 = 4.+bvtg
   g1pbg = rgmma(bvtg1)
   g3pbg = rgmma(bvtg3)
   g4pbg = rgmma(bvtg4)
   pacrg = pi*n0g*avtg*g3pbg*.25
   g5pbgo2 = rgmma(bvtg2)
   pvtg = avtg*g4pbg/6.
   precg1 = 2.*pi*n0g*.78
   precg2 = 2.*pi*n0g*.31*avtg**.5*g5pbgo2
   pidn0g =  pi*deng*n0g

   rslopermax = 1./lamdarmax
   rslopesmax = 1./lamdasmax
   rslopegmax = 1./lamdagmax
   rsloperbmax = rslopermax ** bvtr
   rslopesbmax = rslopesmax ** bvts
   rslopegbmax = rslopegmax ** bvtg
   rsloper2max = rslopermax * rslopermax
   rslopes2max = rslopesmax * rslopesmax
   rslopeg2max = rslopegmax * rslopegmax
   rsloper3max = rsloper2max * rslopermax
   rslopes3max = rslopes2max * rslopesmax
   rslopeg3max = rslopeg2max * rslopegmax

   rslopecmax  = 1./lamdacmax
   rslopec2max = rslopecmax * rslopecmax
   rslopec3max = rslopec2max * rslopecmax

   g6pbgh = rgmma(2.75)
   precg3 = 2.*pi*n0g*.31*g6pbgh*sqrt(sqrt(4.*deng/3./cd))
   bvth2 = 2.5+.5*bvth
   bvth3 = 3.+bvth
   bvth4 = 4.+bvth
   g3pbh = rgmma(bvth3)
   g4pbh = rgmma(bvth4)
   g5pbho2 = rgmma(bvth2)
   pacrh = pi*n0h*avth*g3pbh*.25
   pvth = avth*g4pbh/6.
   prech1 = 2.*pi*n0h*.78
   prech2 = 2.*pi*n0h*.31*avth**.5*g5pbho2
   prech3 = 2.*pi*n0h*.31*g6pbgh*sqrt(sqrt(4.*denh/3./cd))
   pidn0h = pi*denh*n0h

   rslopehmax = 1./lamdahmax
   rslopehbmax = rslopehmax ** bvth
   rslopeh2max = rslopehmax * rslopehmax
   rslopeh3max = rslopeh2max * rslopehmax

   mrmin = pi/6.*denr*di50**3.
   do n = 1, numax
     ccg(n) = rgmma(bm_r1+n+1.)
     ocg1(n) = 1./rgmma(1.+n)
   enddo





   xam_r = pi*denr/6.
   xbm_r = 3.
   xmu_r = 2.
   xam_s = pi*dens/6.
   xbm_s = 3.
   xmu_s = 0.
   xam_g = pi*deng/6.
   xbm_g = 3.
   xmu_g = 0.
   xam_h = pi*denh/6.
   xbm_h = 3.
   xmu_h = 0.

   call radar_init


end subroutine udminit




real function rgmma(x)

   implicit none


   real :: euler
   parameter (euler=0.577215664901532)
   real :: x, y
   integer :: i
   if(x==1.)then
     rgmma=0.
       else
     rgmma=x*exp(euler*x)
     do i=1,10000
       y=float(i)
       rgmma=rgmma*(1.000+x/y)*exp(-x/y)
     enddo
     rgmma=1./rgmma
   endif
end function rgmma

   subroutine adjust_number_concent(ktop,kdim,qq,nn,den,piconst,drconst,       &
              qqmin,nnmin,nnmax,di0,dimin,dimax)

   implicit none

   integer , intent(in ) :: ktop, kdim
   real, dimension(kdim) , intent(inout) :: qq,nn,den
   real, intent(in ) :: piconst,drconst,qqmin,nnmin,nnmax
   real, intent(in ) :: di0,dimin,dimax

   real(kind=kind_phys) :: lamdar, diameter
   integer :: k

   do k = ktop, 1, -1
     if(qq(k)>qqmin) then
       if(nn(k) <= nnmin) then
         lamdar = drconst/di0
         nn(k) = den(k)*qq(k)*exp(log(lamdar)*3.)/piconst
       endif
       lamdar = exp(log(((piconst*nn(k))/(den(k)*qq(k))))*((.33333333)))
       diameter = drconst/lamdar
       if (diameter > dimax) then
         lamdar = drconst/dimax
         nn(k) = den(k)*qq(k)*exp(log(lamdar)*3.)/piconst
       elseif (diameter < dimin) then
         lamdar = drconst/dimin
         nn(k) = den(k)*qq(k)*exp(log(lamdar)*3.)/piconst
       endif
       nn(k) = min(nn(k),nnmax)
     else
       qq(k) = 0.0
       nn(k) = 0.0
     endif
   enddo

  return
end subroutine adjust_number_concent




subroutine find_cloud_top(im,km,ktop,qq,hvalue,ktopout)

   implicit none

   integer               , intent(in   ) :: im, km, ktop
   real, dimension(km)   , intent(in   ) :: qq
   real                  , intent(in   ) :: hvalue
   integer               , intent(inout) :: ktopout
   integer                               :: i,k


    ktopout = 0
    find_qrtop : do k = ktop,1, -1
      if(qq(k)>hvalue) then
        ktopout = k
        exit find_qrtop
      else
        cycle find_qrtop
      endif
    enddo find_qrtop

  return
end subroutine find_cloud_top




subroutine udm_mp_effective_radius (t, qc, qi, qs, rho, qmin, t0c,             &
                                nc,                                            &
                                re_qc, re_qi, re_qs, kts, kte, ii, jj)








   implicit none


   integer, intent(in) :: kts, kte, ii, jj
   real, intent(in) :: qmin
   real, intent(in) :: t0c
   real, dimension( kts:kte ), intent(in)::  t
   real, dimension( kts:kte ), intent(in)::  qc
   real, dimension( kts:kte ), intent(in)::  nc
   real, dimension( kts:kte ), intent(in)::  qi
   real, dimension( kts:kte ), intent(in)::  qs
   real, dimension( kts:kte ), intent(in)::  rho
   real, dimension( kts:kte ), intent(inout):: re_qc
   real, dimension( kts:kte ), intent(inout):: re_qi
   real, dimension( kts:kte ), intent(inout):: re_qs

   integer:: i,k
   integer :: nu_c
   real, dimension( kts:kte ):: ni
   real, dimension( kts:kte ):: rqc
   real, dimension( kts:kte ):: rnc
   real, dimension( kts:kte ):: rqi
   real, dimension( kts:kte ):: rni
   real, dimension( kts:kte ):: rqs
   real :: temp
   real :: lamdac
   real :: supcol, n0sfac, lamdas
   real :: di      
   real :: bfactor, bfactor2, bfactor3
   double precision :: lamc
   logical :: has_qc, has_qi, has_qs

   real, parameter :: r1 = 1.e-12
   real, parameter :: r2 = 1.e-6

   real, parameter :: bm_r = 3.0
   real, parameter :: obmr = 1.0/bm_r
   real, parameter :: nc0  = 3.e8
   real, parameter :: rqi0  = 50.e-3   

   has_qc = .false.
   has_qi = .false.
   has_qs = .false.
   do k = kts, kte

     rqc(k) = max(r1, qc(k)*rho(k))
     rnc(k) = max(R2, nc(k)*rho(k))
     if (rqc(k)>R1 .and. rnc(k)>R2) has_qc = .true.
     if (rqc(k)>r1) has_qc = .true.

     rqi(k) = max(r1, qi(k)*rho(k))
     temp = (rho(k)*max(qi(k),qmin))
     temp = sqrt(sqrt(temp*temp*temp))
     ni(k) = min(max(5.38e7*temp,1.e3),1.e6)
     rni(k)= max(r2, ni(k)*rho(k))
     if (rqi(k)>r1 .and. rni(k)>r2) has_qi = .true.

     rqs(k) = max(r1, qs(k)*rho(k))
     if (rqs(k)>r1) has_qs = .true.
   enddo
   if (has_qc) then
     do k=kts,kte
       if (rqc(k)<=R1 .or. rnc(k)<=R2) CYCLE
       lamc = (pidnc*nc(k)/rqc(k))**obmr
       re_qc(k) = max(recmin,min(0.5*(1./lamc),recmax))
     enddo
   endif
  if (has_qi) then
     do k=kts,kte
       if (rqi(k)<=r1 .or. rni(k)<=r2) cycle
       temp = t0c - t(k)
       bfactor = -2.0 + 1.0e-3*temp*sqrt(temp)*log10(rqi(k)/rqi0)
       bfactor2 = bfactor*bfactor
       bfactor3 = bfactor2*bfactor
       temp = 377.4 + 203.3*bfactor+ 37.91*bfactor2 + 2.3696*bfactor3
       re_qi(k) = max(reimin,min(temp*1.e-6,reimax))
     enddo
   endif
   if (has_qs) then
     do k=kts,kte
       if (rqs(k)<=r1) cycle
       supcol = t0c-t(k)
       n0sfac = max(min(exp(alpha*supcol),n0smax/n0s),1.)
       lamdas = sqrt(sqrt(pidn0s*n0sfac/rqs(k)))
       re_qs(k) = max(resmin,min(0.5*(1./lamdas), resmax))
     enddo
   endif
end subroutine udm_mp_effective_radius




subroutine udm_mp_reflectivity (qv1d, qr1d, qs1d, qg1d,                        &
                          nr1d,                                                &
                          qh1d,                                                &
            t1d, p1d, dbz, kts, kte, ii, jj)

   implicit none


   integer, intent(in):: kts, kte, ii, jj
   real, dimension(kts:kte), intent(in):: t1d, p1d
   real, dimension(kts:kte), intent(in):: qv1d, qr1d, qs1d, qg1d
   real, dimension(kts:kte), intent(in):: qh1d
   real, dimension(kts:kte), intent(in):: nr1d
   real, dimension(kts:kte), intent(inout):: dbz

   real, dimension(kts:kte):: temp, pres, qv, rho
   real, dimension(kts:kte):: rr, nr, rs, rg, rh
   real:: temp_c

   double precision, dimension(kts:kte):: ilamr, ilams, ilamg, ilamh
   double precision, dimension(kts:kte):: n0_r, n0_s, n0_g, n0_h
   double precision:: lamr, lams, lamg, lamh
   logical, dimension(kts:kte):: l_qr, l_qs, l_qg, l_qh

   real, dimension(kts:kte):: ze_rain, ze_snow, ze_graupel
   double precision:: fmelt_s, fmelt_g
   real, dimension(kts:kte):: ze_hail
   double precision:: fmelt_h

   integer:: i, k, k_0, kbot, n
   logical:: melti

   double precision:: cback, x, eta, f_d
   real, parameter:: r=287.



      temp(:) = 0.  ; pres(:) = 0.  ; qv(:) = 0.   ; rho(:) = 0.
      rr(:) = 0. ;  nr(:) = 0. ;  rs(:) = 0. ;  rg(:) = 0. ;  rh(:) = 0.
      temp_c = 0.

      ilamr(:) = 0. ; ilams(:) = 0. ; ilamg(:) = 0. ; ilamh(:) = 0.
      n0_r(:) = 0.;  n0_s(:) = 0. ; n0_g(:) = 0. ; n0_h(:) = 0.
      lamr = 0. ;  lams = 0. ;  lamg = 0. ;  lamh = 0.
      l_qr = .false. ;  l_qs = .false. ;  l_qg = .false. ;  l_qh = .false.

      ze_rain(:) = 0. ;  ze_snow(:) = 0. ;  ze_graupel(:) = 0.
      fmelt_s = 0. ; fmelt_g = 0. 
      ze_hail(:) = 0. ; fmelt_h = 0.

      melti = .false.

      cback = 0. ; x = 0. ; x = 0. ; eta = 0. ; f_d = 0.

   do k = kts, kte
     dbz(k) = -35.0
   enddo




   do k = kts, kte
     temp(k) = t1d(k)
     temp_c = min(-0.001, temp(k)-273.15)
     qv(k) = max(1.e-10, qv1d(k))
     pres(k) = p1d(k)
     rho(k) = 0.622*pres(k)/(r*temp(k)*(qv(k)+0.622))

     if (qr1d(k) > 1.e-9) then
       rr(k) = qr1d(k)*rho(k)
       nr(k) = nr1d(k)*rho(k)
       lamr = (xam_r*xcrg(3)*xorg2*nr(k)/rr(k))**xobmr
       N0_r(k) = nr(k)*xorg2*lamr**xcre(2)
       ilamr(k) = 1./lamr
       l_qr(k) = .true.
     else
       rr(k) = 1.e-12
       nr(k) = 1.e-12
       l_qr(k) = .false.
     endif

     if (qs1d(k) > 1.e-9) then
       rs(k) = qs1d(k)*rho(k)
       n0_s(k) = min(n0smax, n0s*exp(-alpha*temp_c))
       lams = (xam_s*xcsg(3)*n0_s(k)/rs(k))**(1./xcse(1))
       ilams(k) = 1./lams
       l_qs(k) = .true.
     else
       rs(k) = 1.e-12
       l_qs(k) = .false.
     endif

     if (qg1d(k) > 1.e-9) then
       rg(k) = qg1d(k)*rho(k)
       n0_g(k) = n0g
       lamg = (xam_g*xcgg(3)*n0_g(k)/rg(k))**(1./xcge(1))
       ilamg(k) = 1./lamg
       l_qg(k) = .true.
     else
       rg(k) = 1.e-12
       l_qg(k) = .false.
     endif
     if (qh1d(k) > 1.e-9) then
       rh(k) = qh1d(k)*rho(k)
       n0_h(k) = n0h
       lamh = (xam_h*xchg(3)*n0_h(k)/rh(k))**(1./xche(1))
       ilamh(k) = 1./lamh
       l_qh(k) = .true.
     else
       rh(k) = 1.e-12
       l_qh(k) = .false.
     endif
   enddo




   melti = .false.
   k_0 = kts
   do k = kte-1, kts, -1
      if ( (temp(k)>273.15) .and. L_qr(k)                         &
                     .and. (L_qs(k+1).or.L_qg(k+1).or.l_qh(k+1)) ) then
         k_0 = max(k+1, k_0)
         melti=.true.
         goto 195
      endif
   enddo
 195  continue





   do k = kts, kte
      ze_rain(k) = 1.e-22
      ze_snow(k) = 1.e-22
      if (l_qr(k)) ze_rain(k) = n0_r(k)*xcrg(4)*ilamr(k)**xcre(4)
      if (l_qs(k)) ze_snow(k) = (0.176/0.93) * (6.0/pi)*(6.0/pi)     &
                              * (xam_s/900.0)*(xam_s/900.0)          &
                              * n0_s(k)*xcsg(4)*ilams(k)**xcse(4)
      ze_graupel(k) = 1.e-22
      if (L_qg(k)) ze_graupel(k) = (0.176/0.93) * (6.0/PI)*(6.0/PI)  &
     &                           * (xam_g/900.0)*(xam_g/900.0)       &
     &                           * N0_g(k)*xcgg(4)*ilamg(k)**xcge(4)
      ze_hail(k) = 1.e-22
      if (L_qh(k)) ze_hail(k) = (0.176/0.93) * (6.0/PI)*(6.0/PI)     &
     &                           * (xam_h/900.0)*(xam_h/900.0)       &
     &                           * N0_h(k)*xchg(4)*ilamh(k)**xche(4)
  enddo







   if (melti .and. k_0>=kts+1) then
     do k = k_0-1, kts, -1

       if (l_qs(k) .and. l_qs(k_0) ) then
        fmelt_s = max(0.005d0, min(1.0d0-rs(k)/rs(k_0), 0.99d0))
        eta = 0.d0
        lams = 1./ilams(k)
        do n = 1, nrbins
           x = xam_s * xxds(n)**xbm_s
           call rayleigh_soak_wetgraupel (x,dble(xocms),dble(xobms), &
                 fmelt_s, melt_outside_s, m_w_0, m_i_0, lamda_radar, &
                 cback, mixingrulestring_s, matrixstring_s,          &
                 inclusionstring_s, hoststring_s,                    &
                 hostmatrixstring_s, hostinclusionstring_s)
           f_d = n0_s(k)*xxds(n)**xmu_s * dexp(-lams*xxds(n))
           eta = eta + f_d * cback * simpson(n) * xdts(n)
        enddo
        ze_snow(k) = sngl(lamda4 / (pi5 * k_w) * eta)
       endif

          if (L_qg(k) .and. L_qg(k_0) ) then
           fmelt_g = MAX(0.05d0, MIN(1.0d0-rg(k)/rg(k_0), 0.99d0))
           eta = 0.d0
           lamg = 1./ilamg(k)
           do n = 1, nrbins
              x = xam_g * xxDg(n)**xbm_g
              call rayleigh_soak_wetgraupel (x, DBLE(xocmg), DBLE(xobmg), &
     &              fmelt_g, melt_outside_g, m_w_0, m_i_0, lamda_radar, &
     &              CBACK, mixingrulestring_g, matrixstring_g,          &
     &              inclusionstring_g, hoststring_g,                    &
     &              hostmatrixstring_g, hostinclusionstring_g)
              f_d = N0_g(k)*xxDg(n)**xmu_g * DEXP(-lamg*xxDg(n))
              eta = eta + f_d * CBACK * simpson(n) * xdtg(n)
           enddo
           ze_graupel(k) = SNGL(lamda4 / (pi5 * K_w) * eta)
          endif

          if (L_qh(k) .and. L_qh(k_0) ) then
           fmelt_h = MAX(0.05d0, MIN(1.0d0-rh(k)/rh(k_0), 0.99d0))
           eta = 0.d0
           lamh = 1./ilamh(k)
           do n = 1, nrbins
              x = xam_h * xxDh(n)**xbm_h
              call rayleigh_soak_wetgraupel (x, DBLE(xocmh), DBLE(xobmh), &
     &              fmelt_h, melt_outside_h, m_w_0, m_i_0, lamda_radar, &
     &              CBACK, mixingrulestring_h, matrixstring_h,          &
     &              inclusionstring_h, hoststring_h,                    &
     &              hostmatrixstring_h, hostinclusionstring_h)
              f_d = N0_h(k)*xxDh(n)**xmu_h * DEXP(-lamh*xxDh(n))
              eta = eta + f_d * CBACK * simpson(n) * xdth(n)
           enddo
           ze_hail(k) = SNGL(lamda4 / (pi5 * K_w) * eta)
          endif
     enddo
   endif
   do k = kte, kts, -1
     dbz(k) = 10.*log10((ze_rain(k)+ze_snow(k)+ze_graupel(k)+ze_hail(k))*1.d18)
   enddo
end subroutine udm_mp_reflectivity




   subroutine udm_funct_shape_setup

















   implicit none

   real   , parameter  ::  xmin = 10.e-6, xmax = 10.e-3
   integer             ::  jx
   real                ::  xinc,x,d

   xinc = (xmax-xmin)/(nxshape-1)
   c1xshape = 1.-xmin/xinc
   c2xshape = 1./xinc

   do jx = 1,nxshape
     x = xmin+(jx-1)*xinc
     d = x
     tbshape(jx) = fshapex(d)
   enddo

   return
   end subroutine udm_funct_shape_setup

   real function fshapex(d)




















   implicit none



   real   , intent(in   )  ::  d



   real                    ::  shape_rain

     shape_rain =  min(11.8*(1000.*d-0.7)**2 + 2.,10.)
     fshapex = max(rgmma(4.+shape_rain+bvtr)*rgmma(1.+shape_rain)              &
             /rgmma(1.+shape_rain+bvtr)/rgmma(4.+shape_rain),1.)

   return
   end function fshapex

   real function fshape(d)
















   implicit none

   real   , intent(in   )            ::  d

   integer  ::  jx
   real     ::  xj

   xj = min(max(c1xshape+c2xshape*d,1.),real(nxshape))
   jx = min(xj,nxshape-1.)
   fshape = tbshape(jx)+(xj-jx)*(tbshape(jx+1)-tbshape(jx))

   return
   end function fshape




   subroutine udm_funct_lb2017_setup


















   implicit none

   real   , parameter  ::  xmin = 1.e+4, xmax = 1.e+7
   integer             ::  jx, nu
   real                ::  xinc,x,d

   double precision    ::  funct_aut_qc_totx,funct_aut_qc_subx
   double precision    ::  funct_aut_nc_totx,funct_aut_nc_subx

   funct_aut_qc_totx = 0. ; funct_aut_qc_subx = 0.
   funct_aut_nc_totx = 0. ; funct_aut_nc_subx = 0.
   tbaut_qc_tot(:,:) = 0. ; tbaut_qc_sub(:,:) = 0.
   tbaut_nc_tot(:,:) = 0. ; tbaut_nc_sub(:,:) = 0.

   xinc = (xmax-xmin)/(nxaut-1)
   c1xaut = 1.-xmin/xinc
   c2xaut = 1./xinc

   do nu = 1,numax
   do jx = 1,nxaut
     x = xmin+(jx-1)*xinc
     d = x
     call comp_aut_table(d,nu,funct_aut_qc_totx,funct_aut_qc_subx,             &
                           funct_aut_nc_totx,funct_aut_nc_subx)
     tbaut_qc_tot(jx,nu) = funct_aut_qc_totx
     tbaut_qc_sub(jx,nu) = funct_aut_qc_subx
     tbaut_nc_tot(jx,nu) = funct_aut_nc_totx
     tbaut_nc_sub(jx,nu) = funct_aut_nc_subx
   enddo
   enddo

   return
   end subroutine udm_funct_lb2017_setup

   subroutine comp_aut_table(lamc,nu_c,qc_tot,qc_sub,nc_tot,nc_sub)
















   implicit none



   real   , intent(in   )    ::  lamc
   double precision, intent(inout  )   ::  qc_tot,qc_sub,nc_tot,nc_sub
   double precision, parameter         :: &
                  r_star    = 50.0d-6,    &  
                  a_aut     = 0.21421d0,  &
                  b_aut     = -1.11347d4
   integer,parameter           :: n_gamma = 42
   double precision, dimension(0:n_gamma) :: gamma1, gamma2,       &
                          exp1, exp2, exp3, err1, err2
   double precision :: term, temp11, temp12, temp21, temp22
   double precision, dimension(10), parameter :: ak1 =             &
  (/a_aut, 1+a_aut, 1-2*a_aut, -2+a_aut, -1+2*a_aut,               &
         2-a_aut, -1-2*a_aut, -2+a_aut, 1+a_aut, 1.0d0       /)
   double precision, dimension( 7), parameter :: ak2 =             &
  (/a_aut, 1+a_aut, 1-2*a_aut, -2-2*a_aut, -2+a_aut, 1+a_aut,1.0d0/)
   integer :: n, nn, nu_c

   gamma1(:) = 0. ; gamma2(:) = 0. 
   exp1(:) = 0.   ; exp2(:) = 0. ; exp3(:) = 0. ; err1(:) = 0.; err2(:) = 0.
   term = 0.      ; temp11 = 0.0 ; temp12 = 0.  ; temp21 = 0. ; temp22 = 0.

         gamma1(0) = 1.0d0/(lamc)
         gamma2(0) = 1.0d0/(2.0d0*lamc)
         exp1(0) = 1.0d0
         err1(0) = 1.0d0
         exp2(0) = 1.0d0
         err2(0) = 1.0d0

         do n = 1, n_gamma
           gamma1(n) = gamma1(n-1)*(dble(n)/(lamc))
           gamma2(n) = gamma2(n-1)*(dble(n)/(2.0d0*lamc))
           exp1(n) = exp1(n-1) *(( lamc*r_star)/dble(n))
           exp2(n) = exp2(n-1) *((2.0d0*lamc*r_star)/dble(n))
         end do

         exp3 = 1.0d0/(lamc*gamma1)

         do n = 1, n_gamma
           err1(n) = 0.0d0
           err2(n) = 0.0d0
           do nn = 0, n
             err1(n) = err1(n) + exp1(nn)
             err2(n) = err2(n) + exp2(nn)
           end do
           err1(n) = 1.0d0 - exp(- lamc*r_star)*err1(n)
           err2(n) = 1.0d0 - exp(-2.0d0*lamc*r_star)*err2(n)
         end do



         qc_tot = 0.0d0
         qc_sub = 0.0d0

         do n = 1, 10
           temp11 = 0.0d0
           temp12 = 0.0d0
           do nn = 0, nu_c+n
             term = exp3(nn)*gamma2(nu_c+10-n+nn)
             temp11 = temp11 + term
             temp12 = temp12 + term*err2(nu_c+10-n+nn)
           end do
           temp21 = 0.0d0
           temp22 = 0.0d0
           do nn = 0, nu_c+n
             term = exp3(nn)*gamma2(nu_c+11-n+nn)
             temp21 = temp21 + term
             temp22 = temp22 + term*err2(nu_c+11-n+nn)
           end do

           qc_tot = qc_tot + ak1(n)*gamma1(nu_c+n)*(gamma1(nu_c+10-n)          &
                           - temp11 + b_aut*(gamma1(nu_c+11-n) -temp21))
           qc_sub = qc_sub + ak1(n)*gamma1(nu_c+n)*(gamma1(nu_c+10-n)          &
                           *err1(nu_c+10-n)-temp12 +b_aut*(gamma1(nu_c+11-n)   &
                           *err1(nu_c+11-n)-temp22))
         end do



         nc_tot = 0.0d0
         nc_sub = 0.0d0

         do n = 1, 7
           temp11 = 0.0d0
           temp12 = 0.0d0
           do nn=0, nu_c+n
             term = exp3(nn)*gamma2(nu_c+7-n+nn)
             temp11 = temp11 + term
             temp12 = temp12 + term*err2(nu_c+7-n+nn)
           end do
           temp21 = 0.0d0
           temp22 = 0.0d0
           do nn = 0, nu_c+n
             term = exp3(nn)*gamma2(nu_c+8-n+nn)
             temp21 = temp21 + term
             temp22 = temp22 + term*err2(nu_c+8-n+nn)
           end do

           nc_tot = nc_tot + ak2(n)*gamma1(nu_c+n)*(gamma1(nu_c+7-n)           &
                           - temp11 + b_aut*(gamma1(nu_c+8-n) - temp21))
           nc_sub = nc_sub + ak2(n)*gamma1(nu_c+n)*(gamma1(nu_c+7-n)           &
                           *err1(nu_c+7-n)-temp12 +b_aut*(gamma1(nu_c+8-n)     &
                           *err1(nu_c+8-n)-temp22))
         end do

   return
   end subroutine comp_aut_table

   double precision function funct_aut_qc_tot(d,nu)
















   implicit none

   double precision      , intent(in   )            ::  d
   integer   , intent(in   )            :: nu

   integer  ::  jx
   real     ::  xj

   xj = min(max(c1xaut+c2xaut*d,1.d0),real(nxaut))
   jx = min(xj,nxaut-1.d0)
   funct_aut_qc_tot = tbaut_qc_tot(jx,nu)+(xj-jx)*(tbaut_qc_tot(jx+1,nu)       &
                     -tbaut_qc_tot(jx,nu))

   return
   end function funct_aut_qc_tot

   double precision function funct_aut_qc_sub(d,nu)
















   implicit none

   double precision      , intent(in   )            ::  d
   integer   , intent(in   )            :: nu

   integer  ::  jx
   real     ::  xj

   xj = min(max(c1xaut+c2xaut*d,1.d0),real(nxaut))
   jx = min(xj,nxaut-1.d0)
   funct_aut_qc_sub = tbaut_qc_sub(jx,nu)+(xj-jx)*(tbaut_qc_sub(jx+1,nu)       &
                     -tbaut_qc_sub(jx,nu))

   return
   end function funct_aut_qc_sub

   double precision function funct_aut_nc_tot(d,nu)
















   implicit none

   double precision      , intent(in   )            ::  d
   integer   , intent(in   )            :: nu

   integer  ::  jx
   real     ::  xj

   xj = min(max(c1xaut+c2xaut*d,1.d0),real(nxaut))
   jx = min(xj,nxaut-1.d0)
   funct_aut_nc_tot = tbaut_nc_tot(jx,nu)+(xj-jx)*(tbaut_nc_tot(jx+1,nu)       &
                     -tbaut_nc_tot(jx,nu))

   return
   end function funct_aut_nc_tot

   double precision function funct_aut_nc_sub(d,nu)
















   implicit none

   double precision      , intent(in   )            ::  d
   integer   , intent(in   )            :: nu

   integer  ::  jx
   real     ::  xj

   xj = min(max(c1xaut+c2xaut*d,1.d0),real(nxaut))
   jx = min(xj,nxaut-1.d0)
   funct_aut_nc_sub = tbaut_nc_sub(jx,nu)+(xj-jx)*(tbaut_nc_sub(jx+1,nu)       &
                     -tbaut_nc_sub(jx,nu))

   return
   end function funct_aut_nc_sub




   subroutine udm_funct_svp_setup





















   implicit none

   real   , parameter  ::  xmin = 180.0,                                       &
                           xmax = 330.0
   integer             ::  jx
   real                ::  xinc,x,t

   xinc = (xmax-xmin)/(nxsvp-1)
   c1xsvp = 1.-xmin/xinc
   c2xsvp = 1./xinc
   c1xsvpw = c1xsvp
   c2xsvpw = c2xsvp

   do jx = 1,nxsvp
     x = xmin+(jx-1)*xinc
     t = x
     tbsvp(jx) = fsvpx(t)
     tbsvpw(jx) = fsvpxw(t)
   enddo

   return
   end subroutine udm_funct_svp_setup

   real function fsvpx(t)


















   implicit none



   real   , intent(in   )  ::  t



   real                    ::  tr

   tr = ttp_/t
   if (t >= ttp_) then
     fsvpx = psatk*(tr**xa)*exp(xb*(1.-tr))
   else
     fsvpx = psatk*(tr**xai)*exp(xbi*(1.-tr))
   endif

   return
   end function fsvpx

   real function fsvpxw(t)





   implicit none

   real   , intent(in   )  ::  t
   real                    ::  tr

   tr = ttp_/t
   fsvpxw = psatk*(tr**xa)*exp(xb*(1.-tr))

   return
   end function fsvpxw

   real function fsvp(t,p)
















   implicit none

   real   , intent(in   )            ::  t
   real   , intent(in   ), optional  ::  p

   integer  ::  jx
   real     ::  xj

   xj = min(max(c1xsvp+c2xsvp*t,1.),real(nxsvp))
   jx = min(xj,nxsvp-1.)
   fsvp = tbsvp(jx)+(xj-jx)*(tbsvp(jx+1)-tbsvp(jx))

   if (present(p)) fsvp = min(fsvp,0.99*p)

   return
   end function fsvp

   real function fsvp_water(t,p)





   implicit none

   real   , intent(in   )            ::  t
   real   , intent(in   ), optional  ::  p

   integer  ::  jx1
   real     ::  xj1

   xj1 = min(max(c1xsvpw+c2xsvpw*t,1.),real(nxsvpw))
   jx1 = min(xj1,nxsvpw-1.)
   fsvp_water = tbsvpw(jx1)+(xj1-jx1)*(tbsvpw(jx1+1)-tbsvpw(jx1))

   if (present(p)) fsvp_water = min(fsvp_water,0.99*p)

   return
   end function fsvp_water


end module module_mp_udm





