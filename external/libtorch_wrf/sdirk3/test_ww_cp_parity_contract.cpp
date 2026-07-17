// PR 9C.1 commit 1: the complete WRF calc_ww_cp state-to-omega contract.
//
// The reference below is an INDEPENDENT scalar-loop transcription of WRF's
// calc_ww_cp (dyn_em/module_big_step_utilities_em.f90:633) — deliberately
// not reusing the production helper, which is the object under test. The
// em_b_wave simplification (c1h==1, c2h==0, unit map factors — the formula
// the production W-damping used before this PR) is kept as the NEGATIVE
// CONTROL and must FAIL on the non-unit fixtures.
#include <torch/torch.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include "wrf_sdirk3_ww_cp.h"
#include "wrf_sdirk3_w_damping.h"

namespace {

int g_cases = 0;
int g_failures = 0;
void check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

float rel_err(const torch::Tensor& a, const torch::Tensor& b) {
    float an = a.norm().item<float>();
    float bn = b.norm().item<float>();
    return (a - b).norm().item<float>() / std::max({an, bn, 1e-30f});
}

// Scalar-loop reference: verbatim calc_ww_cp with the documented
// domain-edge REPLICATE policy for muu/muv.
torch::Tensor scalar_ww_cp(const torch::Tensor& u, const torch::Tensor& v,
                           const torch::Tensor& mup, const torch::Tensor& mub,
                           const torch::Tensor& c1h, const torch::Tensor& c2h,
                           const torch::Tensor& dnw, float rdx, float rdy,
                           const torch::Tensor& msftx,
                           const torch::Tensor& msfuy,
                           const torch::Tensor& msfvx_inv) {
    const int ny = mup.size(0), nx = mup.size(1), nz = u.size(1);
    auto ua = u.accessor<float, 3>();
    auto va = v.accessor<float, 3>();
    auto mupa = mup.accessor<float, 2>();
    auto muba = mub.accessor<float, 2>();
    auto c1a = c1h.accessor<float, 1>();
    auto c2a = c2h.accessor<float, 1>();
    auto dnwa = dnw.accessor<float, 1>();
    auto mtx = msftx.accessor<float, 2>();
    auto muy = msfuy.accessor<float, 2>();
    auto mvi = msfvx_inv.accessor<float, 2>();

    auto mut = [&](int j, int i) { return mupa[j][i] + muba[j][i]; };
    auto muu = [&](int j, int i) {  // u-point i in [0..nx]
        if (i == 0) return mut(j, 0);
        if (i == nx) return mut(j, nx - 1);
        return 0.5f * (mut(j, i) + mut(j, i - 1));
    };
    auto muv = [&](int j, int i) {  // v-point j in [0..ny]
        if (j == 0) return mut(0, i);
        if (j == ny) return mut(ny - 1, i);
        return 0.5f * (mut(j, i) + mut(j - 1, i));
    };

    auto ww = torch::zeros({ny, nz + 1, nx}, u.options());
    auto wwa = ww.accessor<float, 3>();
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            std::vector<float> divv(nz);
            float dmdt = 0.0f;
            for (int k = 0; k < nz; ++k) {
                const float cuR =
                    (c1a[k] * muu(j, i + 1) + c2a[k]) * ua[j][k][i + 1] /
                    muy[j][i + 1];
                const float cuL =
                    (c1a[k] * muu(j, i) + c2a[k]) * ua[j][k][i] / muy[j][i];
                const float cvT =
                    (c1a[k] * muv(j + 1, i) + c2a[k]) * va[j + 1][k][i] *
                    mvi[j + 1][i];
                const float cvB =
                    (c1a[k] * muv(j, i) + c2a[k]) * va[j][k][i] * mvi[j][i];
                divv[k] = mtx[j][i] * dnwa[k] *
                          (rdx * (cuR - cuL) + rdy * (cvT - cvB));
                dmdt += divv[k];
            }
            wwa[j][0][i] = 0.0f;
            for (int k = 1; k <= nz - 1; ++k) {
                wwa[j][k][i] = wwa[j][k - 1][i] -
                               dnwa[k - 1] * c1a[k - 1] * dmdt - divv[k - 1];
            }
            wwa[j][nz][i] = 0.0f;  // WRF explicit top BC
        }
    }
    return ww;
}

