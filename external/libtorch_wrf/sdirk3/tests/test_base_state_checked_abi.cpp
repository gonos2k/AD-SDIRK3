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

    constexpr int expected_checks = 11;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "BASE_STATE_CHECKED_ABI: PASS" << std::endl; return 0; }
    std::cout << "BASE_STATE_CHECKED_ABI: FAIL (" << failures << ")" << std::endl;
    return 1;
}
