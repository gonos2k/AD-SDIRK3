#pragma once

#ifdef WRF_SDIRK3_PRESSURE_STAGE_PROBE
#include "wrf_sdirk3_pressure_stage_probe.h"
#include "wrf_sdirk3_reference_pressure_model.h"
#include "wrf_sdirk3_pressure_packed_adapter.h"
#include "wrf_sdirk3_state_layout.h"
#include <memory>
#include <string>

namespace wrf::sdirk3 {
class UnifiedPreconditioner;
struct WRFGridInfo;
struct NativePressureProfileReceipt {
    int64_t nx_physical=0,ny_physical=0,nz=0;
    double a=0,gravity=0,dx=0,dy=0;
    float rdx=0,rdy=0;
    std::string provenance="captured projected RHS EOS/geometry; raw grid vertical coefficients checked; FP32 RHS rdx/rdy; flat ground checked, higher eta slopes not checked";
    std::string averaging="FP64 ordered arithmetic mean on independent j=0..ny-2,i=0..nx-2";
    std::string recovery="H=mean(c1*M+c2), nu=mean(t*c1/(H+1e-10f)), dnw=-1/live_rdnw";
    std::vector<double> p_ref,alpha_ref,theta_ref;
    ReferencePressureProfiles profiles;
};
// Pure profile extraction is separately callable on deterministic fixture data.
NativePressureProfileReceipt derive_native_pressure_profiles(
    const pressure_stage_probe::PressureEosContext& eos, const WRFGridInfo& grid,
    const StateLayout& layout, double a);
class NativePressureReference {
public:
    static std::unique_ptr<NativePressureReference> create(
        const pressure_stage_probe::PressureEosContext& eos,
        const UnifiedPreconditioner& raw, const StateLayout& layout,
        const torch::Tensor& U_eval, double a);
    // Physical packed inverse using the shared projected/complement adapter.
    torch::Tensor apply(const torch::Tensor& rhs) const;
#ifdef WRF_SDIRK3_PRESSURE_A5
    torch::Tensor apply_operator(const torch::Tensor& physical_direction) const;
#endif
    const NativePressureProfileReceipt& receipt() const { return receipt_; }
private:
    NativePressureReference(const StateLayout& layout, int64_t nx, int64_t ny, int64_t nz,
                            NativePressureProfileReceipt receipt);
    StateLayout layout_;
    int64_t nx_,ny_,nz_,n_,m_;
    NativePressureProfileReceipt receipt_;
    ReferencePressureModel model_;
    PressurePackedAdapter adapter_;
};
}
#endif
