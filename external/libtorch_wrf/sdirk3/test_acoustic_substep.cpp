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

int main() {
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
    bool ok = coeffs_ok && lambda_ok;
    std::cout << "# advance_w matches numpy scheme  coeffs=" << (coeffs_ok ? "ok" : "BAD")
              << "  |lambda|-match=" << (lambda_ok ? "ok" : "BAD")
              << "  : " << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}
