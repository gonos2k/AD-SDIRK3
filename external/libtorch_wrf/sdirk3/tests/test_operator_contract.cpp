// The right-preconditioner judgement, and the cases that make it a judgement rather than a
// restatement of the code.
//
// The controls matter more than the happy path here. This campaign produced three retracted
// conclusions by reading a raw block summary as an inverse target, so the decisive case is
// COUPLED_EXACT_INVERSE: a 2x2 where A_qq = 1 but (A^-1)_qq = 1/(1-cd). The exact inverse scores
// zero error while the "obvious" diagonal 1/A_qq scores badly -- so the metric rejects exactly
// the reasoning that was wrong.

#include "../wrf_sdirk3_operator_contract.h"

#include <torch/torch.h>

#include <ATen/CPUGeneratorImpl.h>

#include <cmath>
#include <limits>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

constexpr int NX = 5, NY = 4, NZ = 3, NXU = 6, NYV = 5, NZW = 4;

wrf::sdirk3::LinearizationSnapshot make_snapshot() {
    wrf::sdirk3::LinearizationSnapshot s;
    s.layout = wrf::sdirk3::StateLayout::from_grid_dims(NX, NY, NZ, NXU, NYV, NZW);
    s.dt = 600.0;
    s.gamma = 0.4358665215;
    s.ark_stage = 2;
    return s;
}

}  // namespace

