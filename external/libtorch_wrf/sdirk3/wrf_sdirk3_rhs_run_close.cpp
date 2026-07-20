// PR 9F.5: the whole-run RHS close, exposed to Fortran.
//
// Its own translation unit on purpose. Fortran needs a real linkable symbol for
// BIND(C), so this cannot be a header-only inline; and putting it in the big
// interface TU would force every consumer -- including the torch-free standing
// contract binary -- to link the entire solver just to reach a 20-line function.
//
// Called at the points that actually decide process termination. MEASURED on the
// dt=600 path: SDIRK3_C_ABI_EXCEPTION 0, Fortran "FATAL CALLED" 4, closing record 0
// -- the C++ pre-abort hook and the static destructor are fallbacks for exit classes
// WRF never takes, so the authority is Fortran and this is how it speaks.
//
// Contract: disabled -> 1 (no-op); no begin emitted -> 1 (no-op); valid kind ->
// frozen end record, 1; repeat with the same kind -> the same frozen record, 1;
// conflicting kind -> 0. No exception escapes, no MPI call, no allocation, no mutex
// (workers may run under MPI_THREAD_FUNNELED, and a peer blocked on a lock here
// would never reach its own fatal).
#include "wrf_sdirk3_stage_history_diag.h"

// PR 9F.6 P1-4: explicit run BEGIN, called by Fortran on the main thread after
// config is fixed and before any OpenMP/solver/RHS work. Idempotent per generation:
// the first call (IDLE) opens generation 1; a call while already RUNNING returns 0
// (harmless -- the step subroutine may call it every timestep). After a clean
// finalize (CLOSED) it opens the next generation. Registers the C++ fallbacks once,
// off the per-tick hot path.
//   disabled            -> 1 (no-op)
//   IDLE or CLOSED       -> open new generation, 1
//   already RUNNING      -> 0 (already open; not an error at the call site)
extern "C" int sdirk3_rhs_run_begin_checked() noexcept {
    return wrf::sdirk3::sdirk3_rhs_run_begin_checked_impl();
}

extern "C" int sdirk3_rhs_run_close_checked(int exit_kind, int reason) noexcept {
    if (exit_kind != wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_CLEAN &&
        exit_kind != wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_FATAL) {
        return 0;
    }
    const bool clean = (exit_kind == wrf::sdirk3::SDIRK3_RHS_RUN_EXIT_CLEAN);
    const unsigned r = static_cast<unsigned>(reason);
    // PR 9F.7 P1-4: enforce the (exit, authority, reason) matrix at the Fortran-facing
    // boundary instead of laundering an out-of-range reason to OTHER. The Fortran
    // authority may close ONLY clean-with-finalize or fatal-with-a-fatal-site reason;
    // reason=none, reason=finalize-on-fatal, reason=cpp_fallback (a C++-fallback-only
    // code) and any out-of-range value are rejected with status 0. cpp_preabort /
    // cpp_destructor + cpp_fallback are produced by the C++ fallbacks, not this ABI.
    if (clean) {
        if (r != wrf::sdirk3::SDIRK3_RHS_RUN_REASON_FINALIZE) return 0;
    } else {
        switch (r) {
            case wrf::sdirk3::SDIRK3_RHS_RUN_REASON_STEP_OUTCOME:
            case wrf::sdirk3::SDIRK3_RHS_RUN_REASON_OUTCOME_QUERY:
            case wrf::sdirk3::SDIRK3_RHS_RUN_REASON_HALO:
            case wrf::sdirk3::SDIRK3_RHS_RUN_REASON_SOLVER_STATE:
            case wrf::sdirk3::SDIRK3_RHS_RUN_REASON_TOPOLOGY:
            case wrf::sdirk3::SDIRK3_RHS_RUN_REASON_OTHER:
                break;
            default:
                return 0;   // none / finalize / cpp_fallback / out-of-range
        }
    }
    return wrf::sdirk3::sdirk3_rhs_run_close_impl(
        clean ? wrf::sdirk3::Sdirk3RunExit::Clean
              : wrf::sdirk3::Sdirk3RunExit::Fatal,
        clean ? wrf::sdirk3::Sdirk3RunAuthority::FortranFinalize
              : wrf::sdirk3::Sdirk3RunAuthority::FortranOutcome,
        r);
}
