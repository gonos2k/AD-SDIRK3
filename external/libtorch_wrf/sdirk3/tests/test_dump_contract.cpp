// 9F.D40: the advect_u split dump's write contract, as a STANDING gate (review §7).
//
// WHY THIS EXISTS. D36 hardened the dump (open/write/flush/close checked, latch
// published only after a verified write) and D38 added shape/dtype checks. Every bit of
// that was verified by reading the code. No fixture would have failed if the latch went
// back to being published first, or if a failed write left it true, or if two solvers
// wrote the same path. Hardening that only source review can see is not a contract.
//
// THE BUG THESE PIN. Moving the latch from a process-global static into per-solver
// DiagnosticsState (D36) was right for ownership and, alone, unsafe: every solver still
// received the same hard-coded filename, so N solvers each "successfully" wrote the same
// artifact over one another and each logged success. The process-global latch had been
// accidentally protecting the shared name. Per-solver identity in the PATH is what
// actually fixes it, so that is what gets asserted here.

#include "../wrf_sdirk3_u_slow_diagnostics.h"

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>   // rmdir
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const char* what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

std::string tmpdir() {
    const char* t = std::getenv("TMPDIR");
    std::string d = t ? t : "/tmp";
    if (!d.empty() && d.back() == '/') d.pop_back();
    return d;
}

wrf::sdirk3::UAdvectionTerms valid_terms() {
    const auto o = torch::TensorOptions().dtype(torch::kFloat32);
    wrf::sdirk3::UAdvectionTerms a;
    a.x        = torch::randn({4, 3, 5}, o);
    a.y        = torch::randn({4, 3, 5}, o);
    a.vertical = torch::randn({4, 3, 5}, o);
    return a;
}

wrf::sdirk3::DiagnosticContext ctx_of(std::uint64_t solver, int tile) {
    wrf::sdirk3::DiagnosticContext c;
    c.solver_id = solver;
    c.rank = 0;
    c.tile = tile;
    return c;
}

bool exists(const std::string& p) { std::ifstream f(p); return f.good(); }

long long size_of(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    return f.good() ? static_cast<long long>(f.tellg()) : -1;
}

// Every artifact this test creates, removed at the end -- including the .tmp, so a
// leaked partial file is itself detectable rather than quietly reused next run.
std::vector<std::string> litter;
std::string stem(const std::string& name) {
    const std::string s = tmpdir() + "/" + name;
    litter.push_back(s);
    return s;
}

}  // namespace

