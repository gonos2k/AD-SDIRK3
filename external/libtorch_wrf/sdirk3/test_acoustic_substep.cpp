// test_acoustic_substep.cpp — self-contained validation of the libtorch advance_w against the
// numpy von-Neumann stability result (test/em_b_wave/acoustic_amplification_match.py: |lambda|=0.998).
// Builds a 1-D vertical column (ny=nx=1, constant metrics, pure w-phi mode: t'=0, muave=0) and runs
// the REAL advance_w for many substeps from a ph' perturbation; PASS = energy stays bounded.
// Catches libtorch-implementation bugs in the core operator BEFORE integration.
//
// Build (from external/libtorch_wrf/sdirk3):
//   T=/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch
//   g++ -std=c++17 -O2 -I. -I$T/include -I$T/include/torch/csrc/api/include \
//       test_acoustic_substep.cpp wrf_sdirk3_acoustic_substep.cpp \
//       -L$T/lib -ltorch -ltorch_cpu -lc10 -Wl,-rpath,$T/lib -o test_acoustic_substep && ./test_acoustic_substep
#include "wrf_sdirk3_acoustic_substep.h"
#include <iostream>
#include <cmath>
using namespace wrf::sdirk3::acoustic;

// Build a small 2-D case with EVERY State/Const field populated (shared by the smoke + grad tests).
static void make_2d_case(State& s, Const& c) {
    const int ny = 4, nx = 4, nz = 10, nzw = nz + 1, nyv = ny + 1, nxu = nx + 1;
    auto o = torch::TensorOptions().dtype(torch::kFloat32);
    const float mut = 9.0e4f, dts = 5.0f;
    auto M3 = [&](int a, int b, int cc, float v) { return torch::full({a, b, cc}, v, o); };
    auto M2 = [&](int a, int b, float v) { return torch::full({a, b}, v, o); };
    s.u = M3(ny, nz, nxu, 0.1f); s.v = M3(nyv, nz, nx, 0.1f); s.w = M3(ny, nzw, nx, 0.0f);
    s.ph = M3(ny, nzw, nx, 0.0f); s.t = M3(ny, nz, nx, 0.0f); s.mu = M2(ny, nx, 0.0f);
    s.p = M3(ny, nz, nx, 0.0f); s.al = M3(ny, nz, nx, 0.0f); s.ww = M3(ny, nzw, nx, 0.0f);
    s.pm1 = M3(ny, nz, nx, 0.0f); s.t_2ave = M3(ny, nz, nx, 0.0f);
    s.muave = M2(ny, nx, 0.0f); s.muts = M2(ny, nx, mut); s.mudf = M2(ny, nx, 0.0f);
    c.c2a = M3(ny, nz, nx, 1.16e6f); c.alt = M3(ny, nz, nx, 2.5f); c.pb = M3(ny, nz, nx, 5.0e4f);
    c.a = M3(ny, nzw, nx, -0.1f); c.alpha = M3(ny, nzw, nx, 0.9f); c.gamma = M3(ny, nzw, nx, -0.1f);
    c.mut = M2(ny, nx, mut); c.muts = M2(ny, nx, mut); c.muu = M2(ny, nxu, mut); c.muv = M2(nyv, nx, mut);
    c.rdn = torch::full({nz}, 40.0f, o); c.rdnw = torch::full({nz}, 40.0f, o); c.dnw = torch::full({nz}, -0.025f, o);
    c.c1h = torch::ones({nz}, o); c.c2h = torch::zeros({nz}, o);
    c.c1f = torch::ones({nzw}, o); c.c2f = torch::zeros({nzw}, o);
    c.fnm = torch::full({nzw}, 0.5f, o); c.fnp = torch::full({nzw}, 0.5f, o);
    c.rdx = 1e-5f; c.rdy = 1e-5f; c.dts = dts; c.g = 9.81f; c.epssm = 0.1f; c.t0 = 300.0f;
    c.emdiv = 0.01f; c.smdiv = 0.1f;
    c.ru_tend = M3(ny, nz, nxu, 0.0f); c.rv_tend = M3(nyv, nz, nx, 0.0f); c.rw_tend = M3(ny, nzw, nx, 0.0f);
    c.ft = M3(ny, nz, nx, 0.0f); c.mu_tend = M2(ny, nx, 0.0f); c.ph_tend = M3(ny, nzw, nx, 0.0f);
    c.u_1 = M3(ny, nz, nxu, 0.05f); c.v_1 = M3(nyv, nz, nx, 0.05f);
    c.t_1 = M3(ny, nz, nx, 5.0f); c.ww_1 = M3(ny, nzw, nx, 0.0f);
}

