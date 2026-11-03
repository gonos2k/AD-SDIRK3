#pragma once
#ifdef WRF_SDIRK3_ACCEPTED_STEP_PROBE
#include "wrf_sdirk3_terminal_jvp_probe.h"
#include <cmath>

namespace wrf::sdirk3::accepted_step_probe {
using Tensor = torch::Tensor;
inline bool write_exclusive(const std::string& path,const std::string& body) {
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd<0) return false;
    const auto line=body+"\n";
    const bool ok=::write(fd,line.data(),line.size())==static_cast<ssize_t>(line.size());
    return ::close(fd)==0 && ok;
}
[[noreturn]] inline void finish(bool ok, const std::string& body) noexcept {
    try { ok=write_exclusive("accepted_step_probe.json",body) && ok; }
    catch (...) { ok=false; }
    std::fprintf(stderr,"[ACCEPTED_STEP] status=%s forecast_complete=false\n",ok?"completed":"failed");
    std::fflush(stderr); std::fflush(stdout); std::_Exit(ok?86:87);
}
struct TerminalGuard {
    bool active;
    explicit TerminalGuard(bool target):active(target) {}
    ~TerminalGuard() noexcept {
        if(active) finish(false,"{\"status\":\"unavailable\",\"forecast_complete\":false}");
    }
};
struct Extremes { double min,max_abs; };
inline Extremes extremes(const Tensor& x) {
    TORCH_CHECK(x.defined() && x.is_cpu() && x.numel()>0 && x.is_floating_point() &&
                torch::isfinite(x).all().item<bool>(), "invalid scalar-extrema input");
    auto d=x.detach().to(torch::kFloat64);
    return {d.min().item<double>(),d.abs().max().item<double>()};
}
inline std::string measure(const Tensor& base_k,const Tensor& base_r,const Tensor& scale_inv,
                           const Tensor& post_k,const Tensor& post_r,
                           const Tensor& observed_k,const Tensor& observed_r,
                           const Tensor& eos_mu,const Tensor& eos_mu_base,const Tensor& base_state,
                           int64_t mu_start,int64_t mu_size,float native_before,float native_after) {
    torch::NoGradGuard guard;
    TORCH_CHECK(base_k.defined() && base_r.defined() && scale_inv.defined() && post_k.defined() &&
        post_r.defined() && observed_k.defined() && observed_r.defined() && base_state.defined() &&
        base_k.is_cpu() && base_k.scalar_type()==torch::kFloat64 && base_k.dim()==1 &&
        base_k.sizes()==base_r.sizes() && base_k.sizes()==scale_inv.sizes() &&
        base_k.sizes()==post_k.sizes() && base_k.sizes()==post_r.sizes() &&
        base_k.sizes()==observed_k.sizes() && base_k.sizes()==observed_r.sizes() &&
        base_k.sizes()==base_state.sizes() && base_r.scalar_type()==torch::kFloat64 &&
        post_k.scalar_type()==torch::kFloat64 && post_r.scalar_type()==torch::kFloat64 &&
        observed_k.scalar_type()==torch::kFloat64 && observed_r.scalar_type()==torch::kFloat64 &&
        scale_inv.scalar_type()==torch::kFloat64 && base_state.scalar_type()==torch::kFloat64 &&
        mu_start>=0 && mu_size>0 && mu_start+mu_size<=base_k.numel() &&
        eos_mu.defined() && eos_mu_base.defined() && eos_mu.dim()==2 && eos_mu_base.dim()==2 &&
        eos_mu.scalar_type()==torch::kFloat64 && eos_mu_base.scalar_type()==torch::kFloat64 &&
        eos_mu.sizes()==eos_mu_base.sizes() && eos_mu.numel()==mu_size && eos_mu_base.numel()==mu_size &&
        torch::isfinite(base_k).all().item<bool>() && torch::isfinite(base_r).all().item<bool>() &&
        torch::isfinite(post_k).all().item<bool>() && torch::isfinite(post_r).all().item<bool>() &&
        torch::isfinite(scale_inv).all().item<bool>() && scale_inv.gt(0).all().item<bool>() &&
        torch::equal(post_k,observed_k) && torch::equal(post_r,observed_r),
        "invalid or unmatched accepted residual pair");
    auto mu=extremes(eos_mu), mub=extremes(eos_mu_base);
    const auto block=[&](const Tensor& x) {return extremes(x.slice(0,mu_start,mu_start+mu_size)).max_abs;};
    const auto before=(scale_inv*base_r).norm().item<double>();
    const auto after=(scale_inv*post_r).norm().item<double>();
    TORCH_CHECK(std::isfinite(before) && std::isfinite(after) && std::isfinite(native_before) &&
                std::isfinite(native_after), "nonfinite merit");
    std::ostringstream out; out << std::setprecision(17)
        << "{\"status\":\"completed\",\"forecast_complete\":false,\"context_scope\":\"observed_subset\","
        << "\"pair_equality\":\"numeric_finite\",\"mask_scope\":\"identity\",\"N\":" << base_k.numel()
        << ",\"scaled_l2_before\":" << before << ",\"scaled_l2_after\":" << after
        << ",\"native_scaled_rms_before\":" << native_before << ",\"native_scaled_rms_after\":" << native_after
        << ",\"delta_K_l2\":" << (post_k-base_k).norm().item<double>()
        << ",\"delta_K_scaled_l2\":" << (scale_inv*(post_k-base_k)).norm().item<double>()
        << ",\"S_inv_hash\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(scale_inv)))
        << ",\"base_K_hash\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(base_k)))
        << ",\"base_R_hash\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(base_r)))
        << ",\"post_K_hash\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(post_k)))
        << ",\"post_R_hash\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(post_r)))
        << ",\"mu_min\":" << mu.min << ",\"mu_absmax\":" << mu.max_abs
        << ",\"mub_min\":" << mub.min << ",\"mub_absmax\":" << mub.max_abs
        << ",\"B_mu_absmax\":" << block(base_state) << ",\"K_before_mu_absmax\":" << block(base_k)
        << ",\"K_after_mu_absmax\":" << block(post_k) << ",\"mu_count\":" << mu_size
        << ",\"mu_dtype\":" << static_cast<int>(eos_mu.scalar_type())
        << ",\"mub_dtype\":" << static_cast<int>(eos_mu_base.scalar_type()) << "}";
    return out.str();
}
}
#endif
