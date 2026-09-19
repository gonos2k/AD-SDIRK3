// 9F.D45: the base-state C ABI's return contract (review section 4).
//
// WHY THIS EXISTS, AND WHY IT IS SEPARATE FROM THE LAYOUT TEST. Base_State_Layout_Contract
// proves the geometry helper throws on bad input. That is NOT the same claim as "the C
// entry point returns 0 on bad input" -- and the difference is precisely the defect this
// commit fixes: D44's validator threw correctly and sat OUTSIDE the caller's try, so the
// throw escaped through extern "C" instead of becoming a 0. A helper that throws and an
// ABI that converts throws into a status code are two properties, and only the second
// one is what Fortran depends on.
//
// So every case here calls the REAL exported symbol and asserts on its int return,
// never on an exception. If any case terminates the process instead of returning, the
// contract is broken in the way that matters.
//
// NEGATIVE CONTROL, VERIFIED. Reproducing D44's ordering -- calling the layout helper
// just BEFORE the try -- makes this fixture die at the first invalid-geometry case:
//     exit 134, "terminating due to uncaught exception of type std::invalid_argument:
//      base-state layout: i tile [8,2] outside memory [1,16]"
// which is exactly what the Fortran run would have done. Note the failure mode is
// necessarily an ABORT, not a clean assertion: a throw crossing extern "C" terminates,
// so the test cannot survive to report it. Here the abort IS the contract violation,
// unlike the layout fixture where an abort merely made a real regression unreadable.

#include "../wrf_sdirk3_interface.h"
#include "../wrf_sdirk3_interface_params.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

void call_v2_mixed_tendency(void* solver, float* state, float* metric,
                            float* ru_tend) {
    SDIRK3_IndexBounds bounds{2, 8, 2, 6, 1, 8,
                              1, 16, 1, 12, 1, 10,
                              1, 16, 1, 12, 1, 10};
    SDIRK3_Dimensions dims{7, 5, 7, 8, 6, 8, 2};
    SDIRK3_ScalarParams scalars{1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.01f, 0.01f};
    SDIRK3_BoundaryConfig bdy{0, 0, 0, 0};
    sdirk3_tile_unified_step_zerocopy_v2(
        solver,
        state, state, state, state, state, state, state, state,
        nullptr, 0,
        ru_tend, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        metric, metric, metric, metric, metric, metric, metric, metric,
        metric, metric, metric, metric, metric, metric, metric, metric,
        metric, metric,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        &bounds, &dims, &scalars, &bdy);
}

// A memory domain big enough for the tile below.
constexpr int IMS = 1, IME = 16, JMS = 1, JME = 12, KMS = 1, KME = 10;
constexpr int ITS = 2, ITE = 8,  JTS = 2, JTE = 6,  KTS = 1, KTE = 8;

std::vector<float> buf3d() { return std::vector<float>((IME - IMS + 1) *
                                                       (KME - KMS + 1) *
                                                       (JME - JMS + 1), 1.0f); }
std::vector<float> buf2d() { return std::vector<float>((IME - IMS + 1) *
                                                       (JME - JMS + 1), 1.0f); }

// Calls the exported symbol with overridable geometry; solver_ptr is deliberately a
// value that is NOT in the registry unless stated otherwise.
int call(void* solver,
         float* pb, float* t_init, float* phb, float* mub,
         int its = ITS, int ite = ITE, int jts = JTS, int jte = JTE,
         int kts = KTS, int kte = KTE) {
    return sdirk3_tile_set_base_state_checked(
        solver, pb, t_init, phb, mub,
        its, ite, jts, jte, kts, kte,
        IMS, IME, JMS, JME, KMS, KME,
        nullptr, nullptr, nullptr, nullptr,
        0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0.0f);
}

}  // namespace

