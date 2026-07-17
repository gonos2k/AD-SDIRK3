// PR 9B.1 commit 2: standing contract for the rw term-capture safety
// machinery (wrf_sdirk3_rw_term_capture.h). Fails closed on the exact
// hazards the PR #32 review identified: cross-thread contamination,
// exception-leaked armed slots, nested arming, and incomplete/duplicated
// capture inventories. Case-count ratchet: the exact number of executed
// cases is asserted so silent coverage shrink fails the test.
#include <torch/torch.h>
#include <atomic>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "wrf_sdirk3_rw_term_capture.h"

using wrf::sdirk3::RwTermCaptureScope;
using wrf::sdirk3::rw_term_capture_slot;
using wrf::sdirk3::validate_rw_term_inventory;

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
using Terms = std::vector<std::pair<std::string, torch::Tensor>>;
Terms full_inventory(bool with_wdamp) {
    // PR 9C: the whole W-damping family (inputs, chain factors, term) is
    // present only when the parity-gated damping is active.
    static const char* base_names[] = {
        "pg", "buoy_mu1", "buoy_mu2", "rw_pre_pgf", "w_pgf_buoy_all",
        "w_top_contrib", "rw_pre_mask", "rw_post_mask", "rw_tend_final"};
    static const char* wdamp_names[] = {
        "w_input", "mu_input", "wd_vert_cfl", "wd_cfl_excess", "wd_w_sign",
        "wd_mass_factor", "w_damp_padded"};
    Terms t;
    for (const char* n : base_names) t.emplace_back(n, torch::ones({2}));
    if (with_wdamp)
        for (const char* n : wdamp_names) t.emplace_back(n, torch::ones({2}));
    return t;
}
}  // namespace

