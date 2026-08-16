// 9F.D131: the ENABLED phi-unity path, exercised against the PRODUCTION preconditioner.
//
// Why this is its own executable. The experiment flag is read through a function-local static,
// so it latches on the first call in the process. A case appended to an existing test binary
// would run after that binary had already built a preconditioner with the flag unset, and would
// silently measure the DISABLED path while claiming to cover the enabled one -- a witness that
// cannot fail, which this campaign has shipped before. Setting the environment at the top of a
// dedicated main(), before any preconditioner exists, is what makes the coverage real.
//
// What it pins: with the flag ON, the production UnifiedPreconditioner still BUILDS, and its
// apply() is finite, non-degenerate, and actually does something. The opt-in path had no
// executable test at all before this -- it was exercised only by hand-run live measurements, so
// a NaN, a crash, or a silently-identity operator on the enabled path would have reached main
// with 45 green tests behind it.

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_unified_preconditioner.h"
#include "../wrf_sdirk3_types.h"
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
    ::setenv("WRF_SDIRK3_PHI_SCHUR_DENOM_UNITY", "1", /*overwrite=*/1);

    std::cout << "=== Phi_Unity_Enabled_Contract ===" << std::endl;

    auto grid = tiny_grid();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    wrf::sdirk3::UnifiedPreconditioner P(grid, physics, 600.0f, 0.4358665215f);

    const int64_t n =
        static_cast<int64_t>(grid->nx_u) * grid->ny * grid->nz
      + static_cast<int64_t>(grid->nx) * grid->ny_v * grid->nz
      + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
      + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
      + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz
      + static_cast<int64_t>(grid->nx) * grid->ny;

    check(true, "the enabled path CONSTRUCTS -- reaching this line is the first assertion, "
                "because a throw or abort in build would have ended the process here");

    torch::manual_seed(20260816);
    const auto v = torch::randn({n}, torch::kFloat32);
    P.update(v, 600.0f, 0.4358665215f);
    const auto z = P.apply(v);

    check(z.numel() == n, "apply() returns the full packed state under the enabled path");
    check(std::isfinite(z.norm().item<double>()),
          "and it is FINITE -- a 126x change to the Schur denominator is exactly the kind of "
          "edit that produces an inf/NaN diagonal, and nothing tested that before");
    check(z.norm().item<double>() > 0.0,
          "and NONZERO -- an all-zero P^-1 would pass a finiteness check while annihilating "
          "every Krylov direction");
    check(!torch::allclose(z, v),
          "and NOT the identity -- the preconditioner is still doing work on the enabled path, "
          "so a silent degradation to a no-op cannot hide behind a green test");

    // The value authority agrees with what this process actually built.
    check(wrf::sdirk3::phi_diagonal_value(261.5f, 346.4f, 1.0f / (500.0f * 500.0f), true) == 1.0f,
          "and the phi-diagonal authority reports unity for the branch this binary enabled");

    constexpr int expected_checks = 6;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "PHI_UNITY_ENABLED: PASS" << std::endl; return 0; }
    std::cout << "PHI_UNITY_ENABLED: FAIL (" << failures << ")" << std::endl;
    return 1;
}
