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
#include <string>
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

// Scalar-loop reference: verbatim calc_ww_cp with an INDEPENDENT
// transcription of the halo semantics per axis — periodic wrap reads the
// opposite physical edge (WRF memory halo under periodic_x/y), symmetric
// replicate degenerates the one-sided average to the edge value.
torch::Tensor scalar_ww_cp(const torch::Tensor& u, const torch::Tensor& v,
                           const torch::Tensor& mup, const torch::Tensor& mub,
                           const torch::Tensor& c1h, const torch::Tensor& c2h,
                           const torch::Tensor& dnw, float rdx, float rdy,
                           const torch::Tensor& msftx,
                           const torch::Tensor& msfuy,
                           const torch::Tensor& msfvx_inv,
                           bool x_periodic = false, bool y_periodic = false) {
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
        if (i == 0)
            return x_periodic ? 0.5f * (mut(j, 0) + mut(j, nx - 1))
                              : mut(j, 0);
        if (i == nx)
            return x_periodic ? 0.5f * (mut(j, 0) + mut(j, nx - 1))
                              : mut(j, nx - 1);
        return 0.5f * (mut(j, i) + mut(j, i - 1));
    };
    auto muv = [&](int j, int i) {  // v-point j in [0..ny]
        if (j == 0)
            return y_periodic ? 0.5f * (mut(0, i) + mut(ny - 1, i))
                              : mut(0, i);
        if (j == ny)
            return y_periodic ? 0.5f * (mut(0, i) + mut(ny - 1, i))
                              : mut(ny - 1, i);
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
    return wrf::sdirk3::compute_wrf_ww_cp(
        u, v, mup, mub, unit1, zero1, dnw, rdx, rdy, onesT, onesU, onesV,
        wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate,
        wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate);
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
    torch::Tensor prod(wrf::sdirk3::WWCPBoundaryPolicy xp =
                           wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate,
                       wrf::sdirk3::WWCPBoundaryPolicy yp =
                           wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate)
        const {
        return wrf::sdirk3::compute_wrf_ww_cp(u, v, mup, mub, c1h, c2h, dnw,
                                              rdx, rdy, msftx, msfuy,
                                              msfvx_inv, xp, yp);
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
        auto p = wrf::sdirk3::compute_wrf_ww_cp(
            fx.u, fx.v, fx.mup, fx.mub, unit1, zero1, fx.dnw, fx.rdx, fx.rdy,
            onesT, onesU, onesV,
            wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate,
            wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate);
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
        auto d = (wrf::sdirk3::compute_wrf_ww_cp(
                      u2, fx.v, fx.mup, fx.mub, fx.c1h, fx.c2h, fx.dnw,
                      fx.rdx, fx.rdy, fx.msftx, fx.msfuy, fx.msfvx_inv,
                      wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate,
                      wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate) -
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
                      fx.msfvx_inv,
                      wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate,
                      wrf::sdirk3::WWCPBoundaryPolicy::SymmetricReplicate);
              }),
              "fail-close: mis-shaped u-stagger map factor rejected");
    }


    // (7)-(8) PERIODIC seam contract (review P1-1): the production helper's
    //     wrap must match an INDEPENDENT scalar periodic-halo transcription,
    //     and must measurably differ from the replicate policy (the fixture
    //     has first mass != last mass by construction).
    using wrf::sdirk3::WWCPBoundaryPolicy;
    {
        auto prod_px = fx.prod(WWCPBoundaryPolicy::Periodic,
                               WWCPBoundaryPolicy::SymmetricReplicate);
        auto ref_px = scalar_ww_cp(fx.u, fx.v, fx.mup, fx.mub, fx.c1h,
                                   fx.c2h, fx.dnw, fx.rdx, fx.rdy, fx.msftx,
                                   fx.msfuy, fx.msfvx_inv,
                                   /*x_periodic=*/true, /*y_periodic=*/false);
        const float re = rel_err(prod_px, ref_px);
        std::printf("ww_cp periodic-x seam: prod vs scalar rel=%.3e\n", re);
        check(re <= 1e-5f, "periodic-x: wrap matches the scalar halo");
        check((prod_px - fx.prod()).abs().max().item<float>() > 0.0f,
              "periodic-x: wrap measurably differs from replicate");
    }
    {
        auto prod_py = fx.prod(WWCPBoundaryPolicy::SymmetricReplicate,
                               WWCPBoundaryPolicy::Periodic);
        auto ref_py = scalar_ww_cp(fx.u, fx.v, fx.mup, fx.mub, fx.c1h,
                                   fx.c2h, fx.dnw, fx.rdx, fx.rdy, fx.msftx,
                                   fx.msfuy, fx.msfvx_inv,
                                   /*x_periodic=*/false, /*y_periodic=*/true);
        check(rel_err(prod_py, ref_py) <= 1e-5f,
              "periodic-y: wrap matches the scalar halo");
        check((prod_py - fx.prod()).abs().max().item<float>() > 0.0f,
              "periodic-y: wrap measurably differs from replicate");
    }

    // (9)-(11) fail-close families: every violation must end in its STABLE
    //     marker (never a generic c10 exception), and unsupported boundary
    //     policies must refuse rather than guess a halo.
    auto fails_with = [](const char* marker, auto&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            return std::string(e.what()).find(marker) == 0;
        }
        return false;
    };
    {
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  fx.prod(WWCPBoundaryPolicy::Unsupported,
                          WWCPBoundaryPolicy::SymmetricReplicate);
              }),
              "policy fail-close: Unsupported x refuses");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  fx.prod(WWCPBoundaryPolicy::SymmetricReplicate,
                          WWCPBoundaryPolicy::HaloProvided);
              }),
              "policy fail-close: HaloProvided (no halo wired) refuses");
    }
    {
        auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        auto SR = WWCPBoundaryPolicy::SymmetricReplicate;
        auto call_with = [&](const torch::Tensor& mup_a,
                             const torch::Tensor& u_a,
                             const torch::Tensor& c1h_a,
                             const torch::Tensor& dnw_a,
                             const torch::Tensor& msftx_a,
                             const torch::Tensor& msfuy_a) {
            return wrf::sdirk3::compute_wrf_ww_cp(
                u_a, fx.v, mup_a, fx.mub, c1h_a, fx.c2h, dnw_a, fx.rdx,
                fx.rdy, msftx_a, msfuy_a, fx.msfvx_inv, SR, SR);
        };
        torch::Tensor undef;
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  call_with(undef, fx.u, fx.c1h, fx.dnw, fx.msftx, fx.msfuy);
              }),
              "helper fail-close: undefined mup -> stable marker");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  call_with(fx.mup.reshape({-1}), fx.u, fx.c1h, fx.dnw,
                            fx.msftx, fx.msfuy);
              }),
              "helper fail-close: 1-D mup -> stable marker");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  call_with(fx.mup, undef, fx.c1h, fx.dnw, fx.msftx,
                            fx.msfuy);
              }),
              "helper fail-close: undefined u -> stable marker");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  call_with(fx.mup, fx.u, torch::ones({fx.nz + 2}, opt),
                            fx.dnw, fx.msftx, fx.msfuy);
              }),
              "helper fail-close: c1h length nz+2 -> stable marker");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  call_with(fx.mup, fx.u, fx.c1h,
                            torch::full({fx.nz + 1}, -0.01f, opt), fx.msftx,
                            fx.msfuy);
              }),
              "helper fail-close: dnw length nz+1 -> stable marker");
        {
            auto msftx_nan = fx.msftx.clone();
            msftx_nan[0][0] = std::nanf("");
            check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                      call_with(fx.mup, fx.u, fx.c1h, fx.dnw, msftx_nan,
                                fx.msfuy);
                  }),
                  "helper fail-close: NaN msftx -> stable marker");
        }
        {
            auto msfuy_zero = fx.msfuy.clone();
            msfuy_zero[0][0] = 0.0f;
            check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                      call_with(fx.mup, fx.u, fx.c1h, fx.dnw, fx.msftx,
                                msfuy_zero);
                  }),
                  "helper fail-close: zero msfuy (division metric) -> "
                  "stable marker");
        }
    }
    {
        // Denominator negatives (review P1-3): judged on the ACTUAL
        // c1f*mu+c2f, plus the rdnw/shape/dtype input contract.
        auto opt = torch::TensorOptions().dtype(torch::kFloat32);
        const int nz_w = fx.nz + 1;
        const int k_start = 1, k_end = nz_w - 1;
        const int n_int = k_end - k_start;
        auto kw = torch::arange(nz_w, opt);
        auto c1f = 1.0f - 0.05f * kw;
        auto c2f = 90.0f * kw;
        auto rdnw_int =
            (1.0f / (0.011f + 0.0015f * kw)).slice(0, k_start, k_end)
                .reshape({1, n_int, 1});
        auto i3w = torch::arange(fx.ny * nz_w * fx.nx, opt)
                       .reshape({fx.ny, nz_w, fx.nx});
        auto w_phys = 3.0f * torch::sin(i3w * 0.83f);
        auto mut_full = fx.mup + fx.mub;
        auto ww = fx.prod();
        auto damp = [&](const torch::Tensor& mu_a, const torch::Tensor& c1f_a,
                        const torch::Tensor& c2f_a,
                        const torch::Tensor& rdnw_a,
                        const torch::Tensor& ww_a) {
            return wrf::sdirk3::compute_w_damping_term(
                ww_a, w_phys, mu_a, c1f_a, c2f_a, rdnw_a, 30.0f,
                wrf::sdirk3::kWrfWAlpha, wrf::sdirk3::kWrfWBeta, 1.0f,
                k_start, k_end, nz_w);
        };
        check(fails_with("SDIRK3_WDAMP_INVALID_MASS", [&] {
                  damp(mut_full, -c1f, c2f, rdnw_int, ww);
              }),
              "denominator fail-close: positive mu, negative c1f");
        check(fails_with("SDIRK3_WDAMP_INVALID_MASS", [&] {
                  damp(mut_full, c1f, c2f - 2.0e5f, rdnw_int, ww);
              }),
              "denominator fail-close: sufficiently negative c2f");
        check(fails_with("SDIRK3_WDAMP_INVALID_MASS", [&] {
                  auto c1f_nan = c1f.clone();
                  c1f_nan[1] = std::nanf("");
                  damp(mut_full, c1f_nan, c2f, rdnw_int, ww);
              }),
              "denominator fail-close: NaN c1f -> non-finite denominator");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  auto rdnw_inf = rdnw_int.clone();
                  rdnw_inf[0][0][0] = INFINITY;
                  damp(mut_full, c1f, c2f, rdnw_inf, ww);
              }),
              "input fail-close: Inf rdnw_interior");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  damp(mut_full, c1f, c2f, rdnw_int.reshape({n_int}), ww);
              }),
              "input fail-close: wrong rdnw shape");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  damp(mut_full, c1f, c2f, rdnw_int,
                       torch::cat({ww, ww.slice(1, 0, 1)}, 1));
              }),
              "input fail-close: vertical size != nz_w");
        check(fails_with("SDIRK3_WDAMP_INVALID_INPUT", [&] {
                  damp(mut_full.to(torch::kFloat64), c1f, c2f, rdnw_int, ww);
              }),
              "input fail-close: dtype mismatch");
    }


    // (12) PR 9C.2: the runtime-contract authority. Judgment order and
    //      flag PRIORITY are the contract: disabled short-circuits every
    //      topology check; multi-rank / internal-tile / specified / nested
    //      / polar / open / one-sided-symmetric all refuse with the stable
    //      marker; open takes priority over conflicting periodic+symmetric.
    {
        using wrf::sdirk3::resolve_wdamp_runtime_contract;
        auto ok = [&](bool enabled, int npx, int npy, bool covers,
                      bool px, bool sxs, bool sxe, bool oxs, bool oxe,
                      bool py, bool sys, bool sye, bool oys, bool oye,
                      bool spec, bool nest, bool polar) {
            return resolve_wdamp_runtime_contract(
                enabled, npx, npy, covers, px, sxs, sxe, oxs, oxe, py, sys,
                sye, oys, oye, spec, nest, polar);
        };
        // em_b_wave shape: periodic x + two-sided symmetric y.
        auto c = ok(true, 1, 1, true, true, false, false, false, false,
                    false, true, true, false, false, false, false, false);
        check(c.active &&
                  c.x_policy == WWCPBoundaryPolicy::Periodic &&
                  c.y_policy == WWCPBoundaryPolicy::SymmetricReplicate,
              "contract: em_b_wave maps to {Periodic, SymmetricReplicate}");
        // disabled short-circuits even a hostile topology.
        auto d = ok(false, 4, 4, false, false, false, false, true, true,
                    false, false, false, true, true, true, true, true);
        check(!d.active, "contract: disabled is inactive, never throws");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  ok(true, 2, 1, true, true, false, false, false, false,
                     false, true, true, false, false, false, false, false);
              }),
              "contract: multi-rank refuses");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  ok(true, 1, 1, false, true, false, false, false, false,
                     false, true, true, false, false, false, false, false);
              }),
              "contract: internal tile (does not cover patch) refuses");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  ok(true, 1, 1, true, true, true, true, true, false,
                     false, true, true, false, false, false, false, false);
              }),
              "contract: open-x refuses EVEN WITH periodic+symmetric also "
              "set (priority)");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  ok(true, 1, 1, true, true, false, false, false, false,
                     false, true, true, false, false, true, false, false);
              }),
              "contract: specified refuses");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  ok(true, 1, 1, true, true, false, false, false, false,
                     false, true, true, false, false, false, true, false);
              }),
              "contract: nested refuses");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  ok(true, 1, 1, true, true, false, false, false, false,
                     false, true, true, false, false, false, false, true);
              }),
              "contract: polar refuses");
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  ok(true, 1, 1, true, true, false, false, false, false,
                     false, true, false, false, false, false, false, false);
              }),
              "contract: one-sided symmetric y refuses");
    }


    // (13) PR 9C.2: the production fail-handler ROUTING switch. With a
    //      handler installed, contract violations must reach the handler
    //      BEFORE any throw (the mpif90-linked production executable
    //      cannot unwind C++ exceptions — measured); without one, the
    //      helpers throw as every case above verified. The test handler
    //      throws a sentinel std::string, which can only escape if the
    //      router called the handler instead of throwing.
    {
        using wrf::sdirk3::wdamp_contract_fail_handler;
        wdamp_contract_fail_handler() = [](const char* what) {
            throw std::string(what);
        };
        bool routed_geometry = false;
        try {
            fx.prod(WWCPBoundaryPolicy::Unsupported,
                    WWCPBoundaryPolicy::SymmetricReplicate);
        } catch (const std::string& m) {
            routed_geometry =
                m.find("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED") == 0;
        } catch (...) {
        }
        check(routed_geometry,
              "fail routing: geometry violation reaches the installed "
              "handler, not a throw");
        bool routed_input = false;
        try {
            torch::Tensor undef;
            wrf::sdirk3::compute_wrf_ww_cp(
                undef, fx.v, fx.mup, fx.mub, fx.c1h, fx.c2h, fx.dnw, fx.rdx,
                fx.rdy, fx.msftx, fx.msfuy, fx.msfvx_inv,
                WWCPBoundaryPolicy::SymmetricReplicate,
                WWCPBoundaryPolicy::SymmetricReplicate);
        } catch (const std::string& m) {
            routed_input = m.find("SDIRK3_WDAMP_INVALID_INPUT") == 0;
        } catch (...) {
        }
        check(routed_input,
              "fail routing: input violation reaches the installed handler");
        wdamp_contract_fail_handler() = nullptr;
        check(fails_with("SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED", [&] {
                  fx.prod(WWCPBoundaryPolicy::Unsupported,
                          WWCPBoundaryPolicy::SymmetricReplicate);
              }),
              "fail routing: handler reset restores throw semantics");
    }

    const int kExpectedCases =
        3 + 1 + fx.nx + 1 + 3 + 4 + 2 + 2 + 2 + 7 + 7 + 9 + 3;  // = 48 with nx=4
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
