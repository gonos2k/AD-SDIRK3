



   SUBROUTINE feedback_domain_em_part2 ( grid, intermediate_grid, ngrid , config_flags    &








,moist,moist_bxs,moist_bxe,moist_bys,moist_bye,moist_btxs,moist_btxe,moist_btys,moist_btye,dfi_moist,dfi_moist_bxs,dfi_moist_bxe, &
dfi_moist_bys,dfi_moist_bye,dfi_moist_btxs,dfi_moist_btxe,dfi_moist_btys,dfi_moist_btye,scalar,scalar_bxs,scalar_bxe,scalar_bys, &
scalar_bye,scalar_btxs,scalar_btxe,scalar_btys,scalar_btye,dfi_scalar,dfi_scalar_bxs,dfi_scalar_bxe,dfi_scalar_bys, &
dfi_scalar_bye,dfi_scalar_btxs,dfi_scalar_btxe,dfi_scalar_btys,dfi_scalar_btye,aerod,aerocu,ozmixm,aerosolc_1,aerosolc_2,fdda3d, &
fdda2d,advh_t,advz_t,tracer,tracer_bxs,tracer_bxe,tracer_bys,tracer_bye,tracer_btxs,tracer_btxe,tracer_btys,tracer_btye,pert3d, &
nba_mij,nba_rij,sbmradar,chem &



                 )
      USE module_state_description
      USE module_domain, ONLY : domain, domain_clock_get, get_ijk_from_grid
      USE module_configure, ONLY : grid_config_rec_type, model_config_rec
      USE module_dm, ONLY : ntasks, ntasks_x, ntasks_y, itrace, local_communicator, mytask, &
                            ipe_save, jpe_save, ips_save, jps_save, get_dm_max_halo_width,  &
                            nest_pes_x, nest_pes_y,                                         &
                            intercomm_active, nest_task_offsets,                    &
                            mpi_comm_to_mom, mpi_comm_to_kid, which_kid 
                             

      USE module_comm_nesting_dm, ONLY : halo_interp_up_sub
      USE module_utility
      IMPLICIT NONE


      TYPE(domain), POINTER :: grid          
      TYPE(domain), POINTER :: intermediate_grid
      TYPE(domain), POINTER :: ngrid
      TYPE(domain), POINTER :: parent_grid








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
      TYPE (grid_config_rec_type)            :: config_flags
      REAL xv(2000)
      INTEGER       ::          cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe
      INTEGER       ::          nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe
      INTEGER       ::          xids, xide, xjds, xjde, xkds, xkde,    &
                                xims, xime, xjms, xjme, xkms, xkme,    &
                                xips, xipe, xjps, xjpe, xkps, xkpe
      INTEGER       ::          ids, ide, jds, jde, kds, kde,    &
                                ims, ime, jms, jme, kms, kme,    &
                                ips, ipe, jps, jpe, kps, kpe

      INTEGER idim1,idim2,idim3,idim4,idim5,idim6,idim7

      INTEGER icoord, jcoord, idim_cd, jdim_cd
      INTEGER local_comm, myproc, nproc, ioffset
      INTEGER iparstrt, jparstrt, sw, thisdomain_max_halo_width
      REAL    nest_influence

      character*256 :: timestr
      integer ierr

      LOGICAL, EXTERNAL  :: cd_feedback_mask


      integer tjk














      nest_influence = 1.

      CALL domain_clock_get( grid, current_timestr=timestr )

      CALL get_ijk_from_grid (  intermediate_grid ,                   &
                                cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe    )
      CALL get_ijk_from_grid (  grid ,              &
                                nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe    )
      CALL get_ijk_from_grid (  ngrid ,              &
                                xids, xide, xjds, xjde, xkds, xkde,    &
                                xims, xime, xjms, xjme, xkms, xkme,    &
                                xips, xipe, xjps, xjpe, xkps, xkpe    )

      ips_save = ngrid%i_parent_start   
      jps_save = ngrid%j_parent_start
      ipe_save = ngrid%i_parent_start + (xide-xids+1) / ngrid%parent_grid_ratio - 1
      jpe_save = ngrid%j_parent_start + (xjde-xjds+1) / ngrid%parent_grid_ratio - 1




IF ( ngrid%active_this_task ) THEN

    CALL push_communicators_for_domain( ngrid%id )

    do tjk = 1,config_flags%num_traj
     if (ngrid%traj_long(tjk) .eq. -9999.0) then

        ngrid%traj_long(tjk)=grid%traj_long(tjk)
        ngrid%traj_k(tjk)=grid%traj_k(tjk)
     else

        grid%traj_long(tjk)=ngrid%traj_long(tjk)
        grid%traj_k(tjk)=ngrid%traj_k(tjk)
     endif
     if (ngrid%traj_lat(tjk) .eq. -9999.0) then
         ngrid%traj_lat(tjk)=grid%traj_lat(tjk)
         ngrid%traj_k(tjk)=grid%traj_k(tjk)
     else
         grid%traj_lat(tjk)=ngrid%traj_lat(tjk)
         grid%traj_k(tjk)=ngrid%traj_k(tjk)
     endif
    enddo


      CALL nl_get_i_parent_start ( intermediate_grid%id, iparstrt )
      CALL nl_get_j_parent_start ( intermediate_grid%id, jparstrt )
      CALL nl_get_shw            ( intermediate_grid%id, sw )
      icoord =    iparstrt - sw
      jcoord =    jparstrt - sw
      idim_cd = cide - cids + 1
      jdim_cd = cjde - cjds + 1

      nlev  = ckde - ckds + 1

      CALL get_dm_max_halo_width ( grid%id , thisdomain_max_halo_width )

      parent_grid => grid
      grid => ngrid







msize = (207 + ((num_moist - PARAM_FIRST_SCALAR + 1)) & 
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
 + ((num_chem - PARAM_FIRST_SCALAR + 1)) )* nlev + 386
CALL rsl_lite_to_parent_info( msize*4                               &
                        ,cips,cipe,cjps,cjpe                               &
                        ,nids,nide,njds,njde                               &
                        ,nest_task_offsets(ngrid%id)                      &
                        ,nest_pes_x(parent_grid%id)                            &
                        ,nest_pes_y(parent_grid%id)                            &
                        ,nest_pes_x(intermediate_grid%id)                 &
                        ,nest_pes_y(intermediate_grid%id)                 &
                        ,thisdomain_max_halo_width                         &
                        ,icoord,jcoord                                     &
                        ,idim_cd,jdim_cd                                   &
                        ,pig,pjg,retval )