// The retired em_b_wave SIMPLIFICATION (c1h==1, c2h==0, unit map factors) —
// the negative control.
torch::Tensor simplified_ww(const torch::Tensor& u, const torch::Tensor& v,
                            const torch::Tensor& mup, const torch::Tensor& mub,
                            const torch::Tensor& dnw, float rdx, float rdy) {
    const int ny = mup.size(0), nx = mup.size(1), nz = u.size(1);
    auto unit1 = torch::ones({nz}, u.options());
    auto zero1 = torch::zeros({nz}, u.options());
    auto onesT = torch::ones({ny, nx}, u.options());
    auto onesU = torch::ones({ny, nx + 1}, u.options());
    auto onesV = torch::ones({ny + 1, nx}, u.options());
    return wrf::sdirk3::compute_wrf_ww_cp(u, v, mup, mub, unit1, zero1, dnw,
                                          rdx, rdy, onesT, onesU, onesV);
}

struct Fixture {
    int ny = 5, nz = 6, nx = 4;
    torch::Tensor u, v, mup, mub, c1h, c2h, dnw, msftx, msfuy, msfvx_inv;
    float rdx = 2.5e-4f, rdy = 2.5e-4f;
    Fixture() {
        auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        auto i3u = torch::arange(ny * nz * (nx + 1), opt).reshape({ny, nz, nx + 1});
        auto i3v = torch::arange((ny + 1) * nz * nx, opt).reshape({ny + 1, nz, nx});
        auto i2 = torch::arange(ny * nx, opt).reshape({ny, nx});
        u = 12.0f * torch::sin(i3u * 0.7f);              // +/- u
        v = 8.0f * torch::sin(i3v * 1.1f + 0.3f);        // +/- v
        mup = 900.0f * torch::sin(i2 * 1.3f);            // x/y-varying mass
        mub = 94000.0f + 500.0f * torch::sin(i2 * 0.4f);
        auto kf = torch::arange(nz, opt);
        c1h = 1.0f - 0.06f * kf;                          // non-unit c1h
        c2h = 120.0f * kf;                                // non-unit c2h
        dnw = -(0.010f + 0.002f * kf);                    // WRF-negative
        msftx = 1.0f + 0.10f * torch::sin(i2 * 0.9f);     // non-unit msf
        auto i2u = torch::arange(ny * (nx + 1), opt).reshape({ny, nx + 1});
        msfuy = 1.0f + 0.08f * torch::sin(i2u * 1.7f);
        auto i2v = torch::arange((ny + 1) * nx, opt).reshape({ny + 1, nx});
        msfvx_inv = 1.0f / (1.0f + 0.07f * torch::sin(i2v * 2.1f));
    }
    torch::Tensor prod() const {
        return wrf::sdirk3::compute_wrf_ww_cp(u, v, mup, mub, c1h, c2h, dnw,
                                              rdx, rdy, msftx, msfuy,
                                              msfvx_inv);
    }
    torch::Tensor ref() const {
        return scalar_ww_cp(u, v, mup, mub, c1h, c2h, dnw, rdx, rdy, msftx,
                            msfuy, msfvx_inv);
    }
};

}  // namespace

