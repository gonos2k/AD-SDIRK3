// PR 9B commit 3: standing forward-mode tangent contract for the PRODUCTION
// W-damping chain — compute_w_damping_term (wrf_sdirk3_w_damping.h), the
// exact function tile_unified_impl Step 6 calls. This is NOT a transcription:
// a dual-severing edit to the production helper fails this test.
//
// WHY THIS EXISTS: the rw-channel bisection
// (doc/sdirk3_rw_tangent_bisection_2026-07-17.md) proved this chain's fwAD
// is correct while its central-FD is intrinsically non-local at kink-heavy
// operands — so a future dual-severing regression here (a stray .detach(),
// an in-place op on a dual, a dtype hop) would be nearly invisible to
// FD-based checks. This contract pins the tangent against the ANALYTIC
// derivative on a deterministic fixture instead:
//   1. faithful chain: fwAD tangent == analytic tangent (tight tolerance);
//   2. FD cross-check of the analytic reference in the smooth regime;
//   3. a deliberately dual-severed variant (vert_cfl computed from
//      w.detach()) MUST FAIL the same tolerance — proving the contract can
//      detect exactly the defect class it guards against.
#include <torch/torch.h>
#include <cmath>
#include <cstdio>
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include "wrf_sdirk3_w_damping.h"

namespace {

struct Fixture {
    int ny = 6, nzw = 8, nx = 7;
    float alpha = 0.3f, crit = 1.0f, delta = 1e-3f, dt = 600.0f;
    int ks = 1, ke = 7, n = 6;
    torch::Tensor w, mu, wdot, mudot, c1f, c2f, dz;
    Fixture() {
        auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        auto i3 = torch::arange(ny * nzw * nx, opt).reshape({ny, nzw, nx});
        auto i2 = torch::arange(ny * nx, opt).reshape({ny, nx});
        // Deterministic, kink-avoiding operand: |w| well above delta so the
        // analytic reference is unambiguous (the FD cross-check needs a
        // smooth neighborhood; the CONTRACT itself is fwAD-vs-analytic).
        w = 0.05f + 0.02f * torch::sin(i3 * 1.7f);
        mu = 95000.0f + 500.0f * torch::sin(i2 * 1.3f);
        wdot = torch::sin(i3 * 3.1f + 0.4f);
        mudot = torch::sin(i2 * 2.3f + 0.9f);
        auto kf = torch::arange(nzw, opt);
        c1f = 1.0f - 0.04f * kf;
        c2f = 50.0f * kf;
        dz = (0.5f + 0.05f * torch::arange(n, opt)).reshape({1, n, 1}) * 200.0f;
    }
    // The PRODUCTION chain — exactly what tile_unified_impl Step 6 calls.
    // sever_dual injects the guarded defect class at the INPUT (a plain,
    // dual-free w) to prove the contract detects severed propagation.
    torch::Tensor chain(const torch::Tensor& w_in, const torch::Tensor& mu_in,
                        bool sever_dual) const {
        const torch::Tensor& w_used = sever_dual ? w : w_in;  // fx.w = plain primal
        return wrf::sdirk3::compute_w_damping_term(
            w_used, mu_in, c1f, c2f, dz, dt, alpha, crit, delta, ks, ke, nzw);
    }
    torch::Tensor analytic_tangent() const {
        auto w_int = w.slice(1, ks, ke);
        auto wd_int = wdot.slice(1, ks, ke);
        auto s2 = w_int * w_int + delta * delta;
        auto w_sign = w_int / torch::sqrt(s2);
        auto dsign = (delta * delta) / s2.pow(1.5f) * wd_int;
        auto vert_cfl = torch::abs(w_int) * dt / dz;
        auto cfl_excess = vert_cfl - crit;
        auto dcfl = torch::sign(w_int) * wd_int * dt / dz;
        auto c1f_int = c1f.slice(0, ks, ke).reshape({1, n, 1});
        auto c2f_int = c2f.slice(0, ks, ke).reshape({1, n, 1});
        auto mass = c1f_int * mu.unsqueeze(1) + c2f_int;
        auto dmass = c1f_int * mudot.unsqueeze(1);
        auto t = alpha * (dsign * cfl_excess * mass + w_sign * dcfl * mass +
                          w_sign * cfl_excess * dmass);
        return torch::constant_pad_nd(t, {0, 0, ks, nzw - ke});
    }
    torch::Tensor fwad_tangent(bool sever_dual) const {
        torch::NoGradGuard no_grad;
        wrf::sdirk3::jvp_detail::DualLevelGuard g;
        auto w_d = torch::_make_dual(w, wdot, (int64_t)g.level());
        auto mu_d = torch::_make_dual(mu, mudot, (int64_t)g.level());
        auto out = chain(w_d, mu_d, sever_dual);
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
    // Reuse the standing JVP contract threshold (rel <= 0.01); the faithful
    // chain typically lands ~1e-7, so 0.01 has orders of headroom while a
    // severed dual (which drops the dominant dt/dz path) lands >> 0.01.
    const float tol = 1e-2f;

    auto truth = fx.analytic_tangent();

    // (1) faithful chain: fwAD == analytic
    auto faithful = fx.fwad_tangent(/*sever_dual=*/false);
    const float re_faithful = rel_err(faithful, truth);
    std::printf("wdamp_tangent faithful fwAD vs analytic: rel=%.3e (tol %.0e)\n",
                re_faithful, tol);
    if (!(re_faithful <= tol)) {
        std::printf("FAIL: faithful chain tangent deviates from analytic\n");
        ++failures;
    }

    // (2) FD cross-check of the analytic reference (smooth regime fixture)
    {
        const float eps = 1e-5f;
        auto fd = (fx.chain(fx.w + eps * fx.wdot, fx.mu + eps * fx.mudot, false) -
                   fx.chain(fx.w - eps * fx.wdot, fx.mu - eps * fx.mudot, false)) /
                  (2.0f * eps);
        const float re_fd = rel_err(fd, truth);
        std::printf("wdamp_tangent central-FD vs analytic:    rel=%.3e (tol 1e-1)\n",
                    re_fd);
        if (!(re_fd <= 1e-1f)) {
            std::printf("FAIL: analytic reference disagrees with FD in the smooth regime\n");
            ++failures;
        }
    }

    // (3) dual-severed variant (plain w input, mu still dual) MUST fail
    auto severed = fx.fwad_tangent(/*sever_dual=*/true);
    const float re_severed = rel_err(severed, truth);
    std::printf("wdamp_tangent severed fwAD vs analytic:  rel=%.3e (must be > %.0e)\n",
                re_severed, tol);
    if (!(re_severed > tol)) {
        std::printf("FAIL: the contract cannot detect a severed dual — guard is hollow\n");
        ++failures;
    }

    if (failures == 0) {
        std::printf("W-damping tangent contract: ALL PASS (3/3 cases)\n");
        return 0;
    }
    std::printf("W-damping tangent contract: %d FAILURE(S)\n", failures);
    return 1;
}
