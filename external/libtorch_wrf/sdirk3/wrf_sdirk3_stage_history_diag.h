#pragma once
// PR 9E Commit 2: opt-in, diagnosis-only emission of the ARK stage-HISTORY
// provenance and the per-source-stage Newton-defect summary for the record
// stage=2 (converging control) and stage=3 (failing target) operands.
//
// This closes the first two of PR 9E's four closures for the operand:
//   (1) source-stage derivative provenance  — ||k_slow[j]||, ||k_fast[j]|| as
//       they were BORN (captured by the caller at k_slow/k_fast creation; here
//       we only OBSERVE those stored tensors, never re-evaluate any RHS);
//   (2) ARK stage-history assembly — the exact increments
//         dt*aE[i][j]*k_slow[j] + dt*aI[i][j]*k_fast[j]
//       that compose U_stage = U_n + sum_j (...), verified by an FP64 closure
//       against the production-assembled U_stage (must be ~float32 eps).
//
// The per-source-stage defect record answers candidate C (prior-stage Newton
// defect inheritance): whether an un-converged prior stage's residual bleeds
// into stage i's history. newton_defect[j] = K_final[j] - F_fast(U_eval_final[j])
// is the value the Newton solver ALREADY computed on its last accepted
// iteration (R = K - F_fast(U_eval)); this file consumes the observed norms and
// never triggers an extra RHS evaluation.
//
// Non-interference contract (inherited from RwTermCapture / PR 9B and the
// generic StageOperandCapture of Commit 1):
//   - every reduction runs under NoGradGuard on DETACHED tensors, so no node is
//     added to the live autograd graph and no state tensor is mutated;
//   - nothing here touches k_slow / k_fast / U_stage / the residual — it reads
//     copies, so the production trajectory is bit-identical whether this is on
//     or off;
//   - gated by the caller on the cached stage_operand_diag config bool
//     (default off) and single-rank / single-tile topology; with the flag off
//     this header emits nothing and computes nothing.
//
// Spaces: K_norm, f_fast_norm, newton_defect_norm are RAW L2 (one consistent
// space). The solver's own scaled convergence metric (||S^-1 R||_rms) is
// carried alongside as a cross-reference column, NOT mixed into the raw defect.
// The per-block raw/scaled/halo decomposition is Commit 3's job.
#include "wrf_sdirk3_stage_operand_capture.h"
#include "wrf_sdirk3_mpi_safety.h"   // sdirk3_set_pre_abort_hook (PR 9F.3)
#include <torch/torch.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <cerrno>
#include <unistd.h>   // write(2) on the fatal path
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace wrf {
namespace sdirk3 {

// PR 9F P2: every machine-readable Stage-operand line is LINE-ATOMIC. Each line is
// composed in a local ostringstream (so std::scientific / std::setprecision touch
// ONLY the local stream, never the shared std::cerr formatting state) and written
// in ONE call under a process-global mutex, so concurrent emitters (threads/tiles)
// can never interleave characters within a line. This mirrors the solver's
// NEWTON_DIAG / FGMRES_DIAG emit_stage_diag pattern. The mutex is a function-local
// static inside an inline function, so there is exactly ONE instance across every
// translation unit that includes this header.
inline void emit_sdirk3_diag_line(const std::string& line) {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    std::cerr << line;
}

// PR 9F.1: SUCCESS-line TRANSACTION.
//
// PR 9E closed "no success record before the verdict" WITHIN each emitter: both
// emitters buffer their per-source lines and flush only after their own gate. But
// the record stage runs THREE authorities in sequence --
//     emit_stage_history_diag -> validate_stage_defect_tensor -> emit_stage_applied_delta_diag
// -- so the history emitter's success lines reached the log before the defect-tensor
// and applied-delta gates had run. A failure in either later gate therefore left
// SUCCESS-form evidence behind: the same laundering, reopened at STAGE scope.
//
// The fix is a real transaction. Every emitter APPENDS its success lines to this
// bundle instead of writing them; the caller runs ALL authorities first and calls
// commit() only when every one passed. FAILURE markers are NOT buffered -- they must
// appear immediately and are the only thing a rejected stage may leave behind.
struct StageOperandEvidenceBundle {
    std::vector<std::string> success_lines;
    void stage_line(const std::string& line) { success_lines.push_back(line); }
    // Write every buffered success line. Call ONLY after all gates passed.
    void commit() {
        for (const auto& l : success_lines) emit_sdirk3_diag_line(l + "\n");
        success_lines.clear();
    }
    // Drop buffered evidence without writing it (a gate failed).
    void discard() { success_lines.clear(); }
    std::size_t pending() const { return success_lines.size(); }
};

// PR 9F.1: RHS-EVALUATION COUNTER.
//
// The runtime acceptance contract claims "0 extra RHS evaluations when the
// diagnostic is ON", but no production counter existed to prove it: rhs_call_id is
// gated behind debug_level>=2, and jvp_calls counts Newton matvecs (1 or 2
// compute_rhs each, and excluding the base residual, line-search trials and the
// k_slow/predictor evaluations) so it cannot stand in for RHS evaluations.
//
// This counts the single funnel every RHS path goes through.
//
// PR 9F.2 P1-2: the increment is GATED on the same cached flag that gates emission.
// The previous version incremented unconditionally and gated only the printing,
// which meant the DEFAULT production path paid an atomic read-modify-write per RHS
// evaluation. `memory_order_relaxed` removes the ordering fence, not the RMW or the
// cache-line coherence traffic, so with several OpenMP threads evaluating the RHS
// this is real contention on a shared line. The numerics were unchanged, but the
// claim "default path unchanged / no synchronisation cost" was false as written.
// With the gate, an unset WRF_SDIRK3_RHS_COUNT costs one predictable load of a
// function-local static bool and no write of any kind.
// PR 9F.2 P1-2: the increment is GATED on the same cached flag that gates emission.
// The previous version incremented unconditionally and gated only the printing, so
// the DEFAULT production path paid a read-modify-write per RHS evaluation.
//
// PR 9F.2, after Codex review: the counter is THREAD_LOCAL, not a global atomic.
//
// The quantity a sweep wants is "how many RHS evaluations did THIS sweep make", and
// that was never expressible on a process-global counter. RHS evaluations happen at
// many sites outside any sweep scope -- predictor and k_slow bootstrap before the
// sweep opens (impl:5768-5861, 6779-6810), post-solve checks after it closes
// (impl:9828, 10383-10558), and calls from other entry points entirely (impl:34120,
// 37363). With OpenMP tiles those land in whatever sweep another thread happens to
// have open, and no amount of memory ordering reveals them: an unregistered call
// leaves nothing to synchronise with. Three successive fixes (depth sampling, then a
// sequence-counter discriminator, then seq_cst on every participant) each closed one
// interleaving and left this one, because the defect was the shared counter itself.
//
// thread_local makes the property structural. Each thread accumulates only its own
// RHS calls, so a sweep's delta cannot absorb another thread's work -- there is no
// race to reason about, no barrier to get right, and the default path is a plain
// non-atomic load of a cached bool.
inline long long& sdirk3_rhs_call_counter() {
    static thread_local long long counter = 0;
    return counter;
}
inline bool sdirk3_rhs_count_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("WRF_SDIRK3_RHS_COUNT");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return on;
}
// WHOLE-RUN lifecycle as a SINGLE lock-free atomic word (PR 9F.6).
//
// PR 9F.3-9F.5 kept the count, a "frozen" flag, a non-atomic record and a
// "published" flag in FOUR separate atomics, and a loser spun waiting for the
// winner to publish. That is a probabilistic mitigation, not a linearization: if the
// winner is preempted between winning the exchange and publishing, the loser can
// exhaust its bounded spin and return 0 -- ZERO closing records despite tolerating
// duplicates. And because the count and the close were separate atomics with
// separate modification orders, "RHS entries up to the fatal decision" had no
// well-defined stopping time: a tick could advance the counter after the close read
// it.
//
// Everything now lives in one uint64:
//   bits [0..1]   state   0=IDLE 1=RUNNING 2=CLOSED
//   bit  [2]      exit    0=clean 1=fatal          (meaningful once CLOSED)
//   bits [3..4]   authority 0..3                    (meaningful once CLOSED)
//   bit  [5]      post_close_tick -- a tick was seen AFTER the close (a control-flow
//                 violation: the solver ran an RHS past the fatal decision)
//   bits [6..21]  generation (16 bits) -- re-init after a clean finalize
//   bits [22..63] count (42 bits) -- RHS entries this generation
//
// close is a single CAS RUNNING->CLOSED that atomically freezes {count,exit,auth}.
// A loser observes CLOSED and re-decodes the SAME frozen word -- no wait, no
// publication window, byte-identical record. tick and close CAS the same word, so
// they share one modification order and the count has a real linearization point.
enum class Sdirk3RunExit { Clean, Fatal };

// Shared with Fortran via BIND(C).
enum {
    SDIRK3_RHS_RUN_EXIT_CLEAN = 0,
    SDIRK3_RHS_RUN_EXIT_FATAL = 1
};

enum class Sdirk3RunAuthority {
    FortranOutcome,    // Fortran promoted a fail-closed outcome to a process fatal
    FortranFinalize,   // Fortran finalizer completed the normal lifecycle
    CppPreAbort,       // C++ called abort_c_abi_exception directly
    CppDestructor      // last-resort static destructor
};
inline const char* sdirk3_run_authority_name(Sdirk3RunAuthority a) noexcept {
    switch (a) {
        case Sdirk3RunAuthority::FortranOutcome:  return "fortran_outcome";
        case Sdirk3RunAuthority::FortranFinalize: return "fortran_finalize";
        case Sdirk3RunAuthority::CppPreAbort:     return "cpp_preabort";
        default:                                  return "cpp_destructor";
    }
}

// PR 9F.6 P1-5: WHICH fatal site closed the run. authority says the LAYER (Fortran
// vs a C++ fallback); reason says WHY within that layer. Reusing fortran_outcome for
// every Fortran fatal would misattribute a halo/communicator/solver-state fatal as a
// fail-closed step outcome. Shared with Fortran by integer value.
enum {
    SDIRK3_RHS_RUN_REASON_NONE          = 0,  // clean exits carry this
    SDIRK3_RHS_RUN_REASON_STEP_OUTCOME  = 1,  // fail-closed step outcome promoted
    SDIRK3_RHS_RUN_REASON_OUTCOME_QUERY = 2,  // step-outcome query failed
    SDIRK3_RHS_RUN_REASON_FINALIZE      = 3,  // normal finalizer
    SDIRK3_RHS_RUN_REASON_HALO          = 4,  // halo geometry / communicator
    SDIRK3_RHS_RUN_REASON_SOLVER_STATE  = 5,  // solver not initialized / null tile
    SDIRK3_RHS_RUN_REASON_TOPOLOGY      = 6,  // multi-rank / stage-halo rejection
    SDIRK3_RHS_RUN_REASON_OTHER         = 7,  // any other Fortran fatal via the funnel
    SDIRK3_RHS_RUN_REASON_CPP_FALLBACK  = 8   // a C++ fallback closed it
};
inline const char* sdirk3_run_reason_name(unsigned r) noexcept {
    switch (r) {
        case SDIRK3_RHS_RUN_REASON_STEP_OUTCOME:  return "step_outcome";
        case SDIRK3_RHS_RUN_REASON_OUTCOME_QUERY: return "outcome_query";
        case SDIRK3_RHS_RUN_REASON_FINALIZE:      return "finalize";
        case SDIRK3_RHS_RUN_REASON_HALO:          return "halo";
        case SDIRK3_RHS_RUN_REASON_SOLVER_STATE:  return "solver_state";
        case SDIRK3_RHS_RUN_REASON_TOPOLOGY:      return "topology";
        case SDIRK3_RHS_RUN_REASON_CPP_FALLBACK:  return "cpp_fallback";
        case SDIRK3_RHS_RUN_REASON_OTHER:         return "other";
        default:                                  return "none";
    }
}

// ---- packed-word layout ---------------------------------------------------
enum : unsigned {
    SDIRK3_LC_STATE_IDLE    = 0,
    SDIRK3_LC_STATE_RUNNING = 1,
    SDIRK3_LC_STATE_CLOSED  = 2
};
static constexpr unsigned kSdirk3LcAuthShift   = 3;
static constexpr unsigned kSdirk3LcReasonShift = 6;
static constexpr unsigned kSdirk3LcGenShift    = 10;
static constexpr unsigned kSdirk3LcCountShift  = 24;
static constexpr uint64_t kSdirk3LcStateMask   = 0x3ULL;
static constexpr uint64_t kSdirk3LcExitBit     = 1ULL << 2;
static constexpr uint64_t kSdirk3LcAuthMask    = 0x3ULL;
static constexpr uint64_t kSdirk3LcPostBit     = 1ULL << 5;
static constexpr uint64_t kSdirk3LcReasonMask  = 0xFULL;          // 4 bits
static constexpr uint64_t kSdirk3LcGenMask     = 0x3FFFULL;       // 14 bits
static constexpr uint64_t kSdirk3LcCountMask   = (1ULL << 40) - 1ULL;
static constexpr uint64_t kSdirk3LcCountOne    = 1ULL << kSdirk3LcCountShift;

inline std::atomic<uint64_t>& sdirk3_run_lifecycle() {
    static std::atomic<uint64_t> word{0};   // IDLE, gen 0, count 0
    return word;
}
inline unsigned  sdirk3_lc_state(uint64_t w) noexcept { return static_cast<unsigned>(w & kSdirk3LcStateMask); }
inline long long sdirk3_lc_count(uint64_t w) noexcept { return static_cast<long long>((w >> kSdirk3LcCountShift) & kSdirk3LcCountMask); }
inline long long sdirk3_lc_gen  (uint64_t w) noexcept { return static_cast<long long>((w >> kSdirk3LcGenShift) & kSdirk3LcGenMask); }
inline bool      sdirk3_lc_exit_fatal(uint64_t w) noexcept { return (w & kSdirk3LcExitBit) != 0; }
inline Sdirk3RunAuthority sdirk3_lc_auth(uint64_t w) noexcept {
    return static_cast<Sdirk3RunAuthority>((w >> kSdirk3LcAuthShift) & kSdirk3LcAuthMask);
}
inline bool sdirk3_lc_post_close_tick(uint64_t w) noexcept { return (w & kSdirk3LcPostBit) != 0; }
inline unsigned sdirk3_lc_reason(uint64_t w) noexcept {
    return static_cast<unsigned>((w >> kSdirk3LcReasonShift) & kSdirk3LcReasonMask);
}
inline uint64_t sdirk3_lc_pack(unsigned state, long long gen, long long count,
                               Sdirk3RunExit how, Sdirk3RunAuthority who,
                               bool post, unsigned reason) noexcept {
    uint64_t w = static_cast<uint64_t>(state) & kSdirk3LcStateMask;
    if (how == Sdirk3RunExit::Fatal) w |= kSdirk3LcExitBit;
    w |= (static_cast<uint64_t>(who) & kSdirk3LcAuthMask) << kSdirk3LcAuthShift;
    if (post) w |= kSdirk3LcPostBit;
    w |= (static_cast<uint64_t>(reason) & kSdirk3LcReasonMask) << kSdirk3LcReasonShift;
    w |= (static_cast<uint64_t>(gen)   & kSdirk3LcGenMask)   << kSdirk3LcGenShift;
    w |= (static_cast<uint64_t>(count) & kSdirk3LcCountMask) << kSdirk3LcCountShift;
    return w;
}

inline bool sdirk3_rhs_count_enabled();  // fwd (defined above)

// The live count of the current (or last) generation, for diagnostics/tests.
inline long long sdirk3_rhs_run_total() {
    return sdirk3_lc_count(sdirk3_run_lifecycle().load(std::memory_order_acquire));
}

inline void sdirk3_run_register_fallbacks_once();  // fwd

// Emit the opening record for a generation. Not on the fatal path, so the buffered
// mutex-guarded emitter is fine. begin carries NEITHER exit NOR authority. Also the
// one place the C++ fallbacks are registered -- once per generation open, OFF the
// per-tick hot path (PR 9F.6 P1-4).
inline void sdirk3_run_emit_begin(long long gen) noexcept {
    sdirk3_run_register_fallbacks_once();
    try {
        std::ostringstream os;
        os << "SDIRK3_RHS_RUN_TOTAL phase=begin generation=" << gen << " total=0";
        emit_sdirk3_diag_line(os.str() + "\n");
    } catch (...) {
    }
}

// Emit the closing record from a FROZEN word with a single low-level write(2).
// fwrite/fprintf take an internal stdio lock; write(2) to STDERR_FILENO does not, so
// this is safe on the fatal path. Reports write failure honestly (P1-6): EINTR is
// retried, and a short or failed write returns 0 rather than a false success.
inline int sdirk3_run_emit_end(uint64_t w) noexcept {
    char buf[224];
    const int n = std::snprintf(
        buf, sizeof buf,
        "SDIRK3_RHS_RUN_TOTAL phase=end generation=%lld total=%lld exit=%s "
        "authority=%s reason=%s%s\n",
        sdirk3_lc_gen(w), sdirk3_lc_count(w),
        sdirk3_lc_exit_fatal(w) ? "fatal" : "clean",
        sdirk3_run_authority_name(sdirk3_lc_auth(w)),
        sdirk3_run_reason_name(sdirk3_lc_reason(w)),
        sdirk3_lc_post_close_tick(w) ? " post_close_tick=1" : "");
    if (n <= 0) return 0;
    const size_t len = static_cast<size_t>(n) < sizeof buf
                           ? static_cast<size_t>(n) : sizeof buf - 1;
    size_t off = 0;
    while (off < len) {
        const ssize_t wr = ::write(STDERR_FILENO, buf + off, len - off);
        if (wr < 0) {
            if (errno == EINTR) continue;
            return 0;                       // real write failure -> fail-close
        }
        if (wr == 0) return 0;              // no progress -> fail-close
        off += static_cast<size_t>(wr);
    }
    return 1;
}

// Open a generation. Contract: IDLE or CLOSED -> RUNNING(gen+1, count=0), 1;
// RUNNING -> 0 (double begin without a close is a violation); disabled -> 1 no-op.
inline int sdirk3_rhs_run_begin_checked_impl() noexcept {
    if (!sdirk3_rhs_count_enabled()) return 1;
    std::atomic<uint64_t>& word = sdirk3_run_lifecycle();
    uint64_t old = word.load(std::memory_order_acquire);
    for (;;) {
        const unsigned st = sdirk3_lc_state(old);
        if (st == SDIRK3_LC_STATE_RUNNING) return 0;   // already open
        const long long gen = sdirk3_lc_gen(old) + 1;
        const uint64_t neu = sdirk3_lc_pack(SDIRK3_LC_STATE_RUNNING, gen, 0,
                                            Sdirk3RunExit::Clean,
                                            Sdirk3RunAuthority::FortranOutcome, false,
                                            SDIRK3_RHS_RUN_REASON_NONE);
        if (word.compare_exchange_weak(old, neu, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
            sdirk3_run_emit_begin(gen);
            return 1;
        }
    }
}

// Count one RHS entry. RUNNING -> count+1 (1). CLOSED -> set post_close_tick and
// return 0 (the "tick after close" violation the linearized machine exists to
// catch). IDLE -> lazily open a generation (begin record emitted) and count 1; this
// is the transitional lazy-begin -- PR 9F.6 commit 2 replaces it with an explicit
// Fortran begin, after which an IDLE tick would itself be a wiring bug.
inline int sdirk3_run_tick() noexcept {
    std::atomic<uint64_t>& word = sdirk3_run_lifecycle();
    uint64_t old = word.load(std::memory_order_acquire);
    for (;;) {
        const unsigned st = sdirk3_lc_state(old);
        if (st == SDIRK3_LC_STATE_RUNNING) {
            const uint64_t neu = old + kSdirk3LcCountOne;   // count is the top field
            if (word.compare_exchange_weak(old, neu, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
                return 1;
        } else if (st == SDIRK3_LC_STATE_CLOSED) {
            if (sdirk3_lc_post_close_tick(old)) return 0;   // already flagged
            const uint64_t neu = old | kSdirk3LcPostBit;
            word.compare_exchange_weak(old, neu, std::memory_order_acq_rel,
                                       std::memory_order_acquire);
            return 0;
        } else {  // IDLE -> lazy begin
            const long long gen = sdirk3_lc_gen(old) + 1;
            const uint64_t neu = sdirk3_lc_pack(SDIRK3_LC_STATE_RUNNING, gen, 1,
                                                Sdirk3RunExit::Clean,
                                                Sdirk3RunAuthority::FortranOutcome,
                                                false, SDIRK3_RHS_RUN_REASON_NONE);
            if (word.compare_exchange_weak(old, neu, std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
                sdirk3_run_emit_begin(gen);
                return 1;
            }
        }
    }
}

// Close the run. RUNNING -> single CAS to CLOSED that freezes {count,exit,auth};
// the winner emits. CLOSED with the same exit -> re-emit the identical frozen record
// (duplicate close tolerated). CLOSED with a different exit -> 0 (conflict). IDLE ->
// 1 no-op (no begin, nothing to close). No spin, no publication window.
inline int sdirk3_rhs_run_close_impl(Sdirk3RunExit how,
                                     Sdirk3RunAuthority who,
                                     unsigned reason) noexcept {
    if (!sdirk3_rhs_count_enabled()) return 1;
    std::atomic<uint64_t>& word = sdirk3_run_lifecycle();
    uint64_t old = word.load(std::memory_order_acquire);
    for (;;) {
        const unsigned st = sdirk3_lc_state(old);
        if (st == SDIRK3_LC_STATE_IDLE) return 1;   // no begin -> benign no-op
        if (st == SDIRK3_LC_STATE_RUNNING) {
            const uint64_t neu = sdirk3_lc_pack(
                SDIRK3_LC_STATE_CLOSED, sdirk3_lc_gen(old), sdirk3_lc_count(old),
                how, who, sdirk3_lc_post_close_tick(old), reason);
            if (word.compare_exchange_weak(old, neu, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
                return sdirk3_run_emit_end(neu);   // I froze it
            continue;
        }
        // CLOSED
        if (sdirk3_lc_exit_fatal(old) != (how == Sdirk3RunExit::Fatal))
            return 0;                                // conflicting outcome
        return sdirk3_run_emit_end(old);             // duplicate: identical record
    }
}

inline void sdirk3_rhs_run_emit_end_once(Sdirk3RunExit how) noexcept {
    sdirk3_rhs_run_close_impl(how, how == Sdirk3RunExit::Clean
                                       ? Sdirk3RunAuthority::CppDestructor
                                       : Sdirk3RunAuthority::CppPreAbort,
                              SDIRK3_RHS_RUN_REASON_CPP_FALLBACK);
}
inline void sdirk3_rhs_run_emit_end_fatal() noexcept {
    sdirk3_rhs_run_emit_end_once(Sdirk3RunExit::Fatal);
}

// Clean-exit and C++-direct-abort FALLBACKS. Neither is the authority: WRF
// terminates through Fortran's wrf_error_fatal, which reaches neither. Registered
// once, lazily, off the fatal path.
struct Sdirk3RhsRunTotalEmitter {
    ~Sdirk3RhsRunTotalEmitter() { sdirk3_rhs_run_emit_end_once(Sdirk3RunExit::Clean); }
};
inline void sdirk3_run_register_fallbacks_once() {
    static Sdirk3RhsRunTotalEmitter emitter;   // clean-exit fallback (destructor)
    static const bool hooked = [] {
        mpi_safety::sdirk3_set_pre_abort_hook(&sdirk3_rhs_run_emit_end_fatal);
        return true;
    }();
    (void)hooked;
}

// The ONLY place the counters advance. Sweep counter is thread-local; the whole-run
// tick is the lifecycle CAS above. Disabled path writes nothing.
inline void sdirk3_rhs_count_tick() {
    if (!sdirk3_rhs_count_enabled()) return;
    ++sdirk3_rhs_call_counter();          // sweep-local, thread-private
    sdirk3_run_tick();                     // whole-run lifecycle (registers on begin)
}
inline long long sdirk3_rhs_count_value() {
    return sdirk3_rhs_call_counter();
}

// Sweep identifier. This one stays GLOBAL and atomic, because its only job is to
// make record keys unique across threads so the harness can pair begin/end and
// reject duplicates. It carries no part of the attribution argument any more.
inline long long sdirk3_next_rhs_sweep_seq() {
    static std::atomic<long long> seq{0};
    return seq.fetch_add(1, std::memory_order_relaxed);
}

// Same-thread sweep nesting. Cross-thread contamination is now impossible by
// construction, but a sweep opened inside another sweep ON THE SAME THREAD would
// still make the inner calls count twice. unifiedStep is not re-entrant, so this is
// defence-in-depth rather than a known-reachable path -- and it is thread_local, so
// it costs a non-atomic increment and only when counting is enabled.
inline int& sdirk3_rhs_sweep_depth() {
    static thread_local int depth = 0;
    return depth;
}

// Was this sweep's delta contaminated? With a thread-private counter the only
// remaining way is same-thread nesting, which both samples below detect: a non-zero
// prior depth at entry, or a depth other than 1 at exit. Kept as a PURE function of
// sampled values so the standing contracts can drive every case directly -- the
// enabled-path cache initialises false in the unit-test process and cannot be
// flipped, so a predicate reachable only through the live scope would be untestable
// in that binary.
// Thread-local count of sweeps STARTED on this thread. An outer sweep records it at
// entry and compares at exit: if it moved, some inner sweep began during our
// lifetime and its RHS calls are inside our delta -- even if that inner sweep has
// already finished, which is exactly the case the depth samples cannot see (depth
// goes 1 -> 2 -> 1 and both of our samples read 1).
inline long long& sdirk3_rhs_sweep_epoch() {
    static thread_local long long epoch = 0;
    return epoch;
}

// Was this sweep's delta contaminated?
//   nested_at_entry  - a sweep was already open on this thread when we started
//   depth_at_exit    - an inner sweep is STILL open as we finish
//   epoch moved      - an inner sweep began during our lifetime and already ended
// The third term is what makes the OUTER record truthful. Without it the outer row
// reads clean while containing the inner sweep's calls, and any analyser that
// consumes rows individually -- or that drops only tainted rows -- would use a
// contaminated delta.
inline bool sdirk3_sweep_overlapped(bool nested_at_entry, int depth_at_exit,
                                    long long epoch_at_entry, long long epoch_at_exit) {
    return nested_at_entry
        || depth_at_exit != 1
        || epoch_at_exit != epoch_at_entry;
}

// Emit one record in a stable, machine-readable, line-atomic form.
inline void emit_sdirk3_rhs_count(const char* phase, long long sweep_seq,
                                  long long total, long long delta,
                                  bool concurrent = false) {
    if (!sdirk3_rhs_count_enabled()) return;
    std::ostringstream os;
    os << "SDIRK3_RHS_CALLS phase=" << phase
       << " sweep_seq=" << sweep_seq
       << " total=" << total;
    if (delta >= 0) os << " delta=" << delta;
    if (concurrent) os << " concurrent=1";
    emit_sdirk3_diag_line(os.str() + "\n");
}

// PR 9F.2 P1-1: a sweep's begin record alone cannot prove "the diagnostic added zero
// RHS evaluations" — an extra call made AFTER the last begin, in a run that then
// ends, is never reflected in any emitted record, so the OFF/ON sequences match and
// the gate passes while the property is violated. The end record must therefore be
// emitted on EVERY exit from the sweep: normal completion, stage abort, recoverable
// stage failure, and stack unwinding from a thrown C++ exception. A destructor is
// the only construct that covers all four, so the pairing is RAII rather than a
// hand-placed call that a future early-return could bypass.
class Sdirk3RhsSweepScope {
public:
    // EVERY state access below is behind enabled_, a cached function-local static
    // bool. With WRF_SDIRK3_RHS_COUNT unset this object constructs and destructs
    // without touching the counter or the depth at all. An earlier version did the
    // depth increment unconditionally in the member-init list, re-introducing on a
    // per-sweep basis exactly the default-path cost that P1-2 had just removed on a
    // per-RHS-call basis -- the short-circuit && is what prevents that, so do not
    // rewrite it into a statement that evaluates the increment first.
    Sdirk3RhsSweepScope()
        : enabled_(sdirk3_rhs_count_enabled()),
          seq_(enabled_ ? sdirk3_next_rhs_sweep_seq() : -1),
          start_(enabled_ ? sdirk3_rhs_count_value() : 0),
          // Non-zero prior depth means a sweep is already open ON THIS THREAD.
          nested_(enabled_ && sdirk3_rhs_sweep_depth()++ != 0),
          // Sampled AFTER the ++ above, so our own start is not counted as an inner
          // sweep beginning during our lifetime.
          epoch_(enabled_ ? ++sdirk3_rhs_sweep_epoch() : 0) {
        if (enabled_) emit_sdirk3_rhs_count("begin", seq_, start_, -1, nested_);
    }
    ~Sdirk3RhsSweepScope() {
        if (!enabled_) return;   // default path: nothing was acquired, nothing to release
        // noexcept context: emission must not throw out of a destructor. A swallowed
        // failure leaves the `end` record absent, which the harness reports as
        // missing_end and fails closed on -- silence here degrades to a FAILING gate,
        // never to a passing one.
        try {
            // Both reads are thread-private, so there is no interleaving to order
            // against and no barrier to get right -- the property the previous three
            // fixes kept failing to establish is now structural.
            const long long total = sdirk3_rhs_count_value();
            const bool overlapped =
                sdirk3_sweep_overlapped(nested_, sdirk3_rhs_sweep_depth(),
                                        epoch_, sdirk3_rhs_sweep_epoch());
            emit_sdirk3_rhs_count("end", seq_, total, total - start_, overlapped);
        } catch (...) {
        }
        --sdirk3_rhs_sweep_depth();
    }
    Sdirk3RhsSweepScope(const Sdirk3RhsSweepScope&) = delete;
    Sdirk3RhsSweepScope& operator=(const Sdirk3RhsSweepScope&) = delete;
private:
    bool enabled_;        // declared first: the others' initializers read it
    long long seq_;
    long long start_;
    bool nested_;
    long long epoch_;
};

// Route one success line either into the transaction (preferred) or straight out
// (legacy/no-bundle callers, and the standing contract tests).
inline void stage_success_line(StageOperandEvidenceBundle* bundle,
                               const std::string& line) {
    if (bundle) bundle->stage_line(line);
    else emit_sdirk3_diag_line(line + "\n");
}

// Canonical "not applicable" sentinel for the Newton-convergence fields of an
// EXPLICIT ESDIRK stage. An explicit stage runs NO Newton solve, so f_fast /
// newton_defect / ratio / scaled_final_residual are not merely unobserved but
// UNDEFINED. The record carries this fixed sentinel (independent of k_norm), and
// the validator requires it EXACTLY -- so it can never be a manufactured
// "F_fast == K" that makes the explicit check self-fulfilling (P1-1). A real
// norm is >= 0, so -1.0 is unreachable by observation and unambiguous.
inline constexpr double kDefectNA = -1.0;

// One prior stage's convergence quality, snapshotted AT that stage's own
// completion (so it is that stage's value, not a later "last_stage_*" overwrite).
// All norms observe production tensors; nothing is recomputed.
//
// EXPLICIT stage (ESDIRK stage 1): convergence is NOT APPLICABLE. There is no
// Newton solve, hence no F_fast, no defect, and no "converged" verdict to report.
// Such a record MUST set convergence_applicable=false, the four Newton fields to
// kDefectNA, and converged=false (canonical -- it must NOT copy the solver's
// last_stage_converged_, which belongs to a DIFFERENT, implicit stage/sweep).
// The machine-readable line prints `convergence_applicable=0` and omits
// `converged`. This replaces the earlier synthetic f_fast_norm=k_norm /
// newton_defect_norm=0 record, whose validator check (f_fast ~= k_norm,
// defect == 0) merely re-read values the caller had just forced equal.
struct StageDefectSnapshot {
    int stage = -1;                    // 1-based source stage id
    double k_norm = 0.0;               // ||k_fast[stage-1]||  (= ||K_final||), raw L2
    // The four Newton-convergence fields default to kDefectNA, NOT 0.0: a
    // default-constructed / never-populated IMPLICIT snapshot must FAIL the
    // inventory (kDefectNA is negative -> neg_* / n-a-mismatch) rather than pass
    // silently on a 0.0 that reads as a legitimate zero defect. Populated records
    // overwrite these (explicit -> kDefectNA sentinel; implicit -> observed >= 0).
    double f_fast_norm = kDefectNA;         // ||F_fast(U_eval_final)||, raw L2 (kDefectNA = n/a)
    double newton_defect_norm = kDefectNA;  // ||K_final - F_fast(U_eval_final)||, raw L2 (kDefectNA = n/a)
    double defect_to_k_ratio = kDefectNA;   // newton_defect_norm / max(k_norm, tiny) (kDefectNA = n/a)
    double scaled_final_residual = kDefectNA; // solver ||S^-1 R||_rms cross-reference (kDefectNA = n/a)
    bool converged = false;            // solver-reported convergence (implicit stages only)
    bool explicit_stage = false;       // ESDIRK first stage: convergence NOT APPLICABLE
};

// Where the {K, F, R} triple was observed. Unobserved is the default -- a defect
// record that never captured a residual evaluation for the RETURNED stage value
// fails closed rather than reusing a stale/foreign snapshot.
enum class DefectEvaluationPoint { Unobserved = 0, ResidualEval = 1 };

// The COHERENT Newton-defect tensors for ONE implicit stage solve, captured at a
// single residual evaluation (K, F=F_fast(K), R=K-F, all detached at the same
// point). `returned_K` is the stage value the caller actually returned/used; the
// authority check requires K == returned_K so the F/R provably belong to the
// returned evaluation point and not a later trust-region/step update. This carries
// TENSORS (not caller scalars) so the emitter is the norm authority.
struct StageDefectTensorSnapshot {
    int stage = -1;
    int retry_generation = -1;
    int newton_iter = -1;
    DefectEvaluationPoint point = DefectEvaluationPoint::Unobserved;
    torch::Tensor K;           // implicit stage value at the residual evaluation
    torch::Tensor F;           // F_fast(K) at that evaluation
    torch::Tensor R;           // K - F at that evaluation (coherent by construction)
    torch::Tensor returned_K;  // the stage value the SOLVER returned (must equal K)
    // PR 9F.1: the stage derivative the ARK history actually consumes, i.e.
    // k_fast[stage-1] AFTER every post-solve transform. The Newton defect is
    // evaluated at returned_K, which is captured BEFORE those transforms, so these
    // are different objects whenever a transform fires. Supplying it lets the
    // emitter MEASURE the gap instead of silently mixing the two.
    torch::Tensor final_k;
};

// PR 9F.1: the AUTHORITATIVE per-source defect numbers, derived BY THE EMITTER from
// the coherent {K,F,R} tensors -- never from the caller's scalars.
//
// Before this, validate_stage_defect_tensor computed ||K-F-R||, ||R|| and the ratio
// and then the caller DISCARDED them (it passed no output pointers), while the
// success summary printed StageDefectSnapshot's scalar fields. The gate and the
// report were therefore different numbers: a wrong-but-finite-and-positive scalar
// defect passed the scalar inventory and got printed even though the tensor
// authority never endorsed it. These fields are what the summary must publish; the
// scalars are demoted to cross-checked telemetry.
// GENERATION / ITERATION FIELDS ARE IDENTITY METADATA, NOT A STALENESS AUTHORITY.
// What is enforced: both must be PRESENT (>= 0) for every implicit source, checked
// by validate_derived_defect_inventory -- a defaulted -1 means the snapshot carries
// no evaluation-point identity and the record is rejected. Both are published in the
// summary so a consumer can see which attempt/iteration produced the triple.
// What is NOT claimed: they are never compared against an independently-known
// expected generation, so they do NOT by themselves prove "this is not a stale retry".
// The property that actually excludes a stale/foreign triple is the exact
// correspondence gate ||K - returned_K|| == 0 plus the per-solve reset of the
// captured tensors -- not these integers. Do not cite them as retry authority.
struct DerivedDefectRecord {
    int stage = -1;
    int retry_generation = -1;   // identity: which solve attempt produced the triple
    int newton_iter = -1;        // identity: which Newton iteration was evaluated
    double k_norm = kDefectNA;      // ||K||   at the residual evaluation
    double f_norm = kDefectNA;      // ||F||   at that evaluation
    double defect_norm = kDefectNA; // ||R||
    double ratio = kDefectNA;       // ||R|| / max(||K||, eps)
    double closure = kDefectNA;     // ||K - F - R||  (exactly 0 when coherent)
    bool observed = false;          // true ONLY after every tensor gate passed
    // PR 9F.1 evaluation-point semantics. The defect is evaluated at the SOLVER's
    // returned K (pre-postprocess). The ARK history consumes k_fast AFTER the
    // post-solve transforms. postprocess_delta_max = ||final_k - returned_K||_inf
    // measures the gap; postprocess_applied says whether a transform actually fired.
    // Publishing both makes the boundary explicit instead of mixing a pre-transform
    // defect with a post-transform k_norm in one record.
    double postprocess_delta_max = kDefectNA;
    bool postprocess_applied = false;
    bool final_k_observed = false;
};

// One source stage's contribution to the target stage's ARK history. k_slow /
// k_fast MUST point at that source stage's BIRTH-TIME immutable snapshot (a
// detached clone taken when the derivative was finalized), NOT the live vector
// element (P1-3). Consuming the birth snapshot means the recorded provenance is
// the value the derivative had WHEN BORN; if a future in-place mutation ever
// diverged the live derivative from its birth value before the production ARK
// add, the closure / applied-delta gates would catch it (birth != applied delta),
// so the immutability is structurally enforced rather than assumed.
struct StageHistorySource {
    int stage = -1;                    // 1-based source stage id (j+1)
    double a_explicit = 0.0;           // aE[target-1][j]
    double a_implicit = 0.0;           // aI[target-1][j]
    const torch::Tensor* k_slow = nullptr; // BIRTH snapshot (immutable), not owned
    const torch::Tensor* k_fast = nullptr; // BIRTH snapshot (immutable), not owned
    // IDENTITY METADATA ONLY (PR 9F.1). Stamped when the derivative was born and
    // published for traceability. It is NOT validated: no >=1 check, no ordering /
    // uniqueness / monotonicity check, and no re-birth-after-retry check. What
    // actually protects the provenance is that the source consumes a DETACHED BIRTH
    // CLONE, so a later mutation cannot rewrite it (and would be caught by the
    // closure / applied-delta gates). Do not cite this integer as an authority.
    int birth_generation = -1;
                                       // (kept LAST so positional aggregate inits
                                       //  {stage, aE, aI, k_slow, k_fast} still work)
};

// A packed six-block boundary (ru/rv/rw/ph/t/mu), for per-block localization of
// an attribution failure.
struct StageOperandBlock {
    const char* name = "";
    int64_t start = 0;
    int64_t size = 0;
};

// Structural preflight (PR 9E history-authority P1-5): every packed operand must
// be a defined, non-empty, 1-D tensor of the SAME shape / device / dtype as the
// reference. This runs BEFORE any norm / multiply / subtract, which would
// otherwise raise a generic (non-Stage-operand) LibTorch exception on a
// dimension/dtype/device mismatch -- or, worse, SILENTLY BROADCAST a wrong shape.
// Returns "" when sound, else a stable marker+detail:
//   SDIRK3_STAGE_OPERAND_SHAPE_MISMATCH / _DEVICE_MISMATCH / _DTYPE_MISMATCH.
inline std::string check_stage_operand_structure(
    const torch::Tensor& ref, const char* ref_name,
    const std::vector<std::pair<const char*, const torch::Tensor*>>& operands) {
    if (!ref.defined() || ref.numel() == 0 || ref.dim() != 1)
        return std::string("SDIRK3_STAGE_OPERAND_SHAPE_MISMATCH: ") + ref_name +
               " (must be a defined non-empty 1-D packed tensor)";
    for (const auto& kv : operands) {
        const char* name = kv.first;
        const torch::Tensor* t = kv.second;
        // A null/undefined operand is a COMPLETENESS concern, left to the caller's
        // inventory / undefined-source check (which emits CAPTURE_INCOMPLETE or an
        // undefined-source fail). The structural preflight validates the
        // shape/device/dtype of the DEFINED operands, which is what would
        // otherwise raise a generic LibTorch throw or silently broadcast.
        if (!t || !t->defined()) continue;
        if (t->numel() == 0 || t->dim() != 1 || t->sizes() != ref.sizes())
            return std::string("SDIRK3_STAGE_OPERAND_SHAPE_MISMATCH: ") + name;
        if (t->device() != ref.device())
            return std::string("SDIRK3_STAGE_OPERAND_DEVICE_MISMATCH: ") + name;
        if (t->scalar_type() != ref.scalar_type())
            return std::string("SDIRK3_STAGE_OPERAND_DTYPE_MISMATCH: ") + name;
    }
    return std::string();
}

// AUTHORITATIVE Newton-defect coherence check (PR 9F, P1-4). The scalar defect
// telemetry (final_defect_l2_raw etc.) is computed by the solver and is NOT an
// independent authority: nothing proves the stored F/R were evaluated at the K the
// solve actually returned (a trust-region/recovery step can update K after F/R were
// stored). This check derives every number DIRECTLY from the captured tensors and
// pins them to the returned stage value:
//   - point must be ResidualEval (an Unobserved record -> DEFECT_UNOBSERVED)
//   - K/F/R/returned_K structurally sound (defined, 1-D, same shape/device/dtype)
//   - ||K - returned_K|| == 0 EXACTLY: the F/R belong to the RETURNED evaluation
//     point (else DEFECT_UNOBSERVED -- the returned K was never residual-evaluated)
//   - ||K - F - R|| == 0 EXACTLY: the triple is internally coherent (R was defined
//     as K-F in the same float32 evaluation, so this is bit-exact, not a tolerance)
// Outputs (optional) the tensor-derived defect ||R||, closure, and ratio ||R||/||K||
// for the caller to cross-check against its scalar telemetry. Returns "" when
// coherent, else a stable marker string.
inline std::string validate_stage_defect_tensor(
    const StageDefectTensorSnapshot& s,
    double* out_defect_norm = nullptr,
    double* out_closure = nullptr,
    double* out_ratio = nullptr,
    DerivedDefectRecord* out_record = nullptr) {
    if (out_defect_norm) *out_defect_norm = -1.0;
    if (out_closure) *out_closure = -1.0;
    if (out_ratio) *out_ratio = -1.0;
    if (out_record) {
        *out_record = DerivedDefectRecord{};
        out_record->stage = s.stage;
        out_record->retry_generation = s.retry_generation;
        out_record->newton_iter = s.newton_iter;
    }
    const std::string sid = "s" + std::to_string(s.stage);
    if (s.point != DefectEvaluationPoint::ResidualEval)
        return "SDIRK3_STAGE_OPERAND_DEFECT_UNOBSERVED: no residual evaluation captured (" + sid + ")";
    std::string sr = check_stage_operand_structure(
        s.K, "defect_K",
        {{"defect_F", &s.F}, {"defect_R", &s.R}, {"returned_K", &s.returned_K}});
    if (!sr.empty()) return sr;
    torch::NoGradGuard no_grad;
    // Correspondence + coherence in the NATIVE float32 the solve used: R was
    // defined as K-F in float32, so both differences are bit-exact zero when sound.
    double k_return_mismatch = (s.K - s.returned_K).abs().max().item<double>();
    if (!std::isfinite(k_return_mismatch) || k_return_mismatch > 0.0)
        return "SDIRK3_STAGE_OPERAND_DEFECT_UNOBSERVED: captured K != returned K (" + sid + ")";
    double closure = (s.K - s.F - s.R).abs().max().item<double>();
    // norms in float64 for a faithful report (does not affect the exact gates).
    double defect = s.R.to(torch::kFloat64).norm().item<double>();
    double kn = s.K.to(torch::kFloat64).norm().item<double>();
    double fn = s.F.to(torch::kFloat64).norm().item<double>();
    double ratio = defect / std::max(kn, 1e-300);
    if (out_defect_norm) *out_defect_norm = defect;
    if (out_closure) *out_closure = closure;
    if (out_ratio) *out_ratio = ratio;
    if (!std::isfinite(closure) || !std::isfinite(defect) || !std::isfinite(ratio) ||
        !std::isfinite(kn) || !std::isfinite(fn))
        return "SDIRK3_STAGE_OPERAND_DEFECT_NONFINITE: " + sid;
    if (closure > 0.0)
        return "SDIRK3_STAGE_OPERAND_DEFECT_INCOHERENT: ||K-F-R|| != 0 (" + sid + ")";
    // PR 9F.1: MEASURE the pre/post-postprocess boundary rather than mixing it. The
    // defect above is evaluated at the solver-returned K; the ARK history consumes
    // final_k (k_fast after the post-solve transforms). If a transform fired, these
    // differ, and the record must say so explicitly.
    double post_delta = kDefectNA;
    bool post_applied = false;
    bool final_k_seen = false;
    if (s.final_k.defined() && s.final_k.numel() > 0) {
        std::string fr = check_stage_operand_structure(s.K, "defect_K",
                                                       {{"final_k", &s.final_k}});
        if (!fr.empty()) return fr;
        post_delta = (s.final_k - s.returned_K).abs().max().item<double>();
        if (!std::isfinite(post_delta))
            return "SDIRK3_STAGE_OPERAND_DEFECT_NONFINITE: postprocess_delta (" + sid + ")";
        post_applied = (post_delta > 0.0);
        final_k_seen = true;
    }
    if (out_record) {
        out_record->k_norm = kn;
        out_record->f_norm = fn;
        out_record->defect_norm = defect;
        out_record->ratio = ratio;
        out_record->closure = closure;
        out_record->postprocess_delta_max = post_delta;
        out_record->postprocess_applied = post_applied;
        out_record->final_k_observed = final_k_seen;
        out_record->observed = true;   // set ONLY after every gate above passed
    }
    return std::string();
}

// AUTHORITATIVE inventory over the DERIVED (tensor-backed) defect records, PR 9F.1.
// validate_stage_defect_tensor only judges records that EXIST, so a silently missing
// tensor snapshot was invisible: the scalar table could be complete while the tensor
// evidence was not. This derives the expected set from `target_stage` (the same
// independent authority the scalar inventory uses) and requires every IMPLICIT source
// stage 2..target_stage-1 to have exactly one OBSERVED derived record. Stage 1 is the
// explicit ESDIRK stage: convergence is not applicable, so it must NOT appear.
// Returns "" when sound, else a comma-joined reason for
// SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE.
inline std::string validate_derived_defect_inventory(
    const std::vector<DerivedDefectRecord>& derived, int target_stage) {
    std::string reason;
    auto append = [&](const std::string& r) {
        if (!reason.empty()) reason += ",";
        reason += r;
    };
    for (int j = 2; j < target_stage; ++j) {
        int count = 0;
        for (const auto& d : derived) if (d.stage == j) ++count;
        if (count == 0) append("tensor_missing:s" + std::to_string(j));
        if (count > 1) append("tensor_duplicate:s" + std::to_string(j));
    }
    for (const auto& d : derived) {
        const std::string sid = "s" + std::to_string(d.stage);
        if (d.stage < 2 || d.stage >= target_stage)
            append("tensor_unknown_stage:" + sid);
        if (!d.observed) append("tensor_unobserved:" + sid);
        // Evaluation-point identifiers must be real, not defaulted -- otherwise a
        // stale/foreign snapshot could not be told apart from a fresh one.
        if (d.retry_generation < 0) append("tensor_no_retry_generation:" + sid);
        if (d.newton_iter < 0) append("tensor_no_newton_iter:" + sid);
    }
    return reason;
}

// AUTHORITATIVE per-source ARK attribution check (PR 9E history-authority C1).
// The aggregate history closure proves only that the reconstructed TOTAL matches
// U_stage - U_n; it cannot catch a +delta/-delta compensation between two sources
// or a source-label swap, and reconstructing the history in FP64 does not verify
// the real FP32 production rounding. This check instead re-applies each source's
// intended increment with the IDENTICAL FP32 expression production used, on top of
// the captured running state `states[j-1]` (U_n for j=0), and asserts it lands on
// the captured production state `states[j]`:
//     states[j-1] + dt*aE*k_slow[j] + dt*aI*k_fast[j]  ==  states[j]   (per element)
// For a correct attribution this is BIT-EXACT (same inputs, same FP32 ops) even
// under huge-base ULP loss (both round identically), so the check requires
// residual == 0 exactly -- a magnitude-scaled tolerance like eps*|state| would
// allow ~477 at |state|=1e9 and hide a wrong small increment. A wrong k /
// coefficient / sign misses states[j] by a nonzero residual; per-block slicing
// localizes which block failed. The stage LABEL is enforced separately (below).
//
// `states` are the running FP32 states AFTER each source's production add
// (states.size() == sources.size(); states.back() must equal U_stage). Returns ""
// when sound, else a reason for the SDIRK3_STAGE_OPERAND_ATTRIBUTION_FAILED marker.
// Emits SDIRK3_STAGE_APPLIED_DELTA success lines only after the gate passes.
inline std::string emit_stage_applied_delta_diag(
    int64_t step_seq,
    int checkpoint_timestep,
    int target_stage,
    float dt,
    const torch::Tensor& U_n,
    const torch::Tensor& U_stage,
    const std::vector<StageHistorySource>& sources,
    const std::vector<const torch::Tensor*>& states,
    const std::vector<StageOperandBlock>& blocks,
    StageOperandEvidenceBundle* bundle = nullptr) {
    torch::NoGradGuard no_grad;
    char dtbuf[32];
    std::snprintf(dtbuf, sizeof(dtbuf), "%.9e", static_cast<double>(dt));
    const std::string tag = std::string("dt=") + dtbuf +
                            " step_seq=" + std::to_string(step_seq) +
                            " checkpoint_timestep=" +
                            std::to_string(checkpoint_timestep) +
                            " target_stage=" + std::to_string(target_stage) +
                            " iter=0";
    auto fail = [&](const std::string& detail) -> std::string {
        emit_sdirk3_diag_line("[SDIRK3_STAGE_OPERAND_ATTRIBUTION_FAILED] " + tag +
                              " " + detail + "\n");
        return "SDIRK3_STAGE_OPERAND_ATTRIBUTION_FAILED: " + detail;
    };

    if (states.size() != sources.size())
        return fail("state_count=" + std::to_string(states.size()) +
                    " != source_count=" + std::to_string(sources.size()));

    // STRUCTURAL PREFLIGHT (P1-5) before ANY tensor arithmetic below.
    {
        std::vector<std::pair<const char*, const torch::Tensor*>> ops;
        ops.reserve(1 + states.size() + sources.size() * 2);
        ops.push_back({"U_n", &U_n});
        for (std::size_t j = 0; j < states.size(); ++j)
            ops.push_back({"state", states[j]});
        for (const auto& s : sources) {
            ops.push_back({"k_slow", s.k_slow});
            ops.push_back({"k_fast", s.k_fast});
        }
        const std::string sr =
            check_stage_operand_structure(U_stage, "U_stage", ops);
        if (!sr.empty()) {
            const auto pos = sr.find(':');
            const std::string marker =
                (pos == std::string::npos) ? sr : sr.substr(0, pos);
            const std::string detail =
                (pos == std::string::npos) ? std::string() : sr.substr(pos + 2);
            emit_sdirk3_diag_line("[" + marker + "] " + tag + " " + detail + "\n");
            return sr;
        }
    }

    // LABEL AUTHORITY (derived from the AUTHORITATIVE target_stage, not the
    // observed order): the history of target stage i is composed of source stages
    // 1..i-1 applied in order, so there must be exactly target_stage-1 sources and
    // the source at position j MUST carry the label j+1. The per-element reapply
    // check below ties each POSITION's k/coefficient to states[j]; it does NOT
    // constrain the stage LABEL, so a swapped label is a real misattribution the
    // reapply alone cannot catch. Enforce it here.
    if (static_cast<int>(sources.size()) != target_stage - 1)
        return fail("source_count=" + std::to_string(sources.size()) +
                    " != target_stage-1=" + std::to_string(target_stage - 1));
    for (std::size_t j = 0; j < sources.size(); ++j) {
        if (sources[j].stage != static_cast<int>(j) + 1)
            return fail("label_mismatch pos=" + std::to_string(j) + " stage=" +
                        std::to_string(sources[j].stage) +
                        " expected=" + std::to_string(j + 1));
    }

    std::vector<std::string> diag_lines;
    diag_lines.reserve(sources.size());
    double worst_residual = 0.0;
    std::string block_fail;

    for (std::size_t j = 0; j < sources.size(); ++j) {
        const auto& s = sources[j];
        if (!states[j] || !states[j]->defined() || !s.k_slow ||
            !s.k_slow->defined() || !s.k_fast || !s.k_fast->defined())
            return fail("undefined source/state at src_stage=" +
                        std::to_string(s.stage));
        const torch::Tensor& prev = (j == 0) ? U_n : *states[j - 1];
        if (!prev.defined())
            return fail("undefined prev state at src_stage=" +
                        std::to_string(s.stage));
        // IDENTICAL FP32 expression to production's compute_stage_rhs add.
        torch::Tensor reapply = prev +
                                dt * static_cast<float>(s.a_explicit) * (*s.k_slow) +
                                dt * static_cast<float>(s.a_implicit) * (*s.k_fast);
        // Non-finite inputs would make the residual NaN, and with the strict-zero
        // gate below std::max(0,NaN)==0 and NaN>0 is false -- so a NaN would FAIL
        // OPEN, emitting a "successful" attribution. Require finite prev / state /
        // reapply (which also covers non-finite k) BEFORE the zero comparison.
        if (!prev.isfinite().all().item<bool>() ||
            !states[j]->isfinite().all().item<bool>() ||
            !reapply.isfinite().all().item<bool>())
            return fail("nonfinite input/reapply at src_stage=" +
                        std::to_string(s.stage));
        // BIT-EXACT replay. Production and this re-application evaluate the same
        // FP32 expression, so a correct attribution lands on the captured state
        // EXACTLY (residual == 0), independent of |state| -- a huge-base ULP loss
        // is absorbed identically on both sides. A magnitude-scaled tolerance like
        // eps*|state| would allow ~477 at |state|=1e9 and hide a wrong small
        // increment; a strict zero cannot. Any nonzero residual is a real
        // misattribution.
        torch::Tensor residual = (reapply - *states[j]).abs();  // FP32, element-wise
        const double res_max = residual.max().item<double>();
        if (!std::isfinite(res_max))
            return fail("nonfinite residual at src_stage=" +
                        std::to_string(s.stage));
        worst_residual = std::max(worst_residual, res_max);
        const double applied_norm =
            (*states[j] - prev).to(torch::kFloat64).norm().item<double>();

        std::ostringstream os;
        os << "[SDIRK3_STAGE_APPLIED_DELTA] " << tag
           << " src_stage=" << s.stage << std::scientific << std::setprecision(6)
           << " applied_norm=" << applied_norm << " reapply_res_max=" << res_max;
        for (const auto& b : blocks) {
            if (b.start + b.size > residual.numel()) continue;
            const double bres =
                residual.slice(0, b.start, b.start + b.size).max().item<double>();
            os << " res_" << b.name << "=" << bres;
            if (bres > 0.0) {
                if (!block_fail.empty()) block_fail += ",";
                block_fail += std::string(b.name) + "@s" + std::to_string(s.stage);
            }
        }
        diag_lines.push_back(os.str());
    }

    if (worst_residual > 0.0) {
        char rb[128];
        std::snprintf(rb, sizeof(rb), "worst_reapply_residual=%.6e blocks=%s",
                      worst_residual,
                      block_fail.empty() ? "?" : block_fail.c_str());
        return fail(rb);
    }
    // Capture integrity: the last running state must be the assembled U_stage.
    if (states.back() && states.back()->defined() && U_stage.defined()) {
        const double integ =
            (*states.back() - U_stage).abs().max().item<double>();
        if (!std::isfinite(integ) || integ > 0.0) {  // NaN also fails closed
            char rb[96];
            std::snprintf(rb, sizeof(rb), "capture_integrity last_state_vs_U_stage=%.6e",
                          integ);
            return fail(rb);
        }
    }
    // This emitter's own gate passed -- STAGE the success lines. They are written
    // only when the caller commits the bundle, i.e. after EVERY authority passed.
    for (const auto& line : diag_lines) stage_success_line(bundle, line);
    return std::string();
}

// Validate the per-source-stage Newton-defect table against the AUTHORITATIVE
// expected set (the target stage), independent of the observed `defects` vector
// -- an omitted or duplicated source-stage defect must fail closed, not be
// silently under-reported (which would quietly gut PR 9E's candidate C, the
// prior-stage-defect-inheritance question). Returns "" when sound, else a
// comma-joined reason for the SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE marker.
//   expected stage ids = 1 .. target_stage-1, each exactly once
//   stage 1 (the ESDIRK explicit stage) MUST carry explicit=true; every later
//     stage MUST carry explicit=false  (pinned BOTH ways: role <=> stage id)
//   k_norm (observed for both roles) finite AND non-negative, unconditionally
//   IMPLICIT stage: f_fast_norm, newton_defect_norm, defect_to_k_ratio,
//     scaled_final_residual all OBSERVED -> finite AND non-negative
//   EXPLICIT stage (convergence NOT APPLICABLE): those four Newton fields MUST
//     equal kDefectNA EXACTLY (independent of k_norm, so NOT self-fulfilling),
//     and converged MUST be false (no last_stage_converged_ pollution). Every
//     field is constrained in BOTH roles -- the branch changes the EXPECTED
//     value, never SKIPS a field (the earlier role-skip stop-gate bug).
inline std::string validate_stage_defect_inventory(
    const std::vector<StageDefectSnapshot>& defects, int target_stage) {
    std::string reason;
    auto append = [&](const std::string& r) {
        if (!reason.empty()) reason += ",";
        reason += r;
    };
    // presence: each expected source stage appears exactly once
    for (int j = 1; j < target_stage; ++j) {
        int count = 0;
        for (const auto& d : defects)
            if (d.stage == j) ++count;
        if (count == 0) append("missing:s" + std::to_string(j));
        if (count > 1) append("duplicate:s" + std::to_string(j));
    }
    for (const auto& d : defects) {
        const std::string sid = "s" + std::to_string(d.stage);
        if (d.stage < 1 || d.stage >= target_stage)
            append("unknown_stage:" + sid);
        // Role is pinned to stage id BOTH ways: stage 1 IS the ESDIRK explicit
        // stage (a_implicit[0][0]==0, no Newton solve); every later stage is
        // implicit. This anchors which expected-value contract applies below, so
        // a record cannot dodge a check by mislabeling its role.
        if (d.explicit_stage && d.stage != 1)
            append("explicit_flag_nonstage1:" + sid);
        if (d.stage == 1 && !d.explicit_stage)
            append("stage1_not_explicit:" + sid);
        // k_norm = ||k_fast[stage-1]|| is OBSERVED for both roles.
        if (!std::isfinite(d.k_norm)) append("nonfinite_k:" + sid);
        if (d.k_norm < 0.0) append("neg_k_norm:" + sid);
        if (d.explicit_stage) {
            // Convergence NOT APPLICABLE. The four Newton fields MUST equal the
            // fixed kDefectNA sentinel, checked EXACTLY and INDEPENDENT of k_norm
            // -- so this can never be the old self-fulfilling "f_fast ~= k_norm /
            // defect == 0" that just re-read caller-forced values. converged MUST
            // be false: an explicit record must not carry the solver's
            // last_stage_converged_ (a different, implicit stage's verdict).
            if (d.f_fast_norm != kDefectNA) append("explicit_f_fast_not_na:" + sid);
            if (d.newton_defect_norm != kDefectNA) append("explicit_defect_not_na:" + sid);
            if (d.defect_to_k_ratio != kDefectNA) append("explicit_ratio_not_na:" + sid);
            if (d.scaled_final_residual != kDefectNA) append("explicit_scaled_not_na:" + sid);
            if (d.converged) append("explicit_converged_claim:" + sid);
        } else {
            // Implicit solve: the four Newton fields are OBSERVED -> finite AND
            // non-negative (the -1 "unobserved"/n-a sentinel is invalid here). Each
            // field is checked -- the role branch selects the contract, it does not
            // skip any field (the earlier role-skip stop-gate bug).
            if (!std::isfinite(d.f_fast_norm) ||
                !std::isfinite(d.newton_defect_norm) ||
                !std::isfinite(d.defect_to_k_ratio) ||
                !std::isfinite(d.scaled_final_residual))
                append("nonfinite:" + sid);
            if (d.f_fast_norm < 0.0) append("neg_f_fast:" + sid);
            if (d.newton_defect_norm < 0.0) append("neg_defect:" + sid);
            if (d.defect_to_k_ratio < 0.0) append("neg_ratio:" + sid);
            if (d.scaled_final_residual < 0.0) append("neg_scaled_resid:" + sid);
        }
    }
    return reason;
}

// Emit SDIRK3_STAGE_HISTORY_DIAG (per-source ARK increment provenance) and
// SDIRK3_STAGE_HISTORY_SUMMARY (history closure + per-source defect table) for
// the record stage `target_stage`. Every line is stamped with the full-precision
// `dt` and the solver-local monotonic `step_seq` (the primary, always-advancing
// diagnostic identifier). `checkpoint_timestep` is the solver's 4D-Var
// trajectory-checkpoint counter (get_saved_global_timestep()); it advances ONLY
// under save_trajectory and is 0 in a default run, so it is emitted for
// reference but is NOT the WRF model timestep -- do not read it as such.
//
// HARD GATE (PR 9E evidence integrity): returns an EMPTY string when the
// evidence is sound; otherwise a non-empty reason after printing the specific
// failure marker -- SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE (history inventory),
// SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE (defect-table inventory), or
// SDIRK3_STAGE_OPERAND_CLOSURE_FAILED (the AUTHORITATIVE history-relative
// closure: Sum of increments vs U_stage - U_n, whose relative-error denominator
// EXCLUDES U_n so a base state far larger than the increments cannot launder a
// wrong coefficient or sign -- a full-state ||.||/||U_stage|| would). The caller
// MUST controlled-fatal on a non-empty return. On failure ONLY the failure
// marker is printed: no success-form STAGE_HISTORY_* record is emitted for a
// rejected capture, so a consumer that greps only the success markers can never
// mistake broken evidence for sound.
//
// `U_stage` MUST be the pristine production history (before any halo/sanitize
// mutation) so the FP64 closure reproduces it. All output goes to std::cerr
// (WRF routes cerr to rsl.error.0000).
inline std::string emit_stage_history_diag(
    int64_t step_seq,
    int checkpoint_timestep,
    int target_stage,
    float dt,
    const torch::Tensor& U_n,
    const torch::Tensor& U_stage,
    const std::vector<StageHistorySource>& sources,
    const std::vector<StageDefectSnapshot>& defects,
    const std::vector<DerivedDefectRecord>* derived = nullptr,
    StageOperandEvidenceBundle* bundle = nullptr) {
    torch::NoGradGuard no_grad;

    char dtbuf[32];
    std::snprintf(dtbuf, sizeof(dtbuf), "%.9e", static_cast<double>(dt));
    const std::string tag = std::string("dt=") + dtbuf +
                            " step_seq=" + std::to_string(step_seq) +
                            " checkpoint_timestep=" +
                            std::to_string(checkpoint_timestep) +
                            " target_stage=" + std::to_string(target_stage) +
                            " iter=0";

    // ---- PHASE 0: STRUCTURAL PREFLIGHT (P1-5) before ANY tensor arithmetic. ----
    {
        std::vector<std::pair<const char*, const torch::Tensor*>> ops;
        ops.reserve(1 + sources.size() * 2);
        ops.push_back({"U_n", &U_n});
        for (const auto& s : sources) {
            ops.push_back({"k_slow", s.k_slow});
            ops.push_back({"k_fast", s.k_fast});
        }
        const std::string sr =
            check_stage_operand_structure(U_stage, "U_stage", ops);
        if (!sr.empty()) {
            const auto pos = sr.find(':');
            const std::string marker =
                (pos == std::string::npos) ? sr : sr.substr(0, pos);
            const std::string detail =
                (pos == std::string::npos) ? std::string() : sr.substr(pos + 2);
            emit_sdirk3_diag_line("[" + marker + "] " + tag + " " + detail + "\n");
            return sr;
        }
    }

    // ---- PHASE 1: build the FP64 increment leaves and BUFFER the per-source
    // DIAG lines (do NOT print yet -- see PHASE 3/4 ordering). Increments use the
    // SAME float coefficient cast production uses. `leaves` is filled completely
    // first so no reallocation can dangle a pointer. ----
    std::vector<OperandLeaf> leaves;           // inventory records (name + tensor)
    leaves.reserve(sources.size() * 2 + 1);
    std::vector<std::string> diag_lines;
    diag_lines.reserve(sources.size());

    auto add_leaf = [&](const std::string& name, const torch::Tensor& t) {
        OperandLeaf l;
        l.name = name;
        l.split = OperandSplit::History;
        l.kind = OperandTermKind::Additive;
        l.tensor = t;  // detached FP64 or undefined
        leaves.push_back(std::move(l));
    };

    // Base state U_n is the first leaf (index 0); increments follow.
    add_leaf("U_n", U_n.defined() ? U_n.detach().to(torch::kFloat64)
                                  : torch::Tensor());

    for (const auto& s : sources) {
        torch::Tensor expl_incr;
        torch::Tensor impl_incr;
        double expl_norm = 0.0;
        double impl_norm = 0.0;
        double k_slow_norm = 0.0;
        double k_fast_norm = 0.0;
        if (s.k_slow && s.k_slow->defined()) {
            auto ks = s.k_slow->detach();
            k_slow_norm = ks.to(torch::kFloat64).norm().item<double>();
            expl_incr = (dt * static_cast<float>(s.a_explicit) * ks)
                            .to(torch::kFloat64);
            expl_norm = expl_incr.norm().item<double>();
        }
        if (s.k_fast && s.k_fast->defined()) {
            auto kf = s.k_fast->detach();
            k_fast_norm = kf.to(torch::kFloat64).norm().item<double>();
            impl_incr = (dt * static_cast<float>(s.a_implicit) * kf)
                            .to(torch::kFloat64);
            impl_norm = impl_incr.norm().item<double>();
        }
        add_leaf("expl_incr_s" + std::to_string(s.stage), expl_incr);
        add_leaf("impl_incr_s" + std::to_string(s.stage), impl_incr);

        std::ostringstream os;
        os << "[SDIRK3_STAGE_HISTORY_DIAG] " << tag
           << " src_stage=" << s.stage
           << " birth_generation=" << s.birth_generation
           << " aE=" << std::scientific << std::setprecision(9) << s.a_explicit
           << " aI=" << s.a_implicit
           << " k_slow_norm=" << k_slow_norm
           << " k_fast_norm=" << k_fast_norm
           << " expl_incr_norm=" << expl_norm
           << " impl_incr_norm=" << impl_norm;
        diag_lines.push_back(os.str());
    }

    // ---- PHASE 2: validate against INDEPENDENT authorities + compute closures ----
    // 2a. History inventory: expected leaf set derived from the AUTHORITATIVE
    // target stage (lower stages j = 1..i-1, zero-coefficient included), NOT from
    // the observed `sources` -- so an omitted source stage fails closed as a
    // missing leaf.
    std::vector<std::string> required;
    required.reserve(static_cast<size_t>(target_stage > 0 ? target_stage : 1) * 2);
    required.push_back("U_n");
    for (int j = 1; j < target_stage; ++j) {
        required.push_back("expl_incr_s" + std::to_string(j));
        required.push_back("impl_incr_s" + std::to_string(j));
    }
    const std::string inventory =
        validate_stage_operand_inventory(leaves, required, /*optional=*/{});

    // 2b. Defect-table inventory (independent authority: target stage).
    const std::string defect_reason =
        validate_stage_defect_inventory(defects, target_stage);

    // 2c. AUTHORITATIVE history-relative closure: reconstructed increments (leaves
    // 1..N, WITHOUT U_n) vs the actual history U_stage - U_n. The relative-error
    // denominator EXCLUDES U_n, so a base state far larger than the increments
    // cannot hide a wrong coefficient or sign. hist_max_rel additionally catches a
    // block-local spike the L2 metric would average out (per-element denom floored
    // by the history RMS to avoid a zero-crossing blow-up).
    torch::Tensor recon;
    for (std::size_t k = 1; k < leaves.size(); ++k) {
        if (!leaves[k].tensor.defined()) continue;
        recon = recon.defined() ? recon + leaves[k].tensor
                                : leaves[k].tensor.clone();
    }
    torch::Tensor U_n64 = U_n.defined() ? U_n.detach().to(torch::kFloat64)
                                        : torch::Tensor();
    torch::Tensor U_stage64 = U_stage.detach().to(torch::kFloat64);
    torch::Tensor actual_history =
        U_n64.defined() ? (U_stage64 - U_n64) : U_stage64.clone();
    if (!recon.defined()) recon = torch::zeros_like(actual_history);
    torch::Tensor hdiff = recon - actual_history;
    const double hist_abs = hdiff.norm().item<double>();
    const double recon_norm = recon.norm().item<double>();
    const double actual_norm = actual_history.norm().item<double>();
    const double hist_rel =
        hist_abs / std::max(std::max(recon_norm, actual_norm), 1e-300);
    const double hist_max_abs = hdiff.abs().max().item<double>();
    const double actual_rms =
        actual_norm /
        std::sqrt(static_cast<double>(std::max<int64_t>(actual_history.numel(), 1)));
    const double spike_floor = std::max(actual_rms * 1e-3, 1e-300);
    const double hist_max_rel =
        (hdiff.abs() / (actual_history.abs() + spike_floor)).max().item<double>();
    const bool hist_finite = hdiff.isfinite().all().item<bool>() &&
                             recon.isfinite().all().item<bool>();

    // Full-state closure is TELEMETRY ONLY (never the gate): with ||U_n|| >>
    // ||increments|| it stays ~eps even for a wrong increment, which is exactly
    // why it must not be the authority.
    std::vector<const torch::Tensor*> closure_leaves;
    closure_leaves.reserve(leaves.size());
    for (const auto& l : leaves) closure_leaves.push_back(&l.tensor);
    const ClosureResult full_closure = closure_of(closure_leaves, U_stage);

    const double u_n_norm = U_n64.defined() ? U_n64.norm().item<double>() : 0.0;
    const double u_stage_norm = U_stage64.norm().item<double>();

    // ---- PHASE 3: GATE. On ANY failure print ONLY the failure marker and return;
    // NO success-form STAGE_HISTORY_* record is emitted for a rejected capture. ----
    auto fail = [&](const char* marker, const std::string& detail) -> std::string {
        emit_sdirk3_diag_line(std::string("[") + marker + "] " + tag + " " +
                              detail + "\n");
        return std::string(marker) + ": " + detail;
    };
    if (!inventory.empty())
        return fail("SDIRK3_STAGE_OPERAND_CAPTURE_INCOMPLETE",
                    "inventory=" + inventory);
    if (!defect_reason.empty())
        return fail("SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE",
                    "defect=" + defect_reason);
    // PR 9F.1: the TENSOR evidence must be complete and must agree with the scalar
    // telemetry before anything is published. Without this, a missing tensor record
    // was invisible (only existing records were judged) and a wrong scalar could be
    // printed under a passing tensor gate.
    if (derived) {
        const std::string tinv = validate_derived_defect_inventory(*derived, target_stage);
        // validate_derived_defect_inventory already prefixes every reason with
        // "tensor_" (tensor_missing:, tensor_duplicate:, ...); do not double it.
        if (!tinv.empty())
            return fail("SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE", tinv);
        for (const auto& d : defects) {
            if (d.explicit_stage) continue;   // convergence n/a: no tensor expected
            const DerivedDefectRecord* dr = nullptr;
            for (const auto& r : *derived)
                if (r.stage == d.stage && r.observed) { dr = &r; break; }
            if (!dr)
                return fail("SDIRK3_STAGE_OPERAND_DEFECT_INCOMPLETE",
                            "tensor_absent_for_implicit:s" + std::to_string(d.stage));
            // Scalar telemetry is float32-derived, the tensor value float64, so a
            // small relative gap is expected; a LARGE one means the two describe
            // different evaluations and the record must not be published.
            auto rel_gap = [](double scalar, double tensor) {
                return std::abs(scalar - tensor) / std::max(std::abs(tensor), 1e-30);
            };
            const double kTelemetryTol = 1e-3;
            if (rel_gap(d.newton_defect_norm, dr->defect_norm) > kTelemetryTol ||
                rel_gap(d.f_fast_norm, dr->f_norm) > kTelemetryTol)
                return fail("SDIRK3_STAGE_OPERAND_DEFECT_INCOHERENT",
                            "scalar_telemetry_disagrees_with_tensor:s" +
                                std::to_string(d.stage));
        }
    }
    if (!hist_finite || hist_rel > 1e-6 || hist_max_rel > 1e-1) {
        char rb[224];
        std::snprintf(rb, sizeof(rb),
                      "hist_rel=%.6e hist_max_abs=%.6e hist_max_rel=%.6e "
                      "finite=%d recon_norm=%.6e actual_norm=%.6e",
                      hist_rel, hist_max_abs, hist_max_rel, hist_finite ? 1 : 0,
                      recon_norm, actual_norm);
        return fail("SDIRK3_STAGE_OPERAND_CLOSURE_FAILED", rb);
    }

    // ---- PHASE 4: THIS emitter's authorities passed -> STAGE the SUCCESS records.
    // They are NOT written here. The caller commits the bundle only after the
    // coherent-defect-tensor and applied-delta authorities have also passed, so a
    // failure in either of those leaves zero success-form evidence behind.
    for (const auto& line : diag_lines) stage_success_line(bundle, line);
    {
        std::ostringstream os;
        os << "[SDIRK3_STAGE_HISTORY_SUMMARY] " << tag << std::scientific
           << std::setprecision(6)
           << " hist_rel=" << hist_rel
           << " hist_abs=" << hist_abs
           << " hist_max_abs=" << hist_max_abs
           << " hist_max_rel=" << hist_max_rel
           << " recon_norm=" << recon_norm
           << " actual_norm=" << actual_norm
           << " full_state_rel=" << full_closure.rel_err
           << " U_n_norm=" << u_n_norm
           << " U_stage_norm=" << u_stage_norm
           << " inventory=OK defect=OK";
        stage_success_line(bundle, os.str());
    }
    for (const auto& d : defects) {
        // The tensor-derived authority for THIS source stage, if one was supplied.
        const DerivedDefectRecord* dr = nullptr;
        if (derived) {
            for (const auto& r : *derived)
                if (r.stage == d.stage && r.observed) { dr = &r; break; }
        }
        std::ostringstream os;
        os << "[SDIRK3_STAGE_HISTORY_SUMMARY] " << tag
           << " src_stage=" << d.stage
           << " explicit=" << (d.explicit_stage ? 1 : 0)
           << " convergence_applicable=" << (d.explicit_stage ? 0 : 1)
           << " k_norm=" << std::scientific << std::setprecision(6) << d.k_norm;
        if (d.explicit_stage) {
            // Convergence not applicable: emit n/a for every Newton field and do
            // NOT print a converged verdict (there was no solve to converge).
            os << " f_fast_norm=n/a newton_defect_norm=n/a"
               << " defect_to_k_ratio=n/a scaled_final_residual=n/a";
        } else if (dr) {
            // AUTHORITY: every convergence number published here is derived by this
            // emitter from the coherent {K,F,R} tensors. The caller's scalars are
            // demoted to explicitly-labelled telemetry_* columns (already
            // cross-checked against these values in PHASE 3).
            os << " defect_eval=tensor"
               // Evaluation-point semantics, explicit: the defect below is measured
               // at the SOLVER-returned K (pre-postprocess), while k_norm above is
               // the POST-postprocess history derivative. postprocess_delta_max is
               // the measured gap between them (0 when no transform fired).
               << " defect_eval_point=pre_postprocess"
               << " postprocess_applied=" << (dr->postprocess_applied ? 1 : 0)
               << " postprocess_delta_max=" << dr->postprocess_delta_max
               << " final_k_observed=" << (dr->final_k_observed ? 1 : 0)
               << " f_fast_norm=" << dr->f_norm
               << " newton_defect_norm=" << dr->defect_norm
               << " defect_to_k_ratio=" << dr->ratio
               << " defect_closure=" << dr->closure
               << " newton_iter=" << dr->newton_iter
               << " retry_generation=" << dr->retry_generation
               << " scaled_final_residual=" << d.scaled_final_residual
               << " converged=" << (d.converged ? 1 : 0)
               << " telemetry_f_fast_norm=" << d.f_fast_norm
               << " telemetry_newton_defect_norm=" << d.newton_defect_norm
               << " telemetry_defect_to_k_ratio=" << d.defect_to_k_ratio;
        } else {
            // No tensor authority supplied (legacy/no-bundle caller or a standing
            // contract test). Publish the scalars but LABEL them, so a consumer can
            // never mistake un-endorsed telemetry for tensor-derived evidence.
            os << " defect_eval=scalar_unverified"
               << " f_fast_norm=" << d.f_fast_norm
               << " newton_defect_norm=" << d.newton_defect_norm
               << " defect_to_k_ratio=" << d.defect_to_k_ratio
               << " scaled_final_residual=" << d.scaled_final_residual
               << " converged=" << (d.converged ? 1 : 0);
        }
        stage_success_line(bundle, os.str());
    }
    return std::string();
}

}  // namespace sdirk3
}  // namespace wrf
