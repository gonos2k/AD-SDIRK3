






   SUBROUTINE interp_domain_em_part1 ( grid, intermediate_grid, ngrid, config_flags    &








,moist,moist_bxs,moist_bxe,moist_bys,moist_bye,moist_btxs,moist_btxe,moist_btys,moist_btye,dfi_moist,dfi_moist_bxs,dfi_moist_bxe, &
dfi_moist_bys,dfi_moist_bye,dfi_moist_btxs,dfi_moist_btxe,dfi_moist_btys,dfi_moist_btye,scalar,scalar_bxs,scalar_bxe,scalar_bys, &
scalar_bye,scalar_btxs,scalar_btxe,scalar_btys,scalar_btye,dfi_scalar,dfi_scalar_bxs,dfi_scalar_bxe,dfi_scalar_bys, &
dfi_scalar_bye,dfi_scalar_btxs,dfi_scalar_btxe,dfi_scalar_btys,dfi_scalar_btye,aerod,aerocu,ozmixm,aerosolc_1,aerosolc_2,fdda3d, &
fdda2d,advh_t,advz_t,tracer,tracer_bxs,tracer_bxe,tracer_bys,tracer_bye,tracer_btxs,tracer_btxe,tracer_btys,tracer_btye,pert3d, &
nba_mij,nba_rij,sbmradar,chem &



                 )
      USE module_state_description
      USE module_domain, ONLY : domain, get_ijk_from_grid
      USE module_configure, ONLY : grid_config_rec_type
      USE module_dm, ONLY : ntasks, ntasks_x, ntasks_y, itrace, local_communicator, &
                            nest_task_offsets, nest_pes_x, nest_pes_y, which_kid,   &
                            intercomm_active, mpi_comm_to_kid, mpi_comm_to_mom,     &
                            mytask, get_dm_max_halo_width
      USE module_timing
      IMPLICIT NONE

      TYPE(domain), POINTER :: grid          
      TYPE(domain), POINTER :: intermediate_grid
      TYPE(domain), POINTER :: ngrid







real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_moist)           :: moist
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_bxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_bxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_bys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_bye
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_btxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_btxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_btys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_moist)           :: moist_btye
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_dfi_moist)           :: dfi_moist
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_bxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_bxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_bys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_bye
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_btxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_btxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_btys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_moist)           :: dfi_moist_btye
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_scalar)           :: scalar
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_bxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_bxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_bys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_bye
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_btxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_btxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_btys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_scalar)           :: scalar_btye
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_dfi_scalar)           :: dfi_scalar
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_bxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_bxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_bys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_bye
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_btxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_btxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_btys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_dfi_scalar)           :: dfi_scalar_btye
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_aerod)           :: aerod
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_aerocu)           :: aerocu
real      ,DIMENSION(grid%sm31:grid%em31,1:grid%levsiz,grid%sm33:grid%em33,num_ozmixm)           :: ozmixm
real      ,DIMENSION(grid%sm31:grid%em31,1:grid%paerlev,grid%sm33:grid%em33,num_aerosolc)           :: aerosolc_1
real      ,DIMENSION(grid%sm31:grid%em31,1:grid%paerlev,grid%sm33:grid%em33,num_aerosolc)           :: aerosolc_2
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_fdda3d)           :: fdda3d
real      ,DIMENSION(grid%sm31:grid%em31,1:1,grid%sm33:grid%em33,num_fdda2d)           :: fdda2d
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_advh_t)           :: advh_t
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_advz_t)           :: advz_t
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_tracer)           :: tracer
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_bxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_bxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_bys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_bye
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_btxs
real      ,DIMENSION(grid%sm33:grid%em33,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_btxe
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_btys
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%spec_bdy_width,num_tracer)           :: tracer_btye
real      ,DIMENSION(grid%sm31:grid%em31,1:grid%num_stoch_levels,grid%sm33:grid%em33,num_pert3d)           :: pert3d
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_nba_mij)           :: nba_mij
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_nba_rij)           :: nba_rij
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_sbmradar)           :: sbmradar
real      ,DIMENSION(grid%sm31:grid%em31,grid%sm32:grid%em32,grid%sm33:grid%em33,num_chem)           :: chem



      INTEGER nlev, msize
      INTEGER i,j,pig,pjg,cm,cn,nig,njg,retval,k
      INTEGER iparstrt,jparstrt,sw
      TYPE (grid_config_rec_type)            :: config_flags
      REAL xv(2000)
      INTEGER       ::          cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe
      INTEGER       ::          iids, iide, ijds, ijde, ikds, ikde,    &
                                iims, iime, ijms, ijme, ikms, ikme,    &
                                iips, iipe, ijps, ijpe, ikps, ikpe
      INTEGER       ::          nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe

      INTEGER idim1,idim2,idim3,idim4,idim5,idim6,idim7

      INTEGER icoord, jcoord, idim_cd, jdim_cd, pgr
      INTEGER thisdomain_max_halo_width
      INTEGER local_comm, myproc, nproc
      INTEGER ioffset, ierr

      CALL wrf_get_dm_communicator ( local_comm )
      CALL wrf_get_myproc( myproc )
      CALL wrf_get_nproc( nproc )

      CALL get_ijk_from_grid (  grid ,                   &
                                cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe    )
      CALL get_ijk_from_grid (  intermediate_grid ,              &
                                iids, iide, ijds, ijde, ikds, ikde,    &
                                iims, iime, ijms, ijme, ikms, ikme,    &
                                iips, iipe, ijps, ijpe, ikps, ikpe    )
      CALL get_ijk_from_grid (  ngrid ,              &
                                nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe    )

      CALL nl_get_parent_grid_ratio ( ngrid%id, pgr )
      CALL nl_get_i_parent_start ( intermediate_grid%id, iparstrt )
      CALL nl_get_j_parent_start ( intermediate_grid%id, jparstrt )
      CALL nl_get_shw            ( intermediate_grid%id, sw )
      icoord =    iparstrt - sw
      jcoord =    jparstrt - sw
      idim_cd = iide - iids + 1
      jdim_cd = ijde - ijds + 1

      nlev  = ckde - ckds + 1

      
      CALL get_dm_max_halo_width ( grid%id , thisdomain_max_halo_width )

      IF ( grid%active_this_task ) THEN







msize = (228 + ((num_moist - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_dfi_moist - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_scalar - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_dfi_scalar - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_aerod - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_aerocu - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_ozmixm - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_aerosolc - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_fdda3d - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_fdda2d - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_advh_t - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_advz_t - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_tracer - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_pert3d - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_nba_mij - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_nba_rij - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_sbmradar - PARAM_FIRST_SCALAR + 1)) & 
 + ((num_chem - PARAM_FIRST_SCALAR + 1)) )* nlev + 458
