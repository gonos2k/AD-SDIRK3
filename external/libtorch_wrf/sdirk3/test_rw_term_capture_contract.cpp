// PR 9B.1 commit 2: standing contract for the rw term-capture safety
// machinery (wrf_sdirk3_rw_term_capture.h). Fails closed on the exact
// hazards the PR #32 review identified: cross-thread contamination,
// exception-leaked armed slots, nested arming, and incomplete/duplicated
// capture inventories. Case-count ratchet: the exact number of executed
// cases is asserted so silent coverage shrink fails the test.
#include <torch/torch.h>
#include <atomic>
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
    static const char* names[] = {
        "w_input", "mu_input", "pg", "buoy_mu1", "buoy_mu2", "rw_pre_pgf",
        "w_pgf_buoy_all", "w_top_contrib", "rw_pre_mask", "rw_post_mask",
        "wd_vert_cfl", "wd_cfl_excess", "wd_w_sign", "wd_mass_factor",
        "rw_tend_final"};
    Terms t;
    for (const char* n : names) t.emplace_back(n, torch::ones({2}));
    if (with_wdamp) t.emplace_back("w_damp_padded", torch::ones({2}));
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
        missing.erase(missing.begin() + 3);  // drop buoy_mu1
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

    // Case-count ratchet: exactly this many checks must have executed.
    const int kExpectedCases = 22;
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
