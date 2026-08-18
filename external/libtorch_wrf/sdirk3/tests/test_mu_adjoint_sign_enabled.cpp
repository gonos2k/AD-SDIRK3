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
// NOT CALLED "ADJOINT" ANY MORE. Production reduces the staggered gradient and divergence to a
// single positive scalar H_x = 1/dx, so stagger phase, boundary closure, map factors and
// horizontal quadrature are all absent. Opposite scalar signs MIMIC a necessary condition of an
// adjoint pair; they do not establish that the production operators form one. That would take a
// signed, weighted bilinear check on the actual JVP block actions, which does not exist yet.
//
// And the measurement says the product sign is not even the operative quantity: flipping the
// DIVERGENCE leg gives FGMRES rel_error 1 -> 0.9951, while flipping the GRADIENT leg -- same
// product sign -- leaves it at exactly 1. So this is a ROW-ORIENTATION experiment, and the mu
// row is the one that matters.
//
// It proves the branch is reached and the coefficients carry the flipped orientation. It does
// NOT prove the orientation is the correct production default: it also ANTI-COMBINES with the
// phi fix (-> 0.9999), which is why it ships opt-in.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_state_layout.h"
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
    // The coupled path also needs a defined base mass state; without it apply() falls back to a
    // simpler branch and the mu elimination never runs.
    g->mu_base = torch::full({g->ny, g->nx}, 9.0e4f, torch::kFloat32);
    g->mub = g->mu_base.clone();
    return g;
}

}  // namespace

