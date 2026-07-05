


   SUBROUTINE feedback_domain_em_part1 ( grid, ngrid, config_flags    &








,moist,moist_bxs,moist_bxe,moist_bys,moist_bye,moist_btxs,moist_btxe,moist_btys,moist_btye,dfi_moist,dfi_moist_bxs,dfi_moist_bxe, &
dfi_moist_bys,dfi_moist_bye,dfi_moist_btxs,dfi_moist_btxe,dfi_moist_btys,dfi_moist_btye,scalar,scalar_bxs,scalar_bxe,scalar_bys, &
scalar_bye,scalar_btxs,scalar_btxe,scalar_btys,scalar_btye,dfi_scalar,dfi_scalar_bxs,dfi_scalar_bxe,dfi_scalar_bys, &
dfi_scalar_bye,dfi_scalar_btxs,dfi_scalar_btxe,dfi_scalar_btys,dfi_scalar_btye,aerod,aerocu,ozmixm,aerosolc_1,aerosolc_2,fdda3d, &
fdda2d,advh_t,advz_t,tracer,tracer_bxs,tracer_bxe,tracer_bys,tracer_bye,tracer_btxs,tracer_btxe,tracer_btys,tracer_btye,pert3d, &
nba_mij,nba_rij,sbmradar,chem &



                 )
      USE module_state_description
      USE module_domain, ONLY : domain, get_ijk_from_grid
      USE module_configure, ONLY : grid_config_rec_type, model_config_rec, model_to_grid_config_rec
      USE module_dm, ONLY : ntasks, ntasks_x, ntasks_y, itrace, local_communicator, mytask, &
                            ipe_save, jpe_save, ips_save, jps_save,                         &
                            nest_pes_x, nest_pes_y

      IMPLICIT NONE

      TYPE(domain), POINTER :: grid          
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
      TYPE(domain), POINTER :: xgrid
      TYPE (grid_config_rec_type)            :: config_flags, nconfig_flags
      REAL xv(2000)
      INTEGER       ::          cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe
      INTEGER       ::          nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe

      INTEGER idim1,idim2,idim3,idim4,idim5,idim6,idim7

      INTEGER local_comm, myproc, nproc, idum1, idum2
      INTEGER thisdomain_max_halo_width


      integer tjk

      INTERFACE
          SUBROUTINE feedback_nest_prep ( grid, config_flags    &








,moist,moist_bxs,moist_bxe,moist_bys,moist_bye,moist_btxs,moist_btxe,moist_btys,moist_btye,dfi_moist,dfi_moist_bxs,dfi_moist_bxe, &
dfi_moist_bys,dfi_moist_bye,dfi_moist_btxs,dfi_moist_btxe,dfi_moist_btys,dfi_moist_btye,scalar,scalar_bxs,scalar_bxe,scalar_bys, &
scalar_bye,scalar_btxs,scalar_btxe,scalar_btys,scalar_btye,dfi_scalar,dfi_scalar_bxs,dfi_scalar_bxe,dfi_scalar_bys, &
dfi_scalar_bye,dfi_scalar_btxs,dfi_scalar_btxe,dfi_scalar_btys,dfi_scalar_btye,aerod,aerocu,ozmixm,aerosolc_1,aerosolc_2,fdda3d, &
fdda2d,advh_t,advz_t,tracer,tracer_bxs,tracer_bxe,tracer_bys,tracer_bye,tracer_btxs,tracer_btxe,tracer_btys,tracer_btye,pert3d, &
nba_mij,nba_rij,sbmradar,chem &



)
             USE module_state_description
             USE module_domain, ONLY : domain
             USE module_configure, ONLY : grid_config_rec_type

             TYPE (grid_config_rec_type)            :: config_flags
             TYPE(domain), TARGET                   :: grid







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



          END SUBROUTINE feedback_nest_prep
      END INTERFACE


      CALL wrf_get_dm_communicator ( local_comm )
      CALL wrf_get_myproc( myproc )
      CALL wrf_get_nproc( nproc )



      CALL get_ijk_from_grid (  grid ,                                 &
                                cids, cide, cjds, cjde, ckds, ckde,    &
                                cims, cime, cjms, cjme, ckms, ckme,    &
                                cips, cipe, cjps, cjpe, ckps, ckpe    )

      CALL get_ijk_from_grid (  ngrid ,                                &
                                nids, nide, njds, njde, nkds, nkde,    &
                                nims, nime, njms, njme, nkms, nkme,    &
                                nips, nipe, njps, njpe, nkps, nkpe    )

      nlev  = ckde - ckds + 1

      ips_save = ngrid%i_parent_start   
      jps_save = ngrid%j_parent_start
      ipe_save = ngrid%i_parent_start + (nide-nids+1) / ngrid%parent_grid_ratio - 1
      jpe_save = ngrid%j_parent_start + (njde-njds+1) / ngrid%parent_grid_ratio - 1








      CALL model_to_grid_config_rec ( ngrid%id , model_config_rec , nconfig_flags )
      CALL set_scalar_indices_from_config ( ngrid%id , idum1 , idum2 )
      xgrid => grid
      grid => ngrid

      CALL feedback_nest_prep ( grid, nconfig_flags    &








,grid%moist,grid%moist_bxs,grid%moist_bxe,grid%moist_bys,grid%moist_bye,grid%moist_btxs,grid%moist_btxe,grid%moist_btys, &
grid%moist_btye,grid%dfi_moist,grid%dfi_moist_bxs,grid%dfi_moist_bxe,grid%dfi_moist_bys,grid%dfi_moist_bye,grid%dfi_moist_btxs, &
grid%dfi_moist_btxe,grid%dfi_moist_btys,grid%dfi_moist_btye,grid%scalar,grid%scalar_bxs,grid%scalar_bxe,grid%scalar_bys, &
grid%scalar_bye,grid%scalar_btxs,grid%scalar_btxe,grid%scalar_btys,grid%scalar_btye,grid%dfi_scalar,grid%dfi_scalar_bxs, &
grid%dfi_scalar_bxe,grid%dfi_scalar_bys,grid%dfi_scalar_bye,grid%dfi_scalar_btxs,grid%dfi_scalar_btxe,grid%dfi_scalar_btys, &
grid%dfi_scalar_btye,grid%aerod,grid%aerocu,grid%ozmixm,grid%aerosolc_1,grid%aerosolc_2,grid%fdda3d,grid%fdda2d,grid%advh_t, &
grid%advz_t,grid%tracer,grid%tracer_bxs,grid%tracer_bxe,grid%tracer_bys,grid%tracer_bye,grid%tracer_btxs,grid%tracer_btxe, &
grid%tracer_btys,grid%tracer_btye,grid%pert3d,grid%nba_mij,grid%nba_rij,grid%sbmradar,grid%chem &



)



      grid => xgrid
      CALL set_scalar_indices_from_config ( grid%id , idum1 , idum2 )










IF ( SIZE( grid%lu_index, 1 ) * SIZE( grid%lu_index, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lu_index,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lu_index,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%u_2, 1 ) * SIZE( grid%u_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%v_2, 1 ) * SIZE( grid%v_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%w_2, 1 ) * SIZE( grid%w_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%ph_2, 1 ) * SIZE( grid%ph_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%phb, 1 ) * SIZE( grid%phb, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%phb,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( ckde, ckpe ), cjps, cjpe,   &         
                  ngrid%phb,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( nkde, nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_2, 1 ) * SIZE( grid%t_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%mu_2, 1 ) * SIZE( grid%mu_2, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%mub, 1 ) * SIZE( grid%mub, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%mub,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%mub,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%nest_pos, 1 ) * SIZE( grid%nest_pos, 2 ) .GT. 1 ) THEN 
CALL mark_domain (  &         
                  grid%nest_pos,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%nest_pos,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%nest_mask, 1 ) * SIZE( grid%nest_mask, 2 ) .GT. 1 ) THEN 
