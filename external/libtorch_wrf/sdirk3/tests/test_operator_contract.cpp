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

#include <cmath>
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

    using wrf::sdirk3::evaluate_right_preconditioner;
    using wrf::sdirk3::block_direction;

    auto rand_in = [&](const std::string& name) {
        for (const auto& b : snap.layout.blocks) {
            if (b.name == name) {
                return block_direction(snap.layout, name, proto,
                                       torch::randn({b.size}, opts));
            }
        }
        throw std::runtime_error("no block " + name);
    };

    // ------------------------------------------------- 1. the exact inverse scores zero
    {
        auto A = [](const torch::Tensor& v) { return 3.0 * v; };
        auto Pinv = [](const torch::Tensor& v) { return v / 3.0; };
        auto r = evaluate_right_preconditioner(snap, A, Pinv, rand_in("ph"));
        check(r.ok, "report is marked ok for a valid direction");
        check(r.global_error < 1e-12, "exact inverse of a scaling gives ~0 error");
    }

    // ---------------------------------------- 2. identity P against a non-identity A is bad
    {
        auto A = [](const torch::Tensor& v) { return 3.0 * v; };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto r = evaluate_right_preconditioner(snap, A, Pinv, rand_in("ph"));
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
        auto r_exact = evaluate_right_preconditioner(snap, A, Pinv_exact, dir);
        auto r_diag  = evaluate_right_preconditioner(snap, A, Pinv_diag,  dir);

        check(r_exact.global_error < 1e-12,
              "coupled: the EXACT inverse scores ~0 even though its ph gain is 1/(1-cd), not 1");
        check(r_diag.global_error > 0.1,
              "coupled: the 1/A_qq diagonal scores badly -- the retracted inference is rejected");
        check(r_diag.global_error > r_exact.global_error * 1e6,
              "the metric separates them by orders, so it cannot be read either way");

        // And it says WHERE: a ph input under the diagonal leaves its error in mu.
        check(r_diag.in_block_error(snap.layout) < r_diag.global_error,
              "error is not confined to the input block -- cross-block model is what failed");
    }

    // ------------------------------------------ 4. it reports WHERE the error lands
    {
        int mu_s = -1, mu_n = 0;
        for (const auto& b : snap.layout.blocks) if (b.name == "mu") { mu_s = b.start; mu_n = b.size; }
        // A leaves everything alone except it writes ph input into mu.
        int ph_s = -1;
        for (const auto& b : snap.layout.blocks) if (b.name == "ph") ph_s = b.start;
        auto A = [&](const torch::Tensor& v) {
            auto out = v.clone();
            out.slice(0, mu_s, mu_s + mu_n) += 2.0 * v.slice(0, ph_s, ph_s + mu_n);
            return out;
        };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto r = evaluate_right_preconditioner(snap, A, Pinv, rand_in("ph"));
        double mu_err = 0.0;
        for (std::size_t i = 0; i < snap.layout.blocks.size(); ++i) {
            if (snap.layout.blocks[i].name == "mu") mu_err = r.output_block_error[i];
        }
        check(mu_err > 0.1, "leakage into mu is reported in mu's slot");
        check(r.in_block_error(snap.layout) < 1e-12,
              "the input block itself is clean, so the two are distinguishable");
    }

    // --------------------------------------- 5. invalid input is refused, not scored
    {
        auto A = [](const torch::Tensor& v) { return v; };
        auto Pinv = [](const torch::Tensor& v) { return v; };
        auto wrong = torch::randn({snap.layout.total_size - 1}, opts);
        check(!evaluate_right_preconditioner(snap, A, Pinv, wrong).ok,
              "a mis-sized direction returns ok=false rather than a number");

        wrf::sdirk3::LinearizationSnapshot bad;   // dt = gamma = 0
        check(!bad.is_valid(), "a default snapshot is invalid");
        check(!evaluate_right_preconditioner(bad, A, Pinv, rand_in("ph")).ok,
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

    // --------------------------------------------- 7. h is dt*gamma, stated once
    {
        check(std::abs(snap.h() - 600.0 * 0.4358665215) < 1e-12,
              "h = dt * gamma comes from the snapshot, not a local recomputation");
    }

    constexpr int expected_checks = 18;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "OPERATOR_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "OPERATOR_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
