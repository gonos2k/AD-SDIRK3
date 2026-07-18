// PR 9B commit 3 / PR 9C commit 3: standing forward-mode tangent contract for
// the PRODUCTION W-damping chain — compute_w_damping_term
// (wrf_sdirk3_w_damping.h), the exact function tile_unified_impl Step 6
// calls. NOT a transcription: a dual-severing edit to the production helper
// fails this test.
//
// PR 9C: the chain is the WRF-parity form (mass-decoupled CFL from ww, hard
// gate, hard oracle sign). Hard gate and hard sign are piecewise
// differentiable, so the fixture keeps every point WELL AWAY from the two
// kinks (vert_cfl == gate and w == 0): all points active, |w| bounded away
// from zero. At the kinks only primal parity is contracted (the reference
// contract test); FD equality is never demanded there.
#include <torch/torch.h>
#include <cmath>
#include <cstdio>
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include "wrf_sdirk3_w_damping.h"

namespace {

struct Fixture {
    int ny = 6, nzw = 8, nx = 7;
    float alpha = 0.3f, gate = 1.0f, offset = 1.0f, dt = 600.0f;
    int ks = 1, ke = 7, n = 6;
    torch::Tensor ww, w, mu, wwdot, wdot, mudot, c1f, c2f, rdnw_int;
    Fixture() {
        auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        auto i3 = torch::arange(ny * nzw * nx, opt).reshape({ny, nzw, nx});
        auto i2 = torch::arange(ny * nx, opt).reshape({ny, nx});
        mu = 95000.0f + 500.0f * torch::sin(i2 * 1.3f);
        // all-active, kink-free fixture: vert_cfl in ~[2.5, 3.5] >> gate=1,
        // |w| in [0.4, 0.6] bounded away from 0 (sign fixed positive).
        // vert_cfl = |ww/mass * rdnw * dt|; choose ww = cfl*mass/(rdnw*dt).
        auto mass2d = 0.9f * mu + 100.0f;  // c1f~0.9 at k... approximate seed
        auto cfl = 3.0f + 0.5f * torch::sin(i3 * 0.9f);
        rdnw_int = (60.0f + 2.0f * torch::arange(n, opt)).reshape({1, n, 1});
        // build ww on the full grid using a mid rdnw scale; exact CFL value
        // does not matter, only "well above gate" does.
        ww = cfl * mass2d.unsqueeze(1) / (64.0f * dt);
        w = 0.5f + 0.1f * torch::sin(i3 * 1.7f);
        wwdot = torch::sin(i3 * 3.1f + 0.4f);
        wdot = torch::sin(i3 * 2.9f + 1.1f);
        mudot = torch::sin(i2 * 2.3f + 0.9f);
        auto kf = torch::arange(nzw, opt);
        c1f = 1.0f - 0.04f * kf;
        c2f = 50.0f * kf;
    }
    torch::Tensor chain(const torch::Tensor& ww_in, const torch::Tensor& w_in,
                        const torch::Tensor& mu_in) const {
        return wrf::sdirk3::compute_w_damping_term(
            ww_in, w_in, mu_in, c1f, c2f, rdnw_int, dt, alpha, gate, offset,
            ks, ke, nzw);
    }
    // Analytic tangent on the all-active, fixed-sign fixture:
    //   mass = c1f*mu + c2f;  vel = ww/mass;  cfl = |vel * r * dt|
    //   term = s_w * alpha * (cfl - offset) * mass
    //   d(term) = s_w*alpha*[ d(cfl)*mass + (cfl-offset)*c1f*mudot ]
    //   d(cfl) = sign(vel)*r*dt*d(vel)
    //   d(vel) = (wwdot*mass - ww*c1f*mudot) / mass^2
    // (w enters only through the sign, whose derivative is 0 away from 0.)
    torch::Tensor analytic_tangent() const {
        auto ww_i = ww.slice(1, ks, ke);
        auto w_i = w.slice(1, ks, ke);
        auto wwd_i = wwdot.slice(1, ks, ke);
        auto c1f_i = c1f.slice(0, ks, ke).reshape({1, n, 1});
        auto c2f_i = c2f.slice(0, ks, ke).reshape({1, n, 1});
        auto mass = c1f_i * mu.unsqueeze(1) + c2f_i;
        auto dmass = c1f_i * mudot.unsqueeze(1);
        auto vel = ww_i / mass;
        auto cfl = torch::abs(vel * rdnw_int * dt);
        auto dvel = (wwd_i * mass - ww_i * dmass) / (mass * mass);
        auto dcfl = torch::sign(vel) * rdnw_int * dt * dvel;
        auto s_w = torch::where(torch::signbit(w_i),
                                torch::full_like(w_i, -1.0f),
                                torch::full_like(w_i, 1.0f));
        auto t = s_w * alpha * (dcfl * mass + (cfl - offset) * dmass);
        return torch::constant_pad_nd(t, {0, 0, ks, nzw - ke});
    }
    torch::Tensor fwad_tangent(bool sever_ww) const {
        torch::NoGradGuard no_grad;
        wrf::sdirk3::jvp_detail::DualLevelGuard g;
        auto ww_d = sever_ww
                        ? ww  // plain primal: the ww dual path is severed
                        : torch::_make_dual(ww, wwdot, (int64_t)g.level());
        auto w_d = torch::_make_dual(w, wdot, (int64_t)g.level());
        auto mu_d = torch::_make_dual(mu, mudot, (int64_t)g.level());
        auto out = chain(ww_d, w_d, mu_d);
        auto tgt = std::get<1>(torch::_unpack_dual(out, (int64_t)g.level()));
        return tgt.defined() ? tgt : torch::zeros_like(out);
    }
};

float rel_err(const torch::Tensor& a, const torch::Tensor& b) {
    float an = a.norm().item<float>();
    float bn = b.norm().item<float>();
    return (a - b).norm().item<float>() / std::max({an, bn, 1e-30f});
}

}  // namespace

