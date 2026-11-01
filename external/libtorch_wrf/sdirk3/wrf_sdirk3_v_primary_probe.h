#pragma once

#ifdef WRF_SDIRK3_PRESSURE_A5
#include <torch/torch.h>
#include <array>
#include <memory>

namespace wrf::sdirk3 {
struct StateLayout;
class NativePressureReference;
namespace pressure_stage_probe { struct PressureEosContext; struct PressureVPrimaryContext; }
namespace v_primary_probe {
struct Actions {
    torch::Tensor phi,pressure,alpha,fwad;
    std::array<torch::Tensor,3> fd;
    double primal_dpy_max=0, primal_level_mass_max=0, primal_velocity_mass_max=0;
    double primal_alpha_v_max=0, primal_ratio_max=0, projected_state_max=0;
    double primal_alpha_max=0, primal_pressure_max=0;
};
class PreparedPrimary {
public:
    PreparedPrimary(const torch::Tensor& U_eval,const StateLayout& layout,
                    const pressure_stage_probe::PressureEosContext& eos,
                    const pressure_stage_probe::PressureVPrimaryContext& vctx);
    ~PreparedPrimary();
    PreparedPrimary(PreparedPrimary&&) noexcept;
    PreparedPrimary& operator=(PreparedPrimary&&) noexcept;
    Actions apply_terms(const torch::Tensor& z_phys) const;
    torch::Tensor apply_sum(const torch::Tensor& z_phys) const;
    // Physical mass-cell cores, including Phi0; returns interior V faces.
    torch::Tensor apply_core(const torch::Tensor& phi_nodes, const torch::Tensor& t,
                             const torch::Tensor& mu) const;
private:
    friend Actions evaluate(const torch::Tensor&,const torch::Tensor&,const StateLayout&,
                 const pressure_stage_probe::PressureEosContext&,const pressure_stage_probe::PressureVPrimaryContext&);
    Actions compute(const torch::Tensor& z_phys,bool summed) const;
    Actions compute_core(const torch::Tensor& phi_nodes,const torch::Tensor& t,
                         const torch::Tensor& mu,bool summed) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Actions evaluate(const torch::Tensor& U_eval,const torch::Tensor& z_phys,const StateLayout& layout,
                 const pressure_stage_probe::PressureEosContext& eos,
                 const pressure_stage_probe::PressureVPrimaryContext& vctx);
// Observation only: v,z,Az are the actual same first Arnoldi vectors already
// transformed into physical rate coordinates. No native RHS/M call occurs.
void run(const torch::Tensor& U_eval,const torch::Tensor& v_phys,
         const torch::Tensor& z_phys,const torch::Tensor& Az_phys,
         const torch::Tensor& v_scaled,const torch::Tensor& z_scaled,
         const torch::Tensor& Az_scaled,const torch::Tensor& S_diag,double a,
         const StateLayout& layout,const pressure_stage_probe::PressureEosContext& eos,
         const pressure_stage_probe::PressureVPrimaryContext& vctx,
         const NativePressureReference& P0,const torch::Tensor& base_z_scaled=torch::Tensor());
}
}
#endif