DO while ( retval .eq. 1 )
IF ( SIZE(grid%lu_index) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lu_index(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%u_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%u_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%v_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%v_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%w_2) .GT. 1 ) THEN 
DO k = ckds,ckde
xv(k)= intermediate_grid%w_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((ckde)-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ph_2) .GT. 1 ) THEN 
DO k = ckds,ckde
xv(k)= intermediate_grid%ph_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((ckde)-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phb) .GT. 1 ) THEN 
DO k = ckds,ckde
xv(k)= intermediate_grid%phb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((ckde)-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%t_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%mu_2) .GT. 1 ) THEN 
xv(1)= intermediate_grid%mu_2(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%mub) .GT. 1 ) THEN 
xv(1)= intermediate_grid%mub(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%nest_pos) .GT. 1 ) THEN 
xv(1)= intermediate_grid%nest_pos(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%nest_mask) .GT. 1 ) THEN 
xv(1)= intermediate_grid%nest_mask(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%alb) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%alb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%pb) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%pb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%q2) .GT. 1 ) THEN 
xv(1)= intermediate_grid%q2(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t2(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%th2) .GT. 1 ) THEN 
xv(1)= intermediate_grid%th2(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%psfc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%psfc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%u10) .GT. 1 ) THEN 
xv(1)= intermediate_grid%u10(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%v10) .GT. 1 ) THEN 
xv(1)= intermediate_grid%v10(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lpi) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lpi(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_moist
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%moist(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_moist
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%dfi_moist(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
IF ( SIZE(grid%qvold) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%qvold(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qnwfa2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qnwfa2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnifa2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qnifa2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnbca2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qnbca2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnocbb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qnocbb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qnbcbb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qnbcbb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_scalar
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%scalar(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_scalar
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%dfi_scalar(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
IF ( SIZE(grid%toposlpx) .GT. 1 ) THEN 
xv(1)= intermediate_grid%toposlpx(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%toposlpy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%toposlpy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%slope) .GT. 1 ) THEN 
xv(1)= intermediate_grid%slope(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%slp_azi) .GT. 1 ) THEN 
xv(1)= intermediate_grid%slp_azi(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%shdmax) .GT. 1 ) THEN 
xv(1)= intermediate_grid%shdmax(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%shdmin) .GT. 1 ) THEN 
xv(1)= intermediate_grid%shdmin(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%shdavg) .GT. 1 ) THEN 
xv(1)= intermediate_grid%shdavg(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%landusef) .GT. 1 ) THEN 
DO k = 1,config_flags%num_land_cat
xv(k)= intermediate_grid%landusef(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%soilctop) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_cat
xv(k)= intermediate_grid%soilctop(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%soilcbot) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_cat
xv(k)= intermediate_grid%soilcbot(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%irrigation) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irrigation(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irr_rand_field) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irr_rand_field(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tslb) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%tslb(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smois) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%smois(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sh2o) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%sh2o(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smcrel) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%smcrel(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%xice) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xice(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%icedepth) .GT. 1 ) THEN 
xv(1)= intermediate_grid%icedepth(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xicem) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xicem(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%albsi) .GT. 1 ) THEN 
xv(1)= intermediate_grid%albsi(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowsi) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snowsi(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ivgtyp) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ivgtyp(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%isltyp) .GT. 1 ) THEN 
xv(1)= intermediate_grid%isltyp(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%vegfra) .GT. 1 ) THEN 
xv(1)= intermediate_grid%vegfra(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acgrdflx) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acgrdflx(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnow) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsnow(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrunoff) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acrunoff(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnom) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsnom(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snow) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snow(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowh) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snowh(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%canwat) .GT. 1 ) THEN 
xv(1)= intermediate_grid%canwat(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%sstsk) .GT. 1 ) THEN 
xv(1)= intermediate_grid%sstsk(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsk_rural) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tsk_rural(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tr_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tr_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgr_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tgr_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tb_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tb_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tg_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tg_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tc_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tc_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qc_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qc_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%uc_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%uc_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxr_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xxxr_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxb_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xxxb_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxg_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xxxg_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xxxc_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xxxc_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%cmcr_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%cmcr_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%drelr_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%drelr_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%drelb_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%drelb_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%drelg_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%drelg_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%flxhumr_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%flxhumr_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%flxhumb_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%flxhumb_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%flxhumg_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%flxhumg_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgrl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%tgrl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smr_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%smr_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%trl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%trl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tbl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%tbl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tgl_urb3d) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%tgl_urb3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sh_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%sh_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lh_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lh_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%g_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%g_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rn_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rn_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ts_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ts_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%frc_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%frc_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%utype_urb2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%utype_urb2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%imperv) .GT. 1 ) THEN 
xv(1)= intermediate_grid%imperv(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%canfra) .GT. 1 ) THEN 
xv(1)= intermediate_grid%canfra(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%u10e) .GT. 1 ) THEN 
xv(1)= intermediate_grid%u10e(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%v10e) .GT. 1 ) THEN 
xv(1)= intermediate_grid%v10e(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ctopo) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ctopo(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ctopo2) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ctopo2(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%f_ice_phy) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%f_ice_phy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%f_rain_phy) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%f_rain_phy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%f_rimef_phy) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%f_rimef_phy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_tmp) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= intermediate_grid%om_tmp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_s) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= intermediate_grid%om_s(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_u) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= intermediate_grid%om_u(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_v) .GT. 1 ) THEN 
DO k = 1,config_flags%ocean_levels
xv(k)= intermediate_grid%om_v(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%om_ml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%om_ml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%h_diabatic) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%h_diabatic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qv_diabatic) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%qv_diabatic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qc_diabatic) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%qc_diabatic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%msft) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msft(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfu) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfu(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfv) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfv(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msftx) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msftx(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfty) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfty(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfux) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfux(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfuy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfuy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfvx) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfvx(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfvx_inv) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfvx_inv(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%msfvy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%msfvy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%f) .GT. 1 ) THEN 
xv(1)= intermediate_grid%f(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%e) .GT. 1 ) THEN 
xv(1)= intermediate_grid%e(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%sina) .GT. 1 ) THEN 
xv(1)= intermediate_grid%sina(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%cosa) .GT. 1 ) THEN 
xv(1)= intermediate_grid%cosa(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ht) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ht(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsk) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tsk(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rainc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainsh) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rainsh(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainnc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rainnc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_rainc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_rainc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_rainnc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_rainnc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snownc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snownc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%graupelnc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%graupelnc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%hailnc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%hailnc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%refl_10cm) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%refl_10cm(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%mskf_refl_10cm) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%mskf_refl_10cm(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%th_old) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%th_old(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qv_old) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%qv_old(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%vmi3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%vmi3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%di3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%di3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rhopo3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%rhopo3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phii3d) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%phii3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%vmi3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%vmi3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%di3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%di3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rhopo3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%rhopo3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phii3d_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%phii3d_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%vmi3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%vmi3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%di3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%di3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%rhopo3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%rhopo3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%phii3d_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%phii3d_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%itype) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%itype(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%itype_2) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%itype_2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%itype_3) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%itype_3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ssat) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%ssat(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ssati) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%ssati(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%acswupt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswupt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswuptc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswuptc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswdnt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdntc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswdntc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswupb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswupb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswupbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswupbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswdnb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswdnbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwupt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwupt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwuptc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwuptc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwdnt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdntc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwdntc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwupb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwupb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwupbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwupbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwdnb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwdnbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswupt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswupt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswuptc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswuptc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdnt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswdnt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdntc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswdntc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswupb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswupb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswupbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswupbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdnb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswdnb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_acswdnbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_acswdnbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwupt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwupt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwuptc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwuptc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdnt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwdnt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdntc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwdntc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwupb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwupb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwupbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwupbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdnb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwdnb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%i_aclwdnbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%i_aclwdnbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swupt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swuptc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swuptc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swuptcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swuptcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swdnt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdntc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swdntc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdntcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swdntcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swupb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swupbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swupbcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swupbcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swdnb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swdnbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%swdnbcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%swdnbcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwupt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwuptc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwuptc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwuptcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwuptcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwdnt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdntc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwdntc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdntcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwdntcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwupb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwupbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwupbcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwupbcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwdnb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnbc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwdnbc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lwdnbcln) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lwdnbcln(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlat_u) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xlat_u(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlong_u) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xlong_u(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlat_v) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xlat_v(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xlong_v) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xlong_v(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%clat) .GT. 1 ) THEN 
xv(1)= intermediate_grid%clat(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsk_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%tsk_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qsfc_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%qsfc_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tslb_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= intermediate_grid%tslb_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%smois_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= intermediate_grid%smois_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sh2o_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= intermediate_grid%sh2o_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%canwat_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%canwat_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snow_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%snow_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowh_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%snowh_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowc_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%snowc_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tr_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%tr_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tb_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%tb_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tg_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%tg_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tc_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%tc_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ts_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%ts_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%ts_rul2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%ts_rul2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%qc_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%qc_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%uc_urb2d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat
xv(k)= intermediate_grid%uc_urb2d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%trl_urb3d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= intermediate_grid%trl_urb3d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tbl_urb3d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= intermediate_grid%tbl_urb3d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tgl_urb3d_mosaic) .GT. 1 ) THEN 
DO k = 1,config_flags%mosaic_cat_soil
xv(k)= intermediate_grid%tgl_urb3d_mosaic(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%mosaic_cat_index) .GT. 1 ) THEN 
DO k = 1,config_flags%num_land_cat
xv(k)= intermediate_grid%mosaic_cat_index(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%landusef2) .GT. 1 ) THEN 
DO k = 1,config_flags%num_land_cat
xv(k)= intermediate_grid%landusef2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tmn) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tmn(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tyr) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tyr(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tyra) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tyra(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tdly) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tdly(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tlag) .GT. 1 ) THEN 
DO k = 1,config_flags%lagday
xv(k)= intermediate_grid%tlag(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%lagday)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%xland) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xland(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%achfx) .GT. 1 ) THEN 
xv(1)= intermediate_grid%achfx(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclhf) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclhf(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snowc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%prec_acc_c) .GT. 1 ) THEN 
xv(1)= intermediate_grid%prec_acc_c(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%prec_acc_nc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%prec_acc_nc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snow_acc_nc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snow_acc_nc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t0ml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t0ml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%hml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%hml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%h0ml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%h0ml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%huml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%huml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%hvml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%hvml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tmoml) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tmoml(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%k1_u) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%k1_u(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%k2_u) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%k2_u(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
IF ( SIZE(grid%k3_u) .GT. 1 ) THEN 
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%k3_u(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_tracer
DO k = ckds,(ckde-1)
xv(k)= intermediate_grid%tracer(pig,k,pjg,itrace)
ENDDO
CALL rsl_lite_to_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
ENDDO
IF ( SIZE(grid%numc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%numc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%nump) .GT. 1 ) THEN 
xv(1)= intermediate_grid%nump(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snl) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%snl(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowdp) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%snowdp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%wtc) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%wtc(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%wtp) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%wtp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osno) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osno(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_grnd) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_grnd(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_veg) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_veg(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_veg24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_veg24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_veg240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_veg240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsun) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%fsun(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsun24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%fsun24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsun240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%fsun240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsd24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%fsd24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsd240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%fsd240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsi24) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%fsi24(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%fsi240) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%fsi240(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%laip) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%laip(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2ocan) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2ocan(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2ocan_col) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2ocan_col(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t2m_max) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t2m_max(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2m_min) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t2m_min(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2clm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t2clm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_ref2m) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_ref2m(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%q_ref2m) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%q_ref2m(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq_s1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq_s2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq_s3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq_s4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq_s5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq_s5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_liq10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice_s1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice_s2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice_s3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice_s4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice_s5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice_s5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_ice10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno_s1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno_s2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno_s3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno_s4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno_s5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno_s5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_soisno10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%dzsnow1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%dzsnow2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%dzsnow3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%dzsnow4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dzsnow5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%dzsnow5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%snowrds1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%snowrds2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%snowrds3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%snowrds4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snowrds5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%snowrds5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_lake10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%t_lake10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol1) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol1(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol2) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol2(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol3) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol3(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol4) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol4(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol5) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol5(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol6) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol6(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol7) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol7(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol8) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol8(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol9) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol9(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol10) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%h2osoi_vol10(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%albedosubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%albedosubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lhsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%lhsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%hfxsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%hfxsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lwupsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%lwupsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%q2subgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%q2subgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sabvsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%sabvsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%sabgsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%sabgsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%nrasubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%nrasubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%swupsubgrid) .GT. 1 ) THEN 
DO k = 1,config_flags%maxpatch
xv(k)= intermediate_grid%swupsubgrid(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lakedepth2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lakedepth2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%savedtke12d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%savedtke12d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowdp2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snowdp2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%h2osno2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%h2osno2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snl2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snl2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_grnd2d) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t_grnd2d(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t_lake3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%t_lake3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lake_icefrac3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%lake_icefrac3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%z_lake3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%z_lake3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dz_lake3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%dz_lake3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%t_soisno3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= intermediate_grid%t_soisno3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_ice3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= intermediate_grid%h2osoi_ice3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_liq3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= intermediate_grid%h2osoi_liq3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%h2osoi_vol3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= intermediate_grid%h2osoi_vol3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%z3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= intermediate_grid%z3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%dz3d) .GT. 1 ) THEN 
DO k = 1,15
xv(k)= intermediate_grid%dz3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((15)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%zi3d) .GT. 1 ) THEN 
DO k = 1,16
xv(k)= intermediate_grid%zi3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((16)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%watsat3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%watsat3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%csol3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%csol3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tkmg3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%tkmg3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tkdry3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%tkdry3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%tksatu3d) .GT. 1 ) THEN 
DO k = 1,10
xv(k)= intermediate_grid%tksatu3d(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((10)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%isnowxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%isnowxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tvxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tvxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tgxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%canicexy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%canicexy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%canliqxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%canliqxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%eahxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%eahxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tahxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tahxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%cmxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%cmxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%fwetxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%fwetxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%sneqvoxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%sneqvoxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%alboldxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%alboldxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnowxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qsnowxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qrainxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qrainxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%wslakexy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%wslakexy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%zwtxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%zwtxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%waxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%waxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%wtxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%wtxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tsnoxy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snow_layers
xv(k)= intermediate_grid%tsnoxy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%zsnsoxy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snso_layers
xv(k)= intermediate_grid%zsnsoxy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_snso_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snicexy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snow_layers
xv(k)= intermediate_grid%snicexy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%snliqxy) .GT. 1 ) THEN 
DO k = 1,config_flags%num_snow_layers
xv(k)= intermediate_grid%snliqxy(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%lfmassxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lfmassxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rtmassxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rtmassxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%stmassxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%stmassxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%woodxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%woodxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%stblcpxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%stblcpxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%fastcpxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%fastcpxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%xsaixy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%xsaixy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2mvxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t2mvxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%t2mbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%t2mbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%q2mvxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%q2mvxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%q2mbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%q2mbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tradxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tradxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%neexy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%neexy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%gppxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%gppxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%nppxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%nppxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%fvegxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%fvegxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qinxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qinxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%runsfxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%runsfxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%runsbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%runsbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ecanxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ecanxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%edirxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%edirxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%etranxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%etranxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%fsaxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%fsaxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%firaxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%firaxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aparxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aparxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%psnxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%psnxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%savxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%savxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%sagxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%sagxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rssunxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rssunxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rsshaxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rsshaxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%bgapxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%bgapxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%wgapxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%wgapxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgvxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tgvxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%tgbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%tgbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chvxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chvxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%shgxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%shgxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%shcxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%shcxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%shbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%shbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%evgxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%evgxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%evbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%evbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ghvxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ghvxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ghbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ghbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irgxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irgxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ircxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ircxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%trxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%trxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%evcxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%evcxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chleafxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chleafxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chucxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chucxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chv2xy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chv2xy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chb2xy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chb2xy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%chstarxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%chstarxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%fdepthxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%fdepthxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%eqzwt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%eqzwt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rechclim) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rechclim(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%riverbedxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%riverbedxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qintsxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qintsxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qintrxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qintrxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qdripsxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qdripsxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qdriprxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qdriprxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qthrosxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qthrosxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qthrorxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qthrorxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnsubxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qsnsubxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnfroxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qsnfroxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsubcxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qsubcxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qfrocxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qfrocxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qevacxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qevacxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qdewcxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qdewcxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qfrzcxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qfrzcxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qmeltcxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qmeltcxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qsnbotxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qsnbotxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qmeltxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qmeltxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%pondingxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%pondingxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%pahxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahgxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%pahgxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahvxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%pahvxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%pahbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%pahbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%canhsxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%canhsxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%fpicexy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%fpicexy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%rainlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%rainlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snowlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcomp) .GT. 1 ) THEN 
DO k = 1,8
xv(k)= intermediate_grid%soilcomp(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((8)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%soilcl1) .GT. 1 ) THEN 
xv(1)= intermediate_grid%soilcl1(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcl2) .GT. 1 ) THEN 
xv(1)= intermediate_grid%soilcl2(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcl3) .GT. 1 ) THEN 
xv(1)= intermediate_grid%soilcl3(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilcl4) .GT. 1 ) THEN 
xv(1)= intermediate_grid%soilcl4(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acints) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acints(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acintr) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acintr(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acdripr) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acdripr(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acthror) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acthror(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevac) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acevac(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acdewc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acdewc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%forctlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%forctlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%forcqlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%forcqlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%forcplsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%forcplsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%forczlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%forczlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%forcwlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%forcwlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrainlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acrainlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrunsb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acrunsb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrunsf) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acrunsf(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acecan) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acecan(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acetran) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acetran(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acedir) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acedir(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acetlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acetlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnowlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsnowlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsubc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsubc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acfroc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acfroc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acfrzc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acfrzc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acmeltc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acmeltc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnbot) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsnbot(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnmelt) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsnmelt(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acponding) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acponding(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnsub) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsnsub(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsnfro) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsnfro(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acrainsnow) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acrainsnow(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acdrips) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acdrips(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acthros) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acthros(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsagb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsagb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acirb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acirb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acshb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acevb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acghb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acghb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acpahb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsagv) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsagv(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acirg) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acirg(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshg) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acshg(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevg) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acevg(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acghv) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acghv(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahg) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acpahg(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acsav) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acsav(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acirc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acirc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acshc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acevc) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acevc(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%actr) .GT. 1 ) THEN 
xv(1)= intermediate_grid%actr(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahv) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acpahv(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswdnlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswdnlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acswuplsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acswuplsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwdnlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwdnlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclwuplsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclwuplsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acshflsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acshflsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%aclhflsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aclhflsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acghflsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acghflsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acpahlsm) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acpahlsm(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%accanhs) .GT. 1 ) THEN 
xv(1)= intermediate_grid%accanhs(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%soilenergy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%soilenergy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%snowenergy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%snowenergy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_ssoil) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_ssoil(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_qinsur) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_qinsur(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_qseva) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_qseva(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_etrani) .GT. 1 ) THEN 
DO k = 1,config_flags%num_soil_layers
xv(k)= intermediate_grid%acc_etrani(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%aceflxb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%aceflxb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%eflxbxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%eflxbxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_dwaterxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_dwaterxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_prcpxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_prcpxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_ecanxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_ecanxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_etranxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_etranxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%acc_edirxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%acc_edirxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%grainxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%grainxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%gddxy) .GT. 1 ) THEN 
xv(1)= intermediate_grid%gddxy(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%croptype) .GT. 1 ) THEN 
DO k = 1,5
xv(k)= intermediate_grid%croptype(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((5)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%planting) .GT. 1 ) THEN 
xv(1)= intermediate_grid%planting(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%harvest) .GT. 1 ) THEN 
xv(1)= intermediate_grid%harvest(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%season_gdd) .GT. 1 ) THEN 
xv(1)= intermediate_grid%season_gdd(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%gecros_state) .GT. 1 ) THEN 
DO k = 1,60
xv(k)= intermediate_grid%gecros_state(pig,k,pjg)
ENDDO
CALL rsl_lite_to_parent_msg(((60)-(1)+1)*4,xv)
ENDIF
IF ( SIZE(grid%td_fraction) .GT. 1 ) THEN 
xv(1)= intermediate_grid%td_fraction(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%qtdrain) .GT. 1 ) THEN 
xv(1)= intermediate_grid%qtdrain(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irfract) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irfract(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%sifract) .GT. 1 ) THEN 
xv(1)= intermediate_grid%sifract(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%mifract) .GT. 1 ) THEN 
xv(1)= intermediate_grid%mifract(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%fifract) .GT. 1 ) THEN 
xv(1)= intermediate_grid%fifract(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irnumsi) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irnumsi(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irnummi) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irnummi(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irnumfi) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irnumfi(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irsivol) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irsivol(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irmivol) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irmivol(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irfivol) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irfivol(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%ireloss) .GT. 1 ) THEN 
xv(1)= intermediate_grid%ireloss(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%irrsplh) .GT. 1 ) THEN 
xv(1)= intermediate_grid%irrsplh(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%pcb) .GT. 1 ) THEN 
xv(1)= intermediate_grid%pcb(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%pc_2) .GT. 1 ) THEN 
xv(1)= intermediate_grid%pc_2(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%landmask) .GT. 1 ) THEN 
xv(1)= intermediate_grid%landmask(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
IF ( SIZE(grid%lakemask) .GT. 1 ) THEN 
xv(1)= intermediate_grid%lakemask(pig,pjg)
CALL rsl_lite_to_parent_msg(4,xv)
ENDIF
CALL rsl_lite_to_parent_info( msize*4                               &
                        ,cips,cipe,cjps,cjpe                               &
                        ,nids,nide,njds,njde                               &
                        ,nest_task_offsets(ngrid%id)                      &
                        ,nest_pes_x(parent_grid%id)                            &
                        ,nest_pes_y(parent_grid%id)                            &
                        ,nest_pes_x(intermediate_grid%id)                 &
                        ,nest_pes_y(intermediate_grid%id)                 &
                        ,thisdomain_max_halo_width                         &
                        ,icoord,jcoord                                     &
                        ,idim_cd,jdim_cd                                   &
                        ,pig,pjg,retval )
ENDDO


      grid => parent_grid
    CALL pop_communicators_for_domain

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


        CALL rsl_lite_merge_msgs( myproc, nest_pes_x(grid%id)*nest_pes_y(grid%id),         &
                                          nest_pes_x(ngrid%id)*nest_pes_y(ngrid%id),       &
                                          ioffset, local_comm )
      END IF

IF ( grid%active_this_task ) THEN
    CALL push_communicators_for_domain( grid%id )









CALL rsl_lite_from_child_info(pig,pjg,retval)
DO while ( retval .eq. 1 )
IF ( SIZE(grid%lu_index) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lu_index(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%u_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%u_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%v_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .TRUE. ) ) THEN
DO k = ckds,(ckde-1)
grid%v_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%w_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((ckde)-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,ckde
grid%w_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%ph_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((ckde)-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,ckde
grid%ph_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%phb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((ckde)-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,ckde
grid%phb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%t_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%mu_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%mu_2(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%mub) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%mub(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%nest_pos) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%nest_pos(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%nest_mask) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%nest_mask(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%alb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%alb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%pb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%pb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%q2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%q2(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t2(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%th2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%th2(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%psfc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%psfc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%u10) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%u10(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%v10) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%v10(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lpi) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lpi(pig,pjg) = xv(1)
ENDIF
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_moist
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
moist(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDIF
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_moist
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
dfi_moist(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDIF
ENDDO
IF ( SIZE(grid%qvold) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%qvold(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%qnwfa2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qnwfa2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qnifa2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qnifa2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qnbca2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qnbca2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qnocbb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qnocbb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qnbcbb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qnbcbb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_scalar
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
scalar(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDIF
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_scalar
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
dfi_scalar(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDIF
ENDDO
IF ( SIZE(grid%toposlpx) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%toposlpx(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%toposlpy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%toposlpy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%slope) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%slope(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%slp_azi) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%slp_azi(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%shdmax) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%shdmax(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%shdmin) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%shdmin(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%shdavg) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%shdavg(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%landusef) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_land_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_land_cat
grid%landusef(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%soilctop) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_cat
grid%soilctop(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%soilcbot) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_cat
grid%soilcbot(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%irrigation) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irrigation(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irr_rand_field) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irr_rand_field(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tslb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%tslb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%smois) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%smois(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%sh2o) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%sh2o(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%smcrel) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%smcrel(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%xice) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xice(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%icedepth) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%icedepth(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xicem) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xicem(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%albsi) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%albsi(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snowsi) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snowsi(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ivgtyp) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ivgtyp(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%isltyp) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%isltyp(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%vegfra) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%vegfra(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acgrdflx) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acgrdflx(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsnow) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsnow(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acrunoff) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acrunoff(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsnom) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsnom(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snow) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snow(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snowh) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snowh(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%canwat) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%canwat(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%sstsk) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%sstsk(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tsk_rural) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tsk_rural(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tr_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tgr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tgr_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tb_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tg_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tc_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qc_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%uc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%uc_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xxxr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xxxr_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xxxb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xxxb_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xxxg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xxxg_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xxxc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xxxc_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%cmcr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%cmcr_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%drelr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%drelr_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%drelb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%drelb_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%drelg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%drelg_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%flxhumr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%flxhumr_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%flxhumb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%flxhumb_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%flxhumg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%flxhumg_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tgrl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%tgrl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%smr_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%smr_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%trl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%trl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tbl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%tbl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tgl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%tgl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%sh_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%sh_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lh_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lh_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%g_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%g_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rn_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rn_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ts_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ts_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%frc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%frc_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%utype_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%utype_urb2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%imperv) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%imperv(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%canfra) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%canfra(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%u10e) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%u10e(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%v10e) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%v10e(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ctopo) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ctopo(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ctopo2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ctopo2(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%f_ice_phy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%f_ice_phy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%f_rain_phy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%f_rain_phy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%f_rimef_phy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%f_rimef_phy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%om_tmp) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%ocean_levels
grid%om_tmp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%om_s) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%ocean_levels
grid%om_s(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%om_u) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%ocean_levels
grid%om_u(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%om_v) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%ocean_levels)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%ocean_levels
grid%om_v(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%om_ml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%om_ml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%h_diabatic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%h_diabatic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%qv_diabatic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%qv_diabatic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%qc_diabatic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%qc_diabatic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%msft) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%msft(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfu) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
grid%msfu(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfv) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .TRUE. ) ) THEN
grid%msfv(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msftx) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%msftx(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfty) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%msfty(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfux) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
grid%msfux(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfuy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
grid%msfuy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfvx) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .TRUE. ) ) THEN
grid%msfvx(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfvx_inv) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .TRUE. ) ) THEN
grid%msfvx_inv(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%msfvy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .TRUE. ) ) THEN
grid%msfvy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%f) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%f(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%e) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%e(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%sina) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%sina(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%cosa) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%cosa(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ht) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ht(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tsk) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tsk(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rainc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rainc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rainsh) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rainsh(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rainnc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rainnc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_rainc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_rainc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_rainnc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_rainnc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snownc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snownc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%graupelnc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%graupelnc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%hailnc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%hailnc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%refl_10cm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%refl_10cm(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%mskf_refl_10cm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%mskf_refl_10cm(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%th_old) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%th_old(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%qv_old) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%qv_old(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%vmi3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%vmi3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%di3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%di3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%rhopo3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%rhopo3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%phii3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%phii3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%vmi3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%vmi3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%di3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%di3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%rhopo3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%rhopo3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%phii3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%phii3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%vmi3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%vmi3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%di3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%di3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%rhopo3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%rhopo3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%phii3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%phii3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%itype) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%itype(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%itype_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%itype_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%itype_3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%itype_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%ssat) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%ssat(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%ssati) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%ssati(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%acswupt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswupt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswuptc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswdnt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswdntc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswupb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswupb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswupbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswdnb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswdnbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwupt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwupt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwuptc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwdnt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwdntc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwupb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwupb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwupbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwdnb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwdnbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswupt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswupt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswuptc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswdnt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswdntc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswupb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswupb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswupbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswdnb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_acswdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_acswdnbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwupt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwupt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwuptc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwdnt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwdntc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwupb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwupb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwupbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwdnb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%i_aclwdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%i_aclwdnbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swupt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swupt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swuptc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swuptcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swuptcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swdnt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swdntc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swdntcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swdntcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swupb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swupb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swupbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swupbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swupbcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swdnb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swdnbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%swdnbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%swdnbcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwupt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwupt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwuptc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwuptcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwuptcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwdnt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwdntc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwdntcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwdntcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwupb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwupb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwupbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwupbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwupbcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwdnb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwdnbc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lwdnbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lwdnbcln(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xlat_u) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
grid%xlat_u(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xlong_u) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
grid%xlong_u(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xlat_v) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .TRUE. ) ) THEN
grid%xlat_v(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xlong_v) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .TRUE. ) ) THEN
grid%xlong_v(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%clat) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%clat(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tsk_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%tsk_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%qsfc_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%qsfc_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tslb_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat_soil
grid%tslb_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%smois_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat_soil
grid%smois_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%sh2o_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat_soil
grid%sh2o_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%canwat_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%canwat_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snow_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%snow_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowh_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%snowh_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowc_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%snowc_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tr_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%tr_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tb_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%tb_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tg_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%tg_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tc_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%tc_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%ts_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%ts_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%ts_rul2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%ts_rul2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%qc_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%qc_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%uc_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat
grid%uc_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%trl_urb3d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat_soil
grid%trl_urb3d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tbl_urb3d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat_soil
grid%tbl_urb3d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tgl_urb3d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%mosaic_cat_soil
grid%tgl_urb3d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%mosaic_cat_index) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_land_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_land_cat
grid%mosaic_cat_index(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%landusef2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_land_cat)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_land_cat
grid%landusef2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tmn) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tmn(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tyr) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tyr(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tyra) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tyra(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tdly) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tdly(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tlag) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%lagday)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%lagday
grid%tlag(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%xland) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xland(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%achfx) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%achfx(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclhf) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclhf(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snowc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snowc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%prec_acc_c) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%prec_acc_c(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%prec_acc_nc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%prec_acc_nc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snow_acc_nc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snow_acc_nc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t0ml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t0ml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%hml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%hml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%h0ml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%h0ml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%huml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%huml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%hvml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%hvml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tmoml) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tmoml(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%k1_u) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%k1_u(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%k2_u) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%k2_u(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%k3_u) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .TRUE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
grid%k3_u(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_tracer
CALL rsl_lite_from_child_msg((((ckde-1))-(ckds)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = ckds,(ckde-1)
tracer(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDIF
ENDDO
IF ( SIZE(grid%numc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%numc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%nump) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%nump(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snl) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%snl(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowdp) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%snowdp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%wtc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%wtc(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%wtp) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%wtp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osno) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osno(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_grnd) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_grnd(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_veg) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_veg(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_veg24) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_veg24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_veg240) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_veg240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%fsun) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%fsun(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%fsun24) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%fsun24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%fsun240) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%fsun240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%fsd24) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%fsd24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%fsd240) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%fsd240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%fsi24) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%fsi24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%fsi240) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%fsi240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%laip) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%laip(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2ocan) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2ocan(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2ocan_col) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2ocan_col(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t2m_max) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t2m_max(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t2m_min) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t2m_min(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t2clm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t2clm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t_ref2m) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_ref2m(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%q_ref2m) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%q_ref2m(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq_s1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq_s2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq_s3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq_s4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq_s5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq6) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq7) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq8) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq9) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq10) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice_s1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice_s2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice_s3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice_s4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice_s5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice6) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice7) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice8) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice9) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice10) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno_s1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno_s1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno_s2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno_s2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno_s3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno_s3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno_s4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno_s4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno_s5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno_s5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno6) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno7) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno8) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno9) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno10) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_soisno10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%dzsnow1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%dzsnow1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%dzsnow2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%dzsnow2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%dzsnow3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%dzsnow3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%dzsnow4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%dzsnow4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%dzsnow5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%dzsnow5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowrds1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%snowrds1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowrds2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%snowrds2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowrds3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%snowrds3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowrds4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%snowrds4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snowrds5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%snowrds5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake6) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake7) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake8) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake9) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_lake10) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%t_lake10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol5) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol6) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol7) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol8) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol9) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol10) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%albedosubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%albedosubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%lhsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%lhsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%hfxsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%hfxsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%lwupsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%lwupsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%q2subgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%q2subgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%sabvsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%sabvsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%sabgsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%sabgsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%nrasubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%nrasubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%swupsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%maxpatch)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%maxpatch
grid%swupsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%lakedepth2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lakedepth2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%savedtke12d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%savedtke12d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snowdp2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snowdp2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%h2osno2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%h2osno2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snl2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snl2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t_grnd2d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t_grnd2d(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t_lake3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%t_lake3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%lake_icefrac3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%lake_icefrac3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%z_lake3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%z_lake3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%dz_lake3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%dz_lake3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%t_soisno3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((15)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,15
grid%t_soisno3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_ice3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((15)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,15
grid%h2osoi_ice3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_liq3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((15)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,15
grid%h2osoi_liq3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%h2osoi_vol3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((15)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,15
grid%h2osoi_vol3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%z3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((15)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,15
grid%z3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%dz3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((15)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,15
grid%dz3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%zi3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((16)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,16
grid%zi3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%watsat3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%watsat3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%csol3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%csol3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tkmg3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%tkmg3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tkdry3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%tkdry3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%tksatu3d) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((10)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,10
grid%tksatu3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%isnowxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%isnowxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tvxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tgxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%canicexy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%canicexy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%canliqxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%canliqxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%eahxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%eahxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tahxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tahxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%cmxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%cmxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%fwetxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%fwetxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%sneqvoxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%sneqvoxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%alboldxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%alboldxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qsnowxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qsnowxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qrainxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qrainxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%wslakexy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%wslakexy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%zwtxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%zwtxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%waxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%waxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%wtxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%wtxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tsnoxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_snow_layers
grid%tsnoxy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%zsnsoxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_snso_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_snso_layers
grid%zsnsoxy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snicexy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_snow_layers
grid%snicexy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%snliqxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_snow_layers
grid%snliqxy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%lfmassxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lfmassxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rtmassxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rtmassxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%stmassxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%stmassxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%woodxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%woodxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%stblcpxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%stblcpxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%fastcpxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%fastcpxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%xsaixy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%xsaixy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t2mvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t2mvxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%t2mbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%t2mbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%q2mvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%q2mvxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%q2mbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%q2mbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tradxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tradxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%neexy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%neexy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%gppxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%gppxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%nppxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%nppxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%fvegxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%fvegxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qinxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qinxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%runsfxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%runsfxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%runsbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%runsbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ecanxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ecanxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%edirxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%edirxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%etranxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%etranxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%fsaxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%fsaxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%firaxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%firaxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aparxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aparxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%psnxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%psnxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%savxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%savxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%sagxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%sagxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rssunxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rssunxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rsshaxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rsshaxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%bgapxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%bgapxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%wgapxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%wgapxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tgvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tgvxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%tgbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%tgbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chvxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%shgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%shgxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%shcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%shcxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%shbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%shbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%evgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%evgxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%evbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%evbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ghvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ghvxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ghbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ghbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irgxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ircxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ircxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%trxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%trxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%evcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%evcxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chleafxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chleafxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chucxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chucxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chv2xy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chv2xy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chb2xy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chb2xy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%chstarxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%chstarxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%fdepthxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%fdepthxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%eqzwt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%eqzwt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rechclim) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rechclim(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%riverbedxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%riverbedxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qintsxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qintsxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qintrxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qintrxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qdripsxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qdripsxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qdriprxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qdriprxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qthrosxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qthrosxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qthrorxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qthrorxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qsnsubxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qsnsubxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qsnfroxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qsnfroxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qsubcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qsubcxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qfrocxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qfrocxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qevacxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qevacxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qdewcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qdewcxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qfrzcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qfrzcxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qmeltcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qmeltcxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qsnbotxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qsnbotxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qmeltxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qmeltxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%pondingxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%pondingxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%pahxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%pahxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%pahgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%pahgxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%pahvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%pahvxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%pahbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%pahbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%canhsxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%canhsxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%fpicexy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%fpicexy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%rainlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%rainlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snowlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snowlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%soilcomp) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((8)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,8
grid%soilcomp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%soilcl1) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%soilcl1(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%soilcl2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%soilcl2(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%soilcl3) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%soilcl3(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%soilcl4) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%soilcl4(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acints) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acints(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acintr) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acintr(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acdripr) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acdripr(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acthror) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acthror(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acevac) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acevac(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acdewc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acdewc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%forctlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%forctlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%forcqlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%forcqlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%forcplsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%forcplsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%forczlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%forczlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%forcwlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%forcwlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acrainlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acrainlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acrunsb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acrunsb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acrunsf) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acrunsf(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acecan) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acecan(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acetran) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acetran(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acedir) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acedir(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acetlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acetlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsnowlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsnowlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsubc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsubc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acfroc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acfroc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acfrzc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acfrzc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acmeltc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acmeltc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsnbot) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsnbot(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsnmelt) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsnmelt(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acponding) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acponding(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsnsub) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsnsub(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsnfro) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsnfro(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acrainsnow) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acrainsnow(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acdrips) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acdrips(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acthros) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acthros(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsagb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsagb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acirb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acirb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acshb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acshb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acevb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acevb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acghb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acghb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acpahb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acpahb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsagv) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsagv(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acirg) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acirg(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acshg) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acshg(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acevg) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acevg(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acghv) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acghv(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acpahg) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acpahg(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acsav) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acsav(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acirc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acirc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acshc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acshc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acevc) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acevc(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%actr) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%actr(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acpahv) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acpahv(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswdnlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswdnlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acswuplsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acswuplsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwdnlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwdnlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclwuplsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclwuplsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acshflsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acshflsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%aclhflsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aclhflsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acghflsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acghflsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acpahlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acpahlsm(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%accanhs) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%accanhs(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%soilenergy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%soilenergy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%snowenergy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%snowenergy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_ssoil) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_ssoil(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_qinsur) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_qinsur(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_qseva) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_qseva(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_etrani) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,config_flags%num_soil_layers
grid%acc_etrani(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%aceflxb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%aceflxb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%eflxbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%eflxbxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_dwaterxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_dwaterxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_prcpxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_prcpxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_ecanxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_ecanxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_etranxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_etranxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%acc_edirxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%acc_edirxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%grainxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%grainxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%gddxy) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%gddxy(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%croptype) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((5)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,5
grid%croptype(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%planting) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%planting(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%harvest) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%harvest(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%season_gdd) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%season_gdd(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%gecros_state) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(((60)-(1)+1)*4,xv) ;
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
DO k = 1,60
grid%gecros_state(pig,k,pjg) = xv(k)
ENDDO
ENDIF
ENDIF
IF ( SIZE(grid%td_fraction) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%td_fraction(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%qtdrain) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%qtdrain(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irfract) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irfract(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%sifract) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%sifract(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%mifract) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%mifract(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%fifract) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%fifract(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irnumsi) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irnumsi(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irnummi) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irnummi(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irnumfi) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irnumfi(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irsivol) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irsivol(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irmivol) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irmivol(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irfivol) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irfivol(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%ireloss) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%ireloss(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%irrsplh) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%irrsplh(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%pcb) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%pcb(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%pc_2) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%pc_2(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%landmask) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%landmask(pig,pjg) = xv(1)
ENDIF
ENDIF
IF ( SIZE(grid%lakemask) .GT. 1 ) THEN 
CALL rsl_lite_from_child_msg(4,xv)
IF ( cd_feedback_mask( pig, ips_save, ipe_save , pjg, jps_save, jpe_save, .FALSE., .FALSE. ) ) THEN
grid%lakemask(pig,pjg) = xv(1)
ENDIF
ENDIF
CALL rsl_lite_from_child_info(pig,pjg,retval)
ENDDO



      
      CALL get_ijk_from_grid (  ngrid,                           &
                                nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe    )
      CALL get_ijk_from_grid (  grid ,              &
                                ids, ide, jds, jde, kds, kde,    &
                                ims, ime, jms, jme, kms, kme,    &
                                ips, ipe, jps, jpe, kps, kpe    )








CALL HALO_INTERP_UP_sub ( grid, &
  config_flags, &
  num_moist, &
  moist, &
  num_dfi_moist, &
  dfi_moist, &
  num_scalar, &
  scalar, &
  num_dfi_scalar, &
  dfi_scalar, &
  num_tracer, &
  tracer, &
  local_communicator, &
  mytask, ntasks, ntasks_x, ntasks_y, &
  ids, ide, jds, jde, kds, kde,       &
  ims, ime, jms, jme, kms, kme,       &
  ips, ipe, jps, jpe, kps, kpe )



      CALL get_ijk_from_grid (  grid ,                   &
                                cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe    )








IF ( SIZE( grid%u_2, 1 ) * SIZE( grid%u_2, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%u_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%v_2, 1 ) * SIZE( grid%v_2, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%v_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%w_2, 1 ) * SIZE( grid%w_2, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%w_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( ckde, ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( nkde, nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ph_2, 1 ) * SIZE( grid%ph_2, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%ph_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( ckde, ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( nkde, nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%phb, 1 ) * SIZE( grid%phb, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%phb,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( ckde, ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( nkde, nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_2, 1 ) * SIZE( grid%t_2, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%t_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%mu_2, 1 ) * SIZE( grid%mu_2, 2 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%mu_2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%mub, 1 ) * SIZE( grid%mub, 2 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%mub,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%alb, 1 ) * SIZE( grid%alb, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%alb,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pb, 1 ) * SIZE( grid%pb, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%pb,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_moist
IF ( SIZE( moist, 1 ) * SIZE( moist, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  moist(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_moist
IF ( SIZE( dfi_moist, 1 ) * SIZE( dfi_moist, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  dfi_moist(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_scalar
IF ( SIZE( scalar, 1 ) * SIZE( scalar, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  scalar(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_scalar
IF ( SIZE( dfi_scalar, 1 ) * SIZE( dfi_scalar, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  dfi_scalar(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%ht, 1 ) * SIZE( grid%ht, 2 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%ht,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%th_old, 1 ) * SIZE( grid%th_old, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%th_old,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qv_old, 1 ) * SIZE( grid%qv_old, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%qv_old,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_tracer
IF ( SIZE( tracer, 1 ) * SIZE( tracer, 3 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  tracer(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%pcb, 1 ) * SIZE( grid%pcb, 2 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%pcb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pc_2, 1 ) * SIZE( grid%pc_2, 2 ) .GT. 1 ) THEN 
CALL smoother (  &         
                  grid%pc_2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF



    CALL pop_communicators_for_domain
END IF

      RETURN
   END SUBROUTINE feedback_domain_em_part2


