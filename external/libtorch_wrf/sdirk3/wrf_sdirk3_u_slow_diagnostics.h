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

// 9F.D40 (review section 16): only what the DECLARATIONS need. <fstream>, <sstream>,
// <iostream>, <cctype>, <cstdlib> and <atomic> were carried over from the header-only
// era and now describe a dependency this file does not have -- they moved to the .cpp
// with the code that used them.
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace wrf {
namespace sdirk3 {

// The additive decomposition of the u advective tendency. `horizontal` and `total`
// are DERIVED -- computed from the additive fields, never stored -- so they cannot be
// double-counted in a closure sum. That fact used to live in a string comparison
// against the label "horiz", where renaming the label silently changed the closure's
// meaning.
struct UAdvectionTerms {
    torch::Tensor x, y, vertical;

    // 9F.D40 (review section 6): defined-ness ONLY. Kept as the cheap pre-check, but
    // renamed from the old complete() because "complete" overclaimed: three defined
    // tensors of mismatched shape are not a complete decomposition. x+y BROADCASTS, so
    // {1,nz,nx} + {ny,nz,nx} silently succeeds and yields a horizontal field whose
    // shape then matches vertical -- passing every downstream check and writing a
    // plausible, wrong artifact. Use validate() before trusting the values.
    bool all_defined() const {
        return x.defined() && y.defined() && vertical.defined();
    }
    torch::Tensor horizontal() const { return x + y; }
    torch::Tensor total() const { return x + y + vertical; }

    // Full contract: rank, shape, dtype, device. Throws with the offending property
    // named. Finiteness is checked separately -- it costs a reduction, so it belongs
    // at the dump site (once) rather than on every accessor.
    void validate() const;
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
// 9F.D40 (review section 3/9): WHO produced this record.
//
// D36 moved the dump latch and the record counter from process-global statics into
// per-solver DiagnosticsState. That was right for ownership and WRONG on its own: every
// solver still passed the same hard-coded path, so N solvers each "successfully" wrote
// port_advect_u_split.bin over one another, and each logged success. The process-global
// latch had been accidentally protecting the shared filename; per-solver state removed
// that protection without replacing it. Identity has to be explicit now.
//
// HONESTY ABOUT WHAT IS POPULATED. solver_id / rank / tile / rhs_generation are read
// from real state. physical_step and rk_stage are NOT reachable here: computeUnifiedRHS
// takes (state, RhsMode) and no step or stage is threaded to it -- only the unrelated
// sdirk3_debug_step() entry point ever sees grid%itimestep. Rather than emit a
// plausible-looking 0, they are kUnset and print as "unset". A fabricated 0 in an
// evidence artifact is worse than an absent field: it reads as step zero.
struct DiagnosticContext {
    static constexpr int kUnset = -1;

    std::uint64_t solver_id     = 0;
    int           rank          = kUnset;
    int           tile          = kUnset;
    int           physical_step = kUnset;   // not plumbed yet -- see above
    int           rk_stage      = kUnset;   // not plumbed yet -- see above
    std::uint64_t rhs_generation = 0;

    // Canonical, fixed-order, filesystem-safe. Fixed width so names sort in run order.
    std::string filename_suffix() const;
    // Human-readable, same fixed order, for log records.
    std::string provenance() const;
};

// Process-wide solver identity. The COUNTER is global on purpose -- it exists precisely
// to distinguish solvers within one process, which is the thing per-solver state cannot
// do for itself. It is not run state and never enters a numerical path.
std::uint64_t next_solver_id();

// Best-effort MPI rank; DiagnosticContext::kUnset when MPI is unavailable or not
// initialised. Never throws and never initialises MPI -- a diagnostic must not change
// the process's MPI lifecycle.
int diagnostic_mpi_rank();

struct DiagnosticsState {
    std::uint64_t uterms_record = 0;
    bool split_dump_written = false;
    bool experiment_announced = false;
    std::mutex dump_mutex;
};

// 9F.D40 (review section 11): the announce-once decision as a PURE, testable helper.
// D38 wired experiment_announced correctly but the property "each solver announces
// exactly once, and one solver's announcement does not silence another" lived only
// inside a call site in a 38k-line file, where no fixture could reach it.
// Returns true exactly once per DiagnosticsState.
bool take_experiment_announcement(DiagnosticsState& st);

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
// 9F.D40 (review section 16): this header DECLARES; wrf_sdirk3_u_slow_diagnostics.cpp
// implements. The previous note here argued header-only was deliberate and that moving
// to a .cpp was "the right next step" -- that move happened in D38, and the note
// survived it, describing a layout the file no longer has. A comment that outlives the
// design it describes is worse than none: it is the dependency graph told wrong.
// ---------------------------------------------------------------------------
void emit_u_slow_diagnostics(DiagnosticsState& st,
                             const DiagnosticContext& ctx,
                             const USlowTerms& t);



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
// path_stem gets the context suffix and ".bin" appended -- callers pass a STEM, not a
// full filename, so identity cannot be omitted at a call site.
bool dump_advect_u_split(DiagnosticsState& st,
                         const DiagnosticContext& ctx,
                         const UAdvectionTerms& adv,
                         const std::string& path_stem);

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_U_SLOW_DIAGNOSTICS_H