int main() {
    torch::NoGradGuard no_grad;
    Fixture fx;
    int failures = 0;
    const float tol = 1e-2f;  // standing JVP contract threshold

    // sanity: fixture is fully active and kink-free
    {
        auto out = fx.chain(fx.ww, fx.w, fx.mu);
        auto interior = out.slice(1, fx.ks, fx.ke);
        if (!(interior.abs().min().item<float>() > 0.0f)) {
            std::printf("FAIL: fixture not fully active\n");
            ++failures;
        }
    }

    auto truth = fx.analytic_tangent();

    auto faithful = fx.fwad_tangent(false);
    const float re_faithful = rel_err(faithful, truth);
    std::printf("wdamp_tangent faithful fwAD vs analytic: rel=%.3e (tol %.0e)\n",
                re_faithful, tol);
    if (!(re_faithful <= tol)) {
        std::printf("FAIL: faithful chain tangent deviates from analytic\n");
        ++failures;
    }

    {
        const float eps = 1e-4f;
        auto fd = (fx.chain(fx.ww + eps * fx.wwdot, fx.w + eps * fx.wdot,
                            fx.mu + eps * fx.mudot) -
                   fx.chain(fx.ww - eps * fx.wwdot, fx.w - eps * fx.wdot,
                            fx.mu - eps * fx.mudot)) /
                  (2.0f * eps);
        const float re_fd = rel_err(fd, truth);
        std::printf("wdamp_tangent central-FD vs analytic:    rel=%.3e (tol 1e-1)\n",
                    re_fd);
        if (!(re_fd <= 1e-1f)) {
            std::printf("FAIL: analytic reference disagrees with FD (smooth regime)\n");
            ++failures;
        }
    }

    auto severed = fx.fwad_tangent(true);
    const float re_severed = rel_err(severed, truth);
    std::printf("wdamp_tangent severed fwAD vs analytic:  rel=%.3e (must be > %.0e)\n",
                re_severed, tol);
    if (!(re_severed > tol)) {
        std::printf("FAIL: contract cannot detect a severed ww dual — hollow guard\n");
        ++failures;
    }

    // PR 9D load-bearing case A: a PURE-w perturbation (ww and mu severed)
    // produces an EXACTLY zero production tangent in the smooth region — w
    // enters only through the hard SIGN, whose derivative is 0 away from
    // w == 0. This is the proof behind "physical W direct diagonal = 0" in
    // the preconditioner policy: there is no direct W Jacobian to mirror.
    {
        torch::NoGradGuard ng;
        wrf::sdirk3::jvp_detail::DualLevelGuard g;
        auto w_d = torch::_make_dual(fx.w, fx.wdot, (int64_t)g.level());
        auto out = fx.chain(fx.ww, w_d, fx.mu);  // ww, mu severed (plain primal)
        auto tgt = std::get<1>(torch::_unpack_dual(out, (int64_t)g.level()));
        float pure_w = tgt.defined() ? tgt.abs().max().item<float>() : 0.0f;
        std::printf("wdamp_tangent pure-w (ww,mu severed): max|tangent|=%.3e (must be 0)\n",
                    pure_w);
        if (pure_w != 0.0f) {
            std::printf("FAIL: pure-w perturbation moved the term — a direct W "
                        "diagonal WOULD be justified\n");
            ++failures;
        }
    }

    // PR 9D load-bearing case B (positive control): a ww/mu perturbation (w
    // severed) DOES move the term — the real smooth Jacobian is the ww/mu
    // path, which a 1-D W diagonal cannot represent.
    {
        torch::NoGradGuard ng;
        wrf::sdirk3::jvp_detail::DualLevelGuard g;
        auto ww_d = torch::_make_dual(fx.ww, fx.wwdot, (int64_t)g.level());
        auto mu_d = torch::_make_dual(fx.mu, fx.mudot, (int64_t)g.level());
        auto out = fx.chain(ww_d, fx.w, mu_d);  // w severed (plain primal)
        auto tgt = std::get<1>(torch::_unpack_dual(out, (int64_t)g.level()));
        float ww_mu = tgt.defined() ? tgt.abs().max().item<float>() : 0.0f;
        std::printf("wdamp_tangent ww/mu (w severed): max|tangent|=%.3e (must be > 0)\n",
                    ww_mu);
        if (!(ww_mu > 0.0f)) {
            std::printf("FAIL: ww/mu perturbation produced no tangent — chain broken\n");
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("W-damping tangent contract: ALL PASS (6/6 cases)\n");
        return 0;
    }
    std::printf("W-damping tangent contract: %d FAILURE(S)\n", failures);
    return 1;
}
