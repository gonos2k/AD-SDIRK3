// wrf_sdirk3_u_slow_diagnostics.h -- observation of the u-momentum slow tendency.
//
// 9F.D30 (review PR S2-A). This exists to get configuration reading and diagnostic
// output OUT of computeUnifiedRHS. Deliberately minimal, per the review's own
// guidance: typed data carriers plus free functions, minimal and concrete. No
// observer hierarchy, no virtual interfaces, no registry -- those would be
// over-design for a single diagnostic.
//
// THE ONE INVARIANT THAT MATTERS HERE:
//
//     ExperimentConfig  MAY change the trajectory.
//     DiagnosticsConfig MUST NOT change the trajectory.
//
// They are separate types precisely so that boundary cannot blur. This campaign has
// repeatedly been misled in both directions -- a "diagnostic" that silently changed
// an operand, and an "experiment" that turned out to be a no-op -- and a single
// merged config makes both mistakes easy to write and hard to see.

#ifndef WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H
#define WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H

#include <torch/torch.h>

#include "wrf_sdirk3_diag_io.h"
#include "wrf_sdirk3_experiment_config.h"

#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>

namespace wrf {
namespace sdirk3 {

// The additive decomposition of the u advective tendency. `horizontal` and `total`
// are DERIVED -- computed from the additive fields, never stored -- so they cannot be
// double-counted in a closure sum. That fact used to live in a string comparison
// against the label "horiz", where renaming the label silently changed the closure's
// meaning.
struct UAdvectionTerms {
    torch::Tensor x, y, vertical;

