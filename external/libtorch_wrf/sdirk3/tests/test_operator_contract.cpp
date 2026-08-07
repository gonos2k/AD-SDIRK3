// The right-preconditioner judgement, and the cases that make it a judgement rather than a
// restatement of the code.
//
// The controls matter more than the happy path here. This campaign produced three retracted
// conclusions by reading a raw block summary as an inverse target, so the decisive case is
// COUPLED_EXACT_INVERSE: a 2x2 where A_qq = 1 but (A^-1)_qq = 1/(1-cd). The exact inverse scores
// zero error while the "obvious" diagonal 1/A_qq scores badly -- so the metric rejects exactly
// the reasoning that was wrong.

#include "../wrf_sdirk3_operator_contract.h"
#include "../wrf_sdirk3_wrms_norm.h"

#include <torch/torch.h>

#include <ATen/CPUGeneratorImpl.h>

#include <cmath>
#include <limits>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
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
    // Every field DISTINCT and nonzero on purpose. Shared or default values would let a
    // field-mismatch test pass by coincidence rather than by the check.
    s.token.solver_id = 3;
    s.token.stage_state_generation = 5;
    s.token.coefficient_generation = 7;
    s.token.rhs_generation = 11;
    s.token.scale_generation = 13;
    s.token.physical_step = 2;
    s.token.ark_stage = 2;
    s.token.newton_iteration = 1;
    s.token.mass_coordinate_mode = 1;
    s.token.imex_split_mode = 3;
    s.token.hevi_split = false;
    s.scale = wrf::sdirk3::ResidualScale::unscaled(s.token.scale_generation);
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

    // Each operator is bound at the generation of the snapshot it is scored against -- including
    // the invalid-snapshot case, which must fail for INVALIDITY, not for a receipt mismatch it
    // would otherwise trip first.
    // These fixtures are genuinely stateless, so they DECLARE it. Supplying neither a digest nor
    // a declaration is refused -- see case 6j.
    auto bA = [](const wrf::sdirk3::LinearizationSnapshot& sn, wrf::sdirk3::LinearOperator f) {
        return wrf::sdirk3::BoundLinearOperator{std::move(f), sn.token, {}, true};
    };
    auto bP = [](const wrf::sdirk3::LinearizationSnapshot& sn, wrf::sdirk3::LinearOperator f) {
        return wrf::sdirk3::BoundLinearOperator{std::move(f), sn.token, {}, true};
    };

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
        auto r = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv), rand_in("ph"));
        check(r.ok, "report is marked ok for a valid direction");
        check(r.global_error < 1e-12, "exact inverse of a scaling gives ~0 error");
    }

    // ---------------------------------------- 2. identity P against a non-identity A is bad
    {
        auto A = [](const torch::Tensor& v) { return 3.0 * v; };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto r = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv), rand_in("ph"));
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
        auto r_exact = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv_exact), dir);
        auto r_diag  = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv_diag),  dir);

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
        auto r = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv), rand_in("ph"));
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
        auto r2 = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A_ph_only), bP(snap, Pinv), rand_in("ph"));
        check(std::abs(r2.in_block_error(snap.layout) - 2.0) < 1e-9,
              "in_block_error returns the INPUT block's own value (exactly 2 here), not 0");
        check(std::abs(r2.in_block_error(snap.layout) - r2.global_error) < 1e-9,
              "with the error confined to ph, in-block equals global");

        // And it must select the named block, not simply the first or the largest: same operator,
        // a mu direction, which A leaves untouched -> in-block is 0 while global is 0 too.
        auto r3 = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A_ph_only), bP(snap, Pinv), rand_in("mu"));
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
        auto r = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv), two_blocks);
        check(r.ok && r.input_block.empty(),
              "a two-block direction is scored but has NO input block");
        bool threw = false;
        try { (void)r.in_block_error(snap.layout); }
        catch (const std::exception&) { threw = true; }
        check(threw, "in_block_error THROWS when there is nothing to attribute to");

        auto single = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv), rand_in("t"));
        check(single.input_block == "t", "a single-block direction names its block");
    }

    // --------------------------------------- 5. invalid input is refused, not scored
    {
        auto A = [](const torch::Tensor& v) { return v; };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto wrong = torch::randn({snap.layout.total_size - 1}, opts);
        check(!evaluate_directional_right_preconditioner_defect(snap, bA(snap, A), bP(snap, Pinv), wrong).ok,
              "a mis-sized direction returns ok=false rather than a number");

        wrf::sdirk3::LinearizationSnapshot bad;   // dt = gamma = 0
        check(!bad.is_valid(), "a default snapshot is invalid");
        check(!evaluate_directional_right_preconditioner_defect(bad, bA(bad, A), bP(bad, Pinv), rand_in("ph")).ok,
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
        auto r_shape = evaluate_directional_right_preconditioner_defect(snap, bA(snap, A_wrong_shape), bP(snap, Pinv), dir);
        check(!r_shape.ok, "an operator returning the WRONG SHAPE is refused, not broadcast");

        auto A_nan = [](const torch::Tensor& v) {
            auto out = v.clone();
            out.index_put_({0}, std::numeric_limits<double>::quiet_NaN());
            return out;
        };
        check(!evaluate_directional_right_preconditioner_defect(snap, bA(snap, A_nan), bP(snap, Pinv), dir).ok,
              "a NON-FINITE operator result is refused");

        auto A_wrong_dtype = [](const torch::Tensor& v) { return v.to(torch::kFloat32); };
        check(!evaluate_directional_right_preconditioner_defect(snap, bA(snap, A_wrong_dtype), bP(snap, Pinv), dir).ok,
              "a DTYPE change is refused (silent precision loss is not a measurement)");

        auto Pinv_wrong = [](const torch::Tensor&) { return torch::ones({3}, torch::kFloat64); };
        auto A_ok = [](const torch::Tensor& v) { return v; };
        check(!evaluate_directional_right_preconditioner_defect(snap, bA(snap, A_ok), bP(snap, Pinv_wrong), dir).ok,
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
        auto r_all = evaluate_directional_right_preconditioner_defect(all_active, bA(all_active, A_into_mu), bP(all_active, Pinv), dir);
        check(r_all.inactive_output_defect < 1e-12 && r_all.active_error > 0.1,
              "with every block active, the whole defect is ACTIVE and nothing leaks");

        auto hevi = snap;
        hevi.active = wrf::sdirk3::ImplicitActiveDomain::hevi_vertical_fast();   // mu explicit
        auto r_hevi = evaluate_directional_right_preconditioner_defect(hevi, bA(hevi, A_into_mu), bP(hevi, Pinv), dir);
        check(r_hevi.active_error < 1e-12 && r_hevi.inactive_output_defect > 0.1,
              "under HEVI the SAME defect is LEAKAGE -- the domain changes the diagnosis");
        check(std::abs(r_hevi.global_error - r_all.global_error) < 1e-12,
              "the global error is identical: the domain re-attributes, it does not rescale");

        // The blocks partition the vector, so this identity is exact. If a block were dropped
        // from the partition the two parts would no longer reconstruct the whole.
        for (const auto* r : {&r_all, &r_hevi}) {
            check(std::abs(std::hypot(r->active_error, r->inactive_output_defect) - r->global_error) < 1e-12,
                  "active^2 + leakage^2 == global^2 -- the partition is exhaustive");
        }
    }

    // ---------- 6c. ONE token: A, P^-1 and the scale must come from the SAME linearization
    // Two independent counters could not tell "the same linearization" from "two solvers that
    // both happen to be at 7". Every field below has a real producer -- the two generations come
    // from UnifiedPreconditioner::StageBindingReceipt, which already documents why they must stay
    // distinct; the modes from SDIRK3Config; the stage counters from the Newton driver.
    {
        wrf::sdirk3::LinearOperator A = [](const torch::Tensor& v) { return 3.0 * v; };
        wrf::sdirk3::LinearOperator Pinv = [](const torch::Tensor& v) { return v / 3.0; };
        const auto dir = rand_in("ph");
        using wrf::sdirk3::BoundLinearOperator;
        using wrf::sdirk3::LinearizationToken;

        // Control FIRST: correctly bound, this scores. Without it every refusal below could be a
        // blanket rejection reading as a pass.
        check(evaluate_directional_right_preconditioner_defect(
                  snap, bA(snap, A), bP(snap, Pinv), dir).ok,
              "operators sharing the snapshot's token are accepted (refusals are not blanket)");

        // EVERY field is load-bearing. Perturb one at a time; each must be refused on its own.
        const std::vector<std::pair<const char*, std::function<void(LinearizationToken&)>>> bump = {
            {"solver_id",              [](LinearizationToken& t){ ++t.solver_id; }},
            {"stage_state_generation", [](LinearizationToken& t){ ++t.stage_state_generation; }},
            {"coefficient_generation", [](LinearizationToken& t){ ++t.coefficient_generation; }},
            {"rhs_generation",         [](LinearizationToken& t){ ++t.rhs_generation; }},
            {"scale_generation",       [](LinearizationToken& t){ ++t.scale_generation; }},
            {"physical_step",          [](LinearizationToken& t){ ++t.physical_step; }},
            {"ark_stage",              [](LinearizationToken& t){ ++t.ark_stage; }},
            {"newton_iteration",       [](LinearizationToken& t){ ++t.newton_iteration; }},
            {"mass_coordinate_mode",   [](LinearizationToken& t){ ++t.mass_coordinate_mode; }},
            {"imex_split_mode",        [](LinearizationToken& t){ ++t.imex_split_mode; }},
            {"hevi_split",             [](LinearizationToken& t){ t.hevi_split = !t.hevi_split; }},
        };
        bool every_field_bites = true;
        for (const auto& kv : bump) {
            auto drifted = snap.token;
            kv.second(drifted);
            const bool a_refused = !evaluate_directional_right_preconditioner_defect(
                snap, BoundLinearOperator{A, drifted, {}, true}, bP(snap, Pinv), dir).ok;
            const bool p_refused = !evaluate_directional_right_preconditioner_defect(
                snap, bA(snap, A), BoundLinearOperator{Pinv, drifted, {}, true}, dir).ok;
            if (!a_refused || !p_refused) {
                std::cout << "    field with no teeth: " << kv.first << std::endl;
                every_field_bites = false;
            }
        }
        check(every_field_bites,
              "EVERY token field is load-bearing -- perturbing any one, on either operator, is refused");

        // A scale frozen at a different stage cannot weight this defect.
        {
            auto mixed = snap;
            mixed.scale = wrf::sdirk3::ResidualScale::unscaled(snap.token.scale_generation + 1);
            check(!mixed.is_valid(),
                  "a scale from a DIFFERENT generation makes the snapshot invalid");
        }

        // An unset token is the state a caller who supplied nothing leaves behind.
        check(!LinearizationToken{}.is_valid(),
              "a default-constructed token is INVALID (zero and -1 are what 'unset' looks like)");

        check(!evaluate_directional_right_preconditioner_defect(
                  snap, BoundLinearOperator{}, bP(snap, Pinv), dir).ok,
              "an EMPTY operator is refused rather than called");
    }

    // ------- 6d. the defect belongs to the OPERATOR, not to the units the state is stored in
    // Change mu's units by c (Pa -> hPa is c = 100). Nothing physical moves: the stored value
    // becomes D^-1 v and the operator becomes D^-1 A D. Under the scale S~ = D^-1 S the D's
    // cancel exactly, so the scaled defect must be IDENTICAL -- and under the unscaled S it must
    // NOT be, or the invariance below would be a property of the operator rather than of S.
    {
        const double c = 100.0;
        int mu_i = -1, ph_s = -1, mu_s = -1, n = 0;
        for (std::size_t i = 0; i < snap.layout.blocks.size(); ++i) {
            const auto& b = snap.layout.blocks[i];
            if (b.name == "ph") ph_s = b.start;
            if (b.name == "mu") { mu_i = static_cast<int>(i); mu_s = b.start; n = b.size; }
        }

        auto D = torch::ones({snap.layout.total_size}, opts);
        D.slice(0, mu_s, mu_s + n).fill_(c);
        auto Dinv = 1.0 / D;

        // ph and mu are COUPLED, so the unit change is actually felt. A block-diagonal operator
        // would come out invariant for reasons that have nothing to do with the scale.
        wrf::sdirk3::LinearOperator A = [&](const torch::Tensor& v) {
            auto out = v.clone();
            out.slice(0, mu_s, mu_s + n) += 0.50 * v.slice(0, ph_s, ph_s + n);
            out.slice(0, ph_s, ph_s + n) += 0.25 * v.slice(0, mu_s, mu_s + n);
            return out;
        };
        wrf::sdirk3::LinearOperator Pinv = [](const torch::Tensor& v) { return v; };

        // The same operator and direction, expressed in the new units.
        wrf::sdirk3::LinearOperator A_u = [&](const torch::Tensor& v) { return Dinv * A(D * v); };
        wrf::sdirk3::LinearOperator Pinv_u = [&](const torch::Tensor& v) { return Dinv * Pinv(D * v); };
        const auto dir = rand_in("ph");
        const auto dir_u = Dinv * dir;

        auto snap_u = snap;
        snap_u.scale.block_scale[mu_i] = 1.0 / c;      // S~ = D^-1 S

        const double base = evaluate_directional_right_preconditioner_defect(
                                snap, bA(snap, A), bP(snap, Pinv), dir).global_error;
        const double rescaled = evaluate_directional_right_preconditioner_defect(
                                snap_u, bA(snap_u, A_u), bP(snap_u, Pinv_u), dir_u).global_error;
        check(base > 1e-6, "the baseline defect is nonzero, so invariance is not invariance of 0");
        check(std::abs(rescaled - base) < 1e-12,
              "a UNIT CHANGE leaves the scaled defect identical -- it measures the operator");

        // The control. Same unit change, scale left unscaled: the number MUST move, or the check
        // above proves nothing about S.
        const double unscaled_after = evaluate_directional_right_preconditioner_defect(
                                          snap, bA(snap, A_u), bP(snap, Pinv_u), dir_u).global_error;
        check(std::abs(unscaled_after - base) > 1e-3,
              "with an UNSCALED S the same unit change moves the number -- the scale is load-bearing");
    }

    // ---------------------------- 6e. an unusable scale is refused, not silently ignored
    {
        wrf::sdirk3::LinearOperator A = [](const torch::Tensor& v) { return v; };
        wrf::sdirk3::LinearOperator Pinv = [](const torch::Tensor& v) { return v; };
        check(wrf::sdirk3::ResidualScale::unscaled(13).is_valid(), "the unscaled S is valid");
        check(!wrf::sdirk3::ResidualScale{}.is_valid(),
              "a DEFAULT-constructed scale is INVALID -- forgetting to set one is refused, which "
              "is what separates it from deliberately measuring unscaled");
        for (double bad_s : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
            auto s_bad = snap;
            s_bad.scale.block_scale[0] = bad_s;
            check(!s_bad.is_valid() &&
                  !evaluate_directional_right_preconditioner_defect(
                       s_bad, bA(s_bad, A), bP(s_bad, Pinv), rand_in("ph")).ok,
                  "a non-positive or non-finite scale makes the snapshot invalid and is refused");
        }
    }

    // ------------------------------ 6f. a receipt of zero is what "no receipt" leaves behind
    // 0 == 0 compares equal, so the generation check passed for exactly the callers who supplied
    // no generation at all. is_valid() now refuses them.
    {
        wrf::sdirk3::LinearOperator A = [](const torch::Tensor& v) { return v; };
        wrf::sdirk3::LinearOperator Pinv = [](const torch::Tensor& v) { return v; };
        for (int which = 0; which < 2; ++which) {
            auto s0 = snap;
            (which == 0 ? s0.token.rhs_generation : s0.token.coefficient_generation) = 0;
            check(!s0.is_valid() &&
                  !evaluate_directional_right_preconditioner_defect(
                       s0, bA(s0, A), bP(s0, Pinv), rand_in("ph")).ok,
                  "a ZERO generation inside the token is refused (matching operators cannot rescue it)");
        }

        // inf passes `> 0.0`, so the old dt/gamma guard admitted it.
        for (int which = 0; which < 2; ++which) {
            auto sinf = snap;
            (which == 0 ? sinf.dt : sinf.gamma) = std::numeric_limits<double>::infinity();
            check(!sinf.is_valid(), "a NON-FINITE dt or gamma is refused (inf is positive)");
        }
    }

    // ------------------------- 6g. the direction's shape and dtype, not just its element count
    {
        wrf::sdirk3::LinearOperator A = [](const torch::Tensor& v) { return v; };
        wrf::sdirk3::LinearOperator Pinv = [](const torch::Tensor& v) { return v; };

        // [1, N] has the right numel; its block slices along dim 0 would be EMPTY.
        auto two_d = rand_in("ph").reshape({1, snap.layout.total_size});
        check(!evaluate_directional_right_preconditioner_defect(
                   snap, bA(snap, A), bP(snap, Pinv), two_d).ok,
              "a [1, N] direction is refused -- numel alone does not make it packed");

        auto ints = torch::zeros({snap.layout.total_size}, torch::kLong);
        check(!evaluate_directional_right_preconditioner_defect(
                   snap, bA(snap, A), bP(snap, Pinv), ints).ok,
              "a NON-FLOATING direction is refused");
    }

    // --------------- 6h. the direction must live in the domain the implicit solve owns
    {
        wrf::sdirk3::LinearOperator A = [](const torch::Tensor& v) { return v; };
        wrf::sdirk3::LinearOperator Pinv = [](const torch::Tensor& v) { return v; };
        auto hevi = snap;
        hevi.active = wrf::sdirk3::ImplicitActiveDomain::hevi_vertical_fast();   // mu explicit

        const auto mu_dir = rand_in("mu");
        check(!evaluate_directional_right_preconditioner_defect(
                   hevi, bA(hevi, A), bP(hevi, Pinv), mu_dir).ok,
              "under HEVI a mu direction is REFUSED -- the solve never visits it");
        // Control: the identical direction under the full domain is a legitimate question.
        check(evaluate_directional_right_preconditioner_defect(
                  snap, bA(snap, A), bP(snap, Pinv), mu_dir).ok,
              "the SAME direction is accepted when mu is active -- the domain is what refuses it");
    }

    // ------------------------------- 6i. purity: the probe must not change what it measures
    // These arrive as std::function. `const Tensor&` binds the HANDLE, not the storage, so
    // v.add_() compiles -- which is exactly why this needs checking rather than trusting.
    {
        wrf::sdirk3::LinearOperator ident = [](const torch::Tensor& v) { return v.clone(); };
        wrf::sdirk3::LinearOperator writes_through = [](const torch::Tensor& v) {
            v.add_(1.0);           // compiles on a const Tensor& -- the handle is const, not the data
            return v.clone();
        };
        check(!evaluate_directional_right_preconditioner_defect(
                   snap, bA(snap, ident), bP(snap, writes_through), rand_in("ph")).ok,
              "a preconditioner that WRITES THROUGH its input is refused");
        check(!evaluate_directional_right_preconditioner_defect(
                   snap, bA(snap, writes_through), bP(snap, ident), rand_in("ph")).ok,
              "an operator A that writes through its input is refused too");

        // A fallback latch / refinement counter / lazy allocation answers differently the second
        // time. A number that cannot be reproduced is not a measurement.
        auto calls = std::make_shared<int>(0);
        wrf::sdirk3::LinearOperator stateful = [calls](const torch::Tensor& v) {
            return v * static_cast<double>(++(*calls));
        };
        check(!evaluate_directional_right_preconditioner_defect(
                   snap, bA(snap, ident), bP(snap, stateful), rand_in("ph")).ok,
              "a STATEFUL operator is refused -- the second call disagrees with the first");
        check(evaluate_directional_right_preconditioner_defect(
                  snap, bA(snap, ident), bP(snap, ident), rand_in("ph")).ok,
              "a pure, repeatable pair is still accepted (the refusals are not blanket)");
    }

    // ------------- 6j. the mutation that returns the SAME answer -- what repeatability misses
    // Measured before this case existed: an operator that increments a counter and returns a
    // clone was ACCEPTED with ok=true while its counter went 0 -> 2. Write-through and
    // repeatability both pass, because neither asks whether the OPERATOR changed.
    {
        using wrf::sdirk3::BoundLinearOperator;
        auto calls = std::make_shared<int>(0);
        wrf::sdirk3::LinearOperator hidden = [calls](const torch::Tensor& v) {
            ++(*calls);              // internal state moves
            return v.clone();        // output is identical every time
        };
        wrf::sdirk3::LinearOperator ident = [](const torch::Tensor& v) { return v.clone(); };
        auto digest = [calls]() { return static_cast<uint64_t>(*calls); };
        auto zero_digest = []() { return uint64_t{0}; };

        const auto dir = rand_in("ph");

        // WITH a digest the mutation is caught.
        check(!evaluate_directional_right_preconditioner_defect(
                   snap,
                   BoundLinearOperator{ident, snap.token, zero_digest, false},
                   BoundLinearOperator{hidden, snap.token, digest, false},
                   dir).ok,
              "an operator that CHANGES ITSELF while returning identical output is refused");

        // The same mutant that only DECLARES itself stateless is accepted -- and the report says
        // the purity was not measured, so no reader can mistake the two.
        *calls = 0;
        auto declared = evaluate_directional_right_preconditioner_defect(
            snap,
            BoundLinearOperator{ident, snap.token, {}, true},
            BoundLinearOperator{hidden, snap.token, {}, true},
            dir);
        check(declared.ok && !declared.state_digest_unchanged,
              "a DECLARED-stateless operator is scored, but state_digest_unchanged is FALSE");

        // A genuinely pure pair: nothing to move, so the digest does not move.
        auto verified = evaluate_directional_right_preconditioner_defect(
            snap,
            BoundLinearOperator{ident, snap.token, zero_digest, false},
            BoundLinearOperator{ident, snap.token, zero_digest, false},
            dir);
        check(verified.ok && verified.state_digest_unchanged,
              "two digest-carrying pure operators report state_digest_unchanged TRUE");

        // THE LIMIT, pinned so nobody reads the flag as proof of purity. A digest that ignores
        // the operator never moves, so a mutating operator paired with one is ACCEPTED with the
        // flag set. From inside the evaluator that is indistinguishable from a faithful digest
        // whose state did not change -- which is exactly why the field is named for the
        // observation ("digest unchanged") and not for the conclusion ("purity verified").
        *calls = 0;
        auto blind_digest = []() { return uint64_t{42}; };   // watches nothing
        auto blind = evaluate_directional_right_preconditioner_defect(
            snap,
            BoundLinearOperator{ident, snap.token, blind_digest, false},
            BoundLinearOperator{hidden, snap.token, blind_digest, false},
            dir);
        check(blind.ok && blind.state_digest_unchanged && *calls > 0,
              "a BLIND digest lets a mutating operator through with the flag set -- the flag "
              "reports the digest, not purity");

        // State that advances and UNWINDS returns to its initial value, so a before/after pair
        // sees nothing. Sampling after every application is what catches it.
        auto toggle = std::make_shared<int>(0);
        wrf::sdirk3::LinearOperator toggling = [toggle](const torch::Tensor& v) {
            *toggle = 1 - *toggle;      // 0 -> 1 -> 0 across the two applications
            return v.clone();
        };
        auto toggle_digest = [toggle]() { return static_cast<uint64_t>(*toggle); };
        check(!evaluate_directional_right_preconditioner_defect(
                   snap,
                   BoundLinearOperator{ident, snap.token, zero_digest, false},
                   BoundLinearOperator{toggling, snap.token, toggle_digest, false},
                   dir).ok,
              "state that CHANGES AND UNWINDS is caught -- a before/after pair would miss it");

        // Neither digest nor declaration: silence must not read as verified.
        check(!evaluate_directional_right_preconditioner_defect(
                   snap,
                   BoundLinearOperator{ident, snap.token},
                   BoundLinearOperator{ident, snap.token},
                   dir).ok,
              "an operator supplying NEITHER a digest nor a declaration is refused");
    }

    // ------ 6k. the contract and the PRODUCTION stage gate must mean the same thing by "small"
    // Six block scalars cannot express the weighting production actually uses: ewt is
    // rtol*|y_ref| + atol POINTWISE, so it varies within a block by orders across a profile. A
    // defect the contract called small could be large in the metric that decides the step.
    //
    // Under the frozen error weights the two are not merely close, they are EQUAL:
    //     wrms_norm(x) = ||x/ewt||_2 / sqrt(N)
    //     global_error = ||err/ewt||_2 / ||v/ewt||_2   -> the sqrt(N) cancels
    // so this fails the moment either formula drifts from the other.
    {
        wrf::sdirk3::PackedBlockSizes pb;
        int64_t* slot[6] = {&pb.u, &pb.v, &pb.w, &pb.ph, &pb.t, &pb.mu};
        for (std::size_t i = 0; i < snap.layout.blocks.size() && i < 6; ++i) {
            *slot[i] = snap.layout.blocks[i].size;
        }
        const wrf::sdirk3::WRMSNormConfig cfg;

        // A reference state with a WIDE dynamic range, so ewt genuinely varies inside each block.
        // A uniform y_ref would make block scalars accidentally sufficient and prove nothing.
        const auto y_ref = torch::exp(6.0 * torch::randn({snap.layout.total_size}, gen, opts));
        const auto ewt = wrf::sdirk3::error_weights_packed(y_ref, pb, cfg);
        check((ewt.max() / ewt.min()).item<double>() > 100.0,
              "the fixture's error weights span orders -- block scalars could not represent them");

        auto weighted = snap;
        weighted.scale = wrf::sdirk3::ResidualScale::from_error_weights(
            ewt, snap.token.scale_generation);
        check(weighted.is_valid(), "a frozen error-weight scale is a valid scale");

        // A couples ph into mu, so the defect is not confined to the input block.
        int ph_s = -1, mu_s = -1, n = 0;
        for (const auto& b : snap.layout.blocks) {
            if (b.name == "ph") ph_s = b.start;
            if (b.name == "mu") { mu_s = b.start; n = b.size; }
        }
        wrf::sdirk3::LinearOperator A = [&](const torch::Tensor& x) {
            auto out = x.clone();
            out.slice(0, mu_s, mu_s + n) += 0.5 * x.slice(0, ph_s, ph_s + n);
            return out;
        };
        wrf::sdirk3::LinearOperator Pinv = [](const torch::Tensor& x) { return x.clone(); };

        const auto v = rand_in("ph");
        const auto r = evaluate_directional_right_preconditioner_defect(
            weighted, bA(weighted, A), bP(weighted, Pinv), v);
        check(r.ok, "the defect is scored under the production error weights");

        // Independently, through production's own function.
        const auto err = A(Pinv(v)) - v;
        const double expected =
            (wrf::sdirk3::wrms_norm_packed(err, y_ref, pb, cfg) /
             wrf::sdirk3::wrms_norm_packed(v, y_ref, pb, cfg)).item<double>();
        check(expected > 1e-6, "the reference ratio is nonzero, so the match is not a match of 0");
        check(std::abs(r.global_error - expected) < 1e-9,
              "the contract's defect EQUALS wrms(err)/wrms(v) -- one weighting, not two");

        // The weights are part of the linearization: a set sized for another grid is refused
        // rather than broadcast to fit -- and refused BEFORE the operators run. The size match
        // used to be discovered inside inverse_scale_vector, which is reached only after four
        // applications, so an unanswerable query perturbed a live solver four times first.
        // Counting the calls is what makes "before execution" a measurement instead of a claim.
        {
            auto calls = std::make_shared<int>(0);
            wrf::sdirk3::LinearOperator counted = [calls](const torch::Tensor& x) {
                ++(*calls);
                return x.clone();
            };

            auto wrong = weighted;
            wrong.scale = wrf::sdirk3::ResidualScale::from_error_weights(
                ewt.slice(0, 0, ewt.numel() - 1), snap.token.scale_generation);
            check(!evaluate_directional_right_preconditioner_defect(
                       wrong, bA(wrong, counted), bP(wrong, counted), v).ok,
                  "error weights sized for a DIFFERENT grid are refused");
            check(*calls == 0,
                  "and refused with ZERO operator calls -- an unanswerable query costs nothing");

            // A scale that ANNIHILATES the direction. Weights of 1e300 are finite and positive,
            // so is_valid() passes, and 1/w = 1e-300 is finite, so the weight vector builds --
            // but the squares underflow inside the norm, leaving ||S^-1 v|| exactly 0 and the
            // defect with no denominator. Measured before this case existed: that was discovered
            // AFTER four operator applications, because the denominator check sat past them even
            // though it needs no operator at all.
            *calls = 0;
            auto annihilating = weighted;
            annihilating.scale = wrf::sdirk3::ResidualScale::from_error_weights(
                torch::full({snap.layout.total_size}, 1e300, opts), snap.token.scale_generation);
            check(annihilating.scale.is_valid(),
                  "the annihilating scale PASSES is_valid -- finite and positive, so the refusal "
                  "has to come from somewhere else");
            check(!evaluate_directional_right_preconditioner_defect(
                       annihilating, bA(annihilating, counted), bP(annihilating, counted), v).ok
                  && *calls == 0,
                  "a scale that annihilates the direction is refused with ZERO operator calls");

            // Control, so the counters above are not passing because the operators are never
            // called at all.
            *calls = 0;
            check(evaluate_directional_right_preconditioner_defect(
                      weighted, bA(weighted, counted), bP(weighted, counted), v).ok && *calls > 0,
                  "a well-formed query DOES call them (the zeros above are refusals, not inertia)");
        }

        // FROZEN means frozen. A Tensor is a handle, so storing the caller's would leave them
        // holding the same storage; from_error_weights takes a private copy, and mutating the
        // caller's tensor afterwards cannot move the number.
        {
            auto mutable_ewt = ewt.clone();
            auto frozen = weighted;
            frozen.scale = wrf::sdirk3::ResidualScale::from_error_weights(
                mutable_ewt, snap.token.scale_generation);
            const double before = evaluate_directional_right_preconditioner_defect(
                frozen, bA(frozen, A), bP(frozen, Pinv), v).global_error;

            mutable_ewt.mul_(1000.0);      // the caller changes what it handed over
            const double after = evaluate_directional_right_preconditioner_defect(
                frozen, bA(frozen, A), bP(frozen, Pinv), v).global_error;

            check(before > 1e-6 && std::abs(after - before) < 1e-12,
                  "mutating the caller's weights AFTER freezing does not move the defect");
        }
    }

    // --------------------------------------------- 7. h is dt*gamma, stated once
    {
        check(std::abs(snap.h() - 600.0 * 0.4358665215) < 1e-12,
              "h = dt * gamma comes from the snapshot, not a local recomputation");
    }

    constexpr int expected_checks = 75;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "OPERATOR_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "OPERATOR_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