// Full-substep SMOKE test: iterate the chained advance_substep MULTIPLE times (output feeds the next
// input) so an incompletely-populated output State — any field left undefined — crashes/NaNs on the
// following substep. Checks all 14 State fields defined+finite each pass. Catches integration bugs
// (undefined tensors, cross-operator threading, shapes); does NOT check correctness (needs dyn_em).
static bool full_substep_smoke() {
    State s; Const c; make_2d_case(s, c);
    auto all_finite = [](const State& st) {
        auto f = [](const torch::Tensor& t){ return t.defined() && t.isfinite().all().item<bool>(); };
        return f(st.u) && f(st.v) && f(st.w) && f(st.ph) && f(st.t) && f(st.mu) && f(st.p)
            && f(st.al) && f(st.ww) && f(st.pm1) && f(st.t_2ave) && f(st.muave) && f(st.muts) && f(st.mudf);
    };
    State st = s;
    bool fin = true;
    for (int n = 1; n <= 3 && fin; ++n) { st = advance_substep(st, c, n); fin = fin && all_finite(st); }
    std::cout << "# full advance_substep x3 (2-D, all fields threaded) runs + finite : " << (fin ? "PASS" : "FAIL") << "\n";
    return fin;
}

// DIFFERENTIABILITY test — the whole reason for the libtorch rebuild. grad-enabled != differentiable,
// so PROVE it with backward(), and verify the gradient is CORRECT against a central finite difference
// (skill: "verify the JVP/VJP once against FD"). Reverse-mode VJP d(loss)/d(ph) through the full
// advance_substep, contracted with a direction v, must match FD (loss(ph+ev)-loss(ph-ev))/2e.
static bool grad_check() {
    auto o = torch::TensorOptions().dtype(torch::kFloat32);
    State s0; Const c; make_2d_case(s0, c);
    auto v = torch::sin(torch::arange(s0.ph.numel(), o)).reshape(s0.ph.sizes());  // deterministic direction
    auto loss_of = [&](const torch::Tensor& phv, bool grad) -> torch::Tensor {
        State s = s0; s.ph = grad ? phv.clone().set_requires_grad(true) : phv;
        State out = advance_substep(s, c, 1);
        return out.w.sum() + out.ph.sum() + out.p.sum();     // scalar loss touching several outputs
    };
    // analytic reverse-mode VJP contracted with v
    State s = s0; s.ph = s0.ph.clone().set_requires_grad(true);
    State out = advance_substep(s, c, 1);
    (out.w.sum() + out.ph.sum() + out.p.sum()).backward();
    bool has_grad = s.ph.grad().defined();
    float analytic = has_grad ? (s.ph.grad() * v).sum().item<float>() : 0.0f;
    // central finite difference of the same directional derivative
    float e = 1e-1f, fd;
    { torch::NoGradGuard ng;
      float lp = (loss_of(s0.ph + e * v, false)).item<float>();
      float lm = (loss_of(s0.ph - e * v, false)).item<float>();
      fd = (lp - lm) / (2.0f * e); }
    float rel = std::abs(analytic - fd) / (std::abs(fd) + 1e-6f);
    bool ok = has_grad && std::isfinite(analytic) && std::isfinite(fd)
              && std::abs(fd) > 1e-3f            // non-trivial gradient (graph really flows through ph)
              && rel < 1e-2f;                    // VJP matches FD
    std::cout << "# advance_substep differentiable: VJP=" << analytic << " FD=" << fd
              << " rel=" << rel << " grad=" << (has_grad ? "yes" : "NO") << " : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// small_step_prep ENTRY test: the coupling (module_small_step_em.F:238-279) must produce a State that
// (a) is finite with all fields defined, (b) has perturbations ~0 when u_1==u_2 & muus==muu (the rk_step-1
// property — the acoustic loop starts from ~zero perturbations), and (c) is consumable by advance_substep.
static bool prep_chain_check() {
    const int ny = 4, nx = 4, nz = 10, nzw = nz + 1, nyv = ny + 1, nxu = nx + 1;
    auto o = torch::TensorOptions().dtype(torch::kFloat32);
    const float mut = 9.0e4f;
    auto M3 = [&](int a, int b, int cc, float v) { return torch::full({a, b, cc}, v, o); };
    auto M2 = [&](int a, int b, float v) { return torch::full({a, b}, v, o); };
    PrepInput in;
    in.u_1 = M3(ny, nz, nxu, 0.1f); in.u_2 = M3(ny, nz, nxu, 0.1f);   // u_1==u_2 => u' ~ 0
    in.v_1 = M3(nyv, nz, nx, 0.1f); in.v_2 = M3(nyv, nz, nx, 0.1f);
    in.w_1 = M3(ny, nzw, nx, 0.0f); in.w_2 = M3(ny, nzw, nx, 0.0f);
    in.t_1 = M3(ny, nz, nx, 300.0f); in.t_2 = M3(ny, nz, nx, 300.0f);
    in.ph_1 = M3(ny, nzw, nx, 0.0f); in.ph_2 = M3(ny, nzw, nx, 0.0f);
    in.ww = M3(ny, nzw, nx, 0.0f); in.mu_1 = M2(ny, nx, 0.0f); in.mu_2 = M2(ny, nx, 0.0f);
    in.muus = M2(ny, nxu, mut); in.muu = M2(ny, nxu, mut);
    in.muvs = M2(nyv, nx, mut); in.muv = M2(nyv, nx, mut);
    in.muts = M2(ny, nx, mut); in.mut = M2(ny, nx, mut);
    in.c1h = torch::ones({nz}, o); in.c2h = torch::zeros({nz}, o);
    in.c1f = torch::ones({nzw}, o); in.c2f = torch::zeros({nzw}, o);
    in.msfuy = M2(ny, nxu, 1.0f); in.msfvx_inv = M2(nyv, nx, 1.0f); in.msfty = M2(ny, nx, 1.0f);
    Saves saves;
    State prepped = small_step_prep(in, saves);
    auto f = [](const torch::Tensor& t) { return t.defined() && t.isfinite().all().item<bool>(); };
    bool prep_ok = f(prepped.u) && f(prepped.v) && f(prepped.w) && f(prepped.ph) && f(prepped.t)
                && f(prepped.mu) && f(prepped.p) && f(prepped.al) && f(prepped.ww) && f(prepped.pm1)
                && f(prepped.t_2ave) && f(prepped.muave) && f(prepped.muts) && f(prepped.mudf);
    bool pert_zero = prepped.u.abs().max().item<float>() < 1e-3f
                  && prepped.ph.abs().max().item<float>() < 1e-3f;
    State dummy; Const c; make_2d_case(dummy, c);         // Const (coeffs/tendencies) for the loop body
    State out = advance_substep(prepped, c, 1);
    bool consume_ok = f(out.w) && f(out.ph) && f(out.u);
    bool ok = prep_ok && pert_zero && consume_ok;
    std::cout << "# small_step_prep -> advance_substep (finite + pert~0 @ u_1==u_2 + consumable) : "
              << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// prep<->finish INVERTIBILITY: finish(prep(x), no substeps) must recover the reference fields
// (u_1/v_1/w_1/ph_1), even with u_1!=u_2 and muus!=muu (non-trivial coupling). Proves the entry and
// exit couplings are true inverses — a wiring correctness guard independent of the loop dynamics.
static bool roundtrip_check() {
    const int ny = 4, nx = 4, nz = 10, nzw = nz + 1, nyv = ny + 1, nxu = nx + 1;
    auto o = torch::TensorOptions().dtype(torch::kFloat32);
    auto M3 = [&](int a, int b, int cc, float v) { return torch::full({a, b, cc}, v, o); };
    auto M2 = [&](int a, int b, float v) { return torch::full({a, b}, v, o); };
    PrepInput in;
    in.u_1 = M3(ny, nz, nxu, 0.2f); in.u_2 = M3(ny, nz, nxu, 0.15f);   // u_1 != u_2
    in.v_1 = M3(nyv, nz, nx, 0.2f); in.v_2 = M3(nyv, nz, nx, 0.15f);
    in.w_1 = M3(ny, nzw, nx, 0.1f); in.w_2 = M3(ny, nzw, nx, 0.05f);
    in.t_1 = M3(ny, nz, nx, 300.0f); in.t_2 = M3(ny, nz, nx, 295.0f);
    in.ph_1 = M3(ny, nzw, nx, 50.0f); in.ph_2 = M3(ny, nzw, nx, 30.0f);
    in.ww = M3(ny, nzw, nx, 0.0f); in.mu_1 = M2(ny, nx, 100.0f); in.mu_2 = M2(ny, nx, 60.0f);  // mu_1 != mu_2
    in.muus = M2(ny, nxu, 9.1e4f); in.muu = M2(ny, nxu, 9.0e4f);       // muus != muu
    in.muvs = M2(nyv, nx, 9.1e4f); in.muv = M2(nyv, nx, 9.0e4f);
    in.muts = M2(ny, nx, 9.1e4f); in.mut = M2(ny, nx, 9.0e4f);
    in.c1h = torch::ones({nz}, o); in.c2h = torch::zeros({nz}, o);
    in.c1f = torch::ones({nzw}, o); in.c2f = torch::zeros({nzw}, o);
    in.msfuy = M2(ny, nxu, 1.0f); in.msfvx_inv = M2(nyv, nx, 1.0f); in.msfty = M2(ny, nx, 1.0f);
    Saves saves;
    State prepped = small_step_prep(in, saves);
    FinishOutput fin = small_step_finish(prepped, saves, in, /*rk_step*/1, /*rk_order*/3,
                                         /*n_small*/4, /*dts*/5.0f, torch::zeros_like(in.t_2));
    auto rel = [](const torch::Tensor& a, const torch::Tensor& b) {
        return (a - b).abs().max().item<float>() / (b.abs().max().item<float>() + 1e-6f); };
    float ru = rel(fin.u, in.u_1), rv = rel(fin.v, in.v_1), rw = rel(fin.w, in.w_1),
          rp = rel(fin.ph, in.ph_1), rm = rel(fin.mu, in.mu_1);   // mu now coupled -> must recover mu_1
    bool ok = ru < 1e-4f && rv < 1e-4f && rw < 1e-4f && rp < 1e-4f && rm < 1e-4f;
    std::cout << "# prep<->finish invertible: finish(prep(x))==ref  rel(u,v,w,ph,mu)=(" << ru << "," << rv
              << "," << rw << "," << rp << "," << rm << ") : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// mass_to_u/vpoint staggered 0.5-averages (periodic), vs a hand-computed example.
static bool mass_avg_check() {
    auto o = torch::TensorOptions().dtype(torch::kFloat32);
    auto mu = torch::tensor({{1.0f, 2.0f, 3.0f, 4.0f}}, o);              // {1,4}
    auto um = mass_to_upoint(mu);                                        // {1,5}: [edge,1.5,2.5,3.5,edge]
    auto um_exp = torch::tensor({{2.5f, 1.5f, 2.5f, 3.5f, 2.5f}}, o);    // edge=0.5*(4+1)=2.5 periodic
    auto muv = torch::tensor({{1.0f}, {2.0f}, {3.0f}}, o);              // {3,1}
    auto vm = mass_to_vpoint(muv);                                       // {4,1}: [2,1.5,2.5,2]
    auto vm_exp = torch::tensor({{2.0f}, {1.5f}, {2.5f}, {2.0f}}, o);
    float eu = (um - um_exp).abs().max().item<float>();
    float ev = (vm - vm_exp).abs().max().item<float>();
    bool shapes = um.size(1) == 5 && vm.size(0) == 4;
    bool ok = shapes && eu < 1e-6f && ev < 1e-6f;
    std::cout << "# mass_to_u/vpoint staggered averages: err(u,v)=(" << eu << "," << ev
              << ") shapes=" << (shapes ? "ok" : "BAD") << " : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

int main() {
    bool smoke_ok = full_substep_smoke();
    bool grad_ok  = grad_check();
    bool prep_ok  = prep_chain_check();
    bool rt_ok    = roundtrip_check();
    bool mass_ok  = mass_avg_check();
    const int nz = 40, nzw = nz + 1, ny = 1, nx = 1;
    const float g = 9.81f, eps = 0.1f, deta = 1.0f / nz, mut = 9.0e4f, c2a0 = 1.16e6f;
    const float dts = 1.0f;   // match the von-Neumann reference (resolved limit); |lambda| is dts-normalized
    auto opt = torch::TensorOptions().dtype(torch::kFloat32);

    // --- constant metrics ---
    auto rdnw = torch::full({nz}, 1.0f / deta, opt), rdn = torch::full({nzw}, 1.0f / deta, opt);
    auto c1h = torch::ones({nz}, opt),  c2h = torch::zeros({nz}, opt);
    auto c1f = torch::ones({nzw}, opt), c2f = torch::zeros({nzw}, opt);
    auto c2a = torch::full({ny, nz, nx}, c2a0, opt);

    // --- calc_coef_w a/alpha/gamma: bands INDEXED BY FULL LEVEL kf (0..kde), consistent with
    // advance_w's Thomas. Sigma constants => mh=mf=mut; band = cof*c2a/(deta^2*mut^2). ---
    float cof = std::pow(0.5f * dts * g * (1.0f + eps), 2.0f);
    int kde = nzw - 1;
    float band = cof * (1.0f/deta) * (1.0f/deta) * c2a0 / (mut * mut);
    auto a = torch::zeros({ny, nzw, nx}, opt), alpha = torch::zeros({ny, nzw, nx}, opt), gamma = torch::zeros({ny, nzw, nx}, opt);
    std::vector<float> av(nzw, 0), bv(nzw, 1), cv(nzw, 0), alv(nzw, 0), gav(nzw, 0);
    for (int kf = 1; kf <= kde - 1; ++kf) {           // interior rows
        bv[kf] = 1.0f + 2.0f * band;
        cv[kf] = -band;
        av[kf] = (kf >= 2 ? -band : 0.0f);            // a[1]=0 (bottom BC w[0]=0; a[1]*w[0]=0 anyway)
    }
    bv[kde] = 1.0f + 2.0f * band;                     // top row (WRF 2* factor folds into band scaling)
    av[kde] = -2.0f * band;
    for (int kf = 1; kf <= kde; ++kf) {               // Thomas factorization at index kf
        alv[kf] = 1.0f / (bv[kf] - av[kf] * gav[kf - 1]);
        gav[kf] = cv[kf] * alv[kf];
    }
    for (int kf = 0; kf < nzw; ++kf) { a[0][kf][0] = av[kf]; alpha[0][kf][0] = alv[kf]; gamma[0][kf][0] = gav[kf]; }

    Const c;
    c.c2a = c2a; c.alt = torch::full({ny, nz, nx}, 2.5f, opt);
    c.a = a; c.alpha = alpha; c.gamma = gamma;
    c.mut = torch::full({ny, nx}, mut, opt);
    c.c1h = c1h; c.c2h = c2h; c.c1f = c1f; c.c2f = c2f; c.rdn = rdn; c.rdnw = rdnw;
    c.dts = dts; c.g = g; c.epssm = eps; c.t0 = 300.0f;
    c.rw_tend = torch::zeros({ny, nzw, nx}, opt);
    c.ph_tend = torch::zeros({ny, nzw, nx}, opt);   // advance_w rhs now reads ph_tend
    c.t_1 = torch::zeros({ny, nz, nx}, opt);

    // --- initial state: ph' = sin(pi*eta), everything else 0 (pure w-phi mode) ---
    State s;
    auto eta = torch::linspace(0, 1, nzw, opt).view({1, nzw, 1});
    s.ph = 100.0f * torch::sin(M_PI * eta);
    s.ph.index_put_({0, 0, 0}, 0.0f); s.ph.index_put_({0, kde, 0}, 0.0f);
    s.w  = torch::zeros({ny, nzw, nx}, opt);
    s.t  = torch::zeros({ny, nz, nx}, opt);
    s.p  = torch::zeros({ny, nz, nx}, opt);   // advance_w reads s.p.size(1) for nz
    s.mu = torch::zeros({ny, nx}, opt);
    s.muave = torch::zeros({ny, nx}, opt);
    s.muts  = torch::full({ny, nx}, mut, opt);
    s.t_2ave = torch::zeros({ny, nz, nx}, opt);

    // --- ISOLATION CHECK: do the hand-derived a/alpha/gamma factorize A? (M^-1 @ A @ v == v) ---
    float mia_relerr = 1.0f;   // captured for the final PASS gate (not just printed)
    {
        std::vector<float> v(nzw, 0), Av(nzw, 0), w(nzw, 0);
        for (int kf = 1; kf <= kde; ++kf) v[kf] = static_cast<float>(kf);   // ramp, v[0]=0
        for (int kf = 1; kf <= kde; ++kf) {
            Av[kf] = bv[kf] * v[kf] + av[kf] * v[kf - 1];
            if (kf <= kde - 1) Av[kf] += cv[kf] * v[kf + 1];
        }
        for (int kf = 0; kf < nzw; ++kf) w[kf] = Av[kf];
        for (int kf = 1; kf <= kde; ++kf) w[kf] = (w[kf] - av[kf] * w[kf - 1]) * alv[kf];
        for (int kf = kde - 1; kf >= 1; --kf) w[kf] = w[kf] - gav[kf] * w[kf + 1];
        float err = 0, vn = 0;
        for (int kf = 1; kf <= kde; ++kf) { err += std::pow(w[kf] - v[kf], 2); vn += v[kf] * v[kf]; }
        mia_relerr = std::sqrt(err / vn);
        std::cout << "# a/alpha/gamma factorize A?  M^-1@A@v==v rel_err=" << mia_relerr << "\n";
    }

    // --- CORRECT metric: von-Neumann amplification |lambda| of ONE advance_w substep (energy
    // norm is wrong here — the operator is non-normal + ph/w scale-mismatched). Build M (state
    // [w;ph], 2*nzw) by applying advance_w to unit basis vectors, then max|eig|. Matches the
    // numpy acoustic_amplification_match.py (0.998). ---
    auto zeroState = [&]() {
        State z = s;
        z.w = torch::zeros({ny, nzw, nx}, opt); z.ph = torch::zeros({ny, nzw, nx}, opt);
        return z;
    };
    int n2 = 2 * nzw;
    auto M = torch::zeros({n2, n2}, opt);
    for (int j = 0; j < nzw; ++j) {
        for (int which = 0; which < 2; ++which) {           // 0: unit w[j]; 1: unit ph[j]
            State z = zeroState();
            if (which == 0) z.w.index_put_({0, j, 0}, 1.0f); else z.ph.index_put_({0, j, 0}, 1.0f);
            State o = advance_w(z, c);
            int col = which == 0 ? j : nzw + j;
            for (int k = 0; k < nzw; ++k) { M[k][col] = o.w[0][k][0]; M[nzw + k][col] = o.ph[0][k][0]; }
        }
    }
    auto eig = torch::linalg_eigvals(M);      // complex eigenvalues of the (non-symmetric) amp matrix
    float lam = torch::abs(eig).max().item<float>();
    const float lam_ref = 0.998315f;   // numpy acoustic_amplification_match.py reference
    std::cout << "# libtorch advance_w amplification |lambda|_max=" << lam
              << "  (numpy von-Neumann ref: " << lam_ref << ")\n";
    // PASS gate asserts BOTH: (1) coefficients factorize A, and (2) |lambda| MATCHES the reference
    // (not merely <=1 — a stable-but-wrong scheme with |lambda|=0.5 must NOT pass).
    bool coeffs_ok = mia_relerr < 1e-4f;
    bool lambda_ok = std::isfinite(lam) && std::abs(lam - lam_ref) < 1e-3f;
    bool ok = smoke_ok && grad_ok && prep_ok && rt_ok && mass_ok && coeffs_ok && lambda_ok;
    std::cout << "# advance_w matches numpy scheme  coeffs=" << (coeffs_ok ? "ok" : "BAD")
              << "  |lambda|-match=" << (lambda_ok ? "ok" : "BAD")
              << "  : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}
