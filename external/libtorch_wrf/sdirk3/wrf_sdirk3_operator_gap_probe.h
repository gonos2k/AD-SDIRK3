#pragma once
#ifdef WRF_SDIRK3_OPERATOR_GAP_PROBE
#include "wrf_sdirk3_terminal_jvp_probe.h"
#include <array>
#include <functional>

namespace wrf::sdirk3::operator_gap_probe {
using Tensor = torch::Tensor;
using Function = std::function<Tensor(const Tensor&)>;

// All calls are synchronous, and the caller must terminate rather than resume Newton.
inline std::string measure(const Tensor& base, const Tensor& k, const Tensor& f,
                           const Tensor& evaluated, const Tensor& p, const Tensor& scale, const Tensor& mask,
                           double a, const Function& project, const Function& apply,
                           const Function& rhs, const std::function<bool()>& context_ok) {
    TORCH_CHECK(base.defined() && k.defined() && f.defined() && p.defined() && scale.defined() &&
        base.is_cpu() && base.scalar_type()==torch::kFloat64 && base.sizes()==k.sizes() &&
        k.sizes()==f.sizes() && k.sizes()==p.sizes() && k.sizes()==scale.sizes() &&
        torch::isfinite(base).all().item<bool>() && torch::isfinite(k).all().item<bool>() &&
        torch::isfinite(f).all().item<bool>() && torch::isfinite(p).all().item<bool>() &&
        torch::isfinite(scale).all().item<bool>() && scale.gt(0).all().item<bool>() &&
        std::isfinite(a) && a!=0 && context_ok(), "invalid probe inputs or context");
    // The RHS may return a view of mutable scratch. Preserve the original result
    // before any further callback can overwrite it.
    const auto base_f=f.detach().clone();
    const auto matching_metadata=[&](const Tensor& x) {
        return x.scalar_type()==evaluated.scalar_type() && x.device()==evaluated.device() &&
               x.requires_grad()==evaluated.requires_grad();
    };
    TORCH_CHECK(matching_metadata(base+a*k), "base RHS input metadata mismatch");
    const auto norm=[](const Tensor& x) { return x.norm().item<double>(); };
    const double pn=norm(p/scale);
    TORCH_CHECK(std::isfinite(pn) && pn>0, "zero or invalid Newton direction");
    // Deterministic non-periodic pattern exercises periodic aliases and symmetric boundaries.
    auto index=torch::arange(k.numel(), k.options());
    auto seed=scale*torch::sin(index*0.731 + 0.17);
    auto q0=project(seed).detach().clone();
    auto q1=project(q0).detach().clone();
    auto b0=seed-q0;
    auto qb=project(b0).detach().clone();
    TORCH_CHECK(q0.sizes()==k.sizes() && q1.sizes()==k.sizes() && qb.sizes()==k.sizes() &&
        torch::isfinite(q0).all().item<bool>() && torch::isfinite(q1).all().item<bool>() &&
        torch::isfinite(qb).all().item<bool>(), "invalid native projection");
    const double qerr=norm(q1-q0), berr=norm(qb), qn=norm(q0/scale), bn=norm(b0/scale);
    TORCH_CHECK(qerr==0 && berr==0 && qn>0 && bn>0 && std::isfinite(qn) && std::isfinite(bn),
                "projection invalid or complementary direction unavailable");
    auto q=q0*(pn/qn), b=b0*(pn/bn);
    const double cosine=((q/scale)*(b/scale)).sum().item<double>()/(pn*pn);
    const double pq=((p/scale)*(q/scale)).sum().item<double>()/(pn*pn);
    const double pb=((p/scale)*(b/scale)).sum().item<double>()/(pn*pn);
    TORCH_CHECK(std::isfinite(cosine) && std::abs(cosine)<0.999999 &&
                std::isfinite(pq) && std::abs(pq)<0.999999 && std::isfinite(pb),
                "directions not independent");
    const double p_projection_error=norm(project(p)-p);
    const auto base_f_hash=wrf_terminal_jvp::hash(base_f);
    const auto residual=k-base_f;
    const double residual_norm=norm(residual/scale);
    TORCH_CHECK(std::isfinite(residual_norm) && residual_norm>0, "zero or invalid base residual");
    const auto mask_or_one=mask.defined() ? mask : torch::ones_like(k);
    TORCH_CHECK(mask_or_one.sizes()==k.sizes() && context_ok(), "invalid mask or context");
    auto check_center=[&]() {
        auto center=rhs(base+a*k).detach().clone();
        TORCH_CHECK(wrf_terminal_jvp::compare(center,base_f).exact && context_ok(), "center RHS or context changed");
    };
    std::ostringstream out;
    out << std::setprecision(17) << "{\"status\":\"completed\",\"forecast_complete\":false,"
        << "\"context_scope\":\"observed_fixed_inputs_only\",\"exact_forcing_certified\":false,"
        << "\"grad_mode\":" << (at::GradMode::is_enabled()?"true":"false")
        << ",\"base_requires_grad\":" << (base.requires_grad()?"true":"false")
        << ",\"K_requires_grad\":" << (k.requires_grad()?"true":"false")
        << ",\"used_p_requires_grad\":" << (p.requires_grad()?"true":"false")
        << ",\"rhs_input_requires_grad\":" << (evaluated.requires_grad()?"true":"false")
        << ",\"base_F_fnv64\":" << wrf_terminal_jvp::quote(std::to_string(base_f_hash))
        << ",\"projection_idempotence_l2\":" << qerr << ",\"boundary_Q_l2\":" << berr
        << ",\"used_p_Q_error_l2\":" << p_projection_error << ",\"q_b_scaled_cosine\":" << cosine
        << ",\"p_q_scaled_cosine\":" << pq << ",\"p_b_scaled_cosine\":" << pb
        << ",\"base_G_scaled_l2\":" << residual_norm << ",\"directions\":[";
    const std::array<Tensor,3> dirs={p,q,b};
    const std::array<const char*,3> names={"used_p","Q_seed","boundary_complement"};
    const std::array<double,3> eps={1e-2,1e-3,1e-4};
    check_center();
    for (size_t i=0;i<dirs.size();++i) {
        const auto& d=dirs[i];
        auto ahat=apply(d).detach().clone();
        TORCH_CHECK(ahat.sizes()==k.sizes() && torch::isfinite(ahat).all().item<bool>() && context_ok(),
                    "invalid selected JVP or changed context");
        if(i) out << ',';
        out << "{\"name\":" << wrf_terminal_jvp::quote(names[i])
            << ",\"direction_fnv64\":" << wrf_terminal_jvp::quote(std::to_string(wrf_terminal_jvp::hash(d)))
            << ",\"scaled_norm\":" << norm(d/scale) << ",\"Q_direction_l2\":" << norm(project(d))
            << ",\"Ahat_scaled_l2\":" << norm(ahat/scale)
            << ",\"linear_residual_over_base_G\":" << norm((residual+ahat)/scale)/residual_norm
            << ",\"samples\":[";
        Tensor previous;
        for(size_t j=0;j<eps.size();++j) {
            const double h=eps[j]; auto kp=k+h*d, km=k-h*d;
            auto yp=base+a*kp, ym=base+a*km;
            auto y0=base+a*k;
            TORCH_CHECK(matching_metadata(yp) && matching_metadata(ym), "trial RHS input metadata mismatch");
            const int64_t k_lost=((d!=0)&((kp==k)|(km==k))).sum().item<int64_t>();
            const int64_t y_lost=((d!=0)&((yp==y0)|(ym==y0))).sum().item<int64_t>();
            TORCH_CHECK((kp!=km).any().item<bool>() && (yp!=ym).any().item<bool>(), "unrepresentable perturbation");
            auto fp=rhs(yp).detach().clone(); TORCH_CHECK(context_ok(), "context changed after positive RHS");
            auto fm=rhs(ym).detach().clone(); TORCH_CHECK(context_ok(), "context changed after negative RHS");
            auto gp=kp-fp, gm=km-fm, fd=(gp-gm)/(2*h);
            TORCH_CHECK(torch::isfinite(fd).all().item<bool>(), "nonfinite difference");
            auto gap=(ahat-fd)/scale, fd_scaled=fd/scale;
            const double abs=norm(gap), ref=norm(fd_scaled), masked=norm(mask_or_one*gap), masked_ref=norm(mask_or_one*fd_scaled);
            if(j) out << ',';
            out << "{\"epsilon\":" << h << ",\"gap_scaled_l2\":" << abs
                << ",\"gap_scaled_relative\":" << wrf_terminal_jvp::json_number(ref>0?abs/ref:std::numeric_limits<double>::quiet_NaN())
                << ",\"gap_over_base_G\":" << abs/residual_norm
                << ",\"gap_masked_scaled_l2\":" << masked
                << ",\"gap_masked_scaled_relative\":" << wrf_terminal_jvp::json_number(masked_ref>0?masked/masked_ref:std::numeric_limits<double>::quiet_NaN())
                << ",\"fd_adjacent_scaled_l2\":" << (previous.defined()?wrf_terminal_jvp::json_number(norm((fd-previous)/scale)):"null")
                << ",\"delta_G_plus_scaled_l2\":" << norm((gp-residual)/scale)
                << ",\"delta_G_minus_scaled_l2\":" << norm((gm-residual)/scale)
                << ",\"k_lost_components\":" << k_lost << ",\"state_lost_components\":" << y_lost
                << ",\"k_lost_scaled_norm\":" << norm(torch::where((kp==k)|(km==k),d/scale,torch::zeros_like(d)))
                << ",\"state_lost_scaled_norm\":" << norm(torch::where((yp==y0)|(ym==y0),a*d/scale,torch::zeros_like(d))) << '}';
            previous=fd;
        }
        out << "]}"; check_center();
    }
    out << "]}";
    return out.str();
}
inline bool write_exclusive(const std::string& path,const std::string& s) {
    int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd<0) return false;
    const auto data=s+"\n"; const bool ok=::write(fd,data.data(),data.size())==static_cast<ssize_t>(data.size());
    return ::close(fd)==0 && ok;
}
}
#endif
