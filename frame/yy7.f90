!STARTOFREGISTRYGENERATEDINCLUDE 'inc/nl_config.inc'
!
! WARNING This file is generated automatically by use_registry
! using the data base in the file named Registry.
! Do not edit.  Your changes to this file will be lost.
!

SUBROUTINE nl_get_fs_firebrand_gen_dt ( id_id , fs_firebrand_gen_dt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_firebrand_gen_dt
  INTEGER id_id
  fs_firebrand_gen_dt = model_config_rec%fs_firebrand_gen_dt
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_dt
SUBROUTINE nl_get_fs_firebrand_gen_levels ( id_id , fs_firebrand_gen_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_firebrand_gen_levels
  INTEGER id_id
  fs_firebrand_gen_levels = model_config_rec%fs_firebrand_gen_levels
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_levels
SUBROUTINE nl_get_fs_firebrand_gen_maxhgt ( id_id , fs_firebrand_gen_maxhgt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_firebrand_gen_maxhgt
  INTEGER id_id
  fs_firebrand_gen_maxhgt = model_config_rec%fs_firebrand_gen_maxhgt
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_maxhgt
SUBROUTINE nl_get_fs_firebrand_gen_levrand ( id_id , fs_firebrand_gen_levrand )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fs_firebrand_gen_levrand
  INTEGER id_id
  fs_firebrand_gen_levrand = model_config_rec%fs_firebrand_gen_levrand
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_levrand
SUBROUTINE nl_get_fs_firebrand_gen_levrand_seed ( id_id , fs_firebrand_gen_levrand_seed )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_firebrand_gen_levrand_seed
  INTEGER id_id
  fs_firebrand_gen_levrand_seed = model_config_rec%fs_firebrand_gen_levrand_seed
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_levrand_seed
SUBROUTINE nl_get_fs_firebrand_gen_mom3d_dt ( id_id , fs_firebrand_gen_mom3d_dt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_firebrand_gen_mom3d_dt
  INTEGER id_id
  fs_firebrand_gen_mom3d_dt = model_config_rec%fs_firebrand_gen_mom3d_dt
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_mom3d_dt
SUBROUTINE nl_get_fs_firebrand_gen_prop_diam ( id_id , fs_firebrand_gen_prop_diam )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fs_firebrand_gen_prop_diam
  INTEGER id_id
  fs_firebrand_gen_prop_diam = model_config_rec%fs_firebrand_gen_prop_diam
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_prop_diam
SUBROUTINE nl_get_fs_firebrand_gen_prop_effd ( id_id , fs_firebrand_gen_prop_effd )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fs_firebrand_gen_prop_effd
  INTEGER id_id
  fs_firebrand_gen_prop_effd = model_config_rec%fs_firebrand_gen_prop_effd
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_prop_effd
SUBROUTINE nl_get_fs_firebrand_gen_prop_temp ( id_id , fs_firebrand_gen_prop_temp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fs_firebrand_gen_prop_temp
  INTEGER id_id
  fs_firebrand_gen_prop_temp = model_config_rec%fs_firebrand_gen_prop_temp
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_prop_temp
SUBROUTINE nl_get_fs_firebrand_gen_prop_tvel ( id_id , fs_firebrand_gen_prop_tvel )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fs_firebrand_gen_prop_tvel
  INTEGER id_id
  fs_firebrand_gen_prop_tvel = model_config_rec%fs_firebrand_gen_prop_tvel
  RETURN
END SUBROUTINE nl_get_fs_firebrand_gen_prop_tvel
SUBROUTINE nl_get_fs_firebrand_dens ( id_id , fs_firebrand_dens )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fs_firebrand_dens
  INTEGER id_id
  fs_firebrand_dens = model_config_rec%fs_firebrand_dens
  RETURN
END SUBROUTINE nl_get_fs_firebrand_dens
SUBROUTINE nl_get_fs_firebrand_dens_char ( id_id , fs_firebrand_dens_char )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fs_firebrand_dens_char
  INTEGER id_id
  fs_firebrand_dens_char = model_config_rec%fs_firebrand_dens_char
  RETURN
END SUBROUTINE nl_get_fs_firebrand_dens_char
SUBROUTINE nl_get_fs_firebrand_max_life_dt ( id_id , fs_firebrand_max_life_dt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: fs_firebrand_max_life_dt
  INTEGER id_id
  fs_firebrand_max_life_dt = model_config_rec%fs_firebrand_max_life_dt
  RETURN
END SUBROUTINE nl_get_fs_firebrand_max_life_dt
SUBROUTINE nl_get_fs_firebrand_land_hgt ( id_id , fs_firebrand_land_hgt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: fs_firebrand_land_hgt
  INTEGER id_id
  fs_firebrand_land_hgt = model_config_rec%fs_firebrand_land_hgt
  RETURN
END SUBROUTINE nl_get_fs_firebrand_land_hgt
SUBROUTINE nl_get_fuel_crosswalk ( id_id , fuel_crosswalk )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: fuel_crosswalk
  INTEGER id_id
  fuel_crosswalk = model_config_rec%fuel_crosswalk(id_id)
  RETURN
END SUBROUTINE nl_get_fuel_crosswalk
SUBROUTINE nl_get_trackember ( id_id , trackember )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: trackember
  INTEGER id_id
  trackember = model_config_rec%trackember
  RETURN
END SUBROUTINE nl_get_trackember
SUBROUTINE nl_get_do_avgflx_em ( id_id , do_avgflx_em )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: do_avgflx_em
  INTEGER id_id
  do_avgflx_em = model_config_rec%do_avgflx_em(id_id)
  RETURN
END SUBROUTINE nl_get_do_avgflx_em
SUBROUTINE nl_get_do_avgflx_cugd ( id_id , do_avgflx_cugd )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: do_avgflx_cugd
  INTEGER id_id
  do_avgflx_cugd = model_config_rec%do_avgflx_cugd(id_id)
  RETURN
END SUBROUTINE nl_get_do_avgflx_cugd
SUBROUTINE nl_get_multi_perturb ( id_id , multi_perturb )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: multi_perturb
  INTEGER id_id
  multi_perturb = model_config_rec%multi_perturb(id_id)
  RETURN
END SUBROUTINE nl_get_multi_perturb
SUBROUTINE nl_get_pert_farms ( id_id , pert_farms )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: pert_farms
  INTEGER id_id
  pert_farms = model_config_rec%pert_farms(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms
SUBROUTINE nl_get_pert_farms_albedo ( id_id , pert_farms_albedo )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_farms_albedo
  INTEGER id_id
  pert_farms_albedo = model_config_rec%pert_farms_albedo(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms_albedo
SUBROUTINE nl_get_pert_farms_aod ( id_id , pert_farms_aod )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_farms_aod
  INTEGER id_id
  pert_farms_aod = model_config_rec%pert_farms_aod(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms_aod
SUBROUTINE nl_get_pert_farms_angexp ( id_id , pert_farms_angexp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_farms_angexp
  INTEGER id_id
  pert_farms_angexp = model_config_rec%pert_farms_angexp(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms_angexp
SUBROUTINE nl_get_pert_farms_aerasy ( id_id , pert_farms_aerasy )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_farms_aerasy
  INTEGER id_id
  pert_farms_aerasy = model_config_rec%pert_farms_aerasy(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms_aerasy
SUBROUTINE nl_get_pert_farms_qv ( id_id , pert_farms_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_farms_qv
  INTEGER id_id
  pert_farms_qv = model_config_rec%pert_farms_qv(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms_qv
SUBROUTINE nl_get_pert_farms_qc ( id_id , pert_farms_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_farms_qc
  INTEGER id_id
  pert_farms_qc = model_config_rec%pert_farms_qc(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms_qc
SUBROUTINE nl_get_pert_farms_qs ( id_id , pert_farms_qs )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_farms_qs
  INTEGER id_id
  pert_farms_qs = model_config_rec%pert_farms_qs(id_id)
  RETURN
END SUBROUTINE nl_get_pert_farms_qs
SUBROUTINE nl_get_pert_deng ( id_id , pert_deng )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: pert_deng
  INTEGER id_id
  pert_deng = model_config_rec%pert_deng(id_id)
  RETURN
END SUBROUTINE nl_get_pert_deng
SUBROUTINE nl_get_pert_deng_qv ( id_id , pert_deng_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_deng_qv
  INTEGER id_id
  pert_deng_qv = model_config_rec%pert_deng_qv(id_id)
  RETURN
END SUBROUTINE nl_get_pert_deng_qv
SUBROUTINE nl_get_pert_deng_qc ( id_id , pert_deng_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_deng_qc
  INTEGER id_id
  pert_deng_qc = model_config_rec%pert_deng_qc(id_id)
  RETURN
END SUBROUTINE nl_get_pert_deng_qc
SUBROUTINE nl_get_pert_deng_t ( id_id , pert_deng_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_deng_t
  INTEGER id_id
  pert_deng_t = model_config_rec%pert_deng_t(id_id)
  RETURN
END SUBROUTINE nl_get_pert_deng_t
SUBROUTINE nl_get_pert_deng_w ( id_id , pert_deng_w )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_deng_w
  INTEGER id_id
  pert_deng_w = model_config_rec%pert_deng_w(id_id)
  RETURN
END SUBROUTINE nl_get_pert_deng_w
SUBROUTINE nl_get_pert_mynn ( id_id , pert_mynn )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: pert_mynn
  INTEGER id_id
  pert_mynn = model_config_rec%pert_mynn(id_id)
  RETURN
END SUBROUTINE nl_get_pert_mynn
SUBROUTINE nl_get_pert_mynn_qv ( id_id , pert_mynn_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_mynn_qv
  INTEGER id_id
  pert_mynn_qv = model_config_rec%pert_mynn_qv(id_id)
  RETURN
END SUBROUTINE nl_get_pert_mynn_qv
SUBROUTINE nl_get_pert_mynn_qc ( id_id , pert_mynn_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_mynn_qc
  INTEGER id_id
  pert_mynn_qc = model_config_rec%pert_mynn_qc(id_id)
  RETURN
END SUBROUTINE nl_get_pert_mynn_qc
SUBROUTINE nl_get_pert_mynn_t ( id_id , pert_mynn_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_mynn_t
  INTEGER id_id
  pert_mynn_t = model_config_rec%pert_mynn_t(id_id)
  RETURN
END SUBROUTINE nl_get_pert_mynn_t
SUBROUTINE nl_get_pert_mynn_qke ( id_id , pert_mynn_qke )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_mynn_qke
  INTEGER id_id
  pert_mynn_qke = model_config_rec%pert_mynn_qke(id_id)
  RETURN
END SUBROUTINE nl_get_pert_mynn_qke
SUBROUTINE nl_get_pert_noah ( id_id , pert_noah )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: pert_noah
  INTEGER id_id
  pert_noah = model_config_rec%pert_noah(id_id)
  RETURN
END SUBROUTINE nl_get_pert_noah
SUBROUTINE nl_get_pert_noah_qv ( id_id , pert_noah_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_noah_qv
  INTEGER id_id
  pert_noah_qv = model_config_rec%pert_noah_qv(id_id)
  RETURN
END SUBROUTINE nl_get_pert_noah_qv
SUBROUTINE nl_get_pert_noah_t ( id_id , pert_noah_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_noah_t
  INTEGER id_id
  pert_noah_t = model_config_rec%pert_noah_t(id_id)
  RETURN
END SUBROUTINE nl_get_pert_noah_t
SUBROUTINE nl_get_pert_noah_smois ( id_id , pert_noah_smois )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_noah_smois
  INTEGER id_id
  pert_noah_smois = model_config_rec%pert_noah_smois(id_id)
  RETURN
END SUBROUTINE nl_get_pert_noah_smois
SUBROUTINE nl_get_pert_noah_tslb ( id_id , pert_noah_tslb )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_noah_tslb
  INTEGER id_id
  pert_noah_tslb = model_config_rec%pert_noah_tslb(id_id)
  RETURN
END SUBROUTINE nl_get_pert_noah_tslb
SUBROUTINE nl_get_pert_thom ( id_id , pert_thom )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: pert_thom
  INTEGER id_id
  pert_thom = model_config_rec%pert_thom(id_id)
  RETURN
END SUBROUTINE nl_get_pert_thom
SUBROUTINE nl_get_pert_thom_qv ( id_id , pert_thom_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_thom_qv
  INTEGER id_id
  pert_thom_qv = model_config_rec%pert_thom_qv(id_id)
  RETURN
END SUBROUTINE nl_get_pert_thom_qv
SUBROUTINE nl_get_pert_thom_qc ( id_id , pert_thom_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_thom_qc
  INTEGER id_id
  pert_thom_qc = model_config_rec%pert_thom_qc(id_id)
  RETURN
END SUBROUTINE nl_get_pert_thom_qc
SUBROUTINE nl_get_pert_thom_qi ( id_id , pert_thom_qi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_thom_qi
  INTEGER id_id
  pert_thom_qi = model_config_rec%pert_thom_qi(id_id)
  RETURN
END SUBROUTINE nl_get_pert_thom_qi
SUBROUTINE nl_get_pert_thom_qs ( id_id , pert_thom_qs )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_thom_qs
  INTEGER id_id
  pert_thom_qs = model_config_rec%pert_thom_qs(id_id)
  RETURN
END SUBROUTINE nl_get_pert_thom_qs
SUBROUTINE nl_get_pert_thom_ni ( id_id , pert_thom_ni )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_thom_ni
  INTEGER id_id
  pert_thom_ni = model_config_rec%pert_thom_ni(id_id)
  RETURN
END SUBROUTINE nl_get_pert_thom_ni
SUBROUTINE nl_get_pert_cld3 ( id_id , pert_cld3 )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: pert_cld3
  INTEGER id_id
  pert_cld3 = model_config_rec%pert_cld3(id_id)
  RETURN
END SUBROUTINE nl_get_pert_cld3
SUBROUTINE nl_get_pert_cld3_qv ( id_id , pert_cld3_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_cld3_qv
  INTEGER id_id
  pert_cld3_qv = model_config_rec%pert_cld3_qv(id_id)
  RETURN
END SUBROUTINE nl_get_pert_cld3_qv
SUBROUTINE nl_get_pert_cld3_t ( id_id , pert_cld3_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: pert_cld3_t
  INTEGER id_id
  pert_cld3_t = model_config_rec%pert_cld3_t(id_id)
  RETURN
END SUBROUTINE nl_get_pert_cld3_t
SUBROUTINE nl_get_spdt ( id_id , spdt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: spdt
  INTEGER id_id
  spdt = model_config_rec%spdt(id_id)
  RETURN
END SUBROUTINE nl_get_spdt
SUBROUTINE nl_get_num_pert_3d ( id_id , num_pert_3d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: num_pert_3d
  INTEGER id_id
  num_pert_3d = model_config_rec%num_pert_3d
  RETURN
END SUBROUTINE nl_get_num_pert_3d
SUBROUTINE nl_get_nens ( id_id , nens )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: nens
  INTEGER id_id
  nens = model_config_rec%nens
  RETURN
END SUBROUTINE nl_get_nens
SUBROUTINE nl_get_skebs ( id_id , skebs )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: skebs
  INTEGER id_id
  skebs = model_config_rec%skebs(id_id)
  RETURN
END SUBROUTINE nl_get_skebs
SUBROUTINE nl_get_stoch_force_opt ( id_id , stoch_force_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: stoch_force_opt
  INTEGER id_id
  stoch_force_opt = model_config_rec%stoch_force_opt(id_id)
  RETURN
END SUBROUTINE nl_get_stoch_force_opt
SUBROUTINE nl_get_skebs_vertstruc ( id_id , skebs_vertstruc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: skebs_vertstruc
  INTEGER id_id
  skebs_vertstruc = model_config_rec%skebs_vertstruc
  RETURN
END SUBROUTINE nl_get_skebs_vertstruc
SUBROUTINE nl_get_stoch_vertstruc_opt ( id_id , stoch_vertstruc_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: stoch_vertstruc_opt
  INTEGER id_id
  stoch_vertstruc_opt = model_config_rec%stoch_vertstruc_opt(id_id)
  RETURN
END SUBROUTINE nl_get_stoch_vertstruc_opt
SUBROUTINE nl_get_tot_backscat_psi ( id_id , tot_backscat_psi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: tot_backscat_psi
  INTEGER id_id
  tot_backscat_psi = model_config_rec%tot_backscat_psi(id_id)
  RETURN
END SUBROUTINE nl_get_tot_backscat_psi
SUBROUTINE nl_get_tot_backscat_t ( id_id , tot_backscat_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: tot_backscat_t
  INTEGER id_id
  tot_backscat_t = model_config_rec%tot_backscat_t(id_id)
  RETURN
END SUBROUTINE nl_get_tot_backscat_t
SUBROUTINE nl_get_ztau_psi ( id_id , ztau_psi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: ztau_psi
  INTEGER id_id
  ztau_psi = model_config_rec%ztau_psi
  RETURN
END SUBROUTINE nl_get_ztau_psi
SUBROUTINE nl_get_ztau_t ( id_id , ztau_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: ztau_t
  INTEGER id_id
  ztau_t = model_config_rec%ztau_t
  RETURN
END SUBROUTINE nl_get_ztau_t
SUBROUTINE nl_get_rexponent_psi ( id_id , rexponent_psi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: rexponent_psi
  INTEGER id_id
  rexponent_psi = model_config_rec%rexponent_psi
  RETURN
END SUBROUTINE nl_get_rexponent_psi
SUBROUTINE nl_get_rexponent_t ( id_id , rexponent_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: rexponent_t
  INTEGER id_id
  rexponent_t = model_config_rec%rexponent_t
  RETURN
END SUBROUTINE nl_get_rexponent_t
SUBROUTINE nl_get_zsigma2_eps ( id_id , zsigma2_eps )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: zsigma2_eps
  INTEGER id_id
  zsigma2_eps = model_config_rec%zsigma2_eps
  RETURN
END SUBROUTINE nl_get_zsigma2_eps
SUBROUTINE nl_get_zsigma2_eta ( id_id , zsigma2_eta )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: zsigma2_eta
  INTEGER id_id
  zsigma2_eta = model_config_rec%zsigma2_eta
  RETURN
END SUBROUTINE nl_get_zsigma2_eta
SUBROUTINE nl_get_kminforc ( id_id , kminforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: kminforc
  INTEGER id_id
  kminforc = model_config_rec%kminforc
  RETURN
END SUBROUTINE nl_get_kminforc
SUBROUTINE nl_get_lminforc ( id_id , lminforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: lminforc
  INTEGER id_id
  lminforc = model_config_rec%lminforc
  RETURN
END SUBROUTINE nl_get_lminforc
SUBROUTINE nl_get_kminforct ( id_id , kminforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: kminforct
  INTEGER id_id
  kminforct = model_config_rec%kminforct
  RETURN
END SUBROUTINE nl_get_kminforct
SUBROUTINE nl_get_lminforct ( id_id , lminforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: lminforct
  INTEGER id_id
  lminforct = model_config_rec%lminforct
  RETURN
END SUBROUTINE nl_get_lminforct
SUBROUTINE nl_get_kmaxforc ( id_id , kmaxforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: kmaxforc
  INTEGER id_id
  kmaxforc = model_config_rec%kmaxforc
  RETURN
END SUBROUTINE nl_get_kmaxforc
SUBROUTINE nl_get_lmaxforc ( id_id , lmaxforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: lmaxforc
  INTEGER id_id
  lmaxforc = model_config_rec%lmaxforc
  RETURN
END SUBROUTINE nl_get_lmaxforc
SUBROUTINE nl_get_kmaxforct ( id_id , kmaxforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: kmaxforct
  INTEGER id_id
  kmaxforct = model_config_rec%kmaxforct
  RETURN
END SUBROUTINE nl_get_kmaxforct
SUBROUTINE nl_get_lmaxforct ( id_id , lmaxforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: lmaxforct
  INTEGER id_id
  lmaxforct = model_config_rec%lmaxforct
  RETURN
END SUBROUTINE nl_get_lmaxforct
SUBROUTINE nl_get_iseed_skebs ( id_id , iseed_skebs )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: iseed_skebs
  INTEGER id_id
  iseed_skebs = model_config_rec%iseed_skebs
  RETURN
END SUBROUTINE nl_get_iseed_skebs
SUBROUTINE nl_get_kmaxforch ( id_id , kmaxforch )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: kmaxforch
  INTEGER id_id
  kmaxforch = model_config_rec%kmaxforch
  RETURN
END SUBROUTINE nl_get_kmaxforch
SUBROUTINE nl_get_lmaxforch ( id_id , lmaxforch )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: lmaxforch
  INTEGER id_id
  lmaxforch = model_config_rec%lmaxforch
  RETURN
END SUBROUTINE nl_get_lmaxforch
SUBROUTINE nl_get_kmaxforcth ( id_id , kmaxforcth )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: kmaxforcth
  INTEGER id_id
  kmaxforcth = model_config_rec%kmaxforcth
  RETURN
END SUBROUTINE nl_get_kmaxforcth
SUBROUTINE nl_get_lmaxforcth ( id_id , lmaxforcth )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: lmaxforcth
  INTEGER id_id
  lmaxforcth = model_config_rec%lmaxforcth
  RETURN
END SUBROUTINE nl_get_lmaxforcth
SUBROUTINE nl_get_sppt ( id_id , sppt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sppt
  INTEGER id_id
  sppt = model_config_rec%sppt(id_id)
  RETURN
END SUBROUTINE nl_get_sppt
SUBROUTINE nl_get_gridpt_stddev_sppt ( id_id , gridpt_stddev_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: gridpt_stddev_sppt
  INTEGER id_id
  gridpt_stddev_sppt = model_config_rec%gridpt_stddev_sppt(id_id)
  RETURN
END SUBROUTINE nl_get_gridpt_stddev_sppt
SUBROUTINE nl_get_stddev_cutoff_sppt ( id_id , stddev_cutoff_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: stddev_cutoff_sppt
  INTEGER id_id
  stddev_cutoff_sppt = model_config_rec%stddev_cutoff_sppt(id_id)
  RETURN
END SUBROUTINE nl_get_stddev_cutoff_sppt
SUBROUTINE nl_get_lengthscale_sppt ( id_id , lengthscale_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: lengthscale_sppt
  INTEGER id_id
  lengthscale_sppt = model_config_rec%lengthscale_sppt(id_id)
  RETURN
END SUBROUTINE nl_get_lengthscale_sppt
SUBROUTINE nl_get_timescale_sppt ( id_id , timescale_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: timescale_sppt
  INTEGER id_id
  timescale_sppt = model_config_rec%timescale_sppt(id_id)
  RETURN
END SUBROUTINE nl_get_timescale_sppt
SUBROUTINE nl_get_sppt_vertstruc ( id_id , sppt_vertstruc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sppt_vertstruc
  INTEGER id_id
  sppt_vertstruc = model_config_rec%sppt_vertstruc
  RETURN
END SUBROUTINE nl_get_sppt_vertstruc
SUBROUTINE nl_get_iseed_sppt ( id_id , iseed_sppt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: iseed_sppt
  INTEGER id_id
  iseed_sppt = model_config_rec%iseed_sppt
  RETURN
END SUBROUTINE nl_get_iseed_sppt
SUBROUTINE nl_get_rand_perturb ( id_id , rand_perturb )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: rand_perturb
  INTEGER id_id
  rand_perturb = model_config_rec%rand_perturb(id_id)
  RETURN
END SUBROUTINE nl_get_rand_perturb
SUBROUTINE nl_get_gridpt_stddev_rand_pert ( id_id , gridpt_stddev_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: gridpt_stddev_rand_pert
  INTEGER id_id
  gridpt_stddev_rand_pert = model_config_rec%gridpt_stddev_rand_pert(id_id)
  RETURN
END SUBROUTINE nl_get_gridpt_stddev_rand_pert
SUBROUTINE nl_get_stddev_cutoff_rand_pert ( id_id , stddev_cutoff_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: stddev_cutoff_rand_pert
  INTEGER id_id
  stddev_cutoff_rand_pert = model_config_rec%stddev_cutoff_rand_pert(id_id)
  RETURN
END SUBROUTINE nl_get_stddev_cutoff_rand_pert
SUBROUTINE nl_get_lengthscale_rand_pert ( id_id , lengthscale_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: lengthscale_rand_pert
  INTEGER id_id
  lengthscale_rand_pert = model_config_rec%lengthscale_rand_pert(id_id)
  RETURN
END SUBROUTINE nl_get_lengthscale_rand_pert
SUBROUTINE nl_get_timescale_rand_pert ( id_id , timescale_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: timescale_rand_pert
  INTEGER id_id
  timescale_rand_pert = model_config_rec%timescale_rand_pert(id_id)
  RETURN
END SUBROUTINE nl_get_timescale_rand_pert
SUBROUTINE nl_get_rand_pert_vertstruc ( id_id , rand_pert_vertstruc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: rand_pert_vertstruc
  INTEGER id_id
  rand_pert_vertstruc = model_config_rec%rand_pert_vertstruc
  RETURN
END SUBROUTINE nl_get_rand_pert_vertstruc
SUBROUTINE nl_get_iseed_rand_pert ( id_id , iseed_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: iseed_rand_pert
  INTEGER id_id
  iseed_rand_pert = model_config_rec%iseed_rand_pert
  RETURN
END SUBROUTINE nl_get_iseed_rand_pert
SUBROUTINE nl_get_spp ( id_id , spp )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: spp
  INTEGER id_id
  spp = model_config_rec%spp(id_id)
  RETURN
END SUBROUTINE nl_get_spp
SUBROUTINE nl_get_hrrr_cycling ( id_id , hrrr_cycling )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: hrrr_cycling
  INTEGER id_id
  hrrr_cycling = model_config_rec%hrrr_cycling
  RETURN
END SUBROUTINE nl_get_hrrr_cycling
SUBROUTINE nl_get_spp_conv ( id_id , spp_conv )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: spp_conv
  INTEGER id_id
  spp_conv = model_config_rec%spp_conv(id_id)
  RETURN
END SUBROUTINE nl_get_spp_conv
SUBROUTINE nl_get_gridpt_stddev_spp_conv ( id_id , gridpt_stddev_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: gridpt_stddev_spp_conv
  INTEGER id_id
  gridpt_stddev_spp_conv = model_config_rec%gridpt_stddev_spp_conv(id_id)
  RETURN
END SUBROUTINE nl_get_gridpt_stddev_spp_conv
SUBROUTINE nl_get_stddev_cutoff_spp_conv ( id_id , stddev_cutoff_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: stddev_cutoff_spp_conv
  INTEGER id_id
  stddev_cutoff_spp_conv = model_config_rec%stddev_cutoff_spp_conv(id_id)
  RETURN
END SUBROUTINE nl_get_stddev_cutoff_spp_conv
SUBROUTINE nl_get_lengthscale_spp_conv ( id_id , lengthscale_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: lengthscale_spp_conv
  INTEGER id_id
  lengthscale_spp_conv = model_config_rec%lengthscale_spp_conv(id_id)
  RETURN
END SUBROUTINE nl_get_lengthscale_spp_conv
SUBROUTINE nl_get_timescale_spp_conv ( id_id , timescale_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: timescale_spp_conv
  INTEGER id_id
  timescale_spp_conv = model_config_rec%timescale_spp_conv(id_id)
  RETURN
END SUBROUTINE nl_get_timescale_spp_conv
SUBROUTINE nl_get_vertstruc_spp_conv ( id_id , vertstruc_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: vertstruc_spp_conv
  INTEGER id_id
  vertstruc_spp_conv = model_config_rec%vertstruc_spp_conv
  RETURN
END SUBROUTINE nl_get_vertstruc_spp_conv
SUBROUTINE nl_get_iseed_spp_conv ( id_id , iseed_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: iseed_spp_conv
  INTEGER id_id
  iseed_spp_conv = model_config_rec%iseed_spp_conv
  RETURN
END SUBROUTINE nl_get_iseed_spp_conv
SUBROUTINE nl_get_spp_pbl ( id_id , spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: spp_pbl
  INTEGER id_id
  spp_pbl = model_config_rec%spp_pbl(id_id)
  RETURN
END SUBROUTINE nl_get_spp_pbl
SUBROUTINE nl_get_gridpt_stddev_spp_pbl ( id_id , gridpt_stddev_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: gridpt_stddev_spp_pbl
  INTEGER id_id
  gridpt_stddev_spp_pbl = model_config_rec%gridpt_stddev_spp_pbl(id_id)
  RETURN
END SUBROUTINE nl_get_gridpt_stddev_spp_pbl
SUBROUTINE nl_get_stddev_cutoff_spp_pbl ( id_id , stddev_cutoff_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: stddev_cutoff_spp_pbl
  INTEGER id_id
  stddev_cutoff_spp_pbl = model_config_rec%stddev_cutoff_spp_pbl(id_id)
  RETURN
END SUBROUTINE nl_get_stddev_cutoff_spp_pbl
SUBROUTINE nl_get_lengthscale_spp_pbl ( id_id , lengthscale_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: lengthscale_spp_pbl
  INTEGER id_id
  lengthscale_spp_pbl = model_config_rec%lengthscale_spp_pbl(id_id)
  RETURN
END SUBROUTINE nl_get_lengthscale_spp_pbl
SUBROUTINE nl_get_timescale_spp_pbl ( id_id , timescale_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: timescale_spp_pbl
  INTEGER id_id
  timescale_spp_pbl = model_config_rec%timescale_spp_pbl(id_id)
  RETURN
END SUBROUTINE nl_get_timescale_spp_pbl
SUBROUTINE nl_get_vertstruc_spp_pbl ( id_id , vertstruc_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: vertstruc_spp_pbl
  INTEGER id_id
  vertstruc_spp_pbl = model_config_rec%vertstruc_spp_pbl
  RETURN
END SUBROUTINE nl_get_vertstruc_spp_pbl
SUBROUTINE nl_get_iseed_spp_pbl ( id_id , iseed_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: iseed_spp_pbl
  INTEGER id_id
  iseed_spp_pbl = model_config_rec%iseed_spp_pbl
  RETURN
END SUBROUTINE nl_get_iseed_spp_pbl
SUBROUTINE nl_get_spp_lsm ( id_id , spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: spp_lsm
  INTEGER id_id
  spp_lsm = model_config_rec%spp_lsm(id_id)
  RETURN
END SUBROUTINE nl_get_spp_lsm
SUBROUTINE nl_get_gridpt_stddev_spp_lsm ( id_id , gridpt_stddev_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: gridpt_stddev_spp_lsm
  INTEGER id_id
  gridpt_stddev_spp_lsm = model_config_rec%gridpt_stddev_spp_lsm(id_id)
  RETURN
END SUBROUTINE nl_get_gridpt_stddev_spp_lsm
SUBROUTINE nl_get_stddev_cutoff_spp_lsm ( id_id , stddev_cutoff_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: stddev_cutoff_spp_lsm
  INTEGER id_id
  stddev_cutoff_spp_lsm = model_config_rec%stddev_cutoff_spp_lsm(id_id)
  RETURN
END SUBROUTINE nl_get_stddev_cutoff_spp_lsm
SUBROUTINE nl_get_lengthscale_spp_lsm ( id_id , lengthscale_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: lengthscale_spp_lsm
  INTEGER id_id
  lengthscale_spp_lsm = model_config_rec%lengthscale_spp_lsm(id_id)
  RETURN
END SUBROUTINE nl_get_lengthscale_spp_lsm
SUBROUTINE nl_get_timescale_spp_lsm ( id_id , timescale_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: timescale_spp_lsm
  INTEGER id_id
  timescale_spp_lsm = model_config_rec%timescale_spp_lsm(id_id)
  RETURN
END SUBROUTINE nl_get_timescale_spp_lsm
SUBROUTINE nl_get_vertstruc_spp_lsm ( id_id , vertstruc_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: vertstruc_spp_lsm
  INTEGER id_id
  vertstruc_spp_lsm = model_config_rec%vertstruc_spp_lsm
  RETURN
END SUBROUTINE nl_get_vertstruc_spp_lsm
SUBROUTINE nl_get_iseed_spp_lsm ( id_id , iseed_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: iseed_spp_lsm
  INTEGER id_id
  iseed_spp_lsm = model_config_rec%iseed_spp_lsm
  RETURN
END SUBROUTINE nl_get_iseed_spp_lsm
SUBROUTINE nl_get_skebs_on ( id_id , skebs_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: skebs_on
  INTEGER id_id
  skebs_on = model_config_rec%skebs_on
  RETURN
END SUBROUTINE nl_get_skebs_on
SUBROUTINE nl_get_sppt_on ( id_id , sppt_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sppt_on
  INTEGER id_id
  sppt_on = model_config_rec%sppt_on
  RETURN
END SUBROUTINE nl_get_sppt_on
SUBROUTINE nl_get_spp_on ( id_id , spp_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: spp_on
  INTEGER id_id
  spp_on = model_config_rec%spp_on
  RETURN
END SUBROUTINE nl_get_spp_on
SUBROUTINE nl_get_rand_perturb_on ( id_id , rand_perturb_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: rand_perturb_on
  INTEGER id_id
  rand_perturb_on = model_config_rec%rand_perturb_on
  RETURN
END SUBROUTINE nl_get_rand_perturb_on
SUBROUTINE nl_get_num_stoch_levels ( id_id , num_stoch_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: num_stoch_levels
  INTEGER id_id
  num_stoch_levels = model_config_rec%num_stoch_levels
  RETURN
END SUBROUTINE nl_get_num_stoch_levels
SUBROUTINE nl_get_seed_dim ( id_id , seed_dim )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: seed_dim
  INTEGER id_id
  seed_dim = model_config_rec%seed_dim
  RETURN
END SUBROUTINE nl_get_seed_dim
SUBROUTINE nl_get_sfs_opt ( id_id , sfs_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sfs_opt
  INTEGER id_id
  sfs_opt = model_config_rec%sfs_opt(id_id)
  RETURN
END SUBROUTINE nl_get_sfs_opt
SUBROUTINE nl_get_m_opt ( id_id , m_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: m_opt
  INTEGER id_id
  m_opt = model_config_rec%m_opt(id_id)
  RETURN
END SUBROUTINE nl_get_m_opt
SUBROUTINE nl_get_lakedepth_default ( id_id , lakedepth_default )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: lakedepth_default
  INTEGER id_id
  lakedepth_default = model_config_rec%lakedepth_default(id_id)
  RETURN
END SUBROUTINE nl_get_lakedepth_default
SUBROUTINE nl_get_lake_min_elev ( id_id , lake_min_elev )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: lake_min_elev
  INTEGER id_id
  lake_min_elev = model_config_rec%lake_min_elev(id_id)
  RETURN
END SUBROUTINE nl_get_lake_min_elev
SUBROUTINE nl_get_use_lakedepth ( id_id , use_lakedepth )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: use_lakedepth
  INTEGER id_id
  use_lakedepth = model_config_rec%use_lakedepth(id_id)
  RETURN
END SUBROUTINE nl_get_use_lakedepth
SUBROUTINE nl_get_sbm_diagnostics ( id_id , sbm_diagnostics )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sbm_diagnostics
  INTEGER id_id
  sbm_diagnostics = model_config_rec%sbm_diagnostics(id_id)
  RETURN
END SUBROUTINE nl_get_sbm_diagnostics
SUBROUTINE nl_get_afwa_diag_opt ( id_id , afwa_diag_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_diag_opt
  INTEGER id_id
  afwa_diag_opt = model_config_rec%afwa_diag_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_diag_opt
SUBROUTINE nl_get_afwa_ptype_opt ( id_id , afwa_ptype_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_ptype_opt
  INTEGER id_id
  afwa_ptype_opt = model_config_rec%afwa_ptype_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_ptype_opt
SUBROUTINE nl_get_afwa_vil_opt ( id_id , afwa_vil_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_vil_opt
  INTEGER id_id
  afwa_vil_opt = model_config_rec%afwa_vil_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_vil_opt
SUBROUTINE nl_get_afwa_radar_opt ( id_id , afwa_radar_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_radar_opt
  INTEGER id_id
  afwa_radar_opt = model_config_rec%afwa_radar_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_radar_opt
SUBROUTINE nl_get_afwa_severe_opt ( id_id , afwa_severe_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_severe_opt
  INTEGER id_id
  afwa_severe_opt = model_config_rec%afwa_severe_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_severe_opt
SUBROUTINE nl_get_afwa_icing_opt ( id_id , afwa_icing_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_icing_opt
  INTEGER id_id
  afwa_icing_opt = model_config_rec%afwa_icing_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_icing_opt
SUBROUTINE nl_get_afwa_vis_opt ( id_id , afwa_vis_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_vis_opt
  INTEGER id_id
  afwa_vis_opt = model_config_rec%afwa_vis_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_vis_opt
SUBROUTINE nl_get_afwa_cloud_opt ( id_id , afwa_cloud_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_cloud_opt
  INTEGER id_id
  afwa_cloud_opt = model_config_rec%afwa_cloud_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_cloud_opt
SUBROUTINE nl_get_afwa_therm_opt ( id_id , afwa_therm_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_therm_opt
  INTEGER id_id
  afwa_therm_opt = model_config_rec%afwa_therm_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_therm_opt
SUBROUTINE nl_get_afwa_turb_opt ( id_id , afwa_turb_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_turb_opt
  INTEGER id_id
  afwa_turb_opt = model_config_rec%afwa_turb_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_turb_opt
SUBROUTINE nl_get_afwa_buoy_opt ( id_id , afwa_buoy_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_buoy_opt
  INTEGER id_id
  afwa_buoy_opt = model_config_rec%afwa_buoy_opt(id_id)
  RETURN
END SUBROUTINE nl_get_afwa_buoy_opt
SUBROUTINE nl_get_afwa_ptype_ccn_tmp ( id_id , afwa_ptype_ccn_tmp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: afwa_ptype_ccn_tmp
  INTEGER id_id
  afwa_ptype_ccn_tmp = model_config_rec%afwa_ptype_ccn_tmp
  RETURN
END SUBROUTINE nl_get_afwa_ptype_ccn_tmp
SUBROUTINE nl_get_afwa_ptype_tot_melt ( id_id , afwa_ptype_tot_melt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: afwa_ptype_tot_melt
  INTEGER id_id
  afwa_ptype_tot_melt = model_config_rec%afwa_ptype_tot_melt
  RETURN
END SUBROUTINE nl_get_afwa_ptype_tot_melt
SUBROUTINE nl_get_afwa_bad_data_check ( id_id , afwa_bad_data_check )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: afwa_bad_data_check
  INTEGER id_id
  afwa_bad_data_check = model_config_rec%afwa_bad_data_check
  RETURN
END SUBROUTINE nl_get_afwa_bad_data_check
SUBROUTINE nl_get_mean_diag ( id_id , mean_diag )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_diag
  INTEGER id_id
  mean_diag = model_config_rec%mean_diag
  RETURN
END SUBROUTINE nl_get_mean_diag
SUBROUTINE nl_get_mean_freq ( id_id , mean_freq )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_freq
  INTEGER id_id
  mean_freq = model_config_rec%mean_freq
  RETURN
END SUBROUTINE nl_get_mean_freq
SUBROUTINE nl_get_mean_interval ( id_id , mean_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_interval
  INTEGER id_id
  mean_interval = model_config_rec%mean_interval(id_id)
  RETURN
END SUBROUTINE nl_get_mean_interval
SUBROUTINE nl_get_mean_diag_interval ( id_id , mean_diag_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_diag_interval
  INTEGER id_id
  mean_diag_interval = model_config_rec%mean_diag_interval(id_id)
  RETURN
END SUBROUTINE nl_get_mean_diag_interval
SUBROUTINE nl_get_mean_diag_interval_s ( id_id , mean_diag_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_diag_interval_s
  INTEGER id_id
  mean_diag_interval_s = model_config_rec%mean_diag_interval_s(id_id)
  RETURN
END SUBROUTINE nl_get_mean_diag_interval_s
SUBROUTINE nl_get_mean_diag_interval_m ( id_id , mean_diag_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_diag_interval_m
  INTEGER id_id
  mean_diag_interval_m = model_config_rec%mean_diag_interval_m(id_id)
  RETURN
END SUBROUTINE nl_get_mean_diag_interval_m
SUBROUTINE nl_get_mean_diag_interval_h ( id_id , mean_diag_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_diag_interval_h
  INTEGER id_id
  mean_diag_interval_h = model_config_rec%mean_diag_interval_h(id_id)
  RETURN
END SUBROUTINE nl_get_mean_diag_interval_h
SUBROUTINE nl_get_mean_diag_interval_d ( id_id , mean_diag_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_diag_interval_d
  INTEGER id_id
  mean_diag_interval_d = model_config_rec%mean_diag_interval_d(id_id)
  RETURN
END SUBROUTINE nl_get_mean_diag_interval_d
SUBROUTINE nl_get_mean_diag_interval_mo ( id_id , mean_diag_interval_mo )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: mean_diag_interval_mo
  INTEGER id_id
  mean_diag_interval_mo = model_config_rec%mean_diag_interval_mo(id_id)
  RETURN
END SUBROUTINE nl_get_mean_diag_interval_mo
SUBROUTINE nl_get_diurnal_diag ( id_id , diurnal_diag )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: diurnal_diag
  INTEGER id_id
  diurnal_diag = model_config_rec%diurnal_diag
  RETURN
END SUBROUTINE nl_get_diurnal_diag
SUBROUTINE nl_get_nssl_ipelec ( id_id , nssl_ipelec )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: nssl_ipelec
  INTEGER id_id
  nssl_ipelec = model_config_rec%nssl_ipelec(id_id)
  RETURN
END SUBROUTINE nl_get_nssl_ipelec
SUBROUTINE nl_get_nssl_isaund ( id_id , nssl_isaund )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: nssl_isaund
  INTEGER id_id
  nssl_isaund = model_config_rec%nssl_isaund
  RETURN
END SUBROUTINE nl_get_nssl_isaund
SUBROUTINE nl_get_nssl_iscreen ( id_id , nssl_iscreen )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: nssl_iscreen
  INTEGER id_id
  nssl_iscreen = model_config_rec%nssl_iscreen
  RETURN
END SUBROUTINE nl_get_nssl_iscreen
SUBROUTINE nl_get_nssl_lightrad ( id_id , nssl_lightrad )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: nssl_lightrad
  INTEGER id_id
  nssl_lightrad = model_config_rec%nssl_lightrad
  RETURN
END SUBROUTINE nl_get_nssl_lightrad
SUBROUTINE nl_get_nssl_idischarge ( id_id , nssl_idischarge )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: nssl_idischarge
  INTEGER id_id
  nssl_idischarge = model_config_rec%nssl_idischarge
  RETURN
END SUBROUTINE nl_get_nssl_idischarge
SUBROUTINE nl_get_nssl_ibrkd ( id_id , nssl_ibrkd )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: nssl_ibrkd
  INTEGER id_id
  nssl_ibrkd = model_config_rec%nssl_ibrkd
  RETURN
END SUBROUTINE nl_get_nssl_ibrkd
SUBROUTINE nl_get_nssl_ecrit ( id_id , nssl_ecrit )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: nssl_ecrit
  INTEGER id_id
  nssl_ecrit = model_config_rec%nssl_ecrit
  RETURN
END SUBROUTINE nl_get_nssl_ecrit
SUBROUTINE nl_get_nssl_disfrac ( id_id , nssl_disfrac )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: nssl_disfrac
  INTEGER id_id
  nssl_disfrac = model_config_rec%nssl_disfrac
  RETURN
END SUBROUTINE nl_get_nssl_disfrac
SUBROUTINE nl_get_elec_physics ( id_id , elec_physics )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: elec_physics
  INTEGER id_id
  elec_physics = model_config_rec%elec_physics
  RETURN
END SUBROUTINE nl_get_elec_physics
SUBROUTINE nl_get_perturb_bdy ( id_id , perturb_bdy )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: perturb_bdy
  INTEGER id_id
  perturb_bdy = model_config_rec%perturb_bdy
  RETURN
END SUBROUTINE nl_get_perturb_bdy
SUBROUTINE nl_get_perturb_chem_bdy ( id_id , perturb_chem_bdy )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: perturb_chem_bdy
  INTEGER id_id
  perturb_chem_bdy = model_config_rec%perturb_chem_bdy
  RETURN
END SUBROUTINE nl_get_perturb_chem_bdy
SUBROUTINE nl_get_hybrid_opt ( id_id , hybrid_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: hybrid_opt
  INTEGER id_id
  hybrid_opt = model_config_rec%hybrid_opt
  RETURN
END SUBROUTINE nl_get_hybrid_opt
SUBROUTINE nl_get_etac ( id_id , etac )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: etac
  INTEGER id_id
  etac = model_config_rec%etac
  RETURN
END SUBROUTINE nl_get_etac
SUBROUTINE nl_get_num_wif_levels ( id_id , num_wif_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: num_wif_levels
  INTEGER id_id
  num_wif_levels = model_config_rec%num_wif_levels
  RETURN
END SUBROUTINE nl_get_num_wif_levels
SUBROUTINE nl_get_wif_input_opt ( id_id , wif_input_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: wif_input_opt
  INTEGER id_id
  wif_input_opt = model_config_rec%wif_input_opt
  RETURN
END SUBROUTINE nl_get_wif_input_opt
SUBROUTINE nl_get_diag_nwp2 ( id_id , diag_nwp2 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: diag_nwp2
  INTEGER id_id
  diag_nwp2 = model_config_rec%diag_nwp2
  RETURN
END SUBROUTINE nl_get_diag_nwp2
SUBROUTINE nl_get_solar_diagnostics ( id_id , solar_diagnostics )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: solar_diagnostics
  INTEGER id_id
  solar_diagnostics = model_config_rec%solar_diagnostics
  RETURN
END SUBROUTINE nl_get_solar_diagnostics
SUBROUTINE nl_get_p_lev_diags ( id_id , p_lev_diags )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: p_lev_diags
  INTEGER id_id
  p_lev_diags = model_config_rec%p_lev_diags
  RETURN
END SUBROUTINE nl_get_p_lev_diags
SUBROUTINE nl_get_p_lev_diags_dfi ( id_id , p_lev_diags_dfi )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: p_lev_diags_dfi
  INTEGER id_id
  p_lev_diags_dfi = model_config_rec%p_lev_diags_dfi
  RETURN
END SUBROUTINE nl_get_p_lev_diags_dfi
SUBROUTINE nl_get_num_press_levels ( id_id , num_press_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: num_press_levels
  INTEGER id_id
  num_press_levels = model_config_rec%num_press_levels
  RETURN
END SUBROUTINE nl_get_num_press_levels
SUBROUTINE nl_get_press_levels ( id_id , press_levels )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: press_levels
  INTEGER id_id
  press_levels = model_config_rec%press_levels(id_id)
  RETURN
END SUBROUTINE nl_get_press_levels
SUBROUTINE nl_get_use_tot_or_hyd_p ( id_id , use_tot_or_hyd_p )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: use_tot_or_hyd_p
  INTEGER id_id
  use_tot_or_hyd_p = model_config_rec%use_tot_or_hyd_p
  RETURN
END SUBROUTINE nl_get_use_tot_or_hyd_p
SUBROUTINE nl_get_extrap_below_grnd ( id_id , extrap_below_grnd )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: extrap_below_grnd
  INTEGER id_id
  extrap_below_grnd = model_config_rec%extrap_below_grnd
  RETURN
END SUBROUTINE nl_get_extrap_below_grnd
SUBROUTINE nl_get_p_lev_missing ( id_id , p_lev_missing )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: p_lev_missing
  INTEGER id_id
  p_lev_missing = model_config_rec%p_lev_missing
  RETURN
END SUBROUTINE nl_get_p_lev_missing
SUBROUTINE nl_get_p_lev_interval ( id_id , p_lev_interval )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: p_lev_interval
  INTEGER id_id
  p_lev_interval = model_config_rec%p_lev_interval(id_id)
  RETURN
END SUBROUTINE nl_get_p_lev_interval
SUBROUTINE nl_get_z_lev_diags ( id_id , z_lev_diags )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: z_lev_diags
  INTEGER id_id
  z_lev_diags = model_config_rec%z_lev_diags
  RETURN
END SUBROUTINE nl_get_z_lev_diags
SUBROUTINE nl_get_z_lev_diags_dfi ( id_id , z_lev_diags_dfi )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: z_lev_diags_dfi
  INTEGER id_id
  z_lev_diags_dfi = model_config_rec%z_lev_diags_dfi
  RETURN
END SUBROUTINE nl_get_z_lev_diags_dfi
SUBROUTINE nl_get_num_z_levels ( id_id , num_z_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: num_z_levels
  INTEGER id_id
  num_z_levels = model_config_rec%num_z_levels
  RETURN
END SUBROUTINE nl_get_num_z_levels
SUBROUTINE nl_get_z_levels ( id_id , z_levels )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: z_levels
  INTEGER id_id
  z_levels = model_config_rec%z_levels(id_id)
  RETURN
END SUBROUTINE nl_get_z_levels
SUBROUTINE nl_get_z_lev_missing ( id_id , z_lev_missing )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: z_lev_missing
  INTEGER id_id
  z_lev_missing = model_config_rec%z_lev_missing
  RETURN
END SUBROUTINE nl_get_z_lev_missing
SUBROUTINE nl_get_z_lev_interval ( id_id , z_lev_interval )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: z_lev_interval
  INTEGER id_id
  z_lev_interval = model_config_rec%z_lev_interval(id_id)
  RETURN
END SUBROUTINE nl_get_z_lev_interval
SUBROUTINE nl_get_iau ( id_id , iau )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: iau
  INTEGER id_id
  iau = model_config_rec%iau(id_id)
  RETURN
END SUBROUTINE nl_get_iau
SUBROUTINE nl_get_iau_time_window_sec ( id_id , iau_time_window_sec )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: iau_time_window_sec
  INTEGER id_id
  iau_time_window_sec = model_config_rec%iau_time_window_sec(id_id)
  RETURN
END SUBROUTINE nl_get_iau_time_window_sec
SUBROUTINE nl_get_wrf_cmaq_option ( id_id , wrf_cmaq_option )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: wrf_cmaq_option
  INTEGER id_id
  wrf_cmaq_option = model_config_rec%wrf_cmaq_option
  RETURN
END SUBROUTINE nl_get_wrf_cmaq_option
SUBROUTINE nl_get_wrf_cmaq_freq ( id_id , wrf_cmaq_freq )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: wrf_cmaq_freq
  INTEGER id_id
  wrf_cmaq_freq = model_config_rec%wrf_cmaq_freq
  RETURN
END SUBROUTINE nl_get_wrf_cmaq_freq
SUBROUTINE nl_get_met_file_tstep ( id_id , met_file_tstep )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: met_file_tstep
  INTEGER id_id
  met_file_tstep = model_config_rec%met_file_tstep
  RETURN
END SUBROUTINE nl_get_met_file_tstep
SUBROUTINE nl_get_direct_sw_feedback ( id_id , direct_sw_feedback )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: direct_sw_feedback
  INTEGER id_id
  direct_sw_feedback = model_config_rec%direct_sw_feedback
  RETURN
END SUBROUTINE nl_get_direct_sw_feedback
SUBROUTINE nl_get_feedback_restart ( id_id , feedback_restart )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: feedback_restart
  INTEGER id_id
  feedback_restart = model_config_rec%feedback_restart
  RETURN
END SUBROUTINE nl_get_feedback_restart
SUBROUTINE nl_get_sdirk3_jvp_checkpointing ( id_id , sdirk3_jvp_checkpointing )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_jvp_checkpointing
  INTEGER id_id
  sdirk3_jvp_checkpointing = model_config_rec%sdirk3_jvp_checkpointing
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_checkpointing
SUBROUTINE nl_get_sdirk3_jvp_checkpoint_segments ( id_id , sdirk3_jvp_checkpoint_segments )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_jvp_checkpoint_segments
  INTEGER id_id
  sdirk3_jvp_checkpoint_segments = model_config_rec%sdirk3_jvp_checkpoint_segments
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_checkpoint_segments
SUBROUTINE nl_get_sdirk3_jvp_graph_caching ( id_id , sdirk3_jvp_graph_caching )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_jvp_graph_caching
  INTEGER id_id
  sdirk3_jvp_graph_caching = model_config_rec%sdirk3_jvp_graph_caching
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_graph_caching
SUBROUTINE nl_get_sdirk3_jvp_batched ( id_id , sdirk3_jvp_batched )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_jvp_batched
  INTEGER id_id
  sdirk3_jvp_batched = model_config_rec%sdirk3_jvp_batched
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_batched
SUBROUTINE nl_get_sdirk3_jvp_batch_size ( id_id , sdirk3_jvp_batch_size )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_jvp_batch_size
  INTEGER id_id
  sdirk3_jvp_batch_size = model_config_rec%sdirk3_jvp_batch_size
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_batch_size
SUBROUTINE nl_get_sdirk3_jvp_mixed_precision ( id_id , sdirk3_jvp_mixed_precision )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_jvp_mixed_precision
  INTEGER id_id
  sdirk3_jvp_mixed_precision = model_config_rec%sdirk3_jvp_mixed_precision
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_mixed_precision
SUBROUTINE nl_get_sdirk3_implicit_acoustic ( id_id , sdirk3_implicit_acoustic )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_implicit_acoustic
  INTEGER id_id
  sdirk3_implicit_acoustic = model_config_rec%sdirk3_implicit_acoustic
  RETURN
END SUBROUTINE nl_get_sdirk3_implicit_acoustic
SUBROUTINE nl_get_sdirk3_implicit_gravity ( id_id , sdirk3_implicit_gravity )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_implicit_gravity
  INTEGER id_id
  sdirk3_implicit_gravity = model_config_rec%sdirk3_implicit_gravity
  RETURN
END SUBROUTINE nl_get_sdirk3_implicit_gravity
SUBROUTINE nl_get_sdirk3_implicit_rayleigh ( id_id , sdirk3_implicit_rayleigh )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_implicit_rayleigh
  INTEGER id_id
  sdirk3_implicit_rayleigh = model_config_rec%sdirk3_implicit_rayleigh
  RETURN
END SUBROUTINE nl_get_sdirk3_implicit_rayleigh
SUBROUTINE nl_get_sdirk3_implicit_wdamp ( id_id , sdirk3_implicit_wdamp )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_implicit_wdamp
  INTEGER id_id
  sdirk3_implicit_wdamp = model_config_rec%sdirk3_implicit_wdamp
  RETURN
END SUBROUTINE nl_get_sdirk3_implicit_wdamp
SUBROUTINE nl_get_sdirk3_implicit_vdiff ( id_id , sdirk3_implicit_vdiff )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_implicit_vdiff
  INTEGER id_id
  sdirk3_implicit_vdiff = model_config_rec%sdirk3_implicit_vdiff
  RETURN
END SUBROUTINE nl_get_sdirk3_implicit_vdiff
SUBROUTINE nl_get_sdirk3_implicit_hdiff ( id_id , sdirk3_implicit_hdiff )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_implicit_hdiff
  INTEGER id_id
  sdirk3_implicit_hdiff = model_config_rec%sdirk3_implicit_hdiff
  RETURN
END SUBROUTINE nl_get_sdirk3_implicit_hdiff
SUBROUTINE nl_get_sdirk3_implicit_divergence ( id_id , sdirk3_implicit_divergence )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_implicit_divergence
  INTEGER id_id
  sdirk3_implicit_divergence = model_config_rec%sdirk3_implicit_divergence
  RETURN
END SUBROUTINE nl_get_sdirk3_implicit_divergence
SUBROUTINE nl_get_sdirk3_precond_diagonal_only ( id_id , sdirk3_precond_diagonal_only )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_precond_diagonal_only
  INTEGER id_id
  sdirk3_precond_diagonal_only = model_config_rec%sdirk3_precond_diagonal_only
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_diagonal_only
SUBROUTINE nl_get_sdirk3_precond_block_jacobi ( id_id , sdirk3_precond_block_jacobi )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_precond_block_jacobi
  INTEGER id_id
  sdirk3_precond_block_jacobi = model_config_rec%sdirk3_precond_block_jacobi
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_block_jacobi
SUBROUTINE nl_get_sdirk3_precond_block_size ( id_id , sdirk3_precond_block_size )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_precond_block_size
  INTEGER id_id
  sdirk3_precond_block_size = model_config_rec%sdirk3_precond_block_size
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_block_size
SUBROUTINE nl_get_sdirk3_precond_ilu ( id_id , sdirk3_precond_ilu )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_precond_ilu
  INTEGER id_id
  sdirk3_precond_ilu = model_config_rec%sdirk3_precond_ilu
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_ilu
SUBROUTINE nl_get_sdirk3_precond_ilu_level ( id_id , sdirk3_precond_ilu_level )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_precond_ilu_level
  INTEGER id_id
  sdirk3_precond_ilu_level = model_config_rec%sdirk3_precond_ilu_level
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_ilu_level
SUBROUTINE nl_get_sdirk3_precond_multigrid ( id_id , sdirk3_precond_multigrid )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_precond_multigrid
  INTEGER id_id
  sdirk3_precond_multigrid = model_config_rec%sdirk3_precond_multigrid
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_multigrid
SUBROUTINE nl_get_sdirk3_precond_mg_levels ( id_id , sdirk3_precond_mg_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_precond_mg_levels
  INTEGER id_id
  sdirk3_precond_mg_levels = model_config_rec%sdirk3_precond_mg_levels
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_mg_levels
SUBROUTINE nl_get_sdirk3_nk_adaptive_tol ( id_id , sdirk3_nk_adaptive_tol )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_nk_adaptive_tol
  INTEGER id_id
  sdirk3_nk_adaptive_tol = model_config_rec%sdirk3_nk_adaptive_tol
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_adaptive_tol
SUBROUTINE nl_get_sdirk3_nk_forcing_eta_max ( id_id , sdirk3_nk_forcing_eta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_nk_forcing_eta_max
  INTEGER id_id
  sdirk3_nk_forcing_eta_max = model_config_rec%sdirk3_nk_forcing_eta_max
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_forcing_eta_max
SUBROUTINE nl_get_sdirk3_nk_forcing_eta_min ( id_id , sdirk3_nk_forcing_eta_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_nk_forcing_eta_min
  INTEGER id_id
  sdirk3_nk_forcing_eta_min = model_config_rec%sdirk3_nk_forcing_eta_min
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_forcing_eta_min
SUBROUTINE nl_get_sdirk3_nk_forcing_gamma ( id_id , sdirk3_nk_forcing_gamma )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_nk_forcing_gamma
  INTEGER id_id
  sdirk3_nk_forcing_gamma = model_config_rec%sdirk3_nk_forcing_gamma
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_forcing_gamma
SUBROUTINE nl_get_sdirk3_nk_forcing_alpha ( id_id , sdirk3_nk_forcing_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_nk_forcing_alpha
  INTEGER id_id
  sdirk3_nk_forcing_alpha = model_config_rec%sdirk3_nk_forcing_alpha
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_forcing_alpha
SUBROUTINE nl_get_sdirk3_nk_line_search ( id_id , sdirk3_nk_line_search )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_nk_line_search
  INTEGER id_id
  sdirk3_nk_line_search = model_config_rec%sdirk3_nk_line_search
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_line_search
SUBROUTINE nl_get_sdirk3_nk_trust_region ( id_id , sdirk3_nk_trust_region )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_nk_trust_region
  INTEGER id_id
  sdirk3_nk_trust_region = model_config_rec%sdirk3_nk_trust_region
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_trust_region
SUBROUTINE nl_get_sdirk3_nk_trust_radius ( id_id , sdirk3_nk_trust_radius )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_nk_trust_radius
  INTEGER id_id
  sdirk3_nk_trust_radius = model_config_rec%sdirk3_nk_trust_radius
  RETURN
END SUBROUTINE nl_get_sdirk3_nk_trust_radius
SUBROUTINE nl_get_sdirk3_stage2_gmres_restart ( id_id , sdirk3_stage2_gmres_restart )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_stage2_gmres_restart
  INTEGER id_id
  sdirk3_stage2_gmres_restart = model_config_rec%sdirk3_stage2_gmres_restart
  RETURN
END SUBROUTINE nl_get_sdirk3_stage2_gmres_restart
SUBROUTINE nl_get_sdirk3_stage2_max_krylov_restarts ( id_id , sdirk3_stage2_max_krylov_restarts )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_stage2_max_krylov_restarts
  INTEGER id_id
  sdirk3_stage2_max_krylov_restarts = model_config_rec%sdirk3_stage2_max_krylov_restarts
  RETURN
END SUBROUTINE nl_get_sdirk3_stage2_max_krylov_restarts
SUBROUTINE nl_get_sdirk3_stage2_krylov_tol ( id_id , sdirk3_stage2_krylov_tol )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_stage2_krylov_tol
  INTEGER id_id
  sdirk3_stage2_krylov_tol = model_config_rec%sdirk3_stage2_krylov_tol
  RETURN
END SUBROUTINE nl_get_sdirk3_stage2_krylov_tol
SUBROUTINE nl_get_sdirk3_stage2_ew_eta_min ( id_id , sdirk3_stage2_ew_eta_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_stage2_ew_eta_min
  INTEGER id_id
  sdirk3_stage2_ew_eta_min = model_config_rec%sdirk3_stage2_ew_eta_min
  RETURN
END SUBROUTINE nl_get_sdirk3_stage2_ew_eta_min
SUBROUTINE nl_get_sdirk3_stage2_ew_eta_max ( id_id , sdirk3_stage2_ew_eta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_stage2_ew_eta_max
  INTEGER id_id
  sdirk3_stage2_ew_eta_max = model_config_rec%sdirk3_stage2_ew_eta_max
  RETURN
END SUBROUTINE nl_get_sdirk3_stage2_ew_eta_max
SUBROUTINE nl_get_sdirk3_stage3_gmres_restart ( id_id , sdirk3_stage3_gmres_restart )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_stage3_gmres_restart
  INTEGER id_id
  sdirk3_stage3_gmres_restart = model_config_rec%sdirk3_stage3_gmres_restart
  RETURN
END SUBROUTINE nl_get_sdirk3_stage3_gmres_restart
SUBROUTINE nl_get_sdirk3_stage3_max_krylov_restarts ( id_id , sdirk3_stage3_max_krylov_restarts )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_stage3_max_krylov_restarts
  INTEGER id_id
  sdirk3_stage3_max_krylov_restarts = model_config_rec%sdirk3_stage3_max_krylov_restarts
  RETURN
END SUBROUTINE nl_get_sdirk3_stage3_max_krylov_restarts
SUBROUTINE nl_get_sdirk3_stage3_krylov_tol ( id_id , sdirk3_stage3_krylov_tol )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_stage3_krylov_tol
  INTEGER id_id
  sdirk3_stage3_krylov_tol = model_config_rec%sdirk3_stage3_krylov_tol
  RETURN
END SUBROUTINE nl_get_sdirk3_stage3_krylov_tol
SUBROUTINE nl_get_sdirk3_stage3_ew_eta_min ( id_id , sdirk3_stage3_ew_eta_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_stage3_ew_eta_min
  INTEGER id_id
  sdirk3_stage3_ew_eta_min = model_config_rec%sdirk3_stage3_ew_eta_min
  RETURN
END SUBROUTINE nl_get_sdirk3_stage3_ew_eta_min
SUBROUTINE nl_get_sdirk3_stage3_ew_eta_max ( id_id , sdirk3_stage3_ew_eta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_stage3_ew_eta_max
  INTEGER id_id
  sdirk3_stage3_ew_eta_max = model_config_rec%sdirk3_stage3_ew_eta_max
  RETURN
END SUBROUTINE nl_get_sdirk3_stage3_ew_eta_max
SUBROUTINE nl_get_sdirk3_imex_split_mode ( id_id , sdirk3_imex_split_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_imex_split_mode
  INTEGER id_id
  sdirk3_imex_split_mode = model_config_rec%sdirk3_imex_split_mode
  RETURN
END SUBROUTINE nl_get_sdirk3_imex_split_mode
SUBROUTINE nl_get_sdirk3_imex_slow_in_tangent ( id_id , sdirk3_imex_slow_in_tangent )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_imex_slow_in_tangent
  INTEGER id_id
  sdirk3_imex_slow_in_tangent = model_config_rec%sdirk3_imex_slow_in_tangent
  RETURN
END SUBROUTINE nl_get_sdirk3_imex_slow_in_tangent
SUBROUTINE nl_get_sdirk3_imex_phys_in_tangent ( id_id , sdirk3_imex_phys_in_tangent )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_imex_phys_in_tangent
  INTEGER id_id
  sdirk3_imex_phys_in_tangent = model_config_rec%sdirk3_imex_phys_in_tangent
  RETURN
END SUBROUTINE nl_get_sdirk3_imex_phys_in_tangent
SUBROUTINE nl_get_sdirk3_stage1_explicit ( id_id , sdirk3_stage1_explicit )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_stage1_explicit
  INTEGER id_id
  sdirk3_stage1_explicit = model_config_rec%sdirk3_stage1_explicit
  RETURN
END SUBROUTINE nl_get_sdirk3_stage1_explicit
SUBROUTINE nl_get_sdirk3_stage3_warmstart ( id_id , sdirk3_stage3_warmstart )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_stage3_warmstart
  INTEGER id_id
  sdirk3_stage3_warmstart = model_config_rec%sdirk3_stage3_warmstart
  RETURN
END SUBROUTINE nl_get_sdirk3_stage3_warmstart
SUBROUTINE nl_get_sdirk3_stage_fail_action ( id_id , sdirk3_stage_fail_action )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_stage_fail_action
  INTEGER id_id
  sdirk3_stage_fail_action = model_config_rec%sdirk3_stage_fail_action
  RETURN
END SUBROUTINE nl_get_sdirk3_stage_fail_action
SUBROUTINE nl_get_sdirk3_gate_metric_mode ( id_id , sdirk3_gate_metric_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_gate_metric_mode
  INTEGER id_id
  sdirk3_gate_metric_mode = model_config_rec%sdirk3_gate_metric_mode
  RETURN
END SUBROUTINE nl_get_sdirk3_gate_metric_mode
SUBROUTINE nl_get_sdirk3_stage3_gate_rel_threshold ( id_id , sdirk3_stage3_gate_rel_threshold )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_stage3_gate_rel_threshold
  INTEGER id_id
  sdirk3_stage3_gate_rel_threshold = model_config_rec%sdirk3_stage3_gate_rel_threshold
  RETURN
END SUBROUTINE nl_get_sdirk3_stage3_gate_rel_threshold
SUBROUTINE nl_get_sdirk3_hopeless_relax ( id_id , sdirk3_hopeless_relax )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_hopeless_relax
  INTEGER id_id
  sdirk3_hopeless_relax = model_config_rec%sdirk3_hopeless_relax
  RETURN
END SUBROUTINE nl_get_sdirk3_hopeless_relax
SUBROUTINE nl_get_sdirk3_precond_ratio_guard_warn_only ( id_id , sdirk3_precond_ratio_guard_warn_only )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_precond_ratio_guard_warn_only
  INTEGER id_id
  sdirk3_precond_ratio_guard_warn_only = model_config_rec%sdirk3_precond_ratio_guard_warn_only
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_ratio_guard_warn_only
SUBROUTINE nl_get_sdirk3_retain_graph_for_adjoint ( id_id , sdirk3_retain_graph_for_adjoint )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_retain_graph_for_adjoint
  INTEGER id_id
  sdirk3_retain_graph_for_adjoint = model_config_rec%sdirk3_retain_graph_for_adjoint
  RETURN
END SUBROUTINE nl_get_sdirk3_retain_graph_for_adjoint
SUBROUTINE nl_get_sdirk3_enable_ad_halo_exchange ( id_id , sdirk3_enable_ad_halo_exchange )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_enable_ad_halo_exchange
  INTEGER id_id
  sdirk3_enable_ad_halo_exchange = model_config_rec%sdirk3_enable_ad_halo_exchange
  RETURN
END SUBROUTINE nl_get_sdirk3_enable_ad_halo_exchange
SUBROUTINE nl_get_sdirk3_stage_boundary_sync ( id_id , sdirk3_stage_boundary_sync )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_stage_boundary_sync
  INTEGER id_id
  sdirk3_stage_boundary_sync = model_config_rec%sdirk3_stage_boundary_sync
  RETURN
END SUBROUTINE nl_get_sdirk3_stage_boundary_sync
SUBROUTINE nl_get_sdirk3_jvp_auto_bench_calls ( id_id , sdirk3_jvp_auto_bench_calls )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_jvp_auto_bench_calls
  INTEGER id_id
  sdirk3_jvp_auto_bench_calls = model_config_rec%sdirk3_jvp_auto_bench_calls
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_auto_bench_calls
SUBROUTINE nl_get_sdirk3_jvp_auto_bench_quality_gate ( id_id , sdirk3_jvp_auto_bench_quality_gate )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_jvp_auto_bench_quality_gate
  INTEGER id_id
  sdirk3_jvp_auto_bench_quality_gate = model_config_rec%sdirk3_jvp_auto_bench_quality_gate
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_auto_bench_quality_gate
SUBROUTINE nl_get_sdirk3_jvp_auto_bench_warmup ( id_id , sdirk3_jvp_auto_bench_warmup )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_jvp_auto_bench_warmup
  INTEGER id_id
  sdirk3_jvp_auto_bench_warmup = model_config_rec%sdirk3_jvp_auto_bench_warmup
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_auto_bench_warmup
SUBROUTINE nl_get_sdirk3_jvp_auto_bench_seed ( id_id , sdirk3_jvp_auto_bench_seed )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_jvp_auto_bench_seed
  INTEGER id_id
  sdirk3_jvp_auto_bench_seed = model_config_rec%sdirk3_jvp_auto_bench_seed
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_auto_bench_seed
SUBROUTINE nl_get_sdirk3_jvp_auto_bench_lock_reset_stage1 ( id_id , sdirk3_jvp_auto_bench_lock_reset_stage1 )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_jvp_auto_bench_lock_reset_stage1
  INTEGER id_id
  sdirk3_jvp_auto_bench_lock_reset_stage1 = model_config_rec%sdirk3_jvp_auto_bench_lock_reset_stage1
  RETURN
END SUBROUTINE nl_get_sdirk3_jvp_auto_bench_lock_reset_stage1
SUBROUTINE nl_get_sdirk3_solver_telemetry ( id_id , sdirk3_solver_telemetry )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_solver_telemetry
  INTEGER id_id
  sdirk3_solver_telemetry = model_config_rec%sdirk3_solver_telemetry
  RETURN
END SUBROUTINE nl_get_sdirk3_solver_telemetry
SUBROUTINE nl_get_sdirk3_progress_invariant_enable ( id_id , sdirk3_progress_invariant_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_progress_invariant_enable
  INTEGER id_id
  sdirk3_progress_invariant_enable = model_config_rec%sdirk3_progress_invariant_enable
  RETURN
END SUBROUTINE nl_get_sdirk3_progress_invariant_enable
SUBROUTINE nl_get_sdirk3_progress_invariant_min_ratio ( id_id , sdirk3_progress_invariant_min_ratio )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_progress_invariant_min_ratio
  INTEGER id_id
  sdirk3_progress_invariant_min_ratio = model_config_rec%sdirk3_progress_invariant_min_ratio
  RETURN
END SUBROUTINE nl_get_sdirk3_progress_invariant_min_ratio
SUBROUTINE nl_get_sdirk3_progress_invariant_streak_limit ( id_id , sdirk3_progress_invariant_streak_limit )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_progress_invariant_streak_limit
  INTEGER id_id
  sdirk3_progress_invariant_streak_limit = model_config_rec%sdirk3_progress_invariant_streak_limit
  RETURN
END SUBROUTINE nl_get_sdirk3_progress_invariant_streak_limit
SUBROUTINE nl_get_sdirk3_variable_pc_event_log ( id_id , sdirk3_variable_pc_event_log )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_variable_pc_event_log
  INTEGER id_id
  sdirk3_variable_pc_event_log = model_config_rec%sdirk3_variable_pc_event_log
  RETURN
END SUBROUTINE nl_get_sdirk3_variable_pc_event_log
SUBROUTINE nl_get_sdirk3_gmres_warmstart ( id_id , sdirk3_gmres_warmstart )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_gmres_warmstart
  INTEGER id_id
  sdirk3_gmres_warmstart = model_config_rec%sdirk3_gmres_warmstart
  RETURN
END SUBROUTINE nl_get_sdirk3_gmres_warmstart
SUBROUTINE nl_get_sdirk3_gmres_warmstart_quality_gate ( id_id , sdirk3_gmres_warmstart_quality_gate )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_gmres_warmstart_quality_gate
  INTEGER id_id
  sdirk3_gmres_warmstart_quality_gate = model_config_rec%sdirk3_gmres_warmstart_quality_gate
  RETURN
END SUBROUTINE nl_get_sdirk3_gmres_warmstart_quality_gate
SUBROUTINE nl_get_sdirk3_inn_warmstart_enable ( id_id , sdirk3_inn_warmstart_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_inn_warmstart_enable
  INTEGER id_id
  sdirk3_inn_warmstart_enable = model_config_rec%sdirk3_inn_warmstart_enable
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_warmstart_enable
SUBROUTINE nl_get_sdirk3_inn_residual_gate_enable ( id_id , sdirk3_inn_residual_gate_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_inn_residual_gate_enable
  INTEGER id_id
  sdirk3_inn_residual_gate_enable = model_config_rec%sdirk3_inn_residual_gate_enable
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_residual_gate_enable
SUBROUTINE nl_get_sdirk3_inn_q_min ( id_id , sdirk3_inn_q_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_inn_q_min
  INTEGER id_id
  sdirk3_inn_q_min = model_config_rec%sdirk3_inn_q_min
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_q_min
SUBROUTINE nl_get_sdirk3_inn_gate_rel_tol ( id_id , sdirk3_inn_gate_rel_tol )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_inn_gate_rel_tol
  INTEGER id_id
  sdirk3_inn_gate_rel_tol = model_config_rec%sdirk3_inn_gate_rel_tol
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_gate_rel_tol
SUBROUTINE nl_get_sdirk3_inn_gate_q_noise ( id_id , sdirk3_inn_gate_q_noise )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_inn_gate_q_noise
  INTEGER id_id
  sdirk3_inn_gate_q_noise = model_config_rec%sdirk3_inn_gate_q_noise
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_gate_q_noise
SUBROUTINE nl_get_sdirk3_inn_beta_max ( id_id , sdirk3_inn_beta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_inn_beta_max
  INTEGER id_id
  sdirk3_inn_beta_max = model_config_rec%sdirk3_inn_beta_max
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_beta_max
SUBROUTINE nl_get_sdirk3_inn_ood_guard_enable ( id_id , sdirk3_inn_ood_guard_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_inn_ood_guard_enable
  INTEGER id_id
  sdirk3_inn_ood_guard_enable = model_config_rec%sdirk3_inn_ood_guard_enable
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_ood_guard_enable
SUBROUTINE nl_get_sdirk3_inn_tol_ramp_enable ( id_id , sdirk3_inn_tol_ramp_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_inn_tol_ramp_enable
  INTEGER id_id
  sdirk3_inn_tol_ramp_enable = model_config_rec%sdirk3_inn_tol_ramp_enable
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_tol_ramp_enable
SUBROUTINE nl_get_sdirk3_inn_tol_ramp_gamma ( id_id , sdirk3_inn_tol_ramp_gamma )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_inn_tol_ramp_gamma
  INTEGER id_id
  sdirk3_inn_tol_ramp_gamma = model_config_rec%sdirk3_inn_tol_ramp_gamma
  RETURN
END SUBROUTINE nl_get_sdirk3_inn_tol_ramp_gamma
SUBROUTINE nl_get_sdirk3_rhs_bc_parity ( id_id , sdirk3_rhs_bc_parity )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_rhs_bc_parity
  INTEGER id_id
  sdirk3_rhs_bc_parity = model_config_rec%sdirk3_rhs_bc_parity
  RETURN
END SUBROUTINE nl_get_sdirk3_rhs_bc_parity
SUBROUTINE nl_get_sdirk3_mass_pgf_bc_guard ( id_id , sdirk3_mass_pgf_bc_guard )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_mass_pgf_bc_guard
  INTEGER id_id
  sdirk3_mass_pgf_bc_guard = model_config_rec%sdirk3_mass_pgf_bc_guard
  RETURN
END SUBROUTINE nl_get_sdirk3_mass_pgf_bc_guard
SUBROUTINE nl_get_sdirk3_precond_horizontal_smooth_alpha ( id_id , sdirk3_precond_horizontal_smooth_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_precond_horizontal_smooth_alpha
  INTEGER id_id
  sdirk3_precond_horizontal_smooth_alpha = model_config_rec%sdirk3_precond_horizontal_smooth_alpha
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_horizontal_smooth_alpha
SUBROUTINE nl_get_sdirk3_precond_horizontal_smooth_iters ( id_id , sdirk3_precond_horizontal_smooth_iters )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_precond_horizontal_smooth_iters
  INTEGER id_id
  sdirk3_precond_horizontal_smooth_iters = model_config_rec%sdirk3_precond_horizontal_smooth_iters
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_horizontal_smooth_iters
SUBROUTINE nl_get_sdirk3_precond_vertical_smooth_alpha ( id_id , sdirk3_precond_vertical_smooth_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_precond_vertical_smooth_alpha
  INTEGER id_id
  sdirk3_precond_vertical_smooth_alpha = model_config_rec%sdirk3_precond_vertical_smooth_alpha
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_vertical_smooth_alpha
SUBROUTINE nl_get_sdirk3_precond_smooth_boundary_guard ( id_id , sdirk3_precond_smooth_boundary_guard )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_precond_smooth_boundary_guard
  INTEGER id_id
  sdirk3_precond_smooth_boundary_guard = model_config_rec%sdirk3_precond_smooth_boundary_guard
  RETURN
END SUBROUTINE nl_get_sdirk3_precond_smooth_boundary_guard
SUBROUTINE nl_get_sdirk3_trust_region_block_aware_thresh ( id_id , sdirk3_trust_region_block_aware_thresh )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_trust_region_block_aware_thresh
  INTEGER id_id
  sdirk3_trust_region_block_aware_thresh = model_config_rec%sdirk3_trust_region_block_aware_thresh
  RETURN
END SUBROUTINE nl_get_sdirk3_trust_region_block_aware_thresh
SUBROUTINE nl_get_sdirk3_trust_fallback_relax ( id_id , sdirk3_trust_fallback_relax )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_trust_fallback_relax
  INTEGER id_id
  sdirk3_trust_fallback_relax = model_config_rec%sdirk3_trust_fallback_relax
  RETURN
END SUBROUTINE nl_get_sdirk3_trust_fallback_relax
SUBROUTINE nl_get_sdirk3_trust_fallback_ratio ( id_id , sdirk3_trust_fallback_ratio )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_trust_fallback_ratio
  INTEGER id_id
  sdirk3_trust_fallback_ratio = model_config_rec%sdirk3_trust_fallback_ratio
  RETURN
END SUBROUTINE nl_get_sdirk3_trust_fallback_ratio
SUBROUTINE nl_get_sdirk3_direct_u_solve_thresh ( id_id , sdirk3_direct_u_solve_thresh )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_direct_u_solve_thresh
  INTEGER id_id
  sdirk3_direct_u_solve_thresh = model_config_rec%sdirk3_direct_u_solve_thresh
  RETURN
END SUBROUTINE nl_get_sdirk3_direct_u_solve_thresh
SUBROUTINE nl_get_sdirk3_memory_pool ( id_id , sdirk3_memory_pool )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_memory_pool
  INTEGER id_id
  sdirk3_memory_pool = model_config_rec%sdirk3_memory_pool
  RETURN
END SUBROUTINE nl_get_sdirk3_memory_pool
SUBROUTINE nl_get_sdirk3_memory_pool_size_mb ( id_id , sdirk3_memory_pool_size_mb )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_memory_pool_size_mb
  INTEGER id_id
  sdirk3_memory_pool_size_mb = model_config_rec%sdirk3_memory_pool_size_mb
  RETURN
END SUBROUTINE nl_get_sdirk3_memory_pool_size_mb
SUBROUTINE nl_get_sdirk3_tensor_checkpointing ( id_id , sdirk3_tensor_checkpointing )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_tensor_checkpointing
  INTEGER id_id
  sdirk3_tensor_checkpointing = model_config_rec%sdirk3_tensor_checkpointing
  RETURN
END SUBROUTINE nl_get_sdirk3_tensor_checkpointing
SUBROUTINE nl_get_sdirk3_gradient_checkpointing ( id_id , sdirk3_gradient_checkpointing )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_gradient_checkpointing
  INTEGER id_id
  sdirk3_gradient_checkpointing = model_config_rec%sdirk3_gradient_checkpointing
  RETURN
END SUBROUTINE nl_get_sdirk3_gradient_checkpointing
SUBROUTINE nl_get_sdirk3_kdamp ( id_id , sdirk3_kdamp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_kdamp
  INTEGER id_id
  sdirk3_kdamp = model_config_rec%sdirk3_kdamp
  RETURN
END SUBROUTINE nl_get_sdirk3_kdamp
SUBROUTINE nl_get_sdirk3_khdif ( id_id , sdirk3_khdif )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_khdif
  INTEGER id_id
  sdirk3_khdif = model_config_rec%sdirk3_khdif
  RETURN
END SUBROUTINE nl_get_sdirk3_khdif
SUBROUTINE nl_get_sdirk3_kvdif ( id_id , sdirk3_kvdif )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_kvdif
  INTEGER id_id
  sdirk3_kvdif = model_config_rec%sdirk3_kvdif
  RETURN
END SUBROUTINE nl_get_sdirk3_kvdif
SUBROUTINE nl_get_sdirk3_w_damp_alpha ( id_id , sdirk3_w_damp_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_w_damp_alpha
  INTEGER id_id
  sdirk3_w_damp_alpha = model_config_rec%sdirk3_w_damp_alpha
  RETURN
END SUBROUTINE nl_get_sdirk3_w_damp_alpha
SUBROUTINE nl_get_sdirk3_w_crit_cfl ( id_id , sdirk3_w_crit_cfl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_w_crit_cfl
  INTEGER id_id
  sdirk3_w_crit_cfl = model_config_rec%sdirk3_w_crit_cfl
  RETURN
END SUBROUTINE nl_get_sdirk3_w_crit_cfl
SUBROUTINE nl_get_sdirk3_validation_level ( id_id , sdirk3_validation_level )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_validation_level
  INTEGER id_id
  sdirk3_validation_level = model_config_rec%sdirk3_validation_level
  RETURN
END SUBROUTINE nl_get_sdirk3_validation_level
SUBROUTINE nl_get_sdirk3_check_staggered ( id_id , sdirk3_check_staggered )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_check_staggered
  INTEGER id_id
  sdirk3_check_staggered = model_config_rec%sdirk3_check_staggered
  RETURN
END SUBROUTINE nl_get_sdirk3_check_staggered
SUBROUTINE nl_get_sdirk3_collect_stats ( id_id , sdirk3_collect_stats )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_collect_stats
  INTEGER id_id
  sdirk3_collect_stats = model_config_rec%sdirk3_collect_stats
  RETURN
END SUBROUTINE nl_get_sdirk3_collect_stats
SUBROUTINE nl_get_sdirk3_conserv_tol_mass ( id_id , sdirk3_conserv_tol_mass )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_conserv_tol_mass
  INTEGER id_id
  sdirk3_conserv_tol_mass = model_config_rec%sdirk3_conserv_tol_mass
  RETURN
END SUBROUTINE nl_get_sdirk3_conserv_tol_mass
SUBROUTINE nl_get_sdirk3_conserv_tol_energy ( id_id , sdirk3_conserv_tol_energy )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_conserv_tol_energy
  INTEGER id_id
  sdirk3_conserv_tol_energy = model_config_rec%sdirk3_conserv_tol_energy
  RETURN
END SUBROUTINE nl_get_sdirk3_conserv_tol_energy
SUBROUTINE nl_get_sdirk3_conserv_tol_momentum ( id_id , sdirk3_conserv_tol_momentum )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(OUT) :: sdirk3_conserv_tol_momentum
  INTEGER id_id
  sdirk3_conserv_tol_momentum = model_config_rec%sdirk3_conserv_tol_momentum
  RETURN
END SUBROUTINE nl_get_sdirk3_conserv_tol_momentum
SUBROUTINE nl_get_sdirk3_enable_benchmarking ( id_id , sdirk3_enable_benchmarking )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_enable_benchmarking
  INTEGER id_id
  sdirk3_enable_benchmarking = model_config_rec%sdirk3_enable_benchmarking
  RETURN
END SUBROUTINE nl_get_sdirk3_enable_benchmarking
SUBROUTINE nl_get_sdirk3_benchmark_warmup ( id_id , sdirk3_benchmark_warmup )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_benchmark_warmup
  INTEGER id_id
  sdirk3_benchmark_warmup = model_config_rec%sdirk3_benchmark_warmup
  RETURN
END SUBROUTINE nl_get_sdirk3_benchmark_warmup
SUBROUTINE nl_get_sdirk3_benchmark_measure ( id_id , sdirk3_benchmark_measure )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_benchmark_measure
  INTEGER id_id
  sdirk3_benchmark_measure = model_config_rec%sdirk3_benchmark_measure
  RETURN
END SUBROUTINE nl_get_sdirk3_benchmark_measure
SUBROUTINE nl_get_sdirk3_save_trajectory ( id_id , sdirk3_save_trajectory )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_save_trajectory
  INTEGER id_id
  sdirk3_save_trajectory = model_config_rec%sdirk3_save_trajectory
  RETURN
END SUBROUTINE nl_get_sdirk3_save_trajectory
SUBROUTINE nl_get_sdirk3_checkpoint_interval ( id_id , sdirk3_checkpoint_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_checkpoint_interval
  INTEGER id_id
  sdirk3_checkpoint_interval = model_config_rec%sdirk3_checkpoint_interval
  RETURN
END SUBROUTINE nl_get_sdirk3_checkpoint_interval
SUBROUTINE nl_get_sdirk3_obs_aware_4dvar ( id_id , sdirk3_obs_aware_4dvar )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(OUT) :: sdirk3_obs_aware_4dvar
  INTEGER id_id
  sdirk3_obs_aware_4dvar = model_config_rec%sdirk3_obs_aware_4dvar
  RETURN
END SUBROUTINE nl_get_sdirk3_obs_aware_4dvar
SUBROUTINE nl_get_sdirk3_obs_source_mode ( id_id , sdirk3_obs_source_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_obs_source_mode
  INTEGER id_id
  sdirk3_obs_source_mode = model_config_rec%sdirk3_obs_source_mode
  RETURN
END SUBROUTINE nl_get_sdirk3_obs_source_mode
SUBROUTINE nl_get_sdirk3_obs_window_sync_mode ( id_id , sdirk3_obs_window_sync_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: sdirk3_obs_window_sync_mode
  INTEGER id_id
  sdirk3_obs_window_sync_mode = model_config_rec%sdirk3_obs_window_sync_mode
  RETURN
END SUBROUTINE nl_get_sdirk3_obs_window_sync_mode
SUBROUTINE nl_get_chem_opt ( id_id , chem_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(OUT) :: chem_opt
  INTEGER id_id
  chem_opt = model_config_rec%chem_opt(id_id)
  RETURN
END SUBROUTINE nl_get_chem_opt

!ENDOFREGISTRYGENERATEDINCLUDE