int main() {
    std::cout << "=== Operator_Contract ===" << std::endl;

    const auto snap = make_snapshot();
    const auto opts = torch::TensorOptions().dtype(torch::kFloat64);
    const auto proto = torch::zeros({snap.layout.total_size}, opts);

    using wrf::sdirk3::evaluate_directional_right_preconditioner_defect;
    using wrf::sdirk3::block_direction;

    // A FIXED seed on a LOCAL generator. The global RNG made every case measure a different
    // direction each run, so a failure could not be re-run -- and a probe you cannot re-run is
    // not evidence. Local, so this file cannot perturb any other test's stream either.
    auto gen = at::detail::createCPUGenerator(20260806);
    auto rand_in = [&](const std::string& name) {
        for (const auto& b : snap.layout.blocks) {
            if (b.name == name) {
                return block_direction(snap.layout, name, proto,
                                       torch::randn({b.size}, gen, opts));
            }
        }
        throw std::runtime_error("no block " + name);
    };

    // ------------------------------------------------- 1. the exact inverse scores zero
    {
        auto A = [](const torch::Tensor& v) { return 3.0 * v; };
        auto Pinv = [](const torch::Tensor& v) { return v / 3.0; };
        auto r = evaluate_directional_right_preconditioner_defect(snap, A, Pinv, rand_in("ph"));
        check(r.ok, "report is marked ok for a valid direction");
        check(r.global_error < 1e-12, "exact inverse of a scaling gives ~0 error");
    }

    // ---------------------------------------- 2. identity P against a non-identity A is bad
    {
        auto A = [](const torch::Tensor& v) { return 3.0 * v; };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto r = evaluate_directional_right_preconditioner_defect(snap, A, Pinv, rand_in("ph"));
        check(std::abs(r.global_error - 2.0) < 1e-9,
              "P = I against A = 3I gives exactly ||3v - v||/||v|| = 2");
    }

    // -------------- 3. THE CASE THE RETRACTED REASONING GOT WRONG: coupled exact inverse
    // A couples ph and mu. The RAW ph diagonal is 1, so "M's ph gain should be 1/A_qq = 1" --
    // the inference this campaign published. The exact inverse has ph gain 1/(1-cd) instead.
    // The metric must prefer the exact inverse and reject the diagonal.
    {
        const double c = 0.6, d = 0.5;          // 1 - cd = 0.7
        int ph_s = -1, ph_n = 0, mu_s = -1, mu_n = 0;
        for (const auto& b : snap.layout.blocks) {
            if (b.name == "ph") { ph_s = b.start; ph_n = b.size; }
            if (b.name == "mu") { mu_s = b.start; mu_n = b.size; }
        }
        const int n = std::min(ph_n, mu_n);

        // A: ph_i <- ph_i + c*mu_i ; mu_i <- mu_i + d*ph_i   (diagonal of each block is 1)
        auto A = [&](const torch::Tensor& v) {
            auto out = v.clone();
            out.slice(0, ph_s, ph_s + n) += c * v.slice(0, mu_s, mu_s + n);
            out.slice(0, mu_s, mu_s + n) += d * v.slice(0, ph_s, ph_s + n);
            return out;
        };
        // Exact inverse of that 2x2, applied blockwise.
        auto Pinv_exact = [&](const torch::Tensor& v) {
            auto out = v.clone();
            const double det = 1.0 - c * d;
            auto vp = v.slice(0, ph_s, ph_s + n).clone();
            auto vm = v.slice(0, mu_s, mu_s + n).clone();
            out.slice(0, ph_s, ph_s + n).copy_((vp - c * vm) / det);
            out.slice(0, mu_s, mu_s + n).copy_((vm - d * vp) / det);
            return out;
        };
        // The retracted reasoning: raw A_qq = 1, so use a diagonal of 1, i.e. P^-1 = I.
        auto Pinv_diag = [](const torch::Tensor& v) { return v; };

        auto dir = rand_in("ph");
        auto r_exact = evaluate_directional_right_preconditioner_defect(snap, A, Pinv_exact, dir);
        auto r_diag  = evaluate_directional_right_preconditioner_defect(snap, A, Pinv_diag,  dir);

        check(r_exact.global_error < 1e-12,
              "coupled: the EXACT inverse scores ~0 even though its ph gain is 1/(1-cd), not 1");
        check(r_diag.global_error > 0.1,
              "coupled: the 1/A_qq diagonal scores badly -- the retracted inference is rejected");
        check(r_diag.global_error > r_exact.global_error * 1e6,
              "the metric separates them by orders, so it cannot be read either way");

        // in_block_error <= global_error is true for ANY norm decomposition, so comparing them
        // proves nothing. Assert the SHARE instead: under the diagonal the ph input's error is
        // mostly in mu, so the in-block part is a small fraction of the total.
        check(r_diag.in_block_error(snap.layout) < 0.5 * r_diag.global_error,
              "the diagonal's error is mostly OUTSIDE ph -- the cross-block model is what failed");
    }

    // ------------------------------------------ 4. it reports WHERE the error lands
    {
        int mu_s = -1, mu_n = 0;
        for (const auto& b : snap.layout.blocks) if (b.name == "mu") { mu_s = b.start; mu_n = b.size; }
        // A leaves everything alone except it writes ph input into mu.
        int ph_s = -1, ph_n = 0;
        for (const auto& b : snap.layout.blocks) if (b.name == "ph") { ph_s = b.start; ph_n = b.size; }
        auto A = [&](const torch::Tensor& v) {
            auto out = v.clone();
            out.slice(0, mu_s, mu_s + mu_n) += 2.0 * v.slice(0, ph_s, ph_s + mu_n);
            return out;
        };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto r = evaluate_directional_right_preconditioner_defect(snap, A, Pinv, rand_in("ph"));
        double mu_err = 0.0;
        for (std::size_t i = 0; i < snap.layout.blocks.size(); ++i) {
            if (snap.layout.blocks[i].name == "mu") mu_err = r.output_block_error[i];
        }
        check(mu_err > 0.1, "leakage into mu is reported in mu's slot");
        check(r.in_block_error(snap.layout) < 1e-12,
              "ph itself is clean here, so in-block and leakage are separable");

        // The check above is 0 BY CONSTRUCTION -- A never touches ph -- so it would also pass if
        // in_block_error always returned 0. Pin a case where it must return a KNOWN NONZERO
        // value: scale ph by 3 with P = I, so the whole error is in ph at exactly ratio 2.
        auto A_ph_only = [&](const torch::Tensor& v) {
            auto out = v.clone();
            out.slice(0, ph_s, ph_s + ph_n) *= 3.0;   // the WHOLE ph block, not mu_n of it
            return out;
        };
        auto r2 = evaluate_directional_right_preconditioner_defect(snap, A_ph_only, Pinv, rand_in("ph"));
        check(std::abs(r2.in_block_error(snap.layout) - 2.0) < 1e-9,
              "in_block_error returns the INPUT block's own value (exactly 2 here), not 0");
        check(std::abs(r2.in_block_error(snap.layout) - r2.global_error) < 1e-9,
              "with the error confined to ph, in-block equals global");

        // And it must select the named block, not simply the first or the largest: same operator,
        // a mu direction, which A leaves untouched -> in-block is 0 while global is 0 too.
        auto r3 = evaluate_directional_right_preconditioner_defect(snap, A_ph_only, Pinv, rand_in("mu"));
        check(r3.ok && r3.global_error < 1e-12 && r3.in_block_error(snap.layout) < 1e-12,
              "a mu direction through a ph-only operator is clean in both");
    }

    // --- 4b. an unattributable direction must REFUSE, not return 0
    // This is the case that was missing: evaluate_directional_right_preconditioner_defect never set input_block, so
    // in_block_error returned 0.0 for every report and every attribution assertion passed
    // vacuously. A multi-block direction has no single input block, and saying so is the only
    // honest answer.
    {
        auto A = [](const torch::Tensor& v) { return 3.0 * v; };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto two_blocks = rand_in("ph") + rand_in("mu");
        auto r = evaluate_directional_right_preconditioner_defect(snap, A, Pinv, two_blocks);
        check(r.ok && r.input_block.empty(),
              "a two-block direction is scored but has NO input block");
        bool threw = false;
        try { (void)r.in_block_error(snap.layout); }
        catch (const std::exception&) { threw = true; }
        check(threw, "in_block_error THROWS when there is nothing to attribute to");

        auto single = evaluate_directional_right_preconditioner_defect(snap, A, Pinv, rand_in("t"));
        check(single.input_block == "t", "a single-block direction names its block");
    }

    // --------------------------------------- 5. invalid input is refused, not scored
    {
        auto A = [](const torch::Tensor& v) { return v; };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto wrong = torch::randn({snap.layout.total_size - 1}, opts);
        check(!evaluate_directional_right_preconditioner_defect(snap, A, Pinv, wrong).ok,
              "a mis-sized direction returns ok=false rather than a number");

        wrf::sdirk3::LinearizationSnapshot bad;   // dt = gamma = 0
        check(!bad.is_valid(), "a default snapshot is invalid");
        check(!evaluate_directional_right_preconditioner_defect(bad, A, Pinv, rand_in("ph")).ok,
              "an invalid snapshot returns ok=false");
    }

    // ------------------------------------------------- 6. the active domain is explicit
    {
        const auto hevi = wrf::sdirk3::ImplicitActiveDomain::hevi_vertical_fast();
        check(hevi.rw && hevi.ph && hevi.theta, "HEVI vertical-fast owns rw, ph, theta");
        check(!hevi.ru && !hevi.rv && !hevi.mu,
              "HEVI vertical-fast leaves ru, rv and mu to the explicit side");
        check(hevi.by_name("rw") && !hevi.by_name("mu"),
              "by_name agrees with the fields it reads");

        bool threw = false;
        try { hevi.by_name("omega"); } catch (const std::exception&) { threw = true; }
        check(threw, "an unknown block name THROWS rather than defaulting to inactive");

        const wrf::sdirk3::ImplicitActiveDomain all;
        check(all.ru && all.rv && all.rw && all.ph && all.theta && all.mu,
              "the default is every block active (no silent narrowing)");
    }

    // ---------------- 5b. a broken OPERATOR is refused too, not scored (the broadcast trap)
    // `A P^-1 v - v` broadcasts. A wrong-shaped return does not throw -- it produces a plausible
    // number from an operator that is not doing what it claims. Same for a NaN or a dtype change.
    {
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto dir = rand_in("ph");

        auto A_wrong_shape = [](const torch::Tensor&) { return torch::ones({1}, torch::kFloat64); };
        auto r_shape = evaluate_directional_right_preconditioner_defect(snap, A_wrong_shape, Pinv, dir);
        check(!r_shape.ok, "an operator returning the WRONG SHAPE is refused, not broadcast");

        auto A_nan = [](const torch::Tensor& v) {
            auto out = v.clone();
            out.index_put_({0}, std::numeric_limits<double>::quiet_NaN());
            return out;
        };
        check(!evaluate_directional_right_preconditioner_defect(snap, A_nan, Pinv, dir).ok,
              "a NON-FINITE operator result is refused");

        auto A_wrong_dtype = [](const torch::Tensor& v) { return v.to(torch::kFloat32); };
        check(!evaluate_directional_right_preconditioner_defect(snap, A_wrong_dtype, Pinv, dir).ok,
              "a DTYPE change is refused (silent precision loss is not a measurement)");

        auto Pinv_wrong = [](const torch::Tensor&) { return torch::ones({3}, torch::kFloat64); };
        auto A_ok = [](const torch::Tensor& v) { return v; };
        check(!evaluate_directional_right_preconditioner_defect(snap, A_ok, Pinv_wrong, dir).ok,
              "the PRECONDITIONER's output is validated too, not just A's");
    }

    // ------------------- 6b. active blocks vs leakage into blocks nothing is solving
    // A defect inside the implicit domain is a wrong inverse. A defect OUTSIDE it is M polluting
    // a channel the explicit side integrates -- a different failure with a different fix, and a
    // scalar global error cannot tell them apart.
    {
        int ph_s = -1, mu_s = -1, mu_n = 0;
        for (const auto& b : snap.layout.blocks) {
            if (b.name == "ph") ph_s = b.start;
            if (b.name == "mu") { mu_s = b.start; mu_n = b.size; }
        }
        // A writes ph input into mu -- under HEVI, mu is EXPLICIT, so this is pure leakage.
        auto A_into_mu = [&](const torch::Tensor& v) {
            auto out = v.clone();
            out.slice(0, mu_s, mu_s + mu_n) += 2.0 * v.slice(0, ph_s, ph_s + mu_n);
            return out;
        };
        auto Pinv = [](const torch::Tensor& v) { return v; };

        // ONE direction, scored under both domains. Two draws would compare two different
        // vectors, and the re-attribution claim below would be untestable.
        const auto dir = rand_in("ph");

        auto all_active = snap;      // default domain: every block implicit
        auto r_all = evaluate_directional_right_preconditioner_defect(all_active, A_into_mu, Pinv, dir);
        check(r_all.inactive_leakage < 1e-12 && r_all.active_error > 0.1,
              "with every block active, the whole defect is ACTIVE and nothing leaks");

        auto hevi = snap;
        hevi.active = wrf::sdirk3::ImplicitActiveDomain::hevi_vertical_fast();   // mu explicit
        auto r_hevi = evaluate_directional_right_preconditioner_defect(hevi, A_into_mu, Pinv, dir);
        check(r_hevi.active_error < 1e-12 && r_hevi.inactive_leakage > 0.1,
              "under HEVI the SAME defect is LEAKAGE -- the domain changes the diagnosis");
        check(std::abs(r_hevi.global_error - r_all.global_error) < 1e-12,
              "the global error is identical: the domain re-attributes, it does not rescale");

        // The blocks partition the vector, so this identity is exact. If a block were dropped
        // from the partition the two parts would no longer reconstruct the whole.
        for (const auto* r : {&r_all, &r_hevi}) {
            check(std::abs(std::hypot(r->active_error, r->inactive_leakage) - r->global_error) < 1e-12,
                  "active^2 + leakage^2 == global^2 -- the partition is exhaustive");
        }
    }

    // --------------------------------------------- 7. h is dt*gamma, stated once
    {
        check(std::abs(snap.h() - 600.0 * 0.4358665215) < 1e-12,
              "h = dt * gamma comes from the snapshot, not a local recomputation");
    }

    constexpr int expected_checks = 33;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "OPERATOR_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "OPERATOR_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