int main() {
    torch::NoGradGuard no_grad;
    Fixture fx;

    // (1) full non-unit fixture: production helper == scalar reference.
    {
        auto p = fx.prod();
        auto r = fx.ref();
        const float re = rel_err(p, r);
        std::printf("ww_cp full fixture: prod vs scalar ref rel=%.3e\n", re);
        check(re <= 1e-5f, "non-unit fixture: helper matches scalar reference");
        // boundary levels exactly zero
        check(p.select(1, 0).abs().max().item<float>() == 0.0f,
              "bottom ww == 0");
        check(p.select(1, fx.nz).abs().max().item<float>() == 0.0f,
              "top ww == 0 (explicit WRF BC)");
    }

    // (2) unit-metric limit: helper with unit metrics == simplified formula
    //     (sanity that the generalization reduces correctly).
    {
        auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        auto unit1 = torch::ones({fx.nz}, opt);
        auto zero1 = torch::zeros({fx.nz}, opt);
        auto onesT = torch::ones({fx.ny, fx.nx}, opt);
        auto onesU = torch::ones({fx.ny, fx.nx + 1}, opt);
        auto onesV = torch::ones({fx.ny + 1, fx.nx}, opt);
        auto p = wrf::sdirk3::compute_wrf_ww_cp(fx.u, fx.v, fx.mup, fx.mub,
                                                unit1, zero1, fx.dnw, fx.rdx,
                                                fx.rdy, onesT, onesU, onesV);
        auto r = scalar_ww_cp(fx.u, fx.v, fx.mup, fx.mub, unit1, zero1,
                              fx.dnw, fx.rdx, fx.rdy, onesT, onesU, onesV);
        check(rel_err(p, r) <= 1e-5f, "unit-metric limit matches reference");
    }

    // (3) stagger index: shifting a single interior u column changes divv at
    //     exactly the two adjacent mass columns (left/right of the u face).
    {
        auto u2 = fx.u.clone();
        const int iface = 2;  // interior u-point
        u2.select(2, iface) += 1.0f;
        auto d = (wrf::sdirk3::compute_wrf_ww_cp(u2, fx.v, fx.mup, fx.mub,
                                                 fx.c1h, fx.c2h, fx.dnw,
                                                 fx.rdx, fx.rdy, fx.msftx,
                                                 fx.msfuy, fx.msfvx_inv) -
                  fx.prod())
                     .abs()
                     .sum(1);  // [ny, nx]
        auto per_col = d.sum(0);  // [nx]
        for (int i = 0; i < fx.nx; ++i) {
            const bool touched = (i == iface - 1) || (i == iface);
            const float val = per_col[i].item<float>();
            if (touched) {
                check(val > 0.0f, "stagger: adjacent mass column responds");
            } else {
                check(val == 0.0f, "stagger: distant mass column untouched");
            }
        }
    }

    // (4) NEGATIVE CONTROL: the retired em_b_wave simplification must FAIL
    //     against the full reference on the non-unit fixture.
    {
        auto simp = simplified_ww(fx.u, fx.v, fx.mup, fx.mub, fx.dnw, fx.rdx,
                                  fx.rdy);
        const float re = rel_err(simp, fx.ref());
        std::printf("ww_cp negative control: simplified vs full rel=%.3e\n",
                    re);
        check(re > 1e-2f,
              "NEGATIVE CONTROL: the unit-metric simplification fails the "
              "full contract on non-unit fixtures");
    }


    // (5) END-TO-END composed contract: production chain
    //     compute_wrf_ww_cp -> compute_w_damping_term  vs  the scalar
    //     calc_ww_cp reference composed with an independent scalar W_DAMP
    //     transcription (mass = c1f*mut+c2f; vert_cfl = |ww/mass*rdnw*dt|;
    //     gate '>'; term = SIGN(1.,w)*alpha*(cfl-crit)*mass), on the
    //     non-unit fixture with BOTH active and inactive cells present.
    {
        torch::NoGradGuard no_grad2;
        auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        const int nz_w = fx.nz + 1;
        const int k_start = 1, k_end = nz_w - 1;
        const int n_int = k_end - k_start;
        auto kw = torch::arange(nz_w, opt);
        auto c1f = 1.0f - 0.05f * kw;
        auto c2f = 90.0f * kw;
        auto rdnw_full = 1.0f / (0.011f + 0.0015f * kw);
        auto rdnw_int = rdnw_full.slice(0, k_start, k_end)
                            .reshape({1, n_int, 1});
        auto i3w = torch::arange(fx.ny * nz_w * fx.nx, opt)
                       .reshape({fx.ny, nz_w, fx.nx});
        auto w_phys = 3.0f * torch::sin(i3w * 0.83f);  // +/- physical w
        auto mut_full = fx.mup + fx.mub;

        auto ww = fx.prod();
        // Pick dt so the gate splits the interior: scale to max cfl ~ 8.
        float cfl_at_1 = (ww.slice(1, k_start, k_end) /
                          (c1f.slice(0, k_start, k_end).reshape({1, n_int, 1}) *
                               mut_full.unsqueeze(1) +
                           c2f.slice(0, k_start, k_end).reshape({1, n_int, 1})) *
                          rdnw_int)
                             .abs()
                             .max()
                             .item<float>();
        const float dt = 8.0f / cfl_at_1;
        const float alpha = wrf::sdirk3::kWrfWAlpha;
        const float gate = wrf::sdirk3::kWrfWBeta;
        const float crit = 1.0f;

        auto prod_term = wrf::sdirk3::compute_w_damping_term(
            ww, w_phys, mut_full, c1f, c2f, rdnw_int, dt, alpha, gate, crit,
            k_start, k_end, nz_w);

        // Independent scalar composition.
        auto ww_ref = fx.ref();
        auto ref_term = torch::zeros_like(prod_term);
        {
            auto wwa = ww_ref.accessor<float, 3>();
            auto wa = w_phys.accessor<float, 3>();
            auto muta = mut_full.accessor<float, 2>();
            auto c1a = c1f.accessor<float, 1>();
            auto c2a = c2f.accessor<float, 1>();
            auto rda = rdnw_full.accessor<float, 1>();
            auto ra = ref_term.accessor<float, 3>();
            int active = 0, inactive = 0;
            for (int j = 0; j < fx.ny; ++j)
                for (int k = k_start; k < k_end; ++k)
                    for (int i = 0; i < fx.nx; ++i) {
                        const float mass = c1a[k] * muta[j][i] + c2a[k];
                        const float cfl =
                            std::fabs(wwa[j][k][i] / mass * rda[k] * dt);
                        if (cfl > gate) {
                            const float sgn =
                                std::signbit(wa[j][k][i]) ? -1.0f : 1.0f;
                            ra[j][k][i] = sgn * alpha * (cfl - crit) * mass;
                            ++active;
                        } else {
                            ++inactive;
                        }
                    }
            check(active > 0, "end-to-end: gate has active cells");
            check(inactive > 0, "end-to-end: gate has inactive cells");
        }
        const float re = rel_err(prod_term, ref_term);
        std::printf("ww_cp end-to-end composed: prod vs scalar rel=%.3e\n",
                    re);
        check(re <= 1e-5f,
              "END-TO-END: production omega+W_DAMP chain matches the "
              "composed scalar references");

        // (6) validation fail-close: the production helpers REJECT contract
        //     violations with stable markers (never silently repair).
        auto throws_with = [](const char* marker, auto&& fn) {
            try {
                fn();
            } catch (const std::invalid_argument& e) {
                return std::string(e.what()).find(marker) == 0;
            }
            return false;
        };
        check(throws_with("SDIRK3_WDAMP_INVALID_MASS", [&] {
                  wrf::sdirk3::compute_w_damping_term(
                      ww, w_phys, -mut_full, c1f, c2f, rdnw_int, dt, alpha,
                      gate, crit, k_start, k_end, nz_w);
              }),
              "fail-close: non-positive mass rejected");
        check(throws_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  wrf::sdirk3::compute_w_damping_term(
                      ww, w_phys, mut_full, c1f, c2f, rdnw_int, 0.0f, alpha,
                      gate, crit, k_start, k_end, nz_w);
              }),
              "fail-close: dt=0 rejected");
        check(throws_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  wrf::sdirk3::compute_w_damping_term(
                      ww, w_phys, mut_full.slice(1, 0, fx.nx - 1), c1f, c2f,
                      rdnw_int, dt, alpha, gate, crit, k_start, k_end, nz_w);
              }),
              "fail-close: mis-shaped mass rejected");
        check(throws_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  wrf::sdirk3::compute_wrf_ww_cp(
                      fx.u, fx.v, fx.mup, fx.mub, fx.c1h, fx.c2h, fx.dnw,
                      fx.rdx, fx.rdy, fx.msftx,
                      fx.msfuy.slice(1, 0, fx.nx),  // wrong u-stagger width
                      fx.msfvx_inv);
              }),
              "fail-close: mis-shaped u-stagger map factor rejected");
    }

    const int kExpectedCases = 3 + 1 + fx.nx + 1 + 3 + 4;  // = 16 with nx=4
    if (g_cases != kExpectedCases) {
        std::printf("FAIL: case-count ratchet: executed %d, expected %d\n",
                    g_cases, kExpectedCases);
        ++g_failures;
    }
    if (g_failures == 0) {
        std::printf("WW_CP parity contract: ALL PASS (%d/%d)\n", g_cases,
                    kExpectedCases);
        return 0;
    }
    std::printf("WW_CP parity contract: %d FAILURE(S)\n", g_failures);
    return 1;
}
