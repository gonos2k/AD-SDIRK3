#pragma once

#ifdef WRF_SDIRK3_PRESSURE_STAGE_PROBE
#include <torch/torch.h>

namespace wrf::sdirk3 {
struct StateLayout;
class UnifiedPreconditioner;
namespace pressure_stage_probe {
struct PressureGeometryEvidence {
    torch::Tensor c1f,c2f,rdn,msftx,msfty,msfux,msfuy,msfvx,msfvy,ground_phi;
    float rdx=0,rdy=0,gravity=0;
    bool whole_single_rank=false,packed_periodic_x_symmetric_y=false;
};
struct PressureEosContext {
    torch::Tensor ph,t,mu,mu_base,alb,p_base,c1h,c2h,rdnw,alt,p_pert;
    float rd=0,cv=0,cp=0,p0=0,t0=0;
    bool captured=false,duplicate=false,failed=false;
    PressureGeometryEvidence geometry;
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
void mark_capture_failed() noexcept;
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
