!STARTOFREGISTRYGENERATEDINCLUDE 'inc/nl_config.inc'
!
! WARNING This file is generated automatically by use_registry
! using the data base in the file named Registry.
! Do not edit.  Your changes to this file will be lost.
!

SUBROUTINE nl_set_fs_firebrand_gen_dt ( id_id , fs_firebrand_gen_dt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: fs_firebrand_gen_dt
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_dt = fs_firebrand_gen_dt 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_dt
SUBROUTINE nl_set_fs_firebrand_gen_levels ( id_id , fs_firebrand_gen_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: fs_firebrand_gen_levels
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_levels = fs_firebrand_gen_levels 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_levels
SUBROUTINE nl_set_fs_firebrand_gen_maxhgt ( id_id , fs_firebrand_gen_maxhgt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: fs_firebrand_gen_maxhgt
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_maxhgt = fs_firebrand_gen_maxhgt 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_maxhgt
SUBROUTINE nl_set_fs_firebrand_gen_levrand ( id_id , fs_firebrand_gen_levrand )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: fs_firebrand_gen_levrand
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_levrand = fs_firebrand_gen_levrand 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_levrand
SUBROUTINE nl_set_fs_firebrand_gen_levrand_seed ( id_id , fs_firebrand_gen_levrand_seed )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: fs_firebrand_gen_levrand_seed
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_levrand_seed = fs_firebrand_gen_levrand_seed 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_levrand_seed
SUBROUTINE nl_set_fs_firebrand_gen_mom3d_dt ( id_id , fs_firebrand_gen_mom3d_dt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: fs_firebrand_gen_mom3d_dt
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_mom3d_dt = fs_firebrand_gen_mom3d_dt 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_mom3d_dt
SUBROUTINE nl_set_fs_firebrand_gen_prop_diam ( id_id , fs_firebrand_gen_prop_diam )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: fs_firebrand_gen_prop_diam
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_prop_diam = fs_firebrand_gen_prop_diam 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_prop_diam
SUBROUTINE nl_set_fs_firebrand_gen_prop_effd ( id_id , fs_firebrand_gen_prop_effd )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: fs_firebrand_gen_prop_effd
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_prop_effd = fs_firebrand_gen_prop_effd 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_prop_effd
SUBROUTINE nl_set_fs_firebrand_gen_prop_temp ( id_id , fs_firebrand_gen_prop_temp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: fs_firebrand_gen_prop_temp
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_prop_temp = fs_firebrand_gen_prop_temp 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_prop_temp
SUBROUTINE nl_set_fs_firebrand_gen_prop_tvel ( id_id , fs_firebrand_gen_prop_tvel )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: fs_firebrand_gen_prop_tvel
  INTEGER id_id
  model_config_rec%fs_firebrand_gen_prop_tvel = fs_firebrand_gen_prop_tvel 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_gen_prop_tvel
SUBROUTINE nl_set_fs_firebrand_dens ( id_id , fs_firebrand_dens )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: fs_firebrand_dens
  INTEGER id_id
  model_config_rec%fs_firebrand_dens = fs_firebrand_dens 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_dens
SUBROUTINE nl_set_fs_firebrand_dens_char ( id_id , fs_firebrand_dens_char )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: fs_firebrand_dens_char
  INTEGER id_id
  model_config_rec%fs_firebrand_dens_char = fs_firebrand_dens_char 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_dens_char
SUBROUTINE nl_set_fs_firebrand_max_life_dt ( id_id , fs_firebrand_max_life_dt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: fs_firebrand_max_life_dt
  INTEGER id_id
  model_config_rec%fs_firebrand_max_life_dt = fs_firebrand_max_life_dt 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_max_life_dt
SUBROUTINE nl_set_fs_firebrand_land_hgt ( id_id , fs_firebrand_land_hgt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: fs_firebrand_land_hgt
  INTEGER id_id
  model_config_rec%fs_firebrand_land_hgt = fs_firebrand_land_hgt 
  RETURN
END SUBROUTINE nl_set_fs_firebrand_land_hgt
SUBROUTINE nl_set_fuel_crosswalk ( id_id , fuel_crosswalk )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: fuel_crosswalk
  INTEGER id_id
  model_config_rec%fuel_crosswalk(id_id) = fuel_crosswalk
  RETURN
END SUBROUTINE nl_set_fuel_crosswalk
SUBROUTINE nl_set_trackember ( id_id , trackember )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: trackember
  INTEGER id_id
  model_config_rec%trackember = trackember 
  RETURN
END SUBROUTINE nl_set_trackember
SUBROUTINE nl_set_do_avgflx_em ( id_id , do_avgflx_em )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: do_avgflx_em
  INTEGER id_id
  model_config_rec%do_avgflx_em(id_id) = do_avgflx_em
  RETURN
END SUBROUTINE nl_set_do_avgflx_em
SUBROUTINE nl_set_do_avgflx_cugd ( id_id , do_avgflx_cugd )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: do_avgflx_cugd
  INTEGER id_id
  model_config_rec%do_avgflx_cugd(id_id) = do_avgflx_cugd
  RETURN
END SUBROUTINE nl_set_do_avgflx_cugd
SUBROUTINE nl_set_multi_perturb ( id_id , multi_perturb )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: multi_perturb
  INTEGER id_id
  model_config_rec%multi_perturb(id_id) = multi_perturb
  RETURN
END SUBROUTINE nl_set_multi_perturb
SUBROUTINE nl_set_pert_farms ( id_id , pert_farms )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: pert_farms
  INTEGER id_id
  model_config_rec%pert_farms(id_id) = pert_farms
  RETURN
END SUBROUTINE nl_set_pert_farms
SUBROUTINE nl_set_pert_farms_albedo ( id_id , pert_farms_albedo )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_farms_albedo
  INTEGER id_id
  model_config_rec%pert_farms_albedo(id_id) = pert_farms_albedo
  RETURN
END SUBROUTINE nl_set_pert_farms_albedo
SUBROUTINE nl_set_pert_farms_aod ( id_id , pert_farms_aod )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_farms_aod
  INTEGER id_id
  model_config_rec%pert_farms_aod(id_id) = pert_farms_aod
  RETURN
END SUBROUTINE nl_set_pert_farms_aod
SUBROUTINE nl_set_pert_farms_angexp ( id_id , pert_farms_angexp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_farms_angexp
  INTEGER id_id
  model_config_rec%pert_farms_angexp(id_id) = pert_farms_angexp
  RETURN
END SUBROUTINE nl_set_pert_farms_angexp
SUBROUTINE nl_set_pert_farms_aerasy ( id_id , pert_farms_aerasy )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_farms_aerasy
  INTEGER id_id
  model_config_rec%pert_farms_aerasy(id_id) = pert_farms_aerasy
  RETURN
END SUBROUTINE nl_set_pert_farms_aerasy
SUBROUTINE nl_set_pert_farms_qv ( id_id , pert_farms_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_farms_qv
  INTEGER id_id
  model_config_rec%pert_farms_qv(id_id) = pert_farms_qv
  RETURN
END SUBROUTINE nl_set_pert_farms_qv
SUBROUTINE nl_set_pert_farms_qc ( id_id , pert_farms_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_farms_qc
  INTEGER id_id
  model_config_rec%pert_farms_qc(id_id) = pert_farms_qc
  RETURN
END SUBROUTINE nl_set_pert_farms_qc
SUBROUTINE nl_set_pert_farms_qs ( id_id , pert_farms_qs )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_farms_qs
  INTEGER id_id
  model_config_rec%pert_farms_qs(id_id) = pert_farms_qs
  RETURN
END SUBROUTINE nl_set_pert_farms_qs
SUBROUTINE nl_set_pert_deng ( id_id , pert_deng )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: pert_deng
  INTEGER id_id
  model_config_rec%pert_deng(id_id) = pert_deng
  RETURN
END SUBROUTINE nl_set_pert_deng
SUBROUTINE nl_set_pert_deng_qv ( id_id , pert_deng_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_deng_qv
  INTEGER id_id
  model_config_rec%pert_deng_qv(id_id) = pert_deng_qv
  RETURN
END SUBROUTINE nl_set_pert_deng_qv
SUBROUTINE nl_set_pert_deng_qc ( id_id , pert_deng_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_deng_qc
  INTEGER id_id
  model_config_rec%pert_deng_qc(id_id) = pert_deng_qc
  RETURN
END SUBROUTINE nl_set_pert_deng_qc
SUBROUTINE nl_set_pert_deng_t ( id_id , pert_deng_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_deng_t
  INTEGER id_id
  model_config_rec%pert_deng_t(id_id) = pert_deng_t
  RETURN
END SUBROUTINE nl_set_pert_deng_t
SUBROUTINE nl_set_pert_deng_w ( id_id , pert_deng_w )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_deng_w
  INTEGER id_id
  model_config_rec%pert_deng_w(id_id) = pert_deng_w
  RETURN
END SUBROUTINE nl_set_pert_deng_w
SUBROUTINE nl_set_pert_mynn ( id_id , pert_mynn )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: pert_mynn
  INTEGER id_id
  model_config_rec%pert_mynn(id_id) = pert_mynn
  RETURN
END SUBROUTINE nl_set_pert_mynn
SUBROUTINE nl_set_pert_mynn_qv ( id_id , pert_mynn_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_mynn_qv
  INTEGER id_id
  model_config_rec%pert_mynn_qv(id_id) = pert_mynn_qv
  RETURN
END SUBROUTINE nl_set_pert_mynn_qv
SUBROUTINE nl_set_pert_mynn_qc ( id_id , pert_mynn_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_mynn_qc
  INTEGER id_id
  model_config_rec%pert_mynn_qc(id_id) = pert_mynn_qc
  RETURN
END SUBROUTINE nl_set_pert_mynn_qc
SUBROUTINE nl_set_pert_mynn_t ( id_id , pert_mynn_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_mynn_t
  INTEGER id_id
  model_config_rec%pert_mynn_t(id_id) = pert_mynn_t
  RETURN
END SUBROUTINE nl_set_pert_mynn_t
SUBROUTINE nl_set_pert_mynn_qke ( id_id , pert_mynn_qke )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_mynn_qke
  INTEGER id_id
  model_config_rec%pert_mynn_qke(id_id) = pert_mynn_qke
  RETURN
END SUBROUTINE nl_set_pert_mynn_qke
SUBROUTINE nl_set_pert_noah ( id_id , pert_noah )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: pert_noah
  INTEGER id_id
  model_config_rec%pert_noah(id_id) = pert_noah
  RETURN
END SUBROUTINE nl_set_pert_noah
SUBROUTINE nl_set_pert_noah_qv ( id_id , pert_noah_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_noah_qv
  INTEGER id_id
  model_config_rec%pert_noah_qv(id_id) = pert_noah_qv
  RETURN
END SUBROUTINE nl_set_pert_noah_qv
SUBROUTINE nl_set_pert_noah_t ( id_id , pert_noah_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_noah_t
  INTEGER id_id
  model_config_rec%pert_noah_t(id_id) = pert_noah_t
  RETURN
END SUBROUTINE nl_set_pert_noah_t
SUBROUTINE nl_set_pert_noah_smois ( id_id , pert_noah_smois )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_noah_smois
  INTEGER id_id
  model_config_rec%pert_noah_smois(id_id) = pert_noah_smois
  RETURN
END SUBROUTINE nl_set_pert_noah_smois
SUBROUTINE nl_set_pert_noah_tslb ( id_id , pert_noah_tslb )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_noah_tslb
  INTEGER id_id
  model_config_rec%pert_noah_tslb(id_id) = pert_noah_tslb
  RETURN
END SUBROUTINE nl_set_pert_noah_tslb
SUBROUTINE nl_set_pert_thom ( id_id , pert_thom )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: pert_thom
  INTEGER id_id
  model_config_rec%pert_thom(id_id) = pert_thom
  RETURN
END SUBROUTINE nl_set_pert_thom
SUBROUTINE nl_set_pert_thom_qv ( id_id , pert_thom_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_thom_qv
  INTEGER id_id
  model_config_rec%pert_thom_qv(id_id) = pert_thom_qv
  RETURN
END SUBROUTINE nl_set_pert_thom_qv
SUBROUTINE nl_set_pert_thom_qc ( id_id , pert_thom_qc )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_thom_qc
  INTEGER id_id
  model_config_rec%pert_thom_qc(id_id) = pert_thom_qc
  RETURN
END SUBROUTINE nl_set_pert_thom_qc
SUBROUTINE nl_set_pert_thom_qi ( id_id , pert_thom_qi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_thom_qi
  INTEGER id_id
  model_config_rec%pert_thom_qi(id_id) = pert_thom_qi
  RETURN
END SUBROUTINE nl_set_pert_thom_qi
SUBROUTINE nl_set_pert_thom_qs ( id_id , pert_thom_qs )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_thom_qs
  INTEGER id_id
  model_config_rec%pert_thom_qs(id_id) = pert_thom_qs
  RETURN
END SUBROUTINE nl_set_pert_thom_qs
SUBROUTINE nl_set_pert_thom_ni ( id_id , pert_thom_ni )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_thom_ni
  INTEGER id_id
  model_config_rec%pert_thom_ni(id_id) = pert_thom_ni
  RETURN
END SUBROUTINE nl_set_pert_thom_ni
SUBROUTINE nl_set_pert_cld3 ( id_id , pert_cld3 )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: pert_cld3
  INTEGER id_id
  model_config_rec%pert_cld3(id_id) = pert_cld3
  RETURN
END SUBROUTINE nl_set_pert_cld3
SUBROUTINE nl_set_pert_cld3_qv ( id_id , pert_cld3_qv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_cld3_qv
  INTEGER id_id
  model_config_rec%pert_cld3_qv(id_id) = pert_cld3_qv
  RETURN
END SUBROUTINE nl_set_pert_cld3_qv
SUBROUTINE nl_set_pert_cld3_t ( id_id , pert_cld3_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: pert_cld3_t
  INTEGER id_id
  model_config_rec%pert_cld3_t(id_id) = pert_cld3_t
  RETURN
END SUBROUTINE nl_set_pert_cld3_t
SUBROUTINE nl_set_spdt ( id_id , spdt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: spdt
  INTEGER id_id
  model_config_rec%spdt(id_id) = spdt
  RETURN
END SUBROUTINE nl_set_spdt
SUBROUTINE nl_set_num_pert_3d ( id_id , num_pert_3d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: num_pert_3d
  INTEGER id_id
  model_config_rec%num_pert_3d = num_pert_3d 
  RETURN
END SUBROUTINE nl_set_num_pert_3d
SUBROUTINE nl_set_nens ( id_id , nens )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: nens
  INTEGER id_id
  model_config_rec%nens = nens 
  RETURN
END SUBROUTINE nl_set_nens
SUBROUTINE nl_set_skebs ( id_id , skebs )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: skebs
  INTEGER id_id
  model_config_rec%skebs(id_id) = skebs
  RETURN
END SUBROUTINE nl_set_skebs
SUBROUTINE nl_set_stoch_force_opt ( id_id , stoch_force_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: stoch_force_opt
  INTEGER id_id
  model_config_rec%stoch_force_opt(id_id) = stoch_force_opt
  RETURN
END SUBROUTINE nl_set_stoch_force_opt
SUBROUTINE nl_set_skebs_vertstruc ( id_id , skebs_vertstruc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: skebs_vertstruc
  INTEGER id_id
  model_config_rec%skebs_vertstruc = skebs_vertstruc 
  RETURN
END SUBROUTINE nl_set_skebs_vertstruc
SUBROUTINE nl_set_stoch_vertstruc_opt ( id_id , stoch_vertstruc_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: stoch_vertstruc_opt
  INTEGER id_id
  model_config_rec%stoch_vertstruc_opt(id_id) = stoch_vertstruc_opt
  RETURN
END SUBROUTINE nl_set_stoch_vertstruc_opt
SUBROUTINE nl_set_tot_backscat_psi ( id_id , tot_backscat_psi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: tot_backscat_psi
  INTEGER id_id
  model_config_rec%tot_backscat_psi(id_id) = tot_backscat_psi
  RETURN
END SUBROUTINE nl_set_tot_backscat_psi
SUBROUTINE nl_set_tot_backscat_t ( id_id , tot_backscat_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: tot_backscat_t
  INTEGER id_id
  model_config_rec%tot_backscat_t(id_id) = tot_backscat_t
  RETURN
END SUBROUTINE nl_set_tot_backscat_t
SUBROUTINE nl_set_ztau_psi ( id_id , ztau_psi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: ztau_psi
  INTEGER id_id
  model_config_rec%ztau_psi = ztau_psi 
  RETURN
END SUBROUTINE nl_set_ztau_psi
SUBROUTINE nl_set_ztau_t ( id_id , ztau_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: ztau_t
  INTEGER id_id
  model_config_rec%ztau_t = ztau_t 
  RETURN
END SUBROUTINE nl_set_ztau_t
SUBROUTINE nl_set_rexponent_psi ( id_id , rexponent_psi )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: rexponent_psi
  INTEGER id_id
  model_config_rec%rexponent_psi = rexponent_psi 
  RETURN
END SUBROUTINE nl_set_rexponent_psi
SUBROUTINE nl_set_rexponent_t ( id_id , rexponent_t )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: rexponent_t
  INTEGER id_id
  model_config_rec%rexponent_t = rexponent_t 
  RETURN
END SUBROUTINE nl_set_rexponent_t
SUBROUTINE nl_set_zsigma2_eps ( id_id , zsigma2_eps )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: zsigma2_eps
  INTEGER id_id
  model_config_rec%zsigma2_eps = zsigma2_eps 
  RETURN
END SUBROUTINE nl_set_zsigma2_eps
SUBROUTINE nl_set_zsigma2_eta ( id_id , zsigma2_eta )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: zsigma2_eta
  INTEGER id_id
  model_config_rec%zsigma2_eta = zsigma2_eta 
  RETURN
END SUBROUTINE nl_set_zsigma2_eta
SUBROUTINE nl_set_kminforc ( id_id , kminforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: kminforc
  INTEGER id_id
  model_config_rec%kminforc = kminforc 
  RETURN
END SUBROUTINE nl_set_kminforc
SUBROUTINE nl_set_lminforc ( id_id , lminforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: lminforc
  INTEGER id_id
  model_config_rec%lminforc = lminforc 
  RETURN
END SUBROUTINE nl_set_lminforc
SUBROUTINE nl_set_kminforct ( id_id , kminforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: kminforct
  INTEGER id_id
  model_config_rec%kminforct = kminforct 
  RETURN
END SUBROUTINE nl_set_kminforct
SUBROUTINE nl_set_lminforct ( id_id , lminforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: lminforct
  INTEGER id_id
  model_config_rec%lminforct = lminforct 
  RETURN
END SUBROUTINE nl_set_lminforct
SUBROUTINE nl_set_kmaxforc ( id_id , kmaxforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: kmaxforc
  INTEGER id_id
  model_config_rec%kmaxforc = kmaxforc 
  RETURN
END SUBROUTINE nl_set_kmaxforc
SUBROUTINE nl_set_lmaxforc ( id_id , lmaxforc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: lmaxforc
  INTEGER id_id
  model_config_rec%lmaxforc = lmaxforc 
  RETURN
END SUBROUTINE nl_set_lmaxforc
SUBROUTINE nl_set_kmaxforct ( id_id , kmaxforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: kmaxforct
  INTEGER id_id
  model_config_rec%kmaxforct = kmaxforct 
  RETURN
END SUBROUTINE nl_set_kmaxforct
SUBROUTINE nl_set_lmaxforct ( id_id , lmaxforct )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: lmaxforct
  INTEGER id_id
  model_config_rec%lmaxforct = lmaxforct 
  RETURN
END SUBROUTINE nl_set_lmaxforct
SUBROUTINE nl_set_iseed_skebs ( id_id , iseed_skebs )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: iseed_skebs
  INTEGER id_id
  model_config_rec%iseed_skebs = iseed_skebs 
  RETURN
END SUBROUTINE nl_set_iseed_skebs
SUBROUTINE nl_set_kmaxforch ( id_id , kmaxforch )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: kmaxforch
  INTEGER id_id
  model_config_rec%kmaxforch = kmaxforch 
  RETURN
END SUBROUTINE nl_set_kmaxforch
SUBROUTINE nl_set_lmaxforch ( id_id , lmaxforch )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: lmaxforch
  INTEGER id_id
  model_config_rec%lmaxforch = lmaxforch 
  RETURN
END SUBROUTINE nl_set_lmaxforch
SUBROUTINE nl_set_kmaxforcth ( id_id , kmaxforcth )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: kmaxforcth
  INTEGER id_id
  model_config_rec%kmaxforcth = kmaxforcth 
  RETURN
END SUBROUTINE nl_set_kmaxforcth
SUBROUTINE nl_set_lmaxforcth ( id_id , lmaxforcth )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: lmaxforcth
  INTEGER id_id
  model_config_rec%lmaxforcth = lmaxforcth 
  RETURN
END SUBROUTINE nl_set_lmaxforcth
SUBROUTINE nl_set_sppt ( id_id , sppt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sppt
  INTEGER id_id
  model_config_rec%sppt(id_id) = sppt
  RETURN
END SUBROUTINE nl_set_sppt
SUBROUTINE nl_set_gridpt_stddev_sppt ( id_id , gridpt_stddev_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: gridpt_stddev_sppt
  INTEGER id_id
  model_config_rec%gridpt_stddev_sppt(id_id) = gridpt_stddev_sppt
  RETURN
END SUBROUTINE nl_set_gridpt_stddev_sppt
SUBROUTINE nl_set_stddev_cutoff_sppt ( id_id , stddev_cutoff_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: stddev_cutoff_sppt
  INTEGER id_id
  model_config_rec%stddev_cutoff_sppt(id_id) = stddev_cutoff_sppt
  RETURN
END SUBROUTINE nl_set_stddev_cutoff_sppt
SUBROUTINE nl_set_lengthscale_sppt ( id_id , lengthscale_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: lengthscale_sppt
  INTEGER id_id
  model_config_rec%lengthscale_sppt(id_id) = lengthscale_sppt
  RETURN
END SUBROUTINE nl_set_lengthscale_sppt
SUBROUTINE nl_set_timescale_sppt ( id_id , timescale_sppt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: timescale_sppt
  INTEGER id_id
  model_config_rec%timescale_sppt(id_id) = timescale_sppt
  RETURN
END SUBROUTINE nl_set_timescale_sppt
SUBROUTINE nl_set_sppt_vertstruc ( id_id , sppt_vertstruc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sppt_vertstruc
  INTEGER id_id
  model_config_rec%sppt_vertstruc = sppt_vertstruc 
  RETURN
END SUBROUTINE nl_set_sppt_vertstruc
SUBROUTINE nl_set_iseed_sppt ( id_id , iseed_sppt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: iseed_sppt
  INTEGER id_id
  model_config_rec%iseed_sppt = iseed_sppt 
  RETURN
END SUBROUTINE nl_set_iseed_sppt
SUBROUTINE nl_set_rand_perturb ( id_id , rand_perturb )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: rand_perturb
  INTEGER id_id
  model_config_rec%rand_perturb(id_id) = rand_perturb
  RETURN
END SUBROUTINE nl_set_rand_perturb
SUBROUTINE nl_set_gridpt_stddev_rand_pert ( id_id , gridpt_stddev_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: gridpt_stddev_rand_pert
  INTEGER id_id
  model_config_rec%gridpt_stddev_rand_pert(id_id) = gridpt_stddev_rand_pert
  RETURN
END SUBROUTINE nl_set_gridpt_stddev_rand_pert
SUBROUTINE nl_set_stddev_cutoff_rand_pert ( id_id , stddev_cutoff_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: stddev_cutoff_rand_pert
  INTEGER id_id
  model_config_rec%stddev_cutoff_rand_pert(id_id) = stddev_cutoff_rand_pert
  RETURN
END SUBROUTINE nl_set_stddev_cutoff_rand_pert
SUBROUTINE nl_set_lengthscale_rand_pert ( id_id , lengthscale_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: lengthscale_rand_pert
  INTEGER id_id
  model_config_rec%lengthscale_rand_pert(id_id) = lengthscale_rand_pert
  RETURN
END SUBROUTINE nl_set_lengthscale_rand_pert
SUBROUTINE nl_set_timescale_rand_pert ( id_id , timescale_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: timescale_rand_pert
  INTEGER id_id
  model_config_rec%timescale_rand_pert(id_id) = timescale_rand_pert
  RETURN
END SUBROUTINE nl_set_timescale_rand_pert
SUBROUTINE nl_set_rand_pert_vertstruc ( id_id , rand_pert_vertstruc )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: rand_pert_vertstruc
  INTEGER id_id
  model_config_rec%rand_pert_vertstruc = rand_pert_vertstruc 
  RETURN
END SUBROUTINE nl_set_rand_pert_vertstruc
SUBROUTINE nl_set_iseed_rand_pert ( id_id , iseed_rand_pert )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: iseed_rand_pert
  INTEGER id_id
  model_config_rec%iseed_rand_pert = iseed_rand_pert 
  RETURN
END SUBROUTINE nl_set_iseed_rand_pert
SUBROUTINE nl_set_spp ( id_id , spp )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: spp
  INTEGER id_id
  model_config_rec%spp(id_id) = spp
  RETURN
END SUBROUTINE nl_set_spp
SUBROUTINE nl_set_hrrr_cycling ( id_id , hrrr_cycling )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: hrrr_cycling
  INTEGER id_id
  model_config_rec%hrrr_cycling = hrrr_cycling 
  RETURN
END SUBROUTINE nl_set_hrrr_cycling
SUBROUTINE nl_set_spp_conv ( id_id , spp_conv )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: spp_conv
  INTEGER id_id
  model_config_rec%spp_conv(id_id) = spp_conv
  RETURN
END SUBROUTINE nl_set_spp_conv
SUBROUTINE nl_set_gridpt_stddev_spp_conv ( id_id , gridpt_stddev_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: gridpt_stddev_spp_conv
  INTEGER id_id
  model_config_rec%gridpt_stddev_spp_conv(id_id) = gridpt_stddev_spp_conv
  RETURN
END SUBROUTINE nl_set_gridpt_stddev_spp_conv
SUBROUTINE nl_set_stddev_cutoff_spp_conv ( id_id , stddev_cutoff_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: stddev_cutoff_spp_conv
  INTEGER id_id
  model_config_rec%stddev_cutoff_spp_conv(id_id) = stddev_cutoff_spp_conv
  RETURN
END SUBROUTINE nl_set_stddev_cutoff_spp_conv
SUBROUTINE nl_set_lengthscale_spp_conv ( id_id , lengthscale_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: lengthscale_spp_conv
  INTEGER id_id
  model_config_rec%lengthscale_spp_conv(id_id) = lengthscale_spp_conv
  RETURN
END SUBROUTINE nl_set_lengthscale_spp_conv
SUBROUTINE nl_set_timescale_spp_conv ( id_id , timescale_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: timescale_spp_conv
  INTEGER id_id
  model_config_rec%timescale_spp_conv(id_id) = timescale_spp_conv
  RETURN
END SUBROUTINE nl_set_timescale_spp_conv
SUBROUTINE nl_set_vertstruc_spp_conv ( id_id , vertstruc_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: vertstruc_spp_conv
  INTEGER id_id
  model_config_rec%vertstruc_spp_conv = vertstruc_spp_conv 
  RETURN
END SUBROUTINE nl_set_vertstruc_spp_conv
SUBROUTINE nl_set_iseed_spp_conv ( id_id , iseed_spp_conv )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: iseed_spp_conv
  INTEGER id_id
  model_config_rec%iseed_spp_conv = iseed_spp_conv 
  RETURN
END SUBROUTINE nl_set_iseed_spp_conv
SUBROUTINE nl_set_spp_pbl ( id_id , spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: spp_pbl
  INTEGER id_id
  model_config_rec%spp_pbl(id_id) = spp_pbl
  RETURN
END SUBROUTINE nl_set_spp_pbl
SUBROUTINE nl_set_gridpt_stddev_spp_pbl ( id_id , gridpt_stddev_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: gridpt_stddev_spp_pbl
  INTEGER id_id
  model_config_rec%gridpt_stddev_spp_pbl(id_id) = gridpt_stddev_spp_pbl
  RETURN
END SUBROUTINE nl_set_gridpt_stddev_spp_pbl
SUBROUTINE nl_set_stddev_cutoff_spp_pbl ( id_id , stddev_cutoff_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: stddev_cutoff_spp_pbl
  INTEGER id_id
  model_config_rec%stddev_cutoff_spp_pbl(id_id) = stddev_cutoff_spp_pbl
  RETURN
END SUBROUTINE nl_set_stddev_cutoff_spp_pbl
SUBROUTINE nl_set_lengthscale_spp_pbl ( id_id , lengthscale_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: lengthscale_spp_pbl
  INTEGER id_id
  model_config_rec%lengthscale_spp_pbl(id_id) = lengthscale_spp_pbl
  RETURN
END SUBROUTINE nl_set_lengthscale_spp_pbl
SUBROUTINE nl_set_timescale_spp_pbl ( id_id , timescale_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: timescale_spp_pbl
  INTEGER id_id
  model_config_rec%timescale_spp_pbl(id_id) = timescale_spp_pbl
  RETURN
END SUBROUTINE nl_set_timescale_spp_pbl
SUBROUTINE nl_set_vertstruc_spp_pbl ( id_id , vertstruc_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: vertstruc_spp_pbl
  INTEGER id_id
  model_config_rec%vertstruc_spp_pbl = vertstruc_spp_pbl 
  RETURN
END SUBROUTINE nl_set_vertstruc_spp_pbl
SUBROUTINE nl_set_iseed_spp_pbl ( id_id , iseed_spp_pbl )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: iseed_spp_pbl
  INTEGER id_id
  model_config_rec%iseed_spp_pbl = iseed_spp_pbl 
  RETURN
END SUBROUTINE nl_set_iseed_spp_pbl
SUBROUTINE nl_set_spp_lsm ( id_id , spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: spp_lsm
  INTEGER id_id
  model_config_rec%spp_lsm(id_id) = spp_lsm
  RETURN
END SUBROUTINE nl_set_spp_lsm
SUBROUTINE nl_set_gridpt_stddev_spp_lsm ( id_id , gridpt_stddev_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: gridpt_stddev_spp_lsm
  INTEGER id_id
  model_config_rec%gridpt_stddev_spp_lsm(id_id) = gridpt_stddev_spp_lsm
  RETURN
END SUBROUTINE nl_set_gridpt_stddev_spp_lsm
SUBROUTINE nl_set_stddev_cutoff_spp_lsm ( id_id , stddev_cutoff_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: stddev_cutoff_spp_lsm
  INTEGER id_id
  model_config_rec%stddev_cutoff_spp_lsm(id_id) = stddev_cutoff_spp_lsm
  RETURN
END SUBROUTINE nl_set_stddev_cutoff_spp_lsm
SUBROUTINE nl_set_lengthscale_spp_lsm ( id_id , lengthscale_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: lengthscale_spp_lsm
  INTEGER id_id
  model_config_rec%lengthscale_spp_lsm(id_id) = lengthscale_spp_lsm
  RETURN
END SUBROUTINE nl_set_lengthscale_spp_lsm
SUBROUTINE nl_set_timescale_spp_lsm ( id_id , timescale_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: timescale_spp_lsm
  INTEGER id_id
  model_config_rec%timescale_spp_lsm(id_id) = timescale_spp_lsm
  RETURN
END SUBROUTINE nl_set_timescale_spp_lsm
SUBROUTINE nl_set_vertstruc_spp_lsm ( id_id , vertstruc_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: vertstruc_spp_lsm
  INTEGER id_id
  model_config_rec%vertstruc_spp_lsm = vertstruc_spp_lsm 
  RETURN
END SUBROUTINE nl_set_vertstruc_spp_lsm
SUBROUTINE nl_set_iseed_spp_lsm ( id_id , iseed_spp_lsm )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: iseed_spp_lsm
  INTEGER id_id
  model_config_rec%iseed_spp_lsm = iseed_spp_lsm 
  RETURN
END SUBROUTINE nl_set_iseed_spp_lsm
SUBROUTINE nl_set_skebs_on ( id_id , skebs_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: skebs_on
  INTEGER id_id
  model_config_rec%skebs_on = skebs_on 
  RETURN
END SUBROUTINE nl_set_skebs_on
SUBROUTINE nl_set_sppt_on ( id_id , sppt_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sppt_on
  INTEGER id_id
  model_config_rec%sppt_on = sppt_on 
  RETURN
END SUBROUTINE nl_set_sppt_on
SUBROUTINE nl_set_spp_on ( id_id , spp_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: spp_on
  INTEGER id_id
  model_config_rec%spp_on = spp_on 
  RETURN
END SUBROUTINE nl_set_spp_on
SUBROUTINE nl_set_rand_perturb_on ( id_id , rand_perturb_on )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: rand_perturb_on
  INTEGER id_id
  model_config_rec%rand_perturb_on = rand_perturb_on 
  RETURN
END SUBROUTINE nl_set_rand_perturb_on
SUBROUTINE nl_set_num_stoch_levels ( id_id , num_stoch_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: num_stoch_levels
  INTEGER id_id
  model_config_rec%num_stoch_levels = num_stoch_levels 
  RETURN
END SUBROUTINE nl_set_num_stoch_levels
SUBROUTINE nl_set_seed_dim ( id_id , seed_dim )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: seed_dim
  INTEGER id_id
  model_config_rec%seed_dim = seed_dim 
  RETURN
END SUBROUTINE nl_set_seed_dim
SUBROUTINE nl_set_sfs_opt ( id_id , sfs_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sfs_opt
  INTEGER id_id
  model_config_rec%sfs_opt(id_id) = sfs_opt
  RETURN
END SUBROUTINE nl_set_sfs_opt
SUBROUTINE nl_set_m_opt ( id_id , m_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: m_opt
  INTEGER id_id
  model_config_rec%m_opt(id_id) = m_opt
  RETURN
END SUBROUTINE nl_set_m_opt
SUBROUTINE nl_set_lakedepth_default ( id_id , lakedepth_default )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: lakedepth_default
  INTEGER id_id
  model_config_rec%lakedepth_default(id_id) = lakedepth_default
  RETURN
END SUBROUTINE nl_set_lakedepth_default
SUBROUTINE nl_set_lake_min_elev ( id_id , lake_min_elev )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: lake_min_elev
  INTEGER id_id
  model_config_rec%lake_min_elev(id_id) = lake_min_elev
  RETURN
END SUBROUTINE nl_set_lake_min_elev
SUBROUTINE nl_set_use_lakedepth ( id_id , use_lakedepth )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: use_lakedepth
  INTEGER id_id
  model_config_rec%use_lakedepth(id_id) = use_lakedepth
  RETURN
END SUBROUTINE nl_set_use_lakedepth
SUBROUTINE nl_set_sbm_diagnostics ( id_id , sbm_diagnostics )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sbm_diagnostics
  INTEGER id_id
  model_config_rec%sbm_diagnostics(id_id) = sbm_diagnostics
  RETURN
END SUBROUTINE nl_set_sbm_diagnostics
SUBROUTINE nl_set_afwa_diag_opt ( id_id , afwa_diag_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_diag_opt
  INTEGER id_id
  model_config_rec%afwa_diag_opt(id_id) = afwa_diag_opt
  RETURN
END SUBROUTINE nl_set_afwa_diag_opt
SUBROUTINE nl_set_afwa_ptype_opt ( id_id , afwa_ptype_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_ptype_opt
  INTEGER id_id
  model_config_rec%afwa_ptype_opt(id_id) = afwa_ptype_opt
  RETURN
END SUBROUTINE nl_set_afwa_ptype_opt
SUBROUTINE nl_set_afwa_vil_opt ( id_id , afwa_vil_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_vil_opt
  INTEGER id_id
  model_config_rec%afwa_vil_opt(id_id) = afwa_vil_opt
  RETURN
END SUBROUTINE nl_set_afwa_vil_opt
SUBROUTINE nl_set_afwa_radar_opt ( id_id , afwa_radar_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_radar_opt
  INTEGER id_id
  model_config_rec%afwa_radar_opt(id_id) = afwa_radar_opt
  RETURN
END SUBROUTINE nl_set_afwa_radar_opt
SUBROUTINE nl_set_afwa_severe_opt ( id_id , afwa_severe_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_severe_opt
  INTEGER id_id
  model_config_rec%afwa_severe_opt(id_id) = afwa_severe_opt
  RETURN
END SUBROUTINE nl_set_afwa_severe_opt
SUBROUTINE nl_set_afwa_icing_opt ( id_id , afwa_icing_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_icing_opt
  INTEGER id_id
  model_config_rec%afwa_icing_opt(id_id) = afwa_icing_opt
  RETURN
END SUBROUTINE nl_set_afwa_icing_opt
SUBROUTINE nl_set_afwa_vis_opt ( id_id , afwa_vis_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_vis_opt
  INTEGER id_id
  model_config_rec%afwa_vis_opt(id_id) = afwa_vis_opt
  RETURN
END SUBROUTINE nl_set_afwa_vis_opt
SUBROUTINE nl_set_afwa_cloud_opt ( id_id , afwa_cloud_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_cloud_opt
  INTEGER id_id
  model_config_rec%afwa_cloud_opt(id_id) = afwa_cloud_opt
  RETURN
END SUBROUTINE nl_set_afwa_cloud_opt
SUBROUTINE nl_set_afwa_therm_opt ( id_id , afwa_therm_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_therm_opt
  INTEGER id_id
  model_config_rec%afwa_therm_opt(id_id) = afwa_therm_opt
  RETURN
END SUBROUTINE nl_set_afwa_therm_opt
SUBROUTINE nl_set_afwa_turb_opt ( id_id , afwa_turb_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_turb_opt
  INTEGER id_id
  model_config_rec%afwa_turb_opt(id_id) = afwa_turb_opt
  RETURN
END SUBROUTINE nl_set_afwa_turb_opt
SUBROUTINE nl_set_afwa_buoy_opt ( id_id , afwa_buoy_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_buoy_opt
  INTEGER id_id
  model_config_rec%afwa_buoy_opt(id_id) = afwa_buoy_opt
  RETURN
END SUBROUTINE nl_set_afwa_buoy_opt
SUBROUTINE nl_set_afwa_ptype_ccn_tmp ( id_id , afwa_ptype_ccn_tmp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: afwa_ptype_ccn_tmp
  INTEGER id_id
  model_config_rec%afwa_ptype_ccn_tmp = afwa_ptype_ccn_tmp 
  RETURN
END SUBROUTINE nl_set_afwa_ptype_ccn_tmp
SUBROUTINE nl_set_afwa_ptype_tot_melt ( id_id , afwa_ptype_tot_melt )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: afwa_ptype_tot_melt
  INTEGER id_id
  model_config_rec%afwa_ptype_tot_melt = afwa_ptype_tot_melt 
  RETURN
END SUBROUTINE nl_set_afwa_ptype_tot_melt
SUBROUTINE nl_set_afwa_bad_data_check ( id_id , afwa_bad_data_check )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: afwa_bad_data_check
  INTEGER id_id
  model_config_rec%afwa_bad_data_check = afwa_bad_data_check 
  RETURN
END SUBROUTINE nl_set_afwa_bad_data_check
SUBROUTINE nl_set_mean_diag ( id_id , mean_diag )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_diag
  INTEGER id_id
  model_config_rec%mean_diag = mean_diag 
  RETURN
END SUBROUTINE nl_set_mean_diag
SUBROUTINE nl_set_mean_freq ( id_id , mean_freq )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_freq
  INTEGER id_id
  model_config_rec%mean_freq = mean_freq 
  RETURN
END SUBROUTINE nl_set_mean_freq
SUBROUTINE nl_set_mean_interval ( id_id , mean_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_interval
  INTEGER id_id
  model_config_rec%mean_interval(id_id) = mean_interval
  RETURN
END SUBROUTINE nl_set_mean_interval
SUBROUTINE nl_set_mean_diag_interval ( id_id , mean_diag_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_diag_interval
  INTEGER id_id
  model_config_rec%mean_diag_interval(id_id) = mean_diag_interval
  RETURN
END SUBROUTINE nl_set_mean_diag_interval
SUBROUTINE nl_set_mean_diag_interval_s ( id_id , mean_diag_interval_s )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_diag_interval_s
  INTEGER id_id
  model_config_rec%mean_diag_interval_s(id_id) = mean_diag_interval_s
  RETURN
END SUBROUTINE nl_set_mean_diag_interval_s
SUBROUTINE nl_set_mean_diag_interval_m ( id_id , mean_diag_interval_m )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_diag_interval_m
  INTEGER id_id
  model_config_rec%mean_diag_interval_m(id_id) = mean_diag_interval_m
  RETURN
END SUBROUTINE nl_set_mean_diag_interval_m
SUBROUTINE nl_set_mean_diag_interval_h ( id_id , mean_diag_interval_h )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_diag_interval_h
  INTEGER id_id
  model_config_rec%mean_diag_interval_h(id_id) = mean_diag_interval_h
  RETURN
END SUBROUTINE nl_set_mean_diag_interval_h
SUBROUTINE nl_set_mean_diag_interval_d ( id_id , mean_diag_interval_d )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_diag_interval_d
  INTEGER id_id
  model_config_rec%mean_diag_interval_d(id_id) = mean_diag_interval_d
  RETURN
END SUBROUTINE nl_set_mean_diag_interval_d
SUBROUTINE nl_set_mean_diag_interval_mo ( id_id , mean_diag_interval_mo )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: mean_diag_interval_mo
  INTEGER id_id
  model_config_rec%mean_diag_interval_mo(id_id) = mean_diag_interval_mo
  RETURN
END SUBROUTINE nl_set_mean_diag_interval_mo
SUBROUTINE nl_set_diurnal_diag ( id_id , diurnal_diag )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: diurnal_diag
  INTEGER id_id
  model_config_rec%diurnal_diag = diurnal_diag 
  RETURN
END SUBROUTINE nl_set_diurnal_diag
SUBROUTINE nl_set_nssl_ipelec ( id_id , nssl_ipelec )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: nssl_ipelec
  INTEGER id_id
  model_config_rec%nssl_ipelec(id_id) = nssl_ipelec
  RETURN
END SUBROUTINE nl_set_nssl_ipelec
SUBROUTINE nl_set_nssl_isaund ( id_id , nssl_isaund )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: nssl_isaund
  INTEGER id_id
  model_config_rec%nssl_isaund = nssl_isaund 
  RETURN
END SUBROUTINE nl_set_nssl_isaund
SUBROUTINE nl_set_nssl_iscreen ( id_id , nssl_iscreen )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: nssl_iscreen
  INTEGER id_id
  model_config_rec%nssl_iscreen = nssl_iscreen 
  RETURN
END SUBROUTINE nl_set_nssl_iscreen
SUBROUTINE nl_set_nssl_lightrad ( id_id , nssl_lightrad )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: nssl_lightrad
  INTEGER id_id
  model_config_rec%nssl_lightrad = nssl_lightrad 
  RETURN
END SUBROUTINE nl_set_nssl_lightrad
SUBROUTINE nl_set_nssl_idischarge ( id_id , nssl_idischarge )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: nssl_idischarge
  INTEGER id_id
  model_config_rec%nssl_idischarge = nssl_idischarge 
  RETURN
END SUBROUTINE nl_set_nssl_idischarge
SUBROUTINE nl_set_nssl_ibrkd ( id_id , nssl_ibrkd )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: nssl_ibrkd
  INTEGER id_id
  model_config_rec%nssl_ibrkd = nssl_ibrkd 
  RETURN
END SUBROUTINE nl_set_nssl_ibrkd
SUBROUTINE nl_set_nssl_ecrit ( id_id , nssl_ecrit )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: nssl_ecrit
  INTEGER id_id
  model_config_rec%nssl_ecrit = nssl_ecrit 
  RETURN
END SUBROUTINE nl_set_nssl_ecrit
SUBROUTINE nl_set_nssl_disfrac ( id_id , nssl_disfrac )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: nssl_disfrac
  INTEGER id_id
  model_config_rec%nssl_disfrac = nssl_disfrac 
  RETURN
END SUBROUTINE nl_set_nssl_disfrac
SUBROUTINE nl_set_elec_physics ( id_id , elec_physics )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: elec_physics
  INTEGER id_id
  model_config_rec%elec_physics = elec_physics 
  RETURN
END SUBROUTINE nl_set_elec_physics
SUBROUTINE nl_set_perturb_bdy ( id_id , perturb_bdy )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: perturb_bdy
  INTEGER id_id
  model_config_rec%perturb_bdy = perturb_bdy 
  RETURN
END SUBROUTINE nl_set_perturb_bdy
SUBROUTINE nl_set_perturb_chem_bdy ( id_id , perturb_chem_bdy )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: perturb_chem_bdy
  INTEGER id_id
  model_config_rec%perturb_chem_bdy = perturb_chem_bdy 
  RETURN
END SUBROUTINE nl_set_perturb_chem_bdy
SUBROUTINE nl_set_hybrid_opt ( id_id , hybrid_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: hybrid_opt
  INTEGER id_id
  model_config_rec%hybrid_opt = hybrid_opt 
  RETURN
END SUBROUTINE nl_set_hybrid_opt
SUBROUTINE nl_set_etac ( id_id , etac )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: etac
  INTEGER id_id
  model_config_rec%etac = etac 
  RETURN
END SUBROUTINE nl_set_etac
SUBROUTINE nl_set_num_wif_levels ( id_id , num_wif_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: num_wif_levels
  INTEGER id_id
  model_config_rec%num_wif_levels = num_wif_levels 
  RETURN
END SUBROUTINE nl_set_num_wif_levels
SUBROUTINE nl_set_wif_input_opt ( id_id , wif_input_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: wif_input_opt
  INTEGER id_id
  model_config_rec%wif_input_opt = wif_input_opt 
  RETURN
END SUBROUTINE nl_set_wif_input_opt
SUBROUTINE nl_set_diag_nwp2 ( id_id , diag_nwp2 )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: diag_nwp2
  INTEGER id_id
  model_config_rec%diag_nwp2 = diag_nwp2 
  RETURN
END SUBROUTINE nl_set_diag_nwp2
SUBROUTINE nl_set_solar_diagnostics ( id_id , solar_diagnostics )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: solar_diagnostics
  INTEGER id_id
  model_config_rec%solar_diagnostics = solar_diagnostics 
  RETURN
END SUBROUTINE nl_set_solar_diagnostics
SUBROUTINE nl_set_p_lev_diags ( id_id , p_lev_diags )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: p_lev_diags
  INTEGER id_id
  model_config_rec%p_lev_diags = p_lev_diags 
  RETURN
END SUBROUTINE nl_set_p_lev_diags
SUBROUTINE nl_set_p_lev_diags_dfi ( id_id , p_lev_diags_dfi )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: p_lev_diags_dfi
  INTEGER id_id
  model_config_rec%p_lev_diags_dfi = p_lev_diags_dfi 
  RETURN
END SUBROUTINE nl_set_p_lev_diags_dfi
SUBROUTINE nl_set_num_press_levels ( id_id , num_press_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: num_press_levels
  INTEGER id_id
  model_config_rec%num_press_levels = num_press_levels 
  RETURN
END SUBROUTINE nl_set_num_press_levels
SUBROUTINE nl_set_press_levels ( id_id , press_levels )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: press_levels
  INTEGER id_id
  model_config_rec%press_levels(id_id) = press_levels
  RETURN
END SUBROUTINE nl_set_press_levels
SUBROUTINE nl_set_use_tot_or_hyd_p ( id_id , use_tot_or_hyd_p )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: use_tot_or_hyd_p
  INTEGER id_id
  model_config_rec%use_tot_or_hyd_p = use_tot_or_hyd_p 
  RETURN
END SUBROUTINE nl_set_use_tot_or_hyd_p
SUBROUTINE nl_set_extrap_below_grnd ( id_id , extrap_below_grnd )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: extrap_below_grnd
  INTEGER id_id
  model_config_rec%extrap_below_grnd = extrap_below_grnd 
  RETURN
END SUBROUTINE nl_set_extrap_below_grnd
SUBROUTINE nl_set_p_lev_missing ( id_id , p_lev_missing )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: p_lev_missing
  INTEGER id_id
  model_config_rec%p_lev_missing = p_lev_missing 
  RETURN
END SUBROUTINE nl_set_p_lev_missing
SUBROUTINE nl_set_p_lev_interval ( id_id , p_lev_interval )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: p_lev_interval
  INTEGER id_id
  model_config_rec%p_lev_interval(id_id) = p_lev_interval
  RETURN
END SUBROUTINE nl_set_p_lev_interval
SUBROUTINE nl_set_z_lev_diags ( id_id , z_lev_diags )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: z_lev_diags
  INTEGER id_id
  model_config_rec%z_lev_diags = z_lev_diags 
  RETURN
END SUBROUTINE nl_set_z_lev_diags
SUBROUTINE nl_set_z_lev_diags_dfi ( id_id , z_lev_diags_dfi )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: z_lev_diags_dfi
  INTEGER id_id
  model_config_rec%z_lev_diags_dfi = z_lev_diags_dfi 
  RETURN
END SUBROUTINE nl_set_z_lev_diags_dfi
SUBROUTINE nl_set_num_z_levels ( id_id , num_z_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: num_z_levels
  INTEGER id_id
  model_config_rec%num_z_levels = num_z_levels 
  RETURN
END SUBROUTINE nl_set_num_z_levels
SUBROUTINE nl_set_z_levels ( id_id , z_levels )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: z_levels
  INTEGER id_id
  model_config_rec%z_levels(id_id) = z_levels
  RETURN
END SUBROUTINE nl_set_z_levels
SUBROUTINE nl_set_z_lev_missing ( id_id , z_lev_missing )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: z_lev_missing
  INTEGER id_id
  model_config_rec%z_lev_missing = z_lev_missing 
  RETURN
END SUBROUTINE nl_set_z_lev_missing
SUBROUTINE nl_set_z_lev_interval ( id_id , z_lev_interval )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: z_lev_interval
  INTEGER id_id
  model_config_rec%z_lev_interval(id_id) = z_lev_interval
  RETURN
END SUBROUTINE nl_set_z_lev_interval
SUBROUTINE nl_set_iau ( id_id , iau )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: iau
  INTEGER id_id
  model_config_rec%iau(id_id) = iau
  RETURN
END SUBROUTINE nl_set_iau
SUBROUTINE nl_set_iau_time_window_sec ( id_id , iau_time_window_sec )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: iau_time_window_sec
  INTEGER id_id
  model_config_rec%iau_time_window_sec(id_id) = iau_time_window_sec
  RETURN
END SUBROUTINE nl_set_iau_time_window_sec
SUBROUTINE nl_set_wrf_cmaq_option ( id_id , wrf_cmaq_option )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: wrf_cmaq_option
  INTEGER id_id
  model_config_rec%wrf_cmaq_option = wrf_cmaq_option 
  RETURN
END SUBROUTINE nl_set_wrf_cmaq_option
SUBROUTINE nl_set_wrf_cmaq_freq ( id_id , wrf_cmaq_freq )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: wrf_cmaq_freq
  INTEGER id_id
  model_config_rec%wrf_cmaq_freq = wrf_cmaq_freq 
  RETURN
END SUBROUTINE nl_set_wrf_cmaq_freq
SUBROUTINE nl_set_met_file_tstep ( id_id , met_file_tstep )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: met_file_tstep
  INTEGER id_id
  model_config_rec%met_file_tstep = met_file_tstep 
  RETURN
END SUBROUTINE nl_set_met_file_tstep
SUBROUTINE nl_set_direct_sw_feedback ( id_id , direct_sw_feedback )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: direct_sw_feedback
  INTEGER id_id
  model_config_rec%direct_sw_feedback = direct_sw_feedback 
  RETURN
END SUBROUTINE nl_set_direct_sw_feedback
SUBROUTINE nl_set_feedback_restart ( id_id , feedback_restart )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: feedback_restart
  INTEGER id_id
  model_config_rec%feedback_restart = feedback_restart 
  RETURN
END SUBROUTINE nl_set_feedback_restart
SUBROUTINE nl_set_sdirk3_jvp_checkpointing ( id_id , sdirk3_jvp_checkpointing )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_jvp_checkpointing
  INTEGER id_id
  model_config_rec%sdirk3_jvp_checkpointing = sdirk3_jvp_checkpointing 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_checkpointing
SUBROUTINE nl_set_sdirk3_jvp_checkpoint_segments ( id_id , sdirk3_jvp_checkpoint_segments )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_jvp_checkpoint_segments
  INTEGER id_id
  model_config_rec%sdirk3_jvp_checkpoint_segments = sdirk3_jvp_checkpoint_segments 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_checkpoint_segments
SUBROUTINE nl_set_sdirk3_jvp_graph_caching ( id_id , sdirk3_jvp_graph_caching )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_jvp_graph_caching
  INTEGER id_id
  model_config_rec%sdirk3_jvp_graph_caching = sdirk3_jvp_graph_caching 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_graph_caching
SUBROUTINE nl_set_sdirk3_jvp_batched ( id_id , sdirk3_jvp_batched )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_jvp_batched
  INTEGER id_id
  model_config_rec%sdirk3_jvp_batched = sdirk3_jvp_batched 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_batched
SUBROUTINE nl_set_sdirk3_jvp_batch_size ( id_id , sdirk3_jvp_batch_size )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_jvp_batch_size
  INTEGER id_id
  model_config_rec%sdirk3_jvp_batch_size = sdirk3_jvp_batch_size 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_batch_size
SUBROUTINE nl_set_sdirk3_jvp_mixed_precision ( id_id , sdirk3_jvp_mixed_precision )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_jvp_mixed_precision
  INTEGER id_id
  model_config_rec%sdirk3_jvp_mixed_precision = sdirk3_jvp_mixed_precision 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_mixed_precision
SUBROUTINE nl_set_sdirk3_implicit_acoustic ( id_id , sdirk3_implicit_acoustic )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_implicit_acoustic
  INTEGER id_id
  model_config_rec%sdirk3_implicit_acoustic = sdirk3_implicit_acoustic 
  RETURN
END SUBROUTINE nl_set_sdirk3_implicit_acoustic
SUBROUTINE nl_set_sdirk3_implicit_gravity ( id_id , sdirk3_implicit_gravity )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_implicit_gravity
  INTEGER id_id
  model_config_rec%sdirk3_implicit_gravity = sdirk3_implicit_gravity 
  RETURN
END SUBROUTINE nl_set_sdirk3_implicit_gravity
SUBROUTINE nl_set_sdirk3_implicit_rayleigh ( id_id , sdirk3_implicit_rayleigh )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_implicit_rayleigh
  INTEGER id_id
  model_config_rec%sdirk3_implicit_rayleigh = sdirk3_implicit_rayleigh 
  RETURN
END SUBROUTINE nl_set_sdirk3_implicit_rayleigh
SUBROUTINE nl_set_sdirk3_implicit_wdamp ( id_id , sdirk3_implicit_wdamp )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_implicit_wdamp
  INTEGER id_id
  model_config_rec%sdirk3_implicit_wdamp = sdirk3_implicit_wdamp 
  RETURN
END SUBROUTINE nl_set_sdirk3_implicit_wdamp
SUBROUTINE nl_set_sdirk3_implicit_vdiff ( id_id , sdirk3_implicit_vdiff )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_implicit_vdiff
  INTEGER id_id
  model_config_rec%sdirk3_implicit_vdiff = sdirk3_implicit_vdiff 
  RETURN
END SUBROUTINE nl_set_sdirk3_implicit_vdiff
SUBROUTINE nl_set_sdirk3_implicit_hdiff ( id_id , sdirk3_implicit_hdiff )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_implicit_hdiff
  INTEGER id_id
  model_config_rec%sdirk3_implicit_hdiff = sdirk3_implicit_hdiff 
  RETURN
END SUBROUTINE nl_set_sdirk3_implicit_hdiff
SUBROUTINE nl_set_sdirk3_implicit_divergence ( id_id , sdirk3_implicit_divergence )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_implicit_divergence
  INTEGER id_id
  model_config_rec%sdirk3_implicit_divergence = sdirk3_implicit_divergence 
  RETURN
END SUBROUTINE nl_set_sdirk3_implicit_divergence
SUBROUTINE nl_set_sdirk3_precond_diagonal_only ( id_id , sdirk3_precond_diagonal_only )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_precond_diagonal_only
  INTEGER id_id
  model_config_rec%sdirk3_precond_diagonal_only = sdirk3_precond_diagonal_only 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_diagonal_only
SUBROUTINE nl_set_sdirk3_precond_block_jacobi ( id_id , sdirk3_precond_block_jacobi )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_precond_block_jacobi
  INTEGER id_id
  model_config_rec%sdirk3_precond_block_jacobi = sdirk3_precond_block_jacobi 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_block_jacobi
SUBROUTINE nl_set_sdirk3_precond_block_size ( id_id , sdirk3_precond_block_size )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_precond_block_size
  INTEGER id_id
  model_config_rec%sdirk3_precond_block_size = sdirk3_precond_block_size 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_block_size
SUBROUTINE nl_set_sdirk3_precond_ilu ( id_id , sdirk3_precond_ilu )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_precond_ilu
  INTEGER id_id
  model_config_rec%sdirk3_precond_ilu = sdirk3_precond_ilu 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_ilu
SUBROUTINE nl_set_sdirk3_precond_ilu_level ( id_id , sdirk3_precond_ilu_level )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_precond_ilu_level
  INTEGER id_id
  model_config_rec%sdirk3_precond_ilu_level = sdirk3_precond_ilu_level 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_ilu_level
SUBROUTINE nl_set_sdirk3_precond_multigrid ( id_id , sdirk3_precond_multigrid )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_precond_multigrid
  INTEGER id_id
  model_config_rec%sdirk3_precond_multigrid = sdirk3_precond_multigrid 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_multigrid
SUBROUTINE nl_set_sdirk3_precond_mg_levels ( id_id , sdirk3_precond_mg_levels )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_precond_mg_levels
  INTEGER id_id
  model_config_rec%sdirk3_precond_mg_levels = sdirk3_precond_mg_levels 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_mg_levels
SUBROUTINE nl_set_sdirk3_nk_adaptive_tol ( id_id , sdirk3_nk_adaptive_tol )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_nk_adaptive_tol
  INTEGER id_id
  model_config_rec%sdirk3_nk_adaptive_tol = sdirk3_nk_adaptive_tol 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_adaptive_tol
SUBROUTINE nl_set_sdirk3_nk_forcing_eta_max ( id_id , sdirk3_nk_forcing_eta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_nk_forcing_eta_max
  INTEGER id_id
  model_config_rec%sdirk3_nk_forcing_eta_max = sdirk3_nk_forcing_eta_max 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_forcing_eta_max
SUBROUTINE nl_set_sdirk3_nk_forcing_eta_min ( id_id , sdirk3_nk_forcing_eta_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_nk_forcing_eta_min
  INTEGER id_id
  model_config_rec%sdirk3_nk_forcing_eta_min = sdirk3_nk_forcing_eta_min 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_forcing_eta_min
SUBROUTINE nl_set_sdirk3_nk_forcing_gamma ( id_id , sdirk3_nk_forcing_gamma )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_nk_forcing_gamma
  INTEGER id_id
  model_config_rec%sdirk3_nk_forcing_gamma = sdirk3_nk_forcing_gamma 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_forcing_gamma
SUBROUTINE nl_set_sdirk3_nk_forcing_alpha ( id_id , sdirk3_nk_forcing_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_nk_forcing_alpha
  INTEGER id_id
  model_config_rec%sdirk3_nk_forcing_alpha = sdirk3_nk_forcing_alpha 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_forcing_alpha
SUBROUTINE nl_set_sdirk3_nk_line_search ( id_id , sdirk3_nk_line_search )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_nk_line_search
  INTEGER id_id
  model_config_rec%sdirk3_nk_line_search = sdirk3_nk_line_search 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_line_search
SUBROUTINE nl_set_sdirk3_nk_trust_region ( id_id , sdirk3_nk_trust_region )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_nk_trust_region
  INTEGER id_id
  model_config_rec%sdirk3_nk_trust_region = sdirk3_nk_trust_region 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_trust_region
SUBROUTINE nl_set_sdirk3_nk_trust_radius ( id_id , sdirk3_nk_trust_radius )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_nk_trust_radius
  INTEGER id_id
  model_config_rec%sdirk3_nk_trust_radius = sdirk3_nk_trust_radius 
  RETURN
END SUBROUTINE nl_set_sdirk3_nk_trust_radius
SUBROUTINE nl_set_sdirk3_stage2_gmres_restart ( id_id , sdirk3_stage2_gmres_restart )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_stage2_gmres_restart
  INTEGER id_id
  model_config_rec%sdirk3_stage2_gmres_restart = sdirk3_stage2_gmres_restart 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage2_gmres_restart
SUBROUTINE nl_set_sdirk3_stage2_max_krylov_restarts ( id_id , sdirk3_stage2_max_krylov_restarts )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_stage2_max_krylov_restarts
  INTEGER id_id
  model_config_rec%sdirk3_stage2_max_krylov_restarts = sdirk3_stage2_max_krylov_restarts 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage2_max_krylov_restarts
SUBROUTINE nl_set_sdirk3_stage2_krylov_tol ( id_id , sdirk3_stage2_krylov_tol )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_stage2_krylov_tol
  INTEGER id_id
  model_config_rec%sdirk3_stage2_krylov_tol = sdirk3_stage2_krylov_tol 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage2_krylov_tol
SUBROUTINE nl_set_sdirk3_stage2_ew_eta_min ( id_id , sdirk3_stage2_ew_eta_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_stage2_ew_eta_min
  INTEGER id_id
  model_config_rec%sdirk3_stage2_ew_eta_min = sdirk3_stage2_ew_eta_min 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage2_ew_eta_min
SUBROUTINE nl_set_sdirk3_stage2_ew_eta_max ( id_id , sdirk3_stage2_ew_eta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_stage2_ew_eta_max
  INTEGER id_id
  model_config_rec%sdirk3_stage2_ew_eta_max = sdirk3_stage2_ew_eta_max 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage2_ew_eta_max
SUBROUTINE nl_set_sdirk3_stage3_gmres_restart ( id_id , sdirk3_stage3_gmres_restart )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_stage3_gmres_restart
  INTEGER id_id
  model_config_rec%sdirk3_stage3_gmres_restart = sdirk3_stage3_gmres_restart 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage3_gmres_restart
SUBROUTINE nl_set_sdirk3_stage3_max_krylov_restarts ( id_id , sdirk3_stage3_max_krylov_restarts )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_stage3_max_krylov_restarts
  INTEGER id_id
  model_config_rec%sdirk3_stage3_max_krylov_restarts = sdirk3_stage3_max_krylov_restarts 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage3_max_krylov_restarts
SUBROUTINE nl_set_sdirk3_stage3_krylov_tol ( id_id , sdirk3_stage3_krylov_tol )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_stage3_krylov_tol
  INTEGER id_id
  model_config_rec%sdirk3_stage3_krylov_tol = sdirk3_stage3_krylov_tol 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage3_krylov_tol
SUBROUTINE nl_set_sdirk3_stage3_ew_eta_min ( id_id , sdirk3_stage3_ew_eta_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_stage3_ew_eta_min
  INTEGER id_id
  model_config_rec%sdirk3_stage3_ew_eta_min = sdirk3_stage3_ew_eta_min 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage3_ew_eta_min
SUBROUTINE nl_set_sdirk3_stage3_ew_eta_max ( id_id , sdirk3_stage3_ew_eta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_stage3_ew_eta_max
  INTEGER id_id
  model_config_rec%sdirk3_stage3_ew_eta_max = sdirk3_stage3_ew_eta_max 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage3_ew_eta_max
SUBROUTINE nl_set_sdirk3_imex_split_mode ( id_id , sdirk3_imex_split_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_imex_split_mode
  INTEGER id_id
  model_config_rec%sdirk3_imex_split_mode = sdirk3_imex_split_mode 
  RETURN
END SUBROUTINE nl_set_sdirk3_imex_split_mode
SUBROUTINE nl_set_sdirk3_imex_slow_in_tangent ( id_id , sdirk3_imex_slow_in_tangent )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_imex_slow_in_tangent
  INTEGER id_id
  model_config_rec%sdirk3_imex_slow_in_tangent = sdirk3_imex_slow_in_tangent 
  RETURN
END SUBROUTINE nl_set_sdirk3_imex_slow_in_tangent
SUBROUTINE nl_set_sdirk3_imex_phys_in_tangent ( id_id , sdirk3_imex_phys_in_tangent )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_imex_phys_in_tangent
  INTEGER id_id
  model_config_rec%sdirk3_imex_phys_in_tangent = sdirk3_imex_phys_in_tangent 
  RETURN
END SUBROUTINE nl_set_sdirk3_imex_phys_in_tangent
SUBROUTINE nl_set_sdirk3_stage1_explicit ( id_id , sdirk3_stage1_explicit )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_stage1_explicit
  INTEGER id_id
  model_config_rec%sdirk3_stage1_explicit = sdirk3_stage1_explicit 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage1_explicit
SUBROUTINE nl_set_sdirk3_stage3_warmstart ( id_id , sdirk3_stage3_warmstart )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_stage3_warmstart
  INTEGER id_id
  model_config_rec%sdirk3_stage3_warmstart = sdirk3_stage3_warmstart 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage3_warmstart
SUBROUTINE nl_set_sdirk3_stage_fail_action ( id_id , sdirk3_stage_fail_action )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_stage_fail_action
  INTEGER id_id
  model_config_rec%sdirk3_stage_fail_action = sdirk3_stage_fail_action 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage_fail_action
SUBROUTINE nl_set_sdirk3_gate_metric_mode ( id_id , sdirk3_gate_metric_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_gate_metric_mode
  INTEGER id_id
  model_config_rec%sdirk3_gate_metric_mode = sdirk3_gate_metric_mode 
  RETURN
END SUBROUTINE nl_set_sdirk3_gate_metric_mode
SUBROUTINE nl_set_sdirk3_stage3_gate_rel_threshold ( id_id , sdirk3_stage3_gate_rel_threshold )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_stage3_gate_rel_threshold
  INTEGER id_id
  model_config_rec%sdirk3_stage3_gate_rel_threshold = sdirk3_stage3_gate_rel_threshold 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage3_gate_rel_threshold
SUBROUTINE nl_set_sdirk3_hopeless_relax ( id_id , sdirk3_hopeless_relax )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_hopeless_relax
  INTEGER id_id
  model_config_rec%sdirk3_hopeless_relax = sdirk3_hopeless_relax 
  RETURN
END SUBROUTINE nl_set_sdirk3_hopeless_relax
SUBROUTINE nl_set_sdirk3_precond_ratio_guard_warn_only ( id_id , sdirk3_precond_ratio_guard_warn_only )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_precond_ratio_guard_warn_only
  INTEGER id_id
  model_config_rec%sdirk3_precond_ratio_guard_warn_only = sdirk3_precond_ratio_guard_warn_only 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_ratio_guard_warn_only
SUBROUTINE nl_set_sdirk3_retain_graph_for_adjoint ( id_id , sdirk3_retain_graph_for_adjoint )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_retain_graph_for_adjoint
  INTEGER id_id
  model_config_rec%sdirk3_retain_graph_for_adjoint = sdirk3_retain_graph_for_adjoint 
  RETURN
END SUBROUTINE nl_set_sdirk3_retain_graph_for_adjoint
SUBROUTINE nl_set_sdirk3_enable_ad_halo_exchange ( id_id , sdirk3_enable_ad_halo_exchange )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_enable_ad_halo_exchange
  INTEGER id_id
  model_config_rec%sdirk3_enable_ad_halo_exchange = sdirk3_enable_ad_halo_exchange 
  RETURN
END SUBROUTINE nl_set_sdirk3_enable_ad_halo_exchange
SUBROUTINE nl_set_sdirk3_stage_boundary_sync ( id_id , sdirk3_stage_boundary_sync )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_stage_boundary_sync
  INTEGER id_id
  model_config_rec%sdirk3_stage_boundary_sync = sdirk3_stage_boundary_sync 
  RETURN
END SUBROUTINE nl_set_sdirk3_stage_boundary_sync
SUBROUTINE nl_set_sdirk3_jvp_auto_bench_calls ( id_id , sdirk3_jvp_auto_bench_calls )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_jvp_auto_bench_calls
  INTEGER id_id
  model_config_rec%sdirk3_jvp_auto_bench_calls = sdirk3_jvp_auto_bench_calls 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_auto_bench_calls
SUBROUTINE nl_set_sdirk3_jvp_auto_bench_quality_gate ( id_id , sdirk3_jvp_auto_bench_quality_gate )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_jvp_auto_bench_quality_gate
  INTEGER id_id
  model_config_rec%sdirk3_jvp_auto_bench_quality_gate = sdirk3_jvp_auto_bench_quality_gate 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_auto_bench_quality_gate
SUBROUTINE nl_set_sdirk3_jvp_auto_bench_warmup ( id_id , sdirk3_jvp_auto_bench_warmup )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_jvp_auto_bench_warmup
  INTEGER id_id
  model_config_rec%sdirk3_jvp_auto_bench_warmup = sdirk3_jvp_auto_bench_warmup 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_auto_bench_warmup
SUBROUTINE nl_set_sdirk3_jvp_auto_bench_seed ( id_id , sdirk3_jvp_auto_bench_seed )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_jvp_auto_bench_seed
  INTEGER id_id
  model_config_rec%sdirk3_jvp_auto_bench_seed = sdirk3_jvp_auto_bench_seed 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_auto_bench_seed
SUBROUTINE nl_set_sdirk3_jvp_auto_bench_lock_reset_stage1 ( id_id , sdirk3_jvp_auto_bench_lock_reset_stage1 )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_jvp_auto_bench_lock_reset_stage1
  INTEGER id_id
  model_config_rec%sdirk3_jvp_auto_bench_lock_reset_stage1 = sdirk3_jvp_auto_bench_lock_reset_stage1 
  RETURN
END SUBROUTINE nl_set_sdirk3_jvp_auto_bench_lock_reset_stage1
SUBROUTINE nl_set_sdirk3_solver_telemetry ( id_id , sdirk3_solver_telemetry )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_solver_telemetry
  INTEGER id_id
  model_config_rec%sdirk3_solver_telemetry = sdirk3_solver_telemetry 
  RETURN
END SUBROUTINE nl_set_sdirk3_solver_telemetry
SUBROUTINE nl_set_sdirk3_progress_invariant_enable ( id_id , sdirk3_progress_invariant_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_progress_invariant_enable
  INTEGER id_id
  model_config_rec%sdirk3_progress_invariant_enable = sdirk3_progress_invariant_enable 
  RETURN
END SUBROUTINE nl_set_sdirk3_progress_invariant_enable
SUBROUTINE nl_set_sdirk3_progress_invariant_min_ratio ( id_id , sdirk3_progress_invariant_min_ratio )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_progress_invariant_min_ratio
  INTEGER id_id
  model_config_rec%sdirk3_progress_invariant_min_ratio = sdirk3_progress_invariant_min_ratio 
  RETURN
END SUBROUTINE nl_set_sdirk3_progress_invariant_min_ratio
SUBROUTINE nl_set_sdirk3_progress_invariant_streak_limit ( id_id , sdirk3_progress_invariant_streak_limit )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_progress_invariant_streak_limit
  INTEGER id_id
  model_config_rec%sdirk3_progress_invariant_streak_limit = sdirk3_progress_invariant_streak_limit 
  RETURN
END SUBROUTINE nl_set_sdirk3_progress_invariant_streak_limit
SUBROUTINE nl_set_sdirk3_variable_pc_event_log ( id_id , sdirk3_variable_pc_event_log )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_variable_pc_event_log
  INTEGER id_id
  model_config_rec%sdirk3_variable_pc_event_log = sdirk3_variable_pc_event_log 
  RETURN
END SUBROUTINE nl_set_sdirk3_variable_pc_event_log
SUBROUTINE nl_set_sdirk3_gmres_warmstart ( id_id , sdirk3_gmres_warmstart )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_gmres_warmstart
  INTEGER id_id
  model_config_rec%sdirk3_gmres_warmstart = sdirk3_gmres_warmstart 
  RETURN
END SUBROUTINE nl_set_sdirk3_gmres_warmstart
SUBROUTINE nl_set_sdirk3_gmres_warmstart_quality_gate ( id_id , sdirk3_gmres_warmstart_quality_gate )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_gmres_warmstart_quality_gate
  INTEGER id_id
  model_config_rec%sdirk3_gmres_warmstart_quality_gate = sdirk3_gmres_warmstart_quality_gate 
  RETURN
END SUBROUTINE nl_set_sdirk3_gmres_warmstart_quality_gate
SUBROUTINE nl_set_sdirk3_inn_warmstart_enable ( id_id , sdirk3_inn_warmstart_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_inn_warmstart_enable
  INTEGER id_id
  model_config_rec%sdirk3_inn_warmstart_enable = sdirk3_inn_warmstart_enable 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_warmstart_enable
SUBROUTINE nl_set_sdirk3_inn_residual_gate_enable ( id_id , sdirk3_inn_residual_gate_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_inn_residual_gate_enable
  INTEGER id_id
  model_config_rec%sdirk3_inn_residual_gate_enable = sdirk3_inn_residual_gate_enable 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_residual_gate_enable
SUBROUTINE nl_set_sdirk3_inn_q_min ( id_id , sdirk3_inn_q_min )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_inn_q_min
  INTEGER id_id
  model_config_rec%sdirk3_inn_q_min = sdirk3_inn_q_min 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_q_min
SUBROUTINE nl_set_sdirk3_inn_gate_rel_tol ( id_id , sdirk3_inn_gate_rel_tol )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_inn_gate_rel_tol
  INTEGER id_id
  model_config_rec%sdirk3_inn_gate_rel_tol = sdirk3_inn_gate_rel_tol 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_gate_rel_tol
SUBROUTINE nl_set_sdirk3_inn_gate_q_noise ( id_id , sdirk3_inn_gate_q_noise )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_inn_gate_q_noise
  INTEGER id_id
  model_config_rec%sdirk3_inn_gate_q_noise = sdirk3_inn_gate_q_noise 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_gate_q_noise
SUBROUTINE nl_set_sdirk3_inn_beta_max ( id_id , sdirk3_inn_beta_max )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_inn_beta_max
  INTEGER id_id
  model_config_rec%sdirk3_inn_beta_max = sdirk3_inn_beta_max 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_beta_max
SUBROUTINE nl_set_sdirk3_inn_ood_guard_enable ( id_id , sdirk3_inn_ood_guard_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_inn_ood_guard_enable
  INTEGER id_id
  model_config_rec%sdirk3_inn_ood_guard_enable = sdirk3_inn_ood_guard_enable 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_ood_guard_enable
SUBROUTINE nl_set_sdirk3_inn_tol_ramp_enable ( id_id , sdirk3_inn_tol_ramp_enable )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_inn_tol_ramp_enable
  INTEGER id_id
  model_config_rec%sdirk3_inn_tol_ramp_enable = sdirk3_inn_tol_ramp_enable 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_tol_ramp_enable
SUBROUTINE nl_set_sdirk3_inn_tol_ramp_gamma ( id_id , sdirk3_inn_tol_ramp_gamma )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_inn_tol_ramp_gamma
  INTEGER id_id
  model_config_rec%sdirk3_inn_tol_ramp_gamma = sdirk3_inn_tol_ramp_gamma 
  RETURN
END SUBROUTINE nl_set_sdirk3_inn_tol_ramp_gamma
SUBROUTINE nl_set_sdirk3_rhs_bc_parity ( id_id , sdirk3_rhs_bc_parity )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_rhs_bc_parity
  INTEGER id_id
  model_config_rec%sdirk3_rhs_bc_parity = sdirk3_rhs_bc_parity 
  RETURN
END SUBROUTINE nl_set_sdirk3_rhs_bc_parity
SUBROUTINE nl_set_sdirk3_mass_pgf_bc_guard ( id_id , sdirk3_mass_pgf_bc_guard )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_mass_pgf_bc_guard
  INTEGER id_id
  model_config_rec%sdirk3_mass_pgf_bc_guard = sdirk3_mass_pgf_bc_guard 
  RETURN
END SUBROUTINE nl_set_sdirk3_mass_pgf_bc_guard
SUBROUTINE nl_set_sdirk3_precond_horizontal_smooth_alpha ( id_id , sdirk3_precond_horizontal_smooth_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_precond_horizontal_smooth_alpha
  INTEGER id_id
  model_config_rec%sdirk3_precond_horizontal_smooth_alpha = sdirk3_precond_horizontal_smooth_alpha 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_horizontal_smooth_alpha
SUBROUTINE nl_set_sdirk3_precond_horizontal_smooth_iters ( id_id , sdirk3_precond_horizontal_smooth_iters )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_precond_horizontal_smooth_iters
  INTEGER id_id
  model_config_rec%sdirk3_precond_horizontal_smooth_iters = sdirk3_precond_horizontal_smooth_iters 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_horizontal_smooth_iters
SUBROUTINE nl_set_sdirk3_precond_vertical_smooth_alpha ( id_id , sdirk3_precond_vertical_smooth_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_precond_vertical_smooth_alpha
  INTEGER id_id
  model_config_rec%sdirk3_precond_vertical_smooth_alpha = sdirk3_precond_vertical_smooth_alpha 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_vertical_smooth_alpha
SUBROUTINE nl_set_sdirk3_precond_smooth_boundary_guard ( id_id , sdirk3_precond_smooth_boundary_guard )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_precond_smooth_boundary_guard
  INTEGER id_id
  model_config_rec%sdirk3_precond_smooth_boundary_guard = sdirk3_precond_smooth_boundary_guard 
  RETURN
END SUBROUTINE nl_set_sdirk3_precond_smooth_boundary_guard
SUBROUTINE nl_set_sdirk3_trust_region_block_aware_thresh ( id_id , sdirk3_trust_region_block_aware_thresh )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_trust_region_block_aware_thresh
  INTEGER id_id
  model_config_rec%sdirk3_trust_region_block_aware_thresh = sdirk3_trust_region_block_aware_thresh 
  RETURN
END SUBROUTINE nl_set_sdirk3_trust_region_block_aware_thresh
SUBROUTINE nl_set_sdirk3_trust_fallback_relax ( id_id , sdirk3_trust_fallback_relax )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_trust_fallback_relax
  INTEGER id_id
  model_config_rec%sdirk3_trust_fallback_relax = sdirk3_trust_fallback_relax 
  RETURN
END SUBROUTINE nl_set_sdirk3_trust_fallback_relax
SUBROUTINE nl_set_sdirk3_trust_fallback_ratio ( id_id , sdirk3_trust_fallback_ratio )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_trust_fallback_ratio
  INTEGER id_id
  model_config_rec%sdirk3_trust_fallback_ratio = sdirk3_trust_fallback_ratio 
  RETURN
END SUBROUTINE nl_set_sdirk3_trust_fallback_ratio
SUBROUTINE nl_set_sdirk3_direct_u_solve_thresh ( id_id , sdirk3_direct_u_solve_thresh )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_direct_u_solve_thresh
  INTEGER id_id
  model_config_rec%sdirk3_direct_u_solve_thresh = sdirk3_direct_u_solve_thresh 
  RETURN
END SUBROUTINE nl_set_sdirk3_direct_u_solve_thresh
SUBROUTINE nl_set_sdirk3_memory_pool ( id_id , sdirk3_memory_pool )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_memory_pool
  INTEGER id_id
  model_config_rec%sdirk3_memory_pool = sdirk3_memory_pool 
  RETURN
END SUBROUTINE nl_set_sdirk3_memory_pool
SUBROUTINE nl_set_sdirk3_memory_pool_size_mb ( id_id , sdirk3_memory_pool_size_mb )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_memory_pool_size_mb
  INTEGER id_id
  model_config_rec%sdirk3_memory_pool_size_mb = sdirk3_memory_pool_size_mb 
  RETURN
END SUBROUTINE nl_set_sdirk3_memory_pool_size_mb
SUBROUTINE nl_set_sdirk3_tensor_checkpointing ( id_id , sdirk3_tensor_checkpointing )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_tensor_checkpointing
  INTEGER id_id
  model_config_rec%sdirk3_tensor_checkpointing = sdirk3_tensor_checkpointing 
  RETURN
END SUBROUTINE nl_set_sdirk3_tensor_checkpointing
SUBROUTINE nl_set_sdirk3_gradient_checkpointing ( id_id , sdirk3_gradient_checkpointing )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_gradient_checkpointing
  INTEGER id_id
  model_config_rec%sdirk3_gradient_checkpointing = sdirk3_gradient_checkpointing 
  RETURN
END SUBROUTINE nl_set_sdirk3_gradient_checkpointing
SUBROUTINE nl_set_sdirk3_kdamp ( id_id , sdirk3_kdamp )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_kdamp
  INTEGER id_id
  model_config_rec%sdirk3_kdamp = sdirk3_kdamp 
  RETURN
END SUBROUTINE nl_set_sdirk3_kdamp
SUBROUTINE nl_set_sdirk3_khdif ( id_id , sdirk3_khdif )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_khdif
  INTEGER id_id
  model_config_rec%sdirk3_khdif = sdirk3_khdif 
  RETURN
END SUBROUTINE nl_set_sdirk3_khdif
SUBROUTINE nl_set_sdirk3_kvdif ( id_id , sdirk3_kvdif )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_kvdif
  INTEGER id_id
  model_config_rec%sdirk3_kvdif = sdirk3_kvdif 
  RETURN
END SUBROUTINE nl_set_sdirk3_kvdif
SUBROUTINE nl_set_sdirk3_w_damp_alpha ( id_id , sdirk3_w_damp_alpha )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_w_damp_alpha
  INTEGER id_id
  model_config_rec%sdirk3_w_damp_alpha = sdirk3_w_damp_alpha 
  RETURN
END SUBROUTINE nl_set_sdirk3_w_damp_alpha
SUBROUTINE nl_set_sdirk3_w_crit_cfl ( id_id , sdirk3_w_crit_cfl )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_w_crit_cfl
  INTEGER id_id
  model_config_rec%sdirk3_w_crit_cfl = sdirk3_w_crit_cfl 
  RETURN
END SUBROUTINE nl_set_sdirk3_w_crit_cfl
SUBROUTINE nl_set_sdirk3_validation_level ( id_id , sdirk3_validation_level )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_validation_level
  INTEGER id_id
  model_config_rec%sdirk3_validation_level = sdirk3_validation_level 
  RETURN
END SUBROUTINE nl_set_sdirk3_validation_level
SUBROUTINE nl_set_sdirk3_check_staggered ( id_id , sdirk3_check_staggered )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_check_staggered
  INTEGER id_id
  model_config_rec%sdirk3_check_staggered = sdirk3_check_staggered 
  RETURN
END SUBROUTINE nl_set_sdirk3_check_staggered
SUBROUTINE nl_set_sdirk3_collect_stats ( id_id , sdirk3_collect_stats )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_collect_stats
  INTEGER id_id
  model_config_rec%sdirk3_collect_stats = sdirk3_collect_stats 
  RETURN
END SUBROUTINE nl_set_sdirk3_collect_stats
SUBROUTINE nl_set_sdirk3_conserv_tol_mass ( id_id , sdirk3_conserv_tol_mass )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_conserv_tol_mass
  INTEGER id_id
  model_config_rec%sdirk3_conserv_tol_mass = sdirk3_conserv_tol_mass 
  RETURN
END SUBROUTINE nl_set_sdirk3_conserv_tol_mass
SUBROUTINE nl_set_sdirk3_conserv_tol_energy ( id_id , sdirk3_conserv_tol_energy )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_conserv_tol_energy
  INTEGER id_id
  model_config_rec%sdirk3_conserv_tol_energy = sdirk3_conserv_tol_energy 
  RETURN
END SUBROUTINE nl_set_sdirk3_conserv_tol_energy
SUBROUTINE nl_set_sdirk3_conserv_tol_momentum ( id_id , sdirk3_conserv_tol_momentum )
  USE module_configure, ONLY : model_config_rec 
  real , INTENT(IN) :: sdirk3_conserv_tol_momentum
  INTEGER id_id
  model_config_rec%sdirk3_conserv_tol_momentum = sdirk3_conserv_tol_momentum 
  RETURN
END SUBROUTINE nl_set_sdirk3_conserv_tol_momentum
SUBROUTINE nl_set_sdirk3_enable_benchmarking ( id_id , sdirk3_enable_benchmarking )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_enable_benchmarking
  INTEGER id_id
  model_config_rec%sdirk3_enable_benchmarking = sdirk3_enable_benchmarking 
  RETURN
END SUBROUTINE nl_set_sdirk3_enable_benchmarking
SUBROUTINE nl_set_sdirk3_benchmark_warmup ( id_id , sdirk3_benchmark_warmup )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_benchmark_warmup
  INTEGER id_id
  model_config_rec%sdirk3_benchmark_warmup = sdirk3_benchmark_warmup 
  RETURN
END SUBROUTINE nl_set_sdirk3_benchmark_warmup
SUBROUTINE nl_set_sdirk3_benchmark_measure ( id_id , sdirk3_benchmark_measure )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_benchmark_measure
  INTEGER id_id
  model_config_rec%sdirk3_benchmark_measure = sdirk3_benchmark_measure 
  RETURN
END SUBROUTINE nl_set_sdirk3_benchmark_measure
SUBROUTINE nl_set_sdirk3_save_trajectory ( id_id , sdirk3_save_trajectory )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_save_trajectory
  INTEGER id_id
  model_config_rec%sdirk3_save_trajectory = sdirk3_save_trajectory 
  RETURN
END SUBROUTINE nl_set_sdirk3_save_trajectory
SUBROUTINE nl_set_sdirk3_checkpoint_interval ( id_id , sdirk3_checkpoint_interval )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_checkpoint_interval
  INTEGER id_id
  model_config_rec%sdirk3_checkpoint_interval = sdirk3_checkpoint_interval 
  RETURN
END SUBROUTINE nl_set_sdirk3_checkpoint_interval
SUBROUTINE nl_set_sdirk3_obs_aware_4dvar ( id_id , sdirk3_obs_aware_4dvar )
  USE module_configure, ONLY : model_config_rec 
  logical , INTENT(IN) :: sdirk3_obs_aware_4dvar
  INTEGER id_id
  model_config_rec%sdirk3_obs_aware_4dvar = sdirk3_obs_aware_4dvar 
  RETURN
END SUBROUTINE nl_set_sdirk3_obs_aware_4dvar
SUBROUTINE nl_set_sdirk3_obs_source_mode ( id_id , sdirk3_obs_source_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_obs_source_mode
  INTEGER id_id
  model_config_rec%sdirk3_obs_source_mode = sdirk3_obs_source_mode 
  RETURN
END SUBROUTINE nl_set_sdirk3_obs_source_mode
SUBROUTINE nl_set_sdirk3_obs_window_sync_mode ( id_id , sdirk3_obs_window_sync_mode )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: sdirk3_obs_window_sync_mode
  INTEGER id_id
  model_config_rec%sdirk3_obs_window_sync_mode = sdirk3_obs_window_sync_mode 
  RETURN
END SUBROUTINE nl_set_sdirk3_obs_window_sync_mode
SUBROUTINE nl_set_chem_opt ( id_id , chem_opt )
  USE module_configure, ONLY : model_config_rec 
  integer , INTENT(IN) :: chem_opt
  INTEGER id_id
  model_config_rec%chem_opt(id_id) = chem_opt
  RETURN
END SUBROUTINE nl_set_chem_opt

!ENDOFREGISTRYGENERATEDINCLUDE


