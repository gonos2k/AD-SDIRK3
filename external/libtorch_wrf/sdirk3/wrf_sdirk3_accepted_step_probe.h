#pragma once
#ifdef WRF_SDIRK3_ACCEPTED_STEP_PROBE
#include "wrf_sdirk3_terminal_jvp_probe.h"
#include "wrf_sdirk3_pressure_stage_probe.h"
#include "wrf_sdirk3_linear_delay_audit.h"
#include <sys/stat.h>
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
// Endpoint EOS data comes from the original RHS calls. These are measured
// extrema, not an error bound or a statement about the power implementation.
inline std::string eos_scalars(const wrf::sdirk3::pressure_stage_probe::PressureEosContext& b,
                               const wrf::sdirk3::pressure_stage_probe::PressureEosContext& t,
                               const Tensor& B,const Tensor& K0,const Tensor& K1,
                               int64_t ph_start,int64_t ph_size,int64_t t_start,int64_t t_size) {
    torch::NoGradGuard guard;
    using Context=wrf::sdirk3::pressure_stage_probe::PressureEosContext;
    TORCH_CHECK(b.captured && !b.failed && !b.duplicate && t.captured && !t.failed && !t.duplicate,
                "EOS endpoint capture unavailable");
    const auto same=[](const Tensor& x,const Tensor& y) {
        return x.defined() && y.defined() && x.is_cpu() && y.is_cpu() &&
            x.scalar_type()==y.scalar_type() && x.sizes()==y.sizes() &&
            torch::isfinite(x).all().item<bool>() && torch::isfinite(y).all().item<bool>() &&
            torch::equal(x,y) && wrf_terminal_jvp::hash(x)==wrf_terminal_jvp::hash(y);
    };
    TORCH_CHECK(std::isfinite(b.rd) && std::isfinite(b.cv) && std::isfinite(b.cp) &&
        std::isfinite(b.p0) && std::isfinite(b.t0) && same(b.c1h,t.c1h) && same(b.c2h,t.c2h) && same(b.rdnw,t.rdnw) &&
        same(b.mu_base,t.mu_base) && same(b.p_base,t.p_base) && same(b.alb,t.alb) &&
        std::memcmp(&b.rd,&t.rd,sizeof(float))==0 && std::memcmp(&b.cv,&t.cv,sizeof(float))==0 &&
        std::memcmp(&b.cp,&t.cp,sizeof(float))==0 && std::memcmp(&b.p0,&t.p0,sizeof(float))==0 &&
        std::memcmp(&b.t0,&t.t0,sizeof(float))==0, "EOS coefficients changed");
    TORCH_CHECK(b.t.dim()==3 && t.t.sizes()==b.t.sizes() && b.ph.dim()==3 &&
        t.ph.sizes()==b.ph.sizes() && b.ph.size(0)==b.t.size(0) &&
        b.ph.size(1)==b.t.size(1)+1 && b.ph.size(2)==b.t.size(2) &&
        b.alb.sizes()==b.t.sizes() && b.alt.sizes()==b.t.sizes() &&
        t.alt.sizes()==b.t.sizes() && b.p_pert.sizes()==b.t.sizes() &&
        t.p_pert.sizes()==b.t.sizes() && b.c1h.dim()==1 && b.c2h.dim()==1 && b.rdnw.dim()==1 &&
        b.c1h.numel()>=b.t.size(1) && b.c2h.numel()>=b.t.size(1) && b.rdnw.numel()>=b.t.size(1) &&
        ph_size==b.ph.numel() && t_size==b.t.numel() && ph_start>=0 && t_start>=0 &&
        B.defined() && B.dim()==1 && B.sizes()==K0.sizes() && B.sizes()==K1.sizes() &&
        ph_start+ph_size<=B.numel() && t_start+t_size<=B.numel(), "invalid EOS or block shape");
    const auto vector_json=[](const Tensor& x) {
        (void)extremes(x); auto y=x.detach().to(torch::kFloat64).contiguous();
        std::ostringstream z; z << std::setprecision(17) << "[";
        for(int64_t i=0;i<y.numel();++i) {if(i) z << ','; z << y[i].item<double>();}
        z << "]"; return z.str();
    };
    const auto level_json=[](const Tensor& x) {
        TORCH_CHECK(x.dim()==3, "per-level tensor must be rank 3");
        std::ostringstream z; z << std::setprecision(17) << "[";
        for(int64_t i=0;i<x.size(1);++i) {
            auto level=x.select(1,i); auto ex=extremes(level);
            if(i) z << ','; z << "{\"min\":" << ex.min << ",\"max\":"
                << level.max().item<double>() << ",\"absmax\":" << ex.max_abs << "}";
        }
        z << "]"; return z.str();
    };
    const auto endpoint=[&](const Context& e) {
        TORCH_CHECK(e.t.scalar_type()==torch::kFloat64 && e.ph.scalar_type()==torch::kFloat64 &&
            e.alt.scalar_type()==torch::kFloat64 && e.p_pert.scalar_type()==torch::kFloat64,
            "EOS state/output requires FP64");
        const auto dz=e.ph.slice(1,1,e.ph.size(1))-e.ph.slice(1,0,e.ph.size(1)-1);
        return std::string("{\"theta_pert\":")+level_json(e.t)+
            ",\"dphi_pert_computed\":"+level_json(dz)+",\"alb\":"+level_json(e.alb)+
            ",\"alt\":"+level_json(e.alt)+",\"p_pert\":"+level_json(e.p_pert)+"}";
    };
    const auto block=[](const Tensor& x,int64_t start,int64_t size) {
        return extremes(x.slice(0,start,start+size)).max_abs;
    };
    std::ostringstream out; out << std::setprecision(17)
        << "{\"scope\":\"same_call_EOS_endpoint_outputs_no_error_bound\",\"levels\":" << b.t.size(1)
        << ",\"c1h_dtype\":" << int(b.c1h.scalar_type()) << ",\"c1h_hash\":"
        << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(b.c1h)))
        << ",\"c1h\":" << vector_json(b.c1h) << ",\"c2h_dtype\":" << int(b.c2h.scalar_type())
        << ",\"c2h_hash\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(b.c2h)))
        << ",\"c2h\":" << vector_json(b.c2h) << ",\"rdnw_dtype\":" << int(b.rdnw.scalar_type())
        << ",\"rdnw_hash\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(b.rdnw)))
        << ",\"rdnw\":" << vector_json(b.rdnw)
        << ",\"rd\":" << b.rd << ",\"cv\":" << b.cv << ",\"cp\":" << b.cp
        << ",\"p0\":" << b.p0 << ",\"t0\":" << b.t0
        << ",\"B_ph_absmax\":" << block(B,ph_start,ph_size)
        << ",\"K0_ph_absmax\":" << block(K0,ph_start,ph_size)
        << ",\"K1_ph_absmax\":" << block(K1,ph_start,ph_size)
        << ",\"B_t_absmax\":" << block(B,t_start,t_size)
        << ",\"K0_t_absmax\":" << block(K0,t_start,t_size)
        << ",\"K1_t_absmax\":" << block(K1,t_start,t_size)
        << ",\"base\":" << endpoint(b) << ",\"accepted\":" << endpoint(t) << "}";
    return out.str();
}
// Raw EOS subroutine inputs/outputs only. The exclusive directory makes the
// existing NPY writer's files unique for this terminal diagnostic invocation.
inline std::string write_eos_raw(
        const wrf::sdirk3::pressure_stage_probe::PressureEosContext& base,
        const wrf::sdirk3::pressure_stage_probe::PressureEosContext& accepted) {
    torch::NoGradGuard guard;
    TORCH_CHECK(::mkdir("eos_raw",0700)==0, "cannot create exclusive eos_raw directory");
    const std::array<std::pair<const char*,const Tensor*>,16> fields={{
        {"base_ph",&base.ph},{"base_t",&base.t},{"base_mu",&base.mu},
        {"base_alt",&base.alt},{"base_p_pert",&base.p_pert},
        {"accepted_ph",&accepted.ph},{"accepted_t",&accepted.t},{"accepted_mu",&accepted.mu},
        {"accepted_alt",&accepted.alt},{"accepted_p_pert",&accepted.p_pert},
        {"mu_base",&base.mu_base},{"alb",&base.alb},{"p_base",&base.p_base},
        {"c1h",&base.c1h},{"c2h",&base.c2h},{"rdnw",&base.rdnw}
    }};
    std::ostringstream out;
    out << "{\"scope\":\"rounded_EOS_subroutine_inputs_and_outputs_only\","
        << "\"format\":\"flat_npy_little_endian_float64\",\"fnv_scope\":\"flat_data_bytes\",\"files\":[";
    for(size_t i=0;i<fields.size();++i) {
        const auto& x=*fields[i].second;
        TORCH_CHECK(x.defined() && x.is_cpu() && x.numel()>0 &&
            (x.scalar_type()==torch::kFloat32 || x.scalar_type()==torch::kFloat64) &&
            torch::isfinite(x).all().item<bool>(), "invalid EOS raw input ",fields[i].first);
        const auto flat=x.detach().to(torch::kFloat64).contiguous().view({-1});
        const std::string path=std::string("eos_raw/")+fields[i].first+".npy";
        TORCH_CHECK(wrf::sdirk3::linear_delay_audit::write_npy(path.c_str(),flat),
                    "EOS raw write failed: ",path);
        if(i) out << ',';
        out << "{\"name\":" << wrf_terminal_jvp::quote(fields[i].first)
            << ",\"path\":" << wrf_terminal_jvp::quote(path)
            << ",\"source_dtype\":" << int(x.scalar_type()) << ",\"shape\":[";
        for(int64_t j=0;j<x.dim();++j) {if(j) out << ','; out << x.size(j);}
        out << "],\"elements\":" << x.numel() << ",\"data_bytes\":" << flat.nbytes()
            << ",\"fnv64\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(flat))) << "}";
    }
    out << "]}";
    return out.str();
}
inline std::string w_runtime(
        const wrf::sdirk3::pressure_stage_probe::WConsumerEvidence& b,
        const wrf::sdirk3::pressure_stage_probe::WConsumerEvidence& a,
        const Tensor& sinv,int64_t start,int64_t count) {
    torch::NoGradGuard guard;
    using WConsumer=wrf::sdirk3::pressure_stage_probe::WConsumerEvidence;
    const auto valid=[](const WConsumer& w) {
        return w.interior_seen && w.top_seen && w.mass_seen && !w.duplicate && !w.failed &&
            w.canonical && w.cq1_is_one && w.rdn_f32.defined() && w.rdn_f32.is_cpu() &&
            w.rdn_f32.dim()==1 && w.rdn_f32.scalar_type()==torch::kFloat32 && w.rdn_f32.numel()>0 &&
            w.rdn_used_end>1 && w.rdn_used_end<=w.rdn_f32.numel() &&
            torch::isfinite(w.rdn_f32).all().item<bool>() &&
            w.rdn_f32.slice(0,1,w.rdn_used_end).gt(0).all().item<bool>() &&
            std::isfinite(w.gravity) && w.gravity>0 && std::isfinite(w.top_rdnw) && w.top_rdnw>0 &&
            std::isfinite(w.msfty_min) && std::isfinite(w.msfty_max) && w.msfty_min>0 &&
            std::isfinite(w.level_mass_min) && std::isfinite(w.level_mass_max) && w.level_mass_min>0 &&
            std::isfinite(w.velocity_mass_min) && std::isfinite(w.velocity_mass_max) && w.velocity_mass_min>0 &&
            std::isfinite(w.alpha_min) && std::isfinite(w.alpha_max) && w.alpha_min>0 &&
            w.msfty_min<=w.msfty_max && w.level_mass_min<=w.level_mass_max &&
            w.velocity_mass_min<=w.velocity_mass_max && w.alpha_min<=w.alpha_max;
    };
    TORCH_CHECK(valid(b) && valid(a) && b.rdn_used_end==a.rdn_used_end && b.rdn_f32.sizes()==a.rdn_f32.sizes() &&
        torch::equal(b.rdn_f32,a.rdn_f32) &&
        std::memcmp(&b.gravity,&a.gravity,sizeof(float))==0 &&
        std::memcmp(&b.top_rdnw,&a.top_rdnw,sizeof(float))==0 &&
        b.msfty_min==a.msfty_min && b.msfty_max==a.msfty_max,
        "missing, invalid, or changed W consumer evidence");
    TORCH_CHECK(sinv.defined() && sinv.is_cpu() && sinv.dim()==1 && sinv.scalar_type()==torch::kFloat64 &&
        start>=0 && count>0 && start+count<=sinv.numel(), "invalid W inverse-scale layout");
    const auto w=sinv.slice(0,start,start+count);
    TORCH_CHECK(torch::isfinite(w).all().item<bool>() && w.gt(0).all().item<bool>(), "invalid W inverse-scale");
    const auto endpoint=[](const WConsumer& x) {
        const auto r=x.rdn_f32.detach().to(torch::kFloat64).contiguous();
        std::ostringstream o; o << std::setprecision(17)
          << "{\"rdn_source_dtype\":" << int(x.rdn_f32.scalar_type()) << ",\"rdn_count\":" << r.numel()
          << ",\"rdn_used_begin\":1,\"rdn_used_end_exclusive\":" << x.rdn_used_end
          << ",\"rdn_fnv64_f32\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(x.rdn_f32)))
          << ",\"rdn_values\":[";
        for(int64_t i=0;i<r.numel();++i) {if(i) o << ','; o << r[i].item<double>();}
        o << "],\"gravity_f32\":" << static_cast<double>(x.gravity)
          << ",\"top_rdnw_f32\":" << static_cast<double>(x.top_rdnw)
          << ",\"canonical_horizontal\":true,\"cq1_all_one\":true"
          << ",\"msfty_min\":" << x.msfty_min << ",\"msfty_max\":" << x.msfty_max
          << ",\"level_mass_w_min\":" << x.level_mass_min << ",\"level_mass_w_max\":" << x.level_mass_max
          << ",\"velocity_mass_w_min\":" << x.velocity_mass_min << ",\"velocity_mass_w_max\":" << x.velocity_mass_max
          << ",\"alpha_w_min\":" << x.alpha_min << ",\"alpha_w_max\":" << x.alpha_max << "}";
        return o.str();
    };
    std::ostringstream out; out << std::setprecision(17)
        << "{\"scope\":\"actual_W_consumers_and_cloned_metric\",\"fixed_coefficients_equal\":true"
        << ",\"base\":" << endpoint(b) << ",\"accepted\":" << endpoint(a)
        << ",\"metric\":{\"block\":\"rw\",\"start\":" << start << ",\"count\":" << count
        << ",\"dtype\":" << int(sinv.scalar_type()) << ",\"min\":" << w.min().item<double>()
        << ",\"max\":" << w.max().item<double>() << ",\"full_S_inv_fnv64\":"
        << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(sinv))) << "}}";
    return out.str();
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
