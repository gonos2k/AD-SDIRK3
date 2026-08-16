// 9F.D133: the ENABLED mu-Schur adjoint-sign path, against the PRODUCTION preconditioner.
//
// Its own executable for the same reason as Phi_Unity_Experiment_Enabled_Contract: the flag is
// read through a function-local static, so it latches on the first call in the process. A case
// appended to a binary that has already built a preconditioner would silently measure the
// DISABLED path while reporting coverage of the enabled one.
//
// THE DISCRIMINATING ASSERTION is the sign of the coupling product. With the flag OFF production
// gives +7906 per level (both directions sharing a sign); with it ON the divergence direction is
// flipped, so the product must be NEGATIVE. That is a property which is FALSE when the path is
// off -- the only kind of assertion that can prove an opt-in branch was actually taken.
//
// Both horizontal directions are checked, because u and v are siblings in one Schur sum and a
// flag that flipped only one of them would otherwise pass.
//
// NAMED AN EXPERIMENT. This proves the branch is reached and the coefficients carry the derived
// orientation. It does NOT prove the adjoint sign is the correct production default: it was
// measured to help alone (FGMRES rel_error 1 -> 0.9951) and to ANTI-COMBINE with the phi fix
// (-> 0.9999), which is why it ships opt-in.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_types.h"
#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_unified_rhs.h"

#include <torch/torch.h>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

std::shared_ptr<wrf::sdirk3::WRFGridInfo> tiny_grid() {
    auto g = std::make_shared<wrf::sdirk3::WRFGridInfo>();
    g->nx = 4;  g->ny = 3;  g->nz = 5;
    g->nx_u = 5; g->ny_v = 4; g->nz_w = 6;
    g->its = 1; g->ite = 4; g->jts = 1; g->jte = 3; g->kts = 1; g->kte = 5;
    g->ims = 1; g->ime = 4; g->jms = 1; g->jme = 3; g->kms = 1; g->kme = 6;
    g->ids = 1; g->ide = 5; g->jds = 1; g->jde = 4; g->kds = 1; g->kde = 6;
    g->dx = 1000.0f; g->dy = 1000.0f;
    return g;
}

}  // namespace

int main() {
    // BEFORE anything constructs a preconditioner -- see the header comment.
    ::setenv("WRF_SDIRK3_MU_SCHUR_ADJOINT_SIGN", "1", /*overwrite=*/1);

    std::cout << "=== Mu_Adjoint_Sign_Experiment_Enabled_Contract ===" << std::endl;

    auto grid = tiny_grid();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    wrf::sdirk3::UnifiedPreconditioner P(grid, physics, 600.0f, 0.4358665215f);

    check(true, "the enabled path CONSTRUCTS -- a throw in build would have ended the process");

    const auto c = P.horizontal_coupling_snapshot();
    check(c.c_u_mu.defined() && c.c_mu_u.defined() &&
          c.c_v_mu.defined() && c.c_mu_v.defined() &&
          c.c_mu_u.numel() > 0 && c.c_mu_v.numel() > 0,
          "all four couplings are readable, so both halves of the Schur sum have operands");

    struct Dir { const char* name; torch::Tensor prod; };
    const Dir dirs[] = {
        {"u", c.c_mu_u * c.c_u_mu},
        {"v", c.c_mu_v * c.c_v_mu},
    };
    for (const auto& d : dirs) {
        const double lo = d.prod.min().item<double>();
        const double hi = d.prod.max().item<double>();
        std::printf("  enabled C_mu_%s*C_%s_mu: min=%.6g max=%.6g\n", d.name, d.name, lo, hi);

        // FALSE when the flag is off -- production ships +7906 here.
        check(hi < 0.0,
              std::string("BRANCH CONFIRMED (") + d.name + "): every level's coupling product is "
              "NEGATIVE, the adjoint orientation -- shipped production gives +7906 here, so this "
              "assertion fails if the flag did not reach the preconditioner");
        check(std::abs(lo) > 0.0,
              std::string("and the ") + d.name + " product is nonzero, so the sign above is a "
              "real orientation and not a degenerate zero");
    }

    // The Schur step subtracts the product, so a negative product ADDS stiffness -- the whole
    // point of the derivation.
    {
        const auto prod_u = c.c_mu_u * c.c_u_mu;
        const double contribution = -prod_u.max().item<double>();
        std::printf("  Schur contribution to S_mu_mu (= -product): %+.6g\n", contribution);
        check(contribution > 0.0,
              "so S_mu_mu = D_mu - product now ADDS the acoustic round trip to the mass diagonal "
              "instead of eating it -- the direction A = I - hJ predicts");
    }

    // apply() still has to work on the enabled path.
    {
        const int64_t n =
            static_cast<int64_t>(grid->nx_u) * grid->ny * grid->nz
          + static_cast<int64_t>(grid->nx) * grid->ny_v * grid->nz
          + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
          + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
          + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz
          + static_cast<int64_t>(grid->nx) * grid->ny;
        torch::manual_seed(20260816);
        const auto v = torch::randn({n}, torch::kFloat32);
        P.update(v, 600.0f, 0.4358665215f);
        const auto z = P.apply(v);
        check(std::isfinite(z.norm().item<double>()) && z.norm().item<double>() > 0.0,
              "and apply() is finite and nonzero under the flipped sign -- a sign flip inside a "
              "Schur denominator is exactly the kind of edit that can produce a division blowup");
    }

    constexpr int expected_checks = 8;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "MU_ADJOINT_SIGN_ENABLED: PASS" << std::endl; return 0; }
    std::cout << "MU_ADJOINT_SIGN_ENABLED: FAIL (" << failures << ")" << std::endl;
    return 1;
}