int main() {
    torch::NoGradGuard no_grad;

    // (1) RAII basic: armed inside scope, disarmed+empty after destruction
    //     without take().
    {
        {
            RwTermCaptureScope scope;
            check(scope.armed_ok(), "scope arms an idle slot");
            rw_term_capture_slot().add("x", torch::ones({2}));
            check(rw_term_capture_slot().terms.size() == 1,
                  "armed slot records adds");
        }
        check(!rw_term_capture_slot().armed, "slot disarmed after scope end");
        check(rw_term_capture_slot().terms.empty(),
              "slot cleared after scope end (untaken tensors not retained)");
    }

    // (2) Exception safety: a throw inside the captured evaluation unwinds
    //     through the scope; the slot must be disarmed and empty.
    {
        bool threw = false;
        try {
            RwTermCaptureScope scope;
            rw_term_capture_slot().add("leaky", torch::ones({4}));
            throw std::runtime_error("compute_rhs failure");
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "exception propagated");
        check(!rw_term_capture_slot().armed,
              "slot disarmed after exception unwind");
        check(rw_term_capture_slot().terms.empty(),
              "slot empty after exception unwind");
    }

    // (3) Nested arming fails closed; the outer capture stays intact.
    {
        RwTermCaptureScope outer;
        check(outer.armed_ok(), "outer scope arms");
        rw_term_capture_slot().add("outer_term", torch::ones({2}));
        {
            RwTermCaptureScope inner;
            check(!inner.armed_ok(), "nested scope refuses to arm");
        }
        check(rw_term_capture_slot().armed,
              "outer capture still armed after nested refusal");
        auto taken = outer.take();
        check(taken.size() == 1 && taken[0].first == "outer_term",
              "outer capture unaffected by nested refusal");
        check(!rw_term_capture_slot().armed, "take() disarms");
    }

    // (4) Thread-locality: two concurrent captures never contaminate each
    //     other, and the main thread's slot stays idle throughout.
    {
        std::atomic<int> cross_contamination{0};
        std::atomic<int> per_thread_ok{0};
        auto worker = [&](const std::string& tag) {
            RwTermCaptureScope scope;
            if (!scope.armed_ok()) return;
            for (int i = 0; i < 50; ++i) {
                rw_term_capture_slot().add((tag + std::to_string(i)).c_str(),
                                           torch::ones({2}));
                std::this_thread::yield();
            }
            auto taken = scope.take();
            bool all_mine = taken.size() == 50;
            for (const auto& kv : taken) {
                if (kv.first.rfind(tag, 0) != 0) all_mine = false;
            }
            if (all_mine) per_thread_ok.fetch_add(1);
            else cross_contamination.fetch_add(1);
        };
        std::thread a(worker, "A_"), b(worker, "B_");
        a.join();
        b.join();
        check(per_thread_ok.load() == 2,
              "both threads captured exactly their own 50 terms");
        check(cross_contamination.load() == 0,
              "zero cross-thread contamination");
        check(!rw_term_capture_slot().armed && rw_term_capture_slot().terms.empty(),
              "main-thread slot untouched by worker captures");
    }

    // (5) Inventory validation: complete inventories pass; every defect
    //     class is named exactly.
    {
        check(validate_rw_term_inventory(full_inventory(true), true).empty(),
              "complete inventory (wdamp active) validates clean");
        check(validate_rw_term_inventory(full_inventory(false), false).empty(),
              "complete inventory (wdamp inactive) validates clean");

        auto missing = full_inventory(true);
        for (auto it = missing.begin(); it != missing.end(); ++it) {
            if (it->first == "buoy_mu1") { missing.erase(it); break; }
        }
        check(validate_rw_term_inventory(missing, true).find(
                  "missing:buoy_mu1") != std::string::npos,
              "missing buoy_mu1 detected by name");

        auto dup = full_inventory(true);
        dup.emplace_back("pg", torch::ones({2}));
        check(validate_rw_term_inventory(dup, true).find("duplicate:pg") !=
                  std::string::npos,
              "duplicate pg detected by name");

        auto unknown = full_inventory(true);
        unknown.emplace_back("mystery", torch::ones({2}));
        check(validate_rw_term_inventory(unknown, true).find(
                  "unknown:mystery") != std::string::npos,
              "unknown term detected by name");

        check(validate_rw_term_inventory(full_inventory(false), true).find(
                  "missing:w_damp_padded") != std::string::npos,
              "expected-but-missing w_damp_padded detected");
        check(validate_rw_term_inventory(full_inventory(true), false).find(
                  "unexpected:w_damp_padded") != std::string::npos,
              "unexpected w_damp_padded detected");
    }

    // (6) PR 9B.2 P1-1: post-take overlap — a scope kept alive past its
    //     take() must never disarm or clear a later capture.
    {
        auto s1 = std::make_unique<RwTermCaptureScope>();
        check(s1->armed_ok(), "overlap: scope1 arms");
        rw_term_capture_slot().add("first", torch::ones({2}));
        auto taken1 = s1->take();
        check(taken1.size() == 1 && taken1[0].first == "first",
              "overlap: scope1 takes its own term");
        RwTermCaptureScope s2;
        check(s2.armed_ok(), "overlap: post-take scope2 arms");
        rw_term_capture_slot().add("second", torch::ones({2}));
        s1.reset();  // scope1 destructor runs while scope2 is armed
        check(rw_term_capture_slot().armed,
              "overlap: scope2 still armed after scope1 destruction");
        auto taken2 = s2.take();
        check(taken2.size() == 1 && taken2[0].first == "second",
              "overlap: scope2 terms preserved across scope1 destruction");
    }

    // (7) PR 9B.2 P1-2: undefined captured tensors fail closed BY NAME.
    {
        auto undef = full_inventory(true);
        for (auto& kv : undef) {
            if (kv.first == "pg") { kv.second = torch::Tensor(); break; }
        }
        check(validate_rw_term_inventory(undef, true).find("undefined:pg") !=
                  std::string::npos,
              "undefined required term detected by name");

        auto undef_opt = full_inventory(true);
        undef_opt.back().second = torch::Tensor();  // w_damp_padded undefined
        check(validate_rw_term_inventory(undef_opt, true).find(
                  "undefined:w_damp_padded") != std::string::npos,
              "undefined optional w_damp_padded detected by name");

        auto mixed = full_inventory(true);
        mixed.emplace_back("pg", torch::Tensor());  // defined + undefined pair
        check(validate_rw_term_inventory(mixed, true).find("duplicate:pg") !=
                  std::string::npos,
              "defined+undefined pair under one name is still a duplicate");
    }

    // Case-count ratchet: exactly this many checks must have executed.
    const int kExpectedCases = 30;
    if (g_cases != kExpectedCases) {
        std::printf("FAIL: case-count ratchet: executed %d cases, expected %d\n",
                    g_cases, kExpectedCases);
        ++g_failures;
    }

    if (g_failures == 0) {
        std::printf("rw term capture contract: ALL PASS (%d/%d cases)\n",
                    g_cases, kExpectedCases);
        return 0;
    }
    std::printf("rw term capture contract: %d FAILURE(S)\n", g_failures);
    return 1;
}
