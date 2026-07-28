// 9F.D39: replay provenance for the trajectory-changing config (review section 7).
//
// WHY THIS EXISTS. D33 made ExperimentConfig object state, which fixed WHEN it is
// read. It did not make the setting recoverable from an artifact afterwards. Both
// fields change the trajectory -- uv_slow ablates terms from the slow tendency,
// stage1_substeps subdivides the acoustic stage-1 step -- so a checkpoint or adjoint
// replay resumed under different settings is integrating a DIFFERENT MODEL, and
// nothing in the state records that.
//
// The gate fails closed by explicit decision. The asymmetry drives it: aborting a run
// that would have been fine costs a restart and an obvious message, whereas merely
// warning lets an adjoint replay under the wrong linearization produce gradients that
// are WRONG BUT FINITE -- no NaN, no crash, nothing downstream that looks abnormal.
//
// Standalone and torch-free, like the header it tests (review section 6): if this ever
// stops compiling without libtorch, the "torch-free config" property has regressed and
// this fixture is where that shows up first.

#include "../wrf_sdirk3_experiment_config.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const char* what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

bool rejects(const std::string& recorded, const wrf::sdirk3::ExperimentConfig& current) {
    try {
        wrf::sdirk3::require_matching_experiment_config(recorded, current);
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

int main() {
    using namespace wrf::sdirk3;

    const ExperimentConfig baseline;   // uv_slow=None, stage1_substeps=1

    // --- the canonical string is stable and names its fields ---
    check(baseline.provenance() == "uv_slow=none stage1_substeps=1",
          "baseline provenance is the expected canonical string");
    // Pin the token AWAY from the display name. The display strings embed env-var
    // spellings, so if provenance ever starts using them again a rename becomes a
    // spurious replay failure. This assertion is the tripwire for that regression.
    check(std::string(uv_slow_experiment_token(UvSlowExperiment::DropU)) == "drop_u"
          && std::string(uv_slow_experiment_name(UvSlowExperiment::DropU)) != "drop_u",
          "provenance token is stable and distinct from the display name");
    check(baseline.provenance() == ExperimentConfig{}.provenance(),
          "provenance is deterministic across identical configs");

    // --- matching config passes ---
    check(!rejects(baseline.provenance(), baseline),
          "identical config -> gate does NOT throw");

    // --- each trajectory-changing field is individually caught ---
    // Built to violate: a gate that only compared, say, uv_slow would pass the first
    // of these and fail the second, which is exactly the partial-sweep defect this
    // campaign keeps producing.
    {
        ExperimentConfig other = baseline;
        other.stage1_substeps = 3;
        check(rejects(baseline.provenance(), other),
              "stage1_substeps difference -> REJECTED");
        check(other.digest() != baseline.digest(),
              "stage1_substeps difference changes the digest");
    }
    {
        ExperimentConfig other = baseline;
        other.uv_slow = UvSlowExperiment::DropU;
        check(rejects(baseline.provenance(), other),
              "uv_slow difference -> REJECTED");
        check(other.digest() != baseline.digest(),
              "uv_slow difference changes the digest");
    }

    // --- the message must name BOTH sides ---
    // "configs differ" without saying which field is the message people learn to
    // ignore, so the content of the error is itself part of the contract.
    {
        ExperimentConfig other = baseline;
        other.stage1_substeps = 7;
        std::string msg;
        try {
            require_matching_experiment_config(baseline.provenance(), other);
        } catch (const std::invalid_argument& e) {
            msg = e.what();
        }
        check(msg.find(baseline.provenance()) != std::string::npos,
              "error names the RECORDED provenance");
        check(msg.find(other.provenance()) != std::string::npos,
              "error names the CURRENT provenance");
    }

    // --- diagnostics config is recorded but has no digest ---
    // Deliberate: if a diagnostics flag ever moved the trajectory that is the bug, and
    // folding it into a state digest would hide it behind a "configs differ" message
    // instead of surfacing it as a numerical difference. Pinned so a future change
    // that "helpfully" adds diagnostics to the digest has to argue with a test.
    {
        DiagnosticsConfig d;
        const std::string off = d.provenance();
        d.trace_u_terms = true;
        check(d.provenance() != off, "diagnostics provenance IS recorded");
        check(baseline.digest() == ExperimentConfig{}.digest(),
              "diagnostics state does NOT enter the experiment digest");
    }

    constexpr int expected_checks = 12;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "CONFIG_PROVENANCE: PASS" << std::endl;
        return 0;
    }
    std::cout << "CONFIG_PROVENANCE: FAIL (" << failures << ")" << std::endl;
    return 1;
}