int main() {
    // BEFORE anything constructs a preconditioner -- see the header comment.
    ::setenv("WRF_SDIRK3_MU_SCHUR_ADJOINT_SIGN", "1", /*overwrite=*/1);

    // The coupled 4x4 mu-phi-U-V path -- the machinery whose orientation this experiment is
    // about -- is gated on precond_acoustic_4x4 == 1. Without it apply() takes a simpler path and
    // the elimination never runs, which is why the first version of this contract measured a
    // reduction that was never reached.
    wrf::sdirk3::g_sdirk3_config.precond_acoustic_4x4 = 1;

    std::cout << "=== Mu_Coupling_Orientation_Experiment_Contract ===" << std::endl;

    auto grid = tiny_grid();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    wrf::sdirk3::UnifiedPreconditioner P(grid, physics, 600.0f, 0.4358665215f);

    check(true, "the enabled path CONSTRUCTS -- a throw in build would have ended the process");

    {   // what layout does production expect, vs what this fixture feeds?
        const auto L = wrf::sdirk3::StateLayout::from_grid_dims(
            grid->nx, grid->ny, grid->nz, grid->nx_u, grid->ny_v, grid->nz_w);
        int64_t tot = 0;
        for (const auto& b : L.blocks) {
            std::printf("  layout %s=%lld\n", b.name.c_str(), (long long)b.size);
            tot += b.size;
        }
        std::printf("  layout total=%lld\n", (long long)tot);
    }

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

    // ---- THE REDUCED BLOCK, now actually exercised -----------------------------------------
    // The first version of this contract measured a reduction that never ran: the coupled 4x4
    // path is gated on precond_acoustic_4x4 == 1 AND a defined base mass state, and the fixture
    // supplied neither. Both are set above, so the numbers below come from production's own
    // elimination rather than from re-multiplying a coefficient snapshot in the test.
    {
        const auto rec = P.mu_schur_record();
        std::printf("  mu Schur record: recorded=%d base=%.6g reduced=[%.6g, %.6g]\n",
                    static_cast<int>(rec.recorded), rec.base,
                    rec.reduced_min, rec.reduced_max);
        std::printf("  mu-phi correction: schur_corr=%.6g  S_mu_phi=%.6g\n",
                    rec.schur_corr_mean, rec.s_mu_phi_mean);

        check(rec.recorded,
              "production's mu elimination RAN -- the coupled path is reached, so the assertions "
              "below are about the operator the solver meets");

        // WHICH path produced it. The elimination is spelled out three times (packed 1D, batched
        // 4D, per-column scalar), and the copies have already been caught disagreeing: the scalar
        // one recorded BEFORE its own Schur correction, so it reported the pre-reduction value
        // under the name `reduced`. Pinning the identity means a silent switch to a different
        // path shows up as a failure here rather than as numbers that quietly changed meaning.
        std::printf("  mu Schur path=%d (1=packed1D 2=batched4D 3=scalarFallback)\n", rec.path);
        check(rec.path != 0,
              "the record carries a PATH identity, so its fields are attributable to one of the "
              "three eliminations rather than being ambiguous across them");
        // APPLIED, not merely computed. The scalar fallback can break out of its reduction on a
        // singular diagonal and then DISCARD the reduced system for a decoupled solve -- so a
        // `reduced` number by itself does not mean the operator ever saw it.
        std::printf("  mu Schur applied=%d levels=%d\n",
                    static_cast<int>(rec.reduction_applied), rec.levels_applied);
        check(rec.reduction_applied,
              "the reduction the record describes is the one the solver USES -- not a value it "
              "computed and then discarded for the decoupled fallback");
        check(rec.levels_applied > 0,
              "and levels_applied is SET, not the -1 not-recorded sentinel -- it was left unset "
              "on the packed and 4D paths, so the field was silently absent for two of the three "
              "and a reader could not tell 'covered every level' from 'nobody filled this in'");

        // THE 4D PATH IS NOT EXERCISABLE HERE, and that is the finding.
        //
        // Codex asked for direct regression coverage of the 4D copy. Attempting it shows the 4D
        // entry cannot accept a STAGGERED residual at all. apply() dispatches on rank, so a 4-D
        // input routes there, but the path then reads its level counts per variable
        // (r[2].size(0) as nz_w, r[4].size(0) as nz) from ONE dense tensor whose level dimension
        // is necessarily uniform:
        //
        //     level dim = nz    -> the w/phi slots are one level short (nz_w = nz + 1)
        //     level dim = nz_w  -> coefficient arrays built for nz fail to reshape
        //                          ("shape [6,1,1] is invalid for input of size 5")
        //
        // Both were run. The path is self-consistent only where nz_w == nz, i.e. an unstaggered
        // grid -- and no production caller was found building a 4-D residual; the tile drives
        // apply() with packed 1-D vectors. So the 4D copy's record fields, including the
        // levels_applied line added for it, remain UNVERIFIED by execution.
        //
        // Writing a case that fabricates a shape until it stops throwing would assert something
        // about the fabrication, not about production. The honest coverage statement is this
        // comment plus the check below PINNING path 1: this fixture demonstrably routes to the
        // packed elimination, so if a change ever sends it to the unverified 4D copy instead,
        // that is a silent switch to code no test executes -- and the assertion fails.

        check(rec.path == 1,
              "and it is the PACKED path (1) specifically. An earlier version accepted 1, 2 or 3 "
              "while its comment claimed to catch a switch to the unverified 4D copy -- it "
              "accepted exactly that. Pinning the value this fixture actually takes makes the "
              "claim true: a switch to path 2 or 3, or a fourth copy, fails here.");
        check(std::isfinite(rec.base) && std::isfinite(rec.reduced_min) &&
              std::isfinite(rec.reduced_max),
              "and its outputs are finite under the flipped orientation");
        check(rec.base > 0.0f,
              "BASE diagonal POSITIVE (+640) under the flipped orientation: the U/V elimination "
              "now ADDS the acoustic round trip instead of subtracting it, which is what flipping "
              "the divergence leg was for");
        check(rec.reduced_min < 0.0f,
              "but the REDUCED diagonal is still NEGATIVE (-3987) -- so the mu-phi Schur "
              "correction, not the U/V product, is what drives the mass block indefinite here. "
              "Fixing the coupling orientation alone does not rescue it");
        // WHERE the negativity comes from, pinned as a number rather than inferred.
        check(std::abs(rec.s_mu_phi_mean) > 100.0f,
              "S_mu_phi is LARGE (~336) where the corrected mass equation says it should be ~0 -- "
              "the mu tendency is the horizontal divergence of (mu*u, mu*v) and has no phi "
              "dependence, so M still carries the coupling the OLD Omega = mu*w mass equation had");
        check(rec.schur_corr_mean > rec.base,
              "and its Schur correction (4627) EXCEEDS the base diagonal (+640), which is exactly "
              "how a positive base becomes a negative reduced block -- the U/V product is not the "
              "source of the indefiniteness, this term is");

        check(std::abs(rec.reduced_min) > 1e-20,
              "and it is not sitting on the singular clamp floor, so the sign above is the "
              "reduction's own result");

        // PER-CALL, and now discriminating: a second apply() must re-record rather than inherit.
        const int64_t n2 =
            static_cast<int64_t>(grid->nx_u) * grid->ny * grid->nz
          + static_cast<int64_t>(grid->nx) * grid->ny_v * grid->nz
          + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
          + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz_w
          + static_cast<int64_t>(grid->nx) * grid->ny * grid->nz
          + static_cast<int64_t>(grid->nx) * grid->ny;
        const auto v2 = torch::randn({n2}, torch::kFloat32);
        P.update(v2, 600.0f, 0.4358665215f);
        P.apply(v2);
        check(P.mu_schur_record().recorded,
              "and a SECOND apply() re-records -- the coupled path runs again");

        // THE STALENESS PROBE, and it has to be built to fail. Two recording applies in a row
        // cannot distinguish a reset from a latch (both report recorded=true), which is why the
        // earlier version of this check was inert. Turning the coupled path OFF and applying
        // again separates them: with the reset the record must go back to not-recorded; without
        // it, the previous call's numbers survive and are read as this call's.
        wrf::sdirk3::g_sdirk3_config.precond_acoustic_4x4 = 0;
        P.update(v2, 600.0f, 0.4358665215f);
        P.apply(v2);
        const auto after_offpath = P.mu_schur_record();
        wrf::sdirk3::g_sdirk3_config.precond_acoustic_4x4 = 1;   // restore for anything later
        check(!after_offpath.recorded,
              "STALENESS: after an apply() that does NOT reach the elimination, the record reads "
              "not-recorded -- a latching flag would still report the previous call's numbers");
    }

    constexpr int expected_checks = 21;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "MU_ADJOINT_SIGN_ENABLED: PASS" << std::endl; return 0; }
    std::cout << "MU_ADJOINT_SIGN_ENABLED: FAIL (" << failures << ")" << std::endl;
    return 1;
}
