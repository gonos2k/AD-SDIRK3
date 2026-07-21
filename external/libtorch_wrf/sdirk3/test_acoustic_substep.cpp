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
    // save + base geopotential (this session's non-hydro 4th PGF / ww*d(phi) advection wiring):
    // ph_1 = zero perturbation save; phb = monotone base profile so vertical d(phb) is nonzero.
    c.ph_1 = M3(ny, nzw, nx, 0.0f);
    c.phb  = (torch::arange(nzw, o) * 100.0f).reshape({1, nzw, 1}).expand({ny, nzw, nx}).clone();
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

// RK3 acoustic schedule: dts*n_sub must equal the stage fraction of dt (dt/3, dt/2, dt).
static bool schedule_check() {
    const float dt = 12.0f; const int N = 4;
    auto s1 = acoustic_schedule(1, dt, N), s2 = acoustic_schedule(2, dt, N), s3 = acoustic_schedule(3, dt, N);
    bool ok = s1.n_sub == 1 && std::abs(s1.dts * s1.n_sub - dt / 3.0f) < 1e-4f
           && s2.n_sub == 2 && std::abs(s2.dts * s2.n_sub - dt / 2.0f) < 1e-4f
           && s3.n_sub == 4 && std::abs(s3.dts * s3.n_sub - dt)        < 1e-4f;
    std::cout << "# acoustic_schedule dts*N == stage-fraction (dt/3,dt/2,dt) : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// mass_to_u/vpoint staggered 0.5-averages (periodic), vs a hand-computed example.
static bool mass_avg_check() {
    auto o = torch::TensorOptions().dtype(torch::kFloat32);
    auto mu = torch::tensor({{1.0f, 2.0f, 3.0f, 4.0f}}, o);              // {1,4}
    auto um = mass_to_upoint(mu);                                        // {1,5}: [edge,1.5,2.5,3.5,edge]
    auto um_exp = torch::tensor({{2.5f, 1.5f, 2.5f, 3.5f, 2.5f}}, o);    // edge=0.5*(4+1)=2.5 periodic
    auto muv = torch::tensor({{1.0f}, {2.0f}, {3.0f}}, o);              // {3,1}
    auto vm = mass_to_vpoint(muv);                                       // {4,1}: y-SYMMETRIC walls
    auto vm_exp = torch::tensor({{1.0f}, {1.5f}, {2.5f}, {3.0f}}, o);    // edges reflect: [mu0, 1.5, 2.5, mu2]
    float eu = (um - um_exp).abs().max().item<float>();
    float ev = (vm - vm_exp).abs().max().item<float>();
    bool shapes = um.size(1) == 5 && vm.size(0) == 4;
    bool ok = shapes && eu < 1e-6f && ev < 1e-6f;
    std::cout << "# mass_to_u/vpoint staggered averages: err(u,v)=(" << eu << "," << ev
              << ") shapes=" << (shapes ? "ok" : "BAD") << " : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// ============== AD TRIPLE (JVP / VJP / HVP) through the NEW stage operators ==============
// The rebuild exists to give the dynamics all three AD primitives on the dynamic graph:
// JVP (TLM / matvec), VJP (4D-Var gradient), HVP (incremental Gauss-Newton outer loop).
// Prove each with numbers — "grad-enabled" is not "differentiable" — on the composite of the
// split-explicit stage operators (diag_p_al -> horizontal_pgf, curvature_w_stage, rhs_ph_stage),
// in float64 so the FD referee is crisp:
//   VJP: backward(); <dL/dx, v> must match central FD of L.
//   JVP: forward-mode dual (_make_dual/_unpack_dual, the repo's jvp_fwad pattern); the tangent
//        of L is the SAME directional derivative — JVP==VJP is analytic-vs-analytic (~1e-12).
//   HVP: double-backward grad(<grad L, v>); <v,Hv> must match central FD of <grad L, v>.
static bool stage_ops_ad_triple() {
    const int ny = 6, nz = 10, nx = 8, nzw = nz + 1, nyv = ny + 1, nxu = nx + 1;
    auto o = torch::TensorOptions().dtype(torch::kFloat64);
    // sigma-like metrics, hydrostatic-ish base (al_full ~ 0.8)
    auto phb  = (torch::arange(nzw, o) * 1800.0).reshape({1, nzw, 1}).expand({ny, nzw, nx}).clone();
    auto pb   = torch::full({ny, nz, nx}, 5.0e4, o);
    auto mub  = torch::full({ny, nx}, 9.0e4, o), muts = mub.clone(), mut = mub.clone();
    auto muu  = torch::full({ny, nxu}, 9.0e4, o), muv = torch::full({nyv, nx}, 9.0e4, o);
    auto c1h  = torch::ones({nz}, o),  c2h = torch::zeros({nz}, o);
    auto c1f  = torch::ones({nzw}, o), c2f = torch::zeros({nzw}, o);
    auto fnm  = torch::full({nzw}, 0.5, o), fnp = torch::full({nzw}, 0.5, o);
    auto rdnw = torch::full({nz}, 40.0, o);
    auto msfuy = torch::ones({ny, nxu}, o), msfvx_inv = torch::ones({nyv, nx}, o);
    auto msftx = torch::ones({ny, nx}, o),  msfty = torch::ones({ny, nx}, o);
    // structured deterministic fields (nonzero u/v so curvature's quadratic term is live;
    // nonzero ww so rhs_ph's wdwn vertical-advection path is on the graph too)
    auto pat = [&](std::vector<int64_t> sh, double amp, double off) {
        int64_t n = 1; for (auto d : sh) n *= d;
        return (off + amp * torch::sin(0.7 * torch::arange(n, o))).reshape(sh);
    };
    auto ww  = pat({ny, nzw, nx}, 0.5, 0.0), wcn = pat({ny, nzw, nx}, 0.2, 0.0);
    auto ph0 = pat({ny, nzw, nx}, 5.0, 0.0), u0 = pat({ny, nz, nxu}, 2.0, 10.0);
    auto v0  = pat({nyv, nz, nx}, 1.0, 5.0), th0 = pat({ny, nz, nx}, 0.5, 0.0);
    auto mu0 = pat({ny, nx}, 50.0, 0.0);
    auto dir = [&](const torch::Tensor& x, double phase) {   // unit directions, distinct phases
        auto d = torch::cos(phase + 0.3 * torch::arange(x.numel(), o)).reshape(x.sizes());
        return d / d.norm();
    };
    auto dph = dir(ph0, 0.1), du = dir(u0, 0.5), dv = dir(v0, 0.9), dth = dir(th0, 1.3), dmu = dir(mu0, 1.7);
    const float p0 = 1e5f, rd = 287.04f, cpovcv = 1.4f, T0 = 300.0f, rdx = 1e-4f, rdy = 1e-4f;
    const float cf1 = 2.0f, cf2 = -1.0f, cf3 = 0.0f, cfn = 1.5f, cfn1 = -0.5f, grav = 9.81f;
    const float rr = 1.0f / 6370000.0f;
    auto L = [&](const torch::Tensor& ph, const torch::Tensor& u, const torch::Tensor& v,
                 const torch::Tensor& th, const torch::Tensor& mu) {
        auto [p_p, al_p, al_f] = diag_p_al(ph, phb, th, pb, muts, mub, c1h, c2h, rdnw, p0, rd, cpovcv, T0);
        auto [dpx, dpy] = horizontal_pgf(ph, p_p, al_p, al_f, pb, ph + phb, mu, muu, muv,
                                         c1h, c2h, fnm, fnp, rdnw, rdx, rdy, cf1, cf2, cf3);
        auto cw  = curvature_w_stage(u, v, muu, muv, msfuy, msfvx_inv, msftx, msfty, c1h, c2h, fnm, fnp, rr);
        auto pht = rhs_ph_stage(ph, phb, u, v, ww, wcn, mut, muu, muv, msfty,
                                c1f, c2f, fnm, fnp, rdnw, cfn, cfn1, rdx, rdy, grav);
        return dpx.pow(2).mean() + dpy.pow(2).mean() + cw.pow(2).mean() + pht.pow(2).mean();
    };
    // --- VJP (reverse mode) ---
    auto phL = ph0.clone().set_requires_grad(true), uL = u0.clone().set_requires_grad(true),
         vL = v0.clone().set_requires_grad(true), thL = th0.clone().set_requires_grad(true),
         muL = mu0.clone().set_requires_grad(true);
    L(phL, uL, vL, thL, muL).backward();
    bool gdef = phL.grad().defined() && uL.grad().defined() && vL.grad().defined()
             && thL.grad().defined() && muL.grad().defined();
    double vjp = 0.0;
    if (gdef) { torch::NoGradGuard ng;
        vjp = ((phL.grad() * dph).sum() + (uL.grad() * du).sum() + (vL.grad() * dv).sum()
             + (thL.grad() * dth).sum() + (muL.grad() * dmu).sum()).item<double>(); }
    // --- JVP (forward-mode dual) ---
    double jvp = 0.0; bool jvp_ran = true;
    try {
        auto lvl = torch::autograd::forward_ad::enter_dual_level();
        auto mk = [&](const torch::Tensor& x, const torch::Tensor& d) {
            return torch::_make_dual(x, d, static_cast<int64_t>(lvl)); };
        auto lossd = L(mk(ph0, dph), mk(u0, du), mk(v0, dv), mk(th0, dth), mk(mu0, dmu));
        auto parts = torch::_unpack_dual(lossd, static_cast<int64_t>(lvl));
        { torch::NoGradGuard ng;
          jvp = std::get<1>(parts).defined() ? std::get<1>(parts).item<double>() : 0.0; }
        torch::autograd::forward_ad::exit_dual_level(lvl);
    } catch (const std::exception& ex) {
        std::cout << "#   forward-mode JVP threw: " << ex.what() << "\n"; jvp_ran = false;
    }
    // --- FD referee for the same directional derivative ---
    const double e = 1e-4; double fd;
    { torch::NoGradGuard ng;
      auto lp = L(ph0 + e * dph, u0 + e * du, v0 + e * dv, th0 + e * dth, mu0 + e * dmu);
      auto lm = L(ph0 - e * dph, u0 - e * du, v0 - e * dv, th0 - e * dth, mu0 - e * dmu);
      fd = (lp - lm).item<double>() / (2.0 * e); }
    // --- HVP (double-backward) + its own FD referee d/ds <grad L(x+s v), v> ---
    auto gdot_at = [&](double s) {  // <grad L(x + s*v), v> via a fresh reverse pass
        auto a = (ph0 + s * dph).clone().set_requires_grad(true);
        auto b = (u0 + s * du).clone().set_requires_grad(true);
        auto c = (v0 + s * dv).clone().set_requires_grad(true);
        auto d = (th0 + s * dth).clone().set_requires_grad(true);
        auto f = (mu0 + s * dmu).clone().set_requires_grad(true);
        auto gg = torch::autograd::grad({L(a, b, c, d, f)}, {a, b, c, d, f}, {},
                                        /*retain_graph=*/s == 0.0, /*create_graph=*/s == 0.0);
        if (s == 0.0) {  // at the base point, also produce Hv by differentiating the contraction
            auto gdotv = (gg[0] * dph).sum() + (gg[1] * du).sum() + (gg[2] * dv).sum()
                       + (gg[3] * dth).sum() + (gg[4] * dmu).sum();
            auto hv = torch::autograd::grad({gdotv}, {a, b, c, d, f});
            torch::NoGradGuard ng;
            bool hfin = true; double vHv = 0.0;
            for (int i = 0; i < 5; ++i) {
                hfin = hfin && hv[i].defined() && hv[i].isfinite().all().item<bool>();
                const torch::Tensor* dd[5] = {&dph, &du, &dv, &dth, &dmu};
                if (hv[i].defined()) vHv += (hv[i] * *dd[i]).sum().item<double>();
            }
            return std::pair<double, bool>{vHv, hfin};
        }
        torch::NoGradGuard ng;
        double r = ((gg[0] * dph).sum() + (gg[1] * du).sum() + (gg[2] * dv).sum()
                  + (gg[3] * dth).sum() + (gg[4] * dmu).sum()).item<double>();
        return std::pair<double, bool>{r, true};
    };
    double vHv = 0.0; bool hfin = false, hvp_ran = true; double fd_h = 0.0;
    try {
        auto base = gdot_at(0.0); vHv = base.first; hfin = base.second;
        fd_h = (gdot_at(e).first - gdot_at(-e).first) / (2.0 * e);
    } catch (const std::exception& ex) {
        std::cout << "#   HVP double-backward threw: " << ex.what() << "\n"; hvp_ran = false;
    }
    auto rel = [](double a, double b) { return std::abs(a - b) / (std::abs(b) + 1e-30); };
    bool ok = gdef && jvp_ran && hvp_ran && hfin
           && std::isfinite(vjp) && std::isfinite(jvp) && std::isfinite(fd) && std::isfinite(vHv)
           && std::abs(fd) > 1e-8 && std::abs(fd_h) > 1e-8      // non-trivial derivatives
           && rel(jvp, vjp) < 1e-10                             // analytic == analytic
           && rel(vjp, fd) < 1e-5 && rel(jvp, fd) < 1e-5        // both match the FD referee
           && rel(vHv, fd_h) < 1e-4;                            // curvature matches FD of gradient
    std::cout << "# stage-ops AD TRIPLE (f64): JVP=" << jvp << " VJP=" << vjp << " FD=" << fd
              << " rel(J,V)=" << rel(jvp, vjp) << " rel(V,FD)=" << rel(vjp, fd)
              << " | HVP vHv=" << vHv << " FDh=" << fd_h << " rel=" << rel(vHv, fd_h)
              << " : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// Full-chain AD TRIPLE through advance_substep — the same three primitives through the ENTIRE
// acoustic operator chain (uv PGF -> mu/t column -> the implicit vertical Thomas solve in
// advance_w -> calc_p_rho), float32, direction on ph'. JVP-vs-VJP is analytic-vs-analytic;
// FD referees are float32-limited. HVP here proves double-backward survives the out-of-place
// stacked Thomas recurrence — the op most likely to break create_graph=true.
static bool substep_ad_triple() {
    auto o = torch::TensorOptions().dtype(torch::kFloat32);
    State s0; Const c; make_2d_case(s0, c);
    auto v = torch::sin(torch::arange(s0.ph.numel(), o)).reshape(s0.ph.sizes());
    v = v / v.norm();
    auto Lof = [&](const torch::Tensor& phv) {
        State s = s0; s.ph = phv;
        State out = advance_substep(s, c, 1);
        // linear + quadratic: make_2d_case's zero-perturbation base is a FIXED POINT of the
        // substep (outputs == 0), so a pure sum-of-squares has an exactly-zero first derivative
        // there. The linear term gives JVP/VJP a nonzero target; the quadratic keeps H nonzero.
        return out.w.sum() + out.ph.sum() + out.p.sum()
             + 0.5 * (out.w.pow(2).sum() + out.ph.pow(2).sum() + out.p.pow(2).sum());
    };
    // VJP + HVP via one create_graph reverse pass
    auto ph = s0.ph.clone().set_requires_grad(true);
    auto g = torch::autograd::grad({Lof(ph)}, {ph}, {}, true, true);
    double vjp; { torch::NoGradGuard ng; vjp = (g[0] * v).sum().item<double>(); }
    bool hvp_ran = true; torch::Tensor hv;
    try { hv = torch::autograd::grad({(g[0] * v).sum()}, {ph})[0]; }
    catch (const std::exception& ex) {
        std::cout << "#   substep double-backward threw: " << ex.what() << "\n"; hvp_ran = false; }
    bool hdef; double vHv = 0.0, hnorm = 0.0;
    { torch::NoGradGuard ng;
      hdef = hvp_ran && hv.defined() && hv.isfinite().all().item<bool>();
      if (hdef) { vHv = (hv * v).sum().item<double>(); hnorm = hv.norm().item<double>(); } }
    // JVP (forward dual through the whole substep)
    double jvp = 0.0; bool jvp_ran = true;
    try {
        auto lvl = torch::autograd::forward_ad::enter_dual_level();
        auto lossd = Lof(torch::_make_dual(s0.ph, v, static_cast<int64_t>(lvl)));
        auto parts = torch::_unpack_dual(lossd, static_cast<int64_t>(lvl));
        { torch::NoGradGuard ng;
          jvp = std::get<1>(parts).defined() ? std::get<1>(parts).item<double>() : 0.0; }
        torch::autograd::forward_ad::exit_dual_level(lvl);
    } catch (const std::exception& ex) {
        std::cout << "#   substep forward-mode JVP threw: " << ex.what() << "\n"; jvp_ran = false;
    }
    // FD referees (float32: loose but independent)
    const float e = 1e-1f; double fd, fd_h;
    { torch::NoGradGuard ng;
      fd = (Lof(s0.ph + e * v) - Lof(s0.ph - e * v)).item<double>() / (2.0 * e); }
    auto gdot_at = [&](float s) {
        auto p2 = (s0.ph + s * v).clone().set_requires_grad(true);
        auto g2 = torch::autograd::grad({Lof(p2)}, {p2});
        torch::NoGradGuard ng; return (g2[0] * v).sum().item<double>(); };
    fd_h = (gdot_at(e) - gdot_at(-e)) / (2.0 * e);
    auto rel = [](double a, double b) { return std::abs(a - b) / (std::abs(b) + 1e-30); };
    bool ok = jvp_ran && hdef && std::isfinite(vjp) && std::isfinite(jvp) && std::isfinite(vHv)
           && std::abs(fd) > 1e-6 && hnorm > 0.0
           && rel(jvp, vjp) < 1e-3                              // two analytic mechanisms agree
           && rel(vjp, fd) < 1e-2 && rel(vHv, fd_h) < 5e-2;     // float32 FD referees
    std::cout << "# advance_substep AD TRIPLE (f32): JVP=" << jvp << " VJP=" << vjp << " FD=" << fd
              << " rel(J,V)=" << rel(jvp, vjp) << " | HVP vHv=" << vHv << " FDh=" << fd_h
              << " rel=" << rel(vHv, fd_h) << " |Hv|=" << hnorm
              << " : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// WRF contract: ph_tend is NOT physical d(phi)/dt. rhs_ph stores the mass/map-factor
// coupled quantity (mu/msfty)*d(phi)/dt; advance_w decouples it as
//     delta_phi_direct = dts * msfty * ph_tend / (c1f*mut + c2f).
// em_b_wave has msfty=1. Isolate that arm with g=0, ww=0, w=0 and identity Thomas
// factors. Scaling mass and ph_tend together must leave delta_phi invariant.
static bool ph_tendency_mass_coupling_contract() {
    State base; Const base_c; make_2d_case(base, base_c);
    const int64_t ny = base.ph.size(0), nzw = base.ph.size(1), nx = base.ph.size(2);
    const float dts = 5.0f, q = 2.0e-2f;  // physical d(phi)/dt; expected direct increment = 0.1

    auto run = [&](float mass, float physical_q) {
        State s = base;
        Const c = base_c;
        s.ph = torch::zeros_like(base.ph);
        s.w = torch::zeros_like(base.w);
        s.ww = torch::zeros_like(base.ww);
        s.t_ave = torch::zeros_like(base.t);
        s.t_2ave = torch::zeros_like(base.t);
        s.muave = torch::zeros_like(base.muave);
        s.muts = torch::full_like(base.muts, mass);

        c.dts = dts;
        c.g = 0.0f;  // remove acoustic PGF/buoyancy/final-w arms; leave only frozen ph_tend
        c.mut = torch::full_like(base_c.mut, mass);
        c.muts = torch::full_like(base_c.muts, mass);
        c.a = torch::zeros_like(base_c.a);
        c.alpha = torch::ones_like(base_c.alpha);
        c.gamma = torch::zeros_like(base_c.gamma);
        c.rw_tend = torch::zeros_like(base_c.rw_tend);
        c.ph_1 = torch::zeros_like(base_c.ph_1);
        c.phb = torch::zeros_like(base_c.phb);
        c.ph_tend = torch::full({ny, nzw, nx}, mass * physical_q, base.ph.options());
        c.ph_tend.index_put_({torch::indexing::Slice(), 0, torch::indexing::Slice()},
                             torch::zeros({ny, nx}, base.ph.options()));
        return advance_w(s, c);
    };

    State out1 = run(9.0e4f, q);
    State out2 = run(1.8e5f, q);       // same physical q, doubled coupled tendency and mass
    State out3 = run(9.0e4f, 2.0f*q);  // linearity control

    auto expected = torch::zeros_like(base.ph);
    expected.index_put_({torch::indexing::Slice(), torch::indexing::Slice(1, nzw),
                         torch::indexing::Slice()},
                        torch::full({ny, nzw - 1, nx}, dts * q, base.ph.options()));
    const float formula_err = (out1.ph - expected).abs().max().item<float>();
    const float mass_invariance_err = (out2.ph - out1.ph).abs().max().item<float>();
    const float linearity_err = (out3.ph - 2.0f*out1.ph).abs().max().item<float>();
    const float w_leak = std::max({out1.w.abs().max().item<float>(),
                                   out2.w.abs().max().item<float>(),
                                   out3.w.abs().max().item<float>()});
    const float coupled = 9.0e4f * q;
    const float recovered_q = coupled / 9.0e4f;
    const bool dimensional = std::abs(coupled - 1800.0f) < 1e-3f
                          && std::abs(recovered_q - q) < 1e-7f;
    const bool ok = dimensional && formula_err < 2e-6f && mass_invariance_err < 2e-6f
                  && linearity_err < 2e-6f && w_leak < 1e-7f;
    std::cout << "# ph_tend mass-coupled contract: coupled=" << coupled
              << " physical=" << recovered_q << " dphi=" << dts*q
              << " err(formula,mass,linearity,w)=(" << formula_err << ","
              << mass_invariance_err << "," << linearity_err << "," << w_leak
              << ") : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

int main() {
    bool smoke_ok = full_substep_smoke();
    bool grad_ok  = grad_check();
    bool prep_ok  = prep_chain_check();
    bool rt_ok    = roundtrip_check();
    bool mass_ok  = mass_avg_check();
    bool sched_ok = schedule_check();
    bool ad3_ok   = stage_ops_ad_triple();
    bool sub3_ok  = substep_ad_triple();
    bool phmass_ok = ph_tendency_mass_coupling_contract();
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
    c.muts = torch::full({ny, nx}, mut, opt);
    c.fnm = torch::full({nzw}, 0.5f, opt); c.fnp = torch::full({nzw}, 0.5f, opt);
    c.c1h = c1h; c.c2h = c2h; c.c1f = c1f; c.c2f = c2f; c.rdn = rdn; c.rdnw = rdnw;
    c.dts = dts; c.g = g; c.epssm = eps; c.t0 = 300.0f;
    c.rw_tend = torch::zeros({ny, nzw, nx}, opt);
    c.ph_tend = torch::zeros({ny, nzw, nx}, opt);   // advance_w rhs now reads ph_tend
    c.t_1 = torch::zeros({ny, nz, nx}, opt);
    // ZERO save/base geopotential => dphf==0 => the ww*d(phi) advection term is inert, keeping
    // this a pure w-phi mode so the numpy von-Neumann reference |lambda|=0.998315 stays valid.
    c.ph_1 = torch::zeros({ny, nzw, nx}, opt);
    c.phb  = torch::zeros({ny, nzw, nx}, opt);

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
    s.t_ave  = torch::zeros({ny, nz, nx}, opt);   // advance_w's (1-epssm) old-theta arm: zero here
    s.ww     = torch::zeros({ny, nzw, nx}, opt);  // ww==0 keeps the ww*d(phi) advection inert

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
    bool ok = smoke_ok && grad_ok && prep_ok && rt_ok && mass_ok && sched_ok
           && ad3_ok && sub3_ok && phmass_ok && coeffs_ok && lambda_ok;
    std::cout << "# advance_w matches numpy scheme  coeffs=" << (coeffs_ok ? "ok" : "BAD")
              << "  |lambda|-match=" << (lambda_ok ? "ok" : "BAD")
              << "  : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}