int main() {
    auto pb = buf3d(), ti = buf3d(), phb = buf3d();
    auto mub = buf2d();
    int dummy_solver = 0;
    void* not_registered = &dummy_solver;

    // --- null inputs return 0, they do not throw ---
    check(call(nullptr, pb.data(), ti.data(), phb.data(), mub.data()) == 0,
          "null solver -> 0");
    check(call(not_registered, nullptr, ti.data(), phb.data(), mub.data()) == 0,
          "null pb -> 0");
    check(call(not_registered, pb.data(), nullptr, phb.data(), mub.data()) == 0,
          "null t_init -> 0");
    check(call(not_registered, pb.data(), ti.data(), nullptr, mub.data()) == 0,
          "null phb -> 0");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), nullptr) == 0,
          "null mub -> 0");

    // --- a solver that is not in the registry returns 0 ---
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data()) == 0,
          "unregistered solver -> 0");

    // --- INVALID GEOMETRY RETURNS 0 RATHER THAN THROWING ---
    // This is the section-2 defect. Before the fix these threw std::invalid_argument /
    // c10::Error out through extern "C"; reaching the assertion below at all is the
    // proof that they no longer do. The solver is unregistered, so a 0 here could also
    // come from the registry miss -- which is why the layout contract covers the
    // geometry semantics separately and this file covers only "no throw crosses the
    // boundary".
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               /*its*/ 8, /*ite*/ 2) == 0,
          "inverted i tile -> 0, no throw escapes");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               ITS, ITE, JTS, JTE, KTS, /*kte*/ KME) == 0,
          "phb w-level overflow -> 0, no throw escapes");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               /*its*/ IMS - 5, ITE) == 0,
          "tile below memory domain -> 0, no throw escapes");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               ITS, /*ite*/ IME + 5) == 0,
          "tile above memory domain -> 0, no throw escapes");

    // Reaching here without terminating IS the contract: every call above returned an
    // int across a C boundary. State it as an assertion so the log says so explicitly.
    check(true, "all invalid-input calls RETURNED across the C ABI (none terminated)");

    // U04: a registered v2 handle must reject mixed tendency ownership before
    // touching the tile implementation. The all-null production mode reaches
    // the same prevalidation point; one supplied pointer is the forbidden mode.
    std::vector<float> rdnw(8, 1.0f);
    std::vector<float> state(4096, 1.0f), metric(4096, 1.0f);
    void* registered = sdirk3_tile_solver_create_zerocopy(
        7, 5, 7, 1.0f, 1.0f, rdnw.data(), 901, 8, 6, 8);
    check(registered != nullptr, "v2 regression solver handle registered");
    if (registered != nullptr) {
        check(sdirk3_tile_solver_begin_fixed_trajectory_zerocopy(
                  registered, 1, nullptr, 0) == 1,
              "fixed trajectory request accepted before first publication");
        const int trajectory_size = sdirk3_tile_solver_get_state_vector_size_zerocopy(registered);
        bool sentinel_preserved = false;
        if (trajectory_size > 0) {
            std::vector<float> terminal(static_cast<size_t>(trajectory_size), 1.0f);
            std::vector<float> initial(static_cast<size_t>(trajectory_size), -37.0f);
            sentinel_preserved =
                sdirk3_tile_solver_pullback_fixed_trajectory_zerocopy(
                    registered, terminal.data(), trajectory_size, initial.data()) == 0 &&
                initial == std::vector<float>(static_cast<size_t>(trajectory_size), -37.0f);
        }
        check(trajectory_size > 0 && sentinel_preserved,
              "incomplete C ABI pullback returns 0 and preserves sentinel output");
        check(sdirk3_tile_solver_close_fixed_trajectory_zerocopy(registered) == 1,
              "close cancels pending C ABI request before first publication");
        check(call(registered, pb.data(), ti.data(), phb.data(), mub.data()) == 1,
              "v2 regression base state initialized");
        int outcome = -1, aborted = -1, ratio_valid = -1;
        float ratio = -1.0f;
        call_v2_mixed_tendency(registered, state.data(), metric.data(), nullptr);
        check(sdirk3_tile_solver_get_last_step_outcome_zerocopy(
                  registered, &outcome, &aborted, &ratio, &ratio_valid) == 1 &&
                  outcome == SDIRK3_STEP_OUTCOME_OK_SKIPPED && aborted == 0,
              "v2 all-null tendency control reaches OK_SKIPPED");
        const std::vector<float> state_before_mixed = state;
        float sentinel = 7.0f;
        call_v2_mixed_tendency(registered, state.data(), metric.data(), &sentinel);
        outcome = -1;
        check(sdirk3_tile_solver_get_last_step_outcome_zerocopy(
                  registered, &outcome, &aborted, &ratio, &ratio_valid) == 1 &&
                  outcome == SDIRK3_STEP_OUTCOME_FATAL_INPUT && aborted == 1 &&
                  ratio_valid == 0 && sentinel == 7.0f && state == state_before_mixed,
              "v2 mixed tendency pointers -> FATAL_INPUT, state unmodified");
        sdirk3_tile_solver_destroy_zerocopy(registered);
    }

    constexpr int expected_checks = 18;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "BASE_STATE_CHECKED_ABI: PASS" << std::endl; return 0; }
    std::cout << "BASE_STATE_CHECKED_ABI: FAIL (" << failures << ")" << std::endl;
    return 1;
}
