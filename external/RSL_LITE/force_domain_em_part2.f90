



   SUBROUTINE force_domain_em_part2 ( grid, ngrid, pgrid, config_flags    &








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
      USE module_dm, ONLY : ntasks, ntasks_x, ntasks_y, local_communicator, mytask, &
                            nest_pes_x, nest_pes_y 
                            
      USE module_comm_nesting_dm, ONLY : halo_force_down_sub
      USE module_model_constants
      IMPLICIT NONE

      TYPE(domain), POINTER :: grid          
      TYPE(domain), POINTER :: ngrid
      TYPE(domain), POINTER :: pgrid         







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
      INTEGER i,j,pig,pjg,cm,cn,nig,njg,retval,k,kk
      TYPE (grid_config_rec_type)            :: config_flags
      REAL xv(2000)
      INTEGER       ::          cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe
      INTEGER       ::          nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe
      INTEGER       ::          ids, ide, jds, jde, kds, kde,    &
                                ims, ime, jms, jme, kms, kme,    &
                                ips, ipe, jps, jpe, kps, kpe
      INTEGER idim1,idim2,idim3,idim4,idim5,idim6,idim7,itrace
      REAL  dummy_xs, dummy_xe, dummy_ys, dummy_ye

      
      REAL :: p_top_m  , p_surf_m , mu_m , hsca_m , pre_c ,pre_n
      REAL, DIMENSION(pgrid%s_vert:pgrid%e_vert) :: alt_w_c
      REAL, DIMENSION(pgrid%s_vert:pgrid%e_vert+1) :: alt_u_c
      REAL, DIMENSION(ngrid%s_vert:ngrid%e_vert) :: alt_w_n
      REAL, DIMENSION(ngrid%s_vert:ngrid%e_vert+1) :: alt_u_n      
      
      REAL, DIMENSION(:,:,:), ALLOCATABLE :: p, al
      REAL :: pfu, pfd, phm, temp, qvf, qvf1, qvf2    

      
      
      
      
      CALL get_ijk_from_grid (  pgrid ,                   &
                                cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe    )
                                
      
      
      
      
      
      CALL get_ijk_from_grid (  ngrid ,              &
                                nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe    )

      nlev  = ckde - ckds + 1








CALL rsl_lite_from_parent_info(pig,pjg,retval)
DO while ( retval .eq. 1 )
IF ( SIZE(grid%xlat) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xlat(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xlong) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xlong(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lu_index) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lu_index(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%var_sso) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%var_sso(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t_max_p) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t_max_p(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ght_max_p) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ght_max_p(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%max_p) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%max_p(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t_min_p) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t_min_p(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ght_min_p) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ght_min_p(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%min_p) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%min_p(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%erod) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%erosion_dim)-(1)+1)*4,xv)
DO k = 1,config_flags%erosion_dim
grid%erod(pig,pjg,k) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%u_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%u_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%v_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%v_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%w_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((ckde)-(ckds)+1)*4,xv)
DO k = ckds,ckde
grid%w_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%ph_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((ckde)-(ckds)+1)*4,xv)
DO k = ckds,ckde
grid%ph_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%phb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((ckde)-(ckds)+1)*4,xv)
DO k = ckds,ckde
grid%phb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%th_phy_m_t0) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%th_phy_m_t0(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%t_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_init) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%t_init(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%mu_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%mu_2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%mub) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%mub(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%alb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%alb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%pb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%pb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%q2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%q2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%th2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%th2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%psfc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%psfc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%u10) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%u10(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%v10) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%v10(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lpi) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lpi(pig,pjg) = xv(1)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_moist
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
moist(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_moist
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
dfi_moist(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDDO
IF ( SIZE(grid%qvold) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%qvold(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%qnwfa2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qnwfa2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qnifa2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qnifa2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qnbca2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qnbca2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qnocbb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qnocbb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qnbcbb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qnbcbb2d(pig,pjg) = xv(1)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_scalar
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
scalar(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_dfi_scalar
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
dfi_scalar(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDDO
IF ( SIZE(grid%toposlpx) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%toposlpx(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%toposlpy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%toposlpy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%slope) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%slope(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%slp_azi) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%slp_azi(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%shdmax) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%shdmax(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%shdmin) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%shdmin(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%shdavg) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%shdavg(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snoalb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snoalb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%landusef) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%num_land_cat
grid%landusef(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%soilctop) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_cat
grid%soilctop(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%soilcbot) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_cat
grid%soilcbot(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%irrigation) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irrigation(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irr_rand_field) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irr_rand_field(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tslb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%tslb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%smois) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%smois(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%sh2o) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%sh2o(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%smcrel) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%smcrel(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%xice) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xice(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%icedepth) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%icedepth(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xicem) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xicem(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%albsi) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%albsi(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snowsi) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snowsi(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%smstav) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%smstav(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%sfcrunoff) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sfcrunoff(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%udrunoff) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%udrunoff(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ivgtyp) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ivgtyp(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%isltyp) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%isltyp(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%vegfra) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%vegfra(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acgrdflx) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acgrdflx(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsnow) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsnow(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acrunoff) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acrunoff(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsnom) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsnom(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snow) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snow(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snowh) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snowh(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%canwat) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%canwat(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%sstsk) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sstsk(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lake_depth) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lake_depth(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%water_depth) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%water_depth(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%uoce) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%uoce(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%voce) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%voce(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tsk_rural) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tsk_rural(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tr_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tgr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tgr_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tb_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tg_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tc_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qc_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%uc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%uc_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xxxr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xxxr_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xxxb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xxxb_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xxxg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xxxg_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xxxc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xxxc_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%cmcr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%cmcr_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%drelr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%drelr_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%drelb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%drelb_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%drelg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%drelg_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%flxhumr_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%flxhumr_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%flxhumb_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%flxhumb_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%flxhumg_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%flxhumg_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tgrl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%tgrl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%smr_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%smr_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%trl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%trl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tbl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%tbl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tgl_urb3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%tgl_urb3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%sh_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sh_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lh_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lh_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%g_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%g_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rn_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rn_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ts_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ts_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%frc_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%frc_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%utype_urb2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%utype_urb2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%imperv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%imperv(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%canfra) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%canfra(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%u10e) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%u10e(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%v10e) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%v10e(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%var2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%var2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oc12d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oc12d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa1(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa3(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa4(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol1(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol3(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol4(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%var2dls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%var2dls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oc12dls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oc12dls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa1ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa1ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa2ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa2ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa3ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa3ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa4ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa4ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol1ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol1ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol2ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol2ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol3ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol3ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol4ls) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol4ls(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%var2dss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%var2dss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oc12dss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oc12dss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa1ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa1ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa2ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa2ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa3ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa3ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%oa4ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%oa4ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol1ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol1ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol2ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol2ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol3ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol3ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ol4ss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ol4ss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ctopo) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ctopo(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ctopo2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ctopo2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%o3rad) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%o3rad(pig,k,pjg) = xv(k)
ENDDO
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_aerod
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
aerod(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDDO
DO itrace =  PARAM_FIRST_SCALAR, num_aerocu
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
aerocu(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDDO
IF ( SIZE(grid%f_ice_phy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%f_ice_phy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%f_rain_phy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%f_rain_phy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%f_rimef_phy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%f_rimef_phy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%om_tmp) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%ocean_levels
grid%om_tmp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%om_s) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%ocean_levels
grid%om_s(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%om_depth) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%ocean_levels
grid%om_depth(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%om_u) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%ocean_levels
grid%om_u(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%om_v) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%ocean_levels
grid%om_v(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%om_lat) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%om_lat(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%om_lon) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%om_lon(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%om_ml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%om_ml(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%om_tini) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%ocean_levels
grid%om_tini(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%om_sini) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%ocean_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%ocean_levels
grid%om_sini(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h_diabatic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%h_diabatic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%qv_diabatic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%qv_diabatic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%qc_diabatic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%qc_diabatic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%msft) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msft(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfu) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfu(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfv(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msftx) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msftx(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfty) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfty(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfux) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfux(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfuy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfuy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfvx) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfvx(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfvx_inv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfvx_inv(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%msfvy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%msfvy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%f) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%f(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%e) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%e(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%sina) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sina(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%cosa) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%cosa(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ht) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ht(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ht_shad) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ht_shad(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tsk) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tsk(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rainc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rainc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rainsh) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rainsh(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rainnc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rainnc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_rainc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_rainc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_rainnc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_rainnc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snownc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snownc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%graupelnc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%graupelnc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%hailnc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%hailnc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%refl_10cm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%refl_10cm(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%mskf_refl_10cm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%mskf_refl_10cm(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%th_old) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%th_old(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%qv_old) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%qv_old(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%vmi3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%vmi3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%di3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%di3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rhopo3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%rhopo3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%phii3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%phii3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%vmi3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%vmi3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%di3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%di3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rhopo3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%rhopo3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%phii3d_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%phii3d_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%vmi3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%vmi3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%di3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%di3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rhopo3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%rhopo3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%phii3d_3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%phii3d_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%itype) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%itype(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%itype_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%itype_2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%itype_3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%itype_3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%ssat) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%ssat(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%ssati) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%ssati(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rthraten) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%rthraten(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%swdown) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdown(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdown2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdown2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdownc2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdownc2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%gsw) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%gsw(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%glw) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%glw(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swnorm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swnorm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%diffuse_frac) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%diffuse_frac(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddir) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddir(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddir2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddir2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddirc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddirc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddni) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddni(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddni2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddni2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddnic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddnic(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddnic2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddnic2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddif) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddif(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddif2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddif2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%gx) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%gx(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%bx) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%bx(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%gg) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%gg(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%bb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%bb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%coszen_ref) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%coszen_ref(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdown_ref) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdown_ref(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swddir_ref) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swddir_ref(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswupt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswupt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswuptc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswdnt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswdntc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswupb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswupb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswupbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswdnb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswdnbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwupt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwupt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwuptc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwdnt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwdntc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwupb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwupb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwupbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwdnb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwdnbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswupt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswupt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswuptc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswdnt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswdntc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswupb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswupb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswupbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswdnb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_acswdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_acswdnbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwupt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwupt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwuptc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwdnt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwdntc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwupb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwupb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwupbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwdnb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%i_aclwdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%i_aclwdnbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swupt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swupt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swuptc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swuptcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swuptcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdnt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdntc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdntcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdntcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swupb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swupb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swupbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swupbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swupbcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdnb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdnbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%swdnbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%swdnbcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwupt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwupt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwuptc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwuptc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwuptcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwuptcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwdnt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwdnt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwdntc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwdntc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwdntcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwdntcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwupb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwupb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwupbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwupbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwupbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwupbcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwdnb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwdnb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwdnbc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwdnbc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lwdnbcln) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lwdnbcln(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xlat_u) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xlat_u(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xlong_u) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xlong_u(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xlat_v) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xlat_v(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xlong_v) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xlong_v(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%clat) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%clat(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tsk_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%tsk_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%qsfc_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%qsfc_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tslb_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat_soil
grid%tslb_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%smois_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat_soil
grid%smois_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%sh2o_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat_soil
grid%sh2o_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%canwat_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%canwat_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snow_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%snow_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowh_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%snowh_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowc_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%snowc_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tr_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%tr_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tb_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%tb_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tg_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%tg_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tc_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%tc_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%ts_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%ts_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%ts_rul2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%ts_rul2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%qc_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%qc_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%uc_urb2d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat
grid%uc_urb2d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%trl_urb3d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat_soil
grid%trl_urb3d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tbl_urb3d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat_soil
grid%tbl_urb3d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tgl_urb3d_mosaic) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%mosaic_cat_soil)-(1)+1)*4,xv)
DO k = 1,config_flags%mosaic_cat_soil
grid%tgl_urb3d_mosaic(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%mosaic_cat_index) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%num_land_cat
grid%mosaic_cat_index(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%landusef2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_land_cat)-(1)+1)*4,xv)
DO k = 1,config_flags%num_land_cat
grid%landusef2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tmn) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tmn(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tyr) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tyr(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tyra) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tyra(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tdly) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tdly(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tlag) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%lagday)-(1)+1)*4,xv)
DO k = 1,config_flags%lagday
grid%tlag(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%xland) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xland(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%achfx) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%achfx(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclhf) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclhf(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snowc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snowc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%prec_acc_c) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%prec_acc_c(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%prec_acc_nc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%prec_acc_nc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snow_acc_nc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snow_acc_nc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tml(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t0ml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t0ml(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%hml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%hml(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%h0ml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%h0ml(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%huml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%huml(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%hvml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%hvml(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tmoml) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tmoml(pig,pjg) = xv(1)
ENDIF
DO itrace =  PARAM_FIRST_SCALAR, num_tracer
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
tracer(pig,k,pjg,itrace) = xv(k)
ENDDO
ENDDO
IF ( SIZE(grid%vertstrucc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%vertstrucc(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%vertstrucs) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg((((ckde-1))-(ckds)+1)*4,xv)
DO k = ckds,(ckde-1)
grid%vertstrucs(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%field_sf) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%field_sf(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%field_pbl) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%field_pbl(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%field_conv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%field_conv(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%ru_tendf_stoch) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%ru_tendf_stoch(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rv_tendf_stoch) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%rv_tendf_stoch(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rt_tendf_stoch) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%rt_tendf_stoch(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rand_pert) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%rand_pert(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%pattern_spp_conv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%pattern_spp_conv(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%pattern_spp_pbl) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%pattern_spp_pbl(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%pattern_spp_lsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%pattern_spp_lsm(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%rstoch) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%rstoch(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%numc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%numc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%nump) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%nump(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snl) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%snl(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowdp) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%snowdp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%wtc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%wtc(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%wtp) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%wtp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osno) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osno(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_grnd) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_grnd(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_veg) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_veg(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_veg24) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_veg24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_veg240) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_veg240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%fsun) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%fsun(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%fsun24) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%fsun24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%fsun240) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%fsun240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%fsd24) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%fsd24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%fsd240) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%fsd240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%fsi24) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%fsi24(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%fsi240) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%fsi240(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%laip) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%laip(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2ocan) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2ocan(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2ocan_col) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2ocan_col(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t2m_max) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t2m_max(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t2m_min) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t2m_min(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t2clm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t2clm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t_ref2m) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_ref2m(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%q_ref2m) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%q_ref2m(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq_s1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq_s2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq_s3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq_s4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq_s5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq_s5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq6) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq7) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq8) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq9) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq10) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_liq10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice_s1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice_s2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice_s3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice_s4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice_s5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice_s5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice6) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice7) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice8) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice9) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice10) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_ice10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno_s1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno_s1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno_s2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno_s2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno_s3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno_s3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno_s4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno_s4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno_s5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno_s5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno6) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno7) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno8) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno9) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno10) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_soisno10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%dzsnow1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%dzsnow1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%dzsnow2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%dzsnow2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%dzsnow3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%dzsnow3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%dzsnow4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%dzsnow4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%dzsnow5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%dzsnow5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowrds1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%snowrds1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowrds2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%snowrds2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowrds3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%snowrds3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowrds4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%snowrds4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snowrds5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%snowrds5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake6) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake7) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake8) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake9) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_lake10) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%t_lake10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol1(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol2(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol3(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol4(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol5) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol5(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol6) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol6(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol7) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol7(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol8) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol8(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol9) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol9(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol10) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%h2osoi_vol10(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%albedosubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%albedosubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%lhsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%lhsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%hfxsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%hfxsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%lwupsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%lwupsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%q2subgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%q2subgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%sabvsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%sabvsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%sabgsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%sabgsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%nrasubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%nrasubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%swupsubgrid) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%maxpatch)-(1)+1)*4,xv)
DO k = 1,config_flags%maxpatch
grid%swupsubgrid(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%lakedepth2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lakedepth2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%savedtke12d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%savedtke12d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snowdp2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snowdp2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%h2osno2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%h2osno2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snl2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snl2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t_grnd2d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t_grnd2d(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t_lake3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%t_lake3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%lake_icefrac3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%lake_icefrac3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%z_lake3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%z_lake3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%dz_lake3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%dz_lake3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%t_soisno3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((15)-(1)+1)*4,xv)
DO k = 1,15
grid%t_soisno3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_ice3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((15)-(1)+1)*4,xv)
DO k = 1,15
grid%h2osoi_ice3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_liq3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((15)-(1)+1)*4,xv)
DO k = 1,15
grid%h2osoi_liq3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%h2osoi_vol3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((15)-(1)+1)*4,xv)
DO k = 1,15
grid%h2osoi_vol3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%z3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((15)-(1)+1)*4,xv)
DO k = 1,15
grid%z3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%dz3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((15)-(1)+1)*4,xv)
DO k = 1,15
grid%dz3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%zi3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((16)-(1)+1)*4,xv)
DO k = 1,16
grid%zi3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%watsat3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%watsat3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%csol3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%csol3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tkmg3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%tkmg3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tkdry3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%tkdry3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%tksatu3d) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((10)-(1)+1)*4,xv)
DO k = 1,10
grid%tksatu3d(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%isnowxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%isnowxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tvxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tgxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%canicexy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%canicexy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%canliqxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%canliqxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%eahxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%eahxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tahxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tahxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%cmxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%cmxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%fwetxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%fwetxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%sneqvoxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sneqvoxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%alboldxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%alboldxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qsnowxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qsnowxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qrainxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qrainxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%wslakexy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%wslakexy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%zwtxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%zwtxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%waxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%waxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%wtxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%wtxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tsnoxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_snow_layers
grid%tsnoxy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%zsnsoxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_snso_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_snso_layers
grid%zsnsoxy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snicexy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_snow_layers
grid%snicexy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%snliqxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_snow_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_snow_layers
grid%snliqxy(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%lfmassxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lfmassxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rtmassxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rtmassxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%stmassxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%stmassxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%woodxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%woodxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%stblcpxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%stblcpxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%fastcpxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%fastcpxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%xsaixy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%xsaixy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t2mvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t2mvxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%t2mbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%t2mbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%q2mvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%q2mvxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%q2mbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%q2mbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tradxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tradxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%neexy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%neexy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%gppxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%gppxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%nppxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%nppxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%fvegxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%fvegxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qinxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qinxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%runsfxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%runsfxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%runsbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%runsbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ecanxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ecanxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%edirxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%edirxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%etranxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%etranxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%fsaxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%fsaxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%firaxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%firaxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aparxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aparxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%psnxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%psnxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%savxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%savxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%sagxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sagxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rssunxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rssunxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rsshaxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rsshaxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%bgapxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%bgapxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%wgapxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%wgapxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tgvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tgvxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%tgbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%tgbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chvxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%shgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%shgxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%shcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%shcxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%shbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%shbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%evgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%evgxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%evbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%evbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ghvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ghvxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ghbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ghbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irgxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ircxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ircxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%trxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%trxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%evcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%evcxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chleafxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chleafxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chucxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chucxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chv2xy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chv2xy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chb2xy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chb2xy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%chstarxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%chstarxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%fdepthxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%fdepthxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%eqzwt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%eqzwt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rechclim) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rechclim(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%riverbedxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%riverbedxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qintsxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qintsxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qintrxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qintrxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qdripsxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qdripsxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qdriprxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qdriprxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qthrosxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qthrosxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qthrorxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qthrorxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qsnsubxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qsnsubxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qsnfroxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qsnfroxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qsubcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qsubcxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qfrocxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qfrocxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qevacxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qevacxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qdewcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qdewcxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qfrzcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qfrzcxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qmeltcxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qmeltcxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qsnbotxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qsnbotxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qmeltxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qmeltxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%pondingxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%pondingxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%pahxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%pahxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%pahgxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%pahgxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%pahvxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%pahvxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%pahbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%pahbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%canhsxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%canhsxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%fpicexy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%fpicexy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%rainlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%rainlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snowlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snowlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%soilcomp) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((8)-(1)+1)*4,xv)
DO k = 1,8
grid%soilcomp(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%soilcl1) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%soilcl1(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%soilcl2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%soilcl2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%soilcl3) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%soilcl3(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%soilcl4) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%soilcl4(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acints) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acints(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acintr) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acintr(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acdripr) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acdripr(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acthror) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acthror(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acevac) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acevac(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acdewc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acdewc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%forctlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%forctlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%forcqlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%forcqlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%forcplsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%forcplsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%forczlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%forczlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%forcwlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%forcwlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acrainlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acrainlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acrunsb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acrunsb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acrunsf) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acrunsf(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acecan) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acecan(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acetran) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acetran(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acedir) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acedir(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acetlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acetlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsnowlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsnowlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsubc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsubc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acfroc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acfroc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acfrzc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acfrzc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acmeltc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acmeltc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsnbot) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsnbot(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsnmelt) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsnmelt(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acponding) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acponding(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsnsub) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsnsub(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsnfro) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsnfro(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acrainsnow) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acrainsnow(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acdrips) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acdrips(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acthros) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acthros(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsagb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsagb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acirb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acirb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acshb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acshb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acevb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acevb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acghb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acghb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acpahb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acpahb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsagv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsagv(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acirg) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acirg(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acshg) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acshg(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acevg) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acevg(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acghv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acghv(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acpahg) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acpahg(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acsav) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acsav(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acirc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acirc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acshc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acshc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acevc) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acevc(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%actr) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%actr(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acpahv) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acpahv(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswdnlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswdnlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acswuplsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acswuplsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwdnlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwdnlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclwuplsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclwuplsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acshflsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acshflsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%aclhflsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aclhflsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acghflsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acghflsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acpahlsm) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acpahlsm(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%accanhs) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%accanhs(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%soilenergy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%soilenergy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%snowenergy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%snowenergy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_ssoil) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_ssoil(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_qinsur) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_qinsur(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_qseva) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_qseva(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_etrani) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_soil_layers)-(1)+1)*4,xv)
DO k = 1,config_flags%num_soil_layers
grid%acc_etrani(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%aceflxb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%aceflxb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%eflxbxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%eflxbxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_dwaterxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_dwaterxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_prcpxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_prcpxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_ecanxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_ecanxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_etranxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_etranxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%acc_edirxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%acc_edirxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%grainxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%grainxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%gddxy) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%gddxy(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%croptype) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((5)-(1)+1)*4,xv)
DO k = 1,5
grid%croptype(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%planting) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%planting(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%harvest) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%harvest(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%season_gdd) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%season_gdd(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%gecros_state) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((60)-(1)+1)*4,xv)
DO k = 1,60
grid%gecros_state(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%td_fraction) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%td_fraction(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%qtdrain) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%qtdrain(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irfract) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irfract(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%sifract) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sifract(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%mifract) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%mifract(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%fifract) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%fifract(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irnumsi) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irnumsi(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irnummi) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irnummi(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irnumfi) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irnumfi(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irsivol) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irsivol(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irmivol) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irmivol(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irfivol) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irfivol(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%ireloss) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%ireloss(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%irrsplh) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%irrsplh(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%field_u_tend_perturb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%field_u_tend_perturb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%field_v_tend_perturb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%field_v_tend_perturb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%field_t_tend_perturb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(((config_flags%num_stoch_levels)-(1)+1)*4,xv)
DO k = 1,config_flags%num_stoch_levels
grid%field_t_tend_perturb(pig,k,pjg) = xv(k)
ENDDO
ENDIF
IF ( SIZE(grid%pcb) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%pcb(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%pc_2) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%pc_2(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%landmask) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%landmask(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%lakemask) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%lakemask(pig,pjg) = xv(1)
ENDIF
IF ( SIZE(grid%sst) .GT. 1 ) THEN 
CALL rsl_lite_from_parent_msg(4,xv)
grid%sst(pig,pjg) = xv(1)
ENDIF
CALL rsl_lite_from_parent_info(pig,pjg,retval)
ENDDO



if (ngrid%vert_refine_method .NE. 0) then

      
      
      



                
      hsca_m = 6.7 
      p_top_m = ngrid%p_top
      p_surf_m = 1.e5
      mu_m = p_surf_m - p_top_m

      do  k = 1,ckde
      pre_c = mu_m * pgrid%c3f(k) + p_top_m + pgrid%c4f(k)
      alt_w_c(k) =  -hsca_m * alog(pre_c/p_surf_m)
      enddo   
      do  k = 1,ckde-1
      pre_c = mu_m * pgrid%c3h(k) + p_top_m + pgrid%c4h(k)
      alt_u_c(k+1) =  -hsca_m * alog(pre_c/p_surf_m)
      enddo
      alt_u_c(1) =  alt_w_c(1)
      alt_u_c(ckde+1) =  alt_w_c(ckde)       

      do  k = 1,nkde
      pre_n = mu_m * ngrid%c3f(k) + p_top_m + ngrid%c4f(k)
      alt_w_n(k) =  -hsca_m * alog(pre_n/p_surf_m)
      enddo
      do  k = 1,nkde-1
      pre_n = mu_m * ngrid%c3h(k) + p_top_m + ngrid%c4h(k)
      alt_u_n(k+1) =  -hsca_m * alog(pre_n/p_surf_m)
      enddo
      alt_u_n(1) =  alt_w_n(1)
      alt_u_n(nkde+1) =  alt_w_n(nkde)
        
endif   

      
      CALL get_ijk_from_grid (  grid ,                   &
                                cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe    )
                                                      
      CALL get_ijk_from_grid (  grid ,              &
                                ids, ide, jds, jde, kds, kde,    &
                                ims, ime, jms, jme, kms, kme,    &
                                ips, ipe, jps, jpe, kps, kpe    )

      

      IF (ngrid%vert_refine_method .NE. 0) THEN
      







IF ( SIZE( grid%u_2, 1 ) * SIZE( grid%u_2, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  grid%u_2, & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
IF ( SIZE( grid%v_2, 1 ) * SIZE( grid%v_2, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  grid%v_2, & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
IF ( SIZE( grid%w_2, 1 ) * SIZE( grid%w_2, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting_w( &
                                  grid%w_2, & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  alt_w_c, alt_w_n ) 
ENDIF
IF ( SIZE( grid%ph_2, 1 ) * SIZE( grid%ph_2, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting_w( &
                                  grid%ph_2, & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  alt_w_c, alt_w_n ) 
ENDIF
IF ( SIZE( grid%t_2, 1 ) * SIZE( grid%t_2, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  grid%t_2, & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_moist
IF ( SIZE( moist, 1 ) * SIZE( moist, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  moist(grid%sm31,grid%sm32,grid%sm33,itrace), & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_moist
IF ( SIZE( dfi_moist, 1 ) * SIZE( dfi_moist, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  dfi_moist(grid%sm31,grid%sm32,grid%sm33,itrace), & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_scalar
IF ( SIZE( scalar, 1 ) * SIZE( scalar, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  scalar(grid%sm31,grid%sm32,grid%sm33,itrace), & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_scalar
IF ( SIZE( dfi_scalar, 1 ) * SIZE( dfi_scalar, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  dfi_scalar(grid%sm31,grid%sm32,grid%sm33,itrace), & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
ENDDO
IF ( SIZE( grid%o3rad, 1 ) * SIZE( grid%o3rad, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  grid%o3rad, & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_aerod
IF ( SIZE( aerod, 1 ) * SIZE( aerod, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  aerod(grid%sm31,grid%sm32,grid%sm33,itrace), & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_aerocu
IF ( SIZE( aerocu, 1 ) * SIZE( aerocu, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  aerocu(grid%sm31,grid%sm32,grid%sm33,itrace), & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_tracer
IF ( SIZE( tracer, 1 ) * SIZE( tracer, 3 ) .GT. 1 ) THEN 
    CALL vert_interp_vert_nesting( &
                                  tracer(grid%sm31,grid%sm32,grid%sm33,itrace), & 
                                  ids, ide, kds, kde, jds, jde, & 
                                  ims, ime, kms, kme, jms, jme, & 
                                  ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                  pgrid%s_vert, pgrid%e_vert, & 
                                  pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                  alt_u_c, alt_u_n ) 
ENDIF
ENDDO



         IF ( ngrid%this_is_an_ideal_run ) THEN
            IF ( SIZE( grid%t_init, 1 ) * SIZE( grid%t_init, 3 ) .GT. 1 ) THEN
               CALL vert_interp_vert_nesting( grid%t_init, & 
                                              ids, ide, kds, kde, jds, jde, & 
                                              ims, ime, kms, kme, jms, jme, & 
                                              ips, ipe, kps, MIN( (kde-1), kpe ), jps, jpe, & 
                                              pgrid%s_vert, pgrid%e_vert, & 
                                              pgrid%cf1, pgrid%cf2, pgrid%cf3, pgrid%cfn, pgrid%cfn1, & 
                                              alt_u_c, alt_u_n ) 
            END IF  
         END IF 


         
         
         
         
         

         
         

         
         
         
      
         

         DO j = MAX(jds,jps),MIN(jde-1,jpe)
            DO i = MAX(ids,ips),MIN(ide-1,ipe)
               DO k=kds,kde-1
                  grid%t_2(i,k,j) = grid%t_2(i,k,j)/((ngrid%c1h(k)*grid%mub(i,j)+ngrid%c2h(k)) + (ngrid%c1h(k)*grid%mu_2(i,j)))
                  moist(i,k,j,P_QV) = moist(i,k,j,P_QV)/((ngrid%c1h(k)*grid%mub(i,j)+ngrid%c2h(k)) + (ngrid%c1h(k)*grid%mu_2(i,j)))
               END DO
            END DO
         END DO
    
         DO j = MAX(jds,jps),MIN(jde-1,jpe)
            DO i = MAX(ids,ips),MIN(ide-1,ipe)

               DO k = 1, kpe-1
                  grid%pb(i,k,j) = ngrid%c3h(k) * grid%mub(i,j) + ngrid%c4h(k) + ngrid%p_top
             
                  
   
                  IF ( .NOT. ngrid%this_is_an_ideal_run ) THEN
                     temp = MAX ( ngrid%tiso, ngrid%t00 + ngrid%tlp*LOG(grid%pb(i,k,j)/ngrid%p00) )
                     IF ( grid%pb(i,k,j) .LT. ngrid%p_strat ) THEN
                        temp = ngrid%tiso + ngrid%tlp_strat * LOG ( grid%pb(i,k,j)/ngrid%p_strat )
                     END IF
                     grid%t_init(i,k,j) = temp*(ngrid%p00/grid%pb(i,k,j))**(r_d/cp) - t0
                  END IF
                  grid%alb(i,k,j) = (r_d/p1000mb)*(grid%t_init(i,k,j)+t0)*(grid%pb(i,k,j)/p1000mb)**cvpm
               END DO
   
               
               
               
   
               grid%phb(i,1,j) = grid%ht(i,j) * g
               IF (grid%hypsometric_opt == 1) THEN
                  DO kk = 2,kpe
                     k = kk - 1
                     grid%phb(i,kk,j) = grid%phb(i,k,j) - ngrid%dnw(k)*(ngrid%c1h(k)*grid%mub(i,j)+ngrid%c2h(k))*grid%alb(i,k,j)
                  END DO
               ELSE IF (grid%hypsometric_opt == 2) THEN
                  DO k = 2,kpe
                     pfu = ngrid%c3f(k  )*grid%MUB(i,j) + ngrid%c4f(k  ) + ngrid%p_top
                     pfd = ngrid%c3f(k-1)*grid%MUB(i,j) + ngrid%c4f(k-1) + ngrid%p_top
                     phm = ngrid%c3h(k-1)*grid%MUB(i,j) + ngrid%c4h(k-1) + ngrid%p_top
                     grid%phb(i,k,j) = grid%phb(i,k-1,j) + grid%alb(i,k-1,j)*phm*LOG(pfd/pfu)
                  END DO
               ELSE
                  CALL wrf_error_fatal3("<stdin>",3687,&
'module_dm: hypsometric_opt should be 1 or 2' )
               END IF  
            END DO  
         END DO  
         
         ALLOCATE( p (ips:ipe, kps:kpe, jps:jpe) )
         ALLOCATE( al(ips:ipe, kps:kpe, jps:jpe) )
         DO j = MAX(jds,jps),MIN(jde-1,jpe)
            DO i = MAX(ids,ips),MIN(ide-1,ipe)
               
               
               
      
               kk = kpe-1
               k = kk+1
      
               qvf1 = 0.5*(moist(i,kk,j,P_QV)+moist(i,kk,j,P_QV))
               qvf2 = 1./(1.+qvf1)
               qvf1 = qvf1*qvf2
      
               p(i,kk,j) = - 0.5*((ngrid%c1f(k)*grid%Mu_2(i,j))+qvf1*(ngrid%c1f(k)*grid%Mub(i,j)+ngrid%c2f(k)))/ngrid%rdnw(kk)/qvf2
               IF ( config_flags%use_theta_m == 0) THEN
                  qvf = 1. + rvovrd*moist(i,kk,j,P_QV)
               ELSE
                  qvf = 1.
               ENDIF
               al(i,kk,j) = (r_d/p1000mb)*(grid%t_2(i,kk,j)+t0)*qvf* &
                           (((p(i,kk,j)+grid%pb(i,kk,j))/p1000mb)**cvpm) - grid%alb(i,kk,j)
      
               
               
      
               DO kk=kpe-2,1,-1
                  k = kk + 1
                  qvf1 = 0.5*(moist(i,kk,j,P_QV)+moist(i,kk+1,j,P_QV))
                  qvf2 = 1./(1.+qvf1)
                  qvf1 = qvf1*qvf2
                  p(i,kk,j) = p(i,kk+1,j) - ((ngrid%c1f(k)*grid%Mu_2(i,j)) + qvf1*(ngrid%c1f(k)*grid%Mub(i,j)+ngrid%c2f(k)))/qvf2/ngrid%rdn(kk+1)
                  IF ( config_flags%use_theta_m == 0) THEN
                     qvf = 1. + rvovrd*moist(i,kk,j,P_QV)
                  ELSE
                     qvf = 1.
                  ENDIF
                  al(i,kk,j) = (r_d/p1000mb)*(grid%t_2(i,kk,j)+t0)*qvf* &
                              (((p(i,kk,j)+grid%pb(i,kk,j))/p1000mb)**cvpm) - grid%alb(i,kk,j)
               END DO
      
               
               
      
               IF (grid%hypsometric_opt == 1) THEN
                  DO kk = 2,kpe
                     k = kk - 1
                     grid%ph_2(i,kk,j) = grid%ph_2(i,kk-1,j) - &
                                        ngrid%dnw(kk-1) * ( ((ngrid%c1h(k)*grid%mub(i,j)+ngrid%c2h(k))+(ngrid%c1h(k)*grid%mu_2(i,j)))*al(i,kk-1,j) &
                                        + (ngrid%c1h(k)*grid%mu_2(i,j))*grid%alb(i,kk-1,j) )
                  END DO
      
               
               
               
      
               ELSE IF (grid%hypsometric_opt == 2) THEN
      
                  grid%ph_2(i,1,j) = grid%phb(i,1,j)
                  DO k = 2,kpe
                     pfu = ngrid%c3f(k  )*( grid%MUB(i,j)+grid%MU_2(i,j) ) + ngrid%c4f(k  ) + ngrid%p_top
                     pfd = ngrid%c3f(k-1)*( grid%MUB(i,j)+grid%MU_2(i,j) ) + ngrid%c4f(k-1) + ngrid%p_top
                     phm = ngrid%c3h(k-1)*( grid%MUB(i,j)+grid%MU_2(i,j) ) + ngrid%c4h(k-1) + ngrid%p_top
                     grid%ph_2(i,k,j) = grid%ph_2(i,k-1,j) + (grid%alb(i,k-1,j)+al(i,k-1,j))*phm*LOG(pfd/pfu)
                  END DO
      
                  DO k = 1,kpe
                     grid%ph_2(i,k,j) = grid%ph_2(i,k,j) - grid%phb(i,k,j)
                  END DO
      
               END IF

            END DO 
         END DO 

         DEALLOCATE(p)
         DEALLOCATE(al)
      
         
         DO j = MAX(jds,jps),MIN(jde-1,jpe)
            DO i = MAX(ids,ips),MIN(ide-1,ipe)
               DO k=kps,kpe
               grid%ph_2(i,k,j) = grid%ph_2(i,k,j)*((ngrid%c1f(k)*grid%Mub(i,j)+ngrid%c2f(k)) + (ngrid%c1f(k)*grid%Mu_2(i,j)))
               END DO
            END DO
         END DO
         DO j = MAX(jds,jps),MIN(jde-1,jpe)
            DO i = MAX(ids,ips),MIN(ide-1,ipe)
               DO k=kps,kpe-1
               grid%t_2(i,k,j) = grid%t_2(i,k,j)*((ngrid%c1h(k)*grid%mub(i,j)+ngrid%c2h(k)) + (ngrid%c1h(k)*grid%mu_2(i,j)))
               moist(i,k,j,P_QV) = moist(i,k,j,P_QV)*((ngrid%c1h(k)*grid%mub(i,j)+ngrid%c2h(k)) + (ngrid%c1h(k)*grid%mu_2(i,j)))
               END DO
            END DO
         END DO


      END IF
                               








CALL HALO_FORCE_DOWN_sub ( grid, &
  config_flags, &
  num_moist, &
  moist, &
  num_dfi_moist, &
  dfi_moist, &
  num_scalar, &
  scalar, &
  num_dfi_scalar, &
  dfi_scalar, &
  num_aerod, &
  aerod, &
  num_aerocu, &
  aerocu, &
  num_tracer, &
  tracer, &
  local_communicator, &
  mytask, ntasks, ntasks_x, ntasks_y, &
  ids, ide, jds, jde, kds, kde,       &
  ims, ime, jms, jme, kms, kme,       &
  ips, ipe, jps, jpe, kps, kpe )



      







IF ( SIZE( grid%u_2, 1 ) * SIZE( grid%u_2, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%u_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%u_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%u_bxs &
,dummy_xe ,ngrid%u_bxe &
,dummy_ys ,ngrid%u_bys &
,dummy_ye ,ngrid%u_bye &
,dummy_xs ,ngrid%u_btxs &
,dummy_xe ,ngrid%u_btxe &
,dummy_ys ,ngrid%u_btys &
,dummy_ye ,ngrid%u_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
IF ( SIZE( grid%v_2, 1 ) * SIZE( grid%v_2, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%v_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%v_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%v_bxs &
,dummy_xe ,ngrid%v_bxe &
,dummy_ys ,ngrid%v_bys &
,dummy_ye ,ngrid%v_bye &
,dummy_xs ,ngrid%v_btxs &
,dummy_xe ,ngrid%v_btxe &
,dummy_ys ,ngrid%v_btys &
,dummy_ye ,ngrid%v_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
IF ( SIZE( grid%w_2, 1 ) * SIZE( grid%w_2, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%w_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( ckde, ckpe ), cjps, cjpe,   &         
                  ngrid%w_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( nkde, nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%w_bxs &
,dummy_xe ,ngrid%w_bxe &
,dummy_ys ,ngrid%w_bys &
,dummy_ye ,ngrid%w_bye &
,dummy_xs ,ngrid%w_btxs &
,dummy_xe ,ngrid%w_btxe &
,dummy_ys ,ngrid%w_btys &
,dummy_ye ,ngrid%w_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
IF ( SIZE( grid%ph_2, 1 ) * SIZE( grid%ph_2, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%ph_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( ckde, ckpe ), cjps, cjpe,   &         
                  ngrid%ph_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( nkde, nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%ph_bxs &
,dummy_xe ,ngrid%ph_bxe &
,dummy_ys ,ngrid%ph_bys &
,dummy_ye ,ngrid%ph_bye &
,dummy_xs ,ngrid%ph_btxs &
,dummy_xe ,ngrid%ph_btxe &
,dummy_ys ,ngrid%ph_btys &
,dummy_ye ,ngrid%ph_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
IF ( SIZE( grid%t_2, 1 ) * SIZE( grid%t_2, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%t_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%t_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%t_bxs &
,dummy_xe ,ngrid%t_bxe &
,dummy_ys ,ngrid%t_bys &
,dummy_ye ,ngrid%t_bye &
,dummy_xs ,ngrid%t_btxs &
,dummy_xe ,ngrid%t_btxe &
,dummy_ys ,ngrid%t_btys &
,dummy_ye ,ngrid%t_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
IF ( SIZE( grid%mu_2, 1 ) * SIZE( grid%mu_2, 2 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%mu_2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%mu_2,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%mu_bxs &
,dummy_xe ,ngrid%mu_bxe &
,dummy_ys ,ngrid%mu_bys &
,dummy_ye ,ngrid%mu_bye &
,dummy_xs ,ngrid%mu_btxs &
,dummy_xe ,ngrid%mu_btxe &
,dummy_ys ,ngrid%mu_btys &
,dummy_ye ,ngrid%mu_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_moist
IF ( SIZE( moist, 1 ) * SIZE( moist, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  moist(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%moist(ngrid%sm31,ngrid%sm32,ngrid%sm33,itrace),  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,moist_bxs(cjms,1,1,itrace) ,ngrid%moist_bxs(njms,1,1,itrace) &
,moist_bxe(cjms,1,1,itrace) ,ngrid%moist_bxe(njms,1,1,itrace) &
,moist_bys(cims,1,1,itrace) ,ngrid%moist_bys(nims,1,1,itrace) &
,moist_bye(cims,1,1,itrace) ,ngrid%moist_bye(nims,1,1,itrace) &
,moist_btxs(cjms,1,1,itrace) ,ngrid%moist_btxs(njms,1,1,itrace) &
,moist_btxe(cjms,1,1,itrace) ,ngrid%moist_btxe(njms,1,1,itrace) &
,moist_btys(cims,1,1,itrace) ,ngrid%moist_btys(nims,1,1,itrace) &
,moist_btye(cims,1,1,itrace) ,ngrid%moist_btye(nims,1,1,itrace) &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_moist
IF ( SIZE( dfi_moist, 1 ) * SIZE( dfi_moist, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  dfi_moist(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%dfi_moist(ngrid%sm31,ngrid%sm32,ngrid%sm33,itrace),  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dfi_moist_bxs(cjms,1,1,itrace) ,ngrid%dfi_moist_bxs(njms,1,1,itrace) &
,dfi_moist_bxe(cjms,1,1,itrace) ,ngrid%dfi_moist_bxe(njms,1,1,itrace) &
,dfi_moist_bys(cims,1,1,itrace) ,ngrid%dfi_moist_bys(nims,1,1,itrace) &
,dfi_moist_bye(cims,1,1,itrace) ,ngrid%dfi_moist_bye(nims,1,1,itrace) &
,dfi_moist_btxs(cjms,1,1,itrace) ,ngrid%dfi_moist_btxs(njms,1,1,itrace) &
,dfi_moist_btxe(cjms,1,1,itrace) ,ngrid%dfi_moist_btxe(njms,1,1,itrace) &
,dfi_moist_btys(cims,1,1,itrace) ,ngrid%dfi_moist_btys(nims,1,1,itrace) &
,dfi_moist_btye(cims,1,1,itrace) ,ngrid%dfi_moist_btye(nims,1,1,itrace) &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_scalar
IF ( SIZE( scalar, 1 ) * SIZE( scalar, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  scalar(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%scalar(ngrid%sm31,ngrid%sm32,ngrid%sm33,itrace),  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,scalar_bxs(cjms,1,1,itrace) ,ngrid%scalar_bxs(njms,1,1,itrace) &
,scalar_bxe(cjms,1,1,itrace) ,ngrid%scalar_bxe(njms,1,1,itrace) &
,scalar_bys(cims,1,1,itrace) ,ngrid%scalar_bys(nims,1,1,itrace) &
,scalar_bye(cims,1,1,itrace) ,ngrid%scalar_bye(nims,1,1,itrace) &
,scalar_btxs(cjms,1,1,itrace) ,ngrid%scalar_btxs(njms,1,1,itrace) &
,scalar_btxe(cjms,1,1,itrace) ,ngrid%scalar_btxe(njms,1,1,itrace) &
,scalar_btys(cims,1,1,itrace) ,ngrid%scalar_btys(nims,1,1,itrace) &
,scalar_btye(cims,1,1,itrace) ,ngrid%scalar_btye(nims,1,1,itrace) &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_scalar
IF ( SIZE( dfi_scalar, 1 ) * SIZE( dfi_scalar, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  dfi_scalar(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%dfi_scalar(ngrid%sm31,ngrid%sm32,ngrid%sm33,itrace),  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dfi_scalar_bxs(cjms,1,1,itrace) ,ngrid%dfi_scalar_bxs(njms,1,1,itrace) &
,dfi_scalar_bxe(cjms,1,1,itrace) ,ngrid%dfi_scalar_bxe(njms,1,1,itrace) &
,dfi_scalar_bys(cims,1,1,itrace) ,ngrid%dfi_scalar_bys(nims,1,1,itrace) &
,dfi_scalar_bye(cims,1,1,itrace) ,ngrid%dfi_scalar_bye(nims,1,1,itrace) &
,dfi_scalar_btxs(cjms,1,1,itrace) ,ngrid%dfi_scalar_btxs(njms,1,1,itrace) &
,dfi_scalar_btxe(cjms,1,1,itrace) ,ngrid%dfi_scalar_btxe(njms,1,1,itrace) &
,dfi_scalar_btys(cims,1,1,itrace) ,ngrid%dfi_scalar_btys(nims,1,1,itrace) &
,dfi_scalar_btye(cims,1,1,itrace) ,ngrid%dfi_scalar_btye(nims,1,1,itrace) &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%o3rad, 1 ) * SIZE( grid%o3rad, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%o3rad,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%o3rad,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_aerod
IF ( SIZE( aerod, 1 ) * SIZE( aerod, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  aerod(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%aerod(ngrid%sm31,ngrid%sm32,ngrid%sm33,itrace),  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_aerocu
IF ( SIZE( aerocu, 1 ) * SIZE( aerocu, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  aerocu(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%aerocu(ngrid%sm31,ngrid%sm32,ngrid%sm33,itrace),  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%om_tmp, 1 ) * SIZE( grid%om_tmp, 3 ) .GT. 1 ) THEN 
CALL c2f_interp (  &         
                  grid%om_tmp,   &       
                 cids, cide, 1, config_flags%ocean_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%ocean_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%ocean_levels, cjps, cjpe,   &         
                  ngrid%om_tmp,  &   
                 nids, nide, 1, config_flags%ocean_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%ocean_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%ocean_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,grid%grid_id,ngrid%grid_id&
                  ) 
ENDIF
IF ( SIZE( grid%om_s, 1 ) * SIZE( grid%om_s, 3 ) .GT. 1 ) THEN 
CALL c2f_interp (  &         
                  grid%om_s,   &       
                 cids, cide, 1, config_flags%ocean_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%ocean_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%ocean_levels, cjps, cjpe,   &         
                  ngrid%om_s,  &   
                 nids, nide, 1, config_flags%ocean_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%ocean_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%ocean_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,grid%grid_id,ngrid%grid_id&
                  ) 
ENDIF
IF ( SIZE( grid%om_u, 1 ) * SIZE( grid%om_u, 3 ) .GT. 1 ) THEN 
CALL c2f_interp (  &         
                  grid%om_u,   &       
                 cids, cide, 1, config_flags%ocean_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%ocean_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%ocean_levels, cjps, cjpe,   &         
                  ngrid%om_u,  &   
                 nids, nide, 1, config_flags%ocean_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%ocean_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%ocean_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,grid%grid_id,ngrid%grid_id&
                  ) 
ENDIF
IF ( SIZE( grid%om_v, 1 ) * SIZE( grid%om_v, 3 ) .GT. 1 ) THEN 
CALL c2f_interp (  &         
                  grid%om_v,   &       
                 cids, cide, 1, config_flags%ocean_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%ocean_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%ocean_levels, cjps, cjpe,   &         
                  ngrid%om_v,  &   
                 nids, nide, 1, config_flags%ocean_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%ocean_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%ocean_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,grid%grid_id,ngrid%grid_id&
                  ) 
ENDIF
IF ( SIZE( grid%om_ml, 1 ) * SIZE( grid%om_ml, 2 ) .GT. 1 ) THEN 
CALL c2f_interp (  &         
                  grid%om_ml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%om_ml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,grid%grid_id,ngrid%grid_id&
                  ) 
ENDIF
IF ( SIZE( grid%ht_shad, 1 ) * SIZE( grid%ht_shad, 2 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%ht_shad,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ht_shad,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%ht_shad_bxs &
,dummy_xe ,ngrid%ht_shad_bxe &
,dummy_ys ,ngrid%ht_shad_bys &
,dummy_ye ,ngrid%ht_shad_bye &
,dummy_xs ,ngrid%ht_shad_btxs &
,dummy_xe ,ngrid%ht_shad_btxe &
,dummy_ys ,ngrid%ht_shad_btys &
,dummy_ye ,ngrid%ht_shad_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_tracer
IF ( SIZE( tracer, 1 ) * SIZE( tracer, 3 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  tracer(grid%sm31,grid%sm32,grid%sm33,itrace),   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%tracer(ngrid%sm31,ngrid%sm32,ngrid%sm33,itrace),  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,tracer_bxs(cjms,1,1,itrace) ,ngrid%tracer_bxs(njms,1,1,itrace) &
,tracer_bxe(cjms,1,1,itrace) ,ngrid%tracer_bxe(njms,1,1,itrace) &
,tracer_bys(cims,1,1,itrace) ,ngrid%tracer_bys(nims,1,1,itrace) &
,tracer_bye(cims,1,1,itrace) ,ngrid%tracer_bye(nims,1,1,itrace) &
,tracer_btxs(cjms,1,1,itrace) ,ngrid%tracer_btxs(njms,1,1,itrace) &
,tracer_btxe(cjms,1,1,itrace) ,ngrid%tracer_btxe(njms,1,1,itrace) &
,tracer_btys(cims,1,1,itrace) ,ngrid%tracer_btys(nims,1,1,itrace) &
,tracer_btye(cims,1,1,itrace) ,ngrid%tracer_btye(nims,1,1,itrace) &
,grid%dt,ngrid%dt&
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%field_sf, 1 ) * SIZE( grid%field_sf, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%field_sf,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%field_sf,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%field_pbl, 1 ) * SIZE( grid%field_pbl, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%field_pbl,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%field_pbl,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%field_conv, 1 ) * SIZE( grid%field_conv, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%field_conv,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%field_conv,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ru_tendf_stoch, 1 ) * SIZE( grid%ru_tendf_stoch, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%ru_tendf_stoch,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%ru_tendf_stoch,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rv_tendf_stoch, 1 ) * SIZE( grid%rv_tendf_stoch, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%rv_tendf_stoch,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%rv_tendf_stoch,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rt_tendf_stoch, 1 ) * SIZE( grid%rt_tendf_stoch, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%rt_tendf_stoch,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%rt_tendf_stoch,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rand_pert, 1 ) * SIZE( grid%rand_pert, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%rand_pert,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%rand_pert,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pattern_spp_conv, 1 ) * SIZE( grid%pattern_spp_conv, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%pattern_spp_conv,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%pattern_spp_conv,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pattern_spp_pbl, 1 ) * SIZE( grid%pattern_spp_pbl, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%pattern_spp_pbl,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%pattern_spp_pbl,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pattern_spp_lsm, 1 ) * SIZE( grid%pattern_spp_lsm, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%pattern_spp_lsm,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%pattern_spp_lsm,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rstoch, 1 ) * SIZE( grid%rstoch, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%rstoch,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%rstoch,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%field_u_tend_perturb, 1 ) * SIZE( grid%field_u_tend_perturb, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%field_u_tend_perturb,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%field_u_tend_perturb,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%field_v_tend_perturb, 1 ) * SIZE( grid%field_v_tend_perturb, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%field_v_tend_perturb,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%field_v_tend_perturb,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%field_t_tend_perturb, 1 ) * SIZE( grid%field_t_tend_perturb, 3 ) .GT. 1 ) THEN 
CALL p2c (  &         
                  grid%field_t_tend_perturb,   &       
                 cids, cide, 1, config_flags%num_stoch_levels, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_stoch_levels, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_stoch_levels, cjps, cjpe,   &         
                  ngrid%field_t_tend_perturb,  &   
                 nids, nide, 1, config_flags%num_stoch_levels, njds, njde,   &         
                 nims, nime, 1, config_flags%num_stoch_levels, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_stoch_levels, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pc_2, 1 ) * SIZE( grid%pc_2, 2 ) .GT. 1 ) THEN 
CALL bdy_interp (  &         
                  grid%pc_2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%pc_2,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
,dummy_xs ,ngrid%pc_bxs &
,dummy_xe ,ngrid%pc_bxe &
,dummy_ys ,ngrid%pc_bys &
,dummy_ye ,ngrid%pc_bye &
,dummy_xs ,ngrid%pc_btxs &
,dummy_xe ,ngrid%pc_btxe &
,dummy_ys ,ngrid%pc_btys &
,dummy_ye ,ngrid%pc_btye &
,grid%dt,ngrid%dt&
                  ) 
ENDIF



      RETURN
   END SUBROUTINE force_domain_em_part2