    bool complete() const {
        return x.defined() && y.defined() && vertical.defined();
    }
    torch::Tensor horizontal() const { return x + y; }
    torch::Tensor total() const { return x + y + vertical; }
};

// One accumulation site's contribution to ru_tend, captured as a telescoping snapshot
// delta. `kind` (NOT a string) identifies the site. NOTE the residual computed from
// these is a POST-CAPTURE TAIL guard, not site inventory -- an unnamed mutation
// between two captures is absorbed into the next site's delta. See the tail-guard
// comment in the emitter.
// 9F.D32 (review section 4): site IDENTITY is an enum; the string is display only.
// `if (label == "adv")` was the same defect as the earlier `if (label != "horiz")` --
// renaming a display string still compiles and silently disables the closure that
// depends on it. Physics meaning must not live in text.
enum class USlowSiteKind {
    Entry, Advection, PressureGradient, Coriolis, Curvature,
    HorizontalDiffusion, VerticalDiffusion, Final
};

const char* site_name(USlowSiteKind k);

struct USlowSite {
    USlowSiteKind kind;
    torch::Tensor delta;
};

// Everything the u-slow diagnostic observes. Plain data: no policy, no I/O.
struct USlowTerms {
    UAdvectionTerms advection;
    std::vector<USlowSite> sites;   // in accumulation order
    torch::Tensor adv_site_delta;       // the advection site, for the advection closure
    torch::Tensor last_site_tendency;   // ru_tend AS CAPTURED at the last site
    torch::Tensor final_tendency;       // ru_tend at its last mutation
    torch::Tensor u;                // for the work projection sum(u * dR)
};

// 9F.D36 (review section 4): mutable diagnostic state belongs to the SOLVER, not the
// process. Config became per-solver in D33 while the record counter and the dump latch
// stayed function-local statics, so lifetime and ownership disagreed: a second solver
// in one process continued the first's record numbering, could never write its own
// dump, and a re-init never reset either.
struct DiagnosticsState {
    std::uint64_t uterms_record = 0;
    bool split_dump_written = false;
    bool experiment_announced = false;
    std::mutex dump_mutex;
};

// 9F.D35 (review section 5): a closure ALWAYS reports one of three states.
//
// Both closures were previously wrapped in `if (input.defined())`, so a refactor that
// stopped supplying an input produced NO line, NO error and NO marker -- the solver
// carried on and the log simply lacked a check nobody noticed was missing. That exact
// "claimed but silently absent" state has now occurred TWICE in this campaign, so the
// fix has to be structural rather than another comment.
//
// INVALID is deliberately distinct from FAIL: FAIL means the invariant was tested and
// violated; INVALID means it could not be tested at all. Collapsing them would let a
// missing input read as a passing run, which is the bug being closed.
//
// This does NOT abort the solver. Diagnostics must not alter the trajectory, so the
// status is reported and left for experiment acceptance to reject.
enum class ClosureStatus { Pass, Fail, Invalid };

const char* closure_status_name(ClosureStatus s);

// 9F.D34 (review section 4): production and the fixture must run the SAME capture
// code. The fixture previously hand-built a USlowTerms, so it verified only that the
// emitter formats a well-formed input correctly -- while the regression it was written
// for was in the WIRING (a dropped last_site_tendency assignment). A test that cannot
// see the wiring cannot guard the wiring.
struct USlowCaptureState {
    torch::Tensor previous;
    USlowTerms terms;
};

void capture_u_slow_site(USlowCaptureState& st,
                         USlowSiteKind kind,
                         const torch::Tensor& current);

// Emits the u-slow decomposition. Pure observation: takes tensors, writes lines.
// Never mutates its inputs and never influences a solver decision.
//
// `work` here is sum(u * dR) where dR is the COUPLED d(mu*u)/dt, so it is NOT exactly
// dKE/dt (it equals mu*d(u^2/2)/dt + u^2*dmu/dt). Only its SIGN is meaningful -- does
// the term reinforce the existing u field or oppose it -- which is the thing a
// magnitude cannot tell you.


// ---------------------------------------------------------------------------
// Header-only ON PURPOSE. A separate .cpp would have to be added to
// wrf_sdirk3_core_sources.txt, to CMakeLists, and to the CI exact-set install
// contract -- three coupled edits, and the exact-set ratchet has silently rotted
// four times in this repo.
//
// 9F.D34 (review section 8): an earlier version of this note claimed "exactly one
// translation unit includes this". That is no longer true -- tests/test_u_slow_
// closures.cpp includes it as well -- so the "inline costs nothing" argument does not
// hold as stated. It is also true, as the review checked, that adding a .cpp costs
// ONE line in wrf_sdirk3_core_sources.txt plus the CI "22 production sources" counter,
// not the three coupled edits claimed earlier. Moving the implementation to a .cpp is
// the right next step; it is deliberately NOT bundled with the closure-semantics fix
// so that a behavioural change and a file move do not land in one commit.
// ---------------------------------------------------------------------------


void emit_u_slow_diagnostics(DiagnosticsState& st, const USlowTerms& t);



// Notice that the port-side advect_u split dump was written. Kept here so the numeric
// function contains no diagnostic string formatting at all.
// 9F.D33 (review section 6): the DUMP ITSELF, not just its notice. Previously only
// the message moved out and the numeric function still did the enable test, the CPU
// conversion, the header construction, the ofstream and the write. Moving the string
// but leaving the I/O is not a separation.
// 9F.D36 (review section 3): FAIL-CLOSE. The previous version set the "already
// written" latch BEFORE any file operation, never checked is_open/good/flush/close,
// and emitted "port wrote ..." before the ofstream destructor ran. A failed or
// truncated write therefore produced a SUCCESS record and simultaneously blocked any
// retry -- a fabricated evidence record, which is the failure class this campaign
// exists to eliminate. The latch is now published only after a verified write.
// 9F.D38: no default argument. D36 removed acoustic_schedule()'s default for the
// same reason and CI immediately found a caller that had been relying on it -- a
// default is exactly how an omitted argument stops being visible at the call site.
bool dump_advect_u_split(DiagnosticsState& st,
                         const UAdvectionTerms& adv,
                         const std::string& path);

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H