CALL rsl_lite_to_child_info( msize*4                               &
                        ,cips,cipe,cjps,cjpe                               &
                        ,iids,iide,ijds,ijde                               &
                        ,nids,nide,njds,njde                               &
                        ,pgr , sw                                          &
                        ,nest_task_offsets(ngrid%id)                      &
                        ,nest_pes_x(grid%id)                            &
                        ,nest_pes_y(grid%id)                            &
                        ,nest_pes_x(intermediate_grid%id)                 &
                        ,nest_pes_y(intermediate_grid%id)                 &
                        ,thisdomain_max_halo_width                         &
                        ,icoord,jcoord                                     &
                        ,idim_cd,jdim_cd                                   &
                        ,pig,pjg,retval )
DO while ( retval .eq. 1 )
IF ( SIZE(grid%xlat) .GT. 1 ) THEN 
xv(1)=grid%xlat(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlong) .GT. 1 ) THEN 
xv(1)=grid%xlong(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lu_index) .GT. 1 ) THEN 
xv(1)=grid%lu_index(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%var_sso) .GT. 1 ) THEN 
xv(1)=grid%var_sso(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_max_p) .GT. 1 ) THEN 
xv(1)=grid%t_max_p(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ght_max_p) .GT. 1 ) THEN 
xv(1)=grid%ght_max_p(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%max_p) .GT. 1 ) THEN 
xv(1)=grid%max_p(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_min_p) .GT. 1 ) THEN 
xv(1)=grid%t_min_p(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ght_min_p) .GT. 1 ) THEN 
xv(1)=grid%ght_min_p(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%min_p) .GT. 1 ) THEN 
xv(1)=grid%min_p(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%erod) .GT. 1 ) THEN 
DO k = 1,config_flags%erosion_dim
xv(k)= grid%erod(pig,pjg,k)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%erosion_dim)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%u_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%u_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%v_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%v_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%w_2) .GT. 1 ) THEN 
DO k = ckds,ckde
xv(k)= grid%w_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((ckde)-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ph_2) .GT. 1 ) THEN 
DO k = ckds,ckde
xv(k)= grid%ph_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((ckde)-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phb) .GT. 1 ) THEN 
DO k = ckds,ckde
xv(k)= grid%phb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((ckde)-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%th_phy_m_t0) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%th_phy_m_t0(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%t_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_init) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%t_init(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%mu_2) .GT. 1 ) THEN 
xv(1)=grid%mu_2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%mub) .GT. 1 ) THEN 
xv(1)=grid%mub(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%alb) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%alb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%pb) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%pb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%q2) .GT. 1 ) THEN 
xv(1)=grid%q2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2) .GT. 1 ) THEN 
xv(1)=grid%t2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%th2) .GT. 1 ) THEN 
xv(1)=grid%th2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%psfc) .GT. 1 ) THEN 
xv(1)=grid%psfc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%u10) .GT. 1 ) THEN 
xv(1)=grid%u10(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%v10) .GT. 1 ) THEN 
xv(1)=grid%v10(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lpi) .GT. 1 ) THEN 
xv(1)=grid%lpi(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_moist
DO k = ckds,(ckde-1)
xv(k)= moist(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_moist
DO k = ckds,(ckde-1)
xv(k)= dfi_moist(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
IF ( SIZE(grid%qvold) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%qvold(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qnwfa2d) .GT. 1 ) THEN 
xv(1)=grid%qnwfa2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnifa2d) .GT. 1 ) THEN 
xv(1)=grid%qnifa2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnbca2d) .GT. 1 ) THEN 
xv(1)=grid%qnbca2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnocbb2d) .GT. 1 ) THEN 
xv(1)=grid%qnocbb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnbcbb2d) .GT. 1 ) THEN 
xv(1)=grid%qnbcbb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_scalar
DO k = ckds,(ckde-1)
xv(k)= scalar(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_scalar
DO k = ckds,(ckde-1)
xv(k)= dfi_scalar(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
IF ( SIZE(grid%toposlpx) .GT. 1 ) THEN 
xv(1)=grid%toposlpx(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%toposlpy) .GT. 1 ) THEN 
xv(1)=grid%toposlpy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%slope) .GT. 1 ) THEN 
xv(1)=grid%slope(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%slp_azi) .GT. 1 ) THEN 
xv(1)=grid%slp_azi(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%shdmax) .GT. 1 ) THEN 
xv(1)=grid%shdmax(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%shdmin) .GT. 1 ) THEN 
xv(1)=grid%shdmin(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%shdavg) .GT. 1 ) THEN 
xv(1)=grid%shdavg(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snoalb) .GT. 1 ) THEN 
xv(1)=grid%snoalb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%landusef) .GT. 1 ) THEN 
DO k = 1,config_flags%num_land_cat
xv(k)= grid%landusef(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%soilctop) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_cat
xv(k)= grid%soilctop(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%soilcbot) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_cat
xv(k)= grid%soilcbot(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%irrigation) .GT. 1 ) THEN 
xv(1)=grid%irrigation(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irr_rand_field) .GT. 1 ) THEN 
xv(1)=grid%irr_rand_field(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tslb) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%tslb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smois) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%smois(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sh2o) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%sh2o(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smcrel) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%smcrel(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%xice) .GT. 1 ) THEN 
xv(1)=grid%xice(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%icedepth) .GT. 1 ) THEN 
xv(1)=grid%icedepth(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xicem) .GT. 1 ) THEN 
xv(1)=grid%xicem(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%albsi) .GT. 1 ) THEN 
xv(1)=grid%albsi(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowsi) .GT. 1 ) THEN 
xv(1)=grid%snowsi(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%smstav) .GT. 1 ) THEN 
xv(1)=grid%smstav(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%sfcrunoff) .GT. 1 ) THEN 
xv(1)=grid%sfcrunoff(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%udrunoff) .GT. 1 ) THEN 
xv(1)=grid%udrunoff(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ivgtyp) .GT. 1 ) THEN 
xv(1)=grid%ivgtyp(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%isltyp) .GT. 1 ) THEN 
xv(1)=grid%isltyp(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%vegfra) .GT. 1 ) THEN 
xv(1)=grid%vegfra(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acgrdflx) .GT. 1 ) THEN 
xv(1)=grid%acgrdflx(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnow) .GT. 1 ) THEN 
xv(1)=grid%acsnow(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrunoff) .GT. 1 ) THEN 
xv(1)=grid%acrunoff(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnom) .GT. 1 ) THEN 
xv(1)=grid%acsnom(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snow) .GT. 1 ) THEN 
xv(1)=grid%snow(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowh) .GT. 1 ) THEN 
xv(1)=grid%snowh(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%canwat) .GT. 1 ) THEN 
xv(1)=grid%canwat(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%sstsk) .GT. 1 ) THEN 
xv(1)=grid%sstsk(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lake_depth) .GT. 1 ) THEN 
xv(1)=grid%lake_depth(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%water_depth) .GT. 1 ) THEN 
xv(1)=grid%water_depth(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%uoce) .GT. 1 ) THEN 
xv(1)=grid%uoce(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%voce) .GT. 1 ) THEN 
xv(1)=grid%voce(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsk_rural) .GT. 1 ) THEN 
xv(1)=grid%tsk_rural(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tr_urb2d) .GT. 1 ) THEN 
xv(1)=grid%tr_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgr_urb2d) .GT. 1 ) THEN 
xv(1)=grid%tgr_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tb_urb2d) .GT. 1 ) THEN 
xv(1)=grid%tb_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tg_urb2d) .GT. 1 ) THEN 
xv(1)=grid%tg_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tc_urb2d) .GT. 1 ) THEN 
xv(1)=grid%tc_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qc_urb2d) .GT. 1 ) THEN 
xv(1)=grid%qc_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%uc_urb2d) .GT. 1 ) THEN 
xv(1)=grid%uc_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxr_urb2d) .GT. 1 ) THEN 
xv(1)=grid%xxxr_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxb_urb2d) .GT. 1 ) THEN 
xv(1)=grid%xxxb_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxg_urb2d) .GT. 1 ) THEN 
xv(1)=grid%xxxg_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxc_urb2d) .GT. 1 ) THEN 
xv(1)=grid%xxxc_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%cmcr_urb2d) .GT. 1 ) THEN 
xv(1)=grid%cmcr_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%drelr_urb2d) .GT. 1 ) THEN 
xv(1)=grid%drelr_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%drelb_urb2d) .GT. 1 ) THEN 
xv(1)=grid%drelb_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%drelg_urb2d) .GT. 1 ) THEN 
xv(1)=grid%drelg_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%flxhumr_urb2d) .GT. 1 ) THEN 
xv(1)=grid%flxhumr_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%flxhumb_urb2d) .GT. 1 ) THEN 
xv(1)=grid%flxhumb_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%flxhumg_urb2d) .GT. 1 ) THEN 
xv(1)=grid%flxhumg_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgrl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%tgrl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smr_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%smr_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%trl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%trl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tbl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%tbl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tgl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%tgl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sh_urb2d) .GT. 1 ) THEN 
xv(1)=grid%sh_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lh_urb2d) .GT. 1 ) THEN 
xv(1)=grid%lh_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%g_urb2d) .GT. 1 ) THEN 
xv(1)=grid%g_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rn_urb2d) .GT. 1 ) THEN 
xv(1)=grid%rn_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ts_urb2d) .GT. 1 ) THEN 
xv(1)=grid%ts_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%frc_urb2d) .GT. 1 ) THEN 
xv(1)=grid%frc_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%utype_urb2d) .GT. 1 ) THEN 
xv(1)=grid%utype_urb2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%imperv) .GT. 1 ) THEN 
xv(1)=grid%imperv(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%canfra) .GT. 1 ) THEN 
xv(1)=grid%canfra(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%u10e) .GT. 1 ) THEN 
xv(1)=grid%u10e(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%v10e) .GT. 1 ) THEN 
xv(1)=grid%v10e(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%var2d) .GT. 1 ) THEN 
xv(1)=grid%var2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oc12d) .GT. 1 ) THEN 
xv(1)=grid%oc12d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa1) .GT. 1 ) THEN 
xv(1)=grid%oa1(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa2) .GT. 1 ) THEN 
xv(1)=grid%oa2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa3) .GT. 1 ) THEN 
xv(1)=grid%oa3(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa4) .GT. 1 ) THEN 
xv(1)=grid%oa4(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol1) .GT. 1 ) THEN 
xv(1)=grid%ol1(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol2) .GT. 1 ) THEN 
xv(1)=grid%ol2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol3) .GT. 1 ) THEN 
xv(1)=grid%ol3(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol4) .GT. 1 ) THEN 
xv(1)=grid%ol4(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%var2dls) .GT. 1 ) THEN 
xv(1)=grid%var2dls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oc12dls) .GT. 1 ) THEN 
xv(1)=grid%oc12dls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa1ls) .GT. 1 ) THEN 
xv(1)=grid%oa1ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa2ls) .GT. 1 ) THEN 
xv(1)=grid%oa2ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa3ls) .GT. 1 ) THEN 
xv(1)=grid%oa3ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa4ls) .GT. 1 ) THEN 
xv(1)=grid%oa4ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol1ls) .GT. 1 ) THEN 
xv(1)=grid%ol1ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol2ls) .GT. 1 ) THEN 
xv(1)=grid%ol2ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol3ls) .GT. 1 ) THEN 
xv(1)=grid%ol3ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol4ls) .GT. 1 ) THEN 
xv(1)=grid%ol4ls(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%var2dss) .GT. 1 ) THEN 
xv(1)=grid%var2dss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oc12dss) .GT. 1 ) THEN 
xv(1)=grid%oc12dss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa1ss) .GT. 1 ) THEN 
xv(1)=grid%oa1ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa2ss) .GT. 1 ) THEN 
xv(1)=grid%oa2ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa3ss) .GT. 1 ) THEN 
xv(1)=grid%oa3ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%oa4ss) .GT. 1 ) THEN 
xv(1)=grid%oa4ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol1ss) .GT. 1 ) THEN 
xv(1)=grid%ol1ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol2ss) .GT. 1 ) THEN 
xv(1)=grid%ol2ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol3ss) .GT. 1 ) THEN 
xv(1)=grid%ol3ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ol4ss) .GT. 1 ) THEN 
xv(1)=grid%ol4ss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ctopo) .GT. 1 ) THEN 
xv(1)=grid%ctopo(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ctopo2) .GT. 1 ) THEN 
xv(1)=grid%ctopo2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%o3rad) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%o3rad(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_aerod
DO k = ckds,(ckde-1)
xv(k)= aerod(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_aerocu
DO k = ckds,(ckde-1)
xv(k)= aerocu(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
IF ( SIZE(grid%f_ice_phy) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%f_ice_phy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%f_rain_phy) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%f_rain_phy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%f_rimef_phy) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%f_rimef_phy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_tmp) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= grid%om_tmp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_s) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= grid%om_s(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_depth) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= grid%om_depth(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_u) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= grid%om_u(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_v) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= grid%om_v(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_lat) .GT. 1 ) THEN 
xv(1)=grid%om_lat(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%om_lon) .GT. 1 ) THEN 
xv(1)=grid%om_lon(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%om_ml) .GT. 1 ) THEN 
xv(1)=grid%om_ml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%om_tini) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= grid%om_tini(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_sini) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= grid%om_sini(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h_diabatic) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%h_diabatic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qv_diabatic) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%qv_diabatic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qc_diabatic) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%qc_diabatic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%msft) .GT. 1 ) THEN 
xv(1)=grid%msft(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfu) .GT. 1 ) THEN 
xv(1)=grid%msfu(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfv) .GT. 1 ) THEN 
xv(1)=grid%msfv(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msftx) .GT. 1 ) THEN 
xv(1)=grid%msftx(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfty) .GT. 1 ) THEN 
xv(1)=grid%msfty(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfux) .GT. 1 ) THEN 
xv(1)=grid%msfux(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfuy) .GT. 1 ) THEN 
xv(1)=grid%msfuy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfvx) .GT. 1 ) THEN 
xv(1)=grid%msfvx(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfvx_inv) .GT. 1 ) THEN 
xv(1)=grid%msfvx_inv(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfvy) .GT. 1 ) THEN 
xv(1)=grid%msfvy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%f) .GT. 1 ) THEN 
xv(1)=grid%f(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%e) .GT. 1 ) THEN 
xv(1)=grid%e(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%sina) .GT. 1 ) THEN 
xv(1)=grid%sina(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%cosa) .GT. 1 ) THEN 
xv(1)=grid%cosa(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ht) .GT. 1 ) THEN 
xv(1)=grid%ht(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ht_shad) .GT. 1 ) THEN 
xv(1)=grid%ht_shad(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsk) .GT. 1 ) THEN 
xv(1)=grid%tsk(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainc) .GT. 1 ) THEN 
xv(1)=grid%rainc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainsh) .GT. 1 ) THEN 
xv(1)=grid%rainsh(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainnc) .GT. 1 ) THEN 
xv(1)=grid%rainnc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_rainc) .GT. 1 ) THEN 
xv(1)=grid%i_rainc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_rainnc) .GT. 1 ) THEN 
xv(1)=grid%i_rainnc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snownc) .GT. 1 ) THEN 
xv(1)=grid%snownc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%graupelnc) .GT. 1 ) THEN 
xv(1)=grid%graupelnc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%hailnc) .GT. 1 ) THEN 
xv(1)=grid%hailnc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%refl_10cm) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%refl_10cm(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%mskf_refl_10cm) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%mskf_refl_10cm(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%th_old) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%th_old(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qv_old) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%qv_old(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%vmi3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%vmi3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%di3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%di3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rhopo3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%rhopo3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phii3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%phii3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%vmi3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%vmi3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%di3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%di3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rhopo3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%rhopo3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phii3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%phii3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%vmi3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%vmi3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%di3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%di3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rhopo3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%rhopo3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phii3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%phii3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%itype) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%itype(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%itype_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%itype_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%itype_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%itype_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ssat) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%ssat(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ssati) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%ssati(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rthraten) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%rthraten(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%swdown) .GT. 1 ) THEN 
xv(1)=grid%swdown(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdown2) .GT. 1 ) THEN 
xv(1)=grid%swdown2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdownc2) .GT. 1 ) THEN 
xv(1)=grid%swdownc2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%gsw) .GT. 1 ) THEN 
xv(1)=grid%gsw(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%glw) .GT. 1 ) THEN 
xv(1)=grid%glw(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swnorm) .GT. 1 ) THEN 
xv(1)=grid%swnorm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%diffuse_frac) .GT. 1 ) THEN 
xv(1)=grid%diffuse_frac(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddir) .GT. 1 ) THEN 
xv(1)=grid%swddir(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddir2) .GT. 1 ) THEN 
xv(1)=grid%swddir2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddirc) .GT. 1 ) THEN 
xv(1)=grid%swddirc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddni) .GT. 1 ) THEN 
xv(1)=grid%swddni(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddni2) .GT. 1 ) THEN 
xv(1)=grid%swddni2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddnic) .GT. 1 ) THEN 
xv(1)=grid%swddnic(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddnic2) .GT. 1 ) THEN 
xv(1)=grid%swddnic2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddif) .GT. 1 ) THEN 
xv(1)=grid%swddif(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddif2) .GT. 1 ) THEN 
xv(1)=grid%swddif2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%gx) .GT. 1 ) THEN 
xv(1)=grid%gx(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%bx) .GT. 1 ) THEN 
xv(1)=grid%bx(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%gg) .GT. 1 ) THEN 
xv(1)=grid%gg(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%bb) .GT. 1 ) THEN 
xv(1)=grid%bb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%coszen_ref) .GT. 1 ) THEN 
xv(1)=grid%coszen_ref(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdown_ref) .GT. 1 ) THEN 
xv(1)=grid%swdown_ref(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swddir_ref) .GT. 1 ) THEN 
xv(1)=grid%swddir_ref(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswupt) .GT. 1 ) THEN 
xv(1)=grid%acswupt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswuptc) .GT. 1 ) THEN 
xv(1)=grid%acswuptc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnt) .GT. 1 ) THEN 
xv(1)=grid%acswdnt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdntc) .GT. 1 ) THEN 
xv(1)=grid%acswdntc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswupb) .GT. 1 ) THEN 
xv(1)=grid%acswupb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswupbc) .GT. 1 ) THEN 
xv(1)=grid%acswupbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnb) .GT. 1 ) THEN 
xv(1)=grid%acswdnb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnbc) .GT. 1 ) THEN 
xv(1)=grid%acswdnbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwupt) .GT. 1 ) THEN 
xv(1)=grid%aclwupt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwuptc) .GT. 1 ) THEN 
xv(1)=grid%aclwuptc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnt) .GT. 1 ) THEN 
xv(1)=grid%aclwdnt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdntc) .GT. 1 ) THEN 
xv(1)=grid%aclwdntc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwupb) .GT. 1 ) THEN 
xv(1)=grid%aclwupb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwupbc) .GT. 1 ) THEN 
xv(1)=grid%aclwupbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnb) .GT. 1 ) THEN 
xv(1)=grid%aclwdnb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnbc) .GT. 1 ) THEN 
xv(1)=grid%aclwdnbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswupt) .GT. 1 ) THEN 
xv(1)=grid%i_acswupt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswuptc) .GT. 1 ) THEN 
xv(1)=grid%i_acswuptc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdnt) .GT. 1 ) THEN 
xv(1)=grid%i_acswdnt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdntc) .GT. 1 ) THEN 
xv(1)=grid%i_acswdntc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswupb) .GT. 1 ) THEN 
xv(1)=grid%i_acswupb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswupbc) .GT. 1 ) THEN 
xv(1)=grid%i_acswupbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdnb) .GT. 1 ) THEN 
xv(1)=grid%i_acswdnb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdnbc) .GT. 1 ) THEN 
xv(1)=grid%i_acswdnbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwupt) .GT. 1 ) THEN 
xv(1)=grid%i_aclwupt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwuptc) .GT. 1 ) THEN 
xv(1)=grid%i_aclwuptc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdnt) .GT. 1 ) THEN 
xv(1)=grid%i_aclwdnt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdntc) .GT. 1 ) THEN 
xv(1)=grid%i_aclwdntc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwupb) .GT. 1 ) THEN 
xv(1)=grid%i_aclwupb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwupbc) .GT. 1 ) THEN 
xv(1)=grid%i_aclwupbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdnb) .GT. 1 ) THEN 
xv(1)=grid%i_aclwdnb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdnbc) .GT. 1 ) THEN 
xv(1)=grid%i_aclwdnbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupt) .GT. 1 ) THEN 
xv(1)=grid%swupt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swuptc) .GT. 1 ) THEN 
xv(1)=grid%swuptc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swuptcln) .GT. 1 ) THEN 
xv(1)=grid%swuptcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnt) .GT. 1 ) THEN 
xv(1)=grid%swdnt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdntc) .GT. 1 ) THEN 
xv(1)=grid%swdntc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdntcln) .GT. 1 ) THEN 
xv(1)=grid%swdntcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupb) .GT. 1 ) THEN 
xv(1)=grid%swupb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupbc) .GT. 1 ) THEN 
xv(1)=grid%swupbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupbcln) .GT. 1 ) THEN 
xv(1)=grid%swupbcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnb) .GT. 1 ) THEN 
xv(1)=grid%swdnb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnbc) .GT. 1 ) THEN 
xv(1)=grid%swdnbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnbcln) .GT. 1 ) THEN 
xv(1)=grid%swdnbcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupt) .GT. 1 ) THEN 
xv(1)=grid%lwupt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwuptc) .GT. 1 ) THEN 
xv(1)=grid%lwuptc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwuptcln) .GT. 1 ) THEN 
xv(1)=grid%lwuptcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnt) .GT. 1 ) THEN 
xv(1)=grid%lwdnt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdntc) .GT. 1 ) THEN 
xv(1)=grid%lwdntc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdntcln) .GT. 1 ) THEN 
xv(1)=grid%lwdntcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupb) .GT. 1 ) THEN 
xv(1)=grid%lwupb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupbc) .GT. 1 ) THEN 
xv(1)=grid%lwupbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupbcln) .GT. 1 ) THEN 
xv(1)=grid%lwupbcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnb) .GT. 1 ) THEN 
xv(1)=grid%lwdnb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnbc) .GT. 1 ) THEN 
xv(1)=grid%lwdnbc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnbcln) .GT. 1 ) THEN 
xv(1)=grid%lwdnbcln(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlat_u) .GT. 1 ) THEN 
xv(1)=grid%xlat_u(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlong_u) .GT. 1 ) THEN 
xv(1)=grid%xlong_u(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlat_v) .GT. 1 ) THEN 
xv(1)=grid%xlat_v(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlong_v) .GT. 1 ) THEN 
xv(1)=grid%xlong_v(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%clat) .GT. 1 ) THEN 
xv(1)=grid%clat(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsk_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%tsk_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qsfc_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%qsfc_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tslb_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= grid%tslb_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smois_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= grid%smois_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sh2o_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= grid%sh2o_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%canwat_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%canwat_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snow_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%snow_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowh_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%snowh_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowc_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%snowc_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tr_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%tr_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tb_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%tb_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tg_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%tg_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tc_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%tc_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ts_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%ts_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ts_rul2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%ts_rul2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qc_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%qc_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%uc_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= grid%uc_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%trl_urb3d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= grid%trl_urb3d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tbl_urb3d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= grid%tbl_urb3d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tgl_urb3d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= grid%tgl_urb3d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%mosaic_cat_index) .GT. 1 ) THEN 
DO k = 1,config_flags%num_land_cat
xv(k)= grid%mosaic_cat_index(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%landusef2) .GT. 1 ) THEN 
DO k = 1,config_flags%num_land_cat
xv(k)= grid%landusef2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tmn) .GT. 1 ) THEN 
xv(1)=grid%tmn(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tyr) .GT. 1 ) THEN 
xv(1)=grid%tyr(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tyra) .GT. 1 ) THEN 
xv(1)=grid%tyra(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tdly) .GT. 1 ) THEN 
xv(1)=grid%tdly(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tlag) .GT. 1 ) THEN 
DO k = 1,config_flags%lagday
xv(k)= grid%tlag(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%lagday)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%xland) .GT. 1 ) THEN 
xv(1)=grid%xland(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%achfx) .GT. 1 ) THEN 
xv(1)=grid%achfx(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclhf) .GT. 1 ) THEN 
xv(1)=grid%aclhf(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowc) .GT. 1 ) THEN 
xv(1)=grid%snowc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%prec_acc_c) .GT. 1 ) THEN 
xv(1)=grid%prec_acc_c(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%prec_acc_nc) .GT. 1 ) THEN 
xv(1)=grid%prec_acc_nc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snow_acc_nc) .GT. 1 ) THEN 
xv(1)=grid%snow_acc_nc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tml) .GT. 1 ) THEN 
xv(1)=grid%tml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t0ml) .GT. 1 ) THEN 
xv(1)=grid%t0ml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%hml) .GT. 1 ) THEN 
xv(1)=grid%hml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%h0ml) .GT. 1 ) THEN 
xv(1)=grid%h0ml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%huml) .GT. 1 ) THEN 
xv(1)=grid%huml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%hvml) .GT. 1 ) THEN 
xv(1)=grid%hvml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tmoml) .GT. 1 ) THEN 
xv(1)=grid%tmoml(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_tracer
DO k = ckds,(ckde-1)
xv(k)= tracer(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
IF ( SIZE(grid%vertstrucc) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%vertstrucc(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%vertstrucs) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= grid%vertstrucs(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%field_sf) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%field_sf(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%field_pbl) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%field_pbl(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%field_conv) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%field_conv(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ru_tendf_stoch) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%ru_tendf_stoch(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rv_tendf_stoch) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%rv_tendf_stoch(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rt_tendf_stoch) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%rt_tendf_stoch(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rand_pert) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%rand_pert(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%pattern_spp_conv) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%pattern_spp_conv(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%pattern_spp_pbl) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%pattern_spp_pbl(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%pattern_spp_lsm) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%pattern_spp_lsm(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rstoch) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%rstoch(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%numc) .GT. 1 ) THEN 
xv(1)=grid%numc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%nump) .GT. 1 ) THEN 
xv(1)=grid%nump(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snl) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%snl(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowdp) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%snowdp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%wtc) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%wtc(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%wtp) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%wtp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osno) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osno(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_grnd) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_grnd(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_veg) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_veg(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_veg24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_veg24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_veg240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_veg240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsun) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%fsun(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsun24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%fsun24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsun240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%fsun240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsd24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%fsd24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsd240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%fsd240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsi24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%fsi24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsi240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%fsi240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%laip) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%laip(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2ocan) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2ocan(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2ocan_col) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2ocan_col(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t2m_max) .GT. 1 ) THEN 
xv(1)=grid%t2m_max(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2m_min) .GT. 1 ) THEN 
xv(1)=grid%t2m_min(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2clm) .GT. 1 ) THEN 
xv(1)=grid%t2clm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_ref2m) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_ref2m(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%q_ref2m) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%q_ref2m(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq_s1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq_s2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq_s3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq_s4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq_s5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_liq10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice_s1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice_s2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice_s3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice_s4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice_s5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_ice10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno_s1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno_s2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno_s3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno_s4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno_s5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_soisno10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%dzsnow1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%dzsnow2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%dzsnow3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%dzsnow4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%dzsnow5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%snowrds1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%snowrds2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%snowrds3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%snowrds4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%snowrds5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%t_lake10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%h2osoi_vol10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%albedosubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%albedosubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lhsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%lhsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%hfxsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%hfxsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lwupsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%lwupsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%q2subgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%q2subgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sabvsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%sabvsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sabgsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%sabgsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%nrasubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%nrasubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%swupsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= grid%swupsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lakedepth2d) .GT. 1 ) THEN 
xv(1)=grid%lakedepth2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%savedtke12d) .GT. 1 ) THEN 
xv(1)=grid%savedtke12d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowdp2d) .GT. 1 ) THEN 
xv(1)=grid%snowdp2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%h2osno2d) .GT. 1 ) THEN 
xv(1)=grid%h2osno2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snl2d) .GT. 1 ) THEN 
xv(1)=grid%snl2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_grnd2d) .GT. 1 ) THEN 
xv(1)=grid%t_grnd2d(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_lake3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%t_lake3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lake_icefrac3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%lake_icefrac3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%z_lake3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%z_lake3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dz_lake3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%dz_lake3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= grid%t_soisno3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= grid%h2osoi_ice3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= grid%h2osoi_liq3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= grid%h2osoi_vol3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%z3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= grid%z3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dz3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= grid%dz3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%zi3d) .GT. 1 ) THEN 
DO k = 1,16
xv(k)= grid%zi3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((16)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%watsat3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%watsat3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%csol3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%csol3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tkmg3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%tkmg3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tkdry3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%tkdry3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tksatu3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= grid%tksatu3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%isnowxy) .GT. 1 ) THEN 
xv(1)=grid%isnowxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tvxy) .GT. 1 ) THEN 
xv(1)=grid%tvxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgxy) .GT. 1 ) THEN 
xv(1)=grid%tgxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%canicexy) .GT. 1 ) THEN 
xv(1)=grid%canicexy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%canliqxy) .GT. 1 ) THEN 
xv(1)=grid%canliqxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%eahxy) .GT. 1 ) THEN 
xv(1)=grid%eahxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tahxy) .GT. 1 ) THEN 
xv(1)=grid%tahxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%cmxy) .GT. 1 ) THEN 
xv(1)=grid%cmxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chxy) .GT. 1 ) THEN 
xv(1)=grid%chxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%fwetxy) .GT. 1 ) THEN 
xv(1)=grid%fwetxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%sneqvoxy) .GT. 1 ) THEN 
xv(1)=grid%sneqvoxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%alboldxy) .GT. 1 ) THEN 
xv(1)=grid%alboldxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnowxy) .GT. 1 ) THEN 
xv(1)=grid%qsnowxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qrainxy) .GT. 1 ) THEN 
xv(1)=grid%qrainxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%wslakexy) .GT. 1 ) THEN 
xv(1)=grid%wslakexy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%zwtxy) .GT. 1 ) THEN 
xv(1)=grid%zwtxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%waxy) .GT. 1 ) THEN 
xv(1)=grid%waxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%wtxy) .GT. 1 ) THEN 
xv(1)=grid%wtxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsnoxy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snow_layers
xv(k)= grid%tsnoxy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%zsnsoxy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snso_layers
xv(k)= grid%zsnsoxy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_snso_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snicexy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snow_layers
xv(k)= grid%snicexy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snliqxy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snow_layers
xv(k)= grid%snliqxy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lfmassxy) .GT. 1 ) THEN 
xv(1)=grid%lfmassxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rtmassxy) .GT. 1 ) THEN 
xv(1)=grid%rtmassxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%stmassxy) .GT. 1 ) THEN 
xv(1)=grid%stmassxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%woodxy) .GT. 1 ) THEN 
xv(1)=grid%woodxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%stblcpxy) .GT. 1 ) THEN 
xv(1)=grid%stblcpxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%fastcpxy) .GT. 1 ) THEN 
xv(1)=grid%fastcpxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%xsaixy) .GT. 1 ) THEN 
xv(1)=grid%xsaixy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2mvxy) .GT. 1 ) THEN 
xv(1)=grid%t2mvxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2mbxy) .GT. 1 ) THEN 
xv(1)=grid%t2mbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%q2mvxy) .GT. 1 ) THEN 
xv(1)=grid%q2mvxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%q2mbxy) .GT. 1 ) THEN 
xv(1)=grid%q2mbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tradxy) .GT. 1 ) THEN 
xv(1)=grid%tradxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%neexy) .GT. 1 ) THEN 
xv(1)=grid%neexy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%gppxy) .GT. 1 ) THEN 
xv(1)=grid%gppxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%nppxy) .GT. 1 ) THEN 
xv(1)=grid%nppxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%fvegxy) .GT. 1 ) THEN 
xv(1)=grid%fvegxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qinxy) .GT. 1 ) THEN 
xv(1)=grid%qinxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%runsfxy) .GT. 1 ) THEN 
xv(1)=grid%runsfxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%runsbxy) .GT. 1 ) THEN 
xv(1)=grid%runsbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ecanxy) .GT. 1 ) THEN 
xv(1)=grid%ecanxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%edirxy) .GT. 1 ) THEN 
xv(1)=grid%edirxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%etranxy) .GT. 1 ) THEN 
xv(1)=grid%etranxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%fsaxy) .GT. 1 ) THEN 
xv(1)=grid%fsaxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%firaxy) .GT. 1 ) THEN 
xv(1)=grid%firaxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aparxy) .GT. 1 ) THEN 
xv(1)=grid%aparxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%psnxy) .GT. 1 ) THEN 
xv(1)=grid%psnxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%savxy) .GT. 1 ) THEN 
xv(1)=grid%savxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%sagxy) .GT. 1 ) THEN 
xv(1)=grid%sagxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rssunxy) .GT. 1 ) THEN 
xv(1)=grid%rssunxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rsshaxy) .GT. 1 ) THEN 
xv(1)=grid%rsshaxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%bgapxy) .GT. 1 ) THEN 
xv(1)=grid%bgapxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%wgapxy) .GT. 1 ) THEN 
xv(1)=grid%wgapxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgvxy) .GT. 1 ) THEN 
xv(1)=grid%tgvxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgbxy) .GT. 1 ) THEN 
xv(1)=grid%tgbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chvxy) .GT. 1 ) THEN 
xv(1)=grid%chvxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chbxy) .GT. 1 ) THEN 
xv(1)=grid%chbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%shgxy) .GT. 1 ) THEN 
xv(1)=grid%shgxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%shcxy) .GT. 1 ) THEN 
xv(1)=grid%shcxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%shbxy) .GT. 1 ) THEN 
xv(1)=grid%shbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%evgxy) .GT. 1 ) THEN 
xv(1)=grid%evgxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%evbxy) .GT. 1 ) THEN 
xv(1)=grid%evbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ghvxy) .GT. 1 ) THEN 
xv(1)=grid%ghvxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ghbxy) .GT. 1 ) THEN 
xv(1)=grid%ghbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irgxy) .GT. 1 ) THEN 
xv(1)=grid%irgxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ircxy) .GT. 1 ) THEN 
xv(1)=grid%ircxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irbxy) .GT. 1 ) THEN 
xv(1)=grid%irbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%trxy) .GT. 1 ) THEN 
xv(1)=grid%trxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%evcxy) .GT. 1 ) THEN 
xv(1)=grid%evcxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chleafxy) .GT. 1 ) THEN 
xv(1)=grid%chleafxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chucxy) .GT. 1 ) THEN 
xv(1)=grid%chucxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chv2xy) .GT. 1 ) THEN 
xv(1)=grid%chv2xy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chb2xy) .GT. 1 ) THEN 
xv(1)=grid%chb2xy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%chstarxy) .GT. 1 ) THEN 
xv(1)=grid%chstarxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%fdepthxy) .GT. 1 ) THEN 
xv(1)=grid%fdepthxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%eqzwt) .GT. 1 ) THEN 
xv(1)=grid%eqzwt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rechclim) .GT. 1 ) THEN 
xv(1)=grid%rechclim(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%riverbedxy) .GT. 1 ) THEN 
xv(1)=grid%riverbedxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qintsxy) .GT. 1 ) THEN 
xv(1)=grid%qintsxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qintrxy) .GT. 1 ) THEN 
xv(1)=grid%qintrxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qdripsxy) .GT. 1 ) THEN 
xv(1)=grid%qdripsxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qdriprxy) .GT. 1 ) THEN 
xv(1)=grid%qdriprxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qthrosxy) .GT. 1 ) THEN 
xv(1)=grid%qthrosxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qthrorxy) .GT. 1 ) THEN 
xv(1)=grid%qthrorxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnsubxy) .GT. 1 ) THEN 
xv(1)=grid%qsnsubxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnfroxy) .GT. 1 ) THEN 
xv(1)=grid%qsnfroxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsubcxy) .GT. 1 ) THEN 
xv(1)=grid%qsubcxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qfrocxy) .GT. 1 ) THEN 
xv(1)=grid%qfrocxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qevacxy) .GT. 1 ) THEN 
xv(1)=grid%qevacxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qdewcxy) .GT. 1 ) THEN 
xv(1)=grid%qdewcxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qfrzcxy) .GT. 1 ) THEN 
xv(1)=grid%qfrzcxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qmeltcxy) .GT. 1 ) THEN 
xv(1)=grid%qmeltcxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnbotxy) .GT. 1 ) THEN 
xv(1)=grid%qsnbotxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qmeltxy) .GT. 1 ) THEN 
xv(1)=grid%qmeltxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%pondingxy) .GT. 1 ) THEN 
xv(1)=grid%pondingxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahxy) .GT. 1 ) THEN 
xv(1)=grid%pahxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahgxy) .GT. 1 ) THEN 
xv(1)=grid%pahgxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahvxy) .GT. 1 ) THEN 
xv(1)=grid%pahvxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahbxy) .GT. 1 ) THEN 
xv(1)=grid%pahbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%canhsxy) .GT. 1 ) THEN 
xv(1)=grid%canhsxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%fpicexy) .GT. 1 ) THEN 
xv(1)=grid%fpicexy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainlsm) .GT. 1 ) THEN 
xv(1)=grid%rainlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowlsm) .GT. 1 ) THEN 
xv(1)=grid%snowlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcomp) .GT. 1 ) THEN 
DO k = 1,8
xv(k)= grid%soilcomp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((8)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%soilcl1) .GT. 1 ) THEN 
xv(1)=grid%soilcl1(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcl2) .GT. 1 ) THEN 
xv(1)=grid%soilcl2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcl3) .GT. 1 ) THEN 
xv(1)=grid%soilcl3(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcl4) .GT. 1 ) THEN 
xv(1)=grid%soilcl4(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acints) .GT. 1 ) THEN 
xv(1)=grid%acints(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acintr) .GT. 1 ) THEN 
xv(1)=grid%acintr(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acdripr) .GT. 1 ) THEN 
xv(1)=grid%acdripr(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acthror) .GT. 1 ) THEN 
xv(1)=grid%acthror(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevac) .GT. 1 ) THEN 
xv(1)=grid%acevac(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acdewc) .GT. 1 ) THEN 
xv(1)=grid%acdewc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%forctlsm) .GT. 1 ) THEN 
xv(1)=grid%forctlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%forcqlsm) .GT. 1 ) THEN 
xv(1)=grid%forcqlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%forcplsm) .GT. 1 ) THEN 
xv(1)=grid%forcplsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%forczlsm) .GT. 1 ) THEN 
xv(1)=grid%forczlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%forcwlsm) .GT. 1 ) THEN 
xv(1)=grid%forcwlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrainlsm) .GT. 1 ) THEN 
xv(1)=grid%acrainlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrunsb) .GT. 1 ) THEN 
xv(1)=grid%acrunsb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrunsf) .GT. 1 ) THEN 
xv(1)=grid%acrunsf(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acecan) .GT. 1 ) THEN 
xv(1)=grid%acecan(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acetran) .GT. 1 ) THEN 
xv(1)=grid%acetran(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acedir) .GT. 1 ) THEN 
xv(1)=grid%acedir(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acetlsm) .GT. 1 ) THEN 
xv(1)=grid%acetlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnowlsm) .GT. 1 ) THEN 
xv(1)=grid%acsnowlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsubc) .GT. 1 ) THEN 
xv(1)=grid%acsubc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acfroc) .GT. 1 ) THEN 
xv(1)=grid%acfroc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acfrzc) .GT. 1 ) THEN 
xv(1)=grid%acfrzc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acmeltc) .GT. 1 ) THEN 
xv(1)=grid%acmeltc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnbot) .GT. 1 ) THEN 
xv(1)=grid%acsnbot(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnmelt) .GT. 1 ) THEN 
xv(1)=grid%acsnmelt(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acponding) .GT. 1 ) THEN 
xv(1)=grid%acponding(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnsub) .GT. 1 ) THEN 
xv(1)=grid%acsnsub(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnfro) .GT. 1 ) THEN 
xv(1)=grid%acsnfro(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrainsnow) .GT. 1 ) THEN 
xv(1)=grid%acrainsnow(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acdrips) .GT. 1 ) THEN 
xv(1)=grid%acdrips(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acthros) .GT. 1 ) THEN 
xv(1)=grid%acthros(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsagb) .GT. 1 ) THEN 
xv(1)=grid%acsagb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acirb) .GT. 1 ) THEN 
xv(1)=grid%acirb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshb) .GT. 1 ) THEN 
xv(1)=grid%acshb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevb) .GT. 1 ) THEN 
xv(1)=grid%acevb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acghb) .GT. 1 ) THEN 
xv(1)=grid%acghb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahb) .GT. 1 ) THEN 
xv(1)=grid%acpahb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsagv) .GT. 1 ) THEN 
xv(1)=grid%acsagv(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acirg) .GT. 1 ) THEN 
xv(1)=grid%acirg(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshg) .GT. 1 ) THEN 
xv(1)=grid%acshg(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevg) .GT. 1 ) THEN 
xv(1)=grid%acevg(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acghv) .GT. 1 ) THEN 
xv(1)=grid%acghv(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahg) .GT. 1 ) THEN 
xv(1)=grid%acpahg(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsav) .GT. 1 ) THEN 
xv(1)=grid%acsav(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acirc) .GT. 1 ) THEN 
xv(1)=grid%acirc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshc) .GT. 1 ) THEN 
xv(1)=grid%acshc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevc) .GT. 1 ) THEN 
xv(1)=grid%acevc(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%actr) .GT. 1 ) THEN 
xv(1)=grid%actr(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahv) .GT. 1 ) THEN 
xv(1)=grid%acpahv(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnlsm) .GT. 1 ) THEN 
xv(1)=grid%acswdnlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswuplsm) .GT. 1 ) THEN 
xv(1)=grid%acswuplsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnlsm) .GT. 1 ) THEN 
xv(1)=grid%aclwdnlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwuplsm) .GT. 1 ) THEN 
xv(1)=grid%aclwuplsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshflsm) .GT. 1 ) THEN 
xv(1)=grid%acshflsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclhflsm) .GT. 1 ) THEN 
xv(1)=grid%aclhflsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acghflsm) .GT. 1 ) THEN 
xv(1)=grid%acghflsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahlsm) .GT. 1 ) THEN 
xv(1)=grid%acpahlsm(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%accanhs) .GT. 1 ) THEN 
xv(1)=grid%accanhs(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilenergy) .GT. 1 ) THEN 
xv(1)=grid%soilenergy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowenergy) .GT. 1 ) THEN 
xv(1)=grid%snowenergy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_ssoil) .GT. 1 ) THEN 
xv(1)=grid%acc_ssoil(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_qinsur) .GT. 1 ) THEN 
xv(1)=grid%acc_qinsur(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_qseva) .GT. 1 ) THEN 
xv(1)=grid%acc_qseva(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_etrani) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= grid%acc_etrani(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%aceflxb) .GT. 1 ) THEN 
xv(1)=grid%aceflxb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%eflxbxy) .GT. 1 ) THEN 
xv(1)=grid%eflxbxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_dwaterxy) .GT. 1 ) THEN 
xv(1)=grid%acc_dwaterxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_prcpxy) .GT. 1 ) THEN 
xv(1)=grid%acc_prcpxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_ecanxy) .GT. 1 ) THEN 
xv(1)=grid%acc_ecanxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_etranxy) .GT. 1 ) THEN 
xv(1)=grid%acc_etranxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_edirxy) .GT. 1 ) THEN 
xv(1)=grid%acc_edirxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%grainxy) .GT. 1 ) THEN 
xv(1)=grid%grainxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%gddxy) .GT. 1 ) THEN 
xv(1)=grid%gddxy(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%croptype) .GT. 1 ) THEN 
DO k = 1,5
xv(k)= grid%croptype(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((5)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%planting) .GT. 1 ) THEN 
xv(1)=grid%planting(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%harvest) .GT. 1 ) THEN 
xv(1)=grid%harvest(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%season_gdd) .GT. 1 ) THEN 
xv(1)=grid%season_gdd(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%gecros_state) .GT. 1 ) THEN 
DO k = 1,60
xv(k)= grid%gecros_state(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((60)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%td_fraction) .GT. 1 ) THEN 
xv(1)=grid%td_fraction(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%qtdrain) .GT. 1 ) THEN 
xv(1)=grid%qtdrain(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irfract) .GT. 1 ) THEN 
xv(1)=grid%irfract(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%sifract) .GT. 1 ) THEN 
xv(1)=grid%sifract(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%mifract) .GT. 1 ) THEN 
xv(1)=grid%mifract(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%fifract) .GT. 1 ) THEN 
xv(1)=grid%fifract(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irnumsi) .GT. 1 ) THEN 
xv(1)=grid%irnumsi(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irnummi) .GT. 1 ) THEN 
xv(1)=grid%irnummi(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irnumfi) .GT. 1 ) THEN 
xv(1)=grid%irnumfi(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irsivol) .GT. 1 ) THEN 
xv(1)=grid%irsivol(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irmivol) .GT. 1 ) THEN 
xv(1)=grid%irmivol(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irfivol) .GT. 1 ) THEN 
xv(1)=grid%irfivol(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%ireloss) .GT. 1 ) THEN 
xv(1)=grid%ireloss(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%irrsplh) .GT. 1 ) THEN 
xv(1)=grid%irrsplh(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%field_u_tend_perturb) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%field_u_tend_perturb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%field_v_tend_perturb) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%field_v_tend_perturb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%field_t_tend_perturb) .GT. 1 ) THEN 
DO k = 1,config_flags%num_stoch_levels
xv(k)= grid%field_t_tend_perturb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_child_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%pcb) .GT. 1 ) THEN 
xv(1)=grid%pcb(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%pc_2) .GT. 1 ) THEN 
xv(1)=grid%pc_2(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%landmask) .GT. 1 ) THEN 
xv(1)=grid%landmask(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%lakemask) .GT. 1 ) THEN 
xv(1)=grid%lakemask(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
IF ( SIZE(grid%sst) .GT. 1 ) THEN 
xv(1)=grid%sst(pig,pjg)
CALL rsl_lite_to_child_msg(4,xv)
ENDIF
CALL rsl_lite_to_child_info( msize*4                               &
                        ,cips,cipe,cjps,cjpe                               &
                        ,iids,iide,ijds,ijde                               &
                        ,nids,nide,njds,njde                               &
                        ,pgr , sw                                          &
                        ,nest_task_offsets(ngrid%id)                      &
                        ,nest_pes_x(grid%id)                            &
                        ,nest_pes_y(grid%id)                            &
                        ,nest_pes_x(intermediate_grid%id)                 &
                        ,nest_pes_y(intermediate_grid%id)                 &
                        ,thisdomain_max_halo_width                         &
                        ,icoord,jcoord                                     &
                        ,idim_cd,jdim_cd                                   &
                        ,pig,pjg,retval )
ENDDO


      END IF

      
      IF ( intercomm_active( grid%id ) ) THEN        
        local_comm = mpi_comm_to_kid( which_kid(ngrid%id), grid%id )
        ioffset = nest_task_offsets(ngrid%id)
      ELSE IF ( intercomm_active( ngrid%id ) ) THEN  
        local_comm = mpi_comm_to_mom( ngrid%id )
        ioffset = nest_task_offsets(ngrid%id)
      END IF

      IF ( grid%active_this_task .OR. ngrid%active_this_task ) THEN

        CALL mpi_comm_rank(local_comm,myproc,ierr)
        CALL mpi_comm_size(local_comm,nproc,ierr)

        CALL rsl_lite_bcast_msgs( myproc, nest_pes_x(grid%id)*nest_pes_y(grid%id),         &
                                          nest_pes_x(ngrid%id)*nest_pes_y(ngrid%id),       &
                                          ioffset, local_comm )
      END IF

      RETURN
   END SUBROUTINE interp_domain_em_part1


