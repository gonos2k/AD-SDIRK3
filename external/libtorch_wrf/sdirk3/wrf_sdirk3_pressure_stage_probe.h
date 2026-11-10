#pragma once

#ifdef WRF_SDIRK3_PRESSURE_STAGE_PROBE
#include <torch/torch.h>
#include <array>
#include <vector>
#include <cstdint>
#ifdef WRF_SDIRK3_PRESSURE_A5
#include <functional>
#endif

namespace wrf::sdirk3 {
struct StateLayout;
class UnifiedPreconditioner;
namespace pressure_stage_probe {
struct PressureGeometryEvidence {
    torch::Tensor c1f,c2f,rdn,msftx,msfty,msfux,msfuy,msfvx,msfvy,ground_phi,theta_base;
    float rdx=0,rdy=0,gravity=0;
    bool whole_single_rank=false,packed_periodic_x_symmetric_y=false;
};
#ifdef WRF_SDIRK3_PRESSURE_A5
// Captured only from the original base F; callbacks are synchronous and must
// not outlive its Tile solve. They do not evaluate the RHS.
struct PressureVPrimaryContext {
    torch::Tensor cqv_at_v,level_mass_v,velocity_mass_v,alpha_v,msf_ratio_v,dpy_primary;
    float rdy=0,velocity_eps=0;
    bool canonical_packed_dry=false,base_state_branch=false,use_wrf_mass_flux=false,native_nh=false;
    bool captured=false,failed=false,duplicate=false;
    std::function<torch::Tensor(const torch::Tensor&)> project_native,avg_y_3d,avg_y_2d,level_mass_v_of;
};
#endif
struct WConsumerEvidence {
    torch::Tensor rdn_f32;
    float gravity=0, top_rdnw=0;
    int64_t rdn_used_end=0;
    double msfty_min=0, msfty_max=0, level_mass_min=0, level_mass_max=0;
    double velocity_mass_min=0, velocity_mass_max=0, alpha_min=0, alpha_max=0;
    bool canonical=false, cq1_is_one=false;
    bool interior_seen=false, top_seen=false, mass_seen=false, duplicate=false, failed=false;
};
struct WReferenceEvidence {
    torch::Tensor mass_rdnw;
    std::array<uint64_t,5> input_hashes{};
    std::array<int,5> input_dtypes{};
    std::array<std::vector<int64_t>,5> input_shapes;
    float rdx=0, rdy=0;
    int qv_status=0, cqw_status=0; // 0 absent, 1 present zero, 2 invalid/nonzero
    bool seen=false, duplicate=false, failed=false, profile_ok=false, pre_pgf_zero=false, w_seen=false;
};
struct PressureEosContext {
    torch::Tensor ph,t,mu,mu_base,alb,p_base,c1h,c2h,rdnw,alt,p_pert;
    float rd=0,cv=0,cp=0,p0=0,t0=0;
    bool captured=false,duplicate=false,failed=false;
    PressureGeometryEvidence geometry;
    WConsumerEvidence w_consumer;
    WReferenceEvidence w_reference;
#ifdef WRF_SDIRK3_PRESSURE_A5
    PressureVPrimaryContext v_primary;
#endif
};
class ScopedEosCapture {
public:
    explicit ScopedEosCapture(PressureEosContext& context);
    ~ScopedEosCapture() noexcept;
    ScopedEosCapture(const ScopedEosCapture&)=delete;
    ScopedEosCapture& operator=(const ScopedEosCapture&)=delete;
private:
    PressureEosContext* previous_;
};
bool capture_active() noexcept;
#ifdef WRF_SDIRK3_PRESSURE_A5
void capture_v_primary(const PressureVPrimaryContext& input);
void mark_v_primary_capture_failed() noexcept;
#endif
void mark_capture_failed() noexcept;
void capture_w_reference_mass(const torch::Tensor& rdnw,const torch::Tensor& u,const torch::Tensor& v,
    const torch::Tensor& mu,const torch::Tensor& mub,const torch::Tensor& qv,const torch::Tensor& cqw,
    const std::array<torch::Tensor,6>& maps,float rdx,float rdy,bool profile_ok) noexcept;
void capture_w_reference_step(const torch::Tensor& w,const torch::Tensor& pre_pgf_rw) noexcept;
void capture_w_interior(const torch::Tensor& rdn,int64_t used_end,float gravity,bool canonical,
                        const torch::Tensor& cq1,const torch::Tensor& msfty) noexcept;
void capture_w_top(float rdnw) noexcept;
void capture_w_mass(const torch::Tensor& level_mass,const torch::Tensor& velocity_mass,
                    const torch::Tensor& alpha) noexcept;
void capture_eos(const torch::Tensor& ph,const torch::Tensor& t,const torch::Tensor& mu,
                 const torch::Tensor& mu_base,const torch::Tensor& alb,const torch::Tensor& p_base,
                 const torch::Tensor& c1h,const torch::Tensor& c2h,const torch::Tensor& rdnw,
                 const torch::Tensor& alt,const torch::Tensor& p_pert,
                 float rd,float cv,float cp,float p0,float t0,const PressureGeometryEvidence& geometry);
void run(const torch::Tensor& U_eval, const torch::Tensor& F_eval, const torch::Tensor& K,
         const torch::Tensor& b_phys, const torch::Tensor& x_phys,
         const torch::Tensor& b_scaled, const torch::Tensor& x0_scaled,
         const torch::Tensor& x_scaled, const torch::Tensor& r_true_scaled,
         const torch::Tensor& S_diag,
         const torch::Tensor& S_inv_diag, const StateLayout& layout,
         double a, int stage, const PressureEosContext& eos_context, const UnifiedPreconditioner& P);
}
}
#endif