int main() {
    using namespace wrf::sdirk3;
    torch::manual_seed(0);

    // --- a valid write succeeds, publishes the latch, and lands at the final name ---
    std::string first_path;
    {
        DiagnosticsState st;
        const auto ctx = ctx_of(1, 0);
        const std::string s = stem("d40_dump_ok");
        check(dump_advect_u_split(st, ctx, valid_terms(), s), "valid dump returns true");
        check(st.split_dump_written, "successful write publishes the latch");
        first_path = s + ctx.filename_suffix() + ".bin";
        check(exists(first_path), "artifact exists at the context-qualified name");
        check(!exists(first_path + ".tmp"),
              "no .tmp survives a successful write (atomic rename, review section 8)");
        litter.push_back(first_path);
        litter.push_back(first_path + ".tmp");
    }

    // --- one-shot per state ---
    {
        DiagnosticsState st;
        const std::string s = stem("d40_dump_once");
        const auto ctx = ctx_of(2, 0);
        check(dump_advect_u_split(st, ctx, valid_terms(), s), "first dump writes");
        check(!dump_advect_u_split(st, ctx, valid_terms(), s),
              "second dump on the SAME state is refused");
        litter.push_back(s + ctx.filename_suffix() + ".bin");
    }

    // --- THE REGRESSION: two solvers must not collide on one artifact ---
    // With the old hard-coded literal both of these wrote the same file and both
    // returned true. The names must differ, and both files must exist afterwards.
    {
        DiagnosticsState a, b;
        const auto ca = ctx_of(10, 0), cb = ctx_of(11, 1);
        const std::string s = stem("d40_dump_two_solvers");
        check(dump_advect_u_split(a, ca, valid_terms(), s), "solver A writes");
        check(dump_advect_u_split(b, cb, valid_terms(), s), "solver B writes");
        const std::string pa = s + ca.filename_suffix() + ".bin";
        const std::string pb = s + cb.filename_suffix() + ".bin";
        check(pa != pb, "two solvers produce DIFFERENT artifact names");
        check(exists(pa) && exists(pb),
              "both artifacts survive -- neither solver overwrote the other");
        litter.push_back(pa); litter.push_back(pb);
    }

    // --- a failed write must NOT publish the latch ---
    // Publishing first was the original defect: one unwritable path permanently
    // disabled the dump for that solver while reporting nothing wrong.
    {
        DiagnosticsState st;
        bool threw = false;
        try {
            dump_advect_u_split(st, ctx_of(3, 0), valid_terms(),
                                "/nonexistent_dir_d40/does_not_exist");
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "unwritable path THROWS");
        check(!st.split_dump_written,
              "failed write leaves the latch FALSE (a retry is still possible)");
    }

    // --- a failed RENAME must also leave the latch false ---
    // Added after the first version of this file FAILED to catch a deliberately
    // broken build that published the latch before the rename: the only failure case
    // here was an unopenable path, which trips before the latch in both the correct
    // and the broken ordering. A negative control that the fixture survives is not a
    // negative control. Making rename(2) fail deterministically: pre-create a
    // DIRECTORY at the final artifact name, so the .tmp writes fine and only the
    // rename can fail.
    {
        DiagnosticsState st;
        const auto ctx = ctx_of(7, 0);
        const std::string s = stem("d40_dump_rename_fail");
        const std::string final_name = s + ctx.filename_suffix() + ".bin";
        ::mkdir(final_name.c_str(), 0755);
        bool threw = false;
        try { dump_advect_u_split(st, ctx, valid_terms(), s); }
        catch (const std::exception&) { threw = true; }
        check(threw, "failed rename THROWS");
        check(!st.split_dump_written,
              "failed RENAME leaves the latch false (latch published only after it)");
        ::rmdir(final_name.c_str());
        litter.push_back(final_name + ".tmp");
    }

    // --- section 6: broadcasting shapes are rejected, not silently summed ---
    // {1,nz,nx} + {ny,nz,nx} broadcasts to {ny,nz,nx}, which then matches vertical and
    // passes every downstream shape check -- writing a plausible, wrong field.
    {
        DiagnosticsState st;
        const auto o = torch::TensorOptions().dtype(torch::kFloat32);
        UAdvectionTerms bad;
        bad.x        = torch::randn({1, 3, 5}, o);   // would broadcast against y
        bad.y        = torch::randn({4, 3, 5}, o);
        bad.vertical = torch::randn({4, 3, 5}, o);
        check(bad.horizontal().sizes() == bad.vertical.sizes(),
              "PREMISE: the broadcast result really does look valid downstream");
        bool threw = false;
        try {
            dump_advect_u_split(st, ctx_of(4, 0), bad, stem("d40_dump_broadcast"));
        } catch (const std::exception&) { threw = true; }
        check(threw, "broadcasting shape mismatch is REJECTED");
        check(!st.split_dump_written, "rejected input leaves the latch false");
    }

    // --- section 5: a horizontal-only field must still be dumped ---
    // The old live test looked at adv_z alone, so exactly the case one investigates
    // when chasing HORIZONTAL operator parity was discarded forever.
    {
        DiagnosticsState st;
        const auto o = torch::TensorOptions().dtype(torch::kFloat32);
        UAdvectionTerms h;
        h.x        = torch::randn({4, 3, 5}, o);
        h.y        = torch::zeros({4, 3, 5}, o);
        h.vertical = torch::zeros({4, 3, 5}, o);     // vertical EXACTLY zero
        const auto ctx = ctx_of(5, 0);
        check(dump_advect_u_split(st, ctx, h, stem("d40_dump_horiz_only")),
              "horizontal-only field is still dumped (vertical-only heuristic removed)");
        litter.push_back(stem("d40_dump_horiz_only") + ctx.filename_suffix() + ".bin");
    }

    // --- an all-zero field is still skipped (the heuristic's legitimate half) ---
    {
        DiagnosticsState st;
        const auto o = torch::TensorOptions().dtype(torch::kFloat32);
        UAdvectionTerms z;
        z.x = torch::zeros({4, 3, 5}, o);
        z.y = torch::zeros({4, 3, 5}, o);
        z.vertical = torch::zeros({4, 3, 5}, o);
        check(!dump_advect_u_split(st, ctx_of(6, 0), z, stem("d40_dump_zero")),
              "all-zero field is skipped, and does NOT burn the latch");
        check(!st.split_dump_written, "skipped dump leaves the latch false");
    }

    // --- section 11: announce-once is per state, not per process ---
    {
        DiagnosticsState a, b;
        check(take_experiment_announcement(a), "solver A announces once");
        check(!take_experiment_announcement(a), "solver A does not announce twice");
        check(take_experiment_announcement(b),
              "solver B still announces -- A's announcement did not silence it");
    }

    // --- context identity is distinguishing, and 'unset' is not a fake index ---
    {
        check(ctx_of(1, 0).filename_suffix() != ctx_of(2, 0).filename_suffix(),
              "different solver_id -> different suffix");
        check(ctx_of(1, 0).filename_suffix() != ctx_of(1, 1).filename_suffix(),
              "different tile -> different suffix");
        const auto c = ctx_of(1, 0);
        check(c.filename_suffix().find("stepunset") != std::string::npos,
              "unplumbed step prints 'unset', NOT a fabricated 0");
        check(c.provenance().find("step=unset") != std::string::npos,
              "provenance says step=unset rather than inventing a step index");
    }

    for (const auto& f : litter) std::remove(f.c_str());

    constexpr int expected_checks = 27;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "DUMP_CONTRACT: PASS" << std::endl; return 0; }
    std::cout << "DUMP_CONTRACT: FAIL (" << failures << ")" << std::endl;
    return 1;
}
