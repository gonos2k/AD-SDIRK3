// 9F.D44: acoustic_schedule's INVALID-input matrix (review section 9).
//
// WHY THIS EXISTS. D41 added five TORCH_CHECKs to acoustic_schedule() and verified them
// by reading the code and by a live fingerprint that only ever exercises VALID inputs.
// The existing Acoustic_Substep_AD fixture checks the stage-1/2/3 time fractions -- the
// happy path -- so nothing would fail if the guards were removed tomorrow.
//
// Every case below was a SILENT WRONG ANSWER before those guards, not a crash:
//   rk_step 0 or 4        fell through the if-chain and behaved as STAGE 3
//   odd num_sound_steps   broke dts*(N/2) == dt/2 via integer division
//   num_sound_steps == 0  divided by zero on the function's first line
//   stage1_substeps == 0  divided by zero inside the stage-1 branch
//   dt <= 0               produced a non-physical but well-formed schedule
// A wrong-stage schedule does not announce itself; it just integrates the wrong
// fraction of the timestep. That is what makes the negative matrix worth more here
// than another positive case.

#include "../wrf_sdirk3_acoustic_substep.h"

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

using wrf::sdirk3::acoustic::acoustic_schedule;
using wrf::sdirk3::acoustic::AcousticScheduleOptions;

bool rejects(int rk, float dt, int n, int s1) {
    try {
        (void)acoustic_schedule(rk, dt, n, AcousticScheduleOptions{s1});
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

}  // namespace

int main() {
    const AcousticScheduleOptions ok_opts{1};

    // --- positive controls first: the guards must not reject legitimate input ---
    // If these ever fail, the contract is too strict and would abort production runs --
    // which is the failure mode a fail-close change has to be checked against.
    {
        for (int n : {4, 6, 8, 16}) {
            for (int rk : {1, 2, 3}) {
                bool threw = false;
                try { (void)acoustic_schedule(rk, 60.0f, n, ok_opts); }
                catch (const std::exception&) { threw = true; }
                check(!threw, "valid rk=" + std::to_string(rk) +
                              " N=" + std::to_string(n) + " accepted");
            }
        }
        for (int s1 : {1, 2, 4}) {
            bool threw = false;
            try { (void)acoustic_schedule(1, 60.0f, 4, AcousticScheduleOptions{s1}); }
            catch (const std::exception&) { threw = true; }
            check(!threw, "valid stage1_substeps=" + std::to_string(s1) + " accepted");
        }
    }

    // --- the time-fraction invariants the guards exist to protect ---
    {
        const float dt = 12.0f; const int N = 4;
        const auto s1 = acoustic_schedule(1, dt, N, ok_opts);
        const auto s2 = acoustic_schedule(2, dt, N, ok_opts);
        const auto s3 = acoustic_schedule(3, dt, N, ok_opts);
        check(std::abs(s1.dts * s1.n_sub - dt / 3.0f) < 1e-4f, "stage 1 spans dt/3");
        check(std::abs(s2.dts * s2.n_sub - dt / 2.0f) < 1e-4f, "stage 2 spans dt/2");
        check(std::abs(s3.dts * s3.n_sub - dt)        < 1e-4f, "stage 3 spans dt");
    }

    // --- rk_step out of range: previously SILENTLY treated as stage 3 ---
    for (int rk : {0, 4, -1, 99}) {
        check(rejects(rk, 60.0f, 4, 1),
              "rk_step=" + std::to_string(rk) + " REJECTED (was silently stage 3)");
    }

    // --- dt must be finite and positive ---
    check(rejects(1, 0.0f,  4, 1),  "dt=0 REJECTED");
    check(rejects(1, -60.f, 4, 1),  "dt<0 REJECTED");
    check(rejects(1, std::nanf(""), 4, 1), "dt=NaN REJECTED");
    check(rejects(1, INFINITY, 4, 1),      "dt=Inf REJECTED");

    // --- num_sound_steps: >= 4 and EVEN ---
    check(rejects(1, 60.0f, 0, 1), "num_sound_steps=0 REJECTED (was division by zero)");
    check(rejects(1, 60.0f, 2, 1), "num_sound_steps=2 REJECTED (below documented min)");
    check(rejects(1, 60.0f, 5, 1), "num_sound_steps=5 REJECTED (odd breaks stage-2 dt/2)");
    check(rejects(1, 60.0f, 7, 1), "num_sound_steps=7 REJECTED (odd)");

    // --- stage1_substeps >= 1 ---
    check(rejects(1, 60.0f, 4, 0),  "stage1_substeps=0 REJECTED (was division by zero)");
    check(rejects(1, 60.0f, 4, -3), "stage1_substeps<0 REJECTED");

    // --- an odd N is rejected for EVERY stage, not only the one that divides ---
    // Rejecting only on rk_step==2 would leave a run whose stages disagree about the
    // schedule, which is harder to notice than an outright failure.
    for (int rk : {1, 2, 3}) {
        check(rejects(rk, 60.0f, 5, 1),
              "odd N rejected at rk_step=" + std::to_string(rk) + " (not just stage 2)");
    }

    constexpr int expected_checks = 35;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "ACOUSTIC_SCHEDULE_MATRIX: PASS" << std::endl; return 0; }
    std::cout << "ACOUSTIC_SCHEDULE_MATRIX: FAIL (" << failures << ")" << std::endl;
    return 1;
}