CALL mark_domain (  &         
                  grid%nest_mask,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%nest_mask,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%alb, 1 ) * SIZE( grid%alb, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%alb,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%alb,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pb, 1 ) * SIZE( grid%pb, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%pb,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%pb,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%q2, 1 ) * SIZE( grid%q2, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%q2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%q2,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t2, 1 ) * SIZE( grid%t2, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%t2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t2,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%th2, 1 ) * SIZE( grid%th2, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%th2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%th2,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%psfc, 1 ) * SIZE( grid%psfc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%psfc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%psfc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%u10, 1 ) * SIZE( grid%u10, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%u10,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%u10,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%v10, 1 ) * SIZE( grid%v10, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%v10,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%v10,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lpi, 1 ) * SIZE( grid%lpi, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lpi,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lpi,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_moist
IF ( SIZE( moist, 1 ) * SIZE( moist, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_moist
IF ( SIZE( dfi_moist, 1 ) * SIZE( dfi_moist, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%qvold, 1 ) * SIZE( grid%qvold, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qvold,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%qvold,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qnwfa2d, 1 ) * SIZE( grid%qnwfa2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qnwfa2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qnwfa2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qnifa2d, 1 ) * SIZE( grid%qnifa2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qnifa2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qnifa2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qnbca2d, 1 ) * SIZE( grid%qnbca2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qnbca2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qnbca2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qnocbb2d, 1 ) * SIZE( grid%qnocbb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qnocbb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qnocbb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qnbcbb2d, 1 ) * SIZE( grid%qnbcbb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qnbcbb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qnbcbb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_scalar
IF ( SIZE( scalar, 1 ) * SIZE( scalar, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
ENDDO
DO itrace = PARAM_FIRST_SCALAR, num_dfi_scalar
IF ( SIZE( dfi_scalar, 1 ) * SIZE( dfi_scalar, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%toposlpx, 1 ) * SIZE( grid%toposlpx, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%toposlpx,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%toposlpx,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%toposlpy, 1 ) * SIZE( grid%toposlpy, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%toposlpy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%toposlpy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%slope, 1 ) * SIZE( grid%slope, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%slope,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%slope,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%slp_azi, 1 ) * SIZE( grid%slp_azi, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%slp_azi,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%slp_azi,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%shdmax, 1 ) * SIZE( grid%shdmax, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%shdmax,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%shdmax,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%shdmin, 1 ) * SIZE( grid%shdmin, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%shdmin,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%shdmin,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%shdavg, 1 ) * SIZE( grid%shdavg, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%shdavg,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%shdavg,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%landusef, 1 ) * SIZE( grid%landusef, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%landusef,   &       
                 cids, cide, 1, config_flags%num_land_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_land_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_land_cat, cjps, cjpe,   &         
                  ngrid%landusef,  &   
                 nids, nide, 1, config_flags%num_land_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%num_land_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_land_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilctop, 1 ) * SIZE( grid%soilctop, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%soilctop,   &       
                 cids, cide, 1, config_flags%num_soil_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_cat, cjps, cjpe,   &         
                  ngrid%soilctop,  &   
                 nids, nide, 1, config_flags%num_soil_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilcbot, 1 ) * SIZE( grid%soilcbot, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%soilcbot,   &       
                 cids, cide, 1, config_flags%num_soil_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_cat, cjps, cjpe,   &         
                  ngrid%soilcbot,  &   
                 nids, nide, 1, config_flags%num_soil_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irrigation, 1 ) * SIZE( grid%irrigation, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%irrigation,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irrigation,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irr_rand_field, 1 ) * SIZE( grid%irr_rand_field, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%irr_rand_field,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irr_rand_field,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tslb, 1 ) * SIZE( grid%tslb, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tslb,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%tslb,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%smois, 1 ) * SIZE( grid%smois, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%smois,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%smois,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sh2o, 1 ) * SIZE( grid%sh2o, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sh2o,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%sh2o,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%smcrel, 1 ) * SIZE( grid%smcrel, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%smcrel,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%smcrel,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xice, 1 ) * SIZE( grid%xice, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xice,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xice,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%icedepth, 1 ) * SIZE( grid%icedepth, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%icedepth,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%icedepth,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xicem, 1 ) * SIZE( grid%xicem, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xicem,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xicem,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%albsi, 1 ) * SIZE( grid%albsi, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%albsi,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%albsi,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowsi, 1 ) * SIZE( grid%snowsi, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowsi,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snowsi,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ivgtyp, 1 ) * SIZE( grid%ivgtyp, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%ivgtyp,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ivgtyp,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%isltyp, 1 ) * SIZE( grid%isltyp, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%isltyp,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%isltyp,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%vegfra, 1 ) * SIZE( grid%vegfra, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%vegfra,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%vegfra,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acgrdflx, 1 ) * SIZE( grid%acgrdflx, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acgrdflx,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acgrdflx,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsnow, 1 ) * SIZE( grid%acsnow, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsnow,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsnow,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acrunoff, 1 ) * SIZE( grid%acrunoff, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acrunoff,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acrunoff,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsnom, 1 ) * SIZE( grid%acsnom, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsnom,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsnom,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snow, 1 ) * SIZE( grid%snow, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snow,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snow,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowh, 1 ) * SIZE( grid%snowh, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowh,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snowh,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%canwat, 1 ) * SIZE( grid%canwat, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%canwat,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%canwat,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sstsk, 1 ) * SIZE( grid%sstsk, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sstsk,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%sstsk,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tsk_rural, 1 ) * SIZE( grid%tsk_rural, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tsk_rural,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tsk_rural,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tr_urb2d, 1 ) * SIZE( grid%tr_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tr_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tr_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tgr_urb2d, 1 ) * SIZE( grid%tgr_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tgr_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tgr_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tb_urb2d, 1 ) * SIZE( grid%tb_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tb_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tb_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tg_urb2d, 1 ) * SIZE( grid%tg_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tg_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tg_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tc_urb2d, 1 ) * SIZE( grid%tc_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tc_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tc_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qc_urb2d, 1 ) * SIZE( grid%qc_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qc_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qc_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%uc_urb2d, 1 ) * SIZE( grid%uc_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%uc_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%uc_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xxxr_urb2d, 1 ) * SIZE( grid%xxxr_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xxxr_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xxxr_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xxxb_urb2d, 1 ) * SIZE( grid%xxxb_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xxxb_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xxxb_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xxxg_urb2d, 1 ) * SIZE( grid%xxxg_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xxxg_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xxxg_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xxxc_urb2d, 1 ) * SIZE( grid%xxxc_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xxxc_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xxxc_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%cmcr_urb2d, 1 ) * SIZE( grid%cmcr_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%cmcr_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%cmcr_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%drelr_urb2d, 1 ) * SIZE( grid%drelr_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%drelr_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%drelr_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%drelb_urb2d, 1 ) * SIZE( grid%drelb_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%drelb_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%drelb_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%drelg_urb2d, 1 ) * SIZE( grid%drelg_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%drelg_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%drelg_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%flxhumr_urb2d, 1 ) * SIZE( grid%flxhumr_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%flxhumr_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%flxhumr_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%flxhumb_urb2d, 1 ) * SIZE( grid%flxhumb_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%flxhumb_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%flxhumb_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%flxhumg_urb2d, 1 ) * SIZE( grid%flxhumg_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%flxhumg_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%flxhumg_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tgrl_urb3d, 1 ) * SIZE( grid%tgrl_urb3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tgrl_urb3d,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%tgrl_urb3d,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%smr_urb3d, 1 ) * SIZE( grid%smr_urb3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%smr_urb3d,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%smr_urb3d,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%trl_urb3d, 1 ) * SIZE( grid%trl_urb3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%trl_urb3d,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%trl_urb3d,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tbl_urb3d, 1 ) * SIZE( grid%tbl_urb3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tbl_urb3d,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%tbl_urb3d,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tgl_urb3d, 1 ) * SIZE( grid%tgl_urb3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tgl_urb3d,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%tgl_urb3d,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sh_urb2d, 1 ) * SIZE( grid%sh_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sh_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%sh_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lh_urb2d, 1 ) * SIZE( grid%lh_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lh_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lh_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%g_urb2d, 1 ) * SIZE( grid%g_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%g_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%g_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rn_urb2d, 1 ) * SIZE( grid%rn_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%rn_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rn_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ts_urb2d, 1 ) * SIZE( grid%ts_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ts_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ts_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%frc_urb2d, 1 ) * SIZE( grid%frc_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%frc_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%frc_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%utype_urb2d, 1 ) * SIZE( grid%utype_urb2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%utype_urb2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%utype_urb2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%imperv, 1 ) * SIZE( grid%imperv, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%imperv,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%imperv,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%canfra, 1 ) * SIZE( grid%canfra, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%canfra,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%canfra,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%u10e, 1 ) * SIZE( grid%u10e, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%u10e,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%u10e,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%v10e, 1 ) * SIZE( grid%v10e, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%v10e,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%v10e,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ctopo, 1 ) * SIZE( grid%ctopo, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%ctopo,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ctopo,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ctopo2, 1 ) * SIZE( grid%ctopo2, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%ctopo2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ctopo2,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%f_ice_phy, 1 ) * SIZE( grid%f_ice_phy, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%f_ice_phy,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%f_ice_phy,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%f_rain_phy, 1 ) * SIZE( grid%f_rain_phy, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%f_rain_phy,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%f_rain_phy,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%f_rimef_phy, 1 ) * SIZE( grid%f_rimef_phy, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%f_rimef_phy,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%f_rimef_phy,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%om_tmp, 1 ) * SIZE( grid%om_tmp, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%om_s, 1 ) * SIZE( grid%om_s, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%om_u, 1 ) * SIZE( grid%om_u, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%om_v, 1 ) * SIZE( grid%om_v, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%om_ml, 1 ) * SIZE( grid%om_ml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%h_diabatic, 1 ) * SIZE( grid%h_diabatic, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%h_diabatic,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%h_diabatic,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qv_diabatic, 1 ) * SIZE( grid%qv_diabatic, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qv_diabatic,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%qv_diabatic,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qc_diabatic, 1 ) * SIZE( grid%qc_diabatic, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qc_diabatic,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%qc_diabatic,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msft, 1 ) * SIZE( grid%msft, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msft,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msft,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfu, 1 ) * SIZE( grid%msfu, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfu,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfu,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfv, 1 ) * SIZE( grid%msfv, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfv,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfv,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msftx, 1 ) * SIZE( grid%msftx, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msftx,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msftx,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfty, 1 ) * SIZE( grid%msfty, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfty,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfty,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfux, 1 ) * SIZE( grid%msfux, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfux,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfux,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfuy, 1 ) * SIZE( grid%msfuy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfuy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfuy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfvx, 1 ) * SIZE( grid%msfvx, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfvx,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfvx,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfvx_inv, 1 ) * SIZE( grid%msfvx_inv, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfvx_inv,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfvx_inv,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%msfvy, 1 ) * SIZE( grid%msfvy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%msfvy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%msfvy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%f, 1 ) * SIZE( grid%f, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%f,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%f,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%e, 1 ) * SIZE( grid%e, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%e,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%e,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sina, 1 ) * SIZE( grid%sina, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sina,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%sina,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%cosa, 1 ) * SIZE( grid%cosa, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%cosa,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%cosa,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ht, 1 ) * SIZE( grid%ht, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%ht,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ht,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tsk, 1 ) * SIZE( grid%tsk, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tsk,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tsk,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rainc, 1 ) * SIZE( grid%rainc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%rainc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rainc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rainsh, 1 ) * SIZE( grid%rainsh, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%rainsh,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rainsh,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rainnc, 1 ) * SIZE( grid%rainnc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%rainnc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rainnc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_rainc, 1 ) * SIZE( grid%i_rainc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_rainc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_rainc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_rainnc, 1 ) * SIZE( grid%i_rainnc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_rainnc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_rainnc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snownc, 1 ) * SIZE( grid%snownc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%snownc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snownc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%graupelnc, 1 ) * SIZE( grid%graupelnc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%graupelnc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%graupelnc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%hailnc, 1 ) * SIZE( grid%hailnc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%hailnc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%hailnc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%refl_10cm, 1 ) * SIZE( grid%refl_10cm, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%refl_10cm,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%refl_10cm,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%mskf_refl_10cm, 1 ) * SIZE( grid%mskf_refl_10cm, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%mskf_refl_10cm,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%mskf_refl_10cm,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%th_old, 1 ) * SIZE( grid%th_old, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%th_old,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%th_old,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qv_old, 1 ) * SIZE( grid%qv_old, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%qv_old,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%qv_old,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%vmi3d, 1 ) * SIZE( grid%vmi3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%vmi3d,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%vmi3d,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%di3d, 1 ) * SIZE( grid%di3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%di3d,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%di3d,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rhopo3d, 1 ) * SIZE( grid%rhopo3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%rhopo3d,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%rhopo3d,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%phii3d, 1 ) * SIZE( grid%phii3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%phii3d,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%phii3d,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%vmi3d_2, 1 ) * SIZE( grid%vmi3d_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%vmi3d_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%vmi3d_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%di3d_2, 1 ) * SIZE( grid%di3d_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%di3d_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%di3d_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rhopo3d_2, 1 ) * SIZE( grid%rhopo3d_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%rhopo3d_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%rhopo3d_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%phii3d_2, 1 ) * SIZE( grid%phii3d_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%phii3d_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%phii3d_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%vmi3d_3, 1 ) * SIZE( grid%vmi3d_3, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%vmi3d_3,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%vmi3d_3,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%di3d_3, 1 ) * SIZE( grid%di3d_3, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%di3d_3,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%di3d_3,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rhopo3d_3, 1 ) * SIZE( grid%rhopo3d_3, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%rhopo3d_3,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%rhopo3d_3,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%phii3d_3, 1 ) * SIZE( grid%phii3d_3, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%phii3d_3,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%phii3d_3,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%itype, 1 ) * SIZE( grid%itype, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%itype,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%itype,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%itype_2, 1 ) * SIZE( grid%itype_2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%itype_2,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%itype_2,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%itype_3, 1 ) * SIZE( grid%itype_3, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%itype_3,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%itype_3,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ssat, 1 ) * SIZE( grid%ssat, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%ssat,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%ssat,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ssati, 1 ) * SIZE( grid%ssati, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%ssati,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%ssati,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswupt, 1 ) * SIZE( grid%acswupt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswupt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswupt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswuptc, 1 ) * SIZE( grid%acswuptc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswuptc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswuptc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswdnt, 1 ) * SIZE( grid%acswdnt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswdnt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswdnt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswdntc, 1 ) * SIZE( grid%acswdntc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswdntc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswdntc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswupb, 1 ) * SIZE( grid%acswupb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswupb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswupb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswupbc, 1 ) * SIZE( grid%acswupbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswupbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswupbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswdnb, 1 ) * SIZE( grid%acswdnb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswdnb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswdnb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswdnbc, 1 ) * SIZE( grid%acswdnbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%acswdnbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswdnbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwupt, 1 ) * SIZE( grid%aclwupt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwupt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwupt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwuptc, 1 ) * SIZE( grid%aclwuptc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwuptc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwuptc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwdnt, 1 ) * SIZE( grid%aclwdnt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwdnt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwdnt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwdntc, 1 ) * SIZE( grid%aclwdntc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwdntc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwdntc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwupb, 1 ) * SIZE( grid%aclwupb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwupb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwupb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwupbc, 1 ) * SIZE( grid%aclwupbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwupbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwupbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwdnb, 1 ) * SIZE( grid%aclwdnb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwdnb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwdnb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwdnbc, 1 ) * SIZE( grid%aclwdnbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclwdnbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwdnbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswupt, 1 ) * SIZE( grid%i_acswupt, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswupt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswupt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswuptc, 1 ) * SIZE( grid%i_acswuptc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswuptc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswuptc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswdnt, 1 ) * SIZE( grid%i_acswdnt, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswdnt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswdnt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswdntc, 1 ) * SIZE( grid%i_acswdntc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswdntc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswdntc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswupb, 1 ) * SIZE( grid%i_acswupb, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswupb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswupb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswupbc, 1 ) * SIZE( grid%i_acswupbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswupbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswupbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswdnb, 1 ) * SIZE( grid%i_acswdnb, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswdnb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswdnb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_acswdnbc, 1 ) * SIZE( grid%i_acswdnbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_acswdnbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_acswdnbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwupt, 1 ) * SIZE( grid%i_aclwupt, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwupt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwupt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwuptc, 1 ) * SIZE( grid%i_aclwuptc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwuptc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwuptc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwdnt, 1 ) * SIZE( grid%i_aclwdnt, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwdnt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwdnt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwdntc, 1 ) * SIZE( grid%i_aclwdntc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwdntc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwdntc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwupb, 1 ) * SIZE( grid%i_aclwupb, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwupb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwupb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwupbc, 1 ) * SIZE( grid%i_aclwupbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwupbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwupbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwdnb, 1 ) * SIZE( grid%i_aclwdnb, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwdnb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwdnb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%i_aclwdnbc, 1 ) * SIZE( grid%i_aclwdnbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%i_aclwdnbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%i_aclwdnbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swupt, 1 ) * SIZE( grid%swupt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swupt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swupt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swuptc, 1 ) * SIZE( grid%swuptc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swuptc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swuptc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swuptcln, 1 ) * SIZE( grid%swuptcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swuptcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swuptcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swdnt, 1 ) * SIZE( grid%swdnt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swdnt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swdnt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swdntc, 1 ) * SIZE( grid%swdntc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swdntc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swdntc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swdntcln, 1 ) * SIZE( grid%swdntcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swdntcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swdntcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swupb, 1 ) * SIZE( grid%swupb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swupb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swupb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swupbc, 1 ) * SIZE( grid%swupbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swupbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swupbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swupbcln, 1 ) * SIZE( grid%swupbcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swupbcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swupbcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swdnb, 1 ) * SIZE( grid%swdnb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swdnb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swdnb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swdnbc, 1 ) * SIZE( grid%swdnbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swdnbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swdnbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swdnbcln, 1 ) * SIZE( grid%swdnbcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%swdnbcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%swdnbcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwupt, 1 ) * SIZE( grid%lwupt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwupt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwupt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwuptc, 1 ) * SIZE( grid%lwuptc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwuptc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwuptc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwuptcln, 1 ) * SIZE( grid%lwuptcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwuptcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwuptcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwdnt, 1 ) * SIZE( grid%lwdnt, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwdnt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwdnt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwdntc, 1 ) * SIZE( grid%lwdntc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwdntc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwdntc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwdntcln, 1 ) * SIZE( grid%lwdntcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwdntcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwdntcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwupb, 1 ) * SIZE( grid%lwupb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwupb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwupb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwupbc, 1 ) * SIZE( grid%lwupbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwupbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwupbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwupbcln, 1 ) * SIZE( grid%lwupbcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwupbcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwupbcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwdnb, 1 ) * SIZE( grid%lwdnb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwdnb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwdnb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwdnbc, 1 ) * SIZE( grid%lwdnbc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwdnbc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwdnbc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwdnbcln, 1 ) * SIZE( grid%lwdnbcln, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%lwdnbcln,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lwdnbcln,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xlat_u, 1 ) * SIZE( grid%xlat_u, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xlat_u,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xlat_u,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xlong_u, 1 ) * SIZE( grid%xlong_u, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xlong_u,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xlong_u,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xlat_v, 1 ) * SIZE( grid%xlat_v, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xlat_v,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xlat_v,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xlong_v, 1 ) * SIZE( grid%xlong_v, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xlong_v,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xlong_v,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_ystag,         &         
                  .FALSE., .TRUE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%clat, 1 ) * SIZE( grid%clat, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%clat,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%clat,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tsk_mosaic, 1 ) * SIZE( grid%tsk_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tsk_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%tsk_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qsfc_mosaic, 1 ) * SIZE( grid%qsfc_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qsfc_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%qsfc_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tslb_mosaic, 1 ) * SIZE( grid%tslb_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tslb_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat_soil, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat_soil, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat_soil, cjps, cjpe,   &         
                  ngrid%tslb_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat_soil, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat_soil, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat_soil, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%smois_mosaic, 1 ) * SIZE( grid%smois_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%smois_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat_soil, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat_soil, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat_soil, cjps, cjpe,   &         
                  ngrid%smois_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat_soil, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat_soil, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat_soil, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sh2o_mosaic, 1 ) * SIZE( grid%sh2o_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sh2o_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat_soil, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat_soil, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat_soil, cjps, cjpe,   &         
                  ngrid%sh2o_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat_soil, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat_soil, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat_soil, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%canwat_mosaic, 1 ) * SIZE( grid%canwat_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%canwat_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%canwat_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snow_mosaic, 1 ) * SIZE( grid%snow_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snow_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%snow_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowh_mosaic, 1 ) * SIZE( grid%snowh_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowh_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%snowh_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowc_mosaic, 1 ) * SIZE( grid%snowc_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowc_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%snowc_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tr_urb2d_mosaic, 1 ) * SIZE( grid%tr_urb2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tr_urb2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%tr_urb2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tb_urb2d_mosaic, 1 ) * SIZE( grid%tb_urb2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tb_urb2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%tb_urb2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tg_urb2d_mosaic, 1 ) * SIZE( grid%tg_urb2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tg_urb2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%tg_urb2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tc_urb2d_mosaic, 1 ) * SIZE( grid%tc_urb2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tc_urb2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%tc_urb2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ts_urb2d_mosaic, 1 ) * SIZE( grid%ts_urb2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ts_urb2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%ts_urb2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ts_rul2d_mosaic, 1 ) * SIZE( grid%ts_rul2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ts_rul2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%ts_rul2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qc_urb2d_mosaic, 1 ) * SIZE( grid%qc_urb2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qc_urb2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%qc_urb2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%uc_urb2d_mosaic, 1 ) * SIZE( grid%uc_urb2d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%uc_urb2d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat, cjps, cjpe,   &         
                  ngrid%uc_urb2d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%trl_urb3d_mosaic, 1 ) * SIZE( grid%trl_urb3d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%trl_urb3d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat_soil, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat_soil, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat_soil, cjps, cjpe,   &         
                  ngrid%trl_urb3d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat_soil, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat_soil, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat_soil, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tbl_urb3d_mosaic, 1 ) * SIZE( grid%tbl_urb3d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tbl_urb3d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat_soil, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat_soil, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat_soil, cjps, cjpe,   &         
                  ngrid%tbl_urb3d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat_soil, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat_soil, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat_soil, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tgl_urb3d_mosaic, 1 ) * SIZE( grid%tgl_urb3d_mosaic, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tgl_urb3d_mosaic,   &       
                 cids, cide, 1, config_flags%mosaic_cat_soil, cjds, cjde,   &         
                 cims, cime, 1, config_flags%mosaic_cat_soil, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%mosaic_cat_soil, cjps, cjpe,   &         
                  ngrid%tgl_urb3d_mosaic,  &   
                 nids, nide, 1, config_flags%mosaic_cat_soil, njds, njde,   &         
                 nims, nime, 1, config_flags%mosaic_cat_soil, njms, njme,   &         
                 nips, nipe, 1, config_flags%mosaic_cat_soil, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%mosaic_cat_index, 1 ) * SIZE( grid%mosaic_cat_index, 3 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%mosaic_cat_index,   &       
                 cids, cide, 1, config_flags%num_land_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_land_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_land_cat, cjps, cjpe,   &         
                  ngrid%mosaic_cat_index,  &   
                 nids, nide, 1, config_flags%num_land_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%num_land_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_land_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%landusef2, 1 ) * SIZE( grid%landusef2, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%landusef2,   &       
                 cids, cide, 1, config_flags%num_land_cat, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_land_cat, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_land_cat, cjps, cjpe,   &         
                  ngrid%landusef2,  &   
                 nids, nide, 1, config_flags%num_land_cat, njds, njde,   &         
                 nims, nime, 1, config_flags%num_land_cat, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_land_cat, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tmn, 1 ) * SIZE( grid%tmn, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tmn,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tmn,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tyr, 1 ) * SIZE( grid%tyr, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tyr,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tyr,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tyra, 1 ) * SIZE( grid%tyra, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tyra,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tyra,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tdly, 1 ) * SIZE( grid%tdly, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tdly,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tdly,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tlag, 1 ) * SIZE( grid%tlag, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tlag,   &       
                 cids, cide, 1, config_flags%lagday, cjds, cjde,   &         
                 cims, cime, 1, config_flags%lagday, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%lagday, cjps, cjpe,   &         
                  ngrid%tlag,  &   
                 nids, nide, 1, config_flags%lagday, njds, njde,   &         
                 nims, nime, 1, config_flags%lagday, njms, njme,   &         
                 nips, nipe, 1, config_flags%lagday, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xland, 1 ) * SIZE( grid%xland, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xland,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xland,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%achfx, 1 ) * SIZE( grid%achfx, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%achfx,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%achfx,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclhf, 1 ) * SIZE( grid%aclhf, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%aclhf,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclhf,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowc, 1 ) * SIZE( grid%snowc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snowc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%prec_acc_c, 1 ) * SIZE( grid%prec_acc_c, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%prec_acc_c,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%prec_acc_c,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%prec_acc_nc, 1 ) * SIZE( grid%prec_acc_nc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%prec_acc_nc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%prec_acc_nc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snow_acc_nc, 1 ) * SIZE( grid%snow_acc_nc, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%snow_acc_nc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snow_acc_nc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tml, 1 ) * SIZE( grid%tml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t0ml, 1 ) * SIZE( grid%t0ml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t0ml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t0ml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%hml, 1 ) * SIZE( grid%hml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%hml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%hml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h0ml, 1 ) * SIZE( grid%h0ml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h0ml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%h0ml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%huml, 1 ) * SIZE( grid%huml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%huml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%huml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%hvml, 1 ) * SIZE( grid%hvml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%hvml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%hvml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tmoml, 1 ) * SIZE( grid%tmoml, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tmoml,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tmoml,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%k1_u, 1 ) * SIZE( grid%k1_u, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%k1_u,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%k1_u,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%k2_u, 1 ) * SIZE( grid%k2_u, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%k2_u,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%k2_u,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%k3_u, 1 ) * SIZE( grid%k3_u, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%k3_u,   &       
                 cids, cide, ckds, ckde, cjds, cjde,   &         
                 cims, cime, ckms, ckme, cjms, cjme,   &         
                 cips, cipe, ckps, MIN( (ckde-1), ckpe ), cjps, cjpe,   &         
                  ngrid%k3_u,  &   
                 nids, nide, nkds, nkde, njds, njde,   &         
                 nims, nime, nkms, nkme, njms, njme,   &         
                 nips, nipe, nkps, MIN( (nkde-1), nkpe ), njps, njpe,   &         
                  config_flags%shw, ngrid%imask_xstag,         &         
                  .TRUE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
DO itrace = PARAM_FIRST_SCALAR, num_tracer
IF ( SIZE( tracer, 1 ) * SIZE( tracer, 3 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
ENDDO
IF ( SIZE( grid%numc, 1 ) * SIZE( grid%numc, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%numc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%numc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%nump, 1 ) * SIZE( grid%nump, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%nump,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%nump,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snl, 1 ) * SIZE( grid%snl, 3 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%snl,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%snl,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowdp, 1 ) * SIZE( grid%snowdp, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowdp,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%snowdp,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%wtc, 1 ) * SIZE( grid%wtc, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%wtc,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%wtc,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%wtp, 1 ) * SIZE( grid%wtp, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%wtp,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%wtp,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osno, 1 ) * SIZE( grid%h2osno, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osno,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osno,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_grnd, 1 ) * SIZE( grid%t_grnd, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_grnd,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_grnd,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_veg, 1 ) * SIZE( grid%t_veg, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_veg,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_veg,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_veg24, 1 ) * SIZE( grid%t_veg24, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_veg24,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_veg24,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_veg240, 1 ) * SIZE( grid%t_veg240, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_veg240,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_veg240,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsun, 1 ) * SIZE( grid%fsun, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsun,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%fsun,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsun24, 1 ) * SIZE( grid%fsun24, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsun24,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%fsun24,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsun240, 1 ) * SIZE( grid%fsun240, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsun240,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%fsun240,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsd24, 1 ) * SIZE( grid%fsd24, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsd24,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%fsd24,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsd240, 1 ) * SIZE( grid%fsd240, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsd240,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%fsd240,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsi24, 1 ) * SIZE( grid%fsi24, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsi24,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%fsi24,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsi240, 1 ) * SIZE( grid%fsi240, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsi240,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%fsi240,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%laip, 1 ) * SIZE( grid%laip, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%laip,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%laip,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2ocan, 1 ) * SIZE( grid%h2ocan, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2ocan,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2ocan,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2ocan_col, 1 ) * SIZE( grid%h2ocan_col, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2ocan_col,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2ocan_col,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t2m_max, 1 ) * SIZE( grid%t2m_max, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t2m_max,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t2m_max,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t2m_min, 1 ) * SIZE( grid%t2m_min, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t2m_min,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t2m_min,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t2clm, 1 ) * SIZE( grid%t2clm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t2clm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t2clm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_ref2m, 1 ) * SIZE( grid%t_ref2m, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_ref2m,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_ref2m,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%q_ref2m, 1 ) * SIZE( grid%q_ref2m, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%q_ref2m,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%q_ref2m,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq_s1, 1 ) * SIZE( grid%h2osoi_liq_s1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq_s1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq_s1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq_s2, 1 ) * SIZE( grid%h2osoi_liq_s2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq_s2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq_s2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq_s3, 1 ) * SIZE( grid%h2osoi_liq_s3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq_s3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq_s3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq_s4, 1 ) * SIZE( grid%h2osoi_liq_s4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq_s4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq_s4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq_s5, 1 ) * SIZE( grid%h2osoi_liq_s5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq_s5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq_s5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq1, 1 ) * SIZE( grid%h2osoi_liq1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq2, 1 ) * SIZE( grid%h2osoi_liq2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq3, 1 ) * SIZE( grid%h2osoi_liq3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq4, 1 ) * SIZE( grid%h2osoi_liq4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq5, 1 ) * SIZE( grid%h2osoi_liq5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq6, 1 ) * SIZE( grid%h2osoi_liq6, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq6,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq6,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq7, 1 ) * SIZE( grid%h2osoi_liq7, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq7,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq7,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq8, 1 ) * SIZE( grid%h2osoi_liq8, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq8,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq8,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq9, 1 ) * SIZE( grid%h2osoi_liq9, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq9,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq9,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq10, 1 ) * SIZE( grid%h2osoi_liq10, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq10,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_liq10,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice_s1, 1 ) * SIZE( grid%h2osoi_ice_s1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice_s1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice_s1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice_s2, 1 ) * SIZE( grid%h2osoi_ice_s2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice_s2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice_s2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice_s3, 1 ) * SIZE( grid%h2osoi_ice_s3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice_s3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice_s3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice_s4, 1 ) * SIZE( grid%h2osoi_ice_s4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice_s4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice_s4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice_s5, 1 ) * SIZE( grid%h2osoi_ice_s5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice_s5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice_s5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice1, 1 ) * SIZE( grid%h2osoi_ice1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice2, 1 ) * SIZE( grid%h2osoi_ice2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice3, 1 ) * SIZE( grid%h2osoi_ice3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice4, 1 ) * SIZE( grid%h2osoi_ice4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice5, 1 ) * SIZE( grid%h2osoi_ice5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice6, 1 ) * SIZE( grid%h2osoi_ice6, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice6,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice6,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice7, 1 ) * SIZE( grid%h2osoi_ice7, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice7,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice7,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice8, 1 ) * SIZE( grid%h2osoi_ice8, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice8,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice8,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice9, 1 ) * SIZE( grid%h2osoi_ice9, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice9,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice9,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice10, 1 ) * SIZE( grid%h2osoi_ice10, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice10,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_ice10,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno_s1, 1 ) * SIZE( grid%t_soisno_s1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno_s1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno_s1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno_s2, 1 ) * SIZE( grid%t_soisno_s2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno_s2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno_s2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno_s3, 1 ) * SIZE( grid%t_soisno_s3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno_s3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno_s3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno_s4, 1 ) * SIZE( grid%t_soisno_s4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno_s4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno_s4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno_s5, 1 ) * SIZE( grid%t_soisno_s5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno_s5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno_s5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno1, 1 ) * SIZE( grid%t_soisno1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno2, 1 ) * SIZE( grid%t_soisno2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno3, 1 ) * SIZE( grid%t_soisno3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno4, 1 ) * SIZE( grid%t_soisno4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno5, 1 ) * SIZE( grid%t_soisno5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno6, 1 ) * SIZE( grid%t_soisno6, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno6,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno6,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno7, 1 ) * SIZE( grid%t_soisno7, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno7,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno7,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno8, 1 ) * SIZE( grid%t_soisno8, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno8,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno8,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno9, 1 ) * SIZE( grid%t_soisno9, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno9,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno9,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno10, 1 ) * SIZE( grid%t_soisno10, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno10,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_soisno10,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%dzsnow1, 1 ) * SIZE( grid%dzsnow1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%dzsnow1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%dzsnow1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%dzsnow2, 1 ) * SIZE( grid%dzsnow2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%dzsnow2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%dzsnow2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%dzsnow3, 1 ) * SIZE( grid%dzsnow3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%dzsnow3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%dzsnow3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%dzsnow4, 1 ) * SIZE( grid%dzsnow4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%dzsnow4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%dzsnow4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%dzsnow5, 1 ) * SIZE( grid%dzsnow5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%dzsnow5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%dzsnow5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowrds1, 1 ) * SIZE( grid%snowrds1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowrds1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%snowrds1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowrds2, 1 ) * SIZE( grid%snowrds2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowrds2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%snowrds2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowrds3, 1 ) * SIZE( grid%snowrds3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowrds3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%snowrds3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowrds4, 1 ) * SIZE( grid%snowrds4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowrds4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%snowrds4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowrds5, 1 ) * SIZE( grid%snowrds5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowrds5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%snowrds5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake1, 1 ) * SIZE( grid%t_lake1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake2, 1 ) * SIZE( grid%t_lake2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake3, 1 ) * SIZE( grid%t_lake3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake4, 1 ) * SIZE( grid%t_lake4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake5, 1 ) * SIZE( grid%t_lake5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake6, 1 ) * SIZE( grid%t_lake6, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake6,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake6,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake7, 1 ) * SIZE( grid%t_lake7, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake7,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake7,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake8, 1 ) * SIZE( grid%t_lake8, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake8,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake8,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake9, 1 ) * SIZE( grid%t_lake9, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake9,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake9,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake10, 1 ) * SIZE( grid%t_lake10, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake10,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%t_lake10,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol1, 1 ) * SIZE( grid%h2osoi_vol1, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol1,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol1,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol2, 1 ) * SIZE( grid%h2osoi_vol2, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol2,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol2,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol3, 1 ) * SIZE( grid%h2osoi_vol3, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol3,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol3,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol4, 1 ) * SIZE( grid%h2osoi_vol4, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol4,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol4,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol5, 1 ) * SIZE( grid%h2osoi_vol5, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol5,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol5,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol6, 1 ) * SIZE( grid%h2osoi_vol6, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol6,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol6,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol7, 1 ) * SIZE( grid%h2osoi_vol7, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol7,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol7,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol8, 1 ) * SIZE( grid%h2osoi_vol8, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol8,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol8,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol9, 1 ) * SIZE( grid%h2osoi_vol9, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol9,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol9,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol10, 1 ) * SIZE( grid%h2osoi_vol10, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol10,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%h2osoi_vol10,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%albedosubgrid, 1 ) * SIZE( grid%albedosubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%albedosubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%albedosubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lhsubgrid, 1 ) * SIZE( grid%lhsubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lhsubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%lhsubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%hfxsubgrid, 1 ) * SIZE( grid%hfxsubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%hfxsubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%hfxsubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lwupsubgrid, 1 ) * SIZE( grid%lwupsubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lwupsubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%lwupsubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%q2subgrid, 1 ) * SIZE( grid%q2subgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%q2subgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%q2subgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sabvsubgrid, 1 ) * SIZE( grid%sabvsubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sabvsubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%sabvsubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sabgsubgrid, 1 ) * SIZE( grid%sabgsubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sabgsubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%sabgsubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%nrasubgrid, 1 ) * SIZE( grid%nrasubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%nrasubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%nrasubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%swupsubgrid, 1 ) * SIZE( grid%swupsubgrid, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%swupsubgrid,   &       
                 cids, cide, 1, config_flags%maxpatch, cjds, cjde,   &         
                 cims, cime, 1, config_flags%maxpatch, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%maxpatch, cjps, cjpe,   &         
                  ngrid%swupsubgrid,  &   
                 nids, nide, 1, config_flags%maxpatch, njds, njde,   &         
                 nims, nime, 1, config_flags%maxpatch, njms, njme,   &         
                 nips, nipe, 1, config_flags%maxpatch, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lakedepth2d, 1 ) * SIZE( grid%lakedepth2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lakedepth2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lakedepth2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%savedtke12d, 1 ) * SIZE( grid%savedtke12d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%savedtke12d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%savedtke12d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowdp2d, 1 ) * SIZE( grid%snowdp2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowdp2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snowdp2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osno2d, 1 ) * SIZE( grid%h2osno2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osno2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%h2osno2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snl2d, 1 ) * SIZE( grid%snl2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snl2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snl2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_grnd2d, 1 ) * SIZE( grid%t_grnd2d, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_grnd2d,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t_grnd2d,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_lake3d, 1 ) * SIZE( grid%t_lake3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_lake3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%t_lake3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lake_icefrac3d, 1 ) * SIZE( grid%lake_icefrac3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lake_icefrac3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%lake_icefrac3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%z_lake3d, 1 ) * SIZE( grid%z_lake3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%z_lake3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%z_lake3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%dz_lake3d, 1 ) * SIZE( grid%dz_lake3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%dz_lake3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%dz_lake3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t_soisno3d, 1 ) * SIZE( grid%t_soisno3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t_soisno3d,   &       
                 cids, cide, 1, 15, cjds, cjde,   &         
                 cims, cime, 1, 15, cjms, cjme,   &         
                 cips, cipe, 1, 15, cjps, cjpe,   &         
                  ngrid%t_soisno3d,  &   
                 nids, nide, 1, 15, njds, njde,   &         
                 nims, nime, 1, 15, njms, njme,   &         
                 nips, nipe, 1, 15, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_ice3d, 1 ) * SIZE( grid%h2osoi_ice3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_ice3d,   &       
                 cids, cide, 1, 15, cjds, cjde,   &         
                 cims, cime, 1, 15, cjms, cjme,   &         
                 cips, cipe, 1, 15, cjps, cjpe,   &         
                  ngrid%h2osoi_ice3d,  &   
                 nids, nide, 1, 15, njds, njde,   &         
                 nims, nime, 1, 15, njms, njme,   &         
                 nips, nipe, 1, 15, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_liq3d, 1 ) * SIZE( grid%h2osoi_liq3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_liq3d,   &       
                 cids, cide, 1, 15, cjds, cjde,   &         
                 cims, cime, 1, 15, cjms, cjme,   &         
                 cips, cipe, 1, 15, cjps, cjpe,   &         
                  ngrid%h2osoi_liq3d,  &   
                 nids, nide, 1, 15, njds, njde,   &         
                 nims, nime, 1, 15, njms, njme,   &         
                 nips, nipe, 1, 15, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%h2osoi_vol3d, 1 ) * SIZE( grid%h2osoi_vol3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%h2osoi_vol3d,   &       
                 cids, cide, 1, 15, cjds, cjde,   &         
                 cims, cime, 1, 15, cjms, cjme,   &         
                 cips, cipe, 1, 15, cjps, cjpe,   &         
                  ngrid%h2osoi_vol3d,  &   
                 nids, nide, 1, 15, njds, njde,   &         
                 nims, nime, 1, 15, njms, njme,   &         
                 nips, nipe, 1, 15, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%z3d, 1 ) * SIZE( grid%z3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%z3d,   &       
                 cids, cide, 1, 15, cjds, cjde,   &         
                 cims, cime, 1, 15, cjms, cjme,   &         
                 cips, cipe, 1, 15, cjps, cjpe,   &         
                  ngrid%z3d,  &   
                 nids, nide, 1, 15, njds, njde,   &         
                 nims, nime, 1, 15, njms, njme,   &         
                 nips, nipe, 1, 15, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%dz3d, 1 ) * SIZE( grid%dz3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%dz3d,   &       
                 cids, cide, 1, 15, cjds, cjde,   &         
                 cims, cime, 1, 15, cjms, cjme,   &         
                 cips, cipe, 1, 15, cjps, cjpe,   &         
                  ngrid%dz3d,  &   
                 nids, nide, 1, 15, njds, njde,   &         
                 nims, nime, 1, 15, njms, njme,   &         
                 nips, nipe, 1, 15, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%zi3d, 1 ) * SIZE( grid%zi3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%zi3d,   &       
                 cids, cide, 1, 16, cjds, cjde,   &         
                 cims, cime, 1, 16, cjms, cjme,   &         
                 cips, cipe, 1, 16, cjps, cjpe,   &         
                  ngrid%zi3d,  &   
                 nids, nide, 1, 16, njds, njde,   &         
                 nims, nime, 1, 16, njms, njme,   &         
                 nips, nipe, 1, 16, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%watsat3d, 1 ) * SIZE( grid%watsat3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%watsat3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%watsat3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%csol3d, 1 ) * SIZE( grid%csol3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%csol3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%csol3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tkmg3d, 1 ) * SIZE( grid%tkmg3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tkmg3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%tkmg3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tkdry3d, 1 ) * SIZE( grid%tkdry3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tkdry3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%tkdry3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tksatu3d, 1 ) * SIZE( grid%tksatu3d, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tksatu3d,   &       
                 cids, cide, 1, 10, cjds, cjde,   &         
                 cims, cime, 1, 10, cjms, cjme,   &         
                 cips, cipe, 1, 10, cjps, cjpe,   &         
                  ngrid%tksatu3d,  &   
                 nids, nide, 1, 10, njds, njde,   &         
                 nims, nime, 1, 10, njms, njme,   &         
                 nips, nipe, 1, 10, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%isnowxy, 1 ) * SIZE( grid%isnowxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcni (  &         
                  grid%isnowxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%isnowxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tvxy, 1 ) * SIZE( grid%tvxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tvxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tvxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tgxy, 1 ) * SIZE( grid%tgxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tgxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tgxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%canicexy, 1 ) * SIZE( grid%canicexy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%canicexy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%canicexy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%canliqxy, 1 ) * SIZE( grid%canliqxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%canliqxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%canliqxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%eahxy, 1 ) * SIZE( grid%eahxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%eahxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%eahxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tahxy, 1 ) * SIZE( grid%tahxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tahxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tahxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%cmxy, 1 ) * SIZE( grid%cmxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%cmxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%cmxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chxy, 1 ) * SIZE( grid%chxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fwetxy, 1 ) * SIZE( grid%fwetxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fwetxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%fwetxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sneqvoxy, 1 ) * SIZE( grid%sneqvoxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sneqvoxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%sneqvoxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%alboldxy, 1 ) * SIZE( grid%alboldxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%alboldxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%alboldxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qsnowxy, 1 ) * SIZE( grid%qsnowxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qsnowxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qsnowxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qrainxy, 1 ) * SIZE( grid%qrainxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qrainxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qrainxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%wslakexy, 1 ) * SIZE( grid%wslakexy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%wslakexy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%wslakexy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%zwtxy, 1 ) * SIZE( grid%zwtxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%zwtxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%zwtxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%waxy, 1 ) * SIZE( grid%waxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%waxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%waxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%wtxy, 1 ) * SIZE( grid%wtxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%wtxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%wtxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tsnoxy, 1 ) * SIZE( grid%tsnoxy, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tsnoxy,   &       
                 cids, cide, 1, config_flags%num_snow_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_snow_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_snow_layers, cjps, cjpe,   &         
                  ngrid%tsnoxy,  &   
                 nids, nide, 1, config_flags%num_snow_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_snow_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_snow_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%zsnsoxy, 1 ) * SIZE( grid%zsnsoxy, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%zsnsoxy,   &       
                 cids, cide, 1, config_flags%num_snso_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_snso_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_snso_layers, cjps, cjpe,   &         
                  ngrid%zsnsoxy,  &   
                 nids, nide, 1, config_flags%num_snso_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_snso_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_snso_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snicexy, 1 ) * SIZE( grid%snicexy, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snicexy,   &       
                 cids, cide, 1, config_flags%num_snow_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_snow_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_snow_layers, cjps, cjpe,   &         
                  ngrid%snicexy,  &   
                 nids, nide, 1, config_flags%num_snow_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_snow_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_snow_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snliqxy, 1 ) * SIZE( grid%snliqxy, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snliqxy,   &       
                 cids, cide, 1, config_flags%num_snow_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_snow_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_snow_layers, cjps, cjpe,   &         
                  ngrid%snliqxy,  &   
                 nids, nide, 1, config_flags%num_snow_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_snow_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_snow_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lfmassxy, 1 ) * SIZE( grid%lfmassxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lfmassxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lfmassxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rtmassxy, 1 ) * SIZE( grid%rtmassxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%rtmassxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rtmassxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%stmassxy, 1 ) * SIZE( grid%stmassxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%stmassxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%stmassxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%woodxy, 1 ) * SIZE( grid%woodxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%woodxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%woodxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%stblcpxy, 1 ) * SIZE( grid%stblcpxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%stblcpxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%stblcpxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fastcpxy, 1 ) * SIZE( grid%fastcpxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fastcpxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%fastcpxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%xsaixy, 1 ) * SIZE( grid%xsaixy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%xsaixy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%xsaixy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t2mvxy, 1 ) * SIZE( grid%t2mvxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t2mvxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t2mvxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%t2mbxy, 1 ) * SIZE( grid%t2mbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%t2mbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%t2mbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%q2mvxy, 1 ) * SIZE( grid%q2mvxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%q2mvxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%q2mvxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%q2mbxy, 1 ) * SIZE( grid%q2mbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%q2mbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%q2mbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tradxy, 1 ) * SIZE( grid%tradxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tradxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tradxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%neexy, 1 ) * SIZE( grid%neexy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%neexy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%neexy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%gppxy, 1 ) * SIZE( grid%gppxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%gppxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%gppxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%nppxy, 1 ) * SIZE( grid%nppxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%nppxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%nppxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fvegxy, 1 ) * SIZE( grid%fvegxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fvegxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%fvegxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qinxy, 1 ) * SIZE( grid%qinxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qinxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qinxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%runsfxy, 1 ) * SIZE( grid%runsfxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%runsfxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%runsfxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%runsbxy, 1 ) * SIZE( grid%runsbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%runsbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%runsbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ecanxy, 1 ) * SIZE( grid%ecanxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ecanxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ecanxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%edirxy, 1 ) * SIZE( grid%edirxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%edirxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%edirxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%etranxy, 1 ) * SIZE( grid%etranxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%etranxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%etranxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fsaxy, 1 ) * SIZE( grid%fsaxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fsaxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%fsaxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%firaxy, 1 ) * SIZE( grid%firaxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%firaxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%firaxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aparxy, 1 ) * SIZE( grid%aparxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%aparxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aparxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%psnxy, 1 ) * SIZE( grid%psnxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%psnxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%psnxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%savxy, 1 ) * SIZE( grid%savxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%savxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%savxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sagxy, 1 ) * SIZE( grid%sagxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sagxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%sagxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rssunxy, 1 ) * SIZE( grid%rssunxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%rssunxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rssunxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rsshaxy, 1 ) * SIZE( grid%rsshaxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%rsshaxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rsshaxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%bgapxy, 1 ) * SIZE( grid%bgapxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%bgapxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%bgapxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%wgapxy, 1 ) * SIZE( grid%wgapxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%wgapxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%wgapxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tgvxy, 1 ) * SIZE( grid%tgvxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tgvxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tgvxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%tgbxy, 1 ) * SIZE( grid%tgbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%tgbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%tgbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chvxy, 1 ) * SIZE( grid%chvxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chvxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chvxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chbxy, 1 ) * SIZE( grid%chbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%shgxy, 1 ) * SIZE( grid%shgxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%shgxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%shgxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%shcxy, 1 ) * SIZE( grid%shcxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%shcxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%shcxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%shbxy, 1 ) * SIZE( grid%shbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%shbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%shbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%evgxy, 1 ) * SIZE( grid%evgxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%evgxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%evgxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%evbxy, 1 ) * SIZE( grid%evbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%evbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%evbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ghvxy, 1 ) * SIZE( grid%ghvxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ghvxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ghvxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ghbxy, 1 ) * SIZE( grid%ghbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ghbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ghbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irgxy, 1 ) * SIZE( grid%irgxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irgxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irgxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ircxy, 1 ) * SIZE( grid%ircxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ircxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ircxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irbxy, 1 ) * SIZE( grid%irbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%trxy, 1 ) * SIZE( grid%trxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%trxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%trxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%evcxy, 1 ) * SIZE( grid%evcxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%evcxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%evcxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chleafxy, 1 ) * SIZE( grid%chleafxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chleafxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chleafxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chucxy, 1 ) * SIZE( grid%chucxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chucxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chucxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chv2xy, 1 ) * SIZE( grid%chv2xy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chv2xy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chv2xy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chb2xy, 1 ) * SIZE( grid%chb2xy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chb2xy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chb2xy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%chstarxy, 1 ) * SIZE( grid%chstarxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%chstarxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%chstarxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fdepthxy, 1 ) * SIZE( grid%fdepthxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fdepthxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%fdepthxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%eqzwt, 1 ) * SIZE( grid%eqzwt, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%eqzwt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%eqzwt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rechclim, 1 ) * SIZE( grid%rechclim, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%rechclim,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rechclim,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%riverbedxy, 1 ) * SIZE( grid%riverbedxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%riverbedxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%riverbedxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qintsxy, 1 ) * SIZE( grid%qintsxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qintsxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qintsxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qintrxy, 1 ) * SIZE( grid%qintrxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qintrxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qintrxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qdripsxy, 1 ) * SIZE( grid%qdripsxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qdripsxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qdripsxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qdriprxy, 1 ) * SIZE( grid%qdriprxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qdriprxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qdriprxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qthrosxy, 1 ) * SIZE( grid%qthrosxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qthrosxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qthrosxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qthrorxy, 1 ) * SIZE( grid%qthrorxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qthrorxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qthrorxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qsnsubxy, 1 ) * SIZE( grid%qsnsubxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qsnsubxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qsnsubxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qsnfroxy, 1 ) * SIZE( grid%qsnfroxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qsnfroxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qsnfroxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qsubcxy, 1 ) * SIZE( grid%qsubcxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qsubcxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qsubcxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qfrocxy, 1 ) * SIZE( grid%qfrocxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qfrocxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qfrocxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qevacxy, 1 ) * SIZE( grid%qevacxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qevacxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qevacxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qdewcxy, 1 ) * SIZE( grid%qdewcxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qdewcxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qdewcxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qfrzcxy, 1 ) * SIZE( grid%qfrzcxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qfrzcxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qfrzcxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qmeltcxy, 1 ) * SIZE( grid%qmeltcxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qmeltcxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qmeltcxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qsnbotxy, 1 ) * SIZE( grid%qsnbotxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qsnbotxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qsnbotxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qmeltxy, 1 ) * SIZE( grid%qmeltxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qmeltxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qmeltxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pondingxy, 1 ) * SIZE( grid%pondingxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%pondingxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%pondingxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pahxy, 1 ) * SIZE( grid%pahxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%pahxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%pahxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pahgxy, 1 ) * SIZE( grid%pahgxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%pahgxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%pahgxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pahvxy, 1 ) * SIZE( grid%pahvxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%pahvxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%pahvxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pahbxy, 1 ) * SIZE( grid%pahbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%pahbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%pahbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%canhsxy, 1 ) * SIZE( grid%canhsxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%canhsxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%canhsxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fpicexy, 1 ) * SIZE( grid%fpicexy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fpicexy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%fpicexy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%rainlsm, 1 ) * SIZE( grid%rainlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%rainlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%rainlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowlsm, 1 ) * SIZE( grid%snowlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snowlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilcomp, 1 ) * SIZE( grid%soilcomp, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%soilcomp,   &       
                 cids, cide, 1, 8, cjds, cjde,   &         
                 cims, cime, 1, 8, cjms, cjme,   &         
                 cips, cipe, 1, 8, cjps, cjpe,   &         
                  ngrid%soilcomp,  &   
                 nids, nide, 1, 8, njds, njde,   &         
                 nims, nime, 1, 8, njms, njme,   &         
                 nips, nipe, 1, 8, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilcl1, 1 ) * SIZE( grid%soilcl1, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%soilcl1,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%soilcl1,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilcl2, 1 ) * SIZE( grid%soilcl2, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%soilcl2,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%soilcl2,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilcl3, 1 ) * SIZE( grid%soilcl3, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%soilcl3,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%soilcl3,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilcl4, 1 ) * SIZE( grid%soilcl4, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%soilcl4,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%soilcl4,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acints, 1 ) * SIZE( grid%acints, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acints,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acints,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acintr, 1 ) * SIZE( grid%acintr, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acintr,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acintr,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acdripr, 1 ) * SIZE( grid%acdripr, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acdripr,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acdripr,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acthror, 1 ) * SIZE( grid%acthror, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acthror,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acthror,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acevac, 1 ) * SIZE( grid%acevac, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acevac,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acevac,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acdewc, 1 ) * SIZE( grid%acdewc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acdewc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acdewc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%forctlsm, 1 ) * SIZE( grid%forctlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%forctlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%forctlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%forcqlsm, 1 ) * SIZE( grid%forcqlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%forcqlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%forcqlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%forcplsm, 1 ) * SIZE( grid%forcplsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%forcplsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%forcplsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%forczlsm, 1 ) * SIZE( grid%forczlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%forczlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%forczlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%forcwlsm, 1 ) * SIZE( grid%forcwlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%forcwlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%forcwlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acrainlsm, 1 ) * SIZE( grid%acrainlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acrainlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acrainlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acrunsb, 1 ) * SIZE( grid%acrunsb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acrunsb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acrunsb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acrunsf, 1 ) * SIZE( grid%acrunsf, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acrunsf,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acrunsf,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acecan, 1 ) * SIZE( grid%acecan, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acecan,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acecan,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acetran, 1 ) * SIZE( grid%acetran, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acetran,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acetran,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acedir, 1 ) * SIZE( grid%acedir, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acedir,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acedir,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acetlsm, 1 ) * SIZE( grid%acetlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acetlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acetlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsnowlsm, 1 ) * SIZE( grid%acsnowlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsnowlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsnowlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsubc, 1 ) * SIZE( grid%acsubc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsubc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsubc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acfroc, 1 ) * SIZE( grid%acfroc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acfroc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acfroc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acfrzc, 1 ) * SIZE( grid%acfrzc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acfrzc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acfrzc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acmeltc, 1 ) * SIZE( grid%acmeltc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acmeltc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acmeltc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsnbot, 1 ) * SIZE( grid%acsnbot, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsnbot,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsnbot,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsnmelt, 1 ) * SIZE( grid%acsnmelt, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsnmelt,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsnmelt,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acponding, 1 ) * SIZE( grid%acponding, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acponding,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acponding,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsnsub, 1 ) * SIZE( grid%acsnsub, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsnsub,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsnsub,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsnfro, 1 ) * SIZE( grid%acsnfro, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsnfro,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsnfro,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acrainsnow, 1 ) * SIZE( grid%acrainsnow, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acrainsnow,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acrainsnow,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acdrips, 1 ) * SIZE( grid%acdrips, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acdrips,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acdrips,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acthros, 1 ) * SIZE( grid%acthros, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acthros,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acthros,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsagb, 1 ) * SIZE( grid%acsagb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsagb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsagb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acirb, 1 ) * SIZE( grid%acirb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acirb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acirb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acshb, 1 ) * SIZE( grid%acshb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acshb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acshb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acevb, 1 ) * SIZE( grid%acevb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acevb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acevb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acghb, 1 ) * SIZE( grid%acghb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acghb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acghb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acpahb, 1 ) * SIZE( grid%acpahb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acpahb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acpahb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsagv, 1 ) * SIZE( grid%acsagv, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsagv,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsagv,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acirg, 1 ) * SIZE( grid%acirg, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acirg,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acirg,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acshg, 1 ) * SIZE( grid%acshg, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acshg,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acshg,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acevg, 1 ) * SIZE( grid%acevg, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acevg,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acevg,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acghv, 1 ) * SIZE( grid%acghv, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acghv,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acghv,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acpahg, 1 ) * SIZE( grid%acpahg, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acpahg,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acpahg,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acsav, 1 ) * SIZE( grid%acsav, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acsav,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acsav,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acirc, 1 ) * SIZE( grid%acirc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acirc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acirc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acshc, 1 ) * SIZE( grid%acshc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acshc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acshc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acevc, 1 ) * SIZE( grid%acevc, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acevc,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acevc,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%actr, 1 ) * SIZE( grid%actr, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%actr,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%actr,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acpahv, 1 ) * SIZE( grid%acpahv, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acpahv,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acpahv,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswdnlsm, 1 ) * SIZE( grid%acswdnlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acswdnlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswdnlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acswuplsm, 1 ) * SIZE( grid%acswuplsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acswuplsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acswuplsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwdnlsm, 1 ) * SIZE( grid%aclwdnlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%aclwdnlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwdnlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclwuplsm, 1 ) * SIZE( grid%aclwuplsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%aclwuplsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclwuplsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acshflsm, 1 ) * SIZE( grid%acshflsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acshflsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acshflsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aclhflsm, 1 ) * SIZE( grid%aclhflsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%aclhflsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aclhflsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acghflsm, 1 ) * SIZE( grid%acghflsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acghflsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acghflsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acpahlsm, 1 ) * SIZE( grid%acpahlsm, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acpahlsm,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acpahlsm,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%accanhs, 1 ) * SIZE( grid%accanhs, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%accanhs,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%accanhs,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%soilenergy, 1 ) * SIZE( grid%soilenergy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%soilenergy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%soilenergy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%snowenergy, 1 ) * SIZE( grid%snowenergy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%snowenergy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%snowenergy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_ssoil, 1 ) * SIZE( grid%acc_ssoil, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_ssoil,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_ssoil,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_qinsur, 1 ) * SIZE( grid%acc_qinsur, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_qinsur,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_qinsur,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_qseva, 1 ) * SIZE( grid%acc_qseva, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_qseva,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_qseva,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_etrani, 1 ) * SIZE( grid%acc_etrani, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_etrani,   &       
                 cids, cide, 1, config_flags%num_soil_layers, cjds, cjde,   &         
                 cims, cime, 1, config_flags%num_soil_layers, cjms, cjme,   &         
                 cips, cipe, 1, config_flags%num_soil_layers, cjps, cjpe,   &         
                  ngrid%acc_etrani,  &   
                 nids, nide, 1, config_flags%num_soil_layers, njds, njde,   &         
                 nims, nime, 1, config_flags%num_soil_layers, njms, njme,   &         
                 nips, nipe, 1, config_flags%num_soil_layers, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%aceflxb, 1 ) * SIZE( grid%aceflxb, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%aceflxb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%aceflxb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%eflxbxy, 1 ) * SIZE( grid%eflxbxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%eflxbxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%eflxbxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_dwaterxy, 1 ) * SIZE( grid%acc_dwaterxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_dwaterxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_dwaterxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_prcpxy, 1 ) * SIZE( grid%acc_prcpxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_prcpxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_prcpxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_ecanxy, 1 ) * SIZE( grid%acc_ecanxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_ecanxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_ecanxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_etranxy, 1 ) * SIZE( grid%acc_etranxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_etranxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_etranxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%acc_edirxy, 1 ) * SIZE( grid%acc_edirxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%acc_edirxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%acc_edirxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%grainxy, 1 ) * SIZE( grid%grainxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%grainxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%grainxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%gddxy, 1 ) * SIZE( grid%gddxy, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%gddxy,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%gddxy,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%croptype, 1 ) * SIZE( grid%croptype, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%croptype,   &       
                 cids, cide, 1, 5, cjds, cjde,   &         
                 cims, cime, 1, 5, cjms, cjme,   &         
                 cips, cipe, 1, 5, cjps, cjpe,   &         
                  ngrid%croptype,  &   
                 nids, nide, 1, 5, njds, njde,   &         
                 nims, nime, 1, 5, njms, njme,   &         
                 nips, nipe, 1, 5, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%planting, 1 ) * SIZE( grid%planting, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%planting,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%planting,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%harvest, 1 ) * SIZE( grid%harvest, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%harvest,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%harvest,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%season_gdd, 1 ) * SIZE( grid%season_gdd, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%season_gdd,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%season_gdd,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%gecros_state, 1 ) * SIZE( grid%gecros_state, 3 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%gecros_state,   &       
                 cids, cide, 1, 60, cjds, cjde,   &         
                 cims, cime, 1, 60, cjms, cjme,   &         
                 cips, cipe, 1, 60, cjps, cjpe,   &         
                  ngrid%gecros_state,  &   
                 nids, nide, 1, 60, njds, njde,   &         
                 nims, nime, 1, 60, njms, njme,   &         
                 nips, nipe, 1, 60, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%td_fraction, 1 ) * SIZE( grid%td_fraction, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%td_fraction,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%td_fraction,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%qtdrain, 1 ) * SIZE( grid%qtdrain, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%qtdrain,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%qtdrain,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irfract, 1 ) * SIZE( grid%irfract, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irfract,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irfract,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%sifract, 1 ) * SIZE( grid%sifract, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%sifract,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%sifract,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%mifract, 1 ) * SIZE( grid%mifract, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%mifract,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%mifract,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%fifract, 1 ) * SIZE( grid%fifract, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%fifract,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%fifract,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irnumsi, 1 ) * SIZE( grid%irnumsi, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irnumsi,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irnumsi,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irnummi, 1 ) * SIZE( grid%irnummi, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irnummi,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irnummi,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irnumfi, 1 ) * SIZE( grid%irnumfi, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irnumfi,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irnumfi,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irsivol, 1 ) * SIZE( grid%irsivol, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irsivol,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irsivol,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irmivol, 1 ) * SIZE( grid%irmivol, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irmivol,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irmivol,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irfivol, 1 ) * SIZE( grid%irfivol, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irfivol,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irfivol,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%ireloss, 1 ) * SIZE( grid%ireloss, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%ireloss,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%ireloss,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%irrsplh, 1 ) * SIZE( grid%irrsplh, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%irrsplh,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%irrsplh,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pcb, 1 ) * SIZE( grid%pcb, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
                  grid%pcb,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%pcb,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%pc_2, 1 ) * SIZE( grid%pc_2, 2 ) .GT. 1 ) THEN 
CALL copy_fcn (  &         
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
                  ) 
ENDIF
IF ( SIZE( grid%landmask, 1 ) * SIZE( grid%landmask, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%landmask,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%landmask,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF
IF ( SIZE( grid%lakemask, 1 ) * SIZE( grid%lakemask, 2 ) .GT. 1 ) THEN 
CALL copy_fcnm (  &         
                  grid%lakemask,   &       
                 cids, cide, 1, 1, cjds, cjde,   &         
                 cims, cime, 1, 1, cjms, cjme,   &         
                 cips, cipe, 1, 1, cjps, cjpe,   &         
                  ngrid%lakemask,  &   
                 nids, nide, 1, 1, njds, njde,   &         
                 nims, nime, 1, 1, njms, njme,   &         
                 nips, nipe, 1, 1, njps, njpe,   &         
                  config_flags%shw, ngrid%imask_nostag,         &         
                  .FALSE., .FALSE.,                                                &         
                  ngrid%i_parent_start, ngrid%j_parent_start,                     &
                  ngrid%parent_grid_ratio, ngrid%parent_grid_ratio                &
                  ) 
ENDIF



      RETURN
   END SUBROUTINE feedback_domain_em_part1




