// AUTOGRAD_NOTE: Ensure tensors have requires_grad=true for gradient computation
//
// ============================================================================
// AUTOGRAD GRAPH BREAK POINTS - IMPROVED (2025-12-04)
// ============================================================================
// This file has been updated to support conditional autograd graph preservation.
// When use_autograd=true, the following improvements are active:
//
// CONDITIONALLY PRESERVED (use_autograd=true):
//   1. Jacobian cache: F_cached and U_cached kept without detach()
//      - Memory usage increases, but graph is preserved
//
//   2. Line search: compute_rhs runs without NoGradGuard
//      - Graph flows through line search for 4DVAR applications
//
//   3. Bootstrap predictor (stage-1, no-history):
//      - Base explicit seed K0 = F(U_n)
//      - One explicit Picard sample K1 = F(U_n + dt*gamma*K0)
//      - Optional strong-acoustic correction (cheap predictor only):
//          r0 = K0 - K1,  delta ~= -M^{-1}r0,  K_init = K0 + delta
//      - Runs under NoGradGuard with detached tensors (does not alter method table)
//
//   4. GMRES preconditioner:
//      - Uses physics preconditioner on detached Krylov vectors when available
//      - Falls back/locks to identity inside GMRES if conditioning guard trips
//      - Keeps AD graph flow on Newton state path while stabilizing Krylov solve
//
// STILL BREAKING GRAPH (structural limitations):
//   5. Stage explosion guard: .item() for safety check (control flow)
//   6. Convergence checks: .item() needed for termination decisions
//   7. Trust region logic: .item() for ratio calculations
//
// DIAGNOSTIC BREAKS (debug_level gated, acceptable):
//   8. JVP validation: debug_level >= 2 only
//   9. NEWTON DEBUG block: debug_level >= 1 only
//   10. GMRES diagnostics: guarded_item() throughout
//
// SUMMARY:
//   - use_autograd=false (default): Full physics-based preconditioning
//   - use_autograd=true: fwAD/AD-enabled Newton iterations
//     - Stage-1 bootstrap uses detached no-grad predictor refinement
//     - GMRES preconditioner can run on detached Krylov vectors (guarded fallback)
//     - Graph retention is enabled only when retain_graph_for_adjoint=true
//       (otherwise Jacobian cache/line search use detached tensors to bound memory)
//   - For production 4DVAR: Consider checkpointing for memory efficiency
// ============================================================================
//
#include "wrf_sdirk3_newton_solver.h"
#include "wrf_sdirk3_coefficients.h"
#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_trust_model.h"   // PR 9F.9.1: pure exact trust-prediction (testable)
#include "wrf_sdirk3_jvp_autograd.h"
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include "wrf_sdirk3_rw_term_capture.h"  // PR 9B: rw term bisection
#include "wrf_sdirk3_autograd_utils.h"
#include "wrf_sdirk3_krylov_metrics.h"  // relative_residual / shares / E^-1 S
#include "wrf_sdirk3_stage_krylov_policy.h"  // pure stage budget/tolerance resolution
#include "wrf_sdirk3_stage_history_diag.h"  // PR 9F P2: shared emit_sdirk3_diag_line
#include "wrf_sdirk3_u_slow_diagnostics.h"   // next_solver_id: the process-wide one
#include "wrf_sdirk3_probe_validity.h"
#include "wrf_sdirk3_halo_c_api.h"
#include "wrf_sdirk3_unified_preconditioner.h"  // v20.5: For set_stage_state()
#include <torch/torch.h>
#include <ATen/CPUGeneratorImpl.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>  // std::min
#include <stdexcept>
#include <vector>
#include <array>
#include <cstring>  // PR 9B: std::strcmp in the checker direction loop
#include <map>
#include <set>      // PR 9B: per-block best-epsilon tracking in the checker
#include <mutex>    // PR 8.1: emit_stage_diag line-atomic output mutex
#include <sstream>  // PR 8.1: per-record ostringstream (libstdc++ needs it explicit)
#include <atomic>   // std::atomic was arriving transitively; libstdc++ need not provide it
#include <cstdint>  // uint64_t, same reason
#include <chrono>
#include <cstdlib>  // PR 9F.9.1: std::getenv for the numerical-shadow gate

namespace {
// PR 9F.9.1: the numerical shadows (fixed-S0 / block-max / exact-trust) are a very
// specific experiment -- full-state tensor mults + GPU->CPU syncs. Gate them on their
// OWN env flag, NOT the broad debug_level, so a general debug run does not pay for them
// and turning them on is an explicit, intuitive act. Cached once.
inline bool numerical_shadow_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WRF_SDIRK3_NUMERICAL_SHADOW");
        return e != nullptr && e[0] == '1' && e[1] == '\0';
    }();
    return on;
}
// PR 9F.B (B4 EXPERIMENT, [[sdirk3-scaling-metric-separation-plan]]): env-gated block-max
// convergence gate. Default OFF => byte-identical (the production gate is unchanged). When
// WRF_SDIRK3_BLOCKMAX_GATE=1 the Newton "converged" test additionally requires every block's
// scaled RMS to be under the tolerance, not just the size-weighted global RMS. This is a
// MEASUREMENT experiment (measure-first, before a production config knob): the shadow matrix
// showed a stage can pass the global gate while its mu block is >tol; this makes the model
// actually ITERATE on that block so we can measure whether it CONVERGES or stalls.
inline bool blockmax_gate_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WRF_SDIRK3_BLOCKMAX_GATE");
        return e != nullptr && e[0] == '1' && e[1] == '\0';
    }();
    return on;
}
// PR 9F.67 (early-stop ABLATION, [[sdirk3-scaling-metric-separation-plan]]): default OFF =>
// byte-identical. When WRF_SDIRK3_NO_EARLY_STOP=1 the FGMRES aggressive early-stop policies
// are disabled -- the ru-dominant aggressive gate (stag_window=1 vs a hardcoded
// prev_true_err=1.0), the forced mid-budget hopeless probe, and the Arnoldi stagnation break
// -- so GMRES runs the FULL restart budget. This is the paired-ablation lever that decides
// whether the B4 "7-iter rel_error=1" is a genuine operator plateau or a self-inflicted
// early-stop artifact: if the residual drops once the full budget runs, the POLICY was the
// cause, not the operator.
// Which stage the reference probe targets (0 = every stage). The reference solve is the
// most expensive thing in the run, and stage 3 alone can outlast the useful part of a
// session; naming a stage keeps a stage-2 question from paying stage-3's cost.
inline int stage_reference_target() {
    static const int target = [] {
        const char* e = std::getenv("WRF_SDIRK3_STAGE_REFERENCE_STAGE");
        return (e && *e) ? std::atoi(e) : 0;
    }();
    return target;
}

inline bool no_early_stop_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WRF_SDIRK3_NO_EARLY_STOP");
        return e != nullptr && e[0] == '1' && e[1] == '\0';
    }();
    return on;
}
// Route shadow records through the SHARED line-atomic emitter so concurrent OpenMP
// workers never interleave a record (matches the rest of the solver diagnostics).
inline void emit_numerical_shadow_line(const char* line) {
    wrf::sdirk3::emit_sdirk3_diag_line(std::string(line));
}
}  // namespace
#include <limits>
#include <tuple>
#include <functional>
#include <memory>
#include <sstream>

// ============================================================================
// DEBUG OUTPUT POLICY (OPT Pass33+)
// ============================================================================
// Production builds: SDIRK3_DEBUG must NOT be defined
//   - DEBUG_PRINT and ERROR_PRINT become no-ops
//   - All debug cerr outputs are silenced
//
// Debug builds: Define SDIRK3_DEBUG at compile time (-DSDIRK3_DEBUG)
//   - DEBUG_PRINT: Requires debug_level >= 2 (double-gated)
//   - ERROR_PRINT: Always active when SDIRK3_DEBUG defined
//   - Direct std::cerr outputs: Gated by debug_level >= 3 or error conditions
//
// CI/Build recommendation: Ensure SDIRK3_DEBUG is NOT in release configs.
// ============================================================================
// FIX Round159: Use do-while(0) pattern for safe macro expansion
#ifdef SDIRK3_DEBUG
#define DEBUG_PRINT(...) \
    do { \
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) { \
            std::cout << __VA_ARGS__ << std::endl; \
        } \
    } while (0)
#define ERROR_PRINT(...) \
    do { std::cerr << __VA_ARGS__ << std::endl; } while (0)
#else
#define DEBUG_PRINT(...) do {} while (0)
#define ERROR_PRINT(...) do {} while (0)
#endif

// FIX 2025-12-27: Helpers for safe scalar extraction (handles GPU sync + AD)
// These are used throughout Newton solver for convergence checks and diagnostics
// GPU sync is unavoidable for scalar values, but NoGradGuard prevents AD graph growth
namespace {

[[maybe_unused]] inline float safe_scalar(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.to(torch::kCPU).item<float>();
}
[[maybe_unused]] inline float safe_norm(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.norm().to(torch::kCPU).item<float>();
}
// For debugging only - extracts min/max/norm with single sync
[[maybe_unused]] inline std::tuple<float, float, float> safe_stats(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    auto t_cpu = t.to(torch::kCPU);
    return {t_cpu.min().item<float>(), t_cpu.max().item<float>(), t_cpu.norm().item<float>()};
}

// FIX 2026-01-28: Helper function for consistent halo zeroing
// Handles partial periodicity correctly: zeros halos only in non-periodic directions
// For em_b_wave: periodic_x=true, periodic_y=false → only zero y-direction halos
[[maybe_unused]] inline void zero_halo_regions(torch::Tensor& t, int halo_width,
                                               bool periodic_x, bool periodic_y) {
    if (halo_width <= 0 || t.dim() < 3) return;

    auto sizes = t.sizes();
    if (sizes.size() != 3) return;

    int nj = sizes[0];  // y-direction (j)
    [[maybe_unused]] int nk = sizes[1];  // z-direction (k)
    int ni = sizes[2];  // x-direction (i)

    // Zero y-direction halos only if NOT periodic in y
    if (!periodic_y && nj > 2 * halo_width) {
        t.slice(0, 0, halo_width).zero_();
        t.slice(0, nj - halo_width, nj).zero_();
    }

    // Zero x-direction halos only if NOT periodic in x
    if (!periodic_x && ni > 2 * halo_width) {
        t.slice(2, 0, halo_width).zero_();
        t.slice(2, ni - halo_width, ni).zero_();
    }
}

// FIX 2026-01-28: Minimum threshold for ||b|| to avoid spurious relative error
constexpr float BNORM_MIN_THRESHOLD = 1e-12f;

} // anonymous namespace

/**
 * WRF SDIRK-3 Newton-Krylov Solver Implementation
 * 
 * 파일명: wrf_sdirk3_newton_solver.cpp
 * 목적: Implicit stage 해결을 위한 Newton-Krylov 솔버
 */

namespace wrf {
namespace sdirk3 {

// ============================================================================
// PR 8: opt-in Stage-3 convergence diagnostics (post-FGMRES rediagnosis).
// Enabled ONLY by WRF_SDIRK3_STAGE_DIAG=1 in the environment — read once and
// cached. When off, every diagnostic site is a single cached-bool branch: no
// extra norms, no .item(), no output, and no change to production numerics.
// When on, records are emitted to stderr as stable machine-readable
// key=value lines under the markers SDIRK3_NEWTON_DIAG / SDIRK3_FGMRES_DIAG /
// SDIRK3_STAGE_DIAG — the SAME format for every implicit stage (internal
// ARK324 stages 2/3/4 in IMEX mode 3; stage 1 is explicit and has no Newton
// solve), so the failing stage is judged against identical records from its
// healthy siblings. Reads only; the solve path is untouched.
//
// CUDA/MPS cost note (review P2): when ENABLED, diag_norm/diag_all_finite
// perform synchronous device-to-host scalar reads every Newton iteration —
// diagnosis-only; never use WRF_SDIRK3_STAGE_DIAG=1 for throughput or
// timing measurements.
// ============================================================================
static inline bool stage_diag_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("WRF_SDIRK3_STAGE_DIAG");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return on;
}

// PR 8.1 (review P2): machine-readable records must be LINE-ATOMIC. Each
// record is composed in a local ostringstream — so std::scientific /
// std::defaultfloat manipulate ONLY the local stream, never the global
// std::cerr formatting state — and written in one call under a
// process-global mutex, so concurrent emitters cannot interleave characters
// within a line. PR 9F P2: the write is delegated to the shared
// wrf::sdirk3::emit_sdirk3_diag_line so NEWTON/FGMRES and the stage-operand
// markers serialize on ONE process-global mutex (no cross-marker interleaving).
template <typename Build>
static inline void emit_stage_diag(Build&& build) {
    std::ostringstream oss;
    build(oss);
    wrf::sdirk3::emit_sdirk3_diag_line(oss.str());
}

// Finite check for a diagnostic record: detached, no-grad, single sync.
static inline bool diag_all_finite(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.defined() &&
           t.isfinite().all().to(torch::kCPU).item<bool>();
}

// Norm for a diagnostic record: detached, no-grad, single sync.
static inline float diag_norm(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    if (!t.defined()) return -1.0f;
    return t.detach().norm().to(torch::kCPU).item<float>();
}

// ============================================================================
// PR 9A: opt-in directional consistency check of the production
// linearization at the ACTUAL failing operand (WRF_SDIRK3_STAGE4_JVP_CHECK=1).
// Diagnosis-only. When unset, the only cost anywhere is a cached-boolean
// branch (and a null capture pointer inside FGMRES). Like the stage
// diagnostics, the checker performs synchronous device-to-host scalar reads
// and extra RHS evaluations — never enable it for throughput or timing
// measurements.
// ============================================================================
// R9 §10.1: which ORDER the stage budget knobs resolve in. Default is the shipped order, so
// this is a measurement selector, not a behaviour change.
//
// THE ONLY reading of this flag. It governs two sites -- budget resolution and the aggressive
// early-stop gates -- and the first version let each read the environment for itself. One used
// read_experiment_flag, the other a raw getenv whose test is `v[0] != '0'`, so
// WRF_SDIRK3_STAGE_KNOB_FIRST=false parsed as FALSE at one site and TRUE at the other: the
// budget resolved in the shipped order while the gates resolved stage-first. A hybrid policy
// that matches neither arm of the experiment, produced by the word "false".
//
// Two spellings of one boolean is a recurring defect class here. One function, one parser.
static inline wrf::sdirk3::StageKrylovOrder stage_krylov_order() {
    static const bool knob_first =
        wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_STAGE_KNOB_FIRST");
    return knob_first ? wrf::sdirk3::StageKrylovOrder::StageKnobFirst
                      : wrf::sdirk3::StageKrylovOrder::ShippedOrder;
}

static inline bool stage4_jvp_check_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("WRF_SDIRK3_STAGE4_JVP_CHECK");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return on;
}

// PR 9A: KrylovBasisCapture is declared in wrf_sdirk3_newton_solver.h.
static constexpr size_t kKrylovBasisCaptureMax = 16;

// PR 9B: single capture primitive shared by every basis/operator capture
// site (V, Z, A_Z, J_w) — detached clone under NoGradGuard, size-capped.
static inline void capture_basis_vector(std::vector<torch::Tensor>& list,
                                        const torch::Tensor& t) {
    if (list.size() < kKrylovBasisCaptureMax) {
        torch::NoGradGuard no_grad;
        list.push_back(t.detach().clone());
    }
}

// v20.14r27i: Unified GMRES residual norm — consistent for internal, final, and adaptive paths.
// 3D tensors: halo-zeroed norm (boundary noise excluded).
// 1D packed tensors: raw norm (halo mask disabled — see build_halo_mask).
// Returns Tensor for AD compatibility in inner loop.
static inline torch::Tensor gmres_residual_norm(const torch::Tensor& r, int halo_width,
                                                 bool periodic_x, bool periodic_y) {
    if (r.dim() >= 3 && halo_width > 0) {
        auto r_z = r.clone();
        zero_halo_regions(r_z, halo_width, periodic_x, periodic_y);
        return safe_tensor_norm(r_z);
    }
    // 1D packed: raw norm (halo mask is disabled)
    return safe_tensor_norm(r);
}

// Relative residuals, objective shares and the E^-1 S weighting live in
// wrf_sdirk3_krylov_metrics.h -- one implementation, reachable from the contracts. See that
// header for why a relative residual must take (r, b, L) together.

// ============================================================================
// FORWARD-MODE AD: Compute true JVP (J·v) using dual numbers
// ============================================================================
// NOTE: torch::autograd::grad() computes VJP (Jᵀ·v), NOT JVP (J·v)!
// Newton-GMRES requires (I - dt·γ·J)·dK, so we need the true JVP.
// Using VJP causes Arnoldi/Hessenberg to blow up in 1-2 steps (||r||=inf).
//
// Forward-mode AD uses dual numbers: F(u + εv) = F(u) + ε·(J·v)
// We extract the tangent part (J·v) which is the true JVP.
// ============================================================================
// Count every fwAD->FD fallback. Without a counter the fallback is visible only at
// debug_level>=1, so a run that silently spent the whole solve on the noisier FD operator
// reads afterwards as a run that used forward-mode AD. The manifest reports this count.
static std::atomic<long long> g_jvp_fd_fallback_count{0};

static torch::Tensor compute_jvp_forward_mode(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v) {
    bool used_fd_fallback = false;
    auto jvp = compute_jvp_fwad_or_fd(
        F,
        u,
        v,
        /*halo_width=*/0,
        /*fd_epsilon_override=*/0.0f,
        &used_fd_fallback);

    if (used_fd_fallback) {
        g_jvp_fd_fallback_count.fetch_add(1, std::memory_order_relaxed);
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[JVP FWAD] Fallback to finite-difference JVP (fwAD unavailable/failed)"
                      << std::endl;
        }
    }
    return jvp;
}

// Helper: State vector layout with exact offsets
// State ordering: [ru, rv, rw, ph, t, mu]
// 9F.D93: StateLayout moved to wrf_sdirk3_state_layout.h -- one authority, shared.

// ============================================================================
// PR 9B: directional consistency checker, extracted from the Newton loop
// (review refactor of the PR 9A inline block) and strengthened per the
// evidence stop-gate:
//   - per-block FULL epsilon ladders (block-specific best epsilon can differ
//     from the global one — the global norm is ru-dominated);
//   - central AND one-sided (plus/minus) FD at every epsilon: plus != minus
//     limits flag a nonsmooth/branch point where fwAD must not be judged
//     "wrong" outright; Richardson extrapolation from consecutive central
//     pairs;
//   - actual in-situ operator outputs (A_Z, J_w captured inside the live
//     Arnoldi loop) compared against the same FD, separately from the
//     post-solve replay; plus a direct replay-vs-actual drift row;
//   - a shadow-RHS purity probe (repeated + order-swapped evaluations of
//     identical inputs) that gates every FD verdict.
// Everything runs on detached clones under NoGradGuard; compute_rhs is
// invoked directly (the loop's jacobian_cache_ bookkeeping is bypassed);
// the preconditioner is never invoked.
// ============================================================================
namespace jvp_check {

struct Context {
    int ts = 0, stage = 0, newton_iter = 0;
    float dt = 0.0f, gamma = 0.0f;
    torch::Tensor U_stage, K, U_eval, dK;   // detached clones
    bool scaled = false;
    torch::Tensor S_diag, S_inv_diag;
    const StateLayout* layout = nullptr;
    const KrylovBasisCapture* basis = nullptr;
    std::function<torch::Tensor(const torch::Tensor&)> compute_rhs;
    std::function<torch::Tensor(const torch::Tensor&)> apply_jacobian;
    std::function<torch::Tensor(const torch::Tensor&)> gmres_op;
};

// One comparison row (global or block slice); returns rel_err.
static float emit_cmp_row(const Context& c, const char* dlabel, int aj,
                          const char* op, const char* fdlabel, const char* source,
                          float eps_rel, float eps_abs,
                          const torch::Tensor& prod, const torch::Tensor& fd,
                          const char* block, int64_t bstart, int64_t bsize) {
    torch::NoGradGuard no_grad;
    auto pt = (bsize >= 0) ? prod.slice(0, bstart, bstart + bsize) : prod;
    auto ft = (bsize >= 0) ? fd.slice(0, bstart, bstart + bsize) : fd;
    auto stats = torch::stack({pt.norm(), ft.norm(), (pt - ft).norm(),
                               (pt * ft).sum()}).to(torch::kCPU);
    const float pn = stats[0].item<float>();
    const float fn = stats[1].item<float>();
    const float ae = stats[2].item<float>();
    const float denom = std::max({pn, fn, 1e-30f});
    const float re = ae / denom;
    const float cs = (pn > 0.0f && fn > 0.0f) ? stats[3].item<float>() / (pn * fn)
                                              : 0.0f;
    const bool fin = std::isfinite(pn) && std::isfinite(fn) && std::isfinite(ae);
    emit_stage_diag([&](std::ostream& os) {
        os << "SDIRK3_STAGE4_JVP_DIAG ts=" << c.ts
           << " stage=" << c.stage << " newton_iter=" << c.newton_iter
           << " arnoldi_j=" << aj
           << " direction=" << dlabel
           << " operator=" << op
           << " fd=" << fdlabel
           << " source=" << source
           << std::scientific
           << " epsilon=" << eps_rel
           << " eps_abs=" << eps_abs
           << " prod_norm=" << pn
           << " fd_norm=" << fn
           << " abs_err=" << ae
           << " rel_err=" << re
           << " cosine=" << cs
           << std::defaultfloat
           << " finite=" << (fin ? 1 : 0)
           << " block=" << block
           << "\n";
    });
    return re;
}

struct BlockBest {
    float rel = std::numeric_limits<float>::infinity();
    float eps_rel = 0.0f;
    float eps_abs = 0.0f;
};

// Rows for global + every layout block; central rows can feed best-tracking.
static void emit_all_blocks(const Context& c, const char* dlabel, int aj,
                            const char* op, const char* fdlabel, const char* source,
                            float eps_rel, float eps_abs,
                            const torch::Tensor& prod, const torch::Tensor& fd,
                            std::map<std::string, BlockBest>* best) {
    auto note = [&](const std::string& block, float re) {
        if (best && std::isfinite(re)) {
            auto& b = (*best)[block];
            if (re < b.rel) { b.rel = re; b.eps_rel = eps_rel; b.eps_abs = eps_abs; }
        }
    };
    note("global", emit_cmp_row(c, dlabel, aj, op, fdlabel, source, eps_rel,
                                eps_abs, prod, fd, "global", -1, -1));
    if (c.layout) {
        for (const auto& blk : c.layout->blocks) {
            if (blk.start + blk.size <= prod.numel()) {
                note(blk.name, emit_cmp_row(c, dlabel, aj, op, fdlabel, source,
                                            eps_rel, eps_abs, prod, fd,
                                            blk.name.c_str(), blk.start, blk.size));
            }
        }
    }
}

// Full epsilon ladder for one (direction, operator): central + one-sided FD
// at every epsilon, Richardson extrapolation from consecutive central pairs,
// per-block best-epsilon summaries, actual-vs-FD rows when an in-situ
// capture exists, and a replay-vs-actual drift row.
static void evaluate_epsilon_ladder(const Context& c, const char* dlabel, int aj,
                                    const char* op,
                                    const torch::Tensor& prod_replay,
                                    const torch::Tensor& prod_actual,
                                    const std::function<torch::Tensor(float)>& eval_at,
                                    float ref_norm, float dir_norm) {
    static const float kEpsLadder[] = {1e-2f, 3e-3f, 1e-3f, 3e-4f, 1e-4f, 3e-5f, 1e-5f};
    torch::Tensor F0 = eval_at(0.0f);
    std::map<std::string, BlockBest> best;
    torch::Tensor prev_central;
    float prev_eps_abs = -1.0f;
    for (float eps_rel : kEpsLadder) {
        const float eps_abs = eps_rel * (1.0f + ref_norm) / std::max(dir_norm, 1e-30f);
        torch::Tensor central, plus, minus;
        {
            torch::NoGradGuard no_grad;
            auto Fp = eval_at(+eps_abs);
            auto Fm = eval_at(-eps_abs);
            central = ((Fp - Fm) / (2.0f * eps_abs)).detach();
            plus = ((Fp - F0) / eps_abs).detach();
            minus = ((F0 - Fm) / eps_abs).detach();
        }
        emit_all_blocks(c, dlabel, aj, op, "central", "replay", eps_rel, eps_abs,
                        prod_replay, central, &best);
        emit_all_blocks(c, dlabel, aj, op, "plus", "replay", eps_rel, eps_abs,
                        prod_replay, plus, nullptr);
        emit_all_blocks(c, dlabel, aj, op, "minus", "replay", eps_rel, eps_abs,
                        prod_replay, minus, nullptr);
        if (prod_actual.defined()) {
            emit_all_blocks(c, dlabel, aj, op, "central", "actual", eps_rel,
                            eps_abs, prod_actual, central, nullptr);
        }
        if (prev_central.defined() && prev_eps_abs > 0.0f) {
            torch::NoGradGuard no_grad;
            const float r = prev_eps_abs / eps_abs;
            auto rich = (((r * r) * central - prev_central) / (r * r - 1.0f)).detach();
            emit_all_blocks(c, dlabel, aj, op, "richardson", "replay", eps_rel,
                            eps_abs, prod_replay, rich, nullptr);
        }
        prev_central = central;
        prev_eps_abs = eps_abs;
    }
    for (const auto& kv : best) {
        emit_stage_diag([&](std::ostream& os) {
            os << "SDIRK3_STAGE4_JVP_DIAG ts=" << c.ts
               << " stage=" << c.stage << " newton_iter=" << c.newton_iter
               << " arnoldi_j=" << aj
               << " direction=" << dlabel << " operator=" << op
               << " summary=1 fd=central source=replay"
               << std::scientific
               << " best_epsilon=" << kv.second.eps_rel
               << " best_eps_abs=" << kv.second.eps_abs
               << " best_rel_err=" << kv.second.rel
               << std::defaultfloat
               << " block=" << kv.first
               << "\n";
        });
    }
    if (prod_actual.defined()) {
        // Route-lock / cache / mutable-RHS drift between the live Arnoldi
        // application and the post-solve replay of the same operator.
        emit_all_blocks(c, dlabel, aj, op, "replay_vs_actual", "actual", 0.0f,
                        0.0f, prod_replay, prod_actual, nullptr);
    }
}

// Shadow-RHS purity probe: repeated + order-swapped evaluations of IDENTICAL
// inputs. Runs BEFORE any FD ladder and GATES them fail-close: if any pair
// differs beyond 10*FLT_EPSILON (the RHS_DETERMINISM_CHECK threshold), the
// checker refuses to produce FD verdicts from the impure replay and emits a
// purity_gate=failed record instead.
static bool run_purity_probe(const Context& c, const torch::Tensor& w_dir) {
    torch::NoGradGuard no_grad;
    const float w_norm = diag_norm(w_dir);
    const float eps_abs =
        1e-3f * (1.0f + diag_norm(c.U_eval)) / std::max(w_norm, 1e-30f);
    auto Up = (c.U_eval + eps_abs * w_dir).detach();
    auto Um = (c.U_eval - eps_abs * w_dir).detach();
    auto F0a = c.compute_rhs(c.U_eval).detach();
    auto F0b = c.compute_rhs(c.U_eval).detach();
    auto Fp1 = c.compute_rhs(Up).detach();
    auto Fm1 = c.compute_rhs(Um).detach();
    auto Fm2 = c.compute_rhs(Um).detach();
    auto Fp2 = c.compute_rhs(Up).detach();
    auto emit_pair = [&](const char* pair, const torch::Tensor& a,
                         const torch::Tensor& b) -> float {
        auto stats = torch::stack({a.norm(), (a - b).norm()}).to(torch::kCPU);
        const float an = stats[0].item<float>();
        const float dn = stats[1].item<float>();
        const float rd = dn / std::max(an, 1e-30f);
        // Non-finite probe results (NaN norms, NaN-NaN diffs) must FAIL the
        // gate, never slip through it: NaN compares false against the
        // threshold and std::max(0, NaN) keeps 0, so promote any non-finite
        // outcome to +inf before it reaches the accumulator.
        const bool fin = std::isfinite(an) && std::isfinite(dn) && std::isfinite(rd);
        emit_stage_diag([&](std::ostream& os) {
            os << "SDIRK3_STAGE4_JVP_DIAG ts=" << c.ts
               << " stage=" << c.stage << " newton_iter=" << c.newton_iter
               << " purity=1 pair=" << pair
               << std::scientific
               << " eps_abs=" << eps_abs
               << " base_norm=" << an
               << " abs_diff=" << dn
               << " rel_diff=" << rd
               << std::defaultfloat
               << " finite=" << (fin ? 1 : 0)
               << " bit_identical=" << (fin && dn == 0.0f ? 1 : 0)
               << "\n";
        });
        return fin ? rd : std::numeric_limits<float>::infinity();
    };
    float worst = 0.0f;
    worst = std::max(worst, emit_pair("F0_repeat", F0a, F0b));
    worst = std::max(worst, emit_pair("Fplus_order_swap", Fp1, Fp2));
    worst = std::max(worst, emit_pair("Fminus_order_swap", Fm1, Fm2));
    const float purity_tol = 10.0f * std::numeric_limits<float>::epsilon();
    const bool pure = std::isfinite(worst) && worst <= purity_tol;
    emit_stage_diag([&](std::ostream& os) {
        os << "SDIRK3_STAGE4_JVP_DIAG ts=" << c.ts
           << " stage=" << c.stage << " newton_iter=" << c.newton_iter
           << " purity_gate=" << (pure ? "passed" : "failed")
           << std::scientific
           << " worst_rel_diff=" << worst
           << " purity_tol=" << purity_tol
           << std::defaultfloat << "\n";
    });
    return pure;
}

// PR 9B commit 2: term-level bisection of the rw tendency at the actual
// operand. Captures every separable rw term inside dedicated compute_rhs
// calls (primal, production forward-dual, and +/- FD states), verifies the
// two closures (sum of primal terms ~= assembled rw tendency; sum of term
// tangents ~= full production rw tangent), and compares each term's
// production fwAD tangent against central/plus/minus FD. Emits
// SDIRK3_RW_TERM_DIAG records via the line-atomic helper.
static void run_rw_term_bisection(const Context& c) {
    using Terms = std::vector<std::pair<std::string, torch::Tensor>>;

    // Fixed deterministic block-balanced probe (w-rich; same hash as the
    // direction loop) mapped to U-space: w_dir = dt*gamma*probe.
    torch::Tensor w_dir;
    {
        torch::NoGradGuard no_grad;
        auto idx = torch::arange(c.K.numel(),
                                 torch::TensorOptions()
                                     .dtype(torch::kFloat32)
                                     .device(torch::kCPU));
        auto probe = torch::sin(idx * 12.9898f + 78.233f);
        if (c.layout) {
            for (const auto& blk : c.layout->blocks) {
                if (blk.start + blk.size <= probe.numel()) {
                    auto slc = probe.slice(0, blk.start, blk.start + blk.size);
                    auto rms = slc.square().mean().sqrt().clamp_min(1e-30f);
                    slc.div_(rms);
                }
            }
        }
        w_dir = (c.dt * c.gamma *
                 probe.to(c.K.device()).to(c.K.dtype())).detach();
    }
    const float U_ref = diag_norm(c.U_eval);
    const float w_norm = diag_norm(w_dir);

    auto find_term = [](const Terms& t, const char* name) -> torch::Tensor {
        for (const auto& kv : t)
            if (kv.first == name) return kv.second;
        return torch::Tensor();
    };

    auto emit_term_row = [&](const char* term, const char* fdlabel,
                             float eps_rel, float eps_abs,
                             const torch::Tensor& prod,
                             const torch::Tensor& fd) {
        torch::NoGradGuard no_grad;
        if (!prod.defined() || !fd.defined()) return;
        auto stats = torch::stack({prod.norm(), fd.norm(), (prod - fd).norm(),
                                   (prod * fd).sum()}).to(torch::kCPU);
        const float pn = stats[0].item<float>();
        const float fn = stats[1].item<float>();
        const float ae = stats[2].item<float>();
        const float re = ae / std::max({pn, fn, 1e-30f});
        const float cs = (pn > 0.f && fn > 0.f)
            ? stats[3].item<float>() / (pn * fn) : 0.0f;
        const bool fin = std::isfinite(pn) && std::isfinite(fn) &&
                         std::isfinite(ae);
        emit_stage_diag([&](std::ostream& os) {
            os << "SDIRK3_RW_TERM_DIAG ts=" << c.ts
               << " stage=" << c.stage << " newton_iter=" << c.newton_iter
               << " term=" << term
               << " fd=" << fdlabel
               << std::scientific
               << " epsilon=" << eps_rel
               << " eps_abs=" << eps_abs
               << " prod_norm=" << pn
               << " fd_norm=" << fn
               << " abs_err=" << ae
               << " rel_err=" << re
               << " cosine=" << cs
               << std::defaultfloat
               << " finite=" << (fin ? 1 : 0)
               << "\n";
        });
    };

    // PR 9B.1: whether the implicit W-damping gate is active decides the
    // expected capture inventory (w_damp_padded present or not).
    const bool expect_wdamp =
        wrf::sdirk3::g_sdirk3_config.wrf_w_damping == 1 &&
        wrf::sdirk3::g_sdirk3_config.implicit_wdamp &&
        wrf::sdirk3::g_sdirk3_config.w_damp_alpha > 0.0f &&
        wrf::sdirk3::g_sdirk3_config.wrf_w_crit_cfl > 0.0f;

    auto emit_capture_fail = [&](const char* marker, const std::string& why) {
        emit_stage_diag([&](std::ostream& os) {
            os << "SDIRK3_RW_TERM_DIAG ts=" << c.ts
               << " stage=" << c.stage << " newton_iter=" << c.newton_iter
               << " " << marker << "=1";
            if (!why.empty()) os << " reason=" << why;
            os << "\n";
        });
    };

    // Capture the rw terms of one plain evaluation (detached clones).
    // PR 9B.1: RAII arming (exception-safe), nested-arm and incomplete-
    // inventory captures FAIL CLOSED with stable markers — no term verdicts
    // are produced from a partial or contaminated capture.
    auto capture_eval = [&](const torch::Tensor& U, bool& ok) -> Terms {
        ok = false;
        RwTermCaptureScope scope;
        if (!scope.armed_ok()) {
            emit_capture_fail("SDIRK3_RW_TERM_CAPTURE_NESTED", {});
            return {};
        }
        auto F = c.compute_rhs(U);
        (void)F;
        auto raw = scope.take();
        // PR 9B.2 (P1-2): validate the RAW capture (names AND definedness)
        // BEFORE any detach().clone() — an undefined tensor must fail closed
        // with the stable marker, never a libtorch exception.
        const std::string why = validate_rw_term_inventory(raw, expect_wdamp);
        if (!why.empty()) {
            emit_capture_fail("SDIRK3_RW_TERM_CAPTURE_INCOMPLETE", why);
            return {};
        }
        Terms out;
        {
            torch::NoGradGuard no_grad;
            for (const auto& kv : raw)
                out.emplace_back(kv.first, kv.second.detach().clone());
        }
        ok = true;
        return out;
    };

    // ---- 1) primal terms + closure #1 (sum of terms == assembled rw) ----
    bool t0_ok = false;
    Terms t0 = capture_eval(c.U_eval, t0_ok);
    if (!t0_ok) return;  // fail-close: marker already emitted
    {
        torch::NoGradGuard no_grad;
        auto pre = find_term(t0, "rw_pre_pgf");
        auto pgf = find_term(t0, "w_pgf_buoy_all");
        auto top = find_term(t0, "w_top_contrib");
        auto prem = find_term(t0, "rw_pre_mask");
        auto postm = find_term(t0, "rw_post_mask");
        auto damp = find_term(t0, "w_damp_padded");
        auto fin = find_term(t0, "rw_tend_final");
        auto pg = find_term(t0, "pg");
        auto b1 = find_term(t0, "buoy_mu1");
        auto b2 = find_term(t0, "buoy_mu2");
        if (pg.defined() && b1.defined() && b2.defined() && pgf.defined()) {
            emit_term_row("closure_subterms_vs_pgf", "closure", 0, 0,
                          (pg - b1 - b2), pgf);
        }
        if (pre.defined() && pgf.defined() && top.defined() && prem.defined()) {
            emit_term_row("closure_sum_vs_premask", "closure", 0, 0,
                          (pre + pgf + top), prem);
        }
        if (postm.defined() && fin.defined()) {
            auto rhs = damp.defined() ? (postm - damp) : postm;
            emit_term_row("closure_postmask_vs_final", "closure", 0, 0,
                          rhs, fin);
        }
    }

    // ---- 2) production per-term tangents (fwAD, production mechanics) ----
    Terms tg;
    {
        torch::NoGradGuard no_grad;
        wrf::sdirk3::jvp_detail::DualLevelGuard dual_guard;
        auto u_dual = torch::_make_dual(c.U_eval, w_dir,
                                        static_cast<int64_t>(dual_guard.level()));
        RwTermCaptureScope scope;
        if (!scope.armed_ok()) {
            emit_capture_fail("SDIRK3_RW_TERM_CAPTURE_NESTED", {});
            return;
        }
        auto F_dual = c.compute_rhs(u_dual);
        (void)F_dual;
        // take() while the dual level guard is still alive.
        auto raw = scope.take();
        // PR 9B.2 (P1-2): validate the RAW capture BEFORE _unpack_dual —
        // an undefined CAPTURED tensor fails closed with the marker. (An
        // undefined unpacked TANGENT of a defined tensor remains normal for
        // constant terms and is treated as a zero tangent below.)
        const std::string why = validate_rw_term_inventory(raw, expect_wdamp);
        if (!why.empty()) {
            emit_capture_fail("SDIRK3_RW_TERM_CAPTURE_INCOMPLETE", why);
            return;
        }
        for (const auto& kv : raw) {
            auto parts = torch::_unpack_dual(
                kv.second, static_cast<int64_t>(dual_guard.level()));
            auto tgt = std::get<1>(parts);
            tg.emplace_back(kv.first,
                            tgt.defined()
                                ? tgt.detach().clone()
                                : torch::zeros_like(std::get<0>(parts))
                                      .detach());
        }
    }
    // Closure #2: sum of term tangents == full production rw tangent.
    {
        torch::NoGradGuard no_grad;
        auto pg = find_term(tg, "pg");
        auto b1 = find_term(tg, "buoy_mu1");
        auto b2 = find_term(tg, "buoy_mu2");
        auto pgf = find_term(tg, "w_pgf_buoy_all");
        auto postm = find_term(tg, "rw_post_mask");
        auto damp = find_term(tg, "w_damp_padded");
        auto fin = find_term(tg, "rw_tend_final");
        if (pg.defined() && b1.defined() && b2.defined() && pgf.defined()) {
            emit_term_row("closure_tangent_subterms_vs_pgf", "closure", 0, 0,
                          (pg - b1 - b2), pgf);
        }
        if (postm.defined() && fin.defined()) {
            auto rhs = damp.defined() ? (postm - damp) : postm;
            emit_term_row("closure_tangent_postmask_vs_final", "closure", 0, 0,
                          rhs, fin);
        }
    }

    // ---- 3) per-term FD ladder: central + one-sided against the tangent ----
    // Extended far below sign_smooth_delta (1e-3): the W-damping chain's
    // smooth-sign has curvature ~1/delta^2, so its FD converges only once the
    // per-point w perturbation drops well below delta — the discriminating
    // regime between "tangent defect" and "FD truncation artifact".
    const float kTermEps[] = {1e-3f, 1e-4f, 1e-5f, 1e-6f, 1e-7f, 1e-8f, 1e-9f};
    for (float eps_rel : kTermEps) {
        const float eps_abs = eps_rel * (1.0f + U_ref) /
                              std::max(w_norm, 1e-30f);
        Terms tp, tm;
        bool tp_ok = false, tm_ok = false;
        {
            torch::NoGradGuard no_grad;
            tp = capture_eval((c.U_eval + eps_abs * w_dir).detach(), tp_ok);
            tm = capture_eval((c.U_eval - eps_abs * w_dir).detach(), tm_ok);
        }
        if (!tp_ok || !tm_ok) continue;  // fail-close: markers already emitted
        for (const auto& kv : tg) {
            torch::NoGradGuard no_grad;
            auto p = find_term(tp, kv.first.c_str());
            auto m = find_term(tm, kv.first.c_str());
            auto z = find_term(t0, kv.first.c_str());
            if (!p.defined() || !m.defined() || !z.defined()) continue;
            auto central = ((p - m) / (2.0f * eps_abs)).detach();
            auto plus = ((p - z) / eps_abs).detach();
            auto minus = ((z - m) / eps_abs).detach();
            emit_term_row(kv.first.c_str(), "central", eps_rel, eps_abs,
                          kv.second, central);
            emit_term_row(kv.first.c_str(), "plus", eps_rel, eps_abs,
                          kv.second, plus);
            emit_term_row(kv.first.c_str(), "minus", eps_rel, eps_abs,
                          kv.second, minus);
        }
    }
}

static void run_directional_consistency_check(const Context& c) {
    struct Dir {
        const char* label;
        int aj;
        torch::Tensor d;
        bool scaled_space;
        int cap_idx;  // index into the in-situ capture lists (Z only)
    };
    std::vector<Dir> dirs;
    auto add_basis_dirs = [&](const std::vector<torch::Tensor>& basis,
                              const char* label) {
        if (!basis.empty()) {
            dirs.push_back({label, 0, basis.front(), true, 0});
            if (basis.size() > 1) {
                dirs.push_back({label, static_cast<int>(basis.size()) - 1,
                                basis.back(), true,
                                static_cast<int>(basis.size()) - 1});
            }
        }
    };
    if (c.basis) {
        add_basis_dirs(c.basis->V, "V");
        add_basis_dirs(c.basis->Z, "Z");
    }
    if (c.dK.defined() && diag_norm(c.dK) > 0.0f) {
        dirs.push_back({"dK", -1, c.dK, false, -1});
    }
    {
        // Fixed deterministic block-balanced probe (hash, not torch RNG — no
        // generator state, global or local, is created or consumed).
        torch::NoGradGuard no_grad;
        auto idx = torch::arange(c.K.numel(),
                                 torch::TensorOptions()
                                     .dtype(torch::kFloat32)
                                     .device(torch::kCPU));
        auto probe = torch::sin(idx * 12.9898f + 78.233f);
        if (c.layout) {
            for (const auto& blk : c.layout->blocks) {
                if (blk.start + blk.size <= probe.numel()) {
                    auto slc = probe.slice(0, blk.start, blk.start + blk.size);
                    auto rms = slc.square().mean().sqrt().clamp_min(1e-30f);
                    slc.div_(rms);
                }
            }
        }
        dirs.push_back({"random", -1, probe.to(c.K.device()).to(c.K.dtype()),
                        false, -1});
    }

    const float K_ref = diag_norm(c.K);
    const float U_ref = diag_norm(c.U_eval);
    const float dtg = c.dt * c.gamma;

    // GATE (review P1): the purity probe runs BEFORE any FD ladder. An impure
    // shadow replay would contaminate every FD verdict, so on failure the
    // checker refuses to produce them (fail-close) — isolate the replay first.
    {
        torch::Tensor w_probe;
        {
            torch::NoGradGuard no_grad;
            w_probe = (dtg * dirs.back().d).detach();  // dirs.back() = probe (unscaled)
        }
        if (!run_purity_probe(c, w_probe)) {
            emit_stage_diag([&](std::ostream& os) {
                os << "SDIRK3_STAGE4_JVP_DIAG ts=" << c.ts
                   << " stage=" << c.stage << " newton_iter=" << c.newton_iter
                   << " skipped=1 reason=shadow_rhs_impure\n";
            });
            return;
        }
    }

    for (const auto& cd : dirs) {
        torch::Tensor d_unscaled;
        {
            torch::NoGradGuard no_grad;
            d_unscaled = (cd.scaled_space && c.scaled)
                             ? (c.S_diag * cd.d).detach()
                             : cd.d.detach().clone();
        }
        const float d_norm = diag_norm(d_unscaled);
        if (!(d_norm > 0.0f) || !std::isfinite(d_norm)) continue;

        // Production values, called exactly as the solve calls them (replay).
        torch::Tensor prod_A_replay =
            (cd.scaled_space ? c.gmres_op(cd.d) : c.apply_jacobian(d_unscaled))
                .detach();
        torch::Tensor prod_A_unscaled =
            cd.scaled_space ? c.apply_jacobian(d_unscaled).detach()
                            : prod_A_replay;
        torch::Tensor prod_J_replay, w_dir;
        {
            torch::NoGradGuard no_grad;
            prod_J_replay = (d_unscaled - prod_A_unscaled).detach();
            w_dir = (dtg * d_unscaled).detach();
        }
        const float w_norm = diag_norm(w_dir);

        // Actual in-situ operator outputs exist for Z directions (the
        // operator is applied to Z_j in the live Arnoldi loop; V has no
        // directly-applied output).
        torch::Tensor actual_A, actual_J;
        if (c.basis && std::strcmp(cd.label, "Z") == 0 && cd.cap_idx >= 0) {
            if (cd.cap_idx < static_cast<int>(c.basis->A_Z.size()))
                actual_A = c.basis->A_Z[cd.cap_idx];
            if (cd.cap_idx < static_cast<int>(c.basis->J_w.size()))
                actual_J = c.basis->J_w[cd.cap_idx];
        }

        // operator=J: FD of compute_rhs at U_eval along w = dt*gamma*d.
        evaluate_epsilon_ladder(
            c, cd.label, cd.aj, "J", prod_J_replay, actual_J,
            [&](float e) -> torch::Tensor {
                torch::NoGradGuard no_grad;
                if (e == 0.0f) return c.compute_rhs(c.U_eval).detach();
                return c.compute_rhs((c.U_eval + e * w_dir).detach()).detach();
            },
            U_ref, w_norm);

        // operator=A: FD of the assembled Newton residual along d, in the
        // operator's own comparison space.
        evaluate_epsilon_ladder(
            c, cd.label, cd.aj, "A", prod_A_replay, actual_A,
            [&](float e) -> torch::Tensor {
                torch::NoGradGuard no_grad;
                auto Kp = (e == 0.0f)
                              ? c.K
                              : (c.K + e * d_unscaled).detach();
                auto Up = (c.U_stage + c.dt * c.gamma * Kp).detach();
                auto R = (Kp - c.compute_rhs(Up)).detach();
                if (cd.scaled_space && c.scaled) R = (c.S_inv_diag * R).detach();
                return R;
            },
            K_ref, d_norm);
    }

    // PR 9B commit 2: rw term-level bisection at the same (purity-gated)
    // operand.
    run_rw_term_bisection(c);
}

}  // namespace jvp_check

// ============================================================================
// PR 9B (review refactor): single authority for resolving the exact Krylov
// termination reason AND deriving the human-readable message from it — one
// message per reason, shared by solve_gmres and solve_fgmres. Resolution
// order is unchanged from PR 8.1: convergence wins; the restart-level guard
// next; then whichever early-exit detector fired (ArnoldiStagnation vs
// MidBudgetHopeless — previously conflated); then the internal-stop
// criterion; else the budget ran out. Terminal breakdown / NaN-retry /
// initial-convergence exits return through their dedicated sites and never
// reach this resolver.
// ============================================================================
struct KrylovTerminationResolution {
    WRFNewtonKrylovSolver::KrylovTerminationReason reason;
    std::string message;
};

static KrylovTerminationResolution resolve_krylov_termination(
    const char* prefix,
    bool converged,
    bool restart_stag_threshold,
    WRFNewtonKrylovSolver::KrylovTerminationReason early_exit_reason,
    bool internal_convergence,
    int total_arnoldi_iters,
    int max_iter,
    int restart,
    int actual_restarts) {
    using KTR = WRFNewtonKrylovSolver::KrylovTerminationReason;
    KrylovTerminationResolution out{KTR::MaxBudget, {}};
    if (converged) {
        out.reason = KTR::ToleranceReached;
    } else if (restart_stag_threshold) {
        out.reason = KTR::RestartStagnationThreshold;
    } else if (early_exit_reason == KTR::ArnoldiStagnation ||
               early_exit_reason == KTR::MidBudgetHopeless) {
        out.reason = early_exit_reason;
    } else if (internal_convergence) {
        out.reason = KTR::InternalConvergenceStop;
    }
    const std::string restart_suffix = " (restart " + std::to_string(actual_restarts)
                                     + "/" + std::to_string(max_iter) + ")";
    const std::string p(prefix);
    switch (out.reason) {
        case KTR::ToleranceReached:
            out.message = p + " converged";
            break;
        case KTR::RestartStagnationThreshold:
            out.message = p + " restart-stagnation-threshold early exit" + restart_suffix;
            break;
        case KTR::ArnoldiStagnation:
            out.message = p + " Arnoldi stagnation early exit" + restart_suffix;
            break;
        case KTR::MidBudgetHopeless:
            out.message = p + " mid-budget hopeless-policy early exit" + restart_suffix;
            break;
        case KTR::InternalConvergenceStop:
            out.message = p + " internal-stop criterion met before max restarts";
            break;
        default:  // MaxBudget
            out.message = (total_arnoldi_iters < max_iter * restart)
                ? p + " early exit before max restarts" + restart_suffix
                : p + " max iterations reached ("
                  + std::to_string(total_arnoldi_iters) + " Arnoldi)";
            break;
    }
    return out;
}

// GMRES implementation for Krylov subspace method
// ONE answer to "is the A*P^-1 probe on", shared by the capture in the Newton driver and the
// emit inside FGMRES. If they disagreed, capturing without emitting would waste work and emitting
// without capturing would print nothing and look like a broken probe.
//
// Evaluated once: getenv per Newton iteration is needless and the value cannot change mid-run.
// Uses THE project spelling authority, and reports an unrecognised value rather than silently
// treating it as off -- silence from a diagnostic is indistinguishable from a broken one.
static bool apinv_probe_armed() {
    static const bool armed = [] {
        const char* v = std::getenv("WRF_SDIRK3_APINV_DEFECT");
        if (!v) return false;
        const auto parsed = wrf::sdirk3::parse_bool_text(v);
        if (parsed == wrf::sdirk3::BoolText::Unrecognized) {
            std::cerr << "[SDIRK3 WARN] WRF_SDIRK3_APINV_DEFECT='" << v
                      << "' is not a recognised boolean; the A*P^-1 defect probe stays OFF. "
                         "Use 1/true/.true./t/yes." << std::endl;
            return false;
        }
        return parsed == wrf::sdirk3::BoolText::True;
    }();
    return armed;
}

namespace krylov_methods {

WRFNewtonKrylovSolver::GMRESResult solve_gmres(
    const std::function<torch::Tensor(const torch::Tensor&)>& A,
    const torch::Tensor& b,
    const torch::Tensor& x0,
    int stage_id,
    float ru_share_hint,
    int restart,
    float tol,
    int max_iter,
    const std::function<torch::Tensor(const torch::Tensor&)>& M_inv,
    const StateLayout* layout,
    const torch::Tensor* halo_mask,
    bool periodic_x,
    bool periodic_y) {
    
    torch::Tensor x = x0.clone();

    // P0 FIX: Compute initial residual
    // If x0 is zero, skip A(x0) computation since J*0 = 0
    // This prevents calling JVP with v=0 which triggers the guard
    // FIX (2025-12-05): Gate NoGradGuard on !use_autograd to preserve graph in AD mode
    torch::Tensor r_true;  // Unpreconditioned residual for convergence check
    {
        // Use guarded_item for the norm check to support autograd mode
        float x_norm = guarded_item<float>(x.norm());
        if (x_norm < 1e-14f) {
            // x0 is zero, so r = b - J*0 = b
            r_true = b.clone();
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                std::cerr << "[GMRES DEBUG] x0 is zero (norm=" << x_norm << "), skipping A(x0) computation" << std::endl;
                std::cerr << "  Initial residual r = b (no JVP call)" << std::endl;
            }
        } else {
            // x0 is non-zero, compute r = b - A(x)
            // In autograd mode, A(x) preserves graph; in FD mode, it doesn't matter
            r_true = b - A(x);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                std::cerr << "[GMRES DEBUG] x0 is non-zero (norm=" << x_norm << "), computed r = b - A(x0)" << std::endl;
            }
        }
    }

    // RIGHT-PRECONDITIONING: Use unpreconditioned residual for Arnoldi basis
    // Left-preconditioning minimizes ||M^{-1}(b-Ax)|| which is wrong when M changes norms dramatically.
    // Right-preconditioning minimizes ||b - A*M^{-1}*z|| = ||b - Ax|| (the TRUE residual).
    // FIX 2026-01-27: Changed from left to right preconditioning.
    torch::Tensor r_precond = r_true.clone();
    // NOTE: Do NOT apply M_inv here for right-preconditioning.
    // The initial residual r stays unpreconditioned.

    // CRITICAL FIX 2026-01-28: Zero halo regions in residual to prevent boundary artifacts
    // This ensures GMRES vectors don't contain halo contributions.
    // Uses helper function that handles partial periodicity correctly:
    // - For em_b_wave (periodic_x=true, periodic_y=false), only y-halos are zeroed.
    // v20.14r21: periodic_x/y now come from function parameters (instance state),
    // not global config. Callers pass options_.periodic_x/y.
    int halo_width = wrf::sdirk3::g_sdirk3_config.halo_width;
    zero_halo_regions(r_precond, halo_width, periodic_x, periodic_y);

    // CRITICAL FIX 2026-01-28: Also zero halos in r_true for CONSISTENT norm calculation!
    // Previous bug: Convergence used r_true with halos, GMRES used halo-zeroed r_precond.
    // This caused GMRES to fail eliminating residual components that were only in halos.
    auto r_true_inner = r_true.clone();
    zero_halo_regions(r_true_inner, halo_width, periodic_x, periodic_y);

    // FIX 2026-01-29: Compute ||b|| using halo-zeroed b for consistency with r_true_inner.
    // Previously bnorm used the full b (including halos), making the relative error
    // artificially smaller and causing GMRES to stop too early.
    auto b_inner = b.clone();
    zero_halo_regions(b_inner, halo_width, periodic_x, periodic_y);
    // R13.10 (P1-7): the j=0 ratio on the unpreconditioned path too. solve_fgmres had it
    // and solve_gmres did not, so the M-off arm -- the CONTROL arm of every A/B here -- kept
    // the "> 1" divergence rule that fires on a warm start the solve is fixing.
    float initial_rel_error_gmres = -1.0f;
    // R13.18 (deep review P1-4): the INITIAL D-objective. `rho_D_initial` was declared, its
    // header comment promised "both readings (initial and final)", and NOTHING wrote it -- so the
    // headline "both readings are on the record" was true of no record. Captured here, beside its
    // S sibling, from the same initial residual.
    float initial_rho_D_gmres = -1.0f;
    {
        torch::NoGradGuard ng_init;
        const float bn0 = guarded_item<float>(b_inner.norm());
        const float rn0 = guarded_item<float>(r_true_inner.norm());
        if (bn0 > 0.0f && std::isfinite(bn0) && std::isfinite(rn0)) {
            initial_rel_error_gmres = rn0 / bn0;
        }
    }

    // v20.14 r50: GMRES block-scaling (left-preconditioning with D⁻¹).
    // D[block] = ||r0[block]||₂. After scaling, each block contributes exactly 1 to ||D⁻¹r0||².
    // This prevents phi/theta O(10⁴) from masking u O(1-10) in GMRES's L2 minimization.
    // GMRES now solves: min ||D⁻¹(b - AM⁻¹z)|| — same solution x, different search path.
    // G1 storage (unpreconditioned copy): the discriminator for whether the indefiniteness is
    // intrinsic to A or introduced by M. Same capture, same analysis, M = I.
    static const auto ritz_capture_on = [] {
        static const bool on =
            wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_RITZ_CAPTURE");
        return on;
    };
    std::vector<std::vector<double>> ritz_H;

    torch::Tensor D_inv;  // per-element scaling vector, empty if disabled
    bool block_scaled = false;
    // v20.14 r50-fix: Block-scaling requires AUTOGRAD JVP. With FD JVP, D_inv amplifies
    // directional noise (D_inv can reach ~800 for small-residual blocks like w/mu),
    // causing ||x||→0. Only enable when forward-mode AD provides exact JVP.
    if (wrf::sdirk3::g_sdirk3_config.gmres_block_scale &&
        wrf::sdirk3::g_sdirk3_config.use_autograd &&
        layout && layout->is_valid() && layout->total_size == r_true_inner.numel()) {
        torch::NoGradGuard no_grad;
        D_inv = torch::ones_like(r_true_inner);
        auto r_cpu = r_true_inner.detach().to(torch::kCPU).contiguous();
        bool all_blocks_ok = true;
        for (const auto& blk : layout->blocks) {
            if (blk.start + blk.size > r_cpu.numel()) { all_blocks_ok = false; break; }
            float blk_norm = r_cpu.slice(0, blk.start, blk.start + blk.size)
                .norm().item<float>();
            if (blk_norm < 1e-20f) {
                // Block residual is essentially zero — don't scale (leave D_inv = 1)
                continue;
            }
            float scale = 1.0f / blk_norm;
            D_inv.slice(0, blk.start, blk.start + blk.size).fill_(scale);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[GMRES BLOCK-SCALE] " << blk.name
                          << ": ||r0||=" << blk_norm << " → D_inv=" << scale << "\n";
            }
        }
        if (all_blocks_ok) {
            block_scaled = true;
            // Scale the initial residual and RHS
            r_precond = r_precond * D_inv;
            b_inner = b_inner * D_inv;
        }
    }

    // v20.14 r50: Save unscaled bnorm for final return to Newton (trust-region needs it).
    // bnorm_safe uses D⁻¹-scaled b when block_scaled=true (for GMRES internal convergence).
    // bnorm_unscaled always uses the original b (for final rel_error report).
    auto bnorm_unscaled_tensor = safe_tensor_norm(b_inner);  // before scaling!
    if (block_scaled) {
        // b_inner was already scaled above — recompute unscaled from original b
        auto b_orig = b.clone();
        zero_halo_regions(b_orig, halo_width, periodic_x, periodic_y);
        bnorm_unscaled_tensor = safe_tensor_norm(b_orig);
    }
    auto bnorm_unscaled = torch::clamp(bnorm_unscaled_tensor, BNORM_MIN_THRESHOLD);

    auto bnorm_tensor = safe_tensor_norm(b_inner);
    auto bnorm_safe = torch::clamp(bnorm_tensor, BNORM_MIN_THRESHOLD);

    auto error_tensor = block_scaled
        ? safe_tensor_norm(D_inv * r_true_inner) / bnorm_safe
        : safe_tensor_norm(r_true_inner) / bnorm_safe;
    // R13.18 (deep review P1-4): the initial value of the objective the loop will stop on.
    { torch::NoGradGuard ng_rd0; initial_rho_D_gmres = guarded_item<float>(error_tensor); }

    // NUMERICAL STABILITY: Detect NaN in residual error immediately
    if (guarded_item<bool>(torch::isnan(error_tensor).any())) {
        std::cerr << "[GMRES ERROR] NaN detected in initial error_tensor" << std::endl;
        std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
        std::cerr << "  ||b|| = " << guarded_item<float>(bnorm_safe) << std::endl;
        throw std::runtime_error("GMRES initial residual error contains NaN");
    }

    auto converged = error_tensor < tol;
    // GRADIENT FIX: Use guarded_item for control flow check
    if (guarded_item<bool>(converged.all())) {
        float error_val = guarded_item<float>(error_tensor);
        float r_true_norm = guarded_item<float>(safe_tensor_norm(r_true_inner));
        std::cerr << "[GMRES] Initial residual already converged: error = " << error_val << " < tol = " << tol << std::endl;
        // v20.14r24: final_residual = ||r_true_inner|| (absolute), rel_error = error_val (relative).
        // r_true = RAW (not halo-zeroed), consistent with normal exit (line ~1127) and NaN paths.
        // Callers must apply halo zeroing to r_true before per-block analysis.
        // R13.19 (precision review P0-1): the STOP metric and the S metric, kept apart.
        //
        // This return used to set success = true unconditionally and store `error_val` -- which is
        // rho_D under block scaling, or rho_E under the WRMS experiment -- into `rel_error`, the
        // S-COORDINATE field. So rho_stop = 0.85 with rho_S = 0.99 and eta = 0.90 returned SUCCESS,
        // while the normal finaliser on the identical state returns failure and the classifier
        // calls it KrylovObjectiveMismatch. The one state R13.17-R13.18 exist to separate was
        // being merged back into a single success, on a PRODUCTION path: this value feeds
        // gmres_success, gmres_raw_rel_error, the trust-region prediction, the total-failure rule
        // and warm-start quality.
        //
        // `success` is the S-coordinate question, the same one the normal exit answers, and
        // `rel_error` always carries rho_S. The stop metric keeps its own field.
        const float bnorm_unscaled_val = guarded_item<float>(bnorm_unscaled);
        const float rho_S_here = (bnorm_unscaled_val > BNORM_MIN_THRESHOLD)
            ? (r_true_norm / bnorm_unscaled_val) : 1.0f;
        const bool S_reached_here = (rho_S_here < tol);
        WRFNewtonKrylovSolver::GMRESResult res{
                x, S_reached_here, 0, r_true_norm, rho_S_here,
                "Initial residual already converged",
                r_true.detach().clone(), 0, false, false};
        res.rho_S_initial = rho_S_here;
        res.rho_S_final = rho_S_here;
        res.rho_D_initial = error_val;      // the stop objective, named by stopping_metric
        res.rho_D_final = error_val;
        res.tolerance_applied = tol;
        res.D_tolerance_reached = (error_val < tol);
        res.S_tolerance_reached = S_reached_here;
        res.arnoldi_spent = 0;
        res.arnoldi_allowed = max_iter * restart;
        res.stopping_metric = static_cast<int>((block_scaled ? wrf::sdirk3::KrylovStoppingMetric::BlockD
                                          : wrf::sdirk3::KrylovStoppingMetric::IdentityS));
        res.termination_reason =
            WRFNewtonKrylovSolver::KrylovTerminationReason::InitialConverged;
        // R13.13 (red team round 4): the THIRD return path. R13.10 added the computation and
        // two of the three returns; this one kept the -1 sentinel, so a solve that converged
        // on entry was dropped from every r0-relative aggregate -- and it is solve_gmres, the
        // M-off CONTROL arm, so the A/B was comparing arms measured under two different rules.
        // Dropping the stage's BEST solve from a min-over-solves is the direction that
        // manufactures a stall.
        res.initial_rel_error = initial_rel_error_gmres;
                return res;
    }
    
    // GMRES FAILURE DETECTION: Track NaN/Inf occurrences in apply_jacobian
    int nan_failure_count = 0;
    const int max_nan_failures = wrf::sdirk3::g_sdirk3_config.nk_gmres_max_nan_retries;

    // FIX (2025-12-04): Track actual iterations for diagnostics
    int actual_restarts = 0;
    int total_arnoldi_iters = 0;
    bool terminated_by_restart_stag_threshold = false;
    bool terminated_by_arnoldi_stagnation = false;
    bool terminated_by_internal_convergence = false;
    // PR 8.1 (review P1): exact termination metadata. The two early-exit
    // detectors (consecutive Arnoldi stagnation vs the forced ru-dominant
    // mid-budget hopeless probe) previously collapsed into ONE boolean and
    // one message, so the classification could not tell which policy fired.
    using KTR = WRFNewtonKrylovSolver::KrylovTerminationReason;
    KTR early_exit_reason = KTR::MaxBudget;  // set ONLY by the two detectors
    int diag_probe_j = -1;
    float diag_probe_true_err = -1.0f;
    float diag_probe_floor = -1.0f;
    float diag_stag_ratio = -1.0f;
    int diag_stag_count = 0;

    for (int iter = 0; iter < max_iter; ++iter) {
        actual_restarts = iter + 1;  // Track current restart number
        // TIMING INSTRUMENTATION: Start GMRES iteration timer
        auto gmres_iter_start = std::chrono::high_resolution_clock::now();
        const bool log_gmres_v0_debug = (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 &&
                                         iter == 0 &&
                                         !wrf::sdirk3::g_sdirk3_config.use_autograd);

        // PERFORMANCE FIX: Move GMRES V0 diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 2 .item() syncs per GMRES call at debug_level >= 1
        // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
        if (log_gmres_v0_debug) {
            torch::NoGradGuard no_grad;  // Diagnostic logging only - safe in FD mode
            // FIX 2025-12-27: Use guarded_item to ensure CPU transfer before .item()
            float r_true_norm = guarded_item<float>(r_true.norm());
            float r_precond_norm = guarded_item<float>(r_precond.norm());
            std::cerr << "[GMRES V0 DEBUG] Initial vector generation:" << std::endl;
            std::cerr << "  Unpreconditioned residual ||r_true||: " << r_true_norm << std::endl;
            std::cerr << "  Preconditioned residual ||r_precond||: " << r_precond_norm << std::endl;
            std::cerr << "  Preconditioner effect: " << (r_true_norm > 1e-12 ? r_precond_norm / r_true_norm : 0.0f) << "x" << std::endl;
            std::cerr << "  r_precond shape: [" << r_precond.sizes() << "]" << std::endl;
            std::cerr << "  r_precond.dim(): " << r_precond.dim() << std::endl;
            std::cerr << "  r_precond is_contiguous: " << r_precond.is_contiguous() << std::endl;

            // Check if r was affected by halo zeroing
            if (r_precond.dim() == 3) {
                std::cerr << "  r_precond is 3D tensor - halo zeroing may have been applied" << std::endl;
            } else if (r_precond.dim() == 1) {
                std::cerr << "  r_precond is 1D flattened tensor - halo zeroing should NOT apply" << std::endl;
            }

            if (r_precond_norm < 1e-12f) {
                std::cerr << "  ERROR: Preconditioned residual has near-zero norm!" << std::endl;
                std::cerr << "  This will cause v_0 = r_precond / r_precond.norm() to be zero or NaN" << std::endl;
            }
        }

        // Arnoldi process (use preconditioned residual)
        std::vector<torch::Tensor> V;

        // NUMERICAL STABILITY: Check r_precond for NaN/Inf before normalization
        if (guarded_item<bool>(torch::isnan(r_precond).any()) ||
            guarded_item<bool>(torch::isinf(r_precond).any())) {
            std::cerr << "[GMRES ERROR] Preconditioned residual r_precond contains NaN/Inf" << std::endl;
            std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
            std::cerr << "  ||r_precond|| = " << guarded_item<float>(r_precond.norm()) << std::endl;
            throw std::runtime_error("GMRES: Preconditioner produced NaN/Inf in residual");
        }

        // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
        auto r_norm_tensor = safe_tensor_norm(r_precond);

        // NUMERICAL STABILITY: Guard against tiny/zero norm before division
        if (guarded_item<bool>(r_norm_tensor < 1e-12f)) {
            std::cerr << "[GMRES ERROR] Preconditioned residual norm too small for V[0] normalization" << std::endl;
            std::cerr << "  ||r_precond|| = " << guarded_item<float>(r_norm_tensor) << " < 1e-12" << std::endl;
            std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
            std::cerr << "  ||b|| = " << guarded_item<float>(b.norm()) << std::endl;
            throw std::runtime_error("GMRES: Cannot normalize V[0] - residual norm too small");
        }

        V.push_back(r_precond / r_norm_tensor);

        // PERFORMANCE FIX: Move V0 validation diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 2 .item() syncs per GMRES call at debug_level >= 1
        // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
        if (log_gmres_v0_debug) {
            torch::NoGradGuard no_grad;  // Safe in FD mode
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float r_norm_val = r_norm_tensor.to(torch::kCPU).item<float>();
            float v0_norm = V[0].norm().to(torch::kCPU).item<float>();
            std::cerr << "  r.norm() before V[0] creation: " << r_norm_val << std::endl;
            std::cerr << "  After normalization v_0 norm: " << v0_norm << std::endl;
            std::cerr << "  v_0 shape: [" << V[0].sizes() << "]" << std::endl;
            std::cerr << "  Ratio r.norm()/V[0].norm() = " << (v0_norm > 1e-12 ? r_norm_val / v0_norm : 0.0f) << std::endl;

            if (v0_norm < 1e-12f) {
                std::cerr << "  FATAL: v_0 has zero norm after normalization!" << std::endl;
                std::cerr << "  This indicates r / r.norm() produced zero/NaN" << std::endl;
            } else if (std::abs(v0_norm - 1.0f) > 0.01f) {
                std::cerr << "  WARNING: v_0 norm = " << v0_norm << " (expected 1.0)" << std::endl;
                std::cerr << "  Possible halo zeroing corrupted V[0]" << std::endl;
            } else {
                std::cerr << "  v_0 normalized correctly (norm ≈ 1.0)" << std::endl;
            }
        }
        
        // Hessenberg matrix and Givens rotation data
        // Phase 3A: Force H and s to CPU — avoids ~5000 tiny GPU kernel launches
        // for Hessenberg updates, Givens rotations, and back-substitution.
        // PyTorch auto-handles CPU↔GPU transfers for mixed-device scalar ops.
        auto cpu_opts = torch::TensorOptions().dtype(x.dtype());
        torch::Tensor H = torch::zeros({restart + 1, restart}, cpu_opts);
        torch::Tensor s = torch::zeros({restart + 1}, cpu_opts);
        // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
        s[0] = safe_tensor_norm(r_precond).cpu();
        
        // PERFORMANCE: Store Givens rotation coefficients as tensors to avoid .item() syncs
        // Previously extracted as float causing 2 CPU-GPU syncs per Arnoldi vector
        std::vector<torch::Tensor> cs(restart);
        std::vector<torch::Tensor> sn(restart);

        // DEVICE-AWARE: Pre-create constants on correct device to avoid CPU-GPU sync in loop
        // Using x.options() ensures these live on same device as state vectors (CPU/CUDA/MPS)
        const auto eps_safe = torch::full({}, 1e-8f, x.options());
        const auto one_tensor = torch::full({}, 1.0f, x.options());
        const auto zero_tensor = torch::full({}, 0.0f, x.options());

        // Track breakdown for numerical stability handling
        bool breakdown_occurred = false;
        // FIX 2026-01-31: Save converged residual from j-loop to skip redundant JVP at line 915
        torch::Tensor saved_r_true_converged;

        // v20.14r48: Arnoldi-level stagnation tracking for early termination.
        // If true_err improves by less than (1 - stag_ratio) for stag_window consecutive
        // checks, break the Arnoldi loop early (don't waste remaining budget).
        //
        // PERFORMANCE FIX (2026-02-19):
        // Stage-specific budget overrides set an upper bound on work, not a requirement
        // to exhaust all Arnoldi vectors. Keep stagnation early-exit enabled even when
        // stage2/stage3 budgets are active, so stagnating solves stop before burning JVPs.
            float prev_true_err = 1.0f;
            int stag_count = 0;
            auto& cfg_local = wrf::sdirk3::g_sdirk3_config;
            // Cache once per solve (Gemini #66): no_early_stop_enabled() has a thread-safe
            // static-init guard checked on every call; hoist it out of the Arnoldi loop.
            const bool no_early_stop = no_early_stop_enabled();
            // The gate's knobs, resolved for THIS stage. Shipped order reads the stage-2
            // knobs even at stage 3, which is why a stage-3 sweep also flips this gate.
            const auto gate_knobs = wrf::sdirk3::early_stop_gate_knobs(
                stage_id,
                cfg_local.stage2_gmres_restart, cfg_local.stage2_max_krylov_restarts,
                cfg_local.stage3_gmres_restart, cfg_local.stage3_max_krylov_restarts,
                stage_krylov_order());
            const bool aggressive_budget_stag_gate =
                (!no_early_stop &&
                 stage_id >= 2 &&
                 ru_share_hint > 0.98f &&
                 gate_knobs.restart > 0 &&
                 gate_knobs.max_restarts == 1);
            int stag_window = aggressive_budget_stag_gate
                                ? 1
                                : cfg_local.gmres_arnoldi_stag_window;
            float stag_ratio = cfg_local.gmres_arnoldi_stag_ratio;

        int j;
        for (j = 0; j < restart; ++j) {
            // RIGHT-PRECONDITIONING: w = A(M^{-1}(V[j]))
            // Apply preconditioner FIRST, then operator A.
            // This builds Krylov space for A*M^{-1} and minimizes ||b - A*M^{-1}*z||.
            // FIX 2026-01-27: Changed from left (w=M^{-1}(A(V[j]))) to right preconditioning.
            auto jvp_start = std::chrono::high_resolution_clock::now();
            torch::Tensor v_precond = V[j];
            if (M_inv) {
                v_precond = M_inv(V[j]);
            }
            torch::Tensor w = A(v_precond);
            auto jvp_end = std::chrono::high_resolution_clock::now();
            auto jvp_duration = std::chrono::duration_cast<std::chrono::milliseconds>(jvp_end - jvp_start).count();

            // NUMERICAL STABILITY: Check for NaN/Inf immediately after Jacobian application
            if (guarded_item<bool>(torch::isnan(w).any()) || guarded_item<bool>(torch::isinf(w).any())) {
                nan_failure_count++;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[GMRES ERROR] NaN/Inf in Arnoldi vector after A(V[" << j << "])" << std::endl;
                    std::cerr << "  iter=" << iter << " j=" << j
                              << " ||V[j]||=" << guarded_item<float>(V[j].norm()) << std::endl;
                    std::cerr << "  NaN failure count: " << nan_failure_count << "/" << max_nan_failures << std::endl;
                }

                if (nan_failure_count > max_nan_failures) {
                    // GMRES FAILURE RECOVERY: After max retries, return failure status for trust-region fallback
                    std::cerr << "[GMRES FAILURE] Exceeded max NaN retries (" << max_nan_failures
                              << "), returning failure status to trigger trust-region fallback" << std::endl;
                    // v20.14r25: Use halo-zeroed norm for final_residual (contract: all paths consistent).
                    auto r_true_nan = r_true.clone();
                    zero_halo_regions(r_true_nan, halo_width, periodic_x, periodic_y);
                    float r_norm = guarded_item<float>(safe_tensor_norm(r_true_nan));
                    // v20.14r37: Include current restart's j (same fix as early-breakdown path).
                    // r_true returned RAW (caller applies halo zeroing for per-block analysis).
                    WRFNewtonKrylovSolver::GMRESResult res{
                            torch::zeros_like(x0), false,
                            total_arnoldi_iters + j, r_norm, 1.0f,
                            "NaN failures exceeded max retries",
                            r_true.detach().clone(), iter, false, false};
                    res.termination_reason = KTR::NanRetryExhausted;
                    res.initial_rel_error = initial_rel_error_gmres;
                    return res;
                } else {
                    // Continue GMRES loop, hope next iteration succeeds
                    // OPT Pass34: Gate retry message + use \n (avoids flush in hot path)
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES] Continuing after NaN (retry " << nan_failure_count << ")\n";
                    }
                    break;  // Break Arnoldi loop, restart GMRES iteration
                }
            }

            // v20.14r27o: JVP timing is hot-path overhead — raise to debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5) {
                std::cerr << "[GMRES TIMING] Arnoldi j=" << j << ": JVP took " << jvp_duration << " ms" << std::endl;
            }

            // DIAGNOSTIC: Check raw JVP output before preconditioner (first few vectors)
            // PERFORMANCE: .item() causes CPU-GPU sync, only enable at debug_level >= 2
            // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
            // OPT Pass32: Batch 2 norms into single D2H transfer
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5 &&
                !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                torch::NoGradGuard no_grad;
                auto norms_cpu = torch::stack({w.norm(), V[j].norm()}).to(torch::kCPU);
                float w_raw_norm = norms_cpu[0].item<float>();
                float vj_norm = norms_cpu[1].item<float>();
                std::cerr << "[ARNOLDI] j=" << j << " After w = A(V[" << j << "]) (raw JVP):" << std::endl;
                std::cerr << "  ||w_raw|| = " << w_raw_norm << std::endl;
                std::cerr << "  ||V[" << j << "]|| = " << vj_norm << std::endl;

                // Check if ||w|| is already tiny before preconditioning
                if (w_raw_norm < 1e-6f) {
                    std::cerr << "  WARNING: ||A(V[" << j << "])|| = " << w_raw_norm
                              << " is very small BEFORE preconditioner!" << std::endl;
                    std::cerr << "  This suggests Jacobian column space is nearly 1D or rank-deficient" << std::endl;
                }
            }

            // RIGHT-PRECONDITIONING: M_inv was applied BEFORE A (above).
            // No post-A preconditioning needed.
            // FIX 2026-01-27: Removed left-preconditioning w = M_inv(w).

            // CRITICAL FIX 2026-01-28: Zero halo regions in new Arnoldi vector
            // This maintains halo boundary consistency throughout GMRES.
            // Uses helper function that handles partial periodicity correctly:
            // - For em_b_wave (periodic_x=true, periodic_y=false), only y-halos are zeroed.
            zero_halo_regions(w, halo_width, periodic_x, periodic_y);

            // v20.14 r50: Apply block-scaling D⁻¹ to Arnoldi vector.
            // GMRES now builds Krylov space for D⁻¹AM⁻¹ instead of AM⁻¹.
            if (block_scaled) {
                w = w * D_inv;
            }

            // Modified Gram-Schmidt orthogonalization with DGK reorthogonalization
            // v20.14 r49-fix: Daniel-Gragg-Kaufman criterion — if ||w_after|| < 0.7*||w_before||,
            // orthogonality is lost and a second MGS pass is needed.
            // PERFORMANCE DIAGNOSTIC: Measure orthogonalization overhead to identify sync points
            auto gramschmidt_start = std::chrono::high_resolution_clock::now();

            // Save pre-MGS norm for DGK criterion
            torch::Tensor w_norm_before_mgs = safe_tensor_norm(w);

            for (int i = 0; i <= j; ++i) {
                auto dot_start = std::chrono::high_resolution_clock::now();
                H[i][j] = torch::dot(w.flatten(), V[i].flatten());
                auto dot_end = std::chrono::high_resolution_clock::now();

                w = w - H[i][j] * V[i];
                auto subtract_end = std::chrono::high_resolution_clock::now();

                // v20.14r27o: GS timing is hot-path (fires per Arnoldi × per basis vector)
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5) {
                    auto dot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dot_end - dot_start).count();
                    auto subtract_ms = std::chrono::duration_cast<std::chrono::milliseconds>(subtract_end - dot_end).count();
                    std::cerr << "[GMRES TIMING] Gram-Schmidt j=" << j << " i=" << i
                              << ": dot=" << dot_ms << "ms, subtract=" << subtract_ms << "ms" << std::endl;
                }
            }

            // v20.14 r49-fix: DGK reorthogonalization criterion
            // If ||w_after|| < 0.7 * ||w_before||, run second MGS pass to restore orthogonality.
            // This addresses GMRES stagnation when cond(A) > ~3000 (common for S2 ru-dominated).
            {
                torch::Tensor w_norm_after_mgs = safe_tensor_norm(w);
                auto needs_reorth = w_norm_after_mgs < (0.7f * w_norm_before_mgs);
                if (guarded_item<bool>(needs_reorth)) {
                    for (int i = 0; i <= j; ++i) {
                        auto h_corr = torch::dot(w.flatten(), V[i].flatten());
                        H[i][j] = H[i][j] + h_corr;
                        w = w - h_corr * V[i];
                    }
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && j < 3) {
                        torch::NoGradGuard no_grad;
                        float nb = w_norm_before_mgs.to(torch::kCPU).item<float>();
                        float na = w_norm_after_mgs.to(torch::kCPU).item<float>();
                        float nr = safe_tensor_norm(w).to(torch::kCPU).item<float>();
                        std::cerr << "[GMRES REORTH] j=" << j
                                  << " before=" << nb << " after1=" << na
                                  << " after2=" << nr << " ratio=" << (na/nb) << "\n";
                    }
                }
            }

            auto gramschmidt_end = std::chrono::high_resolution_clock::now();
            auto gramschmidt_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(gramschmidt_end - gramschmidt_start).count();

            // v20.14r27o: GS total timing — raise to debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0) {
                std::cerr << "[GMRES TIMING] Gram-Schmidt j=" << j << " TOTAL: " << gramschmidt_total_ms << " ms" << std::endl;
            }

            // DIAGNOSTIC: Check orthogonalization result
            // PERFORMANCE: .item() sync - only at debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j == 0) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_00 = H[0][0].to(torch::kCPU).item<float>();
                float w_ortho_norm = w.norm().to(torch::kCPU).item<float>();
                std::cerr << "[ARNOLDI] After Gram-Schmidt orthogonalization:" << std::endl;
                std::cerr << "  H[0][0] = " << h_00 << std::endl;
                std::cerr << "  ||w - H[0][0]*V[0]|| = " << w_ortho_norm << std::endl;
                std::cerr << "  This will become H[1][0]" << std::endl;
            }

            // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
            H[j + 1][j] = safe_tensor_norm(w);

            // DIAGNOSTIC: Check breakdown condition
            // PERFORMANCE: .item() sync - only at debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j == 0) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_10 = H[1][0].to(torch::kCPU).item<float>();
                std::cerr << "[ARNOLDI] Breakdown check:" << std::endl;
                std::cerr << "  H[1][0] = " << h_10 << std::endl;
                std::cerr << "  Breakdown threshold: 1e-6" << std::endl;
                std::cerr << "  Will breakdown: " << (h_10 < 1e-6f ? "YES" : "NO") << std::endl;
            }

            // AUTOGRAD FIX: Use tensor comparison for breakdown detection
            // RELAXED THRESHOLD: Further relaxed to 1e-10 to handle ill-conditioned systems without preconditioner
            // GRADIENT FIX: Use guarded_item to prevent gradient break
            auto breakdown_check_start = std::chrono::high_resolution_clock::now();
            auto h_small = torch::abs(H[j + 1][j]) < 1e-10f;
            bool is_breakdown = guarded_item<bool>(h_small.all());
            auto breakdown_check_end = std::chrono::high_resolution_clock::now();
            auto breakdown_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(breakdown_check_end - breakdown_check_start).count();

            // v20.14r27o: Breakdown timing — raise to debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5) {
                std::cerr << "[GMRES TIMING] Breakdown check j=" << j << ": " << breakdown_check_ms << " ms" << std::endl;
            }

            // 9F.D101 (review P0-A): the Givens reduction for the CURRENT column, shared by
            // the breakdown path and the normal path so the two cannot drift apart.
            if (ritz_capture_on()) {   // pre-reduction Hessenberg column, M = I path
                torch::NoGradGuard ng_ritz;
                std::vector<double> col;
                col.reserve(static_cast<size_t>(j) + 2);
                for (int i = 0; i <= j + 1 && i < static_cast<int>(H.size(0)); ++i) {
                    col.push_back(H[i][j].to(torch::kCPU).item<double>());
                }
                ritz_H.push_back(std::move(col));
            }

            auto reduce_current_column = [&]() {
                for (int i = 0; i < j; ++i) {
                    auto h_i_j = H[i][j].clone();
                    auto h_ip1_j = H[i + 1][j].clone();
                    H[i][j] = cs[i] * h_i_j + sn[i] * h_ip1_j;
                    H[i + 1][j] = -sn[i] * h_i_j + cs[i] * h_ip1_j;
                }
                auto h_j_t = H[j][j];
                auto h_jp1_t = H[j + 1][j];
                auto r_g = torch::sqrt(h_j_t * h_j_t + h_jp1_t * h_jp1_t);
                auto r_g_safe = torch::where(r_g > 1e-8f, r_g, eps_safe);
                auto safe_mask = (r_g > 1e-8f);
                cs[j] = torch::where(safe_mask, h_j_t / r_g_safe, one_tensor);
                sn[j] = torch::where(safe_mask, h_jp1_t / r_g_safe, zero_tensor);
                H[j][j] = r_g;
                H[j + 1][j] = 0.0f;
                auto s_j = s[j].clone();
                auto s_jp1 = s[j + 1].clone();
                s[j] = cs[j] * s_j + sn[j] * s_jp1;
                s[j + 1] = -sn[j] * s_j + cs[j] * s_jp1;
            };

            if (is_breakdown) {
                breakdown_occurred = true;  // Track for numerical stability handling
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    std::cerr << "[ARNOLDI] BREAKDOWN at j=" << j
                              << ", H[" << (j+1) << "][" << j << "] = "
                              << guarded_item<float>(H[j + 1][j]) << std::endl;
                    // FIX 2026-01-29: Breakdown means Krylov subspace is exhausted.
                    // If j==0, no useful direction was found — this is NOT convergence.
                    // If j>0, we have a partial solution that may still be useful,
                    // but we should NOT report this as "converged" to the Newton solver,
                    // as it can cause trust-region to accept bad steps → stagnation.
                    std::cerr << "[ARNOLDI] Breakdown at j=" << j
                              << " — extracting best available solution" << std::endl;
                }
                // 9F.D101 (review P0-A): COMPLETE THE REDUCTION, then leave.
                //
                // The old code broke out BEFORE the Givens block, so this column was never
                // rotated and the back-substitution below then operated on an H that was not
                // upper-triangular. And h_{j+1,j} = 0 is precisely the case where the Krylov
                // space ALREADY CONTAINS the solution -- with a zero subdiagonal the new
                // rotation is the identity, so reducing here is correct and costs nothing.
                //
                // What must NOT run is V.push_back(w / H[j+1][j]) below: that divides by ~0.
                // Skipping the BASIS VECTOR is the right response to breakdown; skipping the
                // CORRECTION was the defect.
                reduce_current_column();
                j++;
                break;  // no new basis vector, but the projected system is now solvable
            }
            
            V.push_back(w / H[j + 1][j]);
            
            // 9F.D101: the SAME reduction the breakdown path uses -- one implementation.
            reduce_current_column();

            // v20.14r48: PERFORMANCE — Periodic true residual check (replaces hess_est-based).
            // Old: hess_est < 3*tol triggers expensive A(x_trial) check → JVP/Arnoldi ~1.6.
            // New: periodic sampling (j >= start_j && j % period == 0, always check last j).
            // Expected: JVP/Arnoldi ratio → ~1.1, ~30% linear solve time reduction.
            bool gmres_converged = false;

            // Cheap convergence estimate from Givens rotation (no JVP needed)
            // GR v9 G1: Gate behind debug_level to avoid GPU sync in production
            float hess_estimate = -1.0f;  // sentinel for non-debug path
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                hess_estimate = guarded_item<float>(torch::abs(s[j + 1]) / bnorm_safe);
            }

            // Periodic true residual check: j >= start_j && j % period == 0, or last j.
            // v20.14r52: In stage>=2 ru-dominant solves, periodic true residual probes
            // are usually low-value but expensive (extra A(x_trial) ≈ extra JVP work).
            // Keep mandatory check at last Arnoldi index; skip intermediate probes for
            // ru-dominant stage>=2 to reduce wasted compute.
            int start_j = wrf::sdirk3::g_sdirk3_config.gmres_true_residual_start_j;
            int period = wrf::sdirk3::g_sdirk3_config.gmres_true_residual_period;
            // v20.14r54: When stage-aware GMRES budget is explicitly enabled for stage>=2,
            // avoid extra periodic true-residual probes and keep only the mandatory
            // last-Arnoldi true-residual check. This is a default-off behavior because
            // it activates only when stage2_gmres_restart>0 is configured.
            if (stage_id >= 2 && gate_knobs.restart > 0) {
                start_j = std::max(start_j, restart - 1);
            }
            bool skip_periodic_true_check = (stage_id >= 2 && ru_share_hint > 0.9f);
            // For stage-budgeted ru-dominant solves, force one mid-budget probe.
            // If true residual barely improves, Arnoldi stagnation can terminate early
            // without consuming the full restart budget.
            const bool mid_budget_probe =
                aggressive_budget_stag_gate && (j == std::max(2, restart / 2));
            bool near_convergence = (j == restart - 1) ||
                                    mid_budget_probe ||
                                    (!skip_periodic_true_check &&
                                     j >= start_j && (j - start_j) % period == 0);

            if (near_convergence) {
                // Solve H*y_trial = s for the current Krylov subspace [0...j]
                torch::Tensor y_trial = torch::zeros({j + 1}, x.options());
                for (int i = j; i >= 0; --i) {
                    y_trial[i] = s[i];
                    for (int k = i + 1; k <= j; ++k) {
                        y_trial[i] = y_trial[i] - H[i][k] * y_trial[k];
                    }
                    auto h_diag_abs = torch::abs(H[i][i]);
                    y_trial[i] = torch::where(h_diag_abs > 1e-10f,
                                             y_trial[i] / H[i][i],
                                             torch::zeros_like(y_trial[i]));
                }

                // RIGHT-PRECONDITIONING: x_trial = x + M^{-1}(V*y_trial)
                torch::Tensor z_trial = torch::zeros_like(x);
                for (int i = 0; i <= j; ++i) {
                    z_trial = z_trial + y_trial[i] * V[i];
                }
                torch::Tensor correction = z_trial;
                if (M_inv) {
                    correction = M_inv(z_trial);
                }
                torch::Tensor x_trial = x + correction;

                // Compute TRUE unpreconditioned residual: r_true_trial = b - A(x_trial)
                torch::Tensor r_true_trial = b - A(x_trial);

                // v20.14r27i: Use unified helper (halo-zeroed for 3D, raw for 1D packed)
                // v20.14 r50: When block-scaled, measure convergence in D⁻¹-norm.
                // bnorm_safe was already computed from D⁻¹*b, so error_true is in scaled space.
                torch::Tensor r_for_norm = block_scaled
                    ? (r_true_trial * D_inv) : r_true_trial;
                auto r_true_norm = gmres_residual_norm(r_for_norm, halo_width, periodic_x, periodic_y);
                auto error_true = r_true_norm / bnorm_safe;
                gmres_converged = guarded_item<bool>((error_true < tol).all());

                // Budgeted ru-dominant stage guard:
                // If a forced mid-budget probe is still far from tolerance, terminate
                // this restart early instead of burning the remaining Arnoldi budget.
                bool budget_probe_hopeless = false;
                if (mid_budget_probe && aggressive_budget_stag_gate && !gmres_converged) {
                    float err_mid = guarded_item<float>(error_true);
                    const float hopeless_floor = std::max(0.9f, 2.0f * tol);
                    if (err_mid > hopeless_floor) {
                        budget_probe_hopeless = true;
                        saved_r_true_converged = r_true_trial;
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[GMRES] Budget probe early-exit: true_err=" << err_mid
                                      << " > " << hopeless_floor
                                      << " (stage=" << stage_id
                                      << ", ru_share=" << ru_share_hint << ")\n";
                        }
                    }
                }

                // FIX 2026-01-31: Save r_true_trial to avoid redundant A(x) JVP after j-loop.
                // When j == restart-1: y_trial == y (same H,s system), so x_trial == x_updated
                // and r_true_trial == b - A(x_updated). Saves 1 JVP per non-convergent restart.
                if (gmres_converged || j == restart - 1) {
                    saved_r_true_converged = r_true_trial;
                }

                // v20.14r48: Arnoldi stagnation tracking.
                // Track true_err improvement across consecutive checks.
                bool arnoldi_stagnated = false;
                if (!gmres_converged && !no_early_stop) {
                    float err_val_stag = guarded_item<float>(error_true);
                    float ratio = (prev_true_err > 1e-30f) ? err_val_stag / prev_true_err : 0.0f;
                    if (ratio > stag_ratio) {
                        stag_count++;
                    } else {
                        stag_count = 0;  // reset on improvement
                    }
                    prev_true_err = err_val_stag;
                    if (stag_count >= stag_window) {
                        arnoldi_stagnated = true;
                        saved_r_true_converged = r_true_trial;  // save for reuse
                    }
                }

                // v20.14r48: Hot-loop log — use '\n' not std::endl, no flush()
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    float err_val = error_true.to(torch::kCPU).item<float>();
                    std::cerr << "[GMRES] restart=" << (iter + 1) << " j=" << j
                              << ": hess_est=" << std::fixed << std::setprecision(4) << hess_estimate
                              << ", true_err=" << err_val
                              << (gmres_converged ? " CONVERGED" : "")
                              << (arnoldi_stagnated ? " STAGNATED" : "")
                              << std::defaultfloat << '\n';
                }

                // v20.14r48: Early termination on Arnoldi stagnation.
                if (arnoldi_stagnated) {
                    terminated_by_arnoldi_stagnation = true;
                    // PR 8.1: record WHICH detector fired, with its inputs.
                    early_exit_reason = KTR::ArnoldiStagnation;
                    diag_probe_j = j;
                    diag_probe_true_err = prev_true_err;
                    diag_stag_ratio = stag_ratio;
                    diag_stag_count = stag_count;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES] Arnoldi stagnation at j=" << j
                                  << " (" << stag_count << " consecutive, ratio>"
                                  << stag_ratio << ") — early exit\n";
                    }
                    j++;  // advance past current to match convergence exit convention
                    break;
                }

                if (budget_probe_hopeless) {
                    terminated_by_arnoldi_stagnation = true;
                    // PR 8.1: the forced mid-budget hopeless probe is a
                    // DIFFERENT policy from the stagnation detector above.
                    early_exit_reason = KTR::MidBudgetHopeless;
                    diag_probe_j = j;
                    diag_probe_true_err = guarded_item<float>(error_true);
                    diag_probe_floor = std::max(0.9f, 2.0f * tol);
                    diag_stag_ratio = stag_ratio;
                    diag_stag_count = stag_count;
                    j++;  // keep convention with other early exits
                    break;
                }

                // v20.11: Per-block true residual at end of each restart
                if (layout && layout->is_valid() &&
                    layout->total_size == r_true_trial.numel() &&
                    (j == restart - 1 || gmres_converged) &&
                    wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    // v20.14r15: Apply halo zeroing for consistency with GMRES true_err
                    auto r_halo = r_true_trial.detach();
                    if (halo_mask && halo_mask->numel() == r_halo.numel()) {
                        r_halo = r_halo * halo_mask->to(r_halo.dtype()).to(r_halo.device());
                    }
                    auto r_cpu = r_halo.to(torch::kCPU).contiguous();
                    float bnorm_val = bnorm_safe.to(torch::kCPU).item<float>();
                    std::ostringstream bss;
                    // v20.14r27k: Label clarified — values are r_block/||b|| (linear relative error per block).
                    bss << "[GMRES BLOCK r/b] restart=" << (iter + 1) << " j=" << j;
                    for (const auto& blk : layout->blocks) {
                        if (blk.start + blk.size <= r_cpu.numel()) {
                            float r_n = r_cpu.slice(0, blk.start, blk.start + blk.size)
                                .norm().item<float>();
                            float frac = (bnorm_val > 0) ? (r_n / bnorm_val) : 0.0f;
                            bss << " " << blk.name << "=" << std::fixed
                                << std::setprecision(4) << frac;
                        }
                    }
                    std::cerr << bss.str() << std::defaultfloat << '\n';
                }
            }

            if (gmres_converged) {
                j++;
                break;
            }
        }

        // DIAGNOSTIC: Check H matrix conditioning (expensive - debug only)
        // OPT Pass33+: Use configurable heavy sample period (0=every iteration)
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 &&
            (wrf::sdirk3::g_sdirk3_config.debug_heavy_sample_period == 0 ||
             (iter + 1) % wrf::sdirk3::g_sdirk3_config.debug_heavy_sample_period == 0 || iter == 0)) {
            torch::NoGradGuard no_grad;
            float h_min = 1e20f, h_max = 0.0f;
            for (int i = 0; i < j; ++i) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_ii = std::abs(H[i][i].to(torch::kCPU).item<float>());
                h_min = std::min(h_min, h_ii);
                h_max = std::max(h_max, h_ii);
            }
            float condition_est = (h_min > 1e-14f) ? (h_max / h_min) : 1e20f;
            std::cerr << "[GMRES H-DIAG] Restart " << (iter + 1)
                      << ": min|H[i][i]| = " << h_min
                      << ", max|H[i][i]| = " << h_max
                      << ", cond ~ " << condition_est << std::endl;
        }

        // NUMERICAL STABILITY: If breakdown occurred very early, skip update
        // When j <= 2, the Krylov subspace is too small for reliable solution
        // CRITICAL FIX 2026-01-28: Return success ONLY if residual actually converged!
        // Previous bug: Always returned success=true, allowing Newton to accept unconverged solution.
        // 9F.D101 (review P0-A): the "j <= 2 -> return x as-is" branch is DELETED.
        //
        // It returned the INITIAL GUESS and the cycle-start residual, discarding the
        // exact Krylov correction, with the comment "x hasn't been updated in this
        // restart cycle". For a HAPPY breakdown that correction is the exact solution:
        // A = 0.5I breaks down at j = 1 and must give x = 2b in one step. It returned
        // x = 0, and I previously mis-attributed that to a "WRF-shaped harness"
        // limitation in a test comment. It was this branch.
        //
        // With the column now properly reduced above, the normal back-substitution and
        // solution update below handle a breakdown column correctly, so there is
        // nothing left for a special case to protect against.

        // Solve least squares problem with diagonal check
        torch::Tensor y = torch::zeros({j}, x.options());
        bool singular_detected = false;
        for (int i = j - 1; i >= 0; --i) {
            y[i] = s[i];
            for (int k = i + 1; k < j; ++k) {
                y[i] = y[i] - H[i][k] * y[k];
            }

            // GR v8 F4: Sign-preserving regularized division (preserves direction info)
            auto h_diag_abs = torch::abs(H[i][i]);
            auto h_safe = torch::where(h_diag_abs > 1e-10f,
                                       H[i][i],
                                       torch::copysign(torch::tensor(1e-10f, H[i][i].options()), H[i][i]));
            y[i] = y[i] / h_safe;

            // DIAGNOSTIC: Check for singularity (gated to avoid .item() sync in production)
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_diag_val = h_diag_abs.to(torch::kCPU).item<float>();
                if (h_diag_val < 1e-10f) {
                    ERROR_PRINT("WARNING: GMRES H matrix nearly singular at diagonal " << i
                              << ", |H[" << i << "][" << i << "]| = " << h_diag_val);
                    singular_detected = true;
                }
            }
        }
        
        if (singular_detected) {
            ERROR_PRINT("ERROR: GMRES detected singular/ill-conditioned system!");
            ERROR_PRINT("  This typically means:");
            ERROR_PRINT("  - The Jacobian (I + dt*gamma*dF/dU) is nearly singular");
            ERROR_PRINT("  - The timestep dt=" << dt << ", gamma=" << gamma);
            ERROR_PRINT("  - dt*gamma=" << dt*gamma << " (affects Jacobian conditioning)");
            ERROR_PRINT("  - If dt*gamma*eigenvalue ≈ -1, the system becomes singular");
            ERROR_PRINT("  - The system may have reached a bifurcation point");

            // Additional diagnostics (avoid .item() to preserve autodiff)
            ERROR_PRINT("\nDEBUG: H matrix diagonals (as tensors):");
            for (int idx = 0; idx < j && idx < 10; ++idx) {
                ERROR_PRINT("  H[" << idx << "][" << idx << "] = " << H[idx][idx]);
            }
        }

        // PERFORMANCE FIX: Move least-squares diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 2+ .item() syncs per GMRES iteration at debug_level >= 1
        // OPT Pass32: Batch y.norm() and y.abs().max() into single D2H transfer
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && iter == 0) {
            torch::NoGradGuard no_grad;
            auto y_stats_cpu = torch::stack({y.norm(), y.abs().max()}).to(torch::kCPU);
            float y_norm = y_stats_cpu[0].item<float>();
            float y_max = y_stats_cpu[1].item<float>();
            std::cerr << "[GMRES LS] Least-squares solution y:" << std::endl;
            std::cerr << "  j (Krylov dimension): " << j << std::endl;
            std::cerr << "  ||y|| = " << y_norm << std::endl;
            std::cerr << "  max|y| = " << y_max << std::endl;

            // Check H matrix diagonal for breakdown
            std::cerr << "[GMRES LS] H matrix diagonals:" << std::endl;
            for (int idx = 0; idx < j && idx < 5; ++idx) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                // LINT:DIAG_OK - NoGradGuard is at line 838, diagnostic block
                float h_diag = H[idx][idx].to(torch::kCPU).item<float>();
                std::cerr << "  H[" << idx << "][" << idx << "] = " << h_diag << std::endl;
                if (std::abs(h_diag) < 1e-8) {
                    std::cerr << "    WARNING: Near-singular diagonal!" << std::endl;
                }
            }

            if (y_norm < 1e-12f) {
                std::cerr << "  WARNING: y has zero norm - x will not be updated!" << std::endl;
            }
        }

        // NUMERICAL STABILITY: Check y for NaN/Inf before update
        if (guarded_item<bool>(torch::isnan(y).any()) || guarded_item<bool>(torch::isinf(y).any())) {
            std::cerr << "[GMRES ERROR] NaN/Inf detected in y (backsolve result) before update" << std::endl;
            std::cerr << "  j=" << j << " (Krylov dimension)" << std::endl;
            std::cerr << "  ||y|| = " << guarded_item<float>(y.norm()) << std::endl;

            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                torch::NoGradGuard no_grad;
                std::cerr << "[GMRES DEBUG] H matrix diagonals:" << std::endl;
                for (int idx = 0; idx < j && idx < 5; ++idx) {
                    std::cerr << "  H[" << idx << "][" << idx << "] = "
                              << guarded_item<float>(H[idx][idx]) << std::endl;
                }
            }

            throw std::runtime_error("GMRES backsolve produced NaN/Inf in solution vector y");
        }

        // RIGHT-PRECONDITIONING: x = x + M^{-1}(sum(y[i]*V[i]))
        // FIX 2026-01-27: Apply M_inv to correction for right preconditioning.
        {
            torch::Tensor z_update = torch::zeros_like(x);
            for (int i = 0; i < j; ++i) {
                z_update = z_update + y[i] * V[i];
            }
            if (M_inv) {
                z_update = M_inv(z_update);
            }
            x = x + z_update;
        }

        // CRITICAL FIX: Compute new residual IMMEDIATELY after solution update
        // The convergence check must use the FRESH residual r_true = b - A(x_new),
        // not the stale residual from before the update!
        // FIX 2026-01-31: Skip redundant JVP when converged inside j-loop.
        // saved_r_true_converged == b - A(x_trial) where x_trial == x after update
        // (same H, s, y values used in both paths), saving 1 JVP per Newton iter.
        if (saved_r_true_converged.defined()) {
            r_true = saved_r_true_converged;
            saved_r_true_converged.reset();  // Clear for next restart
        } else {
            r_true = b - A(x);
        }
        // RIGHT-PRECONDITIONING: r_precond = r_true (no preconditioning of residual)
        // FIX 2026-01-27: Removed M_inv application to residual for right preconditioning.
        r_precond = r_true.clone();

        // CRITICAL FIX 2026-01-28: Apply halo zeroing CONSISTENTLY after restart!
        // Previous bug: Halo zeroing only applied to initial residual, not after restart.
        // This caused halos to leak into Krylov basis and GMRES couldn't eliminate them.
        zero_halo_regions(r_precond, halo_width, periodic_x, periodic_y);

        // v20.14 r50: Apply block scaling to new residual for next restart
        if (block_scaled) {
            r_precond = r_precond * D_inv;
        }

        // CRITICAL FIX 2026-01-28: Use halo-zeroed residual for error calculation too
        auto r_true_inner = r_true.clone();
        zero_halo_regions(r_true_inner, halo_width, periodic_x, periodic_y);

        // Now compute error_tensor from FRESH residual (halo-zeroed for consistency)
        // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
        // v20.14 r50: Use scaled norm for internal convergence check
        error_tensor = block_scaled
            ? safe_tensor_norm(D_inv * r_true_inner) / bnorm_safe
            : safe_tensor_norm(r_true_inner) / bnorm_safe;

        if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_TRAJECTORY")) {
            torch::NoGradGuard ng_traj_u;
            std::cerr << "SDIRK3_KRYLOV_TRAJECTORY_UNPRECOND restart=" << iter
                      << " scaled=" << (block_scaled ? 1 : 0)
                      << " error=" << guarded_item<float>(error_tensor)
                      << std::endl << std::flush;
        }

        if (ritz_capture_on() && !ritz_H.empty()) {   // M = I numerical range
            torch::NoGradGuard ng_ritz_analysis;
            const int m = static_cast<int>(ritz_H.size());
            auto Hs = torch::zeros({m, m}, torch::kFloat64);
            auto acc = Hs.accessor<double, 2>();
            for (int jj = 0; jj < m; ++jj) {
                const auto& col = ritz_H[static_cast<size_t>(jj)];
                for (int ii = 0; ii < m && ii < static_cast<int>(col.size()); ++ii) {
                    acc[ii][jj] = col[static_cast<size_t>(ii)];
                }
            }
            const auto sym = 0.5 * (Hs + Hs.transpose(0, 1));
            const auto evals = torch::linalg_eigvalsh(sym);
            // R13.8: NAME THE COORDINATES. "UNPRECOND" means M = I -- it does NOT mean
            // unscaled. When block scaling is on, the operator this Hessenberg projects is
            // D^-1 S^-1 A S, not raw physical A. A negative eigenvalue of the projected
            // symmetric part is a genuine witness FOR THE OPERATOR GMRES ITERATED; it does
            // not transfer to A without this receipt, and the record used to omit it.
            std::cerr << "SDIRK3_NUMERICAL_RANGE_UNPRECOND"
                      << " operator_coordinates="
                      << (block_scaled ? "D_left_S_krylov" : "S_krylov")
                      << " block_scaled=" << (block_scaled ? 1 : 0)
                      << " right_precond=identity"
                      << " m=" << m
                      << " min_eig_sym=" << evals.min().item<double>()
                      << " max_eig_sym=" << evals.max().item<double>()
                      << " n_negative=" << (evals < 0.0).sum().item<int64_t>() << "/" << m
                      << " definite=" << (evals.min().item<double>() > 0.0 ? 1 : 0);

            // R13.9: the two defects that decide whether H_m is a PROJECTION at all, and an
            // INDEPENDENT re-evaluation of the negative witness.
            //
            // A negative eigenvalue of sym(H_m) is a witness only if H_m = V^T B V, and that
            // holds only if V is orthonormal and the Arnoldi relation B V_m = V_{m+1} H_m
            // actually held in floating point. Neither was measured; both are cheap once the
            // basis is in scope. The witness is then re-evaluated on the full operator:
            // v_min = V y_min, q = <v_min, B v_min> / <v_min, v_min>. If q < 0 the negative
            // direction is real in the space the solver iterates, by a path that does not go
            // through the Hessenberg's arithmetic at all.
            if (static_cast<int>(V.size()) >= m + 1) {
                torch::NoGradGuard ng_defects;
                // B as the loop applied it: the operator, halo-zeroed, then D^-1 when scaled.
                auto apply_B = [&](const torch::Tensor& v) {
                    auto w_ = A(v);
                    zero_halo_regions(w_, halo_width, periodic_x, periodic_y);
                    if (block_scaled) w_ = w_ * D_inv;
                    return w_;
                };
                // Orthogonality: ||V^T V - I||_F over the m vectors the projection used.
                auto Vm = torch::stack(std::vector<torch::Tensor>(V.begin(), V.begin() + m), 1)
                              .to(torch::kFloat64);           // n x m
                const auto gram = Vm.transpose(0, 1).matmul(Vm);
                // R13.15 (external review P1-3): DEVICE-SAFE. `eye`/`zeros` were built on the
                // CPU with a bare dtype while `Vm` keeps whatever device the solver runs on, so
                // every one of these combinations threw on CUDA/MPS. CI pins CPU Torch, so the
                // path could not be caught there. Build each helper with the options of the
                // tensor it is combined with.
                const double e_orth =
                    (gram - torch::eye(m, Vm.options())).norm().item<double>();
                // Arnoldi relation: ||B V_m - V_{m+1} Hbar_m||_F / ||B V_m||_F, with Hbar the
                // (m+1) x m Hessenberg the loop produced, reconstructed from ritz_H.
                // Filled through a CPU accessor (accessor<> requires CPU), then moved to the
                // device it is multiplied against.
                auto Hbar_cpu = torch::zeros({m + 1, m}, torch::kFloat64);
                {
                    auto hb = Hbar_cpu.accessor<double, 2>();
                    for (int jj = 0; jj < m; ++jj) {
                        const auto& col = ritz_H[static_cast<size_t>(jj)];
                        for (int ii = 0; ii <= m && ii < static_cast<int>(col.size()); ++ii) {
                            hb[ii][jj] = col[static_cast<size_t>(ii)];
                        }
                    }
                }
                auto Vm1 = torch::stack(
                    std::vector<torch::Tensor>(V.begin(), V.begin() + m + 1), 1)
                        .to(torch::kFloat64);                 // n x (m+1)
                std::vector<torch::Tensor> BV_cols;
                BV_cols.reserve(m);
                for (int jj = 0; jj < m; ++jj) {
                    BV_cols.push_back(apply_B(V[static_cast<size_t>(jj)]).to(torch::kFloat64));
                }
                const auto BV = torch::stack(BV_cols, 1);      // n x m
                const double n_BV = BV.norm().item<double>();
                // Moved to the basis' device for the multiply.
                const auto Hbar = Hbar_cpu.to(Vm.options());
                const double e_arnoldi =
                    n_BV > 0.0 ? (BV - Vm1.matmul(Hbar)).norm().item<double>() / n_BV : -1.0;
                // The independent witness. The m x m eigenproblem stays on the CPU -- it is tiny
                // and linalg_eigh is not available on every backend -- and only the eigenvector
                // is moved to the basis' device, which is where it is multiplied.
                auto eig = torch::linalg_eigh(sym.to(torch::kCPU));
                const auto y_min =
                    std::get<1>(eig).select(1, 0).to(Vm.options());  // eigvec of min eigval
                const auto v_min = Vm.matmul(y_min);               // n
                const auto Bv = apply_B(v_min.to(V[0].scalar_type())).to(torch::kFloat64);
                const double vv = v_min.dot(v_min).item<double>();
                const double q_min_direct =
                    vv > 0.0 ? v_min.dot(Bv).item<double>() / vv : 0.0 / 0.0;
                // Referee X3 test (1), zero matvecs: ||Hbar e_i|| = ||B v_i|| with ||v_i|| = 1,
                // so the column norms ARE the operator's magnitude on the directions GMRES
                // built, and 1/||Hbar e_i|| is the identity term's share of the operator there.
                // The 10-30% figure this comment used to quote was an ESTIMATE and is REFUTED:
                // measured directly (identity_frac_rhs_dir / e_hom_rhs_dir in the frozen A/B
                // probe) the identity term carries 0.39% relative error, not 5-20%.
                double hcol_min = std::numeric_limits<double>::infinity(), hcol_max = 0.0;
                for (int jj = 0; jj < m; ++jj) {
                    const double cn = Hbar.select(1, jj).norm().item<double>();
                    hcol_min = std::min(hcol_min, cn);
                    hcol_max = std::max(hcol_max, cn);
                }
                // Referee C6(d): does the negative curvature live in the DIAGONAL blocks or in
                // the coupling? <v_min, B_bd v_min> with B_bd = sum_q P_q B P_q -- six block-
                // restricted matvecs on the witness. If this is positive while the full form is
                // negative, no by-variable block-diagonal M is the right class.
                double q_min_blockdiag = 0.0 / 0.0;
                // R13.13: the SUM was the only thing kept, and it names no culprit. The
                // per-block terms are already computed here -- emitting them says WHICH
                // variable carries the negative curvature, which is what a by-variable
                // preconditioner would have to fix. Each term is reported with the witness's
                // MASS in that block: without the mass a small term is ambiguous between "this
                // block is fine" and "the witness barely lives here", and this project has been
                // caught by exactly that ambiguity before.
                std::string blockdiag_rows;
                if (layout && layout->is_exact && layout->total_size == v_min.numel()) {
                    double acc = 0.0;
                    for (const auto& blk : layout->blocks) {
                        auto vq = torch::zeros_like(v_min);
                        vq.slice(0, blk.start, blk.start + blk.size)
                            .copy_(v_min.slice(0, blk.start, blk.start + blk.size));
                        const auto Bvq = apply_B(vq.to(V[0].scalar_type())).to(torch::kFloat64);
                        const double term = vq.slice(0, blk.start, blk.start + blk.size)
                                   .dot(Bvq.slice(0, blk.start, blk.start + blk.size))
                                   .item<double>();
                        acc += term;
                        const double mass = vq.dot(vq).item<double>();
                        blockdiag_rows += " q_bd_" + std::string(blk.name) + "=" +
                                          std::to_string(vv > 0.0 ? term / vv : 0.0 / 0.0) +
                                          " mass_" + std::string(blk.name) + "=" +
                                          std::to_string(vv > 0.0 ? mass / vv : 0.0 / 0.0);
                    }
                    q_min_blockdiag = vv > 0.0 ? acc / vv : 0.0 / 0.0;
                }
                std::cerr << " e_orthogonality=" << e_orth
                          << " e_arnoldi=" << e_arnoldi
                          << " q_min_direct=" << q_min_direct
                          << " q_min_blockdiag=" << q_min_blockdiag
                          << blockdiag_rows
                          << " hcol_norm_min=" << hcol_min
                          << " hcol_norm_max=" << hcol_max
                          << " witness_confirmed="
                          << ((q_min_direct == q_min_direct && q_min_direct < 0.0) ? 1 : 0);
            } else {
                // R13.14 (round 5, R5-18b): the SAME field set as the branch above. This printed
                // four fields where the other prints eight, so any position-based parser
                // misreads the short form -- and a missing field reads as "not applicable" when
                // it means "not measured".
                std::cerr << " e_orthogonality=-1 e_arnoldi=-1 q_min_direct=nan"
                          << " q_min_blockdiag=nan"
                          << " hcol_norm_min=-1 hcol_norm_max=-1"
                          << " witness_confirmed=0";
            }
            std::cerr << std::endl << std::flush;
        }

        // PER-RESTART. Each restart builds a NEW basis V^(r) and a NEW local Hessenberg; row i of
        // cycle r and row i of cycle r+1 index DIFFERENT vectors. Appending across cycles and then
        // assembling one square matrix mixes bases, so the "projected operator" would not be
        // V^T B V for any single V and its symmetric spectrum would be meaningless. Clearing here
        // keeps each report a genuine single-cycle projection.
        ritz_H.clear();

        // NUMERICAL STABILITY: Detect NaN in residual error after update
        if (guarded_item<bool>(torch::isnan(error_tensor).any())) {
            std::cerr << "[GMRES ERROR] NaN detected in error_tensor after iteration " << iter << std::endl;
            std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
            std::cerr << "  ||b|| = " << guarded_item<float>(bnorm_safe) << std::endl;
            std::cerr << "  ||x|| = " << guarded_item<float>(x.norm()) << std::endl;
            throw std::runtime_error("GMRES residual error contains NaN after update");
        }

        // PERFORMANCE FIX: Move update diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 1 .item() sync per GMRES iteration at debug_level >= 1
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && iter == 0) {
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float x_norm_after = x.norm().to(torch::kCPU).item<float>();
            std::cerr << "[GMRES UPDATE] After x = x + sum(y[i]*V[i]):" << std::endl;
            std::cerr << "  ||x|| after update = " << x_norm_after << std::endl;
            if (x_norm_after < 1e-12f) {
                std::cerr << "  FATAL: x is still zero after GMRES update!" << std::endl;
                std::cerr << "  Either y=0 or V vectors are corrupted" << std::endl;
            }
        }

        // TIMING INSTRUMENTATION: Report GMRES iteration timing
        auto gmres_iter_end = std::chrono::high_resolution_clock::now();
        auto gmres_iter_duration = std::chrono::duration_cast<std::chrono::milliseconds>(gmres_iter_end - gmres_iter_start).count();

        // OPT Pass33: Gate timing log with debug_level + sampling (was: if(true))
        // OPT Pass33+: Use configurable sample period (0=every iteration)
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 &&
            (wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 ||
             (iter + 1) % wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 || iter == 0)) {
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float error_val = error_tensor.to(torch::kCPU).item<float>();
            std::cerr << "[GMRES TIMING] Iteration " << iter << " took " << gmres_iter_duration << " ms"
                      << " (j=" << j << " Arnoldi vectors, error=" << error_val << ")" << std::endl;
        }

        // FIX (2025-12-04): Track actual Arnoldi iterations for accurate diagnostics
        total_arnoldi_iters += j;

        // DIAGNOSTIC: Print GMRES progress (gated for performance)
        // OPT Pass33+: Use configurable sample period (0=every iteration)
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 &&
            (wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 ||
             (iter + 1) % wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0)) {
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float error_val = error_tensor.to(torch::kCPU).item<float>();
            std::cerr << "[GMRES] Restart cycle " << (iter + 1)
                      << ": error = " << error_val
                      << ", Krylov dim used = " << j << std::endl;
        }

        // Convergence check now uses FRESH error_tensor computed from new residual.
        // Stage budget overrides are treated as upper bounds only; they do not alter
        // the convergence metric or suppress early stagnation/convergence exits.
        torch::Tensor error_for_stop = error_tensor;
        // GRADIENT FIX: Use guarded_item to prevent gradient break
        if (guarded_item<bool>((error_for_stop < tol).all())) {
            terminated_by_internal_convergence = true;
            break;
        }

        // v20.9: Configurable stagnation detection.  Default threshold = 1.0 (disabled).
        // Previous hard-coded 0.95 was too aggressive — after 1 restart with < 5% reduction
        // it would skip all 19 remaining restarts, creating "search starvation".
        // With threshold = 1.0, all restarts always run.  Set gmres_stagnation_threshold
        // < 1.0 (e.g. 0.95) to re-enable early exit for well-conditioned problems.
        {
            float stag_thresh = wrf::sdirk3::g_sdirk3_config.gmres_stagnation_threshold;
            if (stag_thresh < 1.0f) {
                float err_val = guarded_item<float>(error_tensor);
                if (err_val > stag_thresh && iter < max_iter - 1) {
                    terminated_by_restart_stag_threshold = true;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES] Early termination: rel_error=" << err_val
                                  << " > " << stag_thresh << " after restart " << (iter + 1)
                                  << ", skipping " << (max_iter - iter - 1)
                                  << " remaining restarts (saves "
                                  << (max_iter - iter - 1) * restart
                                  << " JVP calls)" << std::endl;
                    }
                    break;
                }
            }
        }
    }

    // DIAGNOSTIC: GMRES completion
    // OPT Pass33: Batch 4 D2H into single torch::stack() + gate printing
    float x_norm, r_true_final, r_precond_final, rel_error_final;
    // FIX (2025-12-04): Report total Arnoldi iterations (not restart count)
    int final_iterations = total_arnoldi_iters;
    {
        torch::NoGradGuard no_grad;

        // v20.14r27i: Halo-zeroed residual for final error (consistent with internal check).
        // For 1D packed state, zero_halo_regions is no-op (dim < 3) → raw basis.
        // Both internal (gmres_residual_norm) and final paths use the same semantics.
        auto r_true_inner_final = r_true.clone();
        zero_halo_regions(r_true_inner_final, halo_width, periodic_x, periodic_y);

        // OPT Pass33: Batch norms into single D2H transfer
        // v20.14 r50: Use bnorm_unscaled for final rel_error (trust-region compatibility).
        // GMRES internal convergence used bnorm_safe (D⁻¹-scaled when block_scaled).
        auto stats_cpu = torch::stack({
            x.norm(), r_true_inner_final.norm(), r_precond.norm(), bnorm_unscaled
        }).to(torch::kCPU);
        x_norm = stats_cpu[0].item<float>();
        r_true_final = stats_cpu[1].item<float>();  // Halo-zeroed, UNSCALED residual
        r_precond_final = stats_cpu[2].item<float>();
        float bnorm_val = stats_cpu[3].item<float>();  // UNSCALED ||b||
        // CRITICAL (2025-11-28): Compute relative error for trust region predicted formula
        // v20.14 r50: Always report UNSCALED rel_error to Newton/trust-region.
        rel_error_final = (bnorm_val > BNORM_MIN_THRESHOLD) ? (r_true_final / bnorm_val) : 1.0f;
        // Always print GMRES completion summary (single line)
        bool gmres_converged = (rel_error_final < tol);
        std::cerr << "[GMRES] " << (gmres_converged ? "CONVERGED" : "NOT CONVERGED")
                  << std::fixed << std::setprecision(4)
                  << ": ||x||=" << x_norm
                  << ", ||r_true||=" << r_true_final
                  << ", ||b||=" << bnorm_val
                  << ", rel_error=" << rel_error_final
                  << ", tol=" << tol
                  << ", restarts=" << actual_restarts
                  << ", arnoldi=" << total_arnoldi_iters
                  << (block_scaled ? " (block-scaled)" : "")
                  << std::defaultfloat << std::endl;
    }

    bool gmres_converged = (rel_error_final < tol);
    // Return contract (v20.14r25, all paths unified):
    //   final_residual = ||r_true|| halo-zeroed (absolute norm)
    //   rel_error      = ||r_true||/||b|| halo-zeroed (relative)
    //   r_true         = RAW residual b-A(x) — callers apply halo zeroing for per-block analysis
    torch::Tensor r_true_out = r_true.detach().clone();
    // PR 9B (review refactor): reason + message resolved by the shared
    // resolve_krylov_termination — see its contract note near the top of
    // namespace krylov_methods.
    const KrylovTerminationResolution resolution = resolve_krylov_termination(
        "GMRES", gmres_converged, terminated_by_restart_stag_threshold,
        early_exit_reason, terminated_by_internal_convergence,
        total_arnoldi_iters, max_iter, restart, actual_restarts);
    const std::string& gmres_msg = resolution.message;
    WRFNewtonKrylovSolver::GMRESResult res{
            x, gmres_converged, final_iterations, r_true_final,
            rel_error_final, gmres_msg, r_true_out, actual_restarts, false,
            terminated_by_arnoldi_stagnation || terminated_by_restart_stag_threshold};
    res.termination_reason = resolution.reason;
    res.initial_rel_error = initial_rel_error_gmres;
    res.rho_S_initial = initial_rel_error_gmres;
    res.rho_D_initial = initial_rho_D_gmres;
    // R13.17 (external review P0-1): BOTH convergences on the record. The loop stops on the
    // D-weighted objective it minimises (rho_D, both sides D-scaled -- no mixed denominator) and
    // success is judged on the unweighted rho_S. A solve with rho_D < eta and rho_S >= eta met its
    // own objective and was reported as a failed linear solve; collapsed into one `success` that
    // state is indistinguishable from "the operator could not be solved". Production behaviour is
    // unchanged -- what changes is that the seam is stated.
    // R13.18 (deep review P0-1): name the metric the loop stopped on, rather than calling it D.
    // solve_gmres has no WRMS path, so its objective is D or the identity.
    res.stopping_metric = static_cast<int>(
        block_scaled ? wrf::sdirk3::KrylovStoppingMetric::BlockD
                     : wrf::sdirk3::KrylovStoppingMetric::IdentityS);
    res.arnoldi_spent = total_arnoldi_iters;
    res.arnoldi_allowed = max_iter * restart;
    res.rho_D_final = guarded_item<float>(error_tensor);
    res.rho_S_final = res.rel_error;
    res.tolerance_applied = tol;
    res.D_tolerance_reached = (res.rho_D_final >= 0.0f && res.rho_D_final < tol);
    res.S_tolerance_reached = (res.rho_S_final >= 0.0f && res.rho_S_final < tol);
    res.probe_j = diag_probe_j;
    res.probe_true_err = diag_probe_true_err;
    res.probe_hopeless_floor = diag_probe_floor;
    res.stag_ratio_used = diag_stag_ratio;
    res.stag_count_final = diag_stag_count;
    return res;
}

// ============================================================================
// FGMRES (flexible GMRES) — full-repo review P1-1 remediation.
//
// WHY THIS EXISTS: the production preconditioner wrapper is NOT a fixed linear
// operator within one solve — the amplification ratio guard can LOCK it to
// identity mid-solve, warn_only mode selects per-input, and defect refinement
// can toggle after its first call. Standard right-preconditioned GMRES applies
// M^{-1} per Arnoldi vector but reconstructs corrections as M^{-1}(sum y_i V_i),
// which equals sum y_i M_i^{-1} V_i ONLY for a fixed linear M. With a variable
// M_j the Hessenberg problem Arnoldi solved and the correction actually applied
// describe different operators. FGMRES stores Z_j = M_j^{-1} V_j as actually
// used and reconstructs from Z, restoring A Z_j = sum_i H_ij V_i exactly.
//
// This is a minimal-change clone of solve_gmres above (deliberately NOT a
// shared template yet — correctness first, per the review directive), with
// exactly these deltas: Z basis stored per Arnoldi step; trial and final
// corrections reconstructed from Z (ZERO M_inv calls outside the Arnoldi
// loop); Z memory telemetry; per-cycle Z lifetime. Routing: the production
// forward Newton-Krylov call uses solve_fgmres whenever M_inv is non-null;
// unpreconditioned and adjoint(operator-folded) paths keep solve_gmres.
// variable_pc_event remains TELEMETRY ONLY — with FGMRES a preconditioner
// change no longer breaks the Krylov math, so no basis is discarded.
// ============================================================================
WRFNewtonKrylovSolver::GMRESResult solve_fgmres(
    const std::function<torch::Tensor(const torch::Tensor&)>& A,
    const torch::Tensor& b,
    const torch::Tensor& x0,
    int stage_id,
    float ru_share_hint,
    int restart,
    float tol,
    int max_iter,
    const std::function<torch::Tensor(const torch::Tensor&)>& M_inv,
    const StateLayout* layout,
    const torch::Tensor* halo_mask,
    bool periodic_x,
    bool periodic_y,
    KrylovBasisCapture* basis_capture,
    const wrf::sdirk3::FrozenStageWeights* stage_weights,
    const torch::Tensor* krylov_to_physical,
    torch::Tensor* d_inv_out) {

    torch::Tensor x = x0.clone();

    // P0 FIX: Compute initial residual
    // If x0 is zero, skip A(x0) computation since J*0 = 0
    // This prevents calling JVP with v=0 which triggers the guard
    // FIX (2025-12-05): Gate NoGradGuard on !use_autograd to preserve graph in AD mode
    torch::Tensor r_true;  // Unpreconditioned residual for convergence check
    {
        // Use guarded_item for the norm check to support autograd mode
        float x_norm = guarded_item<float>(x.norm());
        if (x_norm < 1e-14f) {
            // x0 is zero, so r = b - J*0 = b
            r_true = b.clone();
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                std::cerr << "[GMRES DEBUG] x0 is zero (norm=" << x_norm << "), skipping A(x0) computation" << std::endl;
                std::cerr << "  Initial residual r = b (no JVP call)" << std::endl;
            }
        } else {
            // x0 is non-zero, compute r = b - A(x)
            // In autograd mode, A(x) preserves graph; in FD mode, it doesn't matter
            r_true = b - A(x);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                std::cerr << "[GMRES DEBUG] x0 is non-zero (norm=" << x_norm << "), computed r = b - A(x0)" << std::endl;
            }
        }
    }

    // RIGHT-PRECONDITIONING: Use unpreconditioned residual for Arnoldi basis
    // Left-preconditioning minimizes ||M^{-1}(b-Ax)|| which is wrong when M changes norms dramatically.
    // Right-preconditioning minimizes ||b - A*M^{-1}*z|| = ||b - Ax|| (the TRUE residual).
    // FIX 2026-01-27: Changed from left to right preconditioning.
    torch::Tensor r_precond = r_true.clone();
    // NOTE: Do NOT apply M_inv here for right-preconditioning.
    // The initial residual r stays unpreconditioned.

    // CRITICAL FIX 2026-01-28: Zero halo regions in residual to prevent boundary artifacts
    // This ensures GMRES vectors don't contain halo contributions.
    // Uses helper function that handles partial periodicity correctly:
    // - For em_b_wave (periodic_x=true, periodic_y=false), only y-halos are zeroed.
    // v20.14r21: periodic_x/y now come from function parameters (instance state),
    // not global config. Callers pass options_.periodic_x/y.
    int halo_width = wrf::sdirk3::g_sdirk3_config.halo_width;
    zero_halo_regions(r_precond, halo_width, periodic_x, periodic_y);

    // CRITICAL FIX 2026-01-28: Also zero halos in r_true for CONSISTENT norm calculation!
    // Previous bug: Convergence used r_true with halos, GMRES used halo-zeroed r_precond.
    // This caused GMRES to fail eliminating residual components that were only in halos.
    auto r_true_inner = r_true.clone();
    zero_halo_regions(r_true_inner, halo_width, periodic_x, periodic_y);

    // FIX 2026-01-29: Compute ||b|| using halo-zeroed b for consistency with r_true_inner.
    // Previously bnorm used the full b (including halos), making the relative error
    // artificially smaller and causing GMRES to stop too early.
    auto b_inner = b.clone();
    zero_halo_regions(b_inner, halo_width, periodic_x, periodic_y);

    // The UNWEIGHTED Krylov RHS, kept before any left weighting touches b_inner.
    // Every relative residual reported below divides by this vector under its OWN weight,
    // which is what makes the four ratios comparable; the retracted measurement divided an
    // unweighted numerator by a D-weighted denominator taken from the mutated b_inner.
    const auto b_krylov = b_inner.clone();
    // R13.9: the j=0 relative residual, in the same halo-zeroed norm every later rel_error
    // uses, so the two are comparable by construction.
    float initial_rel_error_fgmres = -1.0f;
    // R13.18 (deep review P1-4): the INITIAL D-objective. `rho_D_initial` was declared, its
    // header comment promised "both readings (initial and final)", and NOTHING wrote it -- so the
    // headline "both readings are on the record" was true of no record. Captured here, beside its
    // S sibling, from the same initial residual.
    float initial_rho_D_fgmres = -1.0f;
    {
        torch::NoGradGuard ng_init;
        const float bn0 = guarded_item<float>(b_inner.norm());
        const float rn0 = guarded_item<float>(r_true_inner.norm());
        if (bn0 > 0.0f && std::isfinite(bn0) && std::isfinite(rn0)) {
            initial_rel_error_fgmres = rn0 / bn0;
        }
    }

    // v20.14 r50: GMRES block-scaling (left-preconditioning with D⁻¹).
    // D[block] = ||r0[block]||₂. After scaling, each block contributes exactly 1 to ||D⁻¹r0||².
    // This prevents phi/theta O(10⁴) from masking u O(1-10) in GMRES's L2 minimization.
    // GMRES now solves: min ||D⁻¹(b - AM⁻¹z)|| — same solution x, different search path.
    // G1 storage: Hessenberg columns captured pre-reduction (see the capture site below).
    static const auto ritz_capture_on = [] {
        static const bool on =
            wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_RITZ_CAPTURE");
        return on;
    };
    std::vector<std::vector<double>> ritz_H;

    torch::Tensor D_inv;  // per-element scaling vector, empty if disabled
    bool block_scaled = false;
    // v20.14 r50-fix: Block-scaling requires AUTOGRAD JVP. With FD JVP, D_inv amplifies
    // directional noise (D_inv can reach ~800 for small-residual blocks like w/mu),
    // causing ||x||→0. Only enable when forward-mode AD provides exact JVP.
    // THE FOUR LEFT WEIGHTINGS, built once from the same layout so the ratios reported at
    // every restart are four views of ONE residual rather than four separate measurements.
    //
    //   L_S    = I           unweighted, in the (S) coordinates FGMRES actually iterates
    //   L_phys = S           the physical residual, which is what the stage gate is a function of
    //   L_E    = E^-1 S      the stage gate's own WRMS, expressed on Krylov vectors
    //   L_D    = D^-1        what FGMRES minimises   (assigned after D_inv is built)
    //
    // L_S is left undefined on purpose: relative_residual() treats an undefined weight as the
    // identity, so there is no ones-vector to accidentally diverge from the real identity.
    torch::Tensor L_phys, L_E, L_D;
    if (krylov_to_physical != nullptr && krylov_to_physical->defined() &&
        krylov_to_physical->numel() == r_true_inner.numel()) {
        torch::NoGradGuard ng_w;
        L_phys = krylov_to_physical->detach().reshape_as(r_true_inner);
        if (stage_weights != nullptr && layout && layout->is_valid()) {
            const auto einv = wrf::sdirk3::inverse_scale_vector(
                *layout, stage_weights->scale, torch::ones_like(r_true_inner));
            if (einv.defined() && einv.numel() == r_true_inner.numel()) {
                L_E = wrf::sdirk3::wrms_left_weight(einv, L_phys);
            }
        }
    }
    bool wrms_metric_applied = false;

    // A scientific knob that silently runs the baseline is worse than one that fails: the
    // first WRMS-metric run did exactly that (byte-identical trajectory was the only tell).
    // The weighting is installed inside the block-scaling gate below, so every precondition
    // of that gate is also a precondition of the experiment.
    if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_WRMS_METRIC")) {
        TORCH_CHECK(wrf::sdirk3::g_sdirk3_config.gmres_block_scale &&
                        wrf::sdirk3::g_sdirk3_config.use_autograd &&
                        layout && layout->is_valid() &&
                        layout->total_size == r_true_inner.numel(),
                    "WRF_SDIRK3_KRYLOV_WRMS_METRIC requested but the left-weighting path is "
                    "unavailable (needs gmres_block_scale, use_autograd and a matching layout)");
    }
    if (wrf::sdirk3::g_sdirk3_config.gmres_block_scale &&
        wrf::sdirk3::g_sdirk3_config.use_autograd &&
        layout && layout->is_valid() && layout->total_size == r_true_inner.numel()) {
        torch::NoGradGuard no_grad;
        D_inv = torch::ones_like(r_true_inner);

        // THE EXPERIMENT (env-gated, default OFF).
        //
        // The question is whether FGMRES minimises a norm that disagrees with the one the stage
        // acceptance gate judges. That question is open. What was previously recorded here as its
        // answer -- "18-343x apart, Spearman -0.10" -- is RETRACTED: the quantity compared
        // against rho_D was ||r~||/||D^-1 b~||, a ratio whose numerator and denominator carry
        // different weights, so rho_D/rho_unscaled reduced to ||D^-1 r~||/||r~|| with the RHS
        // cancelled. That is the directional amplification of D^-1, not a disagreement between
        // two objectives. The paired trajectory below now reports four properly normalised
        // ratios; the claim has to be re-earned from those.
        //
        // The experiment itself: weight by the STAGE'S OWN error weights E -- the same pointwise
        // rtol*|y_ref| + atol the acceptance gate uses -- instead of by initial block norms, so
        // FGMRES minimises the quantity that decides whether the stage is accepted. Applied as
        // E^-1 S, because the vectors here are r~ = S^-1 R (see below).
        //
        // An EXPERIMENT, not a proposed default: E-weighting removes the property block scaling
        // exists for (equalising blocks whose initial residuals differ by 10^4), so it may trade
        // one pathology for another.
        const bool wrms_metric =
            wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_WRMS_METRIC");
        if (wrms_metric) {
            // THE LEFT WEIGHT IS E^-1 S, NOT E^-1.
            //
            // The stage gate's WRMS is ||E^-1 R|| on the PHYSICAL residual, and the vectors
            // FGMRES holds are r~ = S^-1 R. So the weighting that makes FGMRES minimise the
            // gate's own quantity, expressed on the vectors it actually has, is E^-1 S.
            //
            // Dropping S is not a magnitude error that a tolerance absorbs. S and E are both
            // block-constant here, so omitting S multiplies each block's weight by 1/S_q and
            // can rank the blocks in the OPPOSITE order from the physical WRMS the experiment
            // claims to be testing -- which is the difference between measuring the aligned
            // objective and measuring a third, meaningless one.
            TORCH_CHECK(stage_weights != nullptr,
                        "WRF_SDIRK3_KRYLOV_WRMS_METRIC requested but no stage weights reached "
                        "the Krylov solve; the run would silently have been the D baseline");
            const auto einv = wrf::sdirk3::inverse_scale_vector(
                *layout, stage_weights->scale, torch::ones_like(r_true_inner));
            TORCH_CHECK(einv.defined() && einv.numel() == r_true_inner.numel(),
                        "WRF_SDIRK3_KRYLOV_WRMS_METRIC requested but the stage error weights do "
                        "not match the state layout");
            TORCH_CHECK(krylov_to_physical != nullptr && krylov_to_physical->defined() &&
                            krylov_to_physical->numel() == r_true_inner.numel(),
                        "WRF_SDIRK3_KRYLOV_WRMS_METRIC requested but S (krylov_to_physical) is "
                        "unavailable; E^-1 alone is not the physical stage-WRMS objective");
            D_inv = wrf::sdirk3::wrms_left_weight(einv, *krylov_to_physical);
            TORCH_CHECK(D_inv.defined() &&
                            torch::isfinite(D_inv).all().item<bool>() &&
                            (D_inv > 0).all().item<bool>(),
                        "WRF_SDIRK3_KRYLOV_WRMS_METRIC: E^-1 S is not finite and positive");
            wrms_metric_applied = true;
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[GMRES METRIC] stage-WRMS weighting active (E^-1 S), replacing "
                             "initial-block-norm scaling" << std::endl;
            }
        }
        auto r_cpu = r_true_inner.detach().to(torch::kCPU).contiguous();
        bool all_blocks_ok = true;
        for (const auto& blk : layout->blocks) {
            if (wrms_metric_applied) break;   // E-weighting already set D_inv
            if (blk.start + blk.size > r_cpu.numel()) { all_blocks_ok = false; break; }
            float blk_norm = r_cpu.slice(0, blk.start, blk.start + blk.size)
                .norm().item<float>();
            if (blk_norm < 1e-20f) {
                // Block residual is essentially zero — don't scale (leave D_inv = 1)
                continue;
            }
            float scale = 1.0f / blk_norm;
            D_inv.slice(0, blk.start, blk.start + blk.size).fill_(scale);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[GMRES BLOCK-SCALE] " << blk.name
                          << ": ||r0||=" << blk_norm << " → D_inv=" << scale << "\n";
            }
        }
        if (all_blocks_ok) {
            block_scaled = true;
            L_D = D_inv;
            // R13.9: publish the weight, once, where it is finalised.
            if (d_inv_out) {
                torch::NoGradGuard ng_dinv;
                *d_inv_out = D_inv.detach().clone();
            }
            // WHY the two weightings behave so differently -- their STRUCTURE, not just their values.
            //
            //   D_q = ||r_0,q||          BLOCK-CONSTANT, set by the initial residual
            //   E_i = rtol|y_i| + atol   POINTWISE, set by the state magnitude
            //
            // Pointwise weighting has a failure mode block-constant weighting cannot have: where the
            // state is near zero, E_i -> atol_q and E^-1 -> 1/atol (1e6 for the velocity blocks), so
            // physically negligible components acquire enormous weight and can dominate the
            // minimiser. The dynamic range max(w)/min(w) measures exactly that exposure, and it is
            // the quantity that decides whether an aligned objective is well-posed.
            if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_TRAJECTORY")) {
                torch::NoGradGuard ng_dr;
                const auto w = D_inv.detach().abs();
                const double wmax = w.max().to(torch::kCPU).item<double>();
                const double wmin = w.min().to(torch::kCPU).item<double>();
                    // PER-BLOCK ALIGNMENT.
                //
                // RETRACTED: the kappa(D)=2.07e6 / kappa(E)=3.98e9 comparison, and the
                // "rho_unscaled/rho_L is 18-343 for D and 5.5 for E" it was weighed against.
                // Both numbers came from the mixed-denominator ratio, and kappa(E) was the
                // condition of E^-1 when the left weight FGMRES would actually apply is E^-1 S.
                // The dynamic_range printed below is the condition number of the weighting
                // ACTUALLY installed (a diagonal weight has kappa = max/min), so it is the
                // measured quantity rather than a recomputed one.
                //
                // What the per-block distributions are for: a weighting that is large exactly
                // where the residual is small contributes nothing to the minimised norm while
                // another norm is dominated by blocks the objective barely sees. That is a
                // statement about ALIGNMENT, and it needs the shares below, not a scalar.
                // OBJECTIVE SHARES, EACH IN A NAMED COORDINATE SYSTEM.
                //
                // The retracted table called ||r~_q||^2 / sum_p ||r~_p||^2 the "physical
                // residual share". It is not: r~ = S^-1 R, so that ratio is the share in the
                // (S) coordinates FGMRES iterates. S is block-constant and spans orders of
                // magnitude, so the (S) share and the physical share can attribute the residual
                // to DIFFERENT blocks -- and attribution was the whole claim.
                //
                // All four come from the same r_true_inner, so nothing has to be inferred about
                // which coordinate a number lives in.
                const auto sh_krylov =
                    wrf::sdirk3::block_energy_shares(*layout, r_true_inner, torch::Tensor{});
                const auto sh_phys =
                    wrf::sdirk3::block_energy_shares(*layout, r_true_inner, L_phys);
                const auto sh_D =
                    wrf::sdirk3::block_energy_shares(*layout, r_true_inner, L_D);
                const auto sh_wrms =
                    wrf::sdirk3::block_energy_shares(*layout, r_true_inner, L_E);
                // A share is only reported when its computation says it is valid. The helper
                // now fails closed on a non-partition layout or a non-positive/mismatched
                // weight, and printing -1 with the reason is the alternative to printing
                // plausible numbers that sum to 1 and mean nothing.
                auto share_of = [](const wrf::sdirk3::BlockShares& b, std::size_t i,
                                   bool weight_present) {
                    return (b.valid && weight_present) ? b.shares[i] : -1.0;
                };
                for (std::size_t i = 0; i < layout->blocks.size(); ++i) {
                    std::cerr << "SDIRK3_OBJECTIVE_SHARE " << layout->blocks[i].name
                              << " s_krylov=" << share_of(sh_krylov, i, true)
                              << " s_physical=" << share_of(sh_phys, i, L_phys.defined())
                              << " s_D=" << share_of(sh_D, i, L_D.defined())
                              << " s_wrms=" << share_of(sh_wrms, i, L_E.defined())
                              << std::endl;
                }
                if (!sh_krylov.valid || (L_phys.defined() && !sh_phys.valid) ||
                    (L_D.defined() && !sh_D.valid) || (L_E.defined() && !sh_wrms.valid)) {
                    std::cerr << "SDIRK3_OBJECTIVE_SHARE_INVALID"
                              << " krylov=\"" << sh_krylov.reason << "\""
                              << " physical=\"" << sh_phys.reason << "\""
                              << " D=\"" << sh_D.reason << "\""
                              << " wrms=\"" << sh_wrms.reason << "\""
                              << std::endl;
                }
                std::cerr << "SDIRK3_WEIGHT_STRUCTURE metric="
                          << (wrms_metric_applied ? "E_pointwise" : "D_blockconst")
                          << " max=" << wmax << " min=" << wmin
                          << " dynamic_range=" << (wmin > 0.0 ? wmax / wmin : -1.0)
                          << std::endl << std::flush;
            }


            // Scale the initial residual and RHS
            r_precond = r_precond * D_inv;
            b_inner = b_inner * D_inv;
        }
    }

    // v20.14 r50: Save unscaled bnorm for final return to Newton (trust-region needs it).
    // bnorm_safe uses D⁻¹-scaled b when block_scaled=true (for GMRES internal convergence).
    // bnorm_unscaled always uses the original b (for final rel_error report).
    auto bnorm_unscaled_tensor = safe_tensor_norm(b_inner);  // before scaling!
    if (block_scaled) {
        // b_inner was already scaled above — recompute unscaled from original b
        auto b_orig = b.clone();
        zero_halo_regions(b_orig, halo_width, periodic_x, periodic_y);
        bnorm_unscaled_tensor = safe_tensor_norm(b_orig);
    }
    auto bnorm_unscaled = torch::clamp(bnorm_unscaled_tensor, BNORM_MIN_THRESHOLD);

    auto bnorm_tensor = safe_tensor_norm(b_inner);
    auto bnorm_safe = torch::clamp(bnorm_tensor, BNORM_MIN_THRESHOLD);

    auto error_tensor = block_scaled
        ? safe_tensor_norm(D_inv * r_true_inner) / bnorm_safe
        : safe_tensor_norm(r_true_inner) / bnorm_safe;
    // R13.18 (deep review P1-4): the initial value of the objective the loop will stop on.
    { torch::NoGradGuard ng_rd0; initial_rho_D_fgmres = guarded_item<float>(error_tensor); }

    // NUMERICAL STABILITY: Detect NaN in residual error immediately
    if (guarded_item<bool>(torch::isnan(error_tensor).any())) {
        std::cerr << "[GMRES ERROR] NaN detected in initial error_tensor" << std::endl;
        std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
        std::cerr << "  ||b|| = " << guarded_item<float>(bnorm_safe) << std::endl;
        throw std::runtime_error("GMRES initial residual error contains NaN");
    }

    auto converged = error_tensor < tol;
    // GRADIENT FIX: Use guarded_item for control flow check
    if (guarded_item<bool>(converged.all())) {
        float error_val = guarded_item<float>(error_tensor);
        float r_true_norm = guarded_item<float>(safe_tensor_norm(r_true_inner));
        std::cerr << "[GMRES] Initial residual already converged: error = " << error_val << " < tol = " << tol << std::endl;
        // v20.14r24: final_residual = ||r_true_inner|| (absolute), rel_error = error_val (relative).
        // r_true = RAW (not halo-zeroed), consistent with normal exit (line ~1127) and NaN paths.
        // Callers must apply halo zeroing to r_true before per-block analysis.
        // R13.19 (precision review P0-1): the STOP metric and the S metric, kept apart.
        //
        // This return used to set success = true unconditionally and store `error_val` -- which is
        // rho_D under block scaling, or rho_E under the WRMS experiment -- into `rel_error`, the
        // S-COORDINATE field. So rho_stop = 0.85 with rho_S = 0.99 and eta = 0.90 returned SUCCESS,
        // while the normal finaliser on the identical state returns failure and the classifier
        // calls it KrylovObjectiveMismatch. The one state R13.17-R13.18 exist to separate was
        // being merged back into a single success, on a PRODUCTION path: this value feeds
        // gmres_success, gmres_raw_rel_error, the trust-region prediction, the total-failure rule
        // and warm-start quality.
        //
        // `success` is the S-coordinate question, the same one the normal exit answers, and
        // `rel_error` always carries rho_S. The stop metric keeps its own field.
        const float bnorm_unscaled_val = guarded_item<float>(bnorm_unscaled);
        const float rho_S_here = (bnorm_unscaled_val > BNORM_MIN_THRESHOLD)
            ? (r_true_norm / bnorm_unscaled_val) : 1.0f;
        const bool S_reached_here = (rho_S_here < tol);
        WRFNewtonKrylovSolver::GMRESResult res{
                x, S_reached_here, 0, r_true_norm, rho_S_here,
                "Initial residual already converged",
                r_true.detach().clone(), 0, false, false};
        res.rho_S_initial = rho_S_here;
        res.rho_S_final = rho_S_here;
        res.rho_D_initial = error_val;      // the stop objective, named by stopping_metric
        res.rho_D_final = error_val;
        res.tolerance_applied = tol;
        res.D_tolerance_reached = (error_val < tol);
        res.S_tolerance_reached = S_reached_here;
        res.arnoldi_spent = 0;
        res.arnoldi_allowed = max_iter * restart;
        res.stopping_metric = static_cast<int>((!block_scaled ? wrf::sdirk3::KrylovStoppingMetric::IdentityS
                       : (wrms_metric_applied ? wrf::sdirk3::KrylovStoppingMetric::StageWRMS
                                              : wrf::sdirk3::KrylovStoppingMetric::BlockD)));
        res.termination_reason =
            WRFNewtonKrylovSolver::KrylovTerminationReason::InitialConverged;
        res.initial_rel_error = initial_rel_error_fgmres;
        return res;
    }
    
    // GMRES FAILURE DETECTION: Track NaN/Inf occurrences in apply_jacobian
    int nan_failure_count = 0;
    const int max_nan_failures = wrf::sdirk3::g_sdirk3_config.nk_gmres_max_nan_retries;

    // FIX (2025-12-04): Track actual iterations for diagnostics
    int actual_restarts = 0;
    int total_arnoldi_iters = 0;
    bool terminated_by_restart_stag_threshold = false;
    bool terminated_by_arnoldi_stagnation = false;
    bool terminated_by_internal_convergence = false;
    // PR 8.1 (review P1): exact termination metadata. The two early-exit
    // detectors (consecutive Arnoldi stagnation vs the forced ru-dominant
    // mid-budget hopeless probe) previously collapsed into ONE boolean and
    // one message, so the classification could not tell which policy fired.
    using KTR = WRFNewtonKrylovSolver::KrylovTerminationReason;
    KTR early_exit_reason = KTR::MaxBudget;  // set ONLY by the two detectors
    int diag_probe_j = -1;
    float diag_probe_true_err = -1.0f;
    float diag_probe_floor = -1.0f;
    float diag_stag_ratio = -1.0f;
    int diag_stag_count = 0;

    for (int iter = 0; iter < max_iter; ++iter) {
        actual_restarts = iter + 1;  // Track current restart number
        // TIMING INSTRUMENTATION: Start GMRES iteration timer
        auto gmres_iter_start = std::chrono::high_resolution_clock::now();
        const bool log_gmres_v0_debug = (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 &&
                                         iter == 0 &&
                                         !wrf::sdirk3::g_sdirk3_config.use_autograd);

        // PERFORMANCE FIX: Move GMRES V0 diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 2 .item() syncs per GMRES call at debug_level >= 1
        // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
        if (log_gmres_v0_debug) {
            torch::NoGradGuard no_grad;  // Diagnostic logging only - safe in FD mode
            // FIX 2025-12-27: Use guarded_item to ensure CPU transfer before .item()
            float r_true_norm = guarded_item<float>(r_true.norm());
            float r_precond_norm = guarded_item<float>(r_precond.norm());
            std::cerr << "[GMRES V0 DEBUG] Initial vector generation:" << std::endl;
            std::cerr << "  Unpreconditioned residual ||r_true||: " << r_true_norm << std::endl;
            std::cerr << "  Preconditioned residual ||r_precond||: " << r_precond_norm << std::endl;
            std::cerr << "  Preconditioner effect: " << (r_true_norm > 1e-12 ? r_precond_norm / r_true_norm : 0.0f) << "x" << std::endl;
            std::cerr << "  r_precond shape: [" << r_precond.sizes() << "]" << std::endl;
            std::cerr << "  r_precond.dim(): " << r_precond.dim() << std::endl;
            std::cerr << "  r_precond is_contiguous: " << r_precond.is_contiguous() << std::endl;

            // Check if r was affected by halo zeroing
            if (r_precond.dim() == 3) {
                std::cerr << "  r_precond is 3D tensor - halo zeroing may have been applied" << std::endl;
            } else if (r_precond.dim() == 1) {
                std::cerr << "  r_precond is 1D flattened tensor - halo zeroing should NOT apply" << std::endl;
            }

            if (r_precond_norm < 1e-12f) {
                std::cerr << "  ERROR: Preconditioned residual has near-zero norm!" << std::endl;
                std::cerr << "  This will cause v_0 = r_precond / r_precond.norm() to be zero or NaN" << std::endl;
            }
        }

        // Arnoldi process (use preconditioned residual)
        std::vector<torch::Tensor> V;
        // FGMRES: store the PRECONDITIONED basis Z[j] = M_j^{-1} V[j] exactly as
        // applied when building each Arnoldi column. The whole point of FGMRES is
        // that trial/final corrections are reconstructed from Z — NEVER by
        // re-applying M_inv to an aggregate of V — so a preconditioner that varies
        // across Arnoldi steps (ratio-guard identity lock, warn_only per-vector
        // mapping, defect-refinement toggling) stays mathematically consistent:
        // A Z_j = sum_i H_ij V_i holds with the Z_j actually used.
        // Memory: one restart cycle of Z is held at a time (declared per cycle,
        // destroyed at cycle end); M_inv == nullptr allocates no Z (V used directly).
        std::vector<torch::Tensor> Z;
        if (M_inv) {
            Z.reserve(restart);
            if (iter == 0 && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                const long long z_bytes = static_cast<long long>(restart) *
                    static_cast<long long>(x.numel()) *
                    static_cast<long long>(x.element_size());
                std::cerr << "[FGMRES] Z-basis memory budget: restart=" << restart
                          << " x numel=" << x.numel()
                          << " => ~" << (z_bytes / (1024.0 * 1024.0)) << " MiB per cycle\n";
            }
        }

        // NUMERICAL STABILITY: Check r_precond for NaN/Inf before normalization
        if (guarded_item<bool>(torch::isnan(r_precond).any()) ||
            guarded_item<bool>(torch::isinf(r_precond).any())) {
            std::cerr << "[GMRES ERROR] Preconditioned residual r_precond contains NaN/Inf" << std::endl;
            std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
            std::cerr << "  ||r_precond|| = " << guarded_item<float>(r_precond.norm()) << std::endl;
            throw std::runtime_error("GMRES: Preconditioner produced NaN/Inf in residual");
        }

        // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
        auto r_norm_tensor = safe_tensor_norm(r_precond);

        // NUMERICAL STABILITY: Guard against tiny/zero norm before division
        if (guarded_item<bool>(r_norm_tensor < 1e-12f)) {
            std::cerr << "[GMRES ERROR] Preconditioned residual norm too small for V[0] normalization" << std::endl;
            std::cerr << "  ||r_precond|| = " << guarded_item<float>(r_norm_tensor) << " < 1e-12" << std::endl;
            std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
            std::cerr << "  ||b|| = " << guarded_item<float>(b.norm()) << std::endl;
            throw std::runtime_error("GMRES: Cannot normalize V[0] - residual norm too small");
        }

        V.push_back(r_precond / r_norm_tensor);
        if (basis_capture) capture_basis_vector(basis_capture->V, V.back());

        // PERFORMANCE FIX: Move V0 validation diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 2 .item() syncs per GMRES call at debug_level >= 1
        // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
        if (log_gmres_v0_debug) {
            torch::NoGradGuard no_grad;  // Safe in FD mode
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float r_norm_val = r_norm_tensor.to(torch::kCPU).item<float>();
            float v0_norm = V[0].norm().to(torch::kCPU).item<float>();
            std::cerr << "  r.norm() before V[0] creation: " << r_norm_val << std::endl;
            std::cerr << "  After normalization v_0 norm: " << v0_norm << std::endl;
            std::cerr << "  v_0 shape: [" << V[0].sizes() << "]" << std::endl;
            std::cerr << "  Ratio r.norm()/V[0].norm() = " << (v0_norm > 1e-12 ? r_norm_val / v0_norm : 0.0f) << std::endl;

            if (v0_norm < 1e-12f) {
                std::cerr << "  FATAL: v_0 has zero norm after normalization!" << std::endl;
                std::cerr << "  This indicates r / r.norm() produced zero/NaN" << std::endl;
            } else if (std::abs(v0_norm - 1.0f) > 0.01f) {
                std::cerr << "  WARNING: v_0 norm = " << v0_norm << " (expected 1.0)" << std::endl;
                std::cerr << "  Possible halo zeroing corrupted V[0]" << std::endl;
            } else {
                std::cerr << "  v_0 normalized correctly (norm ≈ 1.0)" << std::endl;
            }
        }
        
        // Hessenberg matrix and Givens rotation data
        // Phase 3A: Force H and s to CPU — avoids ~5000 tiny GPU kernel launches
        // for Hessenberg updates, Givens rotations, and back-substitution.
        // PyTorch auto-handles CPU↔GPU transfers for mixed-device scalar ops.
        auto cpu_opts = torch::TensorOptions().dtype(x.dtype());
        torch::Tensor H = torch::zeros({restart + 1, restart}, cpu_opts);
        torch::Tensor s = torch::zeros({restart + 1}, cpu_opts);
        // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
        s[0] = safe_tensor_norm(r_precond).cpu();
        
        // PERFORMANCE: Store Givens rotation coefficients as tensors to avoid .item() syncs
        // Previously extracted as float causing 2 CPU-GPU syncs per Arnoldi vector
        std::vector<torch::Tensor> cs(restart);
        std::vector<torch::Tensor> sn(restart);

        // DEVICE-AWARE: Pre-create constants on correct device to avoid CPU-GPU sync in loop
        // Using x.options() ensures these live on same device as state vectors (CPU/CUDA/MPS)
        const auto eps_safe = torch::full({}, 1e-8f, x.options());
        const auto one_tensor = torch::full({}, 1.0f, x.options());
        const auto zero_tensor = torch::full({}, 0.0f, x.options());

        // Track breakdown for numerical stability handling
        bool breakdown_occurred = false;
        // FIX 2026-01-31: Save converged residual from j-loop to skip redundant JVP at line 915
        torch::Tensor saved_r_true_converged;

        // v20.14r48: Arnoldi-level stagnation tracking for early termination.
        // If true_err improves by less than (1 - stag_ratio) for stag_window consecutive
        // checks, break the Arnoldi loop early (don't waste remaining budget).
        //
        // PERFORMANCE FIX (2026-02-19):
        // Stage-specific budget overrides set an upper bound on work, not a requirement
        // to exhaust all Arnoldi vectors. Keep stagnation early-exit enabled even when
        // stage2/stage3 budgets are active, so stagnating solves stop before burning JVPs.
            float prev_true_err = 1.0f;
            int stag_count = 0;
            auto& cfg_local = wrf::sdirk3::g_sdirk3_config;
            // Cache once per solve (Gemini #66): no_early_stop_enabled() has a thread-safe
            // static-init guard checked on every call; hoist it out of the Arnoldi loop.
            const bool no_early_stop = no_early_stop_enabled();
            // The gate's knobs, resolved for THIS stage. Shipped order reads the stage-2
            // knobs even at stage 3, which is why a stage-3 sweep also flips this gate.
            const auto gate_knobs = wrf::sdirk3::early_stop_gate_knobs(
                stage_id,
                cfg_local.stage2_gmres_restart, cfg_local.stage2_max_krylov_restarts,
                cfg_local.stage3_gmres_restart, cfg_local.stage3_max_krylov_restarts,
                stage_krylov_order());
            const bool aggressive_budget_stag_gate =
                (!no_early_stop &&
                 stage_id >= 2 &&
                 ru_share_hint > 0.98f &&
                 gate_knobs.restart > 0 &&
                 gate_knobs.max_restarts == 1);
            int stag_window = aggressive_budget_stag_gate
                                ? 1
                                : cfg_local.gmres_arnoldi_stag_window;
            float stag_ratio = cfg_local.gmres_arnoldi_stag_ratio;

        int j;
        for (j = 0; j < restart; ++j) {
            // RIGHT-PRECONDITIONING: w = A(M^{-1}(V[j]))
            // Apply preconditioner FIRST, then operator A.
            // This builds Krylov space for A*M^{-1} and minimizes ||b - A*M^{-1}*z||.
            // FIX 2026-01-27: Changed from left (w=M^{-1}(A(V[j]))) to right preconditioning.
            auto jvp_start = std::chrono::high_resolution_clock::now();
            torch::Tensor v_precond = V[j];
            if (M_inv) {
                v_precond = M_inv(V[j]);
                // FGMRES: keep the EXACT preconditioned vector used for this
                // Arnoldi column (not a recomputation — M may differ next call).
                Z.push_back(v_precond);
                if (basis_capture) capture_basis_vector(basis_capture->Z, v_precond);
            }
            // PR 9B: arm the in-situ capture around EXACTLY the Arnoldi
            // operator application (probe/true-residual A calls stay dark),
            // and capture the actual operator output the solve used.
            if (basis_capture) basis_capture->arnoldi_call_active = true;
            torch::Tensor w = A(v_precond);

            // IS THE MATVEC LINEAR? A Krylov method assumes it is. If A(a*v) != a*A(v) the
            // Arnoldi relation does not hold, the least-squares minimiser is meaningless, and a
            // flat true residual is expected NO MATTER what preconditioner is used -- which
            // would explain a stall that survives M on, M off, and every coefficient change.
            // Homogeneity is the cheap half and needs one extra operator call, so it is gated
            // and fires once.
            if (j == 0 && wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_MATVEC_LINEARITY")) {
                torch::NoGradGuard ng_lin;
                const double alpha = 0.5;
                const torch::Tensor w_scaled = A(alpha * v_precond);
                const auto lhs = w_scaled.to(torch::kFloat64);
                const auto rhs = (alpha * w).to(torch::kFloat64);
                const double num = (lhs - rhs).norm().to(torch::kCPU).item<double>();
                const double den = rhs.norm().to(torch::kCPU).item<double>();
                // Additivity, the other half: homogeneity alone does not imply linearity.
                const torch::Tensor u = torch::randn_like(v_precond);
                const torch::Tensor w_u = A(u);
                const torch::Tensor w_sum = A(v_precond + u);
                const auto add_lhs = w_sum.to(torch::kFloat64);
                const auto add_rhs = (w + w_u).to(torch::kFloat64);
                const double add_num = (add_lhs - add_rhs).norm().to(torch::kCPU).item<double>();
                const double add_den = add_rhs.norm().to(torch::kCPU).item<double>();

                std::cerr << "SDIRK3_MATVEC_LINEARITY alpha=" << alpha
                          << " ||A(av)-aA(v)||=" << num
                          << " ||aA(v)||=" << den
                          << " rel=" << (den > 0.0 ? num / den : -1.0)
                          << " | additivity rel="
                          << (add_den > 0.0 ? add_num / add_den : -1.0)
                          << std::endl << std::flush;
            }

            // THE JUDGEMENT, from the triplet FGMRES already has.
            //
            //     v_j = V[j],  z_j = M^-1 v_j,  w_j = A z_j
            //     eps_j = ||W (A z_j - v_j)|| / ||W v_j||,   W = diag(1/ewt)
            //
            // Taken from the quantities in hand rather than by calling A and M^-1 again on a
            // random probe direction. Not merely cheaper: re-calling them would advance the
            // preconditioner's counters, fallback latches and caches -- the observer effect this
            // contract spends most of its checks guarding against -- and it would measure a
            // direction the solve never visits. This measures the ACTUAL variable P_j^-1 on the
            // ACTUAL Krylov direction, so a stagnating iteration is named rather than sampled.
            //
            // Weighted by the stage gate's own error weights, so "small" here means what it means
            // to the gate. Opt-in, and silent when no weights were handed over.
            // THE JUDGEMENT, from the triplet FGMRES already has.
            //
            //     v_j = V[j],  z_j = P_j^-1 v_j,  w_j = A z_j
            //
            // Taken from the quantities in hand rather than by calling A and M^-1 again on a
            // random probe direction: re-calling them would advance the preconditioner's
            // counters, fallback latches and caches, and would measure a direction the solve
            // never visits.
            //
            // TWO COORDINATE SYSTEMS, reported separately because conflating them was a real
            // defect here. When scaling is active this loop iterates the CONJUGATED operator
            //
            //     Ahat = S^-1 A S,   Phat^-1 = S^-1 P^-1 S
            //
            // so V[j] and w are in SCALED coordinates. Weighting them with the physical error
            // weights E^-1 -- which the first version did -- mixes the two, and is only correct
            // when S = I. The physical defect must map back first:
            //
            //     eps_phys = ||E^-1 S (w - v)|| / ||E^-1 S v||
            //
            // eps_krylov is reported too, since that is the quantity GMRES's own convergence
            // theory speaks about.
            //
            // FP64 BEFORE the arithmetic, not after. Casting the result of (w - v) * E^-1 would
            // promote a number whose cancellation, overflow and underflow already happened in the
            // source dtype -- the same defect closed in the synthetic contract and reintroduced
            // here on the live path.
            {
                // Uses THE project spelling authority, not a local rule. Presence alone armed
                // this for =0 and =false; the replacement accepted only "1", which is worse in a
                // different way -- WRF_SDIRK3_APINV_DEFECT=true is valid for every other flag in
                // this codebase and would have silently done nothing, which is indistinguishable
                // from a broken probe.
                // Armed but silent is the case worth reporting: it looks identical to a broken
                // probe, and it cost two rebuilds to diagnose the first time. Keyed per STAGE
                // rather than per process, so one stage's missing input cannot mask another's.
                if (apinv_probe_armed() && !(stage_weights && layout)) {
                    static std::mutex said_mu;
                    static std::set<int> said_stages;
                    std::lock_guard<std::mutex> lk(said_mu);
                    if (said_stages.insert(stage_id).second) {
                        std::cerr << "SDIRK3_APINV_DEFECT_SILENT stage=" << stage_id
                                  << " have_weights=" << (stage_weights ? 1 : 0)
                                  << " have_layout=" << (layout ? 1 : 0)
                                  << " -- probe armed but an input is missing" << std::endl;
                    }
                }
                if (apinv_probe_armed() && stage_weights && layout) {
                    torch::NoGradGuard ng_defect;
                    const auto v64 = V[j].to(torch::kFloat64);
                    const auto w64 = w.to(torch::kFloat64);
                    const auto d64 = w64 - v64;

                    const double num_k = d64.norm().to(torch::kCPU).item<double>();
                    const double den_k = v64.norm().to(torch::kCPU).item<double>();

                    // The identity defect ALONE cannot judge conditioning: B = cI has
                    // eps_I = |c-1| -- arbitrarily large -- and GMRES solves it in ONE step
                    // (minimal polynomial of degree 1). So each record also carries, per
                    // coordinate system:
                    //
                    //   gain   alpha = <v,Bv>_W / <v,v>_W    the best scalar multiple
                    //   shape  eta   = ||W(Bv - alpha v)|| / ||Wv||   what NO scalar removes
                    //   cosine c     = <v,Bv>_W / (||Wv|| ||WBv||)
                    //
                    // A pure gain error (alpha != 1, eta ~ 0, c ~ 1) is cheap for GMRES; shape
                    // defect is what actually costs Krylov directions. eps stays in the record
                    // as identity_defect -- still the right target for A*P^-1 ~ I -- but the
                    // conclusion "eps is large, therefore the preconditioner blocks
                    // convergence" needs eta, not eps.
                    const auto triplet64 = [](const torch::Tensor& vv, const torch::Tensor& ww)
                        -> std::array<double, 3> {
                        const double vs = vv.dot(vv).to(torch::kCPU).item<double>();
                        if (!(vs > 0.0)) return {0.0, 0.0, 0.0};
                        const double vw = vv.dot(ww).to(torch::kCPU).item<double>();
                        const double alpha = vw / vs;
                        const double eta =
                            (ww - alpha * vv).norm().to(torch::kCPU).item<double>()
                            / std::sqrt(vs);
                        const double wn = ww.norm().to(torch::kCPU).item<double>();
                        const double cosv = (wn > 0.0) ? vw / (std::sqrt(vs) * wn) : 0.0;
                        return {alpha, eta, cosv};
                    };
                    const auto tk = triplet64(v64, w64);

                    const auto sinv = wrf::sdirk3::inverse_scale_vector(
                        *layout, stage_weights->scale, V[j]);
                    double num_p = 0.0, den_p = 0.0;
                    std::array<double, 3> tp{0.0, 0.0, 0.0};
                    if (sinv.defined()) {
                        const auto E64 = sinv.to(torch::kFloat64);
                        const auto S64 = krylov_to_physical
                                       ? krylov_to_physical->to(torch::kFloat64)
                                       : torch::ones_like(v64);
                        num_p = (E64 * (S64 * d64)).norm().to(torch::kCPU).item<double>();
                        den_p = (E64 * (S64 * v64)).norm().to(torch::kCPU).item<double>();
                        tp = triplet64(E64 * (S64 * v64), E64 * (S64 * w64));
                    }

                    if (std::isfinite(den_k) && den_k > 0.0) {
                        const char* point_name =
                            stage_weights->stage.point ==
                                wrf::sdirk3::WeightingPoint::NewtonLinearization
                                ? "newton_linearization"
                                : (stage_weights->stage.point ==
                                       wrf::sdirk3::WeightingPoint::StageAcceptance
                                       ? "stage_acceptance" : "stage_entry");
                        std::cerr << "SDIRK3_APINV_DEFECT"
                                  << " solver=" << stage_weights->stage.solver_id
                                  << " stage=" << stage_id
                                  << " capture=" << stage_weights->stage.capture_seq
                                  << " weighting_point=" << point_name
                                  // krylov_iter restarts at 0 every cycle, Newton iteration and
                                  // step; without these two a trajectory cannot be attributed.
                                  // newton_iter comes stamped on the weights because the loop
                                  // index lives in the DRIVER, not in this free function.
                                  << " restart=" << iter
                                  << " newton_iter=" << stage_weights->newton_iter
                                  // The weighting that produced these numbers, emitted so a
                                  // reader never has to look it up. It is NOT a physical error
                                  // scale: ewt_rtol is max(newton_tol, 1e-6), and em_b_wave sets
                                  // sdirk3_newton_tol = 0.2, so "small" here means "under ~20% of
                                  // the local state magnitude". A tighter rtol makes the SAME
                                  // defect read much larger, so eps is only comparable across
                                  // runs that share this value.
                                  << " ewt_rtol=" << stage_weights->cfg.rtol
                                  << " krylov_iter=" << j
                                  << " scaled=" << (krylov_to_physical ? 1 : 0)
                                  << " eps_krylov=" << (num_k / den_k)
                                  << " num_k=" << num_k << " den_k=" << den_k;
                        std::cerr << " gain_k=" << tk[0] << " shape_k=" << tk[1]
                                  << " cos_k=" << tk[2];
                        if (std::isfinite(den_p) && den_p > 0.0) {
                            std::cerr << " eps_physical_wrms=" << (num_p / den_p)
                                      << " num_p=" << num_p << " den_p=" << den_p
                                      << " gain_p=" << tp[0] << " shape_p=" << tp[1]
                                      << " cos_p=" << tp[2];
                        } else {
                            std::cerr << " eps_physical_wrms=unavailable";
                        }
                        std::cerr << std::endl;

                        // The i=0 anomaly's CHARACTER, not just its size: per-block norms of the
                        // weighted input and defect, first Arnoldi direction only (V[0] is the
                        // normalized initial residual, the direction that measured cos ~ 0.003).
                        // Which block carries v0 and which receives the defect is what the
                        // aggregate eps cannot say.
                        if (j == 0 && layout && sinv.defined()) {
                            const auto E64b = sinv.to(torch::kFloat64);
                            const auto S64b = krylov_to_physical
                                            ? krylov_to_physical->to(torch::kFloat64)
                                            : torch::ones_like(v64);
                            const auto vw = E64b * (S64b * v64);
                            const auto dw = E64b * (S64b * d64);
                            std::cerr << "SDIRK3_APINV_V0_BLOCKS";
                            for (const auto& b : layout->blocks) {
                                const double vb = vw.slice(0, b.start, b.start + b.size)
                                                    .norm().to(torch::kCPU).item<double>();
                                const double db = dw.slice(0, b.start, b.start + b.size)
                                                    .norm().to(torch::kCPU).item<double>();
                                std::cerr << " " << b.name << ":v=" << vb << ",d=" << db;
                            }
                            std::cerr << std::endl;
                        }
                    }
                }
            }
            if (basis_capture) {
                basis_capture->arnoldi_call_active = false;
                capture_basis_vector(basis_capture->A_Z, w);
            }
            auto jvp_end = std::chrono::high_resolution_clock::now();
            auto jvp_duration = std::chrono::duration_cast<std::chrono::milliseconds>(jvp_end - jvp_start).count();

            // NUMERICAL STABILITY: Check for NaN/Inf immediately after Jacobian application
            if (guarded_item<bool>(torch::isnan(w).any()) || guarded_item<bool>(torch::isinf(w).any())) {
                nan_failure_count++;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[GMRES ERROR] NaN/Inf in Arnoldi vector after A(V[" << j << "])" << std::endl;
                    std::cerr << "  iter=" << iter << " j=" << j
                              << " ||V[j]||=" << guarded_item<float>(V[j].norm()) << std::endl;
                    std::cerr << "  NaN failure count: " << nan_failure_count << "/" << max_nan_failures << std::endl;
                }

                if (nan_failure_count > max_nan_failures) {
                    // GMRES FAILURE RECOVERY: After max retries, return failure status for trust-region fallback
                    std::cerr << "[GMRES FAILURE] Exceeded max NaN retries (" << max_nan_failures
                              << "), returning failure status to trigger trust-region fallback" << std::endl;
                    // v20.14r25: Use halo-zeroed norm for final_residual (contract: all paths consistent).
                    auto r_true_nan = r_true.clone();
                    zero_halo_regions(r_true_nan, halo_width, periodic_x, periodic_y);
                    float r_norm = guarded_item<float>(safe_tensor_norm(r_true_nan));
                    // v20.14r37: Include current restart's j (same fix as early-breakdown path).
                    // r_true returned RAW (caller applies halo zeroing for per-block analysis).
                    WRFNewtonKrylovSolver::GMRESResult res{
                            torch::zeros_like(x0), false,
                            total_arnoldi_iters + j, r_norm, 1.0f,
                            "NaN failures exceeded max retries",
                            r_true.detach().clone(), iter, false, false};
                    res.termination_reason = KTR::NanRetryExhausted;
                    res.initial_rel_error = initial_rel_error_fgmres;
                    return res;
                } else {
                    // Continue GMRES loop, hope next iteration succeeds
                    // OPT Pass34: Gate retry message + use \n (avoids flush in hot path)
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES] Continuing after NaN (retry " << nan_failure_count << ")\n";
                    }
                    break;  // Break Arnoldi loop, restart GMRES iteration
                }
            }

            // v20.14r27o: JVP timing is hot-path overhead — raise to debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5) {
                std::cerr << "[GMRES TIMING] Arnoldi j=" << j << ": JVP took " << jvp_duration << " ms" << std::endl;
            }

            // DIAGNOSTIC: Check raw JVP output before preconditioner (first few vectors)
            // PERFORMANCE: .item() causes CPU-GPU sync, only enable at debug_level >= 2
            // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
            // OPT Pass32: Batch 2 norms into single D2H transfer
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5 &&
                !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                torch::NoGradGuard no_grad;
                auto norms_cpu = torch::stack({w.norm(), V[j].norm()}).to(torch::kCPU);
                float w_raw_norm = norms_cpu[0].item<float>();
                float vj_norm = norms_cpu[1].item<float>();
                std::cerr << "[ARNOLDI] j=" << j << " After w = A(V[" << j << "]) (raw JVP):" << std::endl;
                std::cerr << "  ||w_raw|| = " << w_raw_norm << std::endl;
                std::cerr << "  ||V[" << j << "]|| = " << vj_norm << std::endl;

                // Check if ||w|| is already tiny before preconditioning
                if (w_raw_norm < 1e-6f) {
                    std::cerr << "  WARNING: ||A(V[" << j << "])|| = " << w_raw_norm
                              << " is very small BEFORE preconditioner!" << std::endl;
                    std::cerr << "  This suggests Jacobian column space is nearly 1D or rank-deficient" << std::endl;
                }
            }

            // RIGHT-PRECONDITIONING: M_inv was applied BEFORE A (above).
            // No post-A preconditioning needed.
            // FIX 2026-01-27: Removed left-preconditioning w = M_inv(w).

            // CRITICAL FIX 2026-01-28: Zero halo regions in new Arnoldi vector
            // This maintains halo boundary consistency throughout GMRES.
            // Uses helper function that handles partial periodicity correctly:
            // - For em_b_wave (periodic_x=true, periodic_y=false), only y-halos are zeroed.
            zero_halo_regions(w, halo_width, periodic_x, periodic_y);

            // v20.14 r50: Apply block-scaling D⁻¹ to Arnoldi vector.
            // GMRES now builds Krylov space for D⁻¹AM⁻¹ instead of AM⁻¹.
            if (block_scaled) {
                w = w * D_inv;
            }

            // Modified Gram-Schmidt orthogonalization with DGK reorthogonalization
            // v20.14 r49-fix: Daniel-Gragg-Kaufman criterion — if ||w_after|| < 0.7*||w_before||,
            // orthogonality is lost and a second MGS pass is needed.
            // PERFORMANCE DIAGNOSTIC: Measure orthogonalization overhead to identify sync points
            auto gramschmidt_start = std::chrono::high_resolution_clock::now();

            // Save pre-MGS norm for DGK criterion
            torch::Tensor w_norm_before_mgs = safe_tensor_norm(w);

            for (int i = 0; i <= j; ++i) {
                auto dot_start = std::chrono::high_resolution_clock::now();
                H[i][j] = torch::dot(w.flatten(), V[i].flatten());
                auto dot_end = std::chrono::high_resolution_clock::now();

                w = w - H[i][j] * V[i];
                auto subtract_end = std::chrono::high_resolution_clock::now();

                // v20.14r27o: GS timing is hot-path (fires per Arnoldi × per basis vector)
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5) {
                    auto dot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dot_end - dot_start).count();
                    auto subtract_ms = std::chrono::duration_cast<std::chrono::milliseconds>(subtract_end - dot_end).count();
                    std::cerr << "[GMRES TIMING] Gram-Schmidt j=" << j << " i=" << i
                              << ": dot=" << dot_ms << "ms, subtract=" << subtract_ms << "ms" << std::endl;
                }
            }

            // v20.14 r49-fix: DGK reorthogonalization criterion
            // If ||w_after|| < 0.7 * ||w_before||, run second MGS pass to restore orthogonality.
            // This addresses GMRES stagnation when cond(A) > ~3000 (common for S2 ru-dominated).
            {
                torch::Tensor w_norm_after_mgs = safe_tensor_norm(w);
                auto needs_reorth = w_norm_after_mgs < (0.7f * w_norm_before_mgs);
                if (guarded_item<bool>(needs_reorth)) {
                    for (int i = 0; i <= j; ++i) {
                        auto h_corr = torch::dot(w.flatten(), V[i].flatten());
                        H[i][j] = H[i][j] + h_corr;
                        w = w - h_corr * V[i];
                    }
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && j < 3) {
                        torch::NoGradGuard no_grad;
                        float nb = w_norm_before_mgs.to(torch::kCPU).item<float>();
                        float na = w_norm_after_mgs.to(torch::kCPU).item<float>();
                        float nr = safe_tensor_norm(w).to(torch::kCPU).item<float>();
                        std::cerr << "[GMRES REORTH] j=" << j
                                  << " before=" << nb << " after1=" << na
                                  << " after2=" << nr << " ratio=" << (na/nb) << "\n";
                    }
                }
            }

            auto gramschmidt_end = std::chrono::high_resolution_clock::now();
            auto gramschmidt_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(gramschmidt_end - gramschmidt_start).count();

            // v20.14r27o: GS total timing — raise to debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0) {
                std::cerr << "[GMRES TIMING] Gram-Schmidt j=" << j << " TOTAL: " << gramschmidt_total_ms << " ms" << std::endl;
            }

            // DIAGNOSTIC: Check orthogonalization result
            // PERFORMANCE: .item() sync - only at debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j == 0) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_00 = H[0][0].to(torch::kCPU).item<float>();
                float w_ortho_norm = w.norm().to(torch::kCPU).item<float>();
                std::cerr << "[ARNOLDI] After Gram-Schmidt orthogonalization:" << std::endl;
                std::cerr << "  H[0][0] = " << h_00 << std::endl;
                std::cerr << "  ||w - H[0][0]*V[0]|| = " << w_ortho_norm << std::endl;
                std::cerr << "  This will become H[1][0]" << std::endl;
            }

            // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
            H[j + 1][j] = safe_tensor_norm(w);

            // DIAGNOSTIC: Check breakdown condition
            // PERFORMANCE: .item() sync - only at debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j == 0) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_10 = H[1][0].to(torch::kCPU).item<float>();
                std::cerr << "[ARNOLDI] Breakdown check:" << std::endl;
                std::cerr << "  H[1][0] = " << h_10 << std::endl;
                std::cerr << "  Breakdown threshold: 1e-6" << std::endl;
                std::cerr << "  Will breakdown: " << (h_10 < 1e-6f ? "YES" : "NO") << std::endl;
            }

            // AUTOGRAD FIX: Use tensor comparison for breakdown detection
            // RELAXED THRESHOLD: Further relaxed to 1e-10 to handle ill-conditioned systems without preconditioner
            // GRADIENT FIX: Use guarded_item to prevent gradient break
            auto breakdown_check_start = std::chrono::high_resolution_clock::now();
            auto h_small = torch::abs(H[j + 1][j]) < 1e-10f;
            bool is_breakdown = guarded_item<bool>(h_small.all());
            auto breakdown_check_end = std::chrono::high_resolution_clock::now();
            auto breakdown_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(breakdown_check_end - breakdown_check_start).count();

            // v20.14r27o: Breakdown timing — raise to debug_level >= 2
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && iter == 0 && j < 5) {
                std::cerr << "[GMRES TIMING] Breakdown check j=" << j << ": " << breakdown_check_ms << " ms" << std::endl;
            }

            // 9F.D101 (review P0-A): the Givens reduction for the CURRENT column, shared by
            // the breakdown path and the normal path so the two cannot drift apart.
            // G1: SNAPSHOT THE HESSENBERG COLUMN before the Givens rotations overwrite it with R.
            //
            // The campaign's "A_fast is INTRINSICALLY INDEFINITE" finding drove two strategic
            // pivots, and it was measured on the LEGACY operator -- the one whose negative ph/mu
            // diagonals the Omega fix removes. Re-measuring under WRFParity has been flagged
            // UNDONE ever since. The Arnoldi Hessenberg carries the spectrum of the operator
            // GMRES ACTUALLY iterates, for free, but only BEFORE reduction -- so the capture
            // belongs exactly here.
            //
            // For a NON-NORMAL operator the GMRES-relevant quantity is the numerical range, not
            // the eigenvalues: the min eigenvalue of the symmetric part 1/2(H + H^T). A spectrum
            // in the right half-plane does NOT prove a definite numerical range, and eigenvalues
            // mislead in both directions -- so the symmetric part is what this accumulates.
            if (ritz_capture_on()) {
                torch::NoGradGuard ng_ritz;
                std::vector<double> col;
                col.reserve(static_cast<size_t>(j) + 2);
                for (int i = 0; i <= j + 1 && i < static_cast<int>(H.size(0)); ++i) {
                    col.push_back(H[i][j].to(torch::kCPU).item<double>());
                }
                ritz_H.push_back(std::move(col));
            }

            auto reduce_current_column = [&]() {
                for (int i = 0; i < j; ++i) {
                    auto h_i_j = H[i][j].clone();
                    auto h_ip1_j = H[i + 1][j].clone();
                    H[i][j] = cs[i] * h_i_j + sn[i] * h_ip1_j;
                    H[i + 1][j] = -sn[i] * h_i_j + cs[i] * h_ip1_j;
                }
                auto h_j_t = H[j][j];
                auto h_jp1_t = H[j + 1][j];
                auto r_g = torch::sqrt(h_j_t * h_j_t + h_jp1_t * h_jp1_t);
                auto r_g_safe = torch::where(r_g > 1e-8f, r_g, eps_safe);
                auto safe_mask = (r_g > 1e-8f);
                cs[j] = torch::where(safe_mask, h_j_t / r_g_safe, one_tensor);
                sn[j] = torch::where(safe_mask, h_jp1_t / r_g_safe, zero_tensor);
                H[j][j] = r_g;
                H[j + 1][j] = 0.0f;
                auto s_j = s[j].clone();
                auto s_jp1 = s[j + 1].clone();
                s[j] = cs[j] * s_j + sn[j] * s_jp1;
                s[j + 1] = -sn[j] * s_j + cs[j] * s_jp1;
            };

            if (is_breakdown) {
                breakdown_occurred = true;  // Track for numerical stability handling
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    std::cerr << "[ARNOLDI] BREAKDOWN at j=" << j
                              << ", H[" << (j+1) << "][" << j << "] = "
                              << guarded_item<float>(H[j + 1][j]) << std::endl;
                    // FIX 2026-01-29: Breakdown means Krylov subspace is exhausted.
                    // If j==0, no useful direction was found — this is NOT convergence.
                    // If j>0, we have a partial solution that may still be useful,
                    // but we should NOT report this as "converged" to the Newton solver,
                    // as it can cause trust-region to accept bad steps → stagnation.
                    std::cerr << "[ARNOLDI] Breakdown at j=" << j
                              << " — extracting best available solution" << std::endl;
                }
                // 9F.D101 (review P0-A): COMPLETE THE REDUCTION, then leave.
                //
                // The old code broke out BEFORE the Givens block, so this column was never
                // rotated and the back-substitution below then operated on an H that was not
                // upper-triangular. And h_{j+1,j} = 0 is precisely the case where the Krylov
                // space ALREADY CONTAINS the solution -- with a zero subdiagonal the new
                // rotation is the identity, so reducing here is correct and costs nothing.
                //
                // What must NOT run is V.push_back(w / H[j+1][j]) below: that divides by ~0.
                // Skipping the BASIS VECTOR is the right response to breakdown; skipping the
                // CORRECTION was the defect.
                reduce_current_column();
                j++;
                break;  // no new basis vector, but the projected system is now solvable
            }
            
            V.push_back(w / H[j + 1][j]);
        if (basis_capture) capture_basis_vector(basis_capture->V, V.back());
            
            // 9F.D101: the SAME reduction the breakdown path uses -- one implementation.
            reduce_current_column();

            // v20.14r48: PERFORMANCE — Periodic true residual check (replaces hess_est-based).
            // Old: hess_est < 3*tol triggers expensive A(x_trial) check → JVP/Arnoldi ~1.6.
            // New: periodic sampling (j >= start_j && j % period == 0, always check last j).
            // Expected: JVP/Arnoldi ratio → ~1.1, ~30% linear solve time reduction.
            bool gmres_converged = false;

            // Cheap convergence estimate from Givens rotation (no JVP needed)
            // GR v9 G1: Gate behind debug_level to avoid GPU sync in production
            float hess_estimate = -1.0f;  // sentinel for non-debug path
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                hess_estimate = guarded_item<float>(torch::abs(s[j + 1]) / bnorm_safe);
            }

            // Periodic true residual check: j >= start_j && j % period == 0, or last j.
            // v20.14r52: In stage>=2 ru-dominant solves, periodic true residual probes
            // are usually low-value but expensive (extra A(x_trial) ≈ extra JVP work).
            // Keep mandatory check at last Arnoldi index; skip intermediate probes for
            // ru-dominant stage>=2 to reduce wasted compute.
            int start_j = wrf::sdirk3::g_sdirk3_config.gmres_true_residual_start_j;
            int period = wrf::sdirk3::g_sdirk3_config.gmres_true_residual_period;
            // v20.14r54: When stage-aware GMRES budget is explicitly enabled for stage>=2,
            // avoid extra periodic true-residual probes and keep only the mandatory
            // last-Arnoldi true-residual check. This is a default-off behavior because
            // it activates only when stage2_gmres_restart>0 is configured.
            if (stage_id >= 2 && gate_knobs.restart > 0) {
                start_j = std::max(start_j, restart - 1);
            }
            bool skip_periodic_true_check = (stage_id >= 2 && ru_share_hint > 0.9f);
            // For stage-budgeted ru-dominant solves, force one mid-budget probe.
            // If true residual barely improves, Arnoldi stagnation can terminate early
            // without consuming the full restart budget.
            const bool mid_budget_probe =
                aggressive_budget_stag_gate && (j == std::max(2, restart / 2));
            bool near_convergence = (j == restart - 1) ||
                                    mid_budget_probe ||
                                    (!skip_periodic_true_check &&
                                     j >= start_j && (j - start_j) % period == 0);

            if (near_convergence) {
                // Solve H*y_trial = s for the current Krylov subspace [0...j]
                torch::Tensor y_trial = torch::zeros({j + 1}, x.options());
                for (int i = j; i >= 0; --i) {
                    y_trial[i] = s[i];
                    for (int k = i + 1; k <= j; ++k) {
                        y_trial[i] = y_trial[i] - H[i][k] * y_trial[k];
                    }
                    auto h_diag_abs = torch::abs(H[i][i]);
                    y_trial[i] = torch::where(h_diag_abs > 1e-10f,
                                             y_trial[i] / H[i][i],
                                             torch::zeros_like(y_trial[i]));
                }

                // FGMRES: x_trial = x + sum_i y_trial[i] * Z[i] — reconstruct from
                // the STORED preconditioned basis; re-applying M_inv to an aggregate
                // of V is exactly the variable-preconditioner inconsistency this
                // routine exists to remove. (M_inv == nullptr => Z empty, use V.)
                torch::Tensor correction = torch::zeros_like(x);
                const std::vector<torch::Tensor>& basis_trial = M_inv ? Z : V;
                for (int i = 0; i <= j; ++i) {
                    correction = correction + y_trial[i] * basis_trial[i];
                }
                torch::Tensor x_trial = x + correction;

                // Compute TRUE unpreconditioned residual: r_true_trial = b - A(x_trial)
                torch::Tensor r_true_trial = b - A(x_trial);

                // v20.14r27i: Use unified helper (halo-zeroed for 3D, raw for 1D packed)
                // v20.14 r50: When block-scaled, measure convergence in D⁻¹-norm.
                // bnorm_safe was already computed from D⁻¹*b, so error_true is in scaled space.
                torch::Tensor r_for_norm = block_scaled
                    ? (r_true_trial * D_inv) : r_true_trial;
                auto r_true_norm = gmres_residual_norm(r_for_norm, halo_width, periodic_x, periodic_y);
                auto error_true = r_true_norm / bnorm_safe;
                gmres_converged = guarded_item<bool>((error_true < tol).all());

                // Budgeted ru-dominant stage guard:
                // If a forced mid-budget probe is still far from tolerance, terminate
                // this restart early instead of burning the remaining Arnoldi budget.
                bool budget_probe_hopeless = false;
                if (mid_budget_probe && aggressive_budget_stag_gate && !gmres_converged) {
                    float err_mid = guarded_item<float>(error_true);
                    const float hopeless_floor = std::max(0.9f, 2.0f * tol);
                    if (err_mid > hopeless_floor) {
                        budget_probe_hopeless = true;
                        saved_r_true_converged = r_true_trial;
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[GMRES] Budget probe early-exit: true_err=" << err_mid
                                      << " > " << hopeless_floor
                                      << " (stage=" << stage_id
                                      << ", ru_share=" << ru_share_hint << ")\n";
                        }
                    }
                }

                // FIX 2026-01-31: Save r_true_trial to avoid redundant A(x) JVP after j-loop.
                // When j == restart-1: y_trial == y (same H,s system), so x_trial == x_updated
                // and r_true_trial == b - A(x_updated). Saves 1 JVP per non-convergent restart.
                if (gmres_converged || j == restart - 1) {
                    saved_r_true_converged = r_true_trial;
                }

                // v20.14r48: Arnoldi stagnation tracking.
                // Track true_err improvement across consecutive checks.
                bool arnoldi_stagnated = false;
                if (!gmres_converged && !no_early_stop) {
                    float err_val_stag = guarded_item<float>(error_true);
                    float ratio = (prev_true_err > 1e-30f) ? err_val_stag / prev_true_err : 0.0f;
                    if (ratio > stag_ratio) {
                        stag_count++;
                    } else {
                        stag_count = 0;  // reset on improvement
                    }
                    prev_true_err = err_val_stag;
                    if (stag_count >= stag_window) {
                        arnoldi_stagnated = true;
                        saved_r_true_converged = r_true_trial;  // save for reuse
                    }
                }

                // v20.14r48: Hot-loop log — use '\n' not std::endl, no flush()
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    float err_val = error_true.to(torch::kCPU).item<float>();
                    std::cerr << "[GMRES] restart=" << (iter + 1) << " j=" << j
                              << ": hess_est=" << std::fixed << std::setprecision(4) << hess_estimate
                              << ", true_err=" << err_val
                              << (gmres_converged ? " CONVERGED" : "")
                              << (arnoldi_stagnated ? " STAGNATED" : "")
                              << std::defaultfloat << '\n';
                }

                // v20.14r48: Early termination on Arnoldi stagnation.
                if (arnoldi_stagnated) {
                    terminated_by_arnoldi_stagnation = true;
                    // PR 8.1: record WHICH detector fired, with its inputs.
                    early_exit_reason = KTR::ArnoldiStagnation;
                    diag_probe_j = j;
                    diag_probe_true_err = prev_true_err;
                    diag_stag_ratio = stag_ratio;
                    diag_stag_count = stag_count;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES] Arnoldi stagnation at j=" << j
                                  << " (" << stag_count << " consecutive, ratio>"
                                  << stag_ratio << ") — early exit\n";
                    }
                    j++;  // advance past current to match convergence exit convention
                    break;
                }

                if (budget_probe_hopeless) {
                    terminated_by_arnoldi_stagnation = true;
                    // PR 8.1: the forced mid-budget hopeless probe is a
                    // DIFFERENT policy from the stagnation detector above.
                    early_exit_reason = KTR::MidBudgetHopeless;
                    diag_probe_j = j;
                    diag_probe_true_err = guarded_item<float>(error_true);
                    diag_probe_floor = std::max(0.9f, 2.0f * tol);
                    diag_stag_ratio = stag_ratio;
                    diag_stag_count = stag_count;
                    j++;  // keep convention with other early exits
                    break;
                }

                // v20.11: Per-block true residual at end of each restart
                if (layout && layout->is_valid() &&
                    layout->total_size == r_true_trial.numel() &&
                    (j == restart - 1 || gmres_converged) &&
                    wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    // v20.14r15: Apply halo zeroing for consistency with GMRES true_err
                    auto r_halo = r_true_trial.detach();
                    if (halo_mask && halo_mask->numel() == r_halo.numel()) {
                        r_halo = r_halo * halo_mask->to(r_halo.dtype()).to(r_halo.device());
                    }
                    auto r_cpu = r_halo.to(torch::kCPU).contiguous();
                    float bnorm_val = bnorm_safe.to(torch::kCPU).item<float>();
                    std::ostringstream bss;
                    // v20.14r27k: Label clarified — values are r_block/||b|| (linear relative error per block).
                    bss << "[GMRES BLOCK r/b] restart=" << (iter + 1) << " j=" << j;
                    for (const auto& blk : layout->blocks) {
                        if (blk.start + blk.size <= r_cpu.numel()) {
                            float r_n = r_cpu.slice(0, blk.start, blk.start + blk.size)
                                .norm().item<float>();
                            float frac = (bnorm_val > 0) ? (r_n / bnorm_val) : 0.0f;
                            bss << " " << blk.name << "=" << std::fixed
                                << std::setprecision(4) << frac;
                        }
                    }
                    std::cerr << bss.str() << std::defaultfloat << '\n';
                }
            }

            if (gmres_converged) {
                j++;
                break;
            }
        }

        // DIAGNOSTIC: Check H matrix conditioning (expensive - debug only)
        // OPT Pass33+: Use configurable heavy sample period (0=every iteration)
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 &&
            (wrf::sdirk3::g_sdirk3_config.debug_heavy_sample_period == 0 ||
             (iter + 1) % wrf::sdirk3::g_sdirk3_config.debug_heavy_sample_period == 0 || iter == 0)) {
            torch::NoGradGuard no_grad;
            float h_min = 1e20f, h_max = 0.0f;
            for (int i = 0; i < j; ++i) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_ii = std::abs(H[i][i].to(torch::kCPU).item<float>());
                h_min = std::min(h_min, h_ii);
                h_max = std::max(h_max, h_ii);
            }
            float condition_est = (h_min > 1e-14f) ? (h_max / h_min) : 1e20f;
            std::cerr << "[GMRES H-DIAG] Restart " << (iter + 1)
                      << ": min|H[i][i]| = " << h_min
                      << ", max|H[i][i]| = " << h_max
                      << ", cond ~ " << condition_est << std::endl;
        }

        // NUMERICAL STABILITY: If breakdown occurred very early, skip update
        // When j <= 2, the Krylov subspace is too small for reliable solution
        // CRITICAL FIX 2026-01-28: Return success ONLY if residual actually converged!
        // Previous bug: Always returned success=true, allowing Newton to accept unconverged solution.
        // 9F.D101 (review P0-A): the "j <= 2 -> return x as-is" branch is DELETED.
        //
        // It returned the INITIAL GUESS and the cycle-start residual, discarding the
        // exact Krylov correction, with the comment "x hasn't been updated in this
        // restart cycle". For a HAPPY breakdown that correction is the exact solution:
        // A = 0.5I breaks down at j = 1 and must give x = 2b in one step. It returned
        // x = 0, and I previously mis-attributed that to a "WRF-shaped harness"
        // limitation in a test comment. It was this branch.
        //
        // With the column now properly reduced above, the normal back-substitution and
        // solution update below handle a breakdown column correctly, so there is
        // nothing left for a special case to protect against.

        // Solve least squares problem with diagonal check
        torch::Tensor y = torch::zeros({j}, x.options());
        bool singular_detected = false;
        for (int i = j - 1; i >= 0; --i) {
            y[i] = s[i];
            for (int k = i + 1; k < j; ++k) {
                y[i] = y[i] - H[i][k] * y[k];
            }

            // GR v8 F4: Sign-preserving regularized division (preserves direction info)
            auto h_diag_abs = torch::abs(H[i][i]);
            auto h_safe = torch::where(h_diag_abs > 1e-10f,
                                       H[i][i],
                                       torch::copysign(torch::tensor(1e-10f, H[i][i].options()), H[i][i]));
            y[i] = y[i] / h_safe;

            // DIAGNOSTIC: Check for singularity (gated to avoid .item() sync in production)
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float h_diag_val = h_diag_abs.to(torch::kCPU).item<float>();
                if (h_diag_val < 1e-10f) {
                    ERROR_PRINT("WARNING: GMRES H matrix nearly singular at diagonal " << i
                              << ", |H[" << i << "][" << i << "]| = " << h_diag_val);
                    singular_detected = true;
                }
            }
        }
        
        if (singular_detected) {
            ERROR_PRINT("ERROR: GMRES detected singular/ill-conditioned system!");
            ERROR_PRINT("  This typically means:");
            ERROR_PRINT("  - The Jacobian (I + dt*gamma*dF/dU) is nearly singular");
            ERROR_PRINT("  - The timestep dt=" << dt << ", gamma=" << gamma);
            ERROR_PRINT("  - dt*gamma=" << dt*gamma << " (affects Jacobian conditioning)");
            ERROR_PRINT("  - If dt*gamma*eigenvalue ≈ -1, the system becomes singular");
            ERROR_PRINT("  - The system may have reached a bifurcation point");

            // Additional diagnostics (avoid .item() to preserve autodiff)
            ERROR_PRINT("\nDEBUG: H matrix diagonals (as tensors):");
            for (int idx = 0; idx < j && idx < 10; ++idx) {
                ERROR_PRINT("  H[" << idx << "][" << idx << "] = " << H[idx][idx]);
            }
        }

        // PERFORMANCE FIX: Move least-squares diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 2+ .item() syncs per GMRES iteration at debug_level >= 1
        // OPT Pass32: Batch y.norm() and y.abs().max() into single D2H transfer
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && iter == 0) {
            torch::NoGradGuard no_grad;
            auto y_stats_cpu = torch::stack({y.norm(), y.abs().max()}).to(torch::kCPU);
            float y_norm = y_stats_cpu[0].item<float>();
            float y_max = y_stats_cpu[1].item<float>();
            std::cerr << "[GMRES LS] Least-squares solution y:" << std::endl;
            std::cerr << "  j (Krylov dimension): " << j << std::endl;
            std::cerr << "  ||y|| = " << y_norm << std::endl;
            std::cerr << "  max|y| = " << y_max << std::endl;

            // Check H matrix diagonal for breakdown
            std::cerr << "[GMRES LS] H matrix diagonals:" << std::endl;
            for (int idx = 0; idx < j && idx < 5; ++idx) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                // LINT:DIAG_OK - NoGradGuard is at line 838, diagnostic block
                float h_diag = H[idx][idx].to(torch::kCPU).item<float>();
                std::cerr << "  H[" << idx << "][" << idx << "] = " << h_diag << std::endl;
                if (std::abs(h_diag) < 1e-8) {
                    std::cerr << "    WARNING: Near-singular diagonal!" << std::endl;
                }
            }

            if (y_norm < 1e-12f) {
                std::cerr << "  WARNING: y has zero norm - x will not be updated!" << std::endl;
            }
        }

        // NUMERICAL STABILITY: Check y for NaN/Inf before update
        if (guarded_item<bool>(torch::isnan(y).any()) || guarded_item<bool>(torch::isinf(y).any())) {
            std::cerr << "[GMRES ERROR] NaN/Inf detected in y (backsolve result) before update" << std::endl;
            std::cerr << "  j=" << j << " (Krylov dimension)" << std::endl;
            std::cerr << "  ||y|| = " << guarded_item<float>(y.norm()) << std::endl;

            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                torch::NoGradGuard no_grad;
                std::cerr << "[GMRES DEBUG] H matrix diagonals:" << std::endl;
                for (int idx = 0; idx < j && idx < 5; ++idx) {
                    std::cerr << "  H[" << idx << "][" << idx << "] = "
                              << guarded_item<float>(H[idx][idx]) << std::endl;
                }
            }

            throw std::runtime_error("GMRES backsolve produced NaN/Inf in solution vector y");
        }

        // FGMRES: x = x + sum_i y[i] * Z[i] — the final correction likewise comes
        // ONLY from the stored preconditioned basis. Zero M_inv calls outside the
        // Arnoldi loop, by construction. (M_inv == nullptr => Z empty, use V.)
        {
            torch::Tensor z_update = torch::zeros_like(x);
            const std::vector<torch::Tensor>& basis_final = M_inv ? Z : V;
            for (int i = 0; i < j; ++i) {
                z_update = z_update + y[i] * basis_final[i];
            }
            x = x + z_update;
        }

        // CRITICAL FIX: Compute new residual IMMEDIATELY after solution update
        // The convergence check must use the FRESH residual r_true = b - A(x_new),
        // not the stale residual from before the update!
        // FIX 2026-01-31: Skip redundant JVP when converged inside j-loop.
        // saved_r_true_converged == b - A(x_trial) where x_trial == x after update
        // (same H, s, y values used in both paths), saving 1 JVP per Newton iter.
        if (saved_r_true_converged.defined()) {
            r_true = saved_r_true_converged;
            saved_r_true_converged.reset();  // Clear for next restart
        } else {
            r_true = b - A(x);
        }
        // RIGHT-PRECONDITIONING: r_precond = r_true (no preconditioning of residual)
        // FIX 2026-01-27: Removed M_inv application to residual for right preconditioning.
        r_precond = r_true.clone();

        // CRITICAL FIX 2026-01-28: Apply halo zeroing CONSISTENTLY after restart!
        // Previous bug: Halo zeroing only applied to initial residual, not after restart.
        // This caused halos to leak into Krylov basis and GMRES couldn't eliminate them.
        zero_halo_regions(r_precond, halo_width, periodic_x, periodic_y);

        // v20.14 r50: Apply block scaling to new residual for next restart
        if (block_scaled) {
            r_precond = r_precond * D_inv;
        }

        // CRITICAL FIX 2026-01-28: Use halo-zeroed residual for error calculation too
        auto r_true_inner = r_true.clone();
        zero_halo_regions(r_true_inner, halo_width, periodic_x, periodic_y);

        // Now compute error_tensor from FRESH residual (halo-zeroed for consistency)
        // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
        // v20.14 r50: Use scaled norm for internal convergence check
        error_tensor = block_scaled
            ? safe_tensor_norm(D_inv * r_true_inner) / bnorm_safe
            : safe_tensor_norm(r_true_inner) / bnorm_safe;

        // G1 ANALYSIS: the numerical range of the operator GMRES actually iterates.
        //
        // Assemble the square part of the captured Hessenberg, symmetrise it, and report the
        // extreme eigenvalues of 1/2(H + H^T). If min_eig < 0 the numerical range straddles the
        // origin IN THESE COORDINATES for THIS preconditioner realisation. It does NOT rule out
        // an SPD preconditioner: Sylvester's law governs congruence C^T H C, and right-
        // preconditioning gives A P^-1, whose symmetric part is not a congruence of H(A).
        // Counterexample: A = [[0,-1],[2,2]] has H(A) eigenvalues -0.118 / 2.118, yet the SPD
        // P^-1 = [[2,-1],[-1,2]] gives H(A P^-1) = diag(1,2), positive definite. If min_eig > 0
        // the operator
        // is positive-definite in the field-of-values sense and the campaign's two pivots rest on
        // a premise that no longer holds under WRFParity. Ritz eigenvalues are reported too, but
        // the symmetric part is the quantity that decides.
        if (ritz_capture_on() && !ritz_H.empty()) {
            torch::NoGradGuard ng_ritz_analysis;
            const int m = static_cast<int>(ritz_H.size());
            auto Hs = torch::zeros({m, m}, torch::kFloat64);
            auto acc = Hs.accessor<double, 2>();
            for (int jj = 0; jj < m; ++jj) {
                const auto& col = ritz_H[static_cast<size_t>(jj)];
                for (int ii = 0; ii < m && ii < static_cast<int>(col.size()); ++ii) {
                    acc[ii][jj] = col[static_cast<size_t>(ii)];
                }
            }
            const auto sym = 0.5 * (Hs + Hs.transpose(0, 1));
            const auto evals = torch::linalg_eigvalsh(sym);
            const double lo = evals.min().item<double>();
            const double hi = evals.max().item<double>();
            const int64_t n_neg = (evals < 0.0).sum().item<int64_t>();
            std::cerr << "SDIRK3_NUMERICAL_RANGE"
                      << " operator_coordinates=" << (block_scaled ? "D_left_S_krylov" : "S_krylov")
                      << " block_scaled=" << (block_scaled ? 1 : 0)
                      << " right_precond=M"
                      << " m=" << m
                      << " min_eig_sym=" << lo
                      << " max_eig_sym=" << hi
                      << " n_negative=" << n_neg << "/" << m
                      << " definite=" << (lo > 0.0 ? 1 : 0)
                      << std::endl << std::flush;
        }

        // PER-RESTART. Each restart builds a NEW basis V^(r) and a NEW local Hessenberg; row i of
        // cycle r and row i of cycle r+1 index DIFFERENT vectors. Appending across cycles and then
        // assembling one square matrix mixes bases, so the "projected operator" would not be
        // V^T B V for any single V and its symmetric spectrum would be meaningless. Clearing here
        // keeps each report a genuine single-cycle projection.
        ritz_H.clear();

        // F1: the per-RESTART trajectory of the minimised norm. 0.14% total reduction over 51
        // Arnoldi directions can mean two different things -- flat from the first step (the
        // leading Krylov direction is useless, which the cos_P = 0.003 measurement predicts) or
        // an initial drop then a plateau (stagnation after real progress). Different causes, so
        // print the value at every restart instead of only at exit.
        if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_TRAJECTORY")) {
            torch::NoGradGuard ng_traj;
            // PAIRED TRAJECTORY -- the scientific question, not a diagnostic nicety.
            //
            // FGMRES minimises ||D^-1 r~|| / ||D^-1 b~|| on the vectors r~ = S^-1 R it holds.
            // The stage gate accepts or rejects on ||E^-1 R||, a physically weighted WRMS.
            // Those are different objective functions over the same residual, so on a finite
            // Krylov budget the solver could reduce its own norm while the gate's norm GROWS.
            // Whether it DOES is what these four ratios measure.
            //
            // Reporting all four is the point: with three coordinate systems in play (physical
            // R, Krylov r~ = S^-1 R, objective L r~), a single "unscaled" number is ambiguous
            // enough that the previous version of this emit compared two quantities normalised
            // differently and read the mismatch as a physical finding.
            // Four relative residuals, one residual, one RHS, one helper. Each ratio carries
            // the SAME weight above and below the line -- the property the retracted
            // "rho_unscaled" lacked, which is why its ratio against rho_D reduced to
            // ||D^-1 r~||/||r~|| with the RHS cancelled out entirely.
            const auto rho_D    = wrf::sdirk3::relative_residual(r_true_inner, b_krylov, L_D);
            const auto rho_S    = wrf::sdirk3::relative_residual(r_true_inner, b_krylov, torch::Tensor{});
            const auto rho_phys = wrf::sdirk3::relative_residual(r_true_inner, b_krylov, L_phys);
            const auto rho_E    = wrf::sdirk3::relative_residual(r_true_inner, b_krylov, L_E);
            std::cerr << "SDIRK3_KRYLOV_TRAJECTORY restart=" << iter
                      << " metric=" << (wrms_metric_applied ? "E_S" : "D_blockconst")
                      << " scaled=" << (block_scaled ? 1 : 0)
                      << " rho_D=" << (rho_D.valid ? rho_D.value : -1.0)
                      << " rho_S=" << (rho_S.valid ? rho_S.value : -1.0)
                      << " rho_phys=" << (rho_phys.valid && L_phys.defined() ? rho_phys.value : -1.0)
                      << " rho_wrms=" << (rho_E.valid && L_E.defined() ? rho_E.value : -1.0)
                      // Cross-check: rho_D from the helper must equal the solver's own
                      // error_tensor. If it does not, the helper and the solve disagree about
                      // what is being minimised, and every number on this line is suspect.
                      << " solver_error=" << guarded_item<float>(error_tensor)
                      << std::endl << std::flush;
        }

        // NUMERICAL STABILITY: Detect NaN in residual error after update
        if (guarded_item<bool>(torch::isnan(error_tensor).any())) {
            std::cerr << "[GMRES ERROR] NaN detected in error_tensor after iteration " << iter << std::endl;
            std::cerr << "  ||r_true|| = " << guarded_item<float>(r_true.norm()) << std::endl;
            std::cerr << "  ||b|| = " << guarded_item<float>(bnorm_safe) << std::endl;
            std::cerr << "  ||x|| = " << guarded_item<float>(x.norm()) << std::endl;
            throw std::runtime_error("GMRES residual error contains NaN after update");
        }

        // PERFORMANCE FIX: Move update diagnostics to debug_level >= 3 (HOT PATH)
        // This was causing 1 .item() sync per GMRES iteration at debug_level >= 1
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3 && iter == 0) {
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float x_norm_after = x.norm().to(torch::kCPU).item<float>();
            std::cerr << "[GMRES UPDATE] After x = x + sum(y[i]*V[i]):" << std::endl;
            std::cerr << "  ||x|| after update = " << x_norm_after << std::endl;
            if (x_norm_after < 1e-12f) {
                std::cerr << "  FATAL: x is still zero after GMRES update!" << std::endl;
                std::cerr << "  Either y=0 or V vectors are corrupted" << std::endl;
            }
        }

        // TIMING INSTRUMENTATION: Report GMRES iteration timing
        auto gmres_iter_end = std::chrono::high_resolution_clock::now();
        auto gmres_iter_duration = std::chrono::duration_cast<std::chrono::milliseconds>(gmres_iter_end - gmres_iter_start).count();

        // OPT Pass33: Gate timing log with debug_level + sampling (was: if(true))
        // OPT Pass33+: Use configurable sample period (0=every iteration)
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 &&
            (wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 ||
             (iter + 1) % wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 || iter == 0)) {
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float error_val = error_tensor.to(torch::kCPU).item<float>();
            std::cerr << "[GMRES TIMING] Iteration " << iter << " took " << gmres_iter_duration << " ms"
                      << " (j=" << j << " Arnoldi vectors, error=" << error_val << ")" << std::endl;
        }

        // FIX (2025-12-04): Track actual Arnoldi iterations for accurate diagnostics
        total_arnoldi_iters += j;

        // DIAGNOSTIC: Print GMRES progress (gated for performance)
        // OPT Pass33+: Use configurable sample period (0=every iteration)
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 &&
            (wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 ||
             (iter + 1) % wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0)) {
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            float error_val = error_tensor.to(torch::kCPU).item<float>();
            std::cerr << "[GMRES] Restart cycle " << (iter + 1)
                      << ": error = " << error_val
                      << ", Krylov dim used = " << j << std::endl;
        }

        // Convergence check now uses FRESH error_tensor computed from new residual.
        // Stage budget overrides are treated as upper bounds only; they do not alter
        // the convergence metric or suppress early stagnation/convergence exits.
        torch::Tensor error_for_stop = error_tensor;
        // GRADIENT FIX: Use guarded_item to prevent gradient break
        if (guarded_item<bool>((error_for_stop < tol).all())) {
            terminated_by_internal_convergence = true;
            break;
        }

        // v20.9: Configurable stagnation detection.  Default threshold = 1.0 (disabled).
        // Previous hard-coded 0.95 was too aggressive — after 1 restart with < 5% reduction
        // it would skip all 19 remaining restarts, creating "search starvation".
        // With threshold = 1.0, all restarts always run.  Set gmres_stagnation_threshold
        // < 1.0 (e.g. 0.95) to re-enable early exit for well-conditioned problems.
        {
            float stag_thresh = wrf::sdirk3::g_sdirk3_config.gmres_stagnation_threshold;
            if (stag_thresh < 1.0f) {
                float err_val = guarded_item<float>(error_tensor);
                if (err_val > stag_thresh && iter < max_iter - 1) {
                    terminated_by_restart_stag_threshold = true;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES] Early termination: rel_error=" << err_val
                                  << " > " << stag_thresh << " after restart " << (iter + 1)
                                  << ", skipping " << (max_iter - iter - 1)
                                  << " remaining restarts (saves "
                                  << (max_iter - iter - 1) * restart
                                  << " JVP calls)" << std::endl;
                    }
                    break;
                }
            }
        }
    }

    // DIAGNOSTIC: GMRES completion
    // OPT Pass33: Batch 4 D2H into single torch::stack() + gate printing
    float x_norm, r_true_final, r_precond_final, rel_error_final;
    // FIX (2025-12-04): Report total Arnoldi iterations (not restart count)
    int final_iterations = total_arnoldi_iters;
    {
        torch::NoGradGuard no_grad;

        // v20.14r27i: Halo-zeroed residual for final error (consistent with internal check).
        // For 1D packed state, zero_halo_regions is no-op (dim < 3) → raw basis.
        // Both internal (gmres_residual_norm) and final paths use the same semantics.
        auto r_true_inner_final = r_true.clone();
        zero_halo_regions(r_true_inner_final, halo_width, periodic_x, periodic_y);

        // OPT Pass33: Batch norms into single D2H transfer
        // v20.14 r50: Use bnorm_unscaled for final rel_error (trust-region compatibility).
        // GMRES internal convergence used bnorm_safe (D⁻¹-scaled when block_scaled).
        auto stats_cpu = torch::stack({
            x.norm(), r_true_inner_final.norm(), r_precond.norm(), bnorm_unscaled
        }).to(torch::kCPU);
        x_norm = stats_cpu[0].item<float>();
        r_true_final = stats_cpu[1].item<float>();  // Halo-zeroed, UNSCALED residual
        r_precond_final = stats_cpu[2].item<float>();
        float bnorm_val = stats_cpu[3].item<float>();  // UNSCALED ||b||
        // CRITICAL (2025-11-28): Compute relative error for trust region predicted formula
        // v20.14 r50: Always report UNSCALED rel_error to Newton/trust-region.
        rel_error_final = (bnorm_val > BNORM_MIN_THRESHOLD) ? (r_true_final / bnorm_val) : 1.0f;
        // Always print GMRES completion summary (single line)
        bool gmres_converged = (rel_error_final < tol);
        std::cerr << "[FGMRES] " << (gmres_converged ? "CONVERGED" : "NOT CONVERGED")
                  << std::fixed << std::setprecision(4)
                  << ": ||x||=" << x_norm
                  << ", ||r_true||=" << r_true_final
                  << ", ||b||=" << bnorm_val
                  << ", rel_error=" << rel_error_final
                  << ", tol=" << tol
                  << ", restarts=" << actual_restarts
                  << ", arnoldi=" << total_arnoldi_iters
                  << (block_scaled ? " (block-scaled)" : "")
                  << std::defaultfloat << std::endl;
    }

    bool gmres_converged = (rel_error_final < tol);
    // Return contract (v20.14r25, all paths unified):
    //   final_residual = ||r_true|| halo-zeroed (absolute norm)
    //   rel_error      = ||r_true||/||b|| halo-zeroed (relative)
    //   r_true         = RAW residual b-A(x) — callers apply halo zeroing for per-block analysis
    torch::Tensor r_true_out = r_true.detach().clone();
    // PR 9B (review refactor): reason + message resolved by the shared
    // resolve_krylov_termination — see its contract note near the top of
    // namespace krylov_methods.
    const KrylovTerminationResolution resolution = resolve_krylov_termination(
        "FGMRES", gmres_converged, terminated_by_restart_stag_threshold,
        early_exit_reason, terminated_by_internal_convergence,
        total_arnoldi_iters, max_iter, restart, actual_restarts);
    const std::string& gmres_msg = resolution.message;
    WRFNewtonKrylovSolver::GMRESResult res{
            x, gmres_converged, final_iterations, r_true_final,
            rel_error_final, gmres_msg, r_true_out, actual_restarts, false,
            terminated_by_arnoldi_stagnation || terminated_by_restart_stag_threshold};
    res.termination_reason = resolution.reason;
    res.initial_rel_error = initial_rel_error_fgmres;
    res.rho_S_initial = initial_rel_error_fgmres;
    res.rho_D_initial = initial_rho_D_fgmres;
    // R13.17 (external review P0-1): BOTH convergences on the record. The loop stops on the
    // D-weighted objective it minimises (rho_D, both sides D-scaled -- no mixed denominator) and
    // success is judged on the unweighted rho_S. A solve with rho_D < eta and rho_S >= eta met its
    // own objective and was reported as a failed linear solve; collapsed into one `success` that
    // state is indistinguishable from "the operator could not be solved". Production behaviour is
    // unchanged -- what changes is that the seam is stated.
    // R13.18 (deep review P0-1): name the metric the loop stopped on, rather than calling it D.
    res.stopping_metric = static_cast<int>(
        !block_scaled ? wrf::sdirk3::KrylovStoppingMetric::IdentityS
                      : (wrms_metric_applied ? wrf::sdirk3::KrylovStoppingMetric::StageWRMS
                                             : wrf::sdirk3::KrylovStoppingMetric::BlockD));
    res.arnoldi_spent = total_arnoldi_iters;
    res.arnoldi_allowed = max_iter * restart;
    res.rho_D_final = guarded_item<float>(error_tensor);
    res.rho_S_final = res.rel_error;
    res.tolerance_applied = tol;
    res.D_tolerance_reached = (res.rho_D_final >= 0.0f && res.rho_D_final < tol);
    res.S_tolerance_reached = (res.rho_S_final >= 0.0f && res.rho_S_final < tol);
    res.probe_j = diag_probe_j;
    res.probe_true_err = diag_probe_true_err;
    res.probe_hopeless_floor = diag_probe_floor;
    res.stag_ratio_used = diag_stag_ratio;
    res.stag_count_final = diag_stag_count;
    return res;
}


} // namespace krylov_methods

// Newton-Krylov solver implementation
class sdirk3::WRFNewtonKrylovSolver::Impl {
public:
    WRFNewtonKrylovOptions options_;
    ConvergenceStats stats_;
    // PR 9E (diagnosis-only): detached FINAL fast RHS and Newton defect of the
    // most recent record-stage solve. Stored as sync-free tensor handles during
    // the Newton loop; their norms are materialized (one .item() each) only in
    // get_stats(), so no GPU->CPU transfer happens per Newton iteration.
    torch::Tensor diag_final_F_;
    torch::Tensor diag_final_R_;
    // PR 9F (diagnosis-only): the stage value K captured at the SAME residual
    // evaluation as diag_final_F_/R_, so {K, F, R} is a coherent triple, plus its
    // evaluation-point identifiers. diag_solve_generation_ increments once per
    // solve_stage entry so a retry gets a distinct generation.
    torch::Tensor diag_final_K_;
    int diag_final_newton_iter_ = -1;
    int diag_retry_generation_ = -1;
    int diag_solve_generation_ = 0;

    // The stage gate's weighting for the CURRENT stage, frozen by the caller before solve_stage.
    // Stage-stamped, so one frozen for another stage is refused rather than silently reused.
    wrf::sdirk3::FrozenStageWeights stage_weights_;

    // A MONOTONIC instance id, never a `this` pointer -- addresses are recycled, and this project
    // has already shipped one latch keyed on a recycled address.
    //
    // Uses the PROCESS-WIDE generator that already existed in wrf_sdirk3_u_slow_diagnostics.h. I
    // wrote a private duplicate here first, having failed to check -- the same "find the producer
    // before inventing one" step that turned up the token's fields and the WRMS weights. Sharing
    // it also makes ids unique ACROSS solver kinds in one process, which two private counters
    // could not be.
    const uint64_t solver_id_ = wrf::sdirk3::next_solver_id();
    // R13 E1: the policy manifest's IDENTITY. The record carried `stage` and nothing else,
    // so an offline comparator had no way to distinguish two occurrences of the same stage --
    // from two timesteps, a retry, another tile or another rank -- and keyed on the stage
    // alone, which means it silently compared whichever occurrence happened to come last in
    // each arm. Those need not be the same occurrence, and a diff over mismatched occurrences
    // still reports agreement.
    //
    // step_seq advances when the stage number does NOT advance, rather than on `stage == 1`:
    // in ARK324 the first stage is explicit and never reaches this solver, so keying on stage 1
    // would leave the counter at zero for the whole run.
    // R13.10 (N6): whether the frozen A/B actually ran in this solve. Without it a Newton loop
    // that converged or stalled before the target iteration emitted nothing at all.
    bool      frozen_ab_fired_this_solve_ = false;
    int       manifest_last_stage_ = -1;
    long long manifest_step_seq_   = 0;
    // Distinguishes repeated solves of the SAME physical stage within one host timestep --
    // a retry is not a second timestep, and without this the two collapse onto one key.
    long long manifest_retry_generation_ = 0;
    long long manifest_last_host_ts_     = -2;
    WRFPreconditioner* preconditioner_ = nullptr;
    int mu_size_ = 0;  // Size of mu component for SDIRK3
    
    // 4DVAR trajectory storage
    std::vector<torch::Tensor> trajectory_;
    int global_timestep_ = 0;  // Track timesteps for checkpointing (non-static)

    // Predictor bootstrap tracking
    bool bootstrap_done_ = false;     // Bootstrap runs ONCE per solver instance (first timestep only)
    bool bootstrap_exempt_explosion_ = false;  // v20.14r44: skip explosion guard after bootstrap
    bool stage3_warmstart_disabled_ = false;   // Disable warm-start if repeated non-improving attempts
    bool stage3_warmstart_disable_logged_ = false;
    int stage3_warmstart_noimprove_streak_ = 0;
    // v20.14r61: Hopeless Stage-2 GMRES detection.
    bool stage2_hopeless_budget_mode_ = false;
    int stage2_hopeless_streak_ = 0;
    static constexpr int stage2_hopeless_restart_cap_ = 3;
    // v20.14r60/r64: Hopeless Stage-3 GMRES detection.
    // If stage>=3 repeatedly hits "GMRES total failure + ru-dominant residual"
    // under stage-budget mode, cap Stage-3 restart budget aggressively to
    // reduce repeated JVP/Arnoldi overhead on known hopeless patterns.
    bool stage3_hopeless_budget_mode_ = false;
    int stage3_hopeless_streak_ = 0;
    static constexpr int stage3_hopeless_restart_cap_ = 2;
    // Stage-local predictor caches across timesteps (same-stage warm start).
    torch::Tensor k2_prev_;
    torch::Tensor k3_prev_;
    
    // Newton-level graph caching
    struct CachedJacobianInfo {
        torch::Tensor U_cached;  // State at which Jacobian was computed
        torch::Tensor F_cached;  // RHS F(U_cached) to avoid recomputation
        std::function<torch::Tensor(const torch::Tensor&)> cached_rhs;
        bool is_valid = false;
        float dt_cached = 0.0f;
        float gamma_cached = 0.0f;
        int reuse_count = 0;
        const int max_reuse = 3;  // Max reuses before recompute
    } jacobian_cache_;

    // PERFORMANCE: Cache StateLayout to avoid recomputation in every JVP call
    // Computed once at solver construction from grid dimensions
    StateLayout cached_layout_;

    // 9F.D121 (review 9.4): probe latches are per-SOLVER, not process-global. A file-scope
    // static latches across every solver, tile and configuration in the process, so the second
    // solver's stage 2 is silently never measured and the first one's numbers get read as
    // though they described it.
    int probe_numrange_stage_ = -1;
    int probe_blockgain_stage_ = -1;
    bool layout_initialized_ = false;

    // Block diagonal scaling for GMRES conditioning
    // S_diag_ maps each element to its block's physics-based reference scale
    // S_inv_diag_ is the element-wise reciprocal
    // Built/rebuilt when layout is known and device/dtype matches K
    torch::Tensor S_diag_;
    torch::Tensor S_inv_diag_;
    // PR 9F.9 (numerical review P1-1 SHADOW): the FROZEN iter-0 inverse scale. The
    // production convergence metric uses S_inv_diag_, which grows monotonically over
    // Newton iterations (S[b]=max(S_old,rms(R_b))). The review argues that growing S
    // LOOSENS ||S^-1 R|| rather than tightening it. This captures S_0 once at iter 0 so
    // a diagnosis-only shadow can report BOTH ||S^-1 R|| (dynamic) and ||S_0^-1 R||
    // (fixed) and MEASURE where the two verdicts diverge -- no behaviour change.
    torch::Tensor S0_inv_diag_;
    // PR 9F.A (A4 -- structural separation, [[sdirk3-scaling-metric-separation-plan]]):
    // the METRIC-domain inverse scale. FOUR distinct concepts are conflated in S_inv_diag_:
    // (1) LINEAR-system scaling for GMRES conditioning, and (2) the NONLINEAR convergence /
    // EW / trust METRIC. These want DIFFERENT policies -- the linear scale may grow
    // dynamically to help conditioning, but the metric must stay fixed or growing-S loosens
    // the convergence test (S up => ||S^-1 R|| down). This accessor names the metric-domain
    // use. In PR A it ALIASES the dynamic S_inv_diag_ (byte-identical -- verified by
    // tests/numerical_fingerprint.sh). PR B1 will point it at the FIXED S0_inv_diag_,
    // turning a scattered hunt into a one-line policy change. GMRES/linear sites keep using
    // S_inv_diag_ directly -- they are the linear domain and must NOT be routed here.
    const torch::Tensor& metric_scale_inv() const { return S_inv_diag_; }
    // PR 9F.9 P1-4 SHADOW: the GMRES linear residual r_g = b - A*dK from the current
    // iteration's solve, saved so the trust-region site (a later, nested scope where
    // gmres_result is gone) can build the EXACT predicted reduction with no extra JVP.
    // Written only under debug_level>=1; carries no numerical effect.
    torch::Tensor last_gmres_r_true_;
    bool scaling_initialized_ = false;
    bool physics_scaling_set_ = false;  // True after set_physics_scaling() called
    torch::Device scaling_device_ = torch::kCPU;
    torch::Dtype scaling_dtype_ = torch::kFloat32;

    // 1D halo mask for packed state vectors.
    // Binary mask (1 = interior, 0 = halo) built from StateLayout + grid dims.
    // Replaces zero_halo_regions which is no-op on 1D tensors (dim < 3 early-return).
    torch::Tensor halo_mask_;
    bool halo_mask_initialized_ = false;
    // Track halo config for stale-mask detection
    int halo_mask_hw_ = -1;
    bool halo_mask_px_ = false;
    bool halo_mask_py_ = false;

    // v20.14: Adaptive tuning control flag.
    // Default (retune_mode=0): fires once per run (solver lifetime).
    // Optional (retune_mode=1): reset at each timestep stage 1 for non-stationary cases.
    // Set after first valid per-block residual sample + successful dynamic_cast.
    // Not reset in reset_stats() — controlled by retune_mode in solve_stage_impl().
    bool adaptive_tuning_once_per_run_ = false;
    // v20.14r27l: Deferred ru-dominance lock counter.
    // Requires 2 consecutive ru-dominance observations before locking adaptive tuning.
    int ru_dominance_count_ = 0;

    // v20.14r16: Per-instance diagnostic state (moved from process-global statics).
    // Prevents data races when multiple solver instances exist (multi-tile/OpenMP).
    int precond_fallback_count_ = 0;
    int precond_total_calls_ = 0;
    // R13.16 (round 6, R6-5): CONFIGURATION IS OBJECT STATE, read once at construction -- not a
    // function-local static latched on the first numerical call. This tree states that rule in
    // prose (wrf_sdirk3_tile_unified.h, 9F.D33) and the first version of this knob broke every
    // consequence it lists: process-wide, so two solvers in one process could not differ; a unit
    // test could not vary it; and its provenance was "the environment at the first solve" rather
    // than at construction. Accepted range (0, 1]; out of range warns and keeps the default.
    double krylov_no_progress_threshold_ = wrf::sdirk3::kKrylovNoProgressVsR0;
    // R13.20 (numerics referee, claim 7.1): the Taylor excitation floor gets the same treatment
    // as the no-progress threshold -- a per-run override, the value on the record, and its
    // selection effect stated. It decides WHICH BLOCK `tau_excited_block_max` names.
    double tau_excitation_share_ = wrf::sdirk3::kTauExcitationShare;
    bool   tau_excitation_share_observed_ = false;
    // R13.17 (external review P1-4): the same treatment for the total-failure rule. It was a
    // function-local static, so the first solve in a process latched it for every solver in that
    // process -- the defect fixed for the threshold one commit earlier, left in its sibling.
    bool krylov_failure_vs_r0_ = false;
    bool jvp_vs_fd_done_ = false;
    bool singularity_check_done_ = false;

    // v20.14 r49: JVP auto-bench locked mode (per-instance, thread-safe).
    // -1 = unlocked (use g_sdirk3_config), 0 = forward, 1 = central.
    // Reset at each timestep stage 1 entry for long-run adaptivity.
    int jvp_locked_mode_ = -1;

    // v20.14 r49: Saved ru_share for block-aware trust-region.
    float last_ru_share_ = 0.0f;

    // v20.14 r63: Stage-local GMRES warm-start cache (default-off via config).
    // Cache stores unscaled dK solution by stage index.
    std::vector<torch::Tensor> gmres_warmstart_stage_;
    std::vector<float> gmres_warmstart_relerr_stage_;
    std::vector<bool> gmres_warmstart_varpc_stage_;
    // One-step temporal history for the same stage (shape-safe INN source).
    std::vector<torch::Tensor> gmres_warmstart_prev_stage_;
    std::vector<float> gmres_warmstart_prev_relerr_stage_;
    std::vector<bool> gmres_warmstart_prev_varpc_stage_;

    // MEMORY OPTIMIZATION: Reusable epsilon buffer for JVP finite-difference
    // Avoids allocating full state-sized tensor (full_like/zeros_like) every Arnoldi iteration
    torch::Tensor eps_buffer_;

    // MEMORY OPTIMIZATION: Reusable scaled epsilon buffer for retry loops
    // Avoids cloning eps_tensor on every JVP call for epsilon scaling
    torch::Tensor eps_scaled_;

    // Adaptive timestep control (simplified)
    struct {
        float current_dt = 1.0f;
        float dt_min = 0.1f;
        float dt_max = 10.0f;
    } adaptive_control_;

    // FIX 2026-01-29: Eisenstat-Walker state as member (not function-static)
    // Reset per solve_stage_impl call to prevent cross-stage contamination.
    float ew_prev_res_norm_ = 1e10f;
    float ew_prev_eta_ = 0.0f;  // GR v9 G3: Choice 2 safeguard state

    // Trust-region control (added 2025-10-26)
    // v20.14r27g: Initial radius wired from config nk_trust_radius (was hardcoded 1.0).
    float trust_radius_ = wrf::sdirk3::g_sdirk3_config.nk_trust_radius;
    const float trust_radius_min_ = 1e-6f;   // Minimum allowed radius
    // FIX 2026-01-29: Increased from 10.0 to 1000.0. GMRES produces ||dK||≈150 for
    // em_b_wave, so max_radius=10 permanently clips steps to 10/150=0.067 of the Newton
    // direction, causing slow linear convergence (38+ iters). With 1000, the trust-region
    // can expand to accept the full Newton step when rho≈1.
    const float trust_radius_max_ = 1000.0f;

    // v20.14r27x: Baseline unscaled-RMS from Stage 1 iter 0 of current timestep.
    // Used to make explosion threshold case-agnostic: threshold = max(1e6, 1000 * baseline).
    // Reset at each Stage 1 entry, so Stage 2/3 inherit a meaningful reference.
    float baseline_unscaled_rms_ = 0.0f;

    explicit Impl(const WRFNewtonKrylovOptions& options, int mu_size = 0) : options_(options), mu_size_(mu_size) {
        {
            krylov_failure_vs_r0_ =
                wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_FAILURE_VS_R0");
            const char* e = std::getenv("WRF_SDIRK3_KRYLOV_NOPROGRESS_VS_R0");
            if (e) {
                char* end = nullptr;
                const double v = std::strtod(e, &end);
                if (end && *end == '\0' && v > 0.0 && v <= 1.0) {
                    krylov_no_progress_threshold_ = v;
                } else {
                    std::cerr << "[SDIRK3 WARN] WRF_SDIRK3_KRYLOV_NOPROGRESS_VS_R0='" << e
                              << "' is not in (0, 1]; keeping "
                              << krylov_no_progress_threshold_ << std::endl;
                }
            }
            const char* ex = std::getenv("WRF_SDIRK3_TAU_EXCITATION_SHARE");
            if (ex) {
                char* end = nullptr;
                const double v = std::strtod(ex, &end);
                if (end && *end == '\0' && v > 0.0 && v <= 1.0) {
                    tau_excitation_share_ = v;
                    tau_excitation_share_observed_ = true;
                } else {
                    std::cerr << "[SDIRK3 WARN] WRF_SDIRK3_TAU_EXCITATION_SHARE='" << ex
                              << "' is not in (0, 1]; keeping "
                              << tau_excitation_share_ << std::endl;
                }
            }
        }
        reset_stats();
        if (options_.save_trajectory) {
            trajectory_.reserve(options_.checkpoint_interval);
            // MEMORY WARNING: Each checkpoint stores full 3D state (~4MB for 1M elements)
            // With checkpoint_interval=10, this grows by ~40MB per 10 timesteps
            // PRODUCTION RECOMMENDATION: Disable save_trajectory for long runs or use larger intervals
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[TRAJECTORY] WARNING: Trajectory storage enabled" << std::endl;
                std::cerr << "  Checkpoint interval: " << options_.checkpoint_interval << std::endl;
                std::cerr << "  Memory growth: ~4MB per checkpoint (for 1M state)" << std::endl;
                std::cerr << "  Disable with options.save_trajectory = false for production" << std::endl;
            }
        }
        
        // Initialize adaptive control parameters
        if (options_.use_adaptive_timestep || options_.use_adaptive_tolerances) {
            adaptive_control_.dt_min = options_.dt_min;
            adaptive_control_.dt_max = options_.dt_max;
        }

        gmres_warmstart_stage_.resize(8);
        gmres_warmstart_relerr_stage_.assign(8, 1.0f);
        gmres_warmstart_varpc_stage_.assign(8, false);
        gmres_warmstart_prev_stage_.resize(8);
        gmres_warmstart_prev_relerr_stage_.assign(8, 1.0f);
        gmres_warmstart_prev_varpc_stage_.assign(8, false);

        // PERFORMANCE: Initialize StateLayout cache from grid dimensions
        // This avoids recomputing layout on every JVP call
        if (options_.nx > 0 && options_.ny > 0 && options_.nz > 0 &&
            options_.nx_u > 0 && options_.ny_v > 0 && options_.nz_w > 0) {
            cached_layout_ = StateLayout::from_grid_dims(
                options_.nx, options_.ny, options_.nz,
                options_.nx_u, options_.ny_v, options_.nz_w
            );
            layout_initialized_ = true;
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                std::cerr << "[LAYOUT CACHE] Initialized at construction:" << std::endl;
                std::cerr << "  Grid: " << options_.nx << "×" << options_.ny << "×" << options_.nz << std::endl;
                std::cerr << "  Stagger: " << options_.nx_u << "," << options_.ny_v << "," << options_.nz_w << std::endl;
                std::cerr << "  Total size: " << cached_layout_.total_size << std::endl;
            }
        } else if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[LAYOUT CACHE] Grid dimensions not set - will use heuristic per call" << std::endl;
        }
    }

    void maybe_save_checkpoint(const torch::Tensor& state, int stage) {
        if (!(options_.save_trajectory && stage == 1)) {
            return;
        }
        global_timestep_++;
        if (global_timestep_ % options_.checkpoint_interval != 0) {
            return;
        }
        trajectory_.push_back(state.clone().detach());
        if (options_.verbose) {
            DEBUG_PRINT("Checkpoint saved at timestep " << global_timestep_
                     << " (hour " << global_timestep_ * 10.0f / 3600.0f << ")");
        }
    }
    
    // Build 1D halo mask from StateLayout + grid dimensions.
    // Each 3D block in the packed state vector has shape (nj, nk, ni) flattened
    // in row-major order. Halo elements are zeroed in non-periodic directions.
    // mu block is 2D (nj, ni) — same halo logic in j and i.
    // Build 1D halo mask from StateLayout + grid dimensions.
    // target_device and target_dtype specify the final tensor properties.
    // Mask is always built as float32 on CPU (for safe accessor<float>),
    // then cast to target dtype/device at the end.
    //
    // R13.20 (round 9, R9-9): the v20.14r27f note here said "CURRENTLY UNUSED -- build_halo_mask()
    // is never called", and v20.14r27m added a call ten lines above it without correcting the
    // claim. It IS called, from solve_stage_impl, under
    //   options_.is_multi_tile && (!periodic_x || !periodic_y) && !halo_mask_initialized_ &&
    //   layout_initialized_
    // so the invariant holds for single-tile em_b_wave and is FALSE for exactly the multi-tile
    // configuration the mask was retained for -- which is the reading a maintainer needs when
    // deciding whether a 1-D halo no-op matters.
    // Reason it stays gated: halo masking degraded single-tile accuracy (rel_error 0.5940 -> 0.7984).
    void build_halo_mask(torch::Device target_device, torch::Dtype target_dtype) {
        int hw = wrf::sdirk3::g_sdirk3_config.halo_width;
        // v20.14r21: Use options_ (instance state) instead of global config.
        bool px = options_.periodic_x;
        bool py = options_.periodic_y;

        if (hw <= 0) {
            // No halos — mask is all ones
            halo_mask_ = torch::ones({cached_layout_.total_size},
                torch::TensorOptions().dtype(target_dtype).device(target_device));
            halo_mask_initialized_ = true;
            halo_mask_hw_ = hw; halo_mask_px_ = px; halo_mask_py_ = py;
            return;
        }

        int64_t nx = options_.nx;
        int64_t ny = options_.ny;
        int64_t nz = options_.nz;
        int64_t nx_u = options_.nx_u;
        int64_t ny_v = options_.ny_v;
        int64_t nz_w = options_.nz_w;

        // Always build on CPU as float32 for safe accessor<float, 1> usage,
        // then cast to target dtype/device at the end.
        halo_mask_ = torch::ones({cached_layout_.total_size},
            torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
        auto mask_acc = halo_mask_.accessor<float, 1>();

        // Helper: mark halo elements as 0 for a 3D block of shape (nj, nk, ni).
        // Matches zero_halo_regions guards: skip direction if domain <= 2*hw.
        // Uses int64_t arithmetic to prevent overflow on large domains.
        auto mark_3d_halos = [&](int64_t block_start, int64_t nj, int64_t nk, int64_t ni) {
            bool zero_j = (!py) && (nj > 2 * hw);
            bool zero_i = (!px) && (ni > 2 * hw);
            if (!zero_j && !zero_i) return;  // Nothing to mask
            for (int64_t j = 0; j < nj; ++j) {
                bool j_halo = zero_j && (j < hw || j >= nj - hw);
                for (int64_t k = 0; k < nk; ++k) {
                    for (int64_t i = 0; i < ni; ++i) {
                        bool i_halo = zero_i && (i < hw || i >= ni - hw);
                        if (j_halo || i_halo) {
                            mask_acc[block_start + j * nk * ni + k * ni + i] = 0.0f;
                        }
                    }
                }
            }
        };

        // Helper: mark halo elements for a 2D block of shape (nj, ni).
        auto mark_2d_halos = [&](int64_t block_start, int64_t nj, int64_t ni) {
            bool zero_j = (!py) && (nj > 2 * hw);
            bool zero_i = (!px) && (ni > 2 * hw);
            if (!zero_j && !zero_i) return;
            for (int64_t j = 0; j < nj; ++j) {
                bool j_halo = zero_j && (j < hw || j >= nj - hw);
                for (int64_t i = 0; i < ni; ++i) {
                    bool i_halo = zero_i && (i < hw || i >= ni - hw);
                    if (j_halo || i_halo) {
                        mask_acc[block_start + j * ni + i] = 0.0f;
                    }
                }
            }
        };

        // Pre-validate all blocks before building mask.
        // If any block is out of bounds, the layout is corrupt — abort mask entirely.
        for (const auto& blk : cached_layout_.blocks) {
            if (blk.start < 0 || blk.start + blk.size > cached_layout_.total_size) {
                std::cerr << "[HALO MASK] ERROR: block '" << blk.name
                          << "' out of bounds (start=" << blk.start << ", size=" << blk.size
                          << ", total=" << cached_layout_.total_size
                          << "). Aborting mask build." << std::endl;
                halo_mask_initialized_ = false;
                return;
            }
        }

        for (const auto& blk : cached_layout_.blocks) {
            int64_t bs = blk.start;
            if (blk.name == "ru") {
                mark_3d_halos(bs, ny, nz, nx_u);
            } else if (blk.name == "rv") {
                mark_3d_halos(bs, ny_v, nz, nx);
            } else if (blk.name == "rw") {
                mark_3d_halos(bs, ny, nz_w, nx);
            } else if (blk.name == "ph") {
                mark_3d_halos(bs, ny, nz_w, nx);
            } else if (blk.name == "t") {
                mark_3d_halos(bs, ny, nz, nx);
            } else if (blk.name == "mu") {
                mark_2d_halos(bs, ny, nx);
            }
        }

        // PR 9F.9.6 (review §5): the PRODUCTION mask authority must reject an EMPTY active
        // domain, not only the trust-shadow helper. A non-empty packed residual whose every
        // cell is halo (active_dofs == 0) is a layout/halo wiring failure -- if such a mask
        // were applied, res_old/res_new would both be 0 and the trust region would treat a
        // dead solve as a perfectly-converged one. This cannot happen for a valid layout
        // (mark_*_halos only zeros a direction when the domain exceeds 2*hw, so interior
        // cells always survive), so the check is byte-identical in practice; it fails CLOSED
        // -- refusing to install a degenerate mask -- if a future multi-tile layout regresses.
        {
            torch::NoGradGuard no_grad;
            const int64_t active_dofs =
                static_cast<int64_t>(halo_mask_.sum().item<float>());
            if (cached_layout_.total_size > 0 && active_dofs <= 0) {
                std::cerr << "[HALO MASK] ERROR: empty active domain (active_dofs="
                          << active_dofs << " of total=" << cached_layout_.total_size
                          << ") -- every cell is halo. Layout/halo wiring failure; refusing "
                             "to install a degenerate all-zero mask." << std::endl;
                halo_mask_initialized_ = false;
                return;
            }
        }

        // Cast to target dtype/device
        halo_mask_ = halo_mask_.to(target_dtype).to(target_device);
        halo_mask_initialized_ = true;
        halo_mask_hw_ = hw; halo_mask_px_ = px; halo_mask_py_ = py;

        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            torch::NoGradGuard no_grad;
            int64_t total = cached_layout_.total_size;
            auto mask_cpu = halo_mask_.to(torch::kFloat32).to(torch::kCPU);
            int64_t interior = static_cast<int64_t>(mask_cpu.sum().item<float>());
            std::cerr << "[HALO MASK] Built 1D mask: " << interior << "/" << total
                      << " interior elements (" << (100.0f * interior / total)
                      << "%), hw=" << hw << ", px=" << px << ", py=" << py
                      << ", dtype=" << target_dtype << std::endl;
        }
    }

    // Apply halo zeroing to a tensor (in-place).
    // 3D tensors: zero_halo_regions (direct boundary zeroing).
    // 1D packed tensors: mul by halo_mask_ IF mask was built -- which happens only on multi-tile
    // non-periodic runs (see build_halo_mask). When halo_mask_initialized_=false, which is the
    // single-tile default, the 1D path is a no-op. R13.20: "currently never" was wrong here too.
    void apply_halo_zeroing(torch::Tensor& t) {
        if (t.dim() >= 3) {
            // Original 3D path
            int hw = wrf::sdirk3::g_sdirk3_config.halo_width;
            // v20.14r21: Use options_ for periodic flags.
            bool px = options_.periodic_x;
            bool py = options_.periodic_y;
            zero_halo_regions(t, hw, px, py);
        } else if (halo_mask_initialized_ && t.numel() == halo_mask_.numel()) {
            t.mul_(halo_mask_);
        }
        // else: no mask available, skip (preserves existing no-op behavior)
    }

    // v20.5 FIX: Compute scaled RMS norm with halo masking for consistent Newton/line-search norms.
    // Returns ||S⁻¹·R||_RMS = ||S⁻¹·R||₂ / √N  with halo mask applied.
    // This ensures line search uses the SAME norm as trust region and convergence checks.
    // Without this, line search compared scaled RMS (initial_residual) vs unscaled L2 (res_new_val),
    // causing alpha to collapse to minimum every iteration (||R_new||_L2 ≈ 1.7e8 ≫ target ≈ 3.5).
    float compute_scaled_rms_norm(const torch::Tensor& R) {
        torch::NoGradGuard no_grad;
        float sqrt_N = std::sqrt(static_cast<float>(R.numel()));

        if (scaling_initialized_) {
            // PR 9F.A (A4): METRIC-domain scaling -- see metric_scale_inv().
            auto R_scaled = metric_scale_inv() * R;
            // Apply halo mask (zero out halo regions so they don't contribute to norm)
            if (halo_mask_initialized_ && R_scaled.numel() == halo_mask_.numel()) {
                R_scaled = R_scaled * halo_mask_;
            }
            return R_scaled.norm().to(torch::kCPU).item<float>() / sqrt_N;
        } else {
            // Fallback: unscaled RMS with halo mask
            torch::Tensor R_work = R;
            if (halo_mask_initialized_ && R.numel() == halo_mask_.numel()) {
                R_work = R * halo_mask_;
            }
            return R_work.norm().to(torch::kCPU).item<float>() / sqrt_N;
        }
    }

    // The stage weighting, but only when it was frozen for the stage now being solved. A
    // weighting from an earlier bind describes a different linearization, and handing it over
    // would weight this defect by another stage's error weights -- which is precisely what
    // StageIdentity exists to prevent.
    const wrf::sdirk3::FrozenStageWeights* stage_weights_for(int stage) const {
        wrf::sdirk3::StageIdentity now;
        now.solver_id = solver_id_;
        now.capture_seq = stage_weights_.stage.capture_seq;   // whatever was handed over
        now.ark_stage = stage;
        now.point = wrf::sdirk3::WeightingPoint::StageEntry;
        return stage_weights_.usable(now) ? &stage_weights_ : nullptr;
    }

    // Weights at the state the operator is ACTUALLY linearized about.
    //
    // FGMRES solves the Newton system formed at Y_n = B + h*K_n, not at the stage's entry state.
    // e_i(Y) = max(rtol*|Y_i| + atol, floor) depends on Y, so weighting the defect by the
    // stage-entry state applies the gate's FORMULA at the wrong point -- the same rule, a
    // different metric. Re-captured per Newton iteration from the same config the caller handed
    // over, so the tolerances still have one source.
    //
    // Built only when the probe is armed: it is one axpy and one weight vector per Newton
    // iteration, which is nothing next to a Krylov solve but is not free, and the default path
    // must not pay for a diagnostic.
    const wrf::sdirk3::FrozenStageWeights* newton_weights_for(
        const torch::Tensor& U_eval, int stage, int newton_iter) const {
        const auto* handed = stage_weights_for(stage);
        if (!handed || !layout_initialized_) return nullptr;
        if (!U_eval.defined()) return nullptr;

        torch::NoGradGuard ng;
        // THE state the operator is linearized at, taken FROM the solver rather than rebuilt.
        //
        // The first version recomputed U_stage + dt*gamma*K here. That formula is correct today
        // -- it is literally the definition at :4794 -- but a reconstruction guarantees nothing,
        // it only coincides. A later term, coefficient or clamp in U_eval would leave this copy
        // silently describing a state the JVP was never linearized at, and the probe would report
        // weights for it. Same authority-duplication as the packed block sizes, next_solver_id
        // and the boolean spellings, and the same fix: use the one value, do not restate it.
        const auto& Y_n = U_eval;

        wrf::sdirk3::StageIdentity ident = handed->stage;
        ident.point = wrf::sdirk3::WeightingPoint::NewtonLinearization;
        newton_weights_ = wrf::sdirk3::capture_stage_weights(
            Y_n, cached_layout_, handed->cfg, ident);
        newton_weights_.newton_iter = newton_iter;   // stamped for the emitted record
        newton_weights_newton_iter_ = newton_iter;
        return newton_weights_.scale.is_valid() ? &newton_weights_ : nullptr;
    }
    mutable wrf::sdirk3::FrozenStageWeights newton_weights_;
    mutable int newton_weights_newton_iter_ = -1;
    mutable uint64_t consumed_weight_seq_ = 0;

    // A capture is valid for exactly ONE stage solve, and it is consumed HERE -- at stage entry,
    // once -- not on each FGMRES call.
    //
    // Consuming per call is what limited the probe to the FIRST Newton linear solve:
    // stage_weights_for() runs every Newton iteration, so iterations 1+ were refused and the
    // reported krylov series covered one solve rather than the stage. Moving the consumption to
    // stage entry keeps the leftover protection -- a capture already used by an earlier stage is
    // dropped rather than silently reused -- while every Newton iteration of THIS stage is
    // measured.
    void consume_stage_weights_at_entry() {
        const uint64_t seq = stage_weights_.stage.capture_seq;
        if (seq == 0 || seq == consumed_weight_seq_) {
            stage_weights_ = wrf::sdirk3::FrozenStageWeights{};   // stale or absent
            return;
        }
        consumed_weight_seq_ = seq;
    }

    WRFNewtonKrylovSolver::NewtonResult solve_stage_impl(
        const torch::Tensor& U_n,
        const torch::Tensor& K_prev,
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs_fast,
        float dt,
        float gamma,
        int stage,
        const torch::Tensor& F_phys = torch::Tensor()) {  // Added F_phys with default

        // PR 9E (diagnosis-only): clear the retained final fast-RHS / defect
        // tensors so a stale value from a previous solve can never leak into
        // this stage's history summary. Populated (when stage_operand_diag is on)
        // at the residual site below; norms materialized once in get_stats().
        diag_final_F_ = torch::Tensor();
        diag_final_R_ = torch::Tensor();
        consume_stage_weights_at_entry();
        diag_final_K_ = torch::Tensor();
        diag_final_newton_iter_ = -1;
        diag_retry_generation_ = -1;
        // A fresh generation for THIS solve attempt (a stage retry re-enters here
        // and gets the next generation), so a stale triple from a prior attempt
        // can never be mistaken for this one.
        ++diag_solve_generation_;

        // FIX Round156: Gate entry debug with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cout << "\n=== ENTERED solve_stage_impl ===" << std::endl;
            std::cout << "  U_n shape: " << U_n.sizes() << std::endl;
            std::cout << "  dt=" << dt << ", gamma=" << gamma << ", stage=" << stage << std::endl;

            // Check autograd status
            std::cerr << "\n=== NEWTON SOLVER CONFIG CHECK ===" << std::endl;
            std::cerr << "  use_autograd: " << (wrf::sdirk3::g_sdirk3_config.use_autograd ? "TRUE (SLOW!)" : "FALSE (OK)") << std::endl;
            std::cerr << "  debug_level: " << wrf::sdirk3::g_sdirk3_config.debug_level << std::endl;
            std::cerr << "  n_threads: " << wrf::sdirk3::g_sdirk3_config.n_threads << std::endl;
            std::cerr << "  torch::autograd::GradMode::is_enabled: " << (torch::autograd::GradMode::is_enabled() ? "TRUE (WILL BUILD GRAPH!)" : "FALSE (OK)") << std::endl;
            std::cerr << "==============================" << std::endl;
        }

        // v20.14r27v: Do NOT reset ru_dominance_count_ at stage 1.
        // Counter now tracks consecutive timesteps of ru-dominance.
        // Reset happens in the adaptive tuning block when pair_frac >= 0.1
        // (line ~3144), giving proper "N consecutive" semantics.

        // v20.14 r54: Keep solver-local JVP auto-bench lock by default to avoid
        // paying re-benchmark RHS calls every timestep. When adaptive retune mode
        // is explicitly enabled, allow stage-1 lock reset for dynamic re-tuning.
        const auto& cfg_stage_entry = wrf::sdirk3::g_sdirk3_config;
        const bool reset_jvp_lock_stage1 =
            (cfg_stage_entry.adaptive_retune_mode == 1) ||
            cfg_stage_entry.jvp_auto_bench_lock_reset_stage1;
        if (stage == 1 &&
            cfg_stage_entry.jvp_auto_bench_calls > 0 &&
            reset_jvp_lock_stage1) {
            jvp_locked_mode_ = -1;
        }

        // v20.14: Optional per-timestep re-tuning (retune_mode=1).
        // At stage 1: unconditionally clear override (safe no-op if not active)
        // and reset adaptive flag, so tuning starts fresh from config baseline.
        if (stage == 1 && wrf::sdirk3::g_sdirk3_config.adaptive_retune_mode == 1) {
            auto* unified = dynamic_cast<UnifiedPreconditioner*>(preconditioner_);
            if (unified) {
                unified->clear_theta_acoustic_override();  // no-op if not active
            }
            // v20.14r18: Reset diagnostic one-shot flags at stage 1 only (not every solve_stage).
            jvp_vs_fd_done_ = false;
            singularity_check_done_ = false;
            if (adaptive_tuning_once_per_run_) {
                adaptive_tuning_once_per_run_ = false;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[ADAPTIVE] Retune mode=1: reset for stage 1" << std::endl;
                }
            }
        }

        // PERFORMANCE FIX (2025-11-30): Disable autograd globally when using FD mode
        // This prevents compute_rhs from building expensive graphs unnecessarily
        // Uses RAII unique_ptr so guard is automatically restored on ANY return path
        std::unique_ptr<torch::NoGradGuard> global_no_grad;
        if (!wrf::sdirk3::g_sdirk3_config.use_autograd) {
            global_no_grad = std::make_unique<torch::NoGradGuard>();
            std::cerr << "[NEWTON] Autograd disabled globally for FD mode (GradMode now: "
                      << (torch::autograd::GradMode::is_enabled() ? "ON" : "OFF") << ")" << std::endl;
        }

        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "\n=== NEWTON SOLVER ENTRY ===" << std::endl;
            std::cerr << "  Stage: " << stage << std::endl;
            std::cerr << "  dt: " << dt << ", gamma: " << gamma << std::endl;
            std::cerr << "  dt*gamma: " << dt*gamma << " (implicit timestep)" << std::endl;
            
            // SDIRK3 WARNING: Check if timestep is too large for initial testing
            if (dt > 100.0f && stage == 1) {
                std::cerr << "  WARNING: Large timestep dt=" << dt << "s detected!" << std::endl;
                std::cerr << "           For initial SDIRK3 testing, consider dt=10-60s" << std::endl;
                std::cerr << "           Large timesteps may cause Newton convergence issues" << std::endl;
            }
            
            std::cerr << "  U_n dim: " << U_n.dim() << ", size: " << U_n.numel() << std::endl;
            if (U_n.dim() > 0) {
                std::cerr << "  U_n shape: [";
                for (int i = 0; i < U_n.dim(); ++i) {
                    std::cerr << U_n.size(i);
                    if (i < U_n.dim() - 1) std::cerr << ", ";
                }
                std::cerr << "]" << std::endl;
            }
            std::cerr << "  max_newton_iter: " << options_.max_newton_iter << std::endl;
            std::cerr << "  newton_tol: " << options_.newton_tol << std::endl;
            std::cerr << "  gmres_restart: " << options_.gmres_restart << std::endl;
            std::cerr << "=========================" << std::endl;
        }
        
        reset_stats();
        
        // Get adaptive parameters if enabled
        float newton_tol_adaptive = options_.newton_tol;
        float krylov_tol_adaptive = static_cast<float>(options_.krylov_tol);
        float init_R0_norm = 0.0f;  // Captured at iter 0 for relative convergence criterion
        float last_res_scaled = 0.0f;  // v20.3: Last Newton residual (float) for adaptive α

        // FIX 2026-01-29: Reset E-W state per stage to prevent cross-stage contamination.
        // Previously was function-static, leaking residual norms between stages/timesteps.
        ew_prev_res_norm_ = 1e10f;
        ew_prev_eta_ = 0.0f;

        // Initial guess for K using explicit Euler predictor
        // For fully implicit SDIRK3, a good initial guess is crucial
        torch::Tensor K;

        // Stage-specific initial guess strategy
        if (stage == 1) {
            // NOTE: bootstrap_done_ is NEVER reset - bootstrap runs ONCE per solver instance
            // (i.e., only on the FIRST timestep of the simulation when initial conditions
            // are furthest from the solution). This is by design - subsequent timesteps have
            // better initial guesses from previous stage solutions and don't need bootstrap.

            // Stage-1 predictor policy (time-local, no method-table change):
            // - 1 history: reuse previous timestep k1
            // - 2 histories: damped linear extrapolation in k-space
            // This targets the recurring "per-timestep stage-1" inefficiency.
            if (K_prev.defined() && K_prev.size(0) >= 1) {
                const char* predictor_label = "previous timestep K";
                K = K_prev[0];
                if (K_prev.size(0) >= 2) {
                    // NOTE: This is a state-derivative linear extrapolation predictor,
                    // not a full AB2 time integrator update of y.
                    const torch::Tensor K_latest = K_prev[0];
                    const torch::Tensor K_older = K_prev[1];
                    K = K_latest + 0.5f * (K_latest - K_older);
                    predictor_label = "damped 2-step predictor";
                }
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "Stage 1: Using " << predictor_label << ", ||K|| = "
                             << guarded_item<float>(K.norm()) << std::endl;
                }
            } else {
                // v20.14r44: Bootstrap predictor for first timestep / post-abort.
                // Use explicit seed + one Picard sample, then optionally apply a single
                // preconditioned residual correction (strong-acoustic predictor).
                bootstrap_done_ = false;

                // Stage-1 no-history bootstrap:
                // 1) explicit seed      K0 = F(U_n)
                // 2) one Picard update  K1 = F(U_n + dt*gamma*K0)
                //
                // This keeps SDIRK stage-1 implicit (method table unchanged), but follows
                // the predictor spirit used by ARKODE: provide a better explicit stage
                // estimate before Newton correction.
                const auto& predictor_rhs = compute_rhs_fast ? compute_rhs_fast : compute_rhs;
                torch::Tensor K_seed = predictor_rhs(U_n);
                K = K_seed;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    torch::NoGradGuard no_grad;
                    std::cerr << "[PREDICTOR] Stage-1 bootstrap seed K0=F(U_n), ||K0|| = "
                              << K_seed.to(torch::kCPU).norm().item<float>() << std::endl;
                }
                const float k0_norm = guarded_item<float>(safe_tensor_norm(K_seed));

                bool picard_available = false;
                bool picard_bad = false;
                bool picard_explosive = false;
                torch::Tensor K_picard;
                float k1_norm = k0_norm;

                if (std::isfinite(dt) && std::isfinite(gamma) && dt > 0.0f) {
                    torch::Tensor U_picard = U_n + dt * gamma * K_seed;
                    K_picard = predictor_rhs(U_picard);
                    picard_available = true;
                    picard_bad = guarded_item<bool>(torch::any(torch::isnan(K_picard))) ||
                                 guarded_item<bool>(torch::any(torch::isinf(K_picard)));
                    k1_norm = guarded_item<float>(safe_tensor_norm(K_picard));
                    picard_explosive = (k1_norm > 10.0f * std::max(k0_norm, 1.0f));
                    if (!picard_bad && !picard_explosive) {
                        K = K_picard;
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            torch::NoGradGuard no_grad;
                            std::cerr << "[PREDICTOR] Stage-1 Picard bootstrap K1=F(U_n+dt*gamma*K0), ||K1|| = "
                                      << K_picard.to(torch::kCPU).norm().item<float>() << std::endl;
                        }
                    } else if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        if (picard_bad) {
                            std::cerr << "[PREDICTOR] Stage-1 Picard bootstrap produced NaN/Inf; "
                                      << "falling back to K0=F(U_n)." << std::endl;
                        } else {
                            std::cerr << "[PREDICTOR] Stage-1 Picard bootstrap rejected (||K1||/||K0||="
                                      << (k1_norm / std::max(k0_norm, 1.0f))
                                      << " > 10); falling back to K0=F(U_n)." << std::endl;
                        }
                    }
                } else if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[PREDICTOR] Stage-1 Picard bootstrap skipped (invalid dt/gamma)." << std::endl;
                }

                // Strong acoustic switch:
                // For stiff startup cases, prefer bounded base K=0 over aggressive Picard seed.
                // This keeps one-step preconditioned correction stable and cheap.
                float r_seed_norm = 0.0f;
                float stiffness_ratio = 0.0f;
                bool strong_acoustic_switch = false;
                if (picard_available && !picard_bad) {
                    torch::NoGradGuard no_grad;
                    r_seed_norm = (K_seed.detach() - K_picard.detach()).norm().to(torch::kCPU).item<float>();
                    stiffness_ratio = r_seed_norm / std::max(k0_norm, 1.0f);
                    strong_acoustic_switch = picard_explosive ||
                        (std::isfinite(stiffness_ratio) && stiffness_ratio > 2.0f);
                }
                if (strong_acoustic_switch) {
                    K = torch::zeros_like(K_seed);
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[PREDICTOR] Stage-1 strong acoustic switch: bounded base K=0"
                                  << " (||K0-K1||=" << r_seed_norm
                                  << ", ratio=" << stiffness_ratio << ")" << std::endl;
                    }
                }

                // Strong-acoustic bootstrap: one preconditioned residual correction.
                // This is a cheap predictor-only refinement (not a full GMRES solve):
                //   r0 = Kbase - F(U_n + dt*gamma*Kbase), delta ~= -M^{-1} r0, K_init = Kbase + delta
                if (preconditioner_ && std::isfinite(dt) && std::isfinite(gamma) && dt > 0.0f) {
                    torch::Tensor strong_candidate;
                    bool candidate_bad = true;
                    float candidate_norm = 0.0f;
                    float delta_norm = 0.0f;
                    float r0_norm = 0.0f;
                    float r1_norm = 0.0f;
                    float r_picard_norm = 0.0f;
                    bool residual_improved = false;
                    bool improved_vs_picard = false;
                    float kbase_norm = guarded_item<float>(safe_tensor_norm(K));
                    {
                        torch::NoGradGuard no_grad;
                        auto Kbase_det = K.detach();
                        auto U_base = U_n + dt * gamma * Kbase_det;
                        auto K1_base = predictor_rhs(U_base).detach();
                        auto r0 = Kbase_det - K1_base;
                        auto delta = -preconditioner_->apply(r0);
                        strong_candidate = (Kbase_det + delta).detach();
                        candidate_bad = torch::isnan(strong_candidate).any().to(torch::kCPU).item<bool>()
                                     || torch::isinf(strong_candidate).any().to(torch::kCPU).item<bool>();
                        if (!candidate_bad) {
                            if (picard_available && !picard_bad) {
                                auto K1_det = K_picard.detach();
                                auto U_picard_next = U_n + dt * gamma * K1_det;
                                auto K2_picard = predictor_rhs(U_picard_next).detach();
                                auto r_picard = K1_det - K2_picard;
                                r_picard_norm = r_picard.norm().to(torch::kCPU).item<float>();
                            }

                            auto U_candidate = U_n + dt * gamma * strong_candidate;
                            auto K1_candidate = predictor_rhs(U_candidate).detach();
                            auto r1 = strong_candidate - K1_candidate;
                            r0_norm = r0.norm().to(torch::kCPU).item<float>();
                            r1_norm = r1.norm().to(torch::kCPU).item<float>();
                            residual_improved = std::isfinite(r0_norm) && std::isfinite(r1_norm)
                                             && (r1_norm < r0_norm);
                            improved_vs_picard = std::isfinite(r_picard_norm) && (r_picard_norm > 0.0f) &&
                                                 std::isfinite(r1_norm)
                                              && (r1_norm < r_picard_norm);
                            candidate_norm = strong_candidate.norm().to(torch::kCPU).item<float>();
                            delta_norm = delta.norm().to(torch::kCPU).item<float>();
                        }
                    }

                    // Keep the predictor bounded to avoid replacing K0 with a wildly amplified direction.
                    const float max_gain = 50.0f;
                    float gain = candidate_norm / std::max(std::max(k0_norm, kbase_norm), 1.0f);
                    if (!candidate_bad && (residual_improved || improved_vs_picard) &&
                        std::isfinite(gain) && gain <= max_gain) {
                        K = strong_candidate.requires_grad_(U_n.requires_grad());
                        bootstrap_exempt_explosion_ = true;  // one-shot exemption at iter 0
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[PREDICTOR STRONG] Stage-1 preconditioned correction accepted: "
                                      << "||K0||=" << k0_norm
                                      << ", ||K1||=" << k1_norm
                                      << ", ||r0||=" << r0_norm
                                      << ", ||r_picard||=" << r_picard_norm
                                      << ", ||r1||=" << r1_norm
                                      << ", ||delta||=" << delta_norm
                                      << ", ||K_init||=" << candidate_norm
                                      << ", gain=" << gain << std::endl;
                        }
                    } else if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        if (candidate_bad) {
                            std::cerr << "[PREDICTOR STRONG] NaN/Inf from preconditioned correction; "
                                      << "keeping Picard/seed predictor." << std::endl;
                        } else if (!(residual_improved || improved_vs_picard)) {
                            std::cerr << "[PREDICTOR STRONG] Rejected: residual did not improve "
                                      << "(||r0||=" << r0_norm
                                      << ", ||r_picard||=" << r_picard_norm
                                      << ", ||r1||=" << r1_norm
                                      << "); keeping Picard/seed predictor." << std::endl;
                        } else {
                            std::cerr << "[PREDICTOR STRONG] Rejected by gain guard (gain=" << gain
                                      << " > " << max_gain << "); keeping Picard/seed predictor." << std::endl;
                        }
                    }
                }

                // Bootstrap seed normalization:
                // For no-history stage-1 startup, keep seed magnitude bounded to reduce
                // launcher/singleton sensitivity while preserving direction information.
                constexpr float bootstrap_seed_norm_cap = 50.0f;
                auto bootstrap_seed_norm = safe_tensor_norm(K);
                if (guarded_item<bool>(bootstrap_seed_norm > bootstrap_seed_norm_cap)) {
                    K = K * (torch::full({}, bootstrap_seed_norm_cap, K.options()) / bootstrap_seed_norm);
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[PREDICTOR BOOTSTRAP] Seed norm capped: "
                                  << guarded_item<float>(bootstrap_seed_norm)
                                  << " -> " << bootstrap_seed_norm_cap << std::endl;
                    }
                }
                bootstrap_done_ = true;
            }  // End else (first timestep: bootstrap predictor)
        } else if (stage >= 2) {
            // Stage >=2 predictor policy:
            // 1) Prefer same-stage cache from previous timestep (stage-local warm start).
            // 2) Fallback to current-step K_prev stack (k1/k2 handoff).
            // 3) Final fallback: zero.
            bool used_stage_cache = false;
            bool used_kprev = false;

            if (stage == 2 && k2_prev_.defined()) {
                K = k2_prev_;
                used_stage_cache = true;
            } else if (stage >= 3 && k3_prev_.defined()) {
                K = k3_prev_;
                used_stage_cache = true;
            }

            if (!used_stage_cache && K_prev.defined() && K_prev.size(0) >= 1) {
                K = K_prev.select(0, K_prev.size(0) - 1);
                used_kprev = true;
            } else if (used_stage_cache && K_prev.defined() && K_prev.size(0) >= 1) {
                used_kprev = true;  // for diagnostics only
            }

            if (used_stage_cache || used_kprev) {
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "Stage " << stage << ": Using predictor from "
                              << (used_stage_cache ? "stage-cache" : "K_prev")
                              << ", ||K|| = " << guarded_item<float>(K.norm());
                    if (used_kprev && K_prev.defined()) {
                        int n_prev = K_prev.size(0);
                        std::cerr << " (K_prev depth=" << n_prev << ")";
                    }
                    std::cerr << std::endl;
                }
            } else {
                K = torch::zeros_like(U_n);
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "Warning: Stage " << stage
                              << " predictor cache/K_prev unavailable; using zero initial guess."
                              << std::endl;
                }
            }
        } else {
            // Fallback to zero
            K = torch::zeros_like(U_n);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "Warning: Using zero initial guess for stage " << stage << std::endl;
            }
        }
        
        // C1 FIX 2026-02-15: NaN/Inf check ALWAYS runs (was gated behind debug_level>=1).
        // Cost: 1 GPU sync per stage, justified for production safety — corrupted k1_prev_
        // or failed bootstrap must not pass unchecked into Newton loop.
        if (guarded_item<bool>(torch::any(torch::isnan(K))) || guarded_item<bool>(torch::any(torch::isinf(K)))) {
            std::cerr << "ERROR: NaN/Inf in initial guess! Using zero instead." << std::endl;
            K = torch::zeros_like(U_n);
        }

        // Additional safety: limit initial K magnitude to prevent explosive start
        float K_init_max = 1e6f;  // Maximum initial K magnitude
        // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
        auto K_init_norm = safe_tensor_norm(K);
        if (guarded_item<bool>(K_init_norm > K_init_max)) {
            // PHASE 1D FIX: Use torch::full to create device-aware scalar (not CPU-only torch::tensor)
            K = K * (torch::full({}, K_init_max, K.options()) / K_init_norm);
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "WARNING: Initial K too large (" << guarded_item<float>(K_init_norm)
                         << "), scaling down to " << K_init_max << std::endl;
            }
        }
        
        // FIX 2026-01-29: U_n already IS U_stage — the caller (solveImplicitStage) passes
        // U_stage = U_n + dt*sum(a_ij*K_j) as the first argument. So no recomputation needed.
        // Previously this code added dt*a*K_prev AGAIN, causing double-counting and
        // explosion at timestep 2 (||b|| went from 0.18 to 6.3e8).
        const torch::Tensor& U_stage = U_n;

        // Track last residual for failure path
        // STATS FIX: Track detached tensor for accurate final_residual at debug_level=0
        float last_res_norm = 0.0f;
        float prev_iter_res_norm = 0.0f;  // v20.14r27e: For Newton stall detection
        torch::Tensor res_norm_detached;  // Detached tensor for deferred .item() call
        int stagnation_count = 0;  // FIX 2026-01-31: Track consecutive zero-update iterations
        int newton_stall_count = 0;  // v20.14r27e: Consecutive low-reduction iterations
        bool stage2_hopeless_detected = false;  // v20.14r61
        bool stage2_hopeless_promoted_early = false;  // v20.14r62: same-timestep cap activation
        bool stage2_intra_step_cap_retry_used = false;  // v20.14r62: allow one immediate retry
        bool stage3_hopeless_detected = false;  // v20.14r60

        // v19: Enforce layout assumption — fail fast if violated
        TORCH_CHECK(layout_initialized_ && cached_layout_.total_size == K.numel(),
                    "SDIRK3: layout not initialized or size mismatch at solve_stage entry. "
                    "layout_initialized_=", layout_initialized_,
                    ", cached_layout_.total_size=", cached_layout_.total_size,
                    ", K.numel()=", K.numel());

        // Build placeholder S = I before Newton loop.
        // The real adaptive S is built from R₀ at iter 0 (see rebuild_scaling_from_R0 below).
        // This placeholder ensures scaling_initialized_ = true so the GMRES operator path
        // is always the scaled path, avoiding "silent downgrade" to unscaled.
        //
        // CRITICAL FIX (2026-02-03): S must be built from R₀ (initial residual), not K
        // (initial guess). K's predictor is often near-zero at stage entry, causing all
        // blocks to hit the floor (S ≈ I). R₀ = K - F(U + dt·γ·K) reflects actual RHS
        // tendency magnitudes (ru~3000, rw~26000, ph~90, mu~0.01), giving meaningful scaling.
        // S is then FIXED for the entire Newton stage (no per-iter refresh) to keep the
        // metric ||S⁻¹R|| consistent across iterations for rtol comparison.
        //
        // v19: Skip placeholder when physics scaling is already set by tile solver.
        // v19.1: Also skip allocation if S is already initialized on correct device/dtype
        // (avoids redundant tensor allocation every stage in RMS mode).
        if (!physics_scaling_set_ && !(scaling_initialized_ &&
            scaling_device_ == K.device() && scaling_dtype_ == K.scalar_type())) {
            bool can_build = layout_initialized_ && cached_layout_.total_size == K.numel();
            if (can_build) {
                int64_t n = cached_layout_.total_size;
                auto opts = torch::TensorOptions().dtype(K.scalar_type()).device(torch::kCPU);
                S_diag_ = torch::ones({n}, opts);
                S_inv_diag_ = torch::ones({n}, opts);
                S_diag_ = S_diag_.to(K.device());
                S_inv_diag_ = S_inv_diag_.to(K.device());
                scaling_device_ = K.device();
                scaling_dtype_ = K.scalar_type();
                scaling_initialized_ = true;  // Placeholder S=I; rebuilt from R₀ at iter 0
            }
        }

        // NOTE: Layout size == K.numel() is guaranteed by TORCH_CHECK above.
        // (Previously had a warning branch here; removed as unreachable after v19.)

        // v20.14r26: Halo mask build DISABLED.
        // The 1D halo mask was introduced in v20.14 to zero boundary DOFs in
        // packed state vectors (K, dK, R). However, it prevents the Newton-GMRES
        // solver from adjusting boundary values, degrading convergence:
        //   v20.13 (no mask): rel_error = 0.5940
        //   v20.14 (with mask): rel_error = 0.7984
        // Root cause: K.mul_(halo_mask_) zeros K's boundary before residual eval,
        // and apply_halo_zeroing(dK) zeros GMRES updates at boundaries. The solver
        // cannot adjust boundary K values, so residual at boundary propagates to interior.
        // For multi-tile production where halos may need special treatment, use
        // enable_stage_halo_exchange instead (inter-stage MPI exchange).

        // v20.14r27m: Conditionally activate 1D halo mask for multi-tile non-periodic runs.
        // Single-tile: tile==domain, boundary K values are true DOFs → no masking.
        // Multi-tile non-periodic: halo K values are stale MPI ghosts → zero them.
        if (options_.is_multi_tile && (!options_.periodic_x || !options_.periodic_y) &&
            !halo_mask_initialized_ && layout_initialized_) {
            build_halo_mask(K.device(), K.scalar_type());
            if (halo_mask_initialized_ && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[HALO MASK] Activated for multi-tile non-periodic run" << std::endl;
            }
        }

        // v20.5: Set stage-specific state for preconditioner adaptation
        // Extract mu_pert from U_stage and pass to preconditioner for mu_full computation
        if (preconditioner_ && layout_initialized_ && cached_layout_.blocks.size() >= 6) {
            // mu is the last block (index 5): {"mu", offset, size}
            const auto& mu_block = cached_layout_.blocks[5];
            if (mu_block.name == "mu" && mu_block.size > 0 &&
                mu_block.start + mu_block.size <= U_stage.numel()) {
                // Extract mu_pert as 1D slice
                auto mu_pert_1d = U_stage.slice(0, mu_block.start, mu_block.start + mu_block.size);
                // Reshape to 2D (ny, nx) - mu is 2D field (no k dimension)
                int64_t ny = options_.ny;
                int64_t nx = options_.nx;
                if (ny > 0 && nx > 0 && mu_block.size == ny * nx) {
                    auto mu_pert_2d = mu_pert_1d.reshape({ny, nx});
                    // Set stage state (preconditioner internally computes mu_full = mu_base + mu_pert)
                    auto* unified_precond = dynamic_cast<UnifiedPreconditioner*>(preconditioner_);
                    if (unified_precond) {
                        unified_precond->set_stage_state(mu_pert_2d, stage);
                    }
                }
            }
        }

        // v20.14r59-step3: Stage-3 one-shot preconditioned warm-start.
        // Predictor-only cheap correction:
        //   r0 = K - F(U_stage + dt*gamma*K),  K <- K - M^{-1}r0
        // Default-off behavior: activate only when stage3_* budget override is enabled.
        {
            auto& cfg = wrf::sdirk3::g_sdirk3_config;
            const bool stage3_budget_override_active = (stage >= 3) &&
                (cfg.stage3_gmres_restart > 0 ||
                 cfg.stage3_max_krylov_restarts > 0 ||
                 cfg.stage3_krylov_tol > 0.0f);
            const auto& predictor_rhs = compute_rhs_fast ? compute_rhs_fast : compute_rhs;
            if (cfg.stage3_warmstart &&
                stage >= 3 && stage3_budget_override_active &&
                preconditioner_ && std::isfinite(dt) && std::isfinite(gamma) && dt > 0.0f) {
                if (stage3_warmstart_disabled_) {
                    if (cfg.debug_level >= 1 && !stage3_warmstart_disable_logged_) {
                        std::cerr << "[PREDICTOR WARMSTART] Stage 3 disabled after repeated "
                                  << "non-improving attempts; skipping further warm-start checks."
                                  << std::endl;
                        stage3_warmstart_disable_logged_ = true;
                    }
                } else {
                bool accepted = false;
                float r0_norm = 0.0f;
                float k_base_norm = 0.0f;
                float delta_norm = 0.0f;
                float delta_raw_norm = 0.0f;
                float cand_norm = 0.0f;
                float gain = 0.0f;
                float r1_norm = 0.0f;
                float residual_ratio = 0.0f;
                bool delta_clipped = false;
                float delta_scale = 1.0f;
                float raw_over_seed = 0.0f;
                bool bad_candidate = true;
                bool cheap_gate = false;
                bool bounded_gain = false;
                bool bounded_delta = false;
                bool residual_improved = false;
                torch::Tensor K_candidate;
                {
                    torch::NoGradGuard no_grad;
                    auto K_base = K.detach();
                    auto U_pred = U_stage + dt * gamma * K_base;
                    auto K_rhs = predictor_rhs(U_pred).detach();
                    auto r0 = (K_base - K_rhs).detach();
                    auto delta_raw = -preconditioner_->apply(r0);

                    r0_norm = r0.norm().to(torch::kCPU).item<float>();
                    k_base_norm = K_base.norm().to(torch::kCPU).item<float>();
                    delta_raw_norm = delta_raw.norm().to(torch::kCPU).item<float>();

                    // Conservative cap: keep warm-start correction small vs. current seed.
                    float delta_cap = 0.75f * std::max(k_base_norm, 1.0f);
                    float scale = 1.0f;
                    if (std::isfinite(delta_raw_norm) && delta_raw_norm > delta_cap) {
                        scale = delta_cap / std::max(delta_raw_norm, 1e-30f);
                        delta_clipped = true;
                    }
                    auto delta = delta_raw * scale;
                    delta_norm = delta.norm().to(torch::kCPU).item<float>();
                    K_candidate = (K_base + delta).detach();
                    delta_scale = scale;
                    raw_over_seed = delta_raw_norm / std::max(k_base_norm, 1.0f);

                    bad_candidate = torch::isnan(K_candidate).any().to(torch::kCPU).item<bool>() ||
                                    torch::isinf(K_candidate).any().to(torch::kCPU).item<bool>();
                    if (!bad_candidate) {
                        cand_norm = K_candidate.norm().to(torch::kCPU).item<float>();
                        gain = cand_norm / std::max(k_base_norm, 1.0f);

                        // Predictor acceptance guard:
                        // 1) bounded norm amplification
                        // 2) bounded correction size
                        // 3) fixed-point residual improvement after one extra sample
                        bounded_gain = std::isfinite(gain) && gain <= 1.25f;
                        bounded_delta = std::isfinite(delta_norm) &&
                                        delta_norm <= 0.75f * std::max(k_base_norm, 1.0f);
                        // Cheap gate before expensive extra RHS:
                        // If correction was severely clipped or raw correction is wildly out-of-scale
                        // versus seed K, skip r1 re-evaluation and reject early.
                        const bool clip_not_severe = (!delta_clipped) || (delta_scale >= 0.20f);
                        const bool raw_scale_reasonable = std::isfinite(raw_over_seed) &&
                                                          (raw_over_seed <= 1.0e3f);
                        cheap_gate = clip_not_severe && raw_scale_reasonable;
                        if (cheap_gate && bounded_gain && bounded_delta) {
                            auto U_cand = U_stage + dt * gamma * K_candidate;
                            auto K_rhs_cand = predictor_rhs(U_cand).detach();
                            auto r1 = (K_candidate - K_rhs_cand).detach();
                            r1_norm = r1.norm().to(torch::kCPU).item<float>();
                            residual_ratio = r1_norm / std::max(r0_norm, 1e-30f);
                            residual_improved = std::isfinite(r1_norm) &&
                                                std::isfinite(r0_norm) &&
                                                (r0_norm > 0.0f) &&
                                                (r1_norm <= 0.90f * r0_norm);
                        } else {
                            residual_improved = false;
                        }
                        accepted = std::isfinite(r0_norm) && (r0_norm > 0.0f) &&
                                   bounded_gain && bounded_delta && residual_improved;
                    }
                }

                if (accepted) {
                    K = K_candidate.requires_grad_(U_n.requires_grad());
                    stage3_warmstart_noimprove_streak_ = 0;
                    stage3_warmstart_disable_logged_ = false;
                    if (cfg.debug_level >= 1) {
                        std::cerr << "[PREDICTOR WARMSTART] Stage " << stage
                                  << " accepted: ||r0||=" << r0_norm
                                  << ", ||K||=" << k_base_norm
                                  << ", ||delta||=" << delta_norm
                                  << ", ||delta_raw||=" << delta_raw_norm
                                  << ", ||K_new||=" << cand_norm
                                  << ", ||r1||=" << r1_norm
                                  << ", r1/r0=" << residual_ratio
                                  << ", gain=" << gain
                                  << (delta_clipped ? " (delta clipped)" : "")
                                  << std::endl;
                    }
                } else if (cfg.debug_level >= 1) {
                    std::cerr << "[PREDICTOR WARMSTART] Stage " << stage
                              << " skipped: bad_candidate=" << (bad_candidate ? "true" : "false")
                              << ", ||r0||=" << r0_norm
                              << ", ||K||=" << k_base_norm
                              << ", ||delta||=" << delta_norm
                              << ", ||delta_raw||=" << delta_raw_norm
                              << ", ||r1||=" << r1_norm
                              << ", r1/r0=" << residual_ratio
                              << ", gain=" << gain
                              << ", cheap_gate=" << (cheap_gate ? "true" : "false")
                              << ", delta_scale=" << delta_scale
                              << ", raw_over_seed=" << raw_over_seed
                              << ", bounded_gain=" << (bounded_gain ? "true" : "false")
                              << ", bounded_delta=" << (bounded_delta ? "true" : "false")
                              << ", residual_improved=" << (residual_improved ? "true" : "false")
                              << (delta_clipped ? " (delta clipped)" : "")
                              << std::endl;
                }

                if (!accepted) {
                    if (!residual_improved) {
                        ++stage3_warmstart_noimprove_streak_;
                    } else {
                        stage3_warmstart_noimprove_streak_ = 0;
                    }
                    if (stage3_warmstart_noimprove_streak_ >= 1) {
                        stage3_warmstart_disabled_ = true;
                        if (cfg.debug_level >= 1) {
                            std::cerr << "[PREDICTOR WARMSTART] Stage 3 auto-disabled: "
                                      << "non-improving streak="
                                      << stage3_warmstart_noimprove_streak_
                                      << std::endl;
                            stage3_warmstart_disable_logged_ = true;
                        }
                    }
                }
                }
            }
        }

        auto update_stage_predictor_cache = [&](bool converged, const torch::Tensor& K_stage) {
            if (stage != 2 && stage != 3) {
                return;
            }

            bool has_bad = guarded_item<bool>(torch::any(torch::isnan(K_stage))) ||
                           guarded_item<bool>(torch::any(torch::isinf(K_stage)));
            if (has_bad) {
                if (stage == 2) {
                    k2_prev_ = torch::Tensor();
                } else {
                    k3_prev_ = torch::Tensor();
                }
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[PREDICTOR CACHE] Stage " << stage
                              << " cache cleared (NaN/Inf in K)." << std::endl;
                }
                return;
            }

            torch::Tensor* slot = (stage == 2) ? &k2_prev_ : &k3_prev_;
            const bool seed_empty = !slot->defined();
            if (converged || seed_empty) {
                *slot = K_stage.detach().clone();
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    if (!converged && seed_empty) {
                        std::cerr << "[PREDICTOR CACHE] Stage " << stage
                                  << " non-converged but cache empty; seeding for timestep continuity."
                                  << std::endl;
                    }
                }
                return;
            }

            // Non-converged refresh path:
            // Keep cache moving to latest timestep state to avoid stale repeated predictors.
            // Use conservative blending to reduce jitter from noisy non-converged solves.
            if (slot->sizes() != K_stage.sizes()) {
                *slot = K_stage.detach().clone();
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[PREDICTOR CACHE] Stage " << stage
                              << " cache shape changed; replaced with latest non-converged K."
                              << std::endl;
                }
                return;
            }

            {
                torch::NoGradGuard no_grad;
                constexpr float blend_new = 0.5f;
                auto old_cache = slot->detach();
                auto new_cache = K_stage.detach();
                *slot = ((1.0f - blend_new) * old_cache + blend_new * new_cache).clone();
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    float old_n = old_cache.norm().to(torch::kCPU).item<float>();
                    float new_n = new_cache.norm().to(torch::kCPU).item<float>();
                    float out_n = slot->norm().to(torch::kCPU).item<float>();
                    std::cerr << "[PREDICTOR CACHE] Stage " << stage
                              << " refreshed (non-converged blend): "
                              << "||old||=" << old_n
                              << ", ||new||=" << new_n
                              << ", ||cache||=" << out_n << std::endl;
                }
            }
        };

        // Newton iteration with graph caching
        int actual_newton_iters = 0;
        // R13.5: the loop's own bound, recorded from the loop. Reading it anywhere else is a
        // second authority that can disagree with the iteration that actually ran.
        stats_.newton_iteration_budget = options_.max_newton_iter;
        stats_.final_residual_measured = false;
        frozen_ab_fired_this_solve_ = false;
        for (int newton_iter = 0; newton_iter < options_.max_newton_iter; ++newton_iter) {
            actual_newton_iters = newton_iter + 1;
            // Debug: print Newton iteration start
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "\n--- Newton iteration " << newton_iter << " starting ---" << std::endl;
            }

            // Telemetry-only snapshots for this Newton iteration.
            float ew_eta_used_this_iter = -1.0f;
            bool ew_eta_updated_this_iter = false;
            const bool ew_eta_enabled_this_iter = options_.use_adaptive_tolerances;
            // R13.20 (round 9, R9-5): which knob BOUND this iteration's inner tolerance.
            // Declared here, inside the Newton loop body, so it cannot inherit the previous
            // iteration's answer -- its predecessor was a bool declared outside the loop and
            // never reset, inert only because both E-W arms happened to set it. Seeded `Base`
            // because with adaptive tolerances off the tolerance IS `options_.krylov_tol`; the
            // E-W block below overwrites it with the arm that bound.
            wrf::sdirk3::KrylovToleranceSource ew_tol_source =
                wrf::sdirk3::KrylovToleranceSource::Base;
            
            // Zero K's halo elements before forming U_eval.
            // compute_rhs does not apply halo/BC internally (WRF handles halos
            // at MPI level after tile completion). If K has non-zero halos from
            // GMRES updates, they would propagate into U_eval and potentially
            // contaminate interior stencils that reach into halo cells.
            if (halo_mask_initialized_) {
                K.mul_(halo_mask_);
            }

            // Evaluate residual: R(K) = K - F(U_stage + dt*gamma*K)
            //
            // KNOWN LIMITATION (halo staleness in U_stage):
            // U_stage = U_n + dt * sum(a_ij * K_j) where K_j are previous stage
            // derivatives. K halos are zeroed above, but U_n's halos reflect WRF's
            // state at the start of the timestep — no inter-stage halo exchange
            // occurs within the Newton solver. For single-tile runs (em_b_wave)
            // tile boundaries == domain boundaries, so this is not an issue.
            // For multi-tile runs with stencils wider than 1 cell, U_stage halos
            // may be stale by O(dt * a_ij * K_j).
            //
            // Precision levels for multi-tile halo handling:
            //  (a) Exact: halo exchange every Newton iteration (expensive, MPI inside inner loop)
            //  (b) Accurate: halo exchange between SDIRK stages (2 exchanges/step)
            //      → Implemented: enable_stage_halo_exchange config flag in tile_unified_impl
            //  (c) Approximate: halo exchange once after all stages, before final update
            // Default is (c) via WRF's existing post-tile exchange.
            // For multi-tile production, enable (b) via:
            //   wrf_sdirk3_set_config_bool("enable_stage_halo_exchange", 1)
            // Note: (b) is inter-stage only — within Newton iterations, U_stage halos
            // remain fixed. For exact solve (a), exchange would be needed here inside
            // the Newton loop, but the cost (MPI per Newton iter) is prohibitive.
            //
            // v20.14r26: Runtime warning for multi-tile + non-periodic without stage halo exchange.
            if (newton_iter == 0 && stage == 1) {
                static bool halo_stale_warned = false;
                // v20.14r27i: Gate on is_multi_tile to suppress false-positive on single-tile runs.
                // Single-tile: tile==domain, halos are never stale → no warning needed.
                if (!halo_stale_warned &&
                    options_.is_multi_tile &&
                    (!options_.periodic_x || !options_.periodic_y) &&
                    !wrf::sdirk3::g_sdirk3_config.enable_stage_halo_exchange) {
                    halo_stale_warned = true;
                    std::cerr << "\n[HALO STALE WARNING] Non-periodic boundaries detected "
                              << "(periodic_x=" << options_.periodic_x
                              << ", periodic_y=" << options_.periodic_y
                              << ") with enable_stage_halo_exchange=false.\n"
                              << "  For multi-tile runs, U_stage halos become stale across Newton "
                              << "iterations and SDIRK stages, potentially degrading convergence.\n"
                              << "  Single-tile runs (tile==domain) are unaffected.\n"
                              << "  To enable inter-stage halo exchange:\n"
                              << "    wrf_sdirk3_set_config_bool(\"enable_stage_halo_exchange\", 1)\n"
                              << std::endl;
                }
            }
            torch::Tensor U_eval = U_stage + dt * gamma * K;

            // v20.14: Refresh mu stage state from U_eval every Newton iter.
            // Pre-loop set_stage_state uses U_stage (fixed K=0 linearization point).
            // At iter 0, U_eval = U_stage + dt*gamma*K where K is the initial guess;
            // at iter >= 1, K has been updated by GMRES. Both need fresh mu.
            if (preconditioner_ && layout_initialized_ &&
                cached_layout_.blocks.size() >= 6) {
                const auto& mu_block = cached_layout_.blocks[5];
                if (mu_block.name == "mu" && mu_block.size > 0 &&
                    mu_block.start + mu_block.size <= U_eval.numel()) {
                    auto mu_pert_1d = U_eval.slice(0, mu_block.start, mu_block.start + mu_block.size);
                    int64_t ny = options_.ny;
                    int64_t nx = options_.nx;
                    if (ny > 0 && nx > 0 && mu_block.size == ny * nx) {
                        auto mu_pert_2d = mu_pert_1d.reshape({ny, nx});
                        auto* unified_precond = dynamic_cast<UnifiedPreconditioner*>(preconditioner_);
                        if (unified_precond) {
                            unified_precond->set_stage_state(mu_pert_2d, stage);
                        }
                    }
                }
            }

            // P0 DIAGNOSTIC: State validation at Newton iteration start
            if (newton_iter == 0 && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                torch::NoGradGuard no_grad;  // Diagnostic logging only - don't break AD graph

                std::cerr << "[NEWTON] State validation at iteration 0:" << std::endl;

                // PERF FIX 2025-12-27: Pre-copy tensors to CPU once to avoid multiple GPU syncs
                auto U_eval_cpu = U_eval.to(torch::kCPU);
                auto U_stage_cpu = U_stage.to(torch::kCPU);
                auto U_n_cpu = U_n.to(torch::kCPU);
                auto K_cpu = K.to(torch::kCPU);

                // Check for unphysical state values
                auto U_eval_min = U_eval_cpu.min().item<float>();
                auto U_eval_max = U_eval_cpu.max().item<float>();
                auto U_eval_norm = U_eval_cpu.norm().item<float>();
                std::cerr << "[NEWTON] U_eval range: [" << U_eval_min << ", " << U_eval_max << "]" << std::endl;
                std::cerr << "[NEWTON] U_eval norm: " << U_eval_norm << std::endl;

                // v20.14r27o: Extract W-block using cached_layout_ (not hardcoded dims)
                std::cerr << "[NEWTON] Checking U_eval = U_stage + (" << dt << " * " << gamma << ") * K:" << std::endl;
                if (layout_initialized_ && cached_layout_.blocks.size() >= 4) {
                    // blocks: [0]=ru, [1]=rv, [2]=rw, [3]=ph, [4]=t, [5]=mu
                    const auto& rw_blk = cached_layout_.blocks[2];
                    int64_t w_offset = rw_blk.start;
                    int64_t size_w = rw_blk.size;

                    if (U_eval_cpu.numel() >= w_offset + size_w) {
                        auto w_block_eval = U_eval_cpu.slice(0, w_offset, w_offset + size_w);
                        std::cerr << "  W-block in U_eval: min/max = " << w_block_eval.min().item<float>()
                                  << " / " << w_block_eval.max().item<float>() << std::endl;
                        std::cerr << "  W-block norm = " << w_block_eval.norm().item<float>() << std::endl;

                        if (U_stage_cpu.numel() >= w_offset + size_w) {
                            auto w_block_stage = U_stage_cpu.slice(0, w_offset, w_offset + size_w);
                            std::cerr << "  W-block in U_stage: min/max = " << w_block_stage.min().item<float>()
                                      << " / " << w_block_stage.max().item<float>() << std::endl;
                        }

                        float dt_gamma = dt * gamma;
                        std::cerr << "  Scaling factor dt*gamma = " << dt_gamma << std::endl;
                    }
                } else {
                    std::cerr << "  [NEWTON] Layout not initialized - skip W-block extraction" << std::endl;
                }

                // Check for NaN/Inf (already on CPU)
                bool has_nan = torch::isnan(U_eval_cpu).any().item<bool>();
                bool has_inf = torch::isinf(U_eval_cpu).any().item<bool>();
                std::cerr << "[NEWTON] Has NaN: " << (has_nan ? "YES" : "NO") << std::endl;
                std::cerr << "[NEWTON] Has Inf: " << (has_inf ? "YES" : "NO") << std::endl;

                if (has_nan || has_inf) {
                    std::cerr << "[NEWTON] ERROR: State has NaN/Inf - cannot continue!" << std::endl;
                }

                if (U_eval_norm > 1e8) {
                    std::cerr << "[NEWTON] WARNING: Very large state norm (>1e8) - may indicate unphysical conditions" << std::endl;
                }

                // Additional state components for context (already on CPU)
                auto U_n_norm = U_n_cpu.norm().item<float>();
                auto U_stage_norm = U_stage_cpu.norm().item<float>();
                auto K_norm = K_cpu.norm().item<float>();
                std::cerr << "[NEWTON] U_n norm: " << U_n_norm << std::endl;
                std::cerr << "[NEWTON] U_stage norm: " << U_stage_norm << std::endl;
                std::cerr << "[NEWTON] K norm: " << K_norm << std::endl;
                std::cerr << "[NEWTON] dt: " << dt << ", gamma: " << gamma << std::endl;
            }
            
            // CACHE CLARIFICATION (2025-12-04):
            // - can_reuse_jacobian controls whether we reuse the cached compute_rhs function reference
            // - F = compute_rhs(U_eval) is ALWAYS recomputed because K changes each Newton iteration
            // - The "Jacobian" reuse refers to the linearization point, not avoiding F computation
            // - True benefit: Avoids lambda capture re-creation and maintains cached_rhs reference
            bool can_reuse_jacobian = jacobian_cache_.is_valid &&
                                     jacobian_cache_.dt_cached == dt &&
                                     jacobian_cache_.gamma_cached == gamma &&
                                     jacobian_cache_.reuse_count < jacobian_cache_.max_reuse;
            
            if (can_reuse_jacobian && newton_iter > 0) {
                // AUTOGRAD FIX: Check state change without using .item() to maintain autodiff graph
                auto state_diff_norm = (U_eval - jacobian_cache_.U_cached).norm();
                auto u_eval_norm = U_eval.norm();
                auto state_change_tensor = state_diff_norm / u_eval_norm;
                // Use tensor comparison to maintain autodiff compatibility
                auto reuse_tensor = (state_change_tensor < 0.1f).all();
                can_reuse_jacobian = reuse_tensor.allclose(torch::tensor(true, reuse_tensor.options()));
            }
            
            torch::Tensor F;

            // C3 DIAGNOSTIC 2026-02-15: One-time RHS determinism check.
            // If F(K) returns different values for the same K due to floating-point
            // non-associativity in parallel reductions, Newton cannot converge below
            // the noise floor. Enable via RHS_DETERMINISM_CHECK=1.
            static bool rhs_determinism_checked = false;
            // FIX 2026-06-21: was gated stage==1, which in ARK324/mode-3 is ESDIRK-explicit
            // (a_implicit[0][0]=0, no Newton solve) so the check never fired. A stage>=2 gate
            // would instead REGRESS mode-0/baseline SDIRK, whose stage 1 IS implicit (Codex
            // stop-review). Drop the stage gate entirely: this code only runs inside an actual
            // Newton solve, and the once-only flag + newton_iter==0 fire at the FIRST Newton
            // solve in ANY mode — stage 1 for SDIRK/mode-0, stage 2 for ARK324/mode-3.
            if (!rhs_determinism_checked && newton_iter == 0) {
                static const int check_enabled = []() {
                    const char* v = std::getenv("RHS_DETERMINISM_CHECK");
                    return (v && std::atoi(v) > 0) ? 1 : 0;
                }();
                if (check_enabled) {
                    rhs_determinism_checked = true;
                    torch::NoGradGuard no_grad;
                    auto U_test = U_eval.detach().clone();
                    auto F1 = compute_rhs(U_test);
                    auto F2 = compute_rhs(U_test);
                    auto diff = (F1 - F2).norm();
                    auto f1_norm = F1.norm();
                    float rel_diff = (f1_norm.item<float>() > 0)
                        ? diff.item<float>() / f1_norm.item<float>() : 0.0f;
                    std::cerr << "[RHS DETERMINISM] ||F1-F2||/||F1|| = " << rel_diff
                              << " (eps_machine=" << std::numeric_limits<float>::epsilon() << ")"
                              << std::endl;
                    if (rel_diff > 10.0f * std::numeric_limits<float>::epsilon()) {
                        std::cerr << "[RHS DETERMINISM] WARNING: RHS non-deterministic! "
                                  << "rel_diff=" << rel_diff << " >> eps. "
                                  << "This limits GMRES convergence floor." << std::endl;
                    } else {
                        std::cerr << "[RHS DETERMINISM] PASS: RHS is deterministic within float32 eps."
                                  << std::endl;
                    }
                }
            }

            if (!can_reuse_jacobian) {
                // Recompute and cache both U and F(U)
                F = compute_rhs(U_eval);
                // Graph retention is opt-in for adjoint windows only.
                if (wrf::sdirk3::g_sdirk3_config.use_autograd && options_.retain_graph_for_adjoint) {
                    // Preserve graph - WARNING: Memory usage will increase
                    jacobian_cache_.U_cached = U_eval;
                    jacobian_cache_.F_cached = F;
                } else {
                    // Default: Detach to avoid pinning full stage autograd graph in cache
                    jacobian_cache_.U_cached = U_eval.detach();
                    jacobian_cache_.F_cached = F.detach();
                }
                jacobian_cache_.cached_rhs = compute_rhs;
                jacobian_cache_.is_valid = true;
                jacobian_cache_.dt_cached = dt;
                jacobian_cache_.gamma_cached = gamma;
                jacobian_cache_.reuse_count = 0;

                if (options_.verbose) {
                    DEBUG_PRINT("  Newton iter " << newton_iter
                             << ": Recomputing Jacobian and caching F(U)");
                }
            } else {
                // BUGFIX: Always recompute F since K changes between Newton iterations
                // Even if U_cached ≈ U_eval by the 0.1 threshold, K has been updated
                // so U_eval = U_stage + dt*gamma*K is actually different.
                // Reusing stale F causes residual/JVP mismatch → solver explosion
                F = compute_rhs(U_eval);
                if (wrf::sdirk3::g_sdirk3_config.use_autograd && options_.retain_graph_for_adjoint) {
                    jacobian_cache_.F_cached = F;
                    jacobian_cache_.U_cached = U_eval;
                } else {
                    jacobian_cache_.F_cached = F.detach();
                    jacobian_cache_.U_cached = U_eval.detach();
                }
                jacobian_cache_.reuse_count++;

                if (options_.verbose) {
                    DEBUG_PRINT("  Newton iter " << newton_iter
                             << ": Reusing cached Jacobian (reuse #"
                             << jacobian_cache_.reuse_count << ")");
                }
            }

            // CRITICAL FIX (2025-10-18): Residual calculation bug
            // GENERALIZED (2025-10-18): Physics-aware residual calculation
            //
            // compute_rhs returns total forcing:
            //   - With physics:    F = F_dyn + F_phys (wrf_sdirk3_tile_unified_impl.cpp:2900)
            //   - Without physics: F = F_dyn          (wrf_sdirk3_tile_unified_impl.cpp:2902)
            //
            // Residual equation: K = F(K) where K is the implicit stage value
            // Therefore: R = K - F (correct for both physics and no-physics cases)
            //
            // Before buggy code: R = K - F - F_phys  (double-counted F_phys)
            // After fix:         R = K - F           (correct for all configurations)
            torch::Tensor R = K - F;

            // ================================================================
            // R9 P0-C/D: STAGE-ENTRY RESIDUAL DECOMPOSITION (opt-in, default OFF)
            // ================================================================
            // Stage 3 was observed entering Newton at a raw packed residual ~27x stage 2's.
            // The observation reproduces, but it does not localise anything on its own, because
            // a stage-tendency residual
            //
            //     R_s(K) = K - F_I(Y_s + dt*gamma*K)
            //
            // depends on BOTH the stage base Y_s (assembled from the tableau and the earlier
            // stages) AND the predictor K^(0). "Inherited from Y_3" is therefore not readable
            // off the entry norm: a stale or badly chosen predictor produces the same symptom.
            //
            // Holding Y_s FIXED and varying ONLY the predictor is what divides the
            // responsibility:
            //
            //     K0 = 0         R = -F_I(Y_s)          a pure property of the base state
            //     K0 = Picard    K0 = F_I(Y_s)          one fixed-point step from that base
            //     K0 = K_prev    the previous stage's K what stage 3 actually falls back to
            //     K0 = production                       whatever the predictor policy chose
            //
            // Large at K0 = 0 implicates the base state or the stage's F_I scope; large only at
            // the production predictor implicates the predictor/cache.
            //
            // Also reported per row, because the raw packed L2 mixes six blocks with different
            // units and is not the quantity the stage gate judges: the stage-WRMS norm and the
            // worst block's WRMS. Three extra RHS evaluations -- never enable for timing.
            if (newton_iter == 0 &&
                wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_STAGE_ENTRY_LEDGER")) {
                torch::NoGradGuard ng_entry;
                // Weights captured at the BASE state, so every row is judged by the same E.
                const auto* w_entry = newton_weights_for(U_stage, stage, 0);
                const bool have_layout = layout_initialized_ &&
                                         cached_layout_.is_valid() &&
                                         cached_layout_.total_size == R.numel();

                auto emit_row = [&](const char* label, const torch::Tensor& K0,
                                    const torch::Tensor& F0) {
                    const auto R0 = (K0 - F0).detach();
                    const double raw = R0.to(torch::kFloat64).norm().item<double>();
                    const double k0n = K0.detach().to(torch::kFloat64).norm().item<double>();
                    const double f0n = F0.detach().to(torch::kFloat64).norm().item<double>();
                    const double dotv = (K0.detach().to(torch::kFloat64) *
                                         F0.detach().to(torch::kFloat64)).sum().item<double>();
                    const double cosv = (k0n > 0.0 && f0n > 0.0) ? dotv / (k0n * f0n) : 0.0;

                    double wrms = -1.0, blockmax = -1.0;
                    if (w_entry != nullptr && have_layout) {
                        const auto einv = wrf::sdirk3::inverse_scale_vector(
                            cached_layout_, w_entry->scale, R0);
                        if (einv.defined() && einv.numel() == R0.numel()) {
                            const auto wr = (einv.to(torch::kFloat64) *
                                             R0.to(torch::kFloat64));
                            wrms = wr.norm().item<double>() /
                                   std::sqrt(static_cast<double>(wr.numel()));
                            for (const auto& blk : cached_layout_.blocks) {
                                const auto wb = wr.slice(0, blk.start, blk.start + blk.size);
                                const double bq = wb.norm().item<double>() /
                                                  std::sqrt(static_cast<double>(blk.size));
                                if (bq > blockmax) blockmax = bq;
                            }
                        }
                    }
                    std::cerr << "SDIRK3_STAGE_ENTRY stage=" << stage
                              << " predictor=" << label
                              << " raw_l2=" << raw
                              << " wrms=" << wrms
                              << " block_max_wrms=" << blockmax
                              << " K0_norm=" << k0n
                              << " FI_norm=" << f0n
                              << " cos_K0_FI=" << cosv;
                    if (have_layout) {
                        for (const auto& blk : cached_layout_.blocks) {
                            std::cerr << " " << blk.name << "="
                                      << R0.slice(0, blk.start, blk.start + blk.size)
                                             .to(torch::kFloat64).norm().item<double>();
                        }
                    }
                    std::cerr << std::endl;
                };

                // Base state itself, so a large R(0) can be read against the state it came from.
                std::cerr << "SDIRK3_STAGE_ENTRY stage=" << stage
                          << " Y_base_norm=" << U_stage.detach().norm()
                                                    .to(torch::kFloat64).item<double>()
                          << " dt=" << dt << " gamma=" << gamma << std::endl;

                const auto zero_K = torch::zeros_like(K);
                const auto F_base = compute_rhs(U_stage).detach();
                emit_row("zero", zero_K, F_base);

                const auto K_picard = F_base;
                emit_row("picard", K_picard,
                         compute_rhs(U_stage + dt * gamma * K_picard).detach());

                if (K_prev.defined() && K_prev.size(0) >= 1) {
                    const auto K_last = K_prev.select(0, K_prev.size(0) - 1).detach();
                    emit_row("prev_stage", K_last,
                             compute_rhs(U_stage + dt * gamma * K_last).detach());
                }

                // The production row reuses F already computed for R -- no extra evaluation,
                // and by construction it is the same number the Newton loop is about to use.
                emit_row("production", K.detach(), F.detach());
            }

            // PR 9E (diagnosis-only, opt-in): retain the FINAL fast RHS and Newton
            // defect for this record-stage solve. Assigning the detached tensor
            // handles is O(1) and sync-free; the LAST accepted iteration's write
            // wins. The single ||.||.item() materialization happens once in
            // get_stats() (called once per stage solve), NOT per Newton iteration,
            // so there is no per-iteration GPU->CPU transfer. Gated on
            // stage_operand_diag and the record stages (default off) so the OFF
            // path is byte-identical; F is the production tendency, so this is
            // ZERO extra RHS evaluations.
            if (wrf::sdirk3::g_sdirk3_config.stage_operand_diag &&
                (stage == 2 || stage == 3)) {
                // Capture the COHERENT triple: R was just defined as K - F in this
                // exact float32 evaluation, so {K, F, R} share one evaluation point
                // and K - F - R == 0 bit-exactly. The last accepted iteration's
                // write wins; the emitter later verifies this K equals the K the
                // solve actually returns (else DEFECT_UNOBSERVED).
                diag_final_F_ = F.detach();
                diag_final_R_ = R.detach();
                diag_final_K_ = K.detach();
                diag_final_newton_iter_ = newton_iter;
                diag_retry_generation_ = diag_solve_generation_;
            }

            // DIAGNOSTIC: Per-variable residual decomposition (debug_level >= 2)
            // Identifies which variable (ru/rv/rw/ph/t/mu) dominates the residual
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && layout_initialized_ &&
                cached_layout_.total_size == K.numel()) {
                torch::NoGradGuard no_grad;
                auto R_cpu = R.detach().to(torch::kCPU);
                float total_norm = R_cpu.norm().item<float>();
                std::cerr << "[RESIDUAL DECOMPOSITION] Stage " << stage
                          << ", Newton iter " << newton_iter << ":" << std::endl;
                std::cerr << "  ||R_total|| = " << total_norm << std::endl;
                for (const auto& blk : cached_layout_.blocks) {
                    if (blk.start + blk.size <= R_cpu.numel()) {
                        float var_norm = R_cpu.slice(0, blk.start, blk.start + blk.size)
                                             .norm().item<float>();
                        float pct = (total_norm > 1e-14f) ? (var_norm * var_norm / (total_norm * total_norm) * 100.0f) : 0.0f;
                        std::cerr << "  ||R_" << blk.name << "|| = " << var_norm
                                  << " (" << std::fixed << std::setprecision(1) << pct << "% of ||R||^2)" << std::endl;
                    }
                }
                std::cerr << std::defaultfloat;
            }

            // NOTE 2026-02-03 / r50-F3: S is initialized from R₀ at iter 0, then MONOTONICALLY
            // updated: S[b] = max(S_old[b], rms(R_b)) per iteration. S only grows, so the
            // scaled metric ||S⁻¹R|| becomes tighter over time (never artificially looser).
            // This tracks growing components (e.g., ru that grows while preconditioner focuses on ph)
            // without breaking the rtol criterion consistency.

            // FIX 2026-02-01: Compute Newton residual norm on halo-zeroed R for consistency
            // with GMRES (which operates on halo-zeroed vectors).
            // Uses 1D halo mask (apply_halo_zeroing) instead of zero_halo_regions,
            // which was no-op on packed 1D state vectors (dim < 3 early-return).
            auto R_inner = R.clone();
            apply_halo_zeroing(R_inner);

            // FIX 2026-02-03: At iter 0, rebuild S from R₀ (initial residual).
            // The placeholder S=I (lines 1597-1621) is overwritten here with actual
            // tendency-scale magnitudes. S is then FIXED for the rest of this Newton stage.
            // v19: Skip R₀-based rebuild when physics scaling is active.
            if (newton_iter == 0 && scaling_initialized_ && !physics_scaling_set_ &&
                layout_initialized_ && cached_layout_.total_size == K.numel()) {
                torch::NoGradGuard no_grad;
                auto R_cpu = R_inner.detach().to(torch::kCPU).contiguous();
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[SCALING] Rebuilding S from R₀ (iter 0):" << std::endl;
                }
                for (const auto& blk : cached_layout_.blocks) {
                    if (blk.start + blk.size > R_cpu.numel()) continue;
                    auto blk_data = R_cpu.slice(0, blk.start, blk.start + blk.size);
                    float blk_rms = blk_data.norm().item<float>()
                                    / std::sqrt(static_cast<float>(blk.size));
                    // Floor: use per-variable scale from options, default 1.0
                    float floor_val = 1.0f;
                    if (blk.name == "ru" || blk.name == "rv" || blk.name == "rw") {
                        floor_val = options_.scale_u;
                    } else if (blk.name == "ph") {
                        floor_val = options_.scale_ph;
                    } else if (blk.name == "t") {
                        floor_val = options_.scale_t;
                    } else if (blk.name == "mu") {
                        floor_val = options_.scale_mu;
                    }
                    float scale = std::max(blk_rms, floor_val);
                    S_diag_.slice(0, blk.start, blk.start + blk.size).fill_(scale);
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "  S[" << blk.name << "] = " << scale
                                  << " (RMS=" << blk_rms << ", floor=" << floor_val << ")" << std::endl;
                    }
                }
                S_inv_diag_ = S_diag_.reciprocal();
                // Ensure device consistency
                if (S_diag_.device() != K.device()) {
                    S_diag_ = S_diag_.to(K.device());
                    S_inv_diag_ = S_inv_diag_.to(K.device());
                }
                // P1-1 SHADOW: freeze S_0 (the iter-0 scale) so the monotonic growth of
                // S over later iterations can be compared against a fixed reference.
                // PR 9F.9.2: only when the shadow is ON. Cloning a full-state tensor on
                // EVERY production solve just to feed a default-off diagnostic is a
                // default-path regression (the numerics were unchanged, but the default
                // path paid a full-state alloc + copy). Gate it on the flag.
                if (numerical_shadow_enabled())
                    S0_inv_diag_ = S_inv_diag_.detach().clone();
            }

            // r50-F3: Monotonic S update for iter > 0.
            // S[block] = max(S_old[block], rms(R[block])). S only grows, preserving
            // rtol consistency. Tracks growing residual components (e.g., ru stagnation).
            if (newton_iter > 0 && scaling_initialized_ && !physics_scaling_set_ &&
                layout_initialized_ && cached_layout_.is_exact &&
                cached_layout_.total_size == K.numel()) {
                torch::NoGradGuard no_grad;
                auto R_cpu = R_inner.detach().to(torch::kCPU).contiguous();
                bool s_updated = false;
                for (const auto& blk : cached_layout_.blocks) {
                    if (blk.start + blk.size > R_cpu.numel()) continue;
                    auto blk_data = R_cpu.slice(0, blk.start, blk.start + blk.size);
                    float blk_rms = blk_data.norm().item<float>()
                                    / std::sqrt(static_cast<float>(blk.size));
                    float current_s = S_diag_.slice(0, blk.start, blk.start + 1).item<float>();
                    if (blk_rms > current_s) {
                        S_diag_.slice(0, blk.start, blk.start + blk.size).fill_(blk_rms);
                        s_updated = true;
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[SCALING] S[" << blk.name << "] " << current_s
                                      << " -> " << blk_rms << " (monotonic, iter " << newton_iter << ")" << std::endl;
                        }
                    }
                }
                if (s_updated) {
                    S_inv_diag_ = S_diag_.reciprocal();
                    if (S_diag_.device() != K.device()) {
                        S_diag_ = S_diag_.to(K.device());
                        S_inv_diag_ = S_inv_diag_.to(K.device());
                    }
                }
            }

            // FIX 2026-02-03: Use SCALED RMS norm ||S⁻¹·R||/√N for Newton convergence check.
            // RMS (root-mean-square) is grid-size independent: a per-DOF error measure.
            // L2 norm grows with √N, making fixed tolerances meaningless across grid sizes.
            // With N≈1.6M DOFs, ||S⁻¹R||_L2=147 → ||S⁻¹R||_RMS=0.115, which is physically
            // meaningful as ~0.1 per DOF error.
            // Unscaled ||R|| is kept for diagnostic logging only.
            auto res_norm_unscaled_tensor = safe_tensor_norm(R_inner);
            float sqrt_N = std::sqrt(static_cast<float>(R_inner.numel()));
            torch::Tensor res_norm_tensor;
            // PR 9F.B (Gemini #65): keep the scaled metric residual so the block-max gate
            // below can REUSE it instead of recomputing metric_scale_inv()*R_inner. S and
            // R_inner do not change between here and the gate (same Newton iteration), so the
            // value is identical; hoisting only retains the intermediate (byte-identical).
            torch::Tensor R_scaled_metric;
            if (scaling_initialized_) {
                R_scaled_metric = metric_scale_inv() * R_inner;   // PR 9F.A (A4): metric domain
                res_norm_tensor = safe_tensor_norm(R_scaled_metric) / sqrt_N;  // RMS norm
            } else {
                // Fallback: if scaling not initialized, force-initialize from R with floor=1.0
                // to prevent unscaled convergence check from hiding mu/ph errors.
                if (layout_initialized_ && cached_layout_.total_size == K.numel()) {
                    torch::NoGradGuard no_grad;
                    int64_t n = cached_layout_.total_size;
                    auto opts = torch::TensorOptions().dtype(K.scalar_type()).device(torch::kCPU);
                    S_diag_ = torch::ones({n}, opts);
                    auto R_cpu = R_inner.detach().to(torch::kCPU).contiguous();
                    for (const auto& blk : cached_layout_.blocks) {
                        float blk_rms = R_cpu.slice(0, blk.start, blk.start + blk.size)
                                            .norm().item<float>()
                                        / std::sqrt(static_cast<float>(blk.size));
                        float min_scale = 1.0f;
                        if (blk.name == "ru" || blk.name == "rv" || blk.name == "rw") {
                            min_scale = options_.scale_u;
                        } else if (blk.name == "ph") {
                            min_scale = options_.scale_ph;
                        } else if (blk.name == "t") {
                            min_scale = options_.scale_t;
                        } else if (blk.name == "mu") {
                            min_scale = options_.scale_mu;
                        }
                        float scale = std::max(blk_rms, min_scale);
                        S_diag_.slice(0, blk.start, blk.start + blk.size).fill_(scale);
                    }
                    S_inv_diag_ = S_diag_.reciprocal();
                    S_diag_ = S_diag_.to(K.device());
                    S_inv_diag_ = S_inv_diag_.to(K.device());
                    scaling_initialized_ = true;
                    // PR 9F.9.1: freeze S0 on the FALLBACK init path too, else the shadow
                    // could reuse a stale S0 (or none) here. PR 9F.9.2: only when the
                    // shadow is ON (no default-path clone).
                    if (numerical_shadow_enabled() && !S0_inv_diag_.defined())
                        S0_inv_diag_ = S_inv_diag_.detach().clone();
                    std::cerr << "[SCALING] Force-initialized from R (fallback)" << std::endl;
                }
                if (scaling_initialized_) {
                    R_scaled_metric = metric_scale_inv() * R_inner;   // PR 9F.A (A4): metric domain
                    res_norm_tensor = safe_tensor_norm(R_scaled_metric) / sqrt_N;  // RMS norm
                } else {
                    // Last resort: use unscaled RMS (layout not available)
                    res_norm_tensor = res_norm_unscaled_tensor / sqrt_N;
                    std::cerr << "[WARN] Newton convergence using unscaled RMS ||R||/√N (no layout)" << std::endl;
                }
            }
            // NOTE: k_norm_tensor and rel_res_norm removed (2026-02-03).
            // Convergence now uses scaled norm ||S⁻¹R||; relative ||R||/||K|| is no longer needed.

            // PR 9F.9.1 SHADOW (diagnosis-only; own env flag WRF_SDIRK3_NUMERICAL_SHADOW).
            // Report BOTH the production dynamic-S metric and the FIXED-S0 metric, the
            // unscaled RMS, and the per-block MAX RMS under BOTH scales, so the review's
            // claims -- that the monotonically growing S loosens the test, and that a
            // global RMS hides a small non-converged block -- can be MEASURED and cleanly
            // SEPARATED (block_max under S0 isolates aggregation-laundering from
            // dynamic-scale loosening). Read-only under NoGradGuard; NO control flow
            // consumes these values, so the numerical path is byte-identical.
            if (numerical_shadow_enabled() &&
                scaling_initialized_ && S0_inv_diag_.defined() &&
                S0_inv_diag_.numel() == R_inner.numel()) {
                torch::NoGradGuard no_grad;
                const float dyn_rms = guarded_item<float>(res_norm_tensor);
                const float fix_rms =
                    guarded_item<float>(safe_tensor_norm(S0_inv_diag_ * R_inner)) / sqrt_N;
                const float unsc_rms =
                    guarded_item<float>(res_norm_unscaled_tensor) / sqrt_N;
                float bmax_dyn = 0.0f, bmax_s0 = 0.0f;
                const char* bmax_dyn_name = "none";
                const char* bmax_s0_name = "none";
                // PR 9F.B (metric-policy measurement, [[sdirk3-scaling-metric-separation-plan]]):
                // the block-EQUAL RMS under S0 (M4) -- sqrt(mean_b q_b^2), which weights each
                // block equally instead of by DOF count. Global RMS (M1, fix_S0_rms) weights
                // by N_b/N_total, so a small block (mu ~0.3% of DOF) is hidden; block-equal
                // and block-max (M3) both surface it. Emitting all three per iteration lets
                // the false-convergence count (M1 accepts && M3 rejects) be derived offline
                // across the dt ladder. Diagnosis-only; production still uses res_norm_tensor.
                double sum_bs2 = 0.0; int nblk = 0;
                if (layout_initialized_ &&
                    cached_layout_.total_size == R_inner.numel()) {
                    // One CPU transfer each: the per-block RMS under the dynamic S and
                    // under the frozen S0. Comparing the two isolates aggregation
                    // laundering (S0) from monotonic-S loosening (dyn vs S0).
                    auto Rs_dyn = (S_inv_diag_ * R_inner).detach().to(torch::kCPU).contiguous();
                    auto Rs_s0  = (S0_inv_diag_ * R_inner).detach().to(torch::kCPU).contiguous();
                    for (const auto& blk : cached_layout_.blocks) {
                        if (blk.size == 0 ||
                            blk.start + blk.size > Rs_dyn.numel()) continue;
                        const float inv_sqrt_nb =
                            1.0f / std::sqrt(static_cast<float>(blk.size));
                        const float bd = Rs_dyn.slice(0, blk.start, blk.start + blk.size)
                                             .norm().item<float>() * inv_sqrt_nb;
                        const float bs = Rs_s0.slice(0, blk.start, blk.start + blk.size)
                                             .norm().item<float>() * inv_sqrt_nb;
                        if (bd > bmax_dyn) { bmax_dyn = bd; bmax_dyn_name = blk.name.c_str(); }
                        if (bs > bmax_s0)  { bmax_s0  = bs; bmax_s0_name  = blk.name.c_str(); }
                        sum_bs2 += static_cast<double>(bs) * bs; ++nblk;
                    }
                }
                const float block_equal_s0 =
                    nblk > 0 ? static_cast<float>(std::sqrt(sum_bs2 / nblk)) : 0.0f;
                char sh[576];
                std::snprintf(sh, sizeof sh,
                    "SDIRK3_NEWTON_SHADOW stage=%d iter=%d dyn_S_rms=%.6e "
                    "fix_S0_rms=%.6e unscaled_rms=%.6e block_max_dyn=%.6e block_dyn=%s "
                    "block_max_S0=%.6e block_S0=%s block_equal_S0=%.6e newton_tol=%.6e\n",
                    stage, newton_iter, dyn_rms, fix_rms, unsc_rms,
                    bmax_dyn, bmax_dyn_name, bmax_s0, bmax_s0_name,
                    block_equal_s0, static_cast<double>(options_.newton_tol));
                emit_numerical_shadow_line(sh);
            } else if (numerical_shadow_enabled()) {
                // PR 9F.9.3: the shadow is ON but no frozen S0 is available (e.g. the
                // physics-scaling path, which sets S_diag_ but not S0_inv_diag_). Make
                // the SKIP explicit rather than silently emitting no record, so a missing
                // shadow reads as "unavailable here", not "measured nothing".
                char un[160];
                std::snprintf(un, sizeof un,
                    "SDIRK3_NUMERICAL_SHADOW_UNAVAILABLE stage=%d iter=%d reason=%s\n",
                    stage, newton_iter,
                    physics_scaling_set_ ? "physics_scaling_no_fixed_s0"
                                         : "no_fixed_s0");
                emit_numerical_shadow_line(un);
            }

            // v20.14r27x: Unscaled absolute explosion guard for ALL stages.
            // Scaled-RMS is ~1.0 by construction at newton_iter==0 (S built from R₀),
            // so it cannot detect explosion. Use unscaled RMS instead.
            // Threshold is case-agnostic: max(1e6, 1000 × baseline_unscaled_rms_).
            // Stage 1 records baseline; Stage 2/3 inherit it for relative comparison.
            // b_wave: baseline ≈ 1.1, threshold = 1e6. Catastrophic TS2: 7.4e7 >> 1e6.
            // Large-scale case: baseline ≈ 1e4 → threshold = 1e7, avoids false trigger.
            if (newton_iter == 0) {
                float unscaled_rms = guarded_item<float>(res_norm_unscaled_tensor) / sqrt_N;
                // v20.14r39: Store initial unscaled L2 norm for stage gate growth ratio.
                // R0_L2 = unscaled_rms * sqrt_N. Used by RESIDUAL_REEVAL gate:
                // growth = R_full_norm / R0_L2 replaces rel_R_full = R_full_norm / K_norm.
                stats_.initial_unscaled_residual = unscaled_rms * sqrt_N;
                // R13.5: the only place R0 becomes a measurement. Set both flags HERE, so a
                // solve that never reaches this line reports measured=false rather than
                // inheriting the finiteness of an uninitialised 0.0.
                stats_.initial_residual_measured = true;
                stats_.initial_residual_finite =
                    std::isfinite(stats_.initial_unscaled_residual);
                stats_.initial_residual_vector = R_inner.detach().clone();
                // v20.14r37: Reset baseline at Stage 1 entry, then conditionally update.
                // Without reset, a stale baseline from the previous timestep persists
                // if the current Stage 1 fails the health check. Reset to 0 → threshold = abs_floor.
                // Health condition: finite AND below abs_floor * rel_multiplier.
                if (stage == 1) {
                    baseline_unscaled_rms_ = 0.0f;  // reset first
                    const float abs_floor_val = wrf::sdirk3::g_sdirk3_config.explosion_abs_floor;
                    const float rel_mult_val = wrf::sdirk3::g_sdirk3_config.explosion_rel_multiplier;
                    const float baseline_max = abs_floor_val * rel_mult_val;
                    if (std::isfinite(unscaled_rms) && unscaled_rms < baseline_max) {
                        baseline_unscaled_rms_ = unscaled_rms;
                    }
                }
                // v20.14r35: Configurable explosion guard thresholds.
                // Was hardcoded 1e6/1000; now configurable for case sensitivity.
                const float abs_floor = wrf::sdirk3::g_sdirk3_config.explosion_abs_floor;
                const float rel_multiplier = wrf::sdirk3::g_sdirk3_config.explosion_rel_multiplier;
                float eff_threshold = (baseline_unscaled_rms_ > 0.0f)
                    ? std::max(abs_floor, rel_multiplier * baseline_unscaled_rms_)
                    : abs_floor;
                // IMEX explicit-first-stage fallback (mode>=2):
                // If stage-1 is explicit, baseline_unscaled_rms_ may stay zero because stage-1
                // never enters Newton. In that case, stage>=2 should not be judged by abs_floor
                // alone (too strict for ARK/ESDIRK stage states). Use a conservative floor.
                int split_mode = wrf::sdirk3::g_sdirk3_config.imex_split_mode;
                if (split_mode == 0 && wrf::sdirk3::g_sdirk3_config.imex_enabled) split_mode = 1;
                if (split_mode >= 2 && stage > 1 && baseline_unscaled_rms_ <= 0.0f) {
                    constexpr float imex_baseline_fallback_floor = 1.0e6f;
                    eff_threshold = std::max(eff_threshold, imex_baseline_fallback_floor);
                }
                // v20.14r44: Bootstrap exemption. The bootstrap predictor
                // K=M⁻¹(F(U_n)) intentionally creates a large R₀ (RMS~6e6) to break
                // hydrostatic balance. This balanced residual lets GMRES achieve
                // true_err=0.73 (vs 0.99 with K=0). The large R₀ is expected, not
                // catastrophic. Newton damp handles the overshoot.
                // NaN/Inf are still caught (truly catastrophic).
                bool truly_catastrophic = std::isinf(unscaled_rms) || std::isnan(unscaled_rms);
                bool over_threshold = unscaled_rms > eff_threshold;
                if (bootstrap_exempt_explosion_ && !truly_catastrophic) {
                    if (over_threshold && !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                        std::cerr << "\n[EXPLOSION GUARD EXEMPTED] Bootstrap R₀ RMS = " << unscaled_rms
                                  << " > threshold " << eff_threshold
                                  << " (expected: bootstrap breaks hydrostatic balance)" << std::endl;
                    }
                    bootstrap_exempt_explosion_ = false;  // one-shot: only exempt iter 0
                } else if (truly_catastrophic) {
                    if (!wrf::sdirk3::g_sdirk3_config.use_autograd) {
                        std::cerr << "\n[STAGE " << stage << " EXPLOSION GUARD] Unscaled-RMS = " << unscaled_rms
                                  << " is non-finite (baseline=" << baseline_unscaled_rms_ << ")" << std::endl;
                        std::cerr << "  Failing stage immediately to avoid publishing a non-finite state." << std::endl;
                    }
                    NewtonResult fail_result;
                    fail_result.converged = false;
                    fail_result.K = wrf::sdirk3::g_sdirk3_config.use_autograd ? K : K.detach();
                    fail_result.iterations = 1;
                    fail_result.final_residual = unscaled_rms;
                    fail_result.message = "Stage " + std::to_string(stage) +
                                          " explosion: unscaled-RMS=" +
                                          std::to_string(unscaled_rms) + " is non-finite";
                    update_stage_predictor_cache(false, fail_result.K);
                    return fail_result;
                } else if (over_threshold && !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                    std::cerr << "\n[STAGE " << stage << " EXPLOSION GUARD OBSERVED] Unscaled-RMS = " << unscaled_rms
                              << " > threshold " << eff_threshold
                              << " (baseline=" << baseline_unscaled_rms_ << ")" << std::endl;
                    std::cerr << "  Continuing: finite iter-0 magnitude is judged by the WRMS-relative stage gate after GMRES/Newton work."
                              << std::endl;
                }
            }

            // FIX (2025-12-04): Stage ||R|| threshold guard for Stage 3 explosion prevention
            // When Stage 1/2 converge loosely, Stage 3 can start with explosive residual
            // which causes GMRES to immediately overflow. Detect and fail early with dt suggestion.
            // Lowered threshold from 1e10 to 1e8 since 1e8-1e9 can already break GMRES.
            // FIX (2025-12-05): Use guarded_item and conditional detach for autograd compatibility
            //
            // AUTOGRAD LIMITATION: This guard hard-fails even in autograd mode. The graph is
            // preserved (K not detached), but the early return prevents training through failures.
            // For 4DVAR/ML applications that need to train through failed stages (e.g., learn
            // optimal dt), consider future enhancement:
            //   - Option A: Return clamped K with large penalty residual (graph-preserving)
            //   - Option B: Add differentiable "soft failure" branch with high loss
            // Current behavior: Fail immediately, return un-detached K for gradient analysis
            // v20.14r27x: Legacy scaled-RMS guard for Stage >= 2 REMOVED.
            // Scaled-RMS is ~1.0 by construction at newton_iter==0 (S from R₀),
            // making the threshold 100.0 effectively unreachable.
            // The all-stage unscaled-RMS guard (above, threshold 1e6) replaces this
            // with a metric that can actually detect explosion.

            // v20.14r26: Newton progress logging gated on debug_level >= 1
            {
                torch::NoGradGuard no_grad;
                float res_scaled = res_norm_tensor.to(torch::kCPU).item<float>();
                float res_unscaled = res_norm_unscaled_tensor.to(torch::kCPU).item<float>();
                last_res_scaled = res_scaled;  // v20.3: Store for adaptive α (float, no tensor ops)
                // Capture initial scaled residual for relative convergence criterion
                if (newton_iter == 0) {
                    init_R0_norm = res_scaled;
                    // Effective tolerance: max(atol, rtol * ||S⁻¹·R₀||)
                    float rtol_based = options_.newton_rtol * init_R0_norm;
                    if (rtol_based > newton_tol_adaptive) {
                        newton_tol_adaptive = rtol_based;
                    }
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[Newton] Convergence norm: scaled-RMS ||S⁻¹R||/√N (N="
                                  << static_cast<int64_t>(sqrt_N * sqrt_N) << ")"
                                  << ", atol=" << options_.newton_tol
                                  << ", rtol=" << options_.newton_rtol << "*" << init_R0_norm
                                  << "=" << (options_.newton_rtol * init_R0_norm)
                                  << " => effective_tol=" << newton_tol_adaptive << std::endl;
                    }
                }
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[Newton] iter " << newton_iter
                             << ": ||S⁻¹R||_rms=" << std::scientific << res_scaled << " [scaled-RMS]"
                             << ", ||R||=" << res_unscaled << " [unscaled-L2]"
                             << ", tol=" << newton_tol_adaptive
                             << std::defaultfloat << std::endl;
                }
                // PR 8: per-iteration nonlinear-residual record (opt-in).
                if (stage_diag_enabled()) {
                    emit_stage_diag([&](std::ostream& os) {
                    os << "SDIRK3_NEWTON_DIAG ts=" << global_timestep_
                              << " stage=" << stage
                              << " iter=" << newton_iter
                              << " event=residual"
                              << std::scientific
                              << " res_scaled_rms=" << res_scaled
                              << " res_l2=" << res_unscaled
                              << " tol=" << newton_tol_adaptive
                              << " r0=" << init_R0_norm
                              << std::defaultfloat
                              << " state_finite=" << (diag_all_finite(K) ? 1 : 0)
                              << " rhs_finite=" << (diag_all_finite(F) ? 1 : 0)
                              << "\n";
                    });
                }

                // Per-block scaled residual diagnostic (debug_level >= 1, throttled)
                // Print at iter 0, every 5 iters, and at convergence/failure to limit log volume
                bool print_block_diag = (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                    scaling_initialized_ && layout_initialized_ &&
                    (newton_iter == 0 || newton_iter % 5 == 0));
                // Also print at debug_level >= 2 every iter for detailed analysis
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) print_block_diag = true;
                if (print_block_diag) {
                    auto R_cpu = R_inner.detach().to(torch::kCPU).contiguous();
                    auto Sinv_cpu = S_inv_diag_.to(torch::kCPU);
                    for (const auto& blk : cached_layout_.blocks) {
                        auto blk_R = R_cpu.slice(0, blk.start, blk.start + blk.size);
                        auto blk_Sinv = Sinv_cpu.slice(0, blk.start, blk.start + blk.size);
                        auto blk_scaled = blk_Sinv * blk_R;
                        float blk_rms = blk_scaled.norm().item<float>()
                                        / std::sqrt(static_cast<float>(blk.size));
                        std::cerr << "  " << blk.name << ": ||S⁻¹R||_rms=" << blk_rms
                                  << " (N=" << blk.size << ")" << std::endl;
                    }
                }
            }
            
            // AUTOGRAD FIX: Debug check for NaN in F without (unknown > 0).any().item<bool>() /* AUTOGRAD FIX: Tensor comparison */ calls
            auto f_has_nan = torch::any(torch::isnan(F));
            auto f_has_inf = torch::any(torch::isinf(F));
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3) {
                torch::NoGradGuard no_grad;  // FIX 2025-12-27: Guard for diagnostic block
                auto any_issue = (f_has_nan | f_has_inf);
                // FIX 2025-12-27: Add .to(kCPU) before .item<bool>() to avoid GPU sync
                if (any_issue.any().to(torch::kCPU).item<bool>()) {
                    // Pre-copy to CPU for all diagnostics
                    auto K_cpu = K.to(torch::kCPU);
                    auto U_eval_cpu = U_eval.to(torch::kCPU);
                    auto U_stage_cpu = U_stage.to(torch::kCPU);
                    std::cerr << "ERROR: NaN/Inf detected in F (RHS) at Newton iter " << newton_iter << std::endl;
                    std::cerr << "  F has NaN: " << f_has_nan << std::endl;
                    std::cerr << "  F has Inf: " << f_has_inf << std::endl;
                    std::cerr << "  K norm: " << K_cpu.norm().item<float>() << std::endl;
                    std::cerr << "  U_eval norm: " << U_eval_cpu.norm().item<float>() << std::endl;
                    std::cerr << "  U_stage norm: " << U_stage_cpu.norm().item<float>() << std::endl;
                }
            }
            
            // PHASE 1D FIX: Only extract scalar when needed for Eisenstat-Walker or stats collection
            // This eliminates 5-10 syncs/timestep at debug_level=0 when adaptive tolerances are disabled
            // STATS FIX: Track detached tensor for accurate final_residual at debug_level=0
            float res_norm_for_stats = 0.0f;
            bool need_scalar = options_.use_adaptive_tolerances || (wrf::sdirk3::g_sdirk3_config.debug_level >= 1);

            if (need_scalar) {
                // GRADIENT FIX: Use guarded_item to prevent gradient breaks
                res_norm_for_stats = guarded_item<float>(res_norm_tensor);
                // Always push at least one value for failure path .back() call
                stats_.newton_residuals.push_back(res_norm_for_stats);
                // Track for failure path
                last_res_norm = res_norm_for_stats;
            } else {
                // STATS FIX: For debug_level=0 without adaptive tolerances,
                // track detached tensor and defer .item() call to exit
                // This avoids sync in inner loop while maintaining accurate stats
                res_norm_detached = res_norm_tensor.detach();
            }

            // Eisenstat-Walker adaptive forcing term
            if (options_.use_adaptive_tolerances) {
                auto& cfg = wrf::sdirk3::g_sdirk3_config;
                float ew_eta_min_stage = options_.ew_eta_min;
                float ew_eta_max_stage = options_.ew_eta_max;
                if (stage >= 2) {
                    if (cfg.stage2_ew_eta_min > 0.0f) ew_eta_min_stage = cfg.stage2_ew_eta_min;
                    if (cfg.stage2_ew_eta_max > 0.0f) ew_eta_max_stage = cfg.stage2_ew_eta_max;
                }
                if (stage >= 3) {
                    if (cfg.stage3_ew_eta_min > 0.0f) ew_eta_min_stage = cfg.stage3_ew_eta_min;
                    if (cfg.stage3_ew_eta_max > 0.0f) ew_eta_max_stage = cfg.stage3_ew_eta_max;
                }
                if (ew_eta_max_stage < ew_eta_min_stage) std::swap(ew_eta_min_stage, ew_eta_max_stage);

                if (newton_iter > 0 && ew_prev_res_norm_ > 1e-12f) {
                    // eta_k = gamma * (||R_k|| / ||R_{k-1}||)^alpha
                    float res_ratio = res_norm_for_stats / ew_prev_res_norm_;

                    // Compute adaptive forcing term using configurable parameters
                    float eta_k = options_.ew_gamma * std::pow(res_ratio, options_.ew_alpha);

                    // GR v9 G3: Choice 2 safeguard — prevent eta from tightening
                    // faster than previous value (avoids oscillation when residual
                    // improvement is non-monotonic, especially in S2)
                    if (ew_prev_eta_ > 0.0f) {
                        float eta_floor = options_.ew_gamma * std::pow(ew_prev_eta_, options_.ew_alpha);
                        eta_k = std::max(eta_k, eta_floor);
                    }

                    // Cap using stage-aware eta_min and eta_max.
                    // R13.20 (round 9, R9-5): remember whether the cap BOUND. When it does, the
                    // number in effect is ew_eta_max/min, not gamma*(ratio)^alpha, and a
                    // tolerance-limited verdict must send work to that knob and not to the
                    // forcing-term parameters. eta_max saturating in the failing regime is the
                    // documented operational case, not a corner.
                    const float eta_k_pre_clamp = eta_k;
                    eta_k = std::max(ew_eta_min_stage, std::min(ew_eta_max_stage, eta_k));

                    krylov_tol_adaptive = eta_k;
                    ew_tol_source = (eta_k != eta_k_pre_clamp)
                                        ? wrf::sdirk3::KrylovToleranceSource::EwEtaClamp
                                        : wrf::sdirk3::KrylovToleranceSource::EisenstatWalker;
                    ew_prev_eta_ = eta_k;
                    ew_eta_used_this_iter = eta_k;
                    ew_eta_updated_this_iter = true;

                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[EISENSTAT-WALKER] Newton iter " << newton_iter << std::endl;
                        std::cerr << "  ||R_k|| = " << res_norm_for_stats << std::endl;
                        std::cerr << "  ||R_{k-1}|| = " << ew_prev_res_norm_ << std::endl;
                        std::cerr << "  Residual ratio = " << res_ratio << std::endl;
                        std::cerr << "  gamma=" << options_.ew_gamma << ", alpha=" << options_.ew_alpha << std::endl;
                        std::cerr << "  Adaptive eta_k = " << eta_k << " (range: "
                                  << ew_eta_min_stage << " to " << ew_eta_max_stage << ")" << std::endl;
                    }
                } else {
                    // FIX 2026-02-05: Start with relaxed eta that the preconditioner
                    // can actually achieve.  The 1D TDMA preconditioner typically
                    // reaches true_err ≈ 0.39 (preconditioner spectral limit from
                    // missing U-Φ PGF coupling). Setting eta below this wastes
                    // ~30% of Arnoldi budget on unreachable tolerance.
                    // v20.14r50-fix2: Raised from 0.25 to 0.40 (just above 0.39 floor).
                    // Configurable via env var for tuning.
                    {
                        static const float ew_eta_initial = []() {
                            const char* v = std::getenv("WRF_SDIRK3_EW_ETA_INITIAL");
                            return v ? std::atof(v) : 0.40;
                        }();
                        // R13.20 (round 9, R9-5): no residual ratio is read on this arm --
                        // `ew_eta_updated_this_iter = false` three lines below is the code's own
                        // statement that E-W updated nothing. The value is one of three
                        // constants, and the receipt must name the one that bound, or a
                        // tolerance-limited verdict here routes a week of work to ew_gamma /
                        // ew_alpha, neither of which was read.
                        const float base_tol_this = static_cast<float>(options_.krylov_tol);
                        const float floored_this  = std::max(base_tol_this, ew_eta_initial);
                        krylov_tol_adaptive = std::max(ew_eta_min_stage,
                                                       std::min(ew_eta_max_stage, floored_this));
                        ew_tol_source =
                            (krylov_tol_adaptive != floored_this)
                                ? wrf::sdirk3::KrylovToleranceSource::EwEtaClamp
                                : (floored_this > base_tol_this
                                       ? wrf::sdirk3::KrylovToleranceSource::EwInitialFloor
                                       : wrf::sdirk3::KrylovToleranceSource::Base);
                    }
                    ew_eta_used_this_iter = krylov_tol_adaptive;
                    ew_eta_updated_this_iter = false;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[EISENSTAT-WALKER] Newton iter 0, Stage " << stage
                                  << ": using initial eta = " << krylov_tol_adaptive << std::endl;
                    }
                }

                // Update for next iteration
                ew_prev_res_norm_ = res_norm_for_stats;
                if (ew_prev_eta_ <= 0.0f) {
                    ew_prev_eta_ = krylov_tol_adaptive;  // seed from iter 0
                }
            }

            // Enhanced debugging output using tensor comparisons
            auto is_large = res_norm_tensor > 1e10f;
            auto is_nan = torch::isnan(res_norm_tensor);

            // GRADIENT FIX: Use guarded_item for control flow decisions
            if (options_.verbose || guarded_item<bool>(is_large.any()) || guarded_item<bool>(is_nan.any())) {
                DEBUG_PRINT("  Newton iter " << newton_iter
                         << ": residual = " << std::scientific << std::setprecision(6)
                         << res_norm_tensor << std::defaultfloat);

                // Check for numerical issues
                if (guarded_item<bool>(is_nan.any())) {
                    ERROR_PRINT("ERROR: NaN detected in Newton residual!");
                    stats_.converged = false;
                    stats_.final_residual = res_norm_for_stats;
                    stats_.final_residual_measured = true;

                    WRFNewtonKrylovSolver::NewtonResult result;
                    result.K = K;  // Return current K to avoid propagating NaN
                    result.converged = false;
                    result.iterations = newton_iter + 1;
                    result.final_residual = res_norm_for_stats;
                    result.message = "ERROR: NaN detected in Newton residual";
                    update_stage_predictor_cache(false, result.K);
                    return result;
                }
                if (guarded_item<bool>(is_large.any())) {
                    ERROR_PRINT("WARNING: Very large residual detected!");
                }
            }

            // Block-relative residual: max over blocks of ||R_blk|| / max(||K_blk||, 1)
            // This ensures no single variable block has large relative error,
            // even if the global norm is dominated by one large block.
            float max_block_rel = 0.0f;
            float ru_share = 0.0f;  // v20.14r44c: fraction of residual energy in ru block
            if (layout_initialized_ && cached_layout_.total_size == K.numel()) {
                torch::NoGradGuard no_grad;
                auto R_inner_cpu = R_inner.detach().to(torch::kCPU);
                auto K_cpu = K.detach().to(torch::kCPU);
                std::ostringstream block_log;
                block_log << "[BLOCK_RES] iter=" << newton_iter;
                float ru_norm_sq = 0.0f;
                float total_norm_sq = 0.0f;
                for (const auto& blk : cached_layout_.blocks) {
                    if (blk.start + blk.size <= R_inner_cpu.numel()) {
                        auto R_blk = R_inner_cpu.slice(0, blk.start, blk.start + blk.size);
                        auto K_blk = K_cpu.slice(0, blk.start, blk.start + blk.size);
                        float R_n = R_blk.norm().item<float>();
                        float K_n = K_blk.norm().item<float>();
                        float blk_rel = R_n / std::max(K_n, 1.0f);
                        max_block_rel = std::max(max_block_rel, blk_rel);
                        block_log << " " << blk.name << "=" << std::scientific
                                  << std::setprecision(2) << blk_rel;
                        // v20.14r44c: Accumulate for ru_share computation
                        total_norm_sq += R_n * R_n;
                        if (blk.name == "ru") {
                            ru_norm_sq = R_n * R_n;
                        }
                    }
                }
                ru_share = (total_norm_sq > 1e-30f) ? (ru_norm_sq / total_norm_sq) : 0.0f;
                last_ru_share_ = ru_share;  // v20.14 r49
                block_log << " max=" << std::scientific << std::setprecision(2) << max_block_rel
                          << " ru_share=" << std::fixed << std::setprecision(3) << ru_share
                          << std::defaultfloat;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << block_log.str() << std::endl;
                }

                // v20.14 r47c-fix3: Per-level ru decomposition when ru-dominated.
                // Shows which k-levels carry the residual (interior vs boundary diagnosis).
                if (ru_share > 0.9f && wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                    options_.nx_u > 0 && options_.ny > 0 && options_.nz > 0 &&
                    (newton_iter == 0 || newton_iter >= 3)) {  // N=0 baseline + N3+ collapse tracking
                    for (const auto& blk : cached_layout_.blocks) {
                        if (blk.name == "ru" && blk.start + blk.size <= R_inner_cpu.numel()) {
                            auto R_ru = R_inner_cpu.slice(0, blk.start, blk.start + blk.size);
                            int ny = options_.ny, nz = options_.nz, nx_u = options_.nx_u;
                            if (static_cast<int64_t>(ny) * nz * nx_u == blk.size) {
                                auto R_3d = R_ru.reshape({ny, nz, nx_u});
                                // Per-level norm: ||ru[k]|| for each k
                                std::ostringstream klvl;
                                klvl << "[RU_PROFILE] N=" << newton_iter << " nz=" << nz << " |ru[k]|:";
                                // Find peak across ALL levels (not just displayed ones)
                                float max_k_norm = 0.0f;
                                int max_k = 0;
                                for (int k = 0; k < nz; ++k) {
                                    float kn = R_3d.select(1, k).norm().item<float>();
                                    if (kn > max_k_norm) { max_k_norm = kn; max_k = k; }
                                }
                                // Display first 10 levels
                                for (int k = 0; k < std::min(nz, 10); ++k) {
                                    float kn = R_3d.select(1, k).norm().item<float>();
                                    klvl << " " << std::scientific << std::setprecision(1) << kn;
                                }
                                if (nz > 10) klvl << " ...";
                                // Interior vs boundary: k=0,nz-1 are boundaries
                                float bdy_sq = 0.0f;
                                bdy_sq += std::pow(R_3d.select(1, 0).norm().item<float>(), 2);
                                bdy_sq += std::pow(R_3d.select(1, nz - 1).norm().item<float>(), 2);
                                float total_sq = R_ru.norm().item<float>();
                                total_sq *= total_sq;
                                float bdy_frac = (total_sq > 1e-30f) ? bdy_sq / total_sq : 0.0f;
                                klvl << " peak=k" << max_k
                                     << " bdy_frac=" << std::fixed << std::setprecision(3) << bdy_frac;
                                std::cerr << klvl.str() << std::endl;
                            }
                            break;
                        }
                    }
                }
            }

            // FIX 2026-02-03: Use SCALED convergence criterion ||S⁻¹·R|| < tol
            // Previous unscaled ||R||₂ was dominated by rw (large magnitude), hiding
            // mu/ph errors. Stage 2 explosion (||R₀||=1.75e8) proved unscaled norm
            // missed physically significant errors in small-scale variables.
            // Scaled norm ensures all variable blocks contribute to convergence check,
            // consistent with GMRES and trust region norms.
            auto converged = res_norm_tensor < newton_tol_adaptive;
            // PR 9F.B (B4 experiment): env-gated block-max criterion. Default OFF -> the &&
            // is exactly the legacy gate (block_max_ok stays true), so byte-identical. When
            // ON, ALSO require every block's scaled RMS < tol, so a stage cannot "converge"
            // on a small-DOF block (mu) hidden by the size-weighted global RMS.
            bool block_max_ok = true;
            if (blockmax_gate_enabled() && scaling_initialized_ &&
                R_scaled_metric.defined() && layout_initialized_ &&
                cached_layout_.total_size == R_inner.numel()) {
                torch::NoGradGuard no_grad;
                // Reuse the scaled metric residual computed for res_norm_tensor (Gemini #65):
                // metric_scale_inv() and R_inner are unchanged since, so this is identical.
                auto Rs = R_scaled_metric.detach().to(torch::kCPU).contiguous();
                float bmax = 0.0f;
                for (const auto& blk : cached_layout_.blocks) {
                    if (blk.size == 0 || blk.start + blk.size > Rs.numel()) continue;
                    const float q = Rs.slice(0, blk.start, blk.start + blk.size)
                                        .norm().item<float>()
                                    / std::sqrt(static_cast<float>(blk.size));
                    if (q > bmax) bmax = q;
                }
                block_max_ok = (bmax < newton_tol_adaptive);
            }
            // GRADIENT FIX: Use guarded_item for convergence check
            if (guarded_item<bool>(converged.all()) && block_max_ok) {
                if (stage == 2 && stage2_hopeless_budget_mode_) {
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES HOPLESS MODE] Cleared by successful Stage-2 Newton solve.\n";
                    }
                }
                if (stage == 2) {
                    stage2_hopeless_budget_mode_ = false;
                    stage2_hopeless_streak_ = 0;
                }
                if (stage >= 3 && stage3_hopeless_budget_mode_) {
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES HOPLESS MODE] Cleared by successful Stage-3 Newton solve.\n";
                    }
                }
                if (stage >= 3) {
                    stage3_hopeless_budget_mode_ = false;
                    stage3_hopeless_streak_ = 0;
                }
                stats_.newton_iterations = newton_iter + 1;
                stats_.converged = true;
                // R13.17 (external review P0-3): recorded HERE, by the site that ended the loop.
                stats_.newton_termination =
                    static_cast<int>(wrf::sdirk3::NewtonTerminationReason::Converged);

                // STATS FIX: Extract final residual accurately
                if (need_scalar) {
                    stats_.final_residual = res_norm_for_stats;
                    stats_.final_residual_measured = true;
                } else {
                    // Fast-path: single .item() on exit only (debug_level=0 without adaptive)
                    // GRADIENT FIX: Use guarded_item instead of manual NoGradGuard
                    stats_.final_residual = guarded_item<float>(res_norm_detached);
                    stats_.final_residual_measured = true;
                }
                
                // Always print convergence (scaled norm is the convergence criterion)
                {
                    torch::NoGradGuard no_grad;
                    float unscaled_val = res_norm_unscaled_tensor.to(torch::kCPU).item<float>();
                    std::cerr << "[Newton] CONVERGED stage=" << stage
                             << " iters=" << stats_.newton_iterations
                             << " ||S⁻¹R||_rms=" << std::scientific << stats_.final_residual
                             << " [scaled-RMS] (||R||=" << unscaled_val << " [unscaled-L2])"
                             << std::defaultfloat << std::endl;
                }
                
                // Save checkpoint for 4DVAR if needed.
                maybe_save_checkpoint(U_n, stage);
                
                // Return successful result
                WRFNewtonKrylovSolver::NewtonResult result;
                result.K = K;
                result.converged = true;
                result.iterations = stats_.newton_iterations;
                result.final_residual = res_norm_for_stats;
                result.message = "Newton solver converged successfully";
                update_stage_predictor_cache(true, result.K);
                return result;
            }
            
            // NOTE: Central difference stencil requires 2 RHS evaluations per JVP
            // Cannot cache F_base like forward difference (which needed only 1 eval)
            // Trade-off: 2x RHS cost for O(ε²) vs O(ε) truncation error

            // ========================================
            // JVP METHOD SELECTION
            // ========================================
            // FIX (2025-12-05): Removed dead "graph reuse" prep block.
            // Previously built U_jvp_var/F_jvp_output here, but compute_jvp_forward_mode()
            // builds its own graph internally and doesn't use them. This was one wasted
            // compute_rhs() call per Newton iteration.
            // Now we simply log the JVP method and let apply_jacobian handle it.
            // ========================================
            // PR 9A/9B: capture buffer for the opt-in directional consistency
            // check. Declared BEFORE apply_jacobian so the matvec can attribute
            // its in-situ JVP capture (J_w) when solve_fgmres arms
            // arnoldi_call_active. Null-routed unless the checker is enabled
            // AND this is one of its checkpoints — zero production cost.
            KrylovBasisCapture jvp_check_basis;
            const bool jvp_check_this_iter =
                stage4_jvp_check_enabled() &&
                ((stage == 4 && newton_iter <= 1) ||
                 (stage == 3 && newton_iter == 0));

            int jvp_call_count = 0;
            int jvp_ad_calls = 0;
            int jvp_fd_calls = 0;
            int jvp_fd_forward_calls = 0;
            int jvp_fd_central_calls = 0;
            int jvp_eps_auto_calls = 0;
            int jvp_eps_manual_calls = 0;
            int jvp_eps_sample_count = 0;
            double jvp_eps_sum = 0.0;
            float jvp_eps_min = std::numeric_limits<float>::infinity();
            float jvp_eps_max = 0.0f;
            double total_jvp_time_ms = 0.0;
            // Snapshot hot-path JVP config once per Newton iteration.
            // This avoids repeated global config reads inside apply_jacobian().
            const int debug_level_local = wrf::sdirk3::g_sdirk3_config.debug_level;
            const bool use_autograd_jvp = wrf::sdirk3::g_sdirk3_config.use_autograd;
            const bool jvp_use_forward_diff_cfg = wrf::sdirk3::g_sdirk3_config.jvp_use_forward_diff;
            const int jvp_mixed_fd_switch_cfg = wrf::sdirk3::g_sdirk3_config.jvp_mixed_fd_newton_switch;
            const float jvp_epsilon_cfg = wrf::sdirk3::g_sdirk3_config.jvp_epsilon;
            const bool jvp_block_epsilon_cfg = wrf::sdirk3::g_sdirk3_config.jvp_block_epsilon;
            const int fd_consistency_samples_cfg = wrf::sdirk3::g_sdirk3_config.fd_consistency_samples;

            // FIX (2025-12-05): Gate JVP method logging by debug_level >= 1
            if (debug_level_local >= 1) {
                if (use_autograd_jvp) {
                    std::cerr << "[Newton] JVP method: FORWARD-MODE AD (compute_jvp_forward_mode)\n";
                } else {
                    int mixed_sw = jvp_mixed_fd_switch_cfg;
                    if (mixed_sw != 0) {
                        bool is_fwd = (mixed_sw < 0) || (newton_iter < mixed_sw);
                        std::cerr << "[Newton] JVP method: FINITE DIFFERENCE ("
                                  << (is_fwd ? "forward" : "central")
                                  << " diff, mixed_fd_switch=" << mixed_sw
                                  << ", N=" << newton_iter << ")\n";
                    } else {
                        std::cerr << "[Newton] JVP method: FINITE DIFFERENCE ("
                                  << (jvp_use_forward_diff_cfg ? "forward" : "central")
                                  << " diff)\n";
                    }
                }
            }

            // Set up linear system: (I - dt*gamma*J) dK = -R
            // where J is Jacobian of F
            auto apply_jacobian = [&](const torch::Tensor& dK) -> torch::Tensor {
                jvp_call_count++;
                auto jvp_start = std::chrono::high_resolution_clock::now();

                // JVP: J(U_eval) * (dt*gamma*dK)
                auto v = dt * gamma * dK;

                // Check JVP method
                torch::Tensor jvp_result;

                // ========================================
                // OPTIMIZED PATH: Forward-mode AD (true JVP)
                // ========================================
                // NOTE: Previous implementation used torch::autograd::grad() which
                // computes VJP (Jᵀ·v), NOT JVP (J·v). This caused GMRES to diverge
                // with ||r||=inf after 1-2 iterations. Forward-mode AD correctly
                // computes J·v using dual numbers.
                // FIX (2025-12-05): Simplified condition - just check use_autograd flag.
                // Removed dead jvp_graph_prepared variable.
                // ========================================
                if (use_autograd_jvp) {
                    jvp_ad_calls++;
                    auto grad_start = std::chrono::high_resolution_clock::now();

                    // Use forward-mode AD for true JVP (J·v)
                    jvp_result = compute_jvp_forward_mode(
                        /*F=*/[&](const torch::Tensor& x) { return compute_rhs(x); },
                        /*u=*/U_eval,
                        /*v=*/v
                    );

                    auto grad_end = std::chrono::high_resolution_clock::now();
                    auto grad_duration = std::chrono::duration_cast<std::chrono::milliseconds>(grad_end - grad_start);

                    if (debug_level_local >= 1 && jvp_call_count <= 3) {
                        std::cerr << "[JVP FORWARD] Call #" << jvp_call_count
                                  << ": forward-mode AD took " << grad_duration.count() << " ms" << std::endl;
                    }
                }
                // ========================================
                // FALLBACK PATH: Finite difference (no autograd)
                // ========================================
                else {
                    jvp_fd_calls++;
                    // Use finite difference for JVP
                    auto rhs_func = can_reuse_jacobian && jacobian_cache_.cached_rhs ?
                                   jacobian_cache_.cached_rhs : compute_rhs;

                    // v20.14r50-fix2: Dennis-Schnabel auto-epsilon for FD-JVP.
                    // Replaces fixed jvp_epsilon with adaptive selection:
                    //   eps = cbrt(eps_mach) * max(1, ||U||) / max(||v||, floor)
                    // After v-normalization (v_scaled = eps * v/||v||):
                    //   eps_scaled = cbrt(eps_mach) * max(1, ||U||)  [||v|| cancels]
                    // Per-block refinement: maximize eps across blocks so ALL blocks
                    // get resolvable perturbation relative to their own state scale.
                    float v_norm, U_norm;
                    {
                        torch::NoGradGuard no_grad;
                        v_norm = v.norm().to(torch::kCPU).item<float>();
                        U_norm = U_eval.norm().to(torch::kCPU).item<float>();
                    }

                    constexpr float v_norm_min = 1e-12f;
                    if (v_norm < v_norm_min) {
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                            std::cerr << "[JVP FD] v_norm=" << v_norm
                                      << " < " << v_norm_min << ", returning zero JVP" << std::endl;
                        }
                        jvp_result = torch::zeros_like(v);
                    } else {

                    // Dennis-Schnabel auto-epsilon:
                    //   Central diff: eps = cbrt(eps_mach) * max(1, ||U||)
                    //   Forward diff: eps = sqrt(eps_mach) * max(1, ||U||)
                    // cbrt gives O(eps_mach^{2/3}) total error for central (optimal).
                    // sqrt gives O(eps_mach^{1/2}) total error for forward (optimal).
                    // If jvp_epsilon is explicitly set via env var, use it as override.
                    constexpr float cbrt_eps = 4.93e-3f;   // cbrt(eps_mach_f32≈1.19e-7), optimal for central FD
                    constexpr float sqrt_eps = 3.45e-4f;  // sqrt(eps_mach_f32), optimal for forward FD
                    constexpr float eps_clamp_lo = 1e-4f;
                    constexpr float eps_clamp_hi = 1e4f;

                    // GR v8 F3: Forward diff uses sqrt(eps_mach) [14x smaller], central uses cbrt(eps_mach)
                    bool likely_forward = (jvp_locked_mode_ == 0) ||
                                          jvp_use_forward_diff_cfg;
                    float fd_base_eps = likely_forward ? sqrt_eps : cbrt_eps;

                    float eps_scaled;
                    float eps_base = jvp_epsilon_cfg;
                    bool use_auto = (eps_base <= 0.0f || eps_base == 1e-4f);  // auto if default

                    if (use_auto) {
                        // Auto-select based on diff mode and state scale
                        float scale_factor = std::max(1.0f, U_norm);
                        // Per-block refinement: find max eps needed across blocks
                        if (layout_initialized_ && cached_layout_.blocks.size() >= 2) {
                            float max_eps_needed = fd_base_eps * scale_factor;
                            std::vector<const StateLayout::Block*> eps_blocks;
                            std::vector<torch::Tensor> u_blk_norm_terms;
                            std::vector<torch::Tensor> v_blk_norm_terms;
                            eps_blocks.reserve(cached_layout_.blocks.size());
                            u_blk_norm_terms.reserve(cached_layout_.blocks.size());
                            v_blk_norm_terms.reserve(cached_layout_.blocks.size());
                            for (const auto& blk : cached_layout_.blocks) {
                                if (blk.start + blk.size > U_eval.numel()) continue;
                                eps_blocks.push_back(&blk);
                                u_blk_norm_terms.push_back(
                                    U_eval.slice(0, blk.start, blk.start + blk.size).norm());
                                v_blk_norm_terms.push_back(
                                    v.slice(0, blk.start, blk.start + blk.size).norm());
                            }
                            if (!eps_blocks.empty()) {
                                torch::Tensor u_blk_norm_cpu;
                                torch::Tensor v_blk_norm_cpu;
                                {
                                    torch::NoGradGuard no_grad;
                                    u_blk_norm_cpu = torch::stack(u_blk_norm_terms).to(torch::kCPU);
                                    v_blk_norm_cpu = torch::stack(v_blk_norm_terms).to(torch::kCPU);
                                }
                                for (size_t bi = 0; bi < eps_blocks.size(); ++bi) {
                                    const auto& blk = *eps_blocks[bi];
                                    // R13.20 (hard-constraint audit): the tensors were built
                                    // under a guard, but the EXTRACTION was not. The rule is
                                    // unconditional and `guarded_item` is the established form.
                                    float U_blk_norm =
                                        guarded_item<float>(u_blk_norm_cpu[static_cast<long>(bi)]);
                                    float v_blk_norm =
                                        guarded_item<float>(v_blk_norm_cpu[static_cast<long>(bi)]);
                                    if (v_blk_norm < 1e-30f) continue;
                                    // Per-block: eps must resolve U_blk_rms for this block's v share
                                    float sqrt_N = std::sqrt(static_cast<float>(blk.size));
                                    float U_blk_rms = U_blk_norm / sqrt_N;
                                    float needed = fd_base_eps * std::max(1.0f, U_blk_rms)
                                                 * v_norm / v_blk_norm;
                                    max_eps_needed = std::max(max_eps_needed, needed);
                                }
                            }
                            eps_scaled = std::clamp(max_eps_needed, eps_clamp_lo, eps_clamp_hi);
                        } else {
                            eps_scaled = std::clamp(fd_base_eps * scale_factor, eps_clamp_lo, eps_clamp_hi);
                        }
                        if (debug_level_local >= 2 && jvp_call_count <= 3) {
                            std::cerr << "[JVP FD AUTO-EPS] base_eps*||U||="
                                      << fd_base_eps * scale_factor
                                      << " -> eps_scaled=" << eps_scaled << std::endl;
                        }
                        ++jvp_eps_auto_calls;
                    } else {
                        // Manual override: use user-specified jvp_epsilon with state scaling
                        float state_scale = std::max(1.0f, U_norm);
                        eps_scaled = std::clamp(eps_base * state_scale, eps_clamp_lo, eps_clamp_hi);
                        ++jvp_eps_manual_calls;
                    }
                    jvp_eps_sum += static_cast<double>(eps_scaled);
                    ++jvp_eps_sample_count;
                    jvp_eps_min = std::min(jvp_eps_min, eps_scaled);
                    jvp_eps_max = std::max(jvp_eps_max, eps_scaled);

                    // v20.14r27q: Block-aware epsilon scaling (EXPERIMENTAL).
                    // When enabled, scales perturbation per block so each block
                    // gets eps proportional to its own norm (not global ||v||).
                    // WARNING (v20.14r27s): This computes J*delta (block-rescaled direction)
                    // instead of J*v (original Krylov direction). GMRES correctness is NOT
                    // guaranteed. Default off; enable only for diagnostic experiments.
                    torch::Tensor v_scaled;
                    float inv_eps;  // = 1/eps for result division
                    if (jvp_block_epsilon_cfg &&
                        layout_initialized_ && cached_layout_.blocks.size() >= 4) {
                        // Build per-block scaled v: v_block / max(1, ||v_block||) * eps
                        v_scaled = torch::zeros_like(v);
                        std::vector<const StateLayout::Block*> blk_refs;
                        std::vector<torch::Tensor> v_blk_norm_terms;
                        blk_refs.reserve(cached_layout_.blocks.size());
                        v_blk_norm_terms.reserve(cached_layout_.blocks.size());
                        for (const auto& blk : cached_layout_.blocks) {
                            if (blk.start + blk.size > v.numel()) continue;
                            blk_refs.push_back(&blk);
                            v_blk_norm_terms.push_back(v.slice(0, blk.start, blk.start + blk.size).norm());
                        }
                        torch::Tensor v_blk_norm_cpu;
                        if (!blk_refs.empty()) {
                            torch::NoGradGuard no_grad;
                            v_blk_norm_cpu = torch::stack(v_blk_norm_terms).to(torch::kCPU);
                        }
                        for (size_t bi = 0; bi < blk_refs.size(); ++bi) {
                            const auto& blk = *blk_refs[bi];
                            auto v_blk = v.slice(0, blk.start, blk.start + blk.size);
                            // R13.20 (hard-constraint audit): guarded extraction, same reason.
                            float blk_norm =
                                guarded_item<float>(v_blk_norm_cpu[static_cast<long>(bi)]);
                            float blk_scale = fd_base_eps * std::max(1.0f, blk_norm);
                            blk_scale = std::min(blk_scale, eps_clamp_hi);
                            v_scaled.slice(0, blk.start, blk.start + blk.size)
                                .copy_(v_blk * (blk_scale / std::max(blk_norm, 1e-30f)));
                        }
                        inv_eps = 1.0f;  // v_scaled already includes eps scaling
                    } else {
                        v_scaled = eps_scaled * v / v_norm;
                        inv_eps = v_norm / eps_scaled;
                    }

                    // Finite difference JVP: forward or central
                    // v20.14r48: Mixed FD strategy — forward diff for early Newton iters,
                    // central diff near convergence. Halves JVP cost for initial iterations.
                    // mixed_fd_newton_switch=0: disabled (use jvp_use_forward_diff as-is).
                    // mixed_fd_newton_switch=-1: always forward. mixed_fd_newton_switch=N: forward when newton_iter<N.
                    // v20.14 r49: JVP auto-bench locked mode takes priority
                    bool use_forward_this_call;
                    if (jvp_locked_mode_ >= 0) {
                        use_forward_this_call = (jvp_locked_mode_ == 0);
                    } else {
                        use_forward_this_call = jvp_use_forward_diff_cfg;
                        int mixed_switch = jvp_mixed_fd_switch_cfg;
                        if (mixed_switch != 0) {
                            use_forward_this_call = (mixed_switch < 0) || (newton_iter < mixed_switch);
                        }
                    }
                    if (use_forward_this_call) {
                        ++jvp_fd_forward_calls;
                    } else {
                        ++jvp_fd_central_calls;
                    }
                    torch::Tensor F_plus, F_minus;
                    {
                        torch::NoGradGuard no_grad;
                        if (use_forward_this_call) {
                            F_plus = rhs_func(U_eval + v_scaled);
                            jvp_result = (F_plus - F) * inv_eps;

                            if (jvp_call_count <= 3) {
                                float F_n = F.norm().to(torch::kCPU).item<float>();
                                float Fp_n = F_plus.norm().to(torch::kCPU).item<float>();
                                float diff_n = (F_plus - F).norm().to(torch::kCPU).item<float>();
                                float jvp_n = jvp_result.norm().to(torch::kCPU).item<float>();
                                std::cerr << "[FD DIAG] call #" << jvp_call_count
                                          << ": eps=" << eps_scaled
                                          << ", ||F||=" << F_n
                                          << ", ||F+||=" << Fp_n
                                          << ", ||F+-F||=" << diff_n
                                          << ", ||J*v||=" << jvp_n
                                          << std::endl;
                            }
                        } else {
                            F_plus = rhs_func(U_eval + v_scaled);
                            F_minus = rhs_func(U_eval - v_scaled);
                            jvp_result = (F_plus - F_minus) * (0.5f * inv_eps);
                        }
                    }

                    // v20.14r36: eps vs eps/2 consistency check matching main JVP path.
                    // Uses same diff mode (forward/central) as main computation.
                    // Block-epsilon mode is skipped (experimental, too complex to replicate).
                    // Cost: 2 extra RHS evals (forward) or 4 (central) per checked call.
                    // Restricted to Newton iter 0 to avoid repeated per-iter overhead.
                    // Set samples=0 to fully disable.
                    if (debug_level_local >= 1 &&
                        jvp_block_epsilon_cfg &&
                        jvp_call_count == 1 && newton_iter == 0) {
                        // v20.14r37: True one-shot warning (newton_iter==0 && call#1).
                        // jvp_call_count resets per Newton iter, so without newton_iter
                        // gate this would fire once per iter, not once per run.
                        std::cerr << "[FD CONSISTENCY] SKIPPED (block-epsilon mode active)"
                                  << std::endl;
                    }
                    if (debug_level_local >= 1 &&
                        !jvp_block_epsilon_cfg &&
                        newton_iter == 0 &&
                        jvp_call_count >= 1 &&
                        jvp_call_count <= fd_consistency_samples_cfg) {
                        torch::NoGradGuard no_grad;
                        bool use_forward = use_forward_this_call;
                        auto v_full = eps_scaled * v / v_norm;
                        auto v_half = (eps_scaled * 0.5f) * v / v_norm;
                        torch::Tensor jvp_full, jvp_half;
                        if (use_forward) {
                            // Forward diff at eps and eps/2
                            auto F_full_p = rhs_func(U_eval + v_full);
                            jvp_full = (F_full_p - F) * (v_norm / eps_scaled);
                            auto F_half_p = rhs_func(U_eval + v_half);
                            jvp_half = (F_half_p - F) * (v_norm / (eps_scaled * 0.5f));
                        } else {
                            // Central diff at eps and eps/2
                            auto F_full_p = rhs_func(U_eval + v_full);
                            auto F_full_m = rhs_func(U_eval - v_full);
                            jvp_full = (F_full_p - F_full_m) * (0.5f * v_norm / eps_scaled);
                            auto F_half_p = rhs_func(U_eval + v_half);
                            auto F_half_m = rhs_func(U_eval - v_half);
                            jvp_half = (F_half_p - F_half_m) * (0.5f * v_norm / (eps_scaled * 0.5f));
                        }
                        float jvp_fn = jvp_full.norm().to(torch::kCPU).item<float>();
                        float jvp_hn = jvp_half.norm().to(torch::kCPU).item<float>();
                        float rel_diff = (jvp_fn > 1e-30f) ?
                            (jvp_full - jvp_half).norm().to(torch::kCPU).item<float>() / jvp_fn : 0.0f;
                        float jvp_ratio = (v_norm > 1e-30f) ? jvp_fn / v_norm : 0.0f;
                        const char* mode_str = use_forward ? "fwd" : "central";
                        if (rel_diff > 0.5f) {
                            std::cerr << "[FD CONSISTENCY] WARNING call#" << jvp_call_count
                                      << " (" << mode_str << "): rel_diff=" << rel_diff * 100.0f
                                      << "% (||J_eps||=" << jvp_fn << ", ||J_eps2||=" << jvp_hn
                                      << ", ||Jv||/||v||=" << jvp_ratio
                                      << "). FD may be in nonlinear regime." << std::endl;
                        } else if (debug_level_local >= 1) {
                            std::cerr << "[FD CONSISTENCY] OK call#" << jvp_call_count
                                      << " (" << mode_str << "): rel_diff=" << rel_diff
                                      << " (||J_eps||=" << jvp_fn << ", ||J_eps2||=" << jvp_hn
                                      << ", ||Jv||/||v||=" << jvp_ratio << ")" << std::endl;
                        }
                    }

                    } // end else (v_norm >= v_norm_min)

                    if (debug_level_local >= 1 && jvp_call_count <= 3) {
                        // v20.14r48: Recompute forward/mixed for this log (vars were in inner scope)
                        int ms = jvp_mixed_fd_switch_cfg;
                        bool fwd = jvp_use_forward_diff_cfg;
                        if (ms != 0) fwd = (ms < 0) || (newton_iter < ms);
                        std::cerr << "[JVP FD] Call #" << jvp_call_count
                                  << ": " << (fwd ? "forward" : "central")
                                  << " difference"
                                  << (ms != 0 ? " (mixed_fd)" : "")
                                  << '\n';
                    }
                }
                // FIX (2025-12-05): Removed dead "LEGACY PATH" block.
                // After removing jvp_graph_prepared, there are only two paths:
                //   1. use_autograd=true: Forward-mode AD (L1707-1724)
                //   2. use_autograd=false: Finite difference (L1728-1780)
                // The legacy path was a remnant from when jvp_graph_prepared existed.

                // Record JVP timing (outside if/else for correct scope)
                auto jvp_end = std::chrono::high_resolution_clock::now();
                double jvp_ms = std::chrono::duration_cast<std::chrono::microseconds>(jvp_end - jvp_start).count() / 1000.0;
                total_jvp_time_ms += jvp_ms;

                // CRITICAL FIX: Correct Newton equation formulation
                // We're solving (I - dt*gamma*J)dK = -R
                // Since v = dt*gamma*dK and jvp_result = J*v = J*(dt*gamma*dK),
                // the result is dK - jvp_result (NOT dK - dt*gamma*jvp_result)
                auto result = dK - jvp_result;

                // PR 9B: in-situ capture of the raw production JVP for the
                // checker — armed by solve_fgmres around exactly the Arnoldi
                // operator applications, so probe/true-residual A calls are
                // never captured.
                if (jvp_check_this_iter && jvp_check_basis.arnoldi_call_active) {
                    capture_basis_vector(jvp_check_basis.J_w, jvp_result);
                }

                // DIAG 2026-01-27: JVP diagnostic for first 3 calls
                if (jvp_call_count <= 3) {
                    torch::NoGradGuard no_grad;
                    float dK_n = dK.norm().to(torch::kCPU).item<float>();
                    float v_n = v.norm().to(torch::kCPU).item<float>();
                    float jvp_n = jvp_result.norm().to(torch::kCPU).item<float>();
                    float res_n = result.norm().to(torch::kCPU).item<float>();
                    std::cerr << "[JVP DIAG] call #" << jvp_call_count
                              << ": ||dK||=" << dK_n
                              << ", ||v=dt*γ*dK||=" << v_n
                              << ", ||J*v||=" << jvp_n
                              << ", ||dK-J*v||=" << res_n
                              << ", ratio ||J*v||/||dK||=" << (dK_n > 1e-15f ? jvp_n/dK_n : 0.0f)
                              << std::endl;
                }

                return result;
            };

            // Report JVP setup for this Newton iteration
            // FIX (2025-12-05): Removed dead jvp_graph_prepared reference - just report use_autograd mode
            if (debug_level_local >= 1) {
                std::cerr << "[Newton] JVP ready for GMRES (use_autograd="
                          << (use_autograd_jvp ? "true" : "false") << ")" << std::endl;
            }

            // Report GMRES JVP statistics after each Newton iteration
            // (The actual reporting happens after GMRES completes below)
            // Apply preconditioner if available
            // TEMPORARILY DISABLED: Focus on fixing .item() gradient breaks first
            // Preconditioner amplifies residuals 15.6x - will fix after gradients are correct
            // PRECONDITIONER: Now safe to enable after all .item() calls protected with guarded_item<>
            // FIX 2026-01-30: Adaptive preconditioning for both AD and FD modes.
            // The preconditioner operates on Krylov vectors which are .detach()ed (line 2117-2118),
            // so it does NOT interfere with forward-mode AD tangent propagation.
            // Strategy: Start without preconditioner in AD mode (exact Jacobian is well-conditioned
            // at timestep 1). If GMRES fails (rel_error > 0.9), retry with preconditioner
            // (needed at timestep 2+ where temperature-pressure coupling is ill-conditioned).
            auto variable_pc_event = std::make_shared<bool>(false);
            std::function<torch::Tensor(const torch::Tensor&)> precond_func = nullptr;
            if (preconditioner_) {
                // v20.14r38: Preconditioner fallback is STICKY per GMRES solve.
                // Once triggered, the entire remaining GMRES solve uses identity.
                // This maintains the stationary-M⁻¹ assumption required by GMRES.
                precond_func = [this, variable_pc_event, fallback_locked = false](const torch::Tensor& r) mutable -> torch::Tensor {
                    torch::NoGradGuard no_grad;

                    // Once locked to identity, stay locked for remainder of this GMRES solve
                    if (fallback_locked) {
                        precond_total_calls_++;
                        return r;
                    }

                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3) {
                        float r_norm = r.norm().detach().to(torch::kCPU).item<float>();
                        std::cerr << "[PRECOND] Input residual norm: " << r_norm << std::endl;
                    }

                    auto Minv_r = preconditioner_->apply(r);

                    // Over-correction/under-correction fallback guard
                    {
                        float r_norm = r.norm().detach().to(torch::kCPU).item<float>();
                        float Minv_r_norm = Minv_r.norm().detach().to(torch::kCPU).item<float>();
                        float ratio = (r_norm > 1e-12f) ? (Minv_r_norm / r_norm) : 1.0f;

                        const float max_amplification = 100.0f;
                        const float min_amplification = 1e-6f;

                        precond_total_calls_++;

                        if (ratio > max_amplification || ratio < min_amplification ||
                            std::isnan(ratio) || std::isinf(Minv_r_norm)) {
                            precond_fallback_count_++;
                            *variable_pc_event = true;
                            const bool warn_only = wrf::sdirk3::g_sdirk3_config.precond_ratio_guard_warn_only;
                            fallback_locked = !warn_only;  // Warn-only mode keeps current preconditioner output
                            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 ||
                                wrf::sdirk3::g_sdirk3_config.variable_pc_event_log) {
                                std::cerr << "[PRECOND GUARD] Abnormal conditioning: ratio=" << ratio;
                                if (ratio > max_amplification) std::cerr << " > " << max_amplification << " (over-correction)";
                                else if (ratio < min_amplification) std::cerr << " < " << min_amplification << " (under-correction)";
                                std::cerr << (warn_only
                                             ? ", WARN-ONLY mode: keeping preconditioner output (fallback "
                                             : ", LOCKING to identity for remainder of GMRES solve (fallback ")
                                          << precond_fallback_count_
                                          << "/" << precond_total_calls_ << " = "
                                          << (100.0f * precond_fallback_count_ / precond_total_calls_) << "%)" << std::endl;
                            }
                            return warn_only ? Minv_r : r;  // Default behavior unchanged when warn_only=false
                        }

                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3) {
                            std::cerr << "[PRECOND] Output norm: " << Minv_r_norm << std::endl;
                            std::cerr << "[PRECOND] Conditioning ratio: " << ratio << std::endl;
                        }
                    }

                    return Minv_r;
                };
            }

            // M_inv selection: Always use preconditioner when available.
            // The preconditioner operates on .detach()ed Krylov vectors in GMRES,
            // so it does NOT break forward-mode AD tangent propagation.
            // Without preconditioner, GMRES needs O(cond(A)) iterations for convergence,
            // but cond(I - dt*γ*J) ≈ 15000 for em_b_wave acoustic modes.
            std::function<torch::Tensor(const torch::Tensor&)> M_inv = nullptr;
            if (precond_func) {
                M_inv = precond_func;
                if (newton_iter == 0 && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[PRECOND] Using TDMA preconditioner for GMRES" << std::endl;
                }
            }
            
            // P0 DIAGNOSTIC: GMRES initialization monitoring
            // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                torch::NoGradGuard no_grad;  // Diagnostic logging only - safe in FD mode
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float residual_norm = R.norm().to(torch::kCPU).item<float>();
                std::cerr << "[GMRES INIT] Residual norm: " << residual_norm << std::endl;
                std::cerr << "[GMRES INIT] Krylov tolerance: " << krylov_tol_adaptive << std::endl;
                std::cerr << "[GMRES INIT] Max iterations: " << options_.max_krylov_iter << std::endl;
                std::cerr << "[GMRES INIT] Restart: " << options_.gmres_restart << std::endl;
                std::cerr << "[GMRES INIT] Preconditioner: " << (M_inv ? "YES" : "NO") << std::endl;

                if (residual_norm < 1e-12) {
                    std::cerr << "[GMRES INIT] WARNING: Initial residual is near zero!" << std::endl;
                }
            }

            // Solve using GMRES with preconditioner
            int gmres_start_iter = stats_.total_krylov_iterations;
            torch::Tensor dK;
            bool gmres_success = false;
            bool gmres_warmstart_used = false;
            bool variable_pc_event_this_newton = false;
            bool gmres_inn_candidate_built = false;
            bool gmres_inn_used = false;
            bool gmres_inn_gate_pass = false;
            bool gmres_inn_tol_ramped = false;
            float gmres_inn_q = 0.0f;
            float gmres_inn_r_base = 0.0f;
            float gmres_inn_r_cand = 0.0f;
            std::string gmres_inn_status = "off";
            int gmres_inn_reason_code = 0;
            bool gmres_inn_prev_shape_ok = false;
            bool gmres_inn_prev_quality_ok = false;
            bool gmres_inn_prev_varpc_ok = false;
            float gmres_inn_prev_rel = 0.0f;
            float gmres_inn_x0_base_norm = 0.0f;
            float gmres_inn_x0_cand_norm = 0.0f;
            float gmres_inn_x0_delta_norm = 0.0f;
            float gmres_inn_x0_rel_delta = 0.0f;
            float gmres_inn_gate_rel_diff = 0.0f;
            bool gmres_inn_gate_non_degrade = false;
            bool gmres_inn_gate_quality_ok = false;
            float gmres_inn_ru_share_base = 0.0f;
            float gmres_inn_ru_share_cand = 0.0f;
            float gmres_inn_rw_share_base = 0.0f;
            float gmres_inn_rw_share_cand = 0.0f;
            float gmres_inn_ph_share_base = 0.0f;
            float gmres_inn_ph_share_cand = 0.0f;

            // v20.14 r49: JVP auto-benchmark (one-shot, stage 1, Newton iter 0)
            // BEFORE fd_no_grad_guard so AD bench would work if enabled.
            // Result in jvp_locked_mode_ (solver-local), not g_sdirk3_config.
            if (jvp_locked_mode_ == -1 &&
                wrf::sdirk3::g_sdirk3_config.jvp_auto_bench_calls > 0 &&
                newton_iter == 0 && stage == 1) {
                const auto& cfg_bench = wrf::sdirk3::g_sdirk3_config;
                const int N = std::max(1, cfg_bench.jvp_auto_bench_calls);
                const int warmup = std::max(0, cfg_bench.jvp_auto_bench_warmup);
                const float eps_bench = cfg_bench.jvp_epsilon;
                constexpr float quality_gate_thresh = 0.25f;

                // Deterministic probe vector to reduce timing jitter across ranks/runs.
                auto bench_idx = torch::arange(
                    K.numel(),
                    torch::TensorOptions().dtype(torch::kFloat32).device(K.device()));
                auto bench_v = torch::sin(bench_idx + static_cast<float>(cfg_bench.jvp_auto_bench_seed))
                                   .reshape_as(K)
                                   .to(K.device(), K.scalar_type())
                                   .detach();

                // R13.20 (hard-constraint audit): both of these were bare `.item()` outside a
                // guard, and each is also a GPU->CPU sync.
                float v_norm_val = guarded_item<float>(bench_v.norm());
                if (!(v_norm_val > 1e-20f)) {
                    bench_v = torch::ones_like(K).detach();
                    v_norm_val = guarded_item<float>(bench_v.norm());
                }
                auto v_scaled = (eps_bench / std::max(v_norm_val, 1e-20f)) * bench_v;
                const float inv_eps = v_norm_val / std::max(eps_bench, 1e-20f);
                const float inv_eps_central = 0.5f * inv_eps;

                double fwd_ms = 0.0;
                double cen_ms = 0.0;
                bool quality_fail = false;
                float quality_rel = 0.0f;
                {
                    torch::NoGradGuard no_grad;
                    for (int w = 0; w < warmup; ++w) {
                        auto Fp = compute_rhs(U_eval + v_scaled);
                        auto jvp = (Fp - F) * inv_eps;
                        (void)jvp;
                    }
                    auto t0 = std::chrono::high_resolution_clock::now();
                    for (int b = 0; b < N; ++b) {
                        auto Fp = compute_rhs(U_eval + v_scaled);
                        auto jvp = (Fp - F) * inv_eps;
                        (void)jvp;
                    }
                    fwd_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::high_resolution_clock::now() - t0).count() / N;

                    for (int w = 0; w < warmup; ++w) {
                        auto Fp = compute_rhs(U_eval + v_scaled);
                        auto Fm = compute_rhs(U_eval - v_scaled);
                        auto jvp = (Fp - Fm) * inv_eps_central;
                        (void)jvp;
                    }
                    auto t1 = std::chrono::high_resolution_clock::now();
                    for (int b = 0; b < N; ++b) {
                        auto Fp = compute_rhs(U_eval + v_scaled);
                        auto Fm = compute_rhs(U_eval - v_scaled);
                        auto jvp = (Fp - Fm) * inv_eps_central;
                        (void)jvp;
                    }
                    cen_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::high_resolution_clock::now() - t1).count() / N;

                    if (cfg_bench.jvp_auto_bench_quality_gate) {
                        auto Fp = compute_rhs(U_eval + v_scaled);
                        auto Fh = compute_rhs(U_eval + 0.5f * v_scaled);
                        auto jvp_eps = (Fp - F) * inv_eps;
                        auto jvp_half = (Fh - F) * (2.0f * inv_eps);
                        float jn = jvp_eps.norm().to(torch::kCPU).item<float>();
                        float dn = (jvp_eps - jvp_half).norm().to(torch::kCPU).item<float>();
                        quality_rel = (jn > 1e-20f) ? (dn / jn) : 0.0f;
                        quality_fail = (!std::isfinite(quality_rel) || quality_rel > quality_gate_thresh);
                    }
                }
                jvp_locked_mode_ = (fwd_ms <= cen_ms) ? 0 : 1;
                if (cfg_bench.jvp_auto_bench_quality_gate && quality_fail) {
                    jvp_locked_mode_ = 1;  // conservative fallback
                }
                std::cerr << "[JVP AUTO-BENCH] forward=" << fwd_ms
                          << "ms, central=" << cen_ms
                          << "ms, warmup=" << warmup
                          << ", seed=" << cfg_bench.jvp_auto_bench_seed;
                if (cfg_bench.jvp_auto_bench_quality_gate) {
                    std::cerr << ", quality_rel=" << quality_rel
                              << " (thr=" << quality_gate_thresh << ")"
                              << (quality_fail ? " -> gate=FAIL" : " -> gate=PASS");
                }
                std::cerr << " -> selected "
                          << (jvp_locked_mode_ == 0 ? "FORWARD" : "CENTRAL") << "\n";
            }

            // CRITICAL: Disable autograd graph building in FD mode
            // Without this, compute_rhs builds huge graphs that accumulate
            // across Arnoldi iterations, causing massive slowdown (17+ min stalls)
            std::unique_ptr<torch::NoGradGuard> fd_no_grad_guard;
            if (!wrf::sdirk3::g_sdirk3_config.use_autograd) {
                fd_no_grad_guard = std::make_unique<torch::NoGradGuard>();
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && newton_iter == 0) {
                    std::cerr << "[Newton] FD mode: GradMode disabled for GMRES" << std::endl;
                }
            }

            // v20.3: Inform preconditioner of Newton progress for adaptive α
            // FIX: Use pre-extracted float (last_res_scaled) instead of tensor ops
            // to avoid SIGBUS from nested tensor operations
            if (preconditioner_ && init_R0_norm > 1e-30f) {
                float ratio = std::min(1.0f, last_res_scaled / init_R0_norm);
                preconditioner_->set_newton_residual_ratio(ratio);
                preconditioner_->set_newton_iteration(newton_iter);  // v20.14 r46g
                preconditioner_->set_newton_ru_share(ru_share);    // v20.14 r47c-fix2
            }

            // CRITICAL FIX (2025-11-28): Declare outside try block for use in trust region
            float gmres_rel_error = 1.0f;  // Default to 1.0 (no reduction) if GMRES fails
            float gmres_raw_rel_error = 1.0f;  // v20.14r27g: unclamped, may be >1 when GMRES diverges
            // R13.19 SELF-REVIEW (round 8, P1-A): set where gmres_result is in scope; read by the
            // total-failure predicate below, which is outside that scope.
            bool gmres_converged_on_entry = false;
            float gmres_initial_rel_error = -1.0f;  // R13.11: ||r0||/||b|| from the same solve
            // R9 P0-D: the GMRES residual, kept only when the nonlinear ledger is armed. It is
            // what turns the linear model's prediction into a read rather than an operator
            // call: with b = -R and r_g = b - A dK, the predicted post-step residual
            // R + a A dK is exactly (1-a) R - a r_g.
            torch::Tensor ledger_r_gmres;
            // ... and the dK that residual BELONGS TO. dK is mutated after the solve --
            // apply_halo_zeroing() always, and the direct-U-solve override replaces the whole
            // ru block with -R_u when it is enabled -- so projecting the applied step onto the
            // LIVE dK would compare it against a vector r_g never saw, and certify a prediction
            // built from a mismatched pair. The snapshot is taken before any of that.
            torch::Tensor ledger_dK_solve;
            bool krylov_tol_stage_override = false;
            bool stage_budget_active_this_iter = false;
            bool stage_budget_forcing_coupled = false;
            float stage_budget_forcing_eta = -1.0f;
            float stage_budget_scale = 1.0f;

            // FIX 2026-01-29: Removed stage 3 GMRES budget doubling.
            // The 2x expansion caused 4x JVP calls for stage 3, which was the dominant
            // performance bottleneck. With proper K_prev predictors, stage 3 converges
            // with the same budget as stages 1/2.
            int effective_restart = options_.gmres_restart;
            int effective_max_restarts = options_.max_krylov_iter;

            // v20.14 r49/r59: Stage-aware GMRES budget override
            // Priority:
            //   1) stage3_* for stage>=3 when explicitly set (>0),
            //   2) stage2_* for stage>=2 when set (>0),
            //   3) base solver options.
            // STAGE 3 INHERITS STAGE 2 unless its own knob is set (the stage-3 override lives
            // further down). That inheritance invalidated a published stage-3 budget table:
            // leaving stage3_gmres_restart unset was labelled "global default" when it actually
            // ran stage 3 on the stage-2 value, EW-scaled. Measured, same run otherwise:
            //
            //     stage3 unset          -> 510 Arnoldi, stage-3 gate 0.727
            //     stage3 = 600 explicit -> 600 Arnoldi, stage-3 gate 3.386
            //
            // WHY THE TWO DIFFER, corrected: it is purely the budget, and the mechanism is
            // ORDERING. The EW budget scaling
            //     grep -F 'effective_restart * budget_scale' wrf_sdirk3_newton_solver.cpp
            //         (the -F matters: the * is a regex quantifier, so plain grep finds NOTHING.
            //          The source spells this std::max(2, static_cast<int>(...)) across two lines)
            // runs BEFORE
            //     grep -F 'if (cfg.stage3_gmres_restart > 0) effective_restart = cfg.stage3_gmres_restart;' wrf_sdirk3_newton_solver.cpp
            // so an inherited stage-2 value gets scaled (600 -> 510) while an explicit stage-3
            // value replaces the scaled number outright and stands at 600.
            //
            // Cited by GREP-ABLE SUBSTRING, and both halves of that matter. The first version
            // gave line numbers (:6800, :6848) that inserting THIS comment had already shifted by
            // seven -- a line number in a comment is invalidated by the edit that writes it. The
            // second version replaced them with a pretty-printed expression that matched NOTHING
            // in the file except itself: the source wraps std::max/static_cast across two lines
            // and writes 0.5f, so grepping the citation found the comment and not the code.
            //
            // FOUR iterations on one citation, each failure subtler than the last:
            //   1. line numbers, invalidated by the edit that wrote them
            //   2. a pretty-printed "expression" that matched only itself
            //   3. correct strings, but published as commands that do not run -- plain grep on
            //      the first returns 0 matches because * is a quantifier (the second worked;
            //      parentheses are literal in BRE, so only one of the two was broken)
            //   4. -F added, FILE ARGUMENT omitted -- the published command read stdin, not this
            //      file, so it exits 1 and matches nothing
            //
            // Every round was verified -- on a VARIANT. Escaped when the published form was
            // unescaped; with a file argument when the published form had none. So the rule is
            // not "run the citation" but stronger: EXTRACT the command from this file and execute
            // that, so the thing verified and the thing shipped cannot differ. This version was
            // checked that way.
            //
            // NOT a policy difference -- an earlier version of this comment said setting a
            // stage-3 knob flips stage_budget_active and changes EW coupling. It does not:
            // stage_budget_active is an OR over the stage-2 AND stage-3 knobs, and
            // stage2_gmres_restart = 600 was set in BOTH arms, so it was already true either way.
            //
            // Any stage-budget experiment must set the stage's OWN knob explicitly and report the
            // Arnoldi count actually USED, not the one requested.
            if (stage >= 2) {
                auto& cfg = wrf::sdirk3::g_sdirk3_config;
                // Knob resolution and EW budget scaling are a PURE function of the config, the
                // stage and the forcing -- resolved in wrf_sdirk3_stage_krylov_policy.h so the
                // ORDER is named and testable instead of implicit in this block's line order.
                // Default is ShippedOrder, which reproduces the previous behaviour exactly.
                // The hopeless caps below stay here: they read streak counters, i.e. state.
                wrf::sdirk3::StageKrylovInputs policy_in;
                policy_in.stage             = stage;
                policy_in.base_restart      = effective_restart;
                policy_in.base_max_restarts = effective_max_restarts;
                policy_in.base_tol          = krylov_tol_adaptive;
                policy_in.s2_restart        = cfg.stage2_gmres_restart;
                policy_in.s2_max_restarts   = cfg.stage2_max_krylov_restarts;
                policy_in.s2_tol            = cfg.stage2_krylov_tol;
                policy_in.s3_restart        = cfg.stage3_gmres_restart;
                policy_in.s3_max_restarts   = cfg.stage3_max_krylov_restarts;
                policy_in.s3_tol            = cfg.stage3_krylov_tol;
                policy_in.ew_enabled        = ew_eta_enabled_this_iter;
                policy_in.ew_eta            = ew_eta_used_this_iter;
                policy_in.order = stage_krylov_order();   // the single reading; see its definition
                const auto policy = wrf::sdirk3::resolve_stage_krylov_policy(policy_in);

                // R11 V2: the RUNTIME behaviour manifest.
                //
                // policy_fields_that_differ() compares the PURE policy, and the pure policy is
                // not everything that changes what a run does. Hopeless-mode caps, early-stop
                // streaks, the warm-start latch, the trust radius and the preconditioner mode
                // are all stateful and all alter the solve, and two arms of a "single-variable"
                // sweep can differ in any of them without any knob differing.
                //
                // Two arms are separate processes, so there is no in-process baseline to fail
                // closed against. What makes the sweep verifiable is that every behaviour-bearing
                // value is ON THE RECORD at the same point in both arms: diffing two manifests
                // then shows any unintended difference, instead of leaving it to the assumption
                // that only the knob moved.
                if (newton_iter == 0 &&
                    wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_POLICY_MANIFEST")) {
                    auto& cfgm = wrf::sdirk3::g_sdirk3_config;
                    // A manifest is only machine-checkable if its numbers round-trip: at the
                    // default 6 significant digits two arms whose tolerances differ in the 7th
                    // digit print identically, and the diff that should have caught it is blind.
                    const auto prec_saved = std::cerr.precision();
                    std::cerr << std::setprecision(
                        std::numeric_limits<double>::max_digits10);
                    const int ws_slot = (stage >= 0 && stage < 8) ? stage : -1;
                    const long long host_ts = ::sdirk3_host_global_timestep();
                    if (manifest_last_stage_ < 0 || stage <= manifest_last_stage_) {
                        ++manifest_step_seq_;
                        // A stage that did not advance WITHIN the same host timestep is a
                        // retry; across a timestep boundary it is simply the next step.
                        manifest_retry_generation_ =
                            (host_ts == manifest_last_host_ts_)
                                ? manifest_retry_generation_ + 1 : 0;
                    }
                    manifest_last_host_ts_ = host_ts;
                    manifest_last_stage_ = stage;
                    std::cerr << "SDIRK3_POLICY_MANIFEST stage=" << stage
                              // the composite identity: without it two arms can be compared at
                              // different occurrences of the same stage and still "agree"
                              // R13.1: PHYSICAL identity first. step_seq is inferred from
                              // the stage number and solver_id is a process-wide ordinal --
                              // both are properties of the process, and two separately
                              // launched arms can assign the same solver_id to different
                              // tiles or different ids to the same one. Neither survives the
                              // change of decomposition the comparison exists to make.
                              // grid%itimestep already crosses the ABI at the freshness
                              // consumption point; it was simply not retained.
                              << " global_timestep=" << ::sdirk3_host_global_timestep()
                              << " retry_generation=" << manifest_retry_generation_
                              << " step_seq=" << manifest_step_seq_
                              << " newton_iter=" << newton_iter
                              << " solver_id=" << solver_id_
                              << " rank=" << wrf::sdirk3::diagnostic_mpi_rank()
                              // resolved policy (what the pure resolver decided)
                              << " restart=" << policy.restart
                              << " max_restarts=" << policy.max_restarts
                              << " tol=" << policy.tol
                              << " tol_overridden=" << (policy.tol_overridden ? 1 : 0)
                              << " budget_active=" << (policy.budget_active ? 1 : 0)
                              << " ew_applied=" << (policy.ew_applied ? 1 : 0)
                              << " ew_scale=" << policy.ew_scale
                              << " restart_source=" << static_cast<int>(policy.restart_source)
                              << " order=" << (policy_in.order ==
                                               wrf::sdirk3::StageKrylovOrder::StageKnobFirst
                                                   ? "knob_first" : "shipped")
                              // stateful runtime behaviour the pure policy does NOT contain
                              << " s2_hopeless_mode=" << (stage2_hopeless_budget_mode_ ? 1 : 0)
                              << " s2_hopeless_streak=" << stage2_hopeless_streak_
                              << " s3_hopeless_mode=" << (stage3_hopeless_budget_mode_ ? 1 : 0)
                              << " s3_hopeless_streak=" << stage3_hopeless_streak_
                              << " s3_warmstart_disabled=" << (stage3_warmstart_disabled_ ? 1 : 0)
                              << " trust_radius=" << trust_radius_
                              << " trust_region_on=" << (cfgm.nk_trust_region ? 1 : 0)
                              << " hopeless_relax=" << (cfgm.hopeless_relax ? 1 : 0)
                              << " precond_type=" << cfgm.precond_type
                              << " gmres_block_scale=" << (cfgm.gmres_block_scale ? 1 : 0)
                              << " use_autograd=" << (cfgm.use_autograd ? 1 : 0)
                              << " imex_split_mode=" << cfgm.imex_split_mode
                              << " newton_tol=" << cfgm.newton_tol
                              << " max_newton_iter=" << cfgm.max_newton_iter
                              << " ew_enabled=" << (ew_eta_enabled_this_iter ? 1 : 0)
                              << " ew_eta=" << ew_eta_used_this_iter
                              // when GMRES checks the TRUE residual rather than the Arnoldi estimate
                              << " true_resid_start_j=" << cfgm.gmres_true_residual_start_j
                              << " true_resid_period=" << cfgm.gmres_true_residual_period
                              // what makes a restart cycle give up early
                              << " arnoldi_stag_window=" << cfgm.gmres_arnoldi_stag_window
                              << " arnoldi_stag_ratio=" << cfgm.gmres_arnoldi_stag_ratio
                              << " no_early_stop=" << (no_early_stop_enabled() ? 1 : 0)
                              // degraded-operator latches: a fallback changes the operator, not a knob
                              << " precond_fallback_count=" << precond_fallback_count_
                              << " jvp_method=" << static_cast<int>(cfgm.jvp_method)
                              << " jvp_fd_fallback_count="
                              << g_jvp_fd_fallback_count.load(std::memory_order_relaxed)
                              << " imex_slow_in_tangent=" << (cfgm.imex_slow_in_tangent ? 1 : 0)
                              // warm-start: enabled is a knob, VALID is state -- a sweep needs both
                              << " warmstart_enabled=" << (cfgm.gmres_warmstart ? 1 : 0)
                              << " warmstart_gate=" << cfgm.gmres_warmstart_quality_gate
                              << " warmstart_valid="
                              << ((ws_slot >= 0 && gmres_warmstart_stage_[ws_slot].defined())
                                      ? 1 : 0)
                              << " warmstart_relerr="
                              << ((ws_slot >= 0) ? gmres_warmstart_relerr_stage_[ws_slot] : -1.0f)
                              << std::endl;
                    std::cerr << std::setprecision(prec_saved);
                }

                const int  restart_before_policy = effective_restart;
                const int  maxr_before_policy    = effective_max_restarts;
                effective_restart         = policy.restart;
                effective_max_restarts    = policy.max_restarts;
                krylov_tol_adaptive       = policy.tol;
                krylov_tol_stage_override = policy.tol_overridden;

                const bool stage_budget_active = policy.budget_active;
                stage_budget_active_this_iter = stage_budget_active;
                stage_budget_forcing_eta      = policy.ew_eta_used;
                stage_budget_scale            = policy.ew_scale;
                stage_budget_forcing_coupled  = policy.ew_applied;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && newton_iter == 0) {
                    // Report the value USED and where it came from, so an inherited budget can
                    // never again be read as "the global default".
                    std::cerr << "[GMRES POLICY] Stage " << stage
                              << ": order=" << (policy_in.order ==
                                                wrf::sdirk3::StageKrylovOrder::StageKnobFirst
                                                    ? "stage_knob_first" : "shipped")
                              << ", restart=" << restart_before_policy
                              << "->" << effective_restart
                              << " (source=" << static_cast<int>(policy.restart_source) << ")"
                              << ", max_restarts=" << maxr_before_policy
                              << "->" << effective_max_restarts
                              << ", ew_scale=" << policy.ew_scale
                              << ", ew_applied=" << (policy.ew_applied ? 1 : 0)
                              << ", budget_active=" << (stage_budget_active ? 1 : 0)
                              << std::endl;
                }
                if (stage == 2 && stage_budget_active && stage2_hopeless_budget_mode_ &&
                    !cfg.hopeless_relax) {
                    int restart_before = effective_restart;
                    int maxr_before = effective_max_restarts;
                    effective_restart = std::max(2, std::min(effective_restart, stage2_hopeless_restart_cap_));
                    effective_max_restarts = std::max(1, std::min(effective_max_restarts, 1));
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && newton_iter == 0 &&
                        (restart_before != effective_restart || maxr_before != effective_max_restarts)) {
                        std::cerr << "[GMRES HOPLESS MODE] Stage 2 budget capped: restart="
                                  << restart_before << "->" << effective_restart
                                  << ", max_restarts=" << maxr_before << "->" << effective_max_restarts
                                  << " (streak=" << stage2_hopeless_streak_ << ")\n";
                    }
                } else if (stage == 2 && stage_budget_active && stage2_hopeless_budget_mode_ &&
                           cfg.hopeless_relax && wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                           newton_iter == 0) {
                    std::cerr << "[GMRES HOPLESS MODE] Stage 2 cap bypassed by hopeless_relax=on"
                              << " (streak=" << stage2_hopeless_streak_ << ")\n";
                }
                if (stage >= 3) {
                    // The stage-3 knobs were already applied by the policy resolver above, in
                    // the order the resolver names. Only the stateful cap remains here.

                    // v20.14r60: Repeated hopeless Stage-3 GMRES failures in ru-dominant mode
                    // indicate wasted JVP budget. Cap Stage-3 restart in that mode.
                    if (stage_budget_active && stage3_hopeless_budget_mode_ &&
                        !cfg.hopeless_relax) {
                        int restart_before = effective_restart;
                        int maxr_before = effective_max_restarts;
                        effective_restart = std::max(2, std::min(effective_restart, stage3_hopeless_restart_cap_));
                        effective_max_restarts = std::max(1, std::min(effective_max_restarts, 1));
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && newton_iter == 0 &&
                            (restart_before != effective_restart || maxr_before != effective_max_restarts)) {
                            std::cerr << "[GMRES HOPLESS MODE] Stage 3 budget capped: restart="
                                      << restart_before << "->" << effective_restart
                                      << ", max_restarts=" << maxr_before << "->" << effective_max_restarts
                                      << " (streak=" << stage3_hopeless_streak_ << ")\n";
                        }
                    } else if (stage_budget_active && stage3_hopeless_budget_mode_ &&
                               cfg.hopeless_relax && wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                               newton_iter == 0) {
                        std::cerr << "[GMRES HOPLESS MODE] Stage 3 cap bypassed by hopeless_relax=on"
                                  << " (streak=" << stage3_hopeless_streak_ << ")\n";
                    }
                }
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && newton_iter == 0) {
                    std::cerr << "[GMRES INIT] Stage " << stage << " effective: restart="
                              << effective_restart << ", max_restarts=" << effective_max_restarts
                              << ", tol=" << krylov_tol_adaptive
                              << ", budget_coupled=" << (stage_budget_forcing_coupled ? "yes" : "no")
                              << ", budget_eta=" << stage_budget_forcing_eta
                              << ", budget_scale=" << stage_budget_scale
                              << "\n";
                }
            }

            // GMRES operator and preconditioner (always use preconditioner when available)
            // Apply block diagonal scaling: S⁻¹·A·S transforms the system so all
            // variable blocks have comparable magnitude in the L2 norm.
            std::function<torch::Tensor(const torch::Tensor&)> gmres_op;
            std::function<torch::Tensor(const torch::Tensor&)> gmres_M_inv;

            if (scaling_initialized_) {
                // Scaled operator: dK_tilde → S⁻¹ · A · S · dK_tilde
                // v20.14r26: Halo mask REMOVED from operator and preconditioner.
                // Masking the operator projects out boundary DOFs, removing
                // interior-boundary coupling that GMRES needs for convergence.
                // Halo zeroing is applied post-GMRES (dK) and pre-Newton (K) instead.
                gmres_op = [&](const torch::Tensor& dK_tilde) -> torch::Tensor {
                    auto dK_unscaled = S_diag_ * dK_tilde;       // S · dK_tilde
                    auto A_dK = apply_jacobian(dK_unscaled);      // A · (S · dK_tilde)
                    return S_inv_diag_ * A_dK;                    // S⁻¹ · A · S · dK_tilde
                };
                // Scaled preconditioner: M̃⁻¹(v) = S⁻¹ · M⁻¹(S · v)
                // The preconditioner must approximate (S⁻¹·A·S)⁻¹ in the scaled space,
                // so we conjugate the original M⁻¹ with S to maintain consistency.
                if (M_inv) {
                    gmres_M_inv = [&](const torch::Tensor& v) -> torch::Tensor {
                        auto v_unscaled = S_diag_ * v;       // S · v  (back to original space)
                        auto Minv_v = M_inv(v_unscaled);      // M⁻¹(S · v)  (in original space)
                        return S_inv_diag_ * Minv_v;          // S⁻¹ · M⁻¹(S · v)  (back to scaled space)
                    };
                } else {
                    gmres_M_inv = nullptr;
                }
            } else {
                // Unscaled path: v20.14r26 — no halo mask on operator/preconditioner
                gmres_op = apply_jacobian;
                gmres_M_inv = M_inv;
            }

            // v20.14: DEFECT-CORRECTION PRECONDITIONER REFINEMENT
            // Wraps gmres_M_inv with iterative defect correction:
            //   z = M⁻¹(v); for each extra pass: z += M⁻¹(v - A(z))
            // Cost per extra pass: 1 JVP + 1 precond apply per Arnoldi step.
            {
                int refinement_passes = wrf::sdirk3::g_sdirk3_config.precond_refinement_passes;
                if (refinement_passes > 1 && gmres_M_inv) {
                    auto M_inv_base = gmres_M_inv;
                    auto A_op = gmres_op;
                    int n_extra = refinement_passes - 1;
                    bool do_log = (newton_iter == 0 &&
                                   wrf::sdirk3::g_sdirk3_config.debug_level >= 1);
                    // v20.14r27z: Safety gate — check defect ratio on first call.
                    // If ||defect||/||v|| > threshold, defect correction is counterproductive
                    // (preconditioner error too large). Disable for all remaining Arnoldi steps.
                    constexpr float defect_gate = 5.0f;
                    gmres_M_inv = [M_inv_base, A_op, n_extra, do_log, defect_gate, variable_pc_event,
                                   call_count = 0, refinement_active = true](const torch::Tensor& v) mutable -> torch::Tensor {
                        auto z = M_inv_base(v);
                        if (!refinement_active) return z;
                        for (int p = 0; p < n_extra; ++p) {
                            auto defect = v - A_op(z);
                            if (call_count == 0) {
                                torch::NoGradGuard no_grad;
                                float v_n = v.norm().to(torch::kCPU).item<float>();
                                float d_n = defect.norm().to(torch::kCPU).item<float>();
                                float ratio = (v_n > 1e-30f) ? d_n / v_n : 0.0f;
                                if (do_log) {
                                    std::cerr << "[PRECOND REFINE] pass " << (p + 1)
                                              << ": ||defect||/||v|| = " << ratio << "\n";
                                }
                                if (ratio > defect_gate) {
                                    *variable_pc_event = true;
                                    if (do_log) {
                                        std::cerr << "[PRECOND REFINE] DISABLED: ratio " << ratio
                                                  << " > " << defect_gate
                                                  << " — defect correction counterproductive.\n";
                                    } else if (wrf::sdirk3::g_sdirk3_config.variable_pc_event_log) {
                                        std::cerr << "[PRECOND REFINE EVENT] disable on defect ratio "
                                                  << ratio << " > " << defect_gate << std::endl;
                                    }
                                    refinement_active = false;
                                    break;
                                }
                            }
                            z = z + M_inv_base(defect);
                        }
                        ++call_count;
                        return z;
                    };
                    if (do_log) {
                        std::cerr << "[PRECOND REFINE] Active: " << refinement_passes
                                  << " passes (" << n_extra << " defect corrections)\n";
                    }
                }
            }

            // 9F.D119: NUMERICAL RANGE of the operator GMRES actually iterates.
            //
            // The measurement that killed the preconditioner track was a Ritz spectrum showing
            // A_fast straddling the origin. It was taken on an operator built from Omega = mu*w,
            // which D117/D118 established is not WRF's Omega at all. This re-asks the question
            // on whatever operator is currently configured, so the two can be compared directly.
            //
            // Rayleigh quotients, not eigenvalues, and deliberately so. For a NON-NORMAL operator
            // -- which this is -- the quantity that governs GMRES is the field of values, i.e.
            // the spectrum of the symmetric part (A+A^T)/2, and eigenvalues can mislead in both
            // directions. q(v) = <v,Av>/<v,v> samples exactly that field.
            //
            // ASYMMETRIC EVIDENCE, and the output says so: ONE negative q PROVES the numerical
            // range straddles the origin IN THESE COORDINATES for THIS operator. It does NOT
            // prove no SPD preconditioner can fix it -- right-preconditioning A -> A P^-1 is not
            // a congruence, so Sylvester's law does not transfer (A = [[0,-1],[2,2]] indefinite,
            // SPD P^-1 = [[2,-1],[-1,2]], H(A P^-1) = diag(1,2) definite). (And the
            // sampled v is a witness). All-positive over N samples is EVIDENCE OF NOTHING
            // stronger than "N random directions missed it" -- random vectors concentrate, and a
            // narrow negative cone is easy to miss. Do not read a clean run as a definiteness
            // proof.
            //
            // Opt-in, read-only, once per stage.
            {
                static const bool numrange_on = [](){
                    const char* e = std::getenv("WRF_SDIRK3_NUMRANGE_PROBE");
                    return e && *e && std::string(e) != "0" && std::string(e) != "false";
                }();
                if (numrange_on && newton_iter == 0 && probe_numrange_stage_ != stage &&
                    gmres_op) {
                    probe_numrange_stage_ = stage;
                    // 9F.D121 (review 9.1): the previous comment here said "seeded per sample so
                    // the run is reproducible" above a bare randn_like with no generator. The
                    // comment asserted a property the code did not have -- the samples came from
                    // the global RNG and depended on call order. Pin the seed, and restore the
                    // caller's seed afterwards so the probe does not shift the solver's stream.
                    // NOTE what this does and does not do: it restores the SEED, not the full
                    // philox offset, so it is not a bitwise restore of generator state.
                    // 9F.D123 (review 14): D121 saved current_seed() and called manual_seed()
                    // to restore it. That is NOT a restore -- reseeding REWINDS the stream to
                    // position 0, so a caller that had already drawn N samples silently starts
                    // over. My own comment admitted it did not restore the offset and I shipped
                    // it anyway; rewinding is strictly worse than leaving the stream alone.
                    // Use a probe-LOCAL generator and never touch the global one.
                    auto probe_gen = at::detail::createCPUGenerator(
                        0x5D1BC3ULL + static_cast<uint64_t>(stage));
                    const int n_samp = 24;
                    double q_min = std::numeric_limits<double>::infinity();
                    double qp_min = std::numeric_limits<double>::infinity();
                    double qp_max = -std::numeric_limits<double>::infinity();
                    int np_ok = 0, np_neg = 0;
                    double q_max = -std::numeric_limits<double>::infinity();
                    int n_neg = 0, n_ok = 0;
                    // 9F.D121 (review 9.2): production FGMRES is RIGHT-preconditioned -- the file
                    // states it outright ("Right-preconditioning minimizes ||b - A*M^-1*z||").
                    // The Arnoldi operator is therefore A*M^-1, and the previous probe measured
                    // M^-1*A while its comment claimed to be measuring "the operator GMRES
                    // actually iterates". Different operator, same two factors. Both are reported
                    // now, and the production one is labelled.
                    double qr_min = std::numeric_limits<double>::infinity();  // A*M^-1 (production)
                    int nr_neg = 0, nr_ok = 0;
                    double ql_min = std::numeric_limits<double>::infinity();  // M^-1*A
                    int nl_neg = 0, nl_ok = 0;
                    for (int sidx = 0; sidx < n_samp; ++sidx) {
                        torch::Tensor v;
                        {
                            torch::NoGradGuard ng_seed;
                            v = torch::randn(R.sizes(), probe_gen,
                                             R.options().device(torch::kCPU))
                                    .to(R.device(), R.scalar_type()).detach();
                        }
                        // R13.14 (red team round 5, R5-17): this used to assert that a blanket NoGradGuard
                        // around `gmres_op` "kills the very graph it needs", citing five past mistakes. The
                        // tree REFUTES it: the frozen A/B probe runs every one of its matvecs -- including 30
                        // solve_fgmres arms -- inside `NoGradGuard`, and its records report
                        // jvp_fd_fallback_free=1, i.e. not one matvec fell back. The mechanism agrees:
                        // compute_jvp_fwad_or_fd opens with its OWN NoGradGuard before _make_dual, and
                        // forward-mode dual level is independent of grad mode -- so a caller's guard is
                        // irrelevant on the AD path and harmless on the FD path. The warning may be true of
                        // the PRODUCTION solve, where the linear solve itself must stay differentiable; it was
                        // not true here, and it was asserted at an emit site with no fixture to reject it.
                        // Its cost was real: this probe deliberately ran its matvecs unguarded, building
                        // reverse-mode graphs nothing consumes, at newton_iter == 0 on the hot path.
                        torch::Tensor Av, MAv, AMv;
                        bool threw = false;
                        try {
                            Av = gmres_op(v);
                            // Referee C6(f): this probe samples S^-1 A S. The Arnoldi witness
                            // in solve_gmres samples D^-1 S^-1 A S when block scaling is on --
                            // a DIFFERENT operator -- so "random finds none, Krylov finds half,
                            // on the same operator" was false as stated. D is built inside the
                            // solve from r0 and is not available here without rebuilding it
                            // (the duplicate-authority trap), so this probe names its
                            // coordinates and the same-operator comparison is the Arnoldi
                            // witness run with block scaling OFF, which is also S^-1 A S.
                            if (gmres_M_inv) {
                                MAv = gmres_M_inv(Av);          // M^-1 A
                                AMv = gmres_op(gmres_M_inv(v)); // A M^-1  <- production order
                            }
                        } catch (...) { threw = true; }
                        if (threw || !Av.defined()) continue;
                        torch::NoGradGuard ng_red;
                        double vv = v.dot(v).to(torch::kCPU).item<double>();
                        if (!(vv > 0.0)) continue;
                        double q = v.dot(Av).to(torch::kCPU).item<double>() / vv;
                        if (std::isfinite(q)) {
                            ++n_ok; q_min = std::min(q_min, q); q_max = std::max(q_max, q);
                            if (q < 0.0) ++n_neg;
                        }
                        // R13.18 (external review S13): the RAW PHYSICAL operator. Every witness
                        // this campaign has is for S^-1AS or D^-1S^-1AS, and the numerical range
                        // is NOT similarity-invariant -- so whether W(A) straddles the origin in
                        // the physical inner product has never been established, and the two
                        // campaign pivots that rested on "intrinsically indefinite" rested on a
                        // coordinate statement.
                        //
                        // No new operator is needed. With v~ the scaled direction, the physical
                        // direction is v = S v~ and A v = S * gmres_op(v~), so
                        //   q_phys = <S v~, S gmres_op(v~)> / ||S v~||^2
                        // is the Rayleigh quotient of the raw A on a genuine physical direction,
                        // from matvecs already taken.
                        if (S_diag_.defined() && S_diag_.numel() == v.numel()) {
                            const auto v_phys = (S_diag_ * v).to(torch::kFloat64);
                            const auto Av_phys = (S_diag_ * Av).to(torch::kFloat64);
                            const double vv_p = v_phys.dot(v_phys).item<double>();
                            if (vv_p > 0.0) {
                                const double qp =
                                    v_phys.dot(Av_phys).item<double>() / vv_p;
                                if (std::isfinite(qp)) {
                                    ++np_ok;
                                    qp_min = std::min(qp_min, qp);
                                    qp_max = std::max(qp_max, qp);
                                    if (qp < 0.0) ++np_neg;
                                }
                            }
                        }
                        if (MAv.defined()) {
                            double ql = v.dot(MAv).to(torch::kCPU).item<double>() / vv;
                            if (std::isfinite(ql)) {
                                ++nl_ok; ql_min = std::min(ql_min, ql);
                                if (ql < 0.0) ++nl_neg;
                            }
                        }
                        if (AMv.defined()) {
                            double qr = v.dot(AMv).to(torch::kCPU).item<double>() / vv;
                            if (std::isfinite(qr)) {
                                ++nr_ok; qr_min = std::min(qr_min, qr);
                                if (qr < 0.0) ++nr_neg;
                            }
                        }
                    }
                    // R13.14 (red team round 5, R5-12): the coordinate label was a HARDCODED
                    // constant stamped over three different operators, and it was FALSE on a
                    // supported path. `gmres_op` is S^-1 A S only when the scaling was
                    // initialised; otherwise it is `apply_jacobian`, i.e. RAW coordinates -- and
                    // this probe fires either way. The sibling emitter in this same file derives
                    // the field it needs; this one asserted it. Derived now.
                    //
                    // The three arms are also three DIFFERENT operators: v'AM^-1 v / v'v is not
                    // a Rayleigh quotient of any similarity transform of A (M^-1 is applied on
                    // one side only), so one `operator_coordinates=` cannot cover all of them.
                    // Each arm names its own operator, and `neg=` carries a denominator on every
                    // arm so the three are comparable without going back to `samples=`.
                    std::cerr << "SDIRK3_NUMRANGE"
                              << " operator_coordinates="
                              << (scaling_initialized_ ? "S_krylov" : "raw_unscaled")
                              << " one_sided_precond_arms=AM^-1,M^-1A"
                              << " stage=" << stage
                              << " samples=" << n_ok << "/" << n_samp
                              << " A: q_min=" << q_min << " q_max=" << q_max
                              << " neg=" << n_neg << "/" << n_ok;
                    // R13.18: the random arm above is a ONE-SIDED sample. Random directions in
                    // high dimensions concentrate, so "negative on all 24" is no more a proof of
                    // definiteness than the probe's own caveat says "neg==0 proves nothing" is a
                    // proof of the converse. A spanning check: one direction per variable block.
                    // If blocks disagree in sign, W_phys(A) straddles the origin and the physical
                    // operator IS indefinite; if they agree, the sign is a property of the whole
                    // state, not of a coordinate choice.
                    std::string phys_block_rows;
                    int nb_pos = 0, nb_neg = 0;
                    // R13.20 -- TWO defects fixed here.
                    //
                    // (1) HARD-CONSTRAINT VIOLATION: the two `.item<double>()` calls below ran
                    //     OUTSIDE any NoGradGuard. `ng_red` is declared inside the sample loop
                    //     and its scope closed with that iteration; this loop is after it. The
                    //     repo's standing rule is that every `.item()` sits in a guard, and
                    //     round 9 checked the Taylor probe for exactly this and reported it
                    //     sound without checking this block.
                    //
                    // (2) R9-12 asked whether these six directions are genuinely DISTINCT
                    //     probes, after three of them reported |q-1| identical to six figures.
                    //     Support leakage -- the mechanism the review proposed -- is refuted BY
                    //     CONSTRUCTION: `e_b` is exactly zero outside its block, so <v,Av> reads
                    //     only block-b rows and no other block can contribute to q_b. That is
                    //     now MEASURED (`vout` must be 0) instead of argued, and the response
                    //     norms inside and outside the block are reported beside it -- which is
                    //     where a shared-coefficient explanation for the coincidence would show.
                    //     Until those rows are read, section 13's numbers carry no locality
                    //     verdict and must not be built on again.
                    torch::NoGradGuard ng_blocks;
                    int nb_measured = 0;
                    bool blocks_local = true;
                    if (S_diag_.defined() && layout_initialized_ && cached_layout_.is_exact &&
                        cached_layout_.total_size == R.numel()) {
                        for (const auto& blk : cached_layout_.blocks) {
                            auto e_b = torch::zeros_like(R);
                            e_b.slice(0, blk.start, blk.start + blk.size).fill_(1.0f);
                            torch::Tensor Ae;
                            try { Ae = gmres_op(e_b); } catch (...) { continue; }
                            if (!Ae.defined()) continue;
                            const auto v_p = (S_diag_ * e_b).to(torch::kFloat64);
                            const auto Av_p = (S_diag_ * Ae).to(torch::kFloat64);
                            const double d = v_p.dot(v_p).item<double>();
                            if (!(d > 0.0)) continue;
                            const double qb = v_p.dot(Av_p).item<double>() / d;
                            if (!std::isfinite(qb)) continue;
                            const double v_in =
                                v_p.slice(0, blk.start, blk.start + blk.size).norm().item<double>();
                            const double v_all = v_p.norm().item<double>();
                            const double v_out =
                                std::sqrt(std::max(0.0, v_all * v_all - v_in * v_in));
                            const double Av_in =
                                Av_p.slice(0, blk.start, blk.start + blk.size).norm().item<double>();
                            const double Av_all = Av_p.norm().item<double>();
                            const double Av_out =
                                std::sqrt(std::max(0.0, Av_all * Av_all - Av_in * Av_in));
                            if (v_out > 0.0) blocks_local = false;
                            ++nb_measured;
                            if (qb > 0.0) ++nb_pos; else if (qb < 0.0) ++nb_neg;
                            const std::string nm(blk.name);
                            phys_block_rows += " qphys_" + nm + "=" + std::to_string(qb) +
                                               " qphys_" + nm + "_vout=" + std::to_string(v_out) +
                                               " qphys_" + nm + "_Avin=" + std::to_string(Av_in) +
                                               " qphys_" + nm + "_Avout=" + std::to_string(Av_out);
                        }
                    }
                    // The raw physical operator, on the same directions.
                    if (np_ok > 0) {
                        std::cerr << " | A_physical(raw, unscaled): q_min=" << qp_min
                                  << " q_max=" << qp_max
                                  << " neg=" << np_neg << "/" << np_ok
                                  << " indefinite=" << (qp_min < 0.0 ? 1 : 0)
                                  << phys_block_rows
                                  << " phys_blocks_pos=" << nb_pos
                                  << " phys_blocks_neg=" << nb_neg
                                  // Blocks of BOTH signs is a witness that the physical numerical
                                  // range straddles the origin. All one sign is not proof of the
                                  // converse -- it is a spanning sample, not a bound.
                                  << " phys_straddles_origin="
                                  << ((nb_pos > 0 && nb_neg > 0) ? 1 : 0)
                                  // R13.20 (round 9, R9-12): the probe's OWN precondition,
                                  // measured. `blocks=0` means the block scan did not run, and
                                  // then `local` says so rather than printing the initialiser
                                  // as a verdict -- the class this file has been fixing since
                                  // round 5.
                                  << " phys_blocks_measured=" << nb_measured
                                  << " phys_blocks_local="
                                  << (nb_measured == 0 ? "unmeasured"
                                                       : (blocks_local ? "1" : "0"));
                    }
                    if (nr_ok > 0) {
                        std::cerr << " | AM^-1(production): q_min=" << qr_min
                                  << " neg=" << nr_neg << "/" << nr_ok;
                    }
                    if (nl_ok > 0) {
                        std::cerr << " | M^-1A: q_min=" << ql_min << " neg=" << nl_neg
                                  << "/" << nl_ok;
                    }
                    // 9F.D121 (review 10, 10.1): both previous claims were too strong.
                    //
                    // q(v) = v'Av/v'v sees ONLY the symmetric part, so q clustered near 1 does
                    // NOT mean well-conditioned. Counterexample: A = I + bK with K skew-symmetric
                    // gives v'Kv = 0 for every real v, hence q == 1 EXACTLY for any b, however
                    // large -- while the singular values and the non-normality are nothing like
                    // 1. It says the symmetric part's scale is ~1. Nothing about conditioning,
                    // non-normality, GMRES convergence, or step stability follows.
                    //
                    // And a negative q is a witness that the UNPRECONDITIONED symmetric part is
                    // indefinite in the current coordinates. It does not by itself exclude every
                    // one-sided SPD preconditioner.
                    std::cerr << "  (q sees only the SYMMETRIC part: q~1 does NOT imply"
                                 " well-conditioned -- I+bK with K skew gives q==1 for any b."
                                 " neg>0 witnesses an indefinite symmetric part in these"
                                 " coordinates; neg==0 proves NOTHING)"
                              << std::endl << std::flush;
                }
            }

            // 9F.D120: WHICH BLOCK does the preconditioner suppress?
            //
            // Measured at stage 2/3 with the corrected operator: |K|_ph is ~1300x BELOW the
            // |F|_ph it must converge to when M is on, and the right order when M is off. That
            // says M suppresses the ph correction, but not WHERE or BY HOW MUCH.
            //
            // So probe M directly on block-structured vectors, the way the operator/precond
            // guidance says to: put a random vector in ONE block, zero elsewhere, apply M^-1,
            // and report (a) the gain in that block, ||z_q||/||v_q||, and (b) the leakage into
            // every other block. A block whose gain is ~0 is annihilated; a block whose gain is
            // enormous is amplified. Both are visible, and the leakage row shows the coupling M
            // actually models rather than the one it is documented to.
            //
            // This is the M^-1 A e_k ~ e_k check specialised to blocks, and it needs no
            // reference solution -- it is a property of M alone.
            {
                static const bool blockgain_on = [](){
                    const char* e = std::getenv("WRF_SDIRK3_PRECOND_BLOCK_GAIN");
                    return e && *e && std::string(e) != "0" && std::string(e) != "false";
                }();
                if (blockgain_on && newton_iter == 0 && probe_blockgain_stage_ != stage &&
                    gmres_M_inv && cached_layout_.is_valid() &&
                    cached_layout_.total_size == R.numel()) {
                    probe_blockgain_stage_ = stage;
                    auto bg_gen = at::detail::createCPUGenerator(
                        0x810C6A1ULL + static_cast<uint64_t>(stage));
                    // Several random draws, so a single unlucky direction cannot carry a
                    // conclusion. Structured directions are NOT offered here: the obvious ones
                    // (uniform, alternating-in-z, single-level) are horizontally uniform, which
                    // is a null space of the horizontal terms -- they return cos = 1 for a
                    // trivial reason and say nothing about vertical structure.
                    const char* dir_names[] = {"rand1", "rand2", "rand3"};
                    for (const auto& blk : cached_layout_.blocks) {
                      for (const char* dir_name : dir_names) {
                        torch::Tensor v;
                        {
                            torch::NoGradGuard ng_bg;
                            v = torch::zeros_like(R);
                            auto bv = torch::randn({blk.size}, bg_gen,
                                                   R.options().device(torch::kCPU));
                            v.slice(0, blk.start, blk.start + blk.size)
                                .copy_(bv.to(R.device(), R.scalar_type()));
                            v = v.detach();
                        }
                        // Outside any grad guard: M^-1 may build a graph.
                        torch::Tensor z, Az, Av_only;
                        try {
                            z = gmres_M_inv(v);
                            // 9F.D124 (review 13): the judgement quantity is NOT the M^-1 gain.
                            // A preconditioner approximates A^-1, not I, so a small gain can be
                            // exactly right for a stiff row -- my "486x suppression" reading
                            // assumed otherwise. What matters is how close A M^-1 is to the
                            // identity, in the RIGHT-preconditioned order production uses.
                            if (gmres_op) Az = gmres_op(z);
                            // 9F.D125: A ALONE on the same block vector. This is what separates
                            // "M's row is wrong" from "A's row is degenerate". If A v_q keeps the
                            // block (gain ~1) while A M^-1 v_q loses it, the defect is M.
                            if (gmres_op) Av_only = gmres_op(v);
                        } catch (...) { continue; }
                        if (!z.defined()) continue;
                        torch::NoGradGuard ng_bg2;
                        double vin = v.slice(0, blk.start, blk.start + blk.size)
                                       .norm().to(torch::kCPU).item<double>();
                        if (!(vin > 0.0)) continue;
                        std::cerr << "SDIRK3_PRECOND_BLOCK_GAIN stage=" << stage
                                  << " in=" << blk.name << " dir=" << dir_name
                                  << " gain=" << (z.slice(0, blk.start, blk.start + blk.size)
                                                    .norm().to(torch::kCPU).item<double>() / vin);
                        if (Az.defined()) {
                            // ||(A M^-1 v_q - v_q)|| restricted to block q, over ||v_q||.
                            // ~0 means A M^-1 acts as the identity on this block, which is what
                            // GMRES needs. Large means the preconditioner does not invert this
                            // row, whatever its gain happens to be.
                            auto d = Az - v;
                            // ||A M^-1 v_q|| itself, so "annihilates" stops being an inference
                            // from err~1 and becomes a direct reading.
                            std::cerr << " AMinv_norm="
                                      << (Az.norm().to(torch::kCPU).item<double>() / vin);
                            std::cerr << " AMinv_id_err="
                                      << (d.slice(0, blk.start, blk.start + blk.size)
                                            .norm().to(torch::kCPU).item<double>() / vin)
                                      << " AMinv_id_err_total="
                                      << (d.norm().to(torch::kCPU).item<double>() / vin);
                        }
                        if (Av_only.defined()) {
                            // Directional summaries of P_q A E_q. rayleigh_q is the Rayleigh
                            // quotient v'A_qq v / v'v for THIS v -- not the block operator, not
                            // an eigenvalue, and NOT a preconditioner target: for a coupled
                            // Schur preconditioner A_qq = 1 does not imply (A^-1)_qq = 1.
                            // alignment_cosine near 0 says v is not an eigendirection of A_qq;
                            // it does not establish non-normality (diag(1,100) is symmetric and
                            // rotates generic vectors just as well).
                            {
                                torch::NoGradGuard ng_rq;
                                auto vq = v.slice(0, blk.start, blk.start + blk.size);
                                auto aq = Av_only.slice(0, blk.start, blk.start + blk.size);
                                double vv = vq.dot(vq).to(torch::kCPU).item<double>();
                                double an = aq.norm().to(torch::kCPU).item<double>();
                                if (vv > 0.0) {
                                    double vav = vq.dot(aq).to(torch::kCPU).item<double>();
                                    std::cerr << " rayleigh_q=" << (vav / vv);
                                    // cos between (Av)_q and v_q, for this direction only.
                                    if (an > 0.0) {
                                        std::cerr << " alignment_cosine="
                                                  << (vav / (std::sqrt(vv) * an));
                                    }
                                }
                            }
                            // A's OFF-DIAGONAL COLUMN: ||(A v_q)_p|| / ||v_q|| for p != q. With v
                            // supported only on q, holding the INPUT fixed and reading the output
                            // across p gives ||A_pq|| -- the entries of COLUMN q.
                            //
                            // This said "row", which SWAPS the two indices: input ph / output mu
                            // is A_mu_ph, but read as ph's row it becomes A_ph_mu -- the exact
                            // pair this probe exists to compare. Each entry now prints its full
                            // name, so the index order cannot be reconstructed wrongly.
                            {
                                torch::NoGradGuard ng_col;
                                std::cerr << " Acol[in=" << blk.name << "]:";
                                for (const auto& ob : cached_layout_.blocks) {
                                    if (ob.name == blk.name) continue;
                                    std::cerr << " A_" << ob.name << "_" << blk.name << "="
                                              << (Av_only.slice(0, ob.start, ob.start + ob.size)
                                                    .norm().to(torch::kCPU).item<double>() / vin);
                                }
                            }
                            std::cerr << " A_gain="
                                      << (Av_only.slice(0, blk.start, blk.start + blk.size)
                                            .norm().to(torch::kCPU).item<double>() / vin)
                                      << " A_norm_total="
                                      << (Av_only.norm().to(torch::kCPU).item<double>() / vin);
                        }
                        std::cerr << " leak:";
                        for (const auto& ob : cached_layout_.blocks) {
                            if (ob.name == blk.name) continue;
                            std::cerr << " " << ob.name << "="
                                      << (z.slice(0, ob.start, ob.start + ob.size)
                                            .norm().to(torch::kCPU).item<double>() / vin);
                        }
                        std::cerr << std::endl << std::flush;
                      }
                    }
                }
            }

            try {
                // FIX 2026-01-29: Detach R before GMRES to prevent dual tangent propagation.
                // Apply scaling to RHS. v20.14r26: No halo mask on RHS — GMRES solves full system.
                // Halo zeroing is applied to dK post-GMRES and K pre-residual-eval instead.
                torch::Tensor gmres_rhs;
                if (scaling_initialized_) {
                    gmres_rhs = -(S_inv_diag_ * R).detach();
                } else {
                    gmres_rhs = (-R).detach();
                }

                // r50-F4: D block-scaling REMOVED. Was redundant with S-scaling (both
                // normalize per-block RHS norms). With F3's monotonic S update, S already
                // tracks growing components. D also lacked preconditioner conjugation
                // (M⁻¹ not conjugated with D), making it theoretically inconsistent.

                if (stage >= static_cast<int>(gmres_warmstart_stage_.size())) {
                    const int new_size = stage + 4;
                    gmres_warmstart_stage_.resize(new_size);
                    gmres_warmstart_relerr_stage_.resize(new_size, 1.0f);
                    gmres_warmstart_varpc_stage_.resize(new_size, false);
                    gmres_warmstart_prev_stage_.resize(new_size);
                    gmres_warmstart_prev_relerr_stage_.resize(new_size, 1.0f);
                    gmres_warmstart_prev_varpc_stage_.resize(new_size, false);
                }

                torch::Tensor gmres_x0 = torch::zeros_like(K).detach();
                if (wrf::sdirk3::g_sdirk3_config.gmres_warmstart &&
                    stage >= 0 && stage < static_cast<int>(gmres_warmstart_stage_.size())) {
                    const auto& x0_cached_unscaled = gmres_warmstart_stage_[stage];
                    const float prev_rel = gmres_warmstart_relerr_stage_[stage];
                    const bool prev_varpc = gmres_warmstart_varpc_stage_[stage];
                    const bool shape_ok =
                        x0_cached_unscaled.defined() &&
                        x0_cached_unscaled.sizes() == K.sizes() &&
                        x0_cached_unscaled.device() == K.device() &&
                        x0_cached_unscaled.scalar_type() == K.scalar_type();
                    const bool quality_ok = (prev_rel <= wrf::sdirk3::g_sdirk3_config.gmres_warmstart_quality_gate);
                    const bool variable_pc_ok = !prev_varpc;
                    if (shape_ok && quality_ok && variable_pc_ok) {
                        gmres_x0 = scaling_initialized_
                            ? (S_inv_diag_ * x0_cached_unscaled).detach()
                            : x0_cached_unscaled.detach();
                        gmres_warmstart_used = true;
                    } else if (wrf::sdirk3::g_sdirk3_config.solver_telemetry && newton_iter == 0) {
                        std::cerr << "[GMRES WARMSTART] stage=" << stage
                                  << " disabled (shape_ok=" << shape_ok
                                  << ", prev_rel=" << prev_rel
                                  << ", gate=" << wrf::sdirk3::g_sdirk3_config.gmres_warmstart_quality_gate
                                  << ", prev_varpc=" << (prev_varpc ? "yes" : "no")
                                  << ")\n";
                    }
                }

                // INN-assisted x0 hook (default-off):
                // Use a bounded interpolation candidate and keep strict fallback to base x0.
                // This is intentionally conservative until an external INN artifact is wired.
                const auto& cfg_inn = wrf::sdirk3::g_sdirk3_config;
                // Numeric reason code for telemetry consumers:
                // 0 off, 1 disabled, 2 no_base_warmstart, 3 cache_oob,
                // 4 prev_history_missing, 5 prev_stage_invalid, 6 beta_zero,
                // 7 candidate, 8 ood_reject, 9 reserved(gate_skip_ad_mode, legacy),
                // 10 gate_pass, 11 gate_reject_non_degrade,
                // 12 accepted, 13 accepted_tol_ramped, 14 gate_reject_quality,
                // 15 gate_reject_both.
                const auto set_inn_status = [&](const char* status, int code) {
                    gmres_inn_status = status;
                    gmres_inn_reason_code = code;
                };
                torch::Tensor gmres_x0_base = gmres_x0.detach();
                if (cfg_inn.inn_warmstart_enable) {
                    set_inn_status("disabled", 1);
                    if (!gmres_warmstart_used) {
                        set_inn_status("no_base_warmstart", 2);
                    } else if (stage >= static_cast<int>(gmres_warmstart_stage_.size())) {
                        set_inn_status("cache_oob", 3);
                    } else {
                        // Shape-safe source: same-stage temporal history (one-step lag).
                        // Avoid stage-1 cache mix that caused frequent shape mismatches.
                        const auto& prev_stage_cache = gmres_warmstart_prev_stage_[stage];
                        const float prev_stage_rel = gmres_warmstart_prev_relerr_stage_[stage];
                        const bool prev_stage_varpc = gmres_warmstart_prev_varpc_stage_[stage];
                        const bool prev_defined = prev_stage_cache.defined();
                        const bool prev_shape_ok =
                            prev_defined &&
                            prev_stage_cache.sizes() == K.sizes() &&
                            prev_stage_cache.device() == K.device() &&
                            prev_stage_cache.scalar_type() == K.scalar_type();
                        const bool prev_quality_ok =
                            (prev_stage_rel <= cfg_inn.gmres_warmstart_quality_gate);
                        const bool prev_varpc_ok = !prev_stage_varpc;
                        gmres_inn_prev_shape_ok = prev_shape_ok;
                        gmres_inn_prev_quality_ok = prev_quality_ok;
                        gmres_inn_prev_varpc_ok = prev_varpc_ok;
                        gmres_inn_prev_rel = prev_stage_rel;

                        if (!prev_defined) {
                            set_inn_status("prev_history_missing", 4);
                        } else if (!(prev_shape_ok && prev_quality_ok && prev_varpc_ok)) {
                            set_inn_status("prev_stage_invalid", 5);
                        } else {
                            const float beta = std::clamp(cfg_inn.inn_beta_max, 0.0f, 1.0f);
                            if (!(beta > 0.0f)) {
                                set_inn_status("beta_zero", 6);
                            } else {
                                torch::Tensor prev_stage_scaled = scaling_initialized_
                                    ? (S_inv_diag_ * prev_stage_cache).detach()
                                    : prev_stage_cache.detach();
                                torch::Tensor gmres_x0_cand =
                                    ((1.0f - beta) * gmres_x0_base + beta * prev_stage_scaled).detach();
                                gmres_inn_candidate_built = true;
                                set_inn_status("candidate", 7);
                                gmres_inn_x0_base_norm = safe_norm(gmres_x0_base);
                                gmres_inn_x0_cand_norm = safe_norm(gmres_x0_cand);
                                gmres_inn_x0_delta_norm = safe_norm(gmres_x0_cand - gmres_x0_base);
                                gmres_inn_x0_rel_delta =
                                    gmres_inn_x0_delta_norm / std::max(gmres_inn_x0_base_norm, 1.0e-20f);

                                bool ood_reject = false;
                                if (cfg_inn.inn_ood_guard_enable) {
                                    if (!std::isfinite(gmres_inn_x0_rel_delta) || gmres_inn_x0_rel_delta > 1.0f) {
                                        ood_reject = true;
                                        set_inn_status("ood_reject", 8);
                                    }
                                }

                                if (!ood_reject) {
                                    bool accept_candidate = true;
                                    if (cfg_inn.inn_residual_gate_enable) {
                                        struct INNResidualDiag {
                                            float norm = 0.0f;
                                            float ru_share = 0.0f;
                                            float rw_share = 0.0f;
                                            float ph_share = 0.0f;
                                        };
                                        auto residual_diag = [&](const torch::Tensor& x0_try) -> INNResidualDiag {
                                            INNResidualDiag d;
                                            float x0_norm = safe_norm(x0_try);
                                            torch::Tensor r_try;
                                            if (x0_norm < 1.0e-14f) {
                                                r_try = gmres_rhs;
                                            } else {
                                                r_try = gmres_rhs - gmres_op(x0_try);
                                            }
                                            d.norm = safe_norm(r_try);
                                            if (layout_initialized_ &&
                                                cached_layout_.is_exact &&
                                                cached_layout_.total_size == r_try.numel()) {
                                                auto r_cpu = r_try.detach().to(torch::kCPU).contiguous();
                                                float total_sq = 0.0f;
                                                float ru_sq = 0.0f;
                                                float rw_sq = 0.0f;
                                                float ph_sq = 0.0f;
                                                for (const auto& blk : cached_layout_.blocks) {
                                                    if (blk.start + blk.size > r_cpu.numel()) continue;
                                                    auto r_blk = r_cpu.slice(0, blk.start, blk.start + blk.size);
                                                    float blk_n = safe_norm(r_blk);
                                                    float blk_sq = blk_n * blk_n;
                                                    total_sq += blk_sq;
                                                    if (blk.name == "ru") {
                                                        ru_sq = blk_sq;
                                                    } else if (blk.name == "rw") {
                                                        rw_sq = blk_sq;
                                                    } else if (blk.name == "ph") {
                                                        ph_sq = blk_sq;
                                                    }
                                                }
                                                if (total_sq > 1.0e-30f) {
                                                    d.ru_share = ru_sq / total_sq;
                                                    d.rw_share = rw_sq / total_sq;
                                                    d.ph_share = ph_sq / total_sq;
                                                }
                                            }
                                            return d;
                                        };

                                        const INNResidualDiag base_diag = residual_diag(gmres_x0_base);
                                        const INNResidualDiag cand_diag = residual_diag(gmres_x0_cand);
                                        gmres_inn_r_base = base_diag.norm;
                                        gmres_inn_r_cand = cand_diag.norm;
                                        gmres_inn_ru_share_base = base_diag.ru_share;
                                        gmres_inn_ru_share_cand = cand_diag.ru_share;
                                        gmres_inn_rw_share_base = base_diag.rw_share;
                                        gmres_inn_rw_share_cand = cand_diag.rw_share;
                                        gmres_inn_ph_share_base = base_diag.ph_share;
                                        gmres_inn_ph_share_cand = cand_diag.ph_share;
                                        gmres_inn_q =
                                            (gmres_inn_r_base - gmres_inn_r_cand) /
                                            std::max(gmres_inn_r_base, 1.0e-20f);

                                        // Numerical-noise tolerant gate:
                                        // q around +/-1e-7 can appear from rounding
                                        // even when residuals are effectively equal.
                                        const float inn_gate_rel_tol = std::max(cfg_inn.inn_gate_rel_tol, 0.0f);
                                        const float inn_gate_q_noise = std::max(cfg_inn.inn_gate_q_noise, 0.0f);
                                        const float r_base_safe = std::max(gmres_inn_r_base, 1.0e-20f);
                                        const float rel_diff = (gmres_inn_r_cand - gmres_inn_r_base) / r_base_safe;
                                        gmres_inn_gate_rel_diff = rel_diff;
                                        const bool non_degrade =
                                            std::isfinite(gmres_inn_r_cand) &&
                                            std::isfinite(gmres_inn_q) &&
                                            (rel_diff <= inn_gate_rel_tol);
                                        const bool quality_ok =
                                            (gmres_inn_q >= cfg_inn.inn_q_min) ||
                                            (cfg_inn.inn_q_min <= 0.0f &&
                                             gmres_inn_q >= -inn_gate_q_noise);
                                        gmres_inn_gate_non_degrade = non_degrade;
                                        gmres_inn_gate_quality_ok = quality_ok;
                                        accept_candidate = non_degrade && quality_ok;
                                        gmres_inn_gate_pass = accept_candidate;
                                        int gate_reject_reason = 11; // non_degrade fail
                                        if (!non_degrade && !quality_ok) {
                                            gate_reject_reason = 15; // both fail
                                        } else if (non_degrade && !quality_ok) {
                                            gate_reject_reason = 14; // quality fail
                                        }
                                        set_inn_status(accept_candidate ? "gate_pass" : "gate_reject",
                                                       accept_candidate ? 10 : gate_reject_reason);
                                    }

                                    if (accept_candidate) {
                                        gmres_x0 = gmres_x0_cand;
                                        gmres_inn_used = true;
                                        gmres_inn_gate_pass = true;
                                        if (gmres_inn_status == "candidate") set_inn_status("accepted", 12);
                                    }
                                }
                            }
                        }
                    }

                    // Optional conservative tol ramp: only when INN proposal is accepted.
                    // Progressive coupling with INN quality q:
                    // - q<=0  : no extra tightening (gamma_eff ~ 1)
                    // - q>=1  : max tightening (gamma_eff = configured gamma)
                    // This keeps default-off behavior and degrades gracefully when q is small.
                    if (cfg_inn.inn_tol_ramp_enable && gmres_inn_used && stage >= 2) {
                        const float gamma = std::clamp(cfg_inn.inn_tol_ramp_gamma, 1.0e-3f, 1.0f);
                        if (gamma < 1.0f) {
                            float q_pos = 1.0f;
                            if (std::isfinite(gmres_inn_q)) {
                                q_pos = std::clamp(gmres_inn_q, 0.0f, 1.0f);
                            }
                            const float gamma_eff = 1.0f - q_pos * (1.0f - gamma);
                            krylov_tol_adaptive = std::max(1.0e-12f, gamma_eff * krylov_tol_adaptive);
                            gmres_inn_tol_ramped = true;
                            set_inn_status("accepted_tol_ramped", 13);
                        }
                    }

                    if (cfg_inn.solver_telemetry && newton_iter == 0 && cfg_inn.debug_level >= 1) {
                        std::cerr << "[GMRES INN] stage=" << stage
                                  << ", status=" << gmres_inn_status
                                  << ", reason_code=" << gmres_inn_reason_code
                                  << ", base_warmstart=" << (gmres_warmstart_used ? "yes" : "no")
                                  << ", built=" << (gmres_inn_candidate_built ? "yes" : "no")
                                  << ", used=" << (gmres_inn_used ? "yes" : "no")
                                  << ", gate_pass=" << (gmres_inn_gate_pass ? "yes" : "no")
                                  << ", prev_shape_ok=" << (gmres_inn_prev_shape_ok ? "yes" : "no")
                                  << ", prev_quality_ok=" << (gmres_inn_prev_quality_ok ? "yes" : "no")
                                  << ", prev_varpc_ok=" << (gmres_inn_prev_varpc_ok ? "yes" : "no")
                                  << ", prev_rel=" << gmres_inn_prev_rel
                                  << ", q=" << gmres_inn_q
                                  << ", r_base=" << gmres_inn_r_base
                                  << ", r_cand=" << gmres_inn_r_cand
                                  << ", x0_base_norm=" << gmres_inn_x0_base_norm
                                  << ", x0_cand_norm=" << gmres_inn_x0_cand_norm
                                  << ", x0_delta_norm=" << gmres_inn_x0_delta_norm
                                  << ", x0_rel_delta=" << gmres_inn_x0_rel_delta
                                  << ", gate_rel_diff=" << gmres_inn_gate_rel_diff
                                  << ", gate_non_degrade=" << (gmres_inn_gate_non_degrade ? "yes" : "no")
                                  << ", gate_quality_ok=" << (gmres_inn_gate_quality_ok ? "yes" : "no")
                                  << ", ru_share_base=" << gmres_inn_ru_share_base
                                  << ", ru_share_cand=" << gmres_inn_ru_share_cand
                                  << ", rw_share_base=" << gmres_inn_rw_share_base
                                  << ", rw_share_cand=" << gmres_inn_rw_share_cand
                                  << ", ph_share_base=" << gmres_inn_ph_share_base
                                  << ", ph_share_cand=" << gmres_inn_ph_share_cand
                                  << ", tol_ramped=" << (gmres_inn_tol_ramped ? "yes" : "no")
                                  << std::endl;
                    }
                }

                // R13.8: the FROZEN LINEAR-SYSTEM A/B, made non-interfering and self-judging.
                //
                // R13.7 froze (A, b, x0) and routed both arms through the same solve_fgmres --
                // a real advance over the run-level comparison it replaced. Two defects made
                // its number unattributable anyway, and both are fixed here.
                //
                // (1) THE PROBE MUTATED WHAT IT MEASURED. Production's preconditioner closures
                //     are `mutable`: precond_func holds a fallback latch (once set, identity
                //     forever) and the defect-correction wrapper evaluates its gate ONLY at
                //     call_count == 0. R13.7 called gmres_M_inv five times before production's
                //     own solve used the same object, so only the first arm ever saw a fresh
                //     preconditioner and production inherited an aged one. Copying the
                //     std::function copies its target's state, so a copy taken HERE -- before
                //     any arm runs, while the closure is still pristine -- is the fresh
                //     preconditioner every arm needs, and restoring from it leaves production
                //     exactly where it would have been.
                //
                // (2) THE VERDICT WAS NEVER CONSUMED. R13.7 added ab_attributable() to stop an
                //     unattributable A/B being read as a result, and did not call it from this
                //     emitter. Sixth occurrence of that shape in this tree. It is called now,
                //     and every conclusion-shaped field is suppressed when it refuses.
                // R13.9: WHICH Newton iteration to freeze. Iteration 0 is the cleanest
                // system but not the one where GMRES fails -- the first-failure ladder
                // measured that at iteration 4 (0-indexed 3) for every adequate budget. The
                // probe cannot know in advance whether the CURRENT iteration will fail, so
                // the target is chosen by the caller from that measurement. Default 0 keeps
                // every earlier record comparable.
                // Strict parse (red team P1-5): atoi("iter3") == 0 would silently freeze
                // iteration 0 under a run labelled "iteration 3" -- a scientific knob that
                // silently runs the baseline is worse than one that fails.
                static const int ab_target_iter = [] {
                    const char* e = std::getenv("WRF_SDIRK3_FROZEN_MI_AB_ITER");
                    if (!e || !*e) return 0;
                    char* end = nullptr;
                    const long v = std::strtol(e, &end, 10);
                    TORCH_CHECK(end && *end == '\0' && v >= 0 && v < 1000,
                        "WRF_SDIRK3_FROZEN_MI_AB_ITER='", e,
                        "' is not a non-negative integer");
                    return static_cast<int>(v);
                }();
                // Out of range for THIS solve: say so once, rather than emit nothing.
                if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_FROZEN_MI_AB") &&
                    newton_iter == 0 && ab_target_iter >= options_.max_newton_iter) {
                    std::cerr << "SDIRK3_FROZEN_AB_SYSTEM stage=" << stage
                              << " ab_valid=0 ab_reason=target_iteration_unreachable"
                              << " requested_iter=" << ab_target_iter
                              << " max_newton_iter=" << options_.max_newton_iter
                              << "  (no A/B will be recorded in this solve)" << std::endl;
                }
                if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_FROZEN_MI_AB") &&
                    gmres_M_inv && newton_iter == ab_target_iter) {
                    frozen_ab_fired_this_solve_ = true;
                    torch::NoGradGuard ng_ab;
                    // R13.10 (red team P0-2): NOT a copy of gmres_M_inv. On the scaled path
                    // gmres_M_inv is a [&] wrapper around the single M_inv, so copying it
                    // copies a REFERENCE to the one fallback latch -- every "fresh" row and
                    // production shared it. The reported numbers survived only because the
                    // latch never fired (the counter proves that), not because the mechanism
                    // existed. Each row now builds its own wrapper around its own BY-VALUE
                    // copy of M_inv, so the latch is per row by construction and production's
                    // gmres_M_inv is never touched by the probe at all.
                    auto make_fresh_M = [&]() -> std::function<torch::Tensor(const torch::Tensor&)> {
                        if (scaling_initialized_) {
                            return [M_copy = M_inv, S = S_diag_, Si = S_inv_diag_]
                                   (const torch::Tensor& v) -> torch::Tensor {
                                return Si * M_copy(S * v);
                            };
                        }
                        return M_inv;   // by value: its own latch
                    };
                    const int fallbacks_before = precond_fallback_count_;
                    // The probe applies M thousands of times over the ladder; production's
                    // per-solve call count must not carry that.
                    const long long precond_total_calls_before = precond_total_calls_;
                    // R13.14 (red team round 5, R5-9, P0): the probe also issues thousands of
                    // MATVECS, and every one bumps the per-iteration JVP counters that
                    // production's own one-shot diagnostics gate on. Unrestored, that means:
                    // production's JVP diagnostics are entirely suppressed on the probe
                    // iteration (they fire on jvp_call_count <= 3); the `== 1 && newton_iter ==
                    // 0` one-shot fires on the PROBE's first arm's first matvec and is reported
                    // as production's; the FD-consistency sampler samples the probe's matvecs;
                    // and the "[Newton] JVP calls / avg" summary reports the probe's count and
                    // mean. All while the row attests probe_noninterfering=1. The PR-9B
                    // directional check ~500 lines below already does this correctly -- it
                    // snapshots eleven counters and forces the call count high so the one-shots
                    // cannot fire during it. The probe that runs FIRST and issues far more
                    // matvecs restored none of them.
                    const int  sv_jvp_calls   = jvp_call_count;
                    const int  sv_jvp_ad      = jvp_ad_calls;
                    const int  sv_jvp_fd      = jvp_fd_calls;
                    const int  sv_jvp_fdf     = jvp_fd_forward_calls;
                    const int  sv_jvp_fdc     = jvp_fd_central_calls;
                    const double sv_jvp_ms    = total_jvp_time_ms;
                    // Same guard the sibling uses: past every one-shot threshold, so the
                    // probe's own matvecs cannot trigger a production diagnostic.
                    jvp_call_count = std::max(jvp_call_count, 1000);
                    // R13.14 (round 5, R5-10, P0): and the shared preconditioner-event flag.
                    // An arm's M tripping its ratio guard sets it, it is never restored on the
                    // arm path, and it is read at stage 3 / iteration 0 to decide
                    // `stage3_hopeless_detected` -- which feeds `stage3_hopeless_streak_` and
                    // `stage3_hopeless_budget_mode_`, MEMBERS THAT PERSIST ACROSS SOLVES AND
                    // TIMESTEPS. The probe's default target IS stage-any / iteration 0, so the
                    // probe could steer the rest of the run. `ab_valid=0` would have said "you
                    // may not compare the arms"; it never said "this run's trajectory was
                    // altered". Snapshot and restore, so it cannot be.
                    const bool sv_variable_pc_event =
                        variable_pc_event ? *variable_pc_event : false;
                    // R13.15 (external review P0-1/P0-2): BEHAVIOURAL fingerprints of the two
                    // shared objects. `make_fresh_M()` copies the mutable wrapper but the
                    // UnifiedPreconditioner underneath is one instance for every arm, and the
                    // operator closure is deliberately one instance too. Both were reported as
                    // "fresh per arm", hardcoded. What the comparison needs is not freshness but
                    // that neither object MOVED between arms -- so apply each to one fixed probe
                    // vector before and after the ladder and compare the outputs. A state change
                    // that could alter a result is caught; one that could not is ignored, which
                    // is the right sensitivity for this question.
                    torch::Tensor fp_probe, A_fp_before, M_fp_before;
                    std::function<torch::Tensor(const torch::Tensor&)> M_fp_probe;
                    if (gmres_rhs.defined() && gmres_rhs.numel() > 0) {
                        const double bn = gmres_rhs.detach().to(torch::kFloat64)
                                              .norm().item<double>();
                        if (bn > 0.0) {
                            fp_probe = (gmres_rhs.detach() / static_cast<float>(bn)).clone();
                            A_fp_before = gmres_op(fp_probe).detach().clone();
                            // R13.17 (external review P1-3): fingerprint a PRISTINE COPY, never
                            // production's closure. Production M carries the amplification
                            // guard's mutable `fallback_locked`; if the probe vector trips it,
                            // the very act of taking the "before" reading locks production to the
                            // identity for the rest of the run -- a latch no counter restore can
                            // undo -- and then "before" and "after" agree BECAUSE both are the
                            // identity, printing preconditioner_state_unchanged=1 from a
                            // preconditioner the probe itself changed. Observing a state must not
                            // be done by exercising it.
                            M_fp_probe = make_fresh_M();
                            M_fp_before = M_fp_probe ? M_fp_probe(fp_probe).detach().clone()
                                                     : torch::Tensor();
                        }
                    }
                    // fd-fallback count BEFORE the arms; compared after (red team P1-2: reading
                    // it once before the arms made a fallback inside an arm invisible).
                    const long long fd_before =
                        wrf::sdirk3::g_jvp_fd_fallback_count.load(std::memory_order_relaxed);
                    auto identity_M =
                        std::function<torch::Tensor(const torch::Tensor&)>(
                            [](const torch::Tensor& v) { return v; });
                    // R13.9: a THIRD arm, from the per-block measurement. M helped ru/rv/t and
                    // did nothing for rw/ph/mu, while the identity took those three down
                    // 73/38/84%. So: M where it works, identity where it does not. This is a
                    // diagnostic arm in an opt-in probe, not a production change -- it asks
                    // whether the block-level picture composes, before anything is built on it.
                    // P1-5: a receipt that the selection actually engaged. Without it the arm
                    // silently degrades to plain M on a layout mismatch and the row still
                    // prints arm=Msel.
                    auto msel_engaged = std::make_shared<bool>(false);
                    auto selective_M =
                        std::function<torch::Tensor(const torch::Tensor&)>(
                            [this, M_own = make_fresh_M(), msel_engaged]
                            (const torch::Tensor& v) {
                                auto z = M_own(v);
                                if (!cached_layout_.is_exact ||
                                    cached_layout_.total_size != v.numel()) {
                                    *msel_engaged = false;
                                    return z;
                                }
                                *msel_engaged = true;
                                for (const auto& blk : cached_layout_.blocks) {
                                    const std::string n = blk.name;
                                    if (n == "rw" || n == "ph" || n == "mu") {
                                        z.slice(0, blk.start, blk.start + blk.size)
                                            .copy_(v.slice(0, blk.start, blk.start + blk.size));
                                    }
                                }
                                return z;
                            });

                    // R13.8 (B4): a digest that can see DIRECTION. abs().sum() cannot
                    // separate v from -v or from any permutation -- the same weakness fixed in
                    // carried_state_digest and reintroduced here. Norm, sum and first moment:
                    // the sum is odd under negation, the first moment breaks under reordering.
                    // R13.15 (external review P1-6): an INDEX-AWARE WIDE hash, not a scalar.
                    //
                    // The previous digest was ||x||_2 + 7*sum(x) + 13*sum(i*x_i) -- three signed
                    // reductions collapsed into one double, and the review supplies an explicit
                    // collision: x = (1,-2,1,0) and y = (0,1,-2,1) match on all three. Three
                    // moments cannot separate vectors of any length, so no number of extra
                    // signed projections fixes the class; the fix is to hash the BYTES with an
                    // avalanche, and to include shape/dtype/device so two differently-shaped
                    // tensors with the same bytes cannot agree either.
                    //
                    // The in-process A/B shares tensors, so a collision here could not corrupt a
                    // computation -- but this digest is quoted as provenance, and provenance is
                    // exactly where a weak hash misleads.
                    auto digest_u64 = [](const torch::Tensor& t) -> unsigned long long {
                        unsigned long long h = 1469598103934665603ULL;   // FNV-1a offset basis
                        auto mix = [&h](unsigned long long v) {
                            h ^= v;
                            h *= 1099511628211ULL;
                            h ^= h >> 29;            // avalanche: FNV alone is blind to bit 63
                        };
                        if (!t.defined() || t.numel() == 0) { mix(0xDEADBEEFULL); return h; }
                        mix(static_cast<unsigned long long>(t.dim()));
                        for (int64_t d = 0; d < t.dim(); ++d) {
                            mix(static_cast<unsigned long long>(t.size(d)));
                        }
                        mix(static_cast<unsigned long long>(t.scalar_type()));
                        mix(static_cast<unsigned long long>(t.device().type()));
                        // Contiguous float64 on the CPU so the byte view is well defined and
                        // identical across devices.
                        const auto flat =
                            t.detach().to(torch::kCPU).to(torch::kFloat64).contiguous()
                             .reshape({-1});
                        const double* p = flat.data_ptr<double>();
                        const int64_t n = flat.numel();
                        for (int64_t i = 0; i < n; ++i) {
                            unsigned long long bits = 0;
                            std::memcpy(&bits, &p[i], sizeof(bits));
                            mix(bits);
                            mix(static_cast<unsigned long long>(i));   // index-aware
                        }
                        return h;
                    };
                    // Kept for the printed provenance field, which is a double in the record
                    // format. It is NOT what the agreement flags compare.
                    auto digest = [](const torch::Tensor& t) -> double {
                        if (!t.defined() || t.numel() == 0) return -1.0;
                        const auto t64 = t.detach().to(torch::kFloat64).reshape({-1});
                        const auto idx = torch::arange(t64.numel(), t64.options());
                        return t64.norm().item<double>()
                             + 7.0 * t64.sum().item<double>()
                             + 13.0 * (t64 * idx).sum().item<double>();
                    };

                    // R13.8 (A6): FGMRES PRESUMES A LINEAR OPERATOR. An FD matvec with a
                    // block-dependent epsilon is not one, and the failure is silent -- the
                    // Arnoldi relation simply stops describing what was computed. Measured
                    // here rather than assumed, on the same operator the arms use.
                    double e_repeat = -1.0, e_hom = -1.0, e_add = -1.0;
                    // R13.12 (referee X3): is the IDENTITY term of A = I - h*gamma*J even
                    // resolved in float32? A*v = v - h*gamma*J*v, so the identity contributes
                    // ||v|| out of ||A*v||: identity_frac = ||v||/||A*v||. The operator's own
                    // floating-point noise is e_hom (measured just below -- an upper bound,
                    // since scaling v and A*v each round once). The error ON THE IDENTITY TERM
                    // is therefore e_hom / identity_frac, and if that reaches 1 the identity is
                    // below the noise floor. The campaign's "5-20%" figure was an ESTIMATE from
                    // eps x scale-separation; these two numbers replace it with a measurement.
                    // Measured on the RHS direction b/||b|| as well as a random one, and
                    // reported separately because this operator behaves differently on the two.
                    //
                    // R13.20 (numerics referee, claim 2a): these were named `*_krylov` on the
                    // strength of the comment right here -- "at a cold start b/||b|| IS the first
                    // Arnoldi vector" -- which states a PRECONDITION THE PROBE NEVER MEASURED.
                    // The record the campaign quoted (stage 2, iteration 3) is a WARM start; this
                    // case has a measured r0/||b|| = 1.054, and at a warm start the first Arnoldi
                    // vector is r0/||r0|| = (b - A x0)/||.||, not b/||b||. That is the campaign's
                    // own rule -- a probe that prints a conclusion must carry a verdict over its
                    // own preconditions -- recurring one probe over. Renamed to what it IS (the
                    // RHS direction), with the cold-start condition MEASURED beside it.
                    //
                    // (2b) And even at a cold start this is Arnoldi vector #1. The later basis
                    // vectors, formed by subtracting nearly-parallel projections, are where
                    // cancellation lives and are NOT measured -- so "the directions GMRES builds"
                    // was never earned either. The verdict below says `first_arnoldi`, singular.
                    double identity_frac_rand = -1.0, identity_frac_rhs_dir = -1.0;
                    double e_hom_rhs_dir = -1.0;
                    // MEASURED, not asserted: b/||b|| coincides with the first Arnoldi vector
                    // only when x0 = 0. -1 = the initial guess was not available to check.
                    int rhs_dir_is_first_arnoldi = -1;
                    if (gmres_x0.defined()) {
                        rhs_dir_is_first_arnoldi =
                            (guarded_item<float>(gmres_x0.norm()) == 0.0f) ? 1 : 0;
                    }
                    // Probe-local generator (the file's own D121/D123 rule): the global stream
                    // must not shift for downstream one-shot diagnostics.
                    auto probe_gen = at::detail::createCPUGenerator(
                        0xAB5EEDULL + static_cast<uint64_t>(stage) * 7919ULL +
                        static_cast<uint64_t>(newton_iter));
                    auto probe_randn = [&](const torch::Tensor& like) {
                        auto t = torch::randn(like.sizes(), probe_gen,
                                              like.options().device(torch::kCPU));
                        return t.to(like.device()).to(like.scalar_type());
                    };
                    {
                        auto v = probe_randn(gmres_rhs);
                        auto w = probe_randn(gmres_rhs);
                        const auto Av1 = gmres_op(v), Av2 = gmres_op(v);
                        const auto n_Av = Av1.detach().to(torch::kFloat64).norm().item<double>();
                        auto rel = [&](const torch::Tensor& a, double den) {
                            return den > 0.0
                                ? a.detach().to(torch::kFloat64).norm().item<double>() / den
                                : -1.0;
                        };
                        e_repeat = rel(Av1 - Av2, n_Av);
                        const double alpha = 2.5;
                        e_hom = rel(gmres_op(alpha * v) - alpha * Av1, alpha * n_Av);
                        const auto Aw = gmres_op(w);
                        const double den_add =
                            n_Av + Aw.detach().to(torch::kFloat64).norm().item<double>();
                        e_add = rel(gmres_op(v + w) - Av1 - Aw, den_add);
                        {
                            const double n_v =
                                v.detach().to(torch::kFloat64).norm().item<double>();
                            identity_frac_rand = (n_Av > 0.0) ? n_v / n_Av : -1.0;
                        }
                        // Same pair on the RHS direction b/||b||.
                        if (gmres_rhs.defined()) {
                            const double n_b =
                                gmres_rhs.detach().to(torch::kFloat64).norm().item<double>();
                            if (n_b > 0.0) {
                                auto vb = gmres_rhs.detach() / static_cast<float>(n_b);
                                const auto Avb = gmres_op(vb);
                                const double n_Avb =
                                    Avb.detach().to(torch::kFloat64).norm().item<double>();
                                if (n_Avb > 0.0) {
                                    const double n_vb =
                                        vb.detach().to(torch::kFloat64).norm().item<double>();
                                    identity_frac_rhs_dir = n_vb / n_Avb;
                                    e_hom_rhs_dir =
                                        rel(gmres_op(alpha * vb) - alpha * Avb, alpha * n_Avb);
                                }
                            }
                        }
                    }
                    // The verdict, computed once and printed beside the numbers it is made of.
                    auto resolution = [](double noise, double frac) {
                        return (noise >= 0.0 && frac > 0.0) ? noise / frac : -1.0;
                    };
                    const double identity_resolution_rand = resolution(e_hom, identity_frac_rand);
                    const double identity_resolution_rhs_dir =
                        resolution(e_hom_rhs_dir, identity_frac_rhs_dir);
                    // "Resolved" = the identity term stands above the operator's noise floor.
                    // Unmeasured is NOT resolved: -1 fails the range test rather than passing it.
                    // Consumed by ab_attributable via cmp.identity_resolved below -- the rule
                    // lives in wrf_sdirk3_probe_validity.h so a fixture can reject its negation.
                    const bool identity_resolved_rhs_dir =
                        (identity_resolution_rhs_dir >= 0.0 && identity_resolution_rhs_dir < 1.0);
                    // R13.9 (referee C5): the operator's linearity was measured; the
                    // PRECONDITIONER's was not, and the reading "Krylov space of D^-1 A M^-1"
                    // needs M linear too. Production M^-1 is a tridiagonal/column solve on
                    // coefficients fixed at build time, so it should be linear per solve --
                    // "should be" is the word this probe exists to remove. Measured on the
                    // pristine copy, so the check itself cannot age the closure.
                    double eM_hom = -1.0, eM_add = -1.0;
                    {
                        // N3: the ratio guard inside M shares variable_pc_event with production
                        // through a shared_ptr that by-value copies do not detach, and random
                        // vectors with no physical structure are exactly what trips it. So the
                        // linearity check (a) uses vectors built from r0 -- the structure M is
                        // designed for -- and (b) snapshots the shared flag and the fallback
                        // counter and restores both: this is a measurement of M's algebra, not
                        // an arm, and it must not be able to disqualify the arms.
                        const bool pc_event_before = *variable_pc_event;
                        const int  fallbacks_lin_before = precond_fallback_count_;
                        auto copyM = make_fresh_M();
                        auto v = gmres_rhs.detach() * (1.0 + 0.1 * probe_randn(gmres_rhs));
                        auto w = gmres_rhs.detach().roll(1, 0) *
                                 (1.0 + 0.1 * probe_randn(gmres_rhs));
                        const double alpha = 2.5;
                        const auto Mv = copyM(v), Mw = copyM(w);
                        const double n_Mv = Mv.detach().to(torch::kFloat64).norm().item<double>();
                        auto relM = [&](const torch::Tensor& a, double den) {
                            return den > 0.0
                                ? a.detach().to(torch::kFloat64).norm().item<double>() / den
                                : -1.0;
                        };
                        eM_hom = relM(copyM(alpha * v) - alpha * Mv, alpha * n_Mv);
                        const double den_add =
                            n_Mv + Mw.detach().to(torch::kFloat64).norm().item<double>();
                        eM_add = relM(copyM(v + w) - Mv - Mw, den_add);
                        *variable_pc_event = pc_event_before;
                        precond_fallback_count_ = fallbacks_lin_before;
                    }
                    constexpr double kLinearTol = 1.0e-5;
                    const bool precond_linear =
                        (eM_hom >= 0.0 && eM_hom < kLinearTol) &&
                        (eM_add >= 0.0 && eM_add < kLinearTol);
                    // R13.20 (numerics referee, claim 1C): these three test LINEARITY, and a
                    // wrong-but-linear operator A = J + E passes ALL of them EXACTLY. So
                    // `operator_linear = 1` must never be read as "the operator is accurate" --
                    // it says the matvec is a linear map, deterministically evaluated. The only
                    // instrument in this tree that can see a linear operator defect is
                    // `tau_alpha_over_tau`, and that one is limited to accepted steps and cannot
                    // see a defect in compute_rhs itself (claim 1B).
                    const bool operator_linear =
                        (e_repeat >= 0.0 && e_repeat < kLinearTol) &&
                        (e_hom    >= 0.0 && e_hom    < kLinearTol) &&
                        (e_add    >= 0.0 && e_add    < kLinearTol);
                    const double b_norm = gmres_rhs.defined()
                        ? gmres_rhs.detach().to(torch::kFloat64).norm().item<double>() : -1.0;
                    // R13.9: THE j=0 BASELINE. Every rho below is normalised by ||b||, but the
                    // solve starts from x0, and at a warm-started iteration x0 != 0. A
                    // minimal-residual method reduces ||r|| from ||r0|| = ||b - A x0||, not
                    // from ||b|| -- so rho(j) > 1 says nothing about convergence unless
                    // ||r0||/||b|| is on the record beside it. "Anti-convergent" is earned
                    // only by rho(j) > rho(0), and that comparison needs this number.
                    double rho0_S = -1.0, rho0_phys = -1.0, rho0_D = -1.0;
                    torch::Tensor r0_kept;
                    {
                        torch::Tensor r0 = gmres_rhs.detach();
                        if (gmres_x0.defined() &&
                            gmres_x0.detach().to(torch::kFloat64).norm().item<double>() > 0.0) {
                            r0 = gmres_rhs.detach() - gmres_op(gmres_x0.detach());
                        }
                        r0_kept = r0;
                        if (b_norm > 0.0) {
                            rho0_S = r0.to(torch::kFloat64).norm().item<double>() / b_norm;
                            if (scaling_initialized_ && S_diag_.defined()) {
                                const auto nb = (S_diag_ * gmres_rhs.detach())
                                                    .to(torch::kFloat64).norm().item<double>();
                                if (nb > 0.0) {
                                    rho0_phys = (S_diag_ * r0).to(torch::kFloat64)
                                                    .norm().item<double>() / nb;
                                }
                            }
                        }
                    }
                    // WHICH operator was compared. An FD matvec with a block-dependent epsilon
                    // is not linear, and FGMRES presumes it is -- so the JVP receipt is part
                    // of the attribution, not a footnote.
                    bool jvp_ok = (fd_before == 0);   // finalised after the arms
                    const bool early_stop_off = no_early_stop_enabled();

                    struct Row { const char* arm; int j; double rho; double rho_phys;
                                 double rho_D; float rel; int iters; int term;
                                 long long a_applies; bool d_weighted; bool msel_engaged;
                                 // R13.13 (red team round 4): the arms' inputs, digested INSIDE
                                 // each arm. `same_rhs`/`same_x0` were hardcoded true while b
                                 // and x0 digests were printed once for the whole block -- so a
                                 // reader of `ab_valid=1 ... b_digest=0x..` saw evidence of a
                                 // comparison that never happened, and three of
                                 // ab_attributable's clauses were unreachable from its only
                                 // production caller. Now they are compared.
                                 unsigned long long b_dig; unsigned long long x0_dig;
                                 // R13.15 (external review P1-2): the D the arm ACTUALLY used.
                                 // `d_weighted` was printed and never enforced, so nothing
                                 // checked that every arm weighted by the same D -- and rho0_D
                                 // from the FIRST row was reused as the baseline for all of
                                 // them. An undefined d_inv_used also could not be told apart
                                 // from a D that was requested and failed to arrive.
                                 unsigned long long d_dig; bool d_requested; };
                    enum class Arm { M, I, Sel };
                    std::vector<Row> rows;
                    // R13.8 (A4): equal j is equal ARNOLDI DIMENSION, not equal work -- the M
                    // arm additionally applies M^-1 per direction. Counting the operator
                    // applications makes the cost difference visible instead of implied.
                    long long a_apply_count = 0;
                    auto counting_op = [&](const torch::Tensor& v) {
                        ++a_apply_count;
                        return gmres_op(v);
                    };

                    const int halo_width_for_probe = 0;   // packed 1-D Krylov vectors
                    // Each row gets its OWN fresh preconditioner, so rows are independent of
                    // each other as well as of production. R13.7 ran them through one aging
                    // closure, where only the first row started clean.
                    auto run = [&](Arm which, int m) {
                        const bool use_M = (which == Arm::M);
                        // R13.15 (external review P1-1): reset per ROW. `msel_engaged` is a
                        // shared_ptr read after the arm returns; on a path that exits before
                        // the projection runs (zero iterations, an early termination) the
                        // PREVIOUS row's `true` was still there and was reported as this row's
                        // receipt.
                        if (which == Arm::Sel && msel_engaged) *msel_engaged = false;
                        auto arm_M = (which == Arm::M)   ? make_fresh_M()
                                   : (which == Arm::Sel) ? selective_M
                                                         : identity_M;
                        const char* arm_name = (which == Arm::M)   ? "M"
                                             : (which == Arm::Sel) ? "Msel" : "I";
                        a_apply_count = 0;
                        torch::Tensor d_inv_used;
                        auto res = krylov_methods::solve_fgmres(
                            std::function<torch::Tensor(const torch::Tensor&)>(counting_op),
                            gmres_rhs, gmres_x0, stage, ru_share,
                            /*restart=*/m, /*tol=*/0.0f, /*max_iter=*/1,
                            arm_M,
                            layout_initialized_ ? &cached_layout_ : nullptr,
                            halo_mask_initialized_ ? &halo_mask_ : nullptr,
                            options_.periodic_x, options_.periodic_y,
                            nullptr, nullptr, nullptr, &d_inv_used);
                        double rho = -1.0, rho_phys = -1.0, rho_D = -1.0;
                        if (res.x.defined() && b_norm > 0.0) {
                            // P1-4: in the SAME halo-zeroed norm FGMRES minimises. A no-op on
                            // 1-D packed vectors, which is what makes rel_reported and rho_S
                            // agree to the 4th digit -- but consistency is by construction
                            // now, not by that accident.
                            auto r = gmres_rhs.detach() - gmres_op(res.x.detach());
                            zero_halo_regions(r, halo_width_for_probe,
                                              options_.periodic_x, options_.periodic_y);
                            rho = r.to(torch::kFloat64).norm().item<double>() / b_norm;
                            // R13.8 (A7): rho_S lives in the SCALED Krylov coordinates. The
                            // physical residual is S*r~, and winning in one norm does not
                            // imply winning in the other -- the two weight the blocks
                            // differently by construction.
                            // R13.9: FGMRES's OWN objective, under the weight IT used and
                            // published. The sharp question this answers: a preconditioner can
                            // help the norm the solver minimises while hurting the physical
                            // one, which makes it a preconditioner for the wrong objective
                            // rather than a bad one.
                            if (d_inv_used.defined() && d_inv_used.numel() > 0) {
                                const auto rD = (d_inv_used * r).to(torch::kFloat64);
                                const auto bD =
                                    (d_inv_used * gmres_rhs.detach()).to(torch::kFloat64);
                                const double nbD = bD.norm().item<double>();
                                if (nbD > 0.0) rho_D = rD.norm().item<double>() / nbD;
                            }
                            if (scaling_initialized_ && S_diag_.defined()) {
                                const auto r_phys = (S_diag_ * r).to(torch::kFloat64);
                                const auto b_phys =
                                    (S_diag_ * gmres_rhs.detach()).to(torch::kFloat64);
                                const double nb = b_phys.norm().item<double>();
                                if (nb > 0.0) {
                                    rho_phys = r_phys.norm().item<double>() / nb;
                                }
                            }
                        }
                        // FGMRES's own objective at j=0, from the D it just built. D depends
                        // only on r0, which both arms and every row share, so the first row
                        // that publishes it supplies the baseline for all of them. This is the
                        // one quantity the algorithm GUARANTEES non-increasing -- and it is
                        // measured rather than assumed for the same reason everything else is.
                        if (rho0_D < 0.0 && d_inv_used.defined() && d_inv_used.numel() > 0 &&
                            r0_kept.defined()) {
                            const auto bD = (d_inv_used * gmres_rhs.detach()).to(torch::kFloat64);
                            const double nbD = bD.norm().item<double>();
                            if (nbD > 0.0) {
                                rho0_D = (d_inv_used * r0_kept).to(torch::kFloat64)
                                             .norm().item<double>() / nbD;
                            }
                        }
                        const bool d_weighted = (rho_D >= 0.0);
                        if (!d_weighted && rho >= 0.0) {
                            // Block scaling was off (or refused) for this row: FGMRES's own
                            // objective IS the unweighted norm. Substituted, and SAID (N4).
                            rho_D = rho;
                        }
                        // R13.14 (round 5, R5-8b): `digest` returns a DOUBLE built from a
                        // norm plus signed sums, so converting it to an unsigned integer is
                        // undefined behaviour for any negative value -- and the
                        // undefined-tensor sentinel is -1.0, which every cold start hits. The
                        // conversion also truncated the fraction, so two digests differing by
                        // less than 1.0 compared EQUAL. Compare the bit patterns instead:
                        // exact, total, and no conversion.
                        // R13.15: the WIDE index-aware hash for the comparison. The scalar
                        // digest stays only as the printed provenance number.
                        const unsigned long long b_dig_arm  = digest_u64(gmres_rhs);
                        const unsigned long long x0_dig_arm = digest_u64(gmres_x0);
                        const unsigned long long d_dig_arm = digest_u64(d_inv_used);
                        const bool d_requested_arm =
                            (wrf::sdirk3::g_sdirk3_config.gmres_block_scale != 0);
                        rows.push_back({arm_name, m, rho, rho_phys, rho_D,
                                        res.rel_error,
                                        res.iterations,
                                        static_cast<int>(res.termination_reason),
                                        a_apply_count, d_weighted,
                                        (which == Arm::Sel) ? *msel_engaged : false,
                                        b_dig_arm, x0_dig_arm,
                                        d_dig_arm, d_requested_arm});
                        // R13.9: PER-BLOCK residual at the largest budget, each arm. The three
                        // aggregate norms say M's reduction is large in rho_D and small in
                        // rho_S, and D^-1 up-weights the small-residual blocks -- so the
                        // inference is that M works on the blocks that do not matter to the
                        // physical residual. That is an inference from aggregates. This is the
                        // measurement: ||r_block|| / ||b_block|| for each block, in the S
                        // coordinates, at j=48, beside the same ratio at j=0.
                        // X4: m=8 is the production budget (restart=7, one cycle); j=48 is where
                        // the arms are compared. Both, so the block that binds production (ph,
                        // ~3/4 of rho_S) is seen at the budget production actually spends.
                        if ((m == 8 || m == 48) && res.x.defined() && cached_layout_.is_exact &&
                            r0_kept.defined()) {
                            const auto r = (gmres_rhs.detach() - gmres_op(res.x.detach()))
                                               .to(torch::kFloat64).reshape({-1});
                            const auto b64 = gmres_rhs.detach().to(torch::kFloat64).reshape({-1});
                            const auto r064 = r0_kept.to(torch::kFloat64).reshape({-1});
                            if (cached_layout_.total_size == r.numel()) {
                                std::cerr << "SDIRK3_FROZEN_AB_BLOCKS stage=" << stage
                                          << " arm=" << arm_name << " j=" << m;
                                for (const auto& blk : cached_layout_.blocks) {
                                    const auto rb  = r.slice(0, blk.start, blk.start + blk.size);
                                    const auto bb  = b64.slice(0, blk.start, blk.start + blk.size);
                                    const auto r0b = r064.slice(0, blk.start, blk.start + blk.size);
                                    const double nb = bb.norm().item<double>();
                                    std::cerr << " " << blk.name << "_rho0="
                                              << (nb > 0.0 ? r0b.norm().item<double>() / nb : -1.0)
                                              << " " << blk.name << "_rho="
                                              << (nb > 0.0 ? rb.norm().item<double>() / nb : -1.0);
                                }
                                std::cerr << std::endl;
                            }
                        }
                    };

                    // ARM ORDER REVERSED between the two passes. If the numbers move with the
                    // order, something is still stateful and the record must say so rather
                    // than average it away.
                    // Referee C1: "better at every j <= 48" is a statement about j <= 48. A
                    // cluster-plus-outliers preconditioner loses at small j and wins past the
                    // outlier count; the ladder must reach where a crossing could live.
                    std::vector<int> ladder{4, 8, 16, 32, 48};
                    if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_FROZEN_MI_AB_EXTEND")) {
                        ladder.push_back(96);
                        ladder.push_back(192);
                    }
                    for (int m : ladder) {
                        run(Arm::M, m); run(Arm::I, m); run(Arm::Sel, m);
                    }
                    const size_t forward_rows = rows.size();
                    for (auto it = ladder.rbegin(); it != ladder.rend(); ++it) {
                        run(Arm::Sel, *it); run(Arm::I, *it); run(Arm::M, *it);
                    }

                    // Restore production's preconditioner to the state it would have had, and
                    // MEASURE whether anything else moved.
                    // Production's wrapper was never handed to an arm; nothing to restore.
                    //
                    // R13.13 (red team round 4): "whether ANYTHING else moved" was one counter.
                    // `precond_total_calls_` is bumped once per M application by every arm --
                    // thousands of increments over the ladder -- and it is the denominator of
                    // the fallback-percentage print for the rest of the solve, so the probe was
                    // mutating production telemetry while reporting that it had not. It is
                    // restored here (the probe's applications are not production's), and the
                    // restore is what `noninterfering` now attests. The JVP fallback counter
                    // was folded into `jvp_ok` instead, so `probe_noninterfering=1` could print
                    // on a run where the probe demonstrably moved a global; it is counted here
                    // as well, and still gates jvp_ok below.
                    // The same two fingerprint probes after the ladder. Bit-exact equality:
                    // the operator is deterministic (e_repeat = 0 is measured beside this) and
                    // M is a fixed-coefficient solve, so any difference is a state change, not
                    // noise. Taken BEFORE the restores, because these two applies move the same
                    // counters the restores fix -- measuring interference with a probe that
                    // interferes is how the first version of this reported probe_interfered=1
                    // against itself.
                    bool operator_state_unchanged = false, precond_state_unchanged = false;
                    if (fp_probe.defined() && A_fp_before.defined()) {
                        const auto A_fp_after = gmres_op(fp_probe).detach();
                        operator_state_unchanged = torch::equal(A_fp_before, A_fp_after);
                    }
                    if (fp_probe.defined() && M_fp_before.defined() && M_fp_probe) {
                        // The SAME pristine copy, so what is compared is the shared state the two
                        // wrappers read -- not the wrapper's own latch, which is per-copy by
                        // construction and would make this trivially true.
                        const auto M_fp_after = M_fp_probe(fp_probe).detach();
                        precond_state_unchanged = torch::equal(M_fp_before, M_fp_after);
                    } else if (!M_inv) {
                        // No preconditioner to move. Not evidence of movement, and not a claim
                        // about one either -- the arms' M is then the identity by construction.
                        precond_state_unchanged = true;
                    }

                    // WHAT MOVED, read before anything is put back. R13.15: the previous version
                    // restored first and then compared the restored values to the snapshots,
                    // which is true by construction -- two of the clauses were vacuous. The
                    // decomposition that is not: some state must NOT have moved at all (a
                    // fallback, an FD fallback, the shared pc event), and some state moves by
                    // design (call counts, timers) and must be RESTORED.
                    const long long fd_after_arms =
                        wrf::sdirk3::g_jvp_fd_fallback_count.load(std::memory_order_relaxed);
                    const int    precond_fb_after   = precond_fallback_count_;
                    const bool   pc_event_after =
                        variable_pc_event ? *variable_pc_event : sv_variable_pc_event;
                    const bool pc_event_moved = (pc_event_after != sv_variable_pc_event);

                    // R5-9 / R5-10: restore everything the probe moved that production reads.
                    precond_total_calls_   = precond_total_calls_before;
                    jvp_call_count         = sv_jvp_calls;
                    jvp_ad_calls           = sv_jvp_ad;
                    jvp_fd_calls           = sv_jvp_fd;
                    jvp_fd_forward_calls   = sv_jvp_fdf;
                    jvp_fd_central_calls   = sv_jvp_fdc;
                    total_jvp_time_ms      = sv_jvp_ms;
                    if (variable_pc_event) *variable_pc_event = sv_variable_pc_event;

                    // The restore is a separate claim from the no-movement one, and both are
                    // reported. A restore makes the RUN safe; it does not make the arms
                    // comparable, which is what `noninterfering` is for.
                    const bool counters_restored =
                        (precond_total_calls_ == precond_total_calls_before) &&
                        (jvp_call_count == sv_jvp_calls) &&
                        (jvp_ad_calls == sv_jvp_ad) &&
                        (jvp_fd_calls == sv_jvp_fd) &&
                        (jvp_fd_forward_calls == sv_jvp_fdf) &&
                        (jvp_fd_central_calls == sv_jvp_fdc) &&
                        (total_jvp_time_ms == sv_jvp_ms) &&
                        (!variable_pc_event || *variable_pc_event == sv_variable_pc_event);
                    const bool noninterfering =
                        (precond_fb_after == fallbacks_before) &&
                        (fd_after_arms == fd_before) &&
                        !pc_event_moved &&
                        counters_restored;
                    jvp_ok = jvp_ok &&
                        (wrf::sdirk3::g_jvp_fd_fallback_count.load(std::memory_order_relaxed)
                             == fd_before);

                    // Order invariance, measured across the two passes.
                    // R13.15 (external review P0-3): METRIC-COMPLETE order invariance.
                    //
                    // This compared `rows[i].rho` -- rho_S -- and nothing else, while the
                    // headline of every recent report is rho_D, rho_phys, the per-arm Msel
                    // receipt and the per-block table. So a run whose rho_S was order-invariant
                    // while rho_D, rho_phys, the termination reason and the Msel engagement all
                    // flipped with arm order still printed ab_valid=1. Every metric the record
                    // reports is now compared forward-vs-reverse for the same (arm, j), and the
                    // discrete ones must match exactly rather than within a tolerance.
                    double worst_order_delta = 0.0;
                    int order_pairs_compared = 0;
                    const char* worst_order_metric = "none";
                    bool discrete_order_invariant = true;
                    auto note_delta = [&](double x, double y, const char* name) {
                        if (x > 0.0 && y > 0.0) {
                            const double d = std::abs(x - y) / x;
                            if (d > worst_order_delta) {
                                worst_order_delta = d;
                                worst_order_metric = name;
                            }
                        } else if (!(x == y)) {
                            // One measured and one not is not agreement.
                            worst_order_delta = std::numeric_limits<double>::infinity();
                            worst_order_metric = name;
                        }
                    };
                    for (size_t i = 0; i < forward_rows; ++i) {
                        for (size_t k = forward_rows; k < rows.size(); ++k) {
                            if (rows[i].j == rows[k].j &&
                                std::string(rows[i].arm) == rows[k].arm) {
                                const auto& f = rows[i];
                                const auto& r = rows[k];
                                if (f.rho > 0.0 && r.rho > 0.0) {
                                    ++order_pairs_compared;
                                } else {
                                    // A row that could not be compared is not a row that
                                    // agreed. Poison the delta so the clause refuses.
                                    worst_order_delta = std::numeric_limits<double>::infinity();
                                    worst_order_metric = "rho_S_unmeasured";
                                }
                                note_delta(f.rho,      r.rho,      "rho_S");
                                note_delta(f.rho_phys, r.rho_phys, "rho_phys");
                                note_delta(f.rho_D,    r.rho_D,    "rho_D");
                                // Discrete quantities must be IDENTICAL, not close. A solve
                                // that took a different number of iterations, ended for a
                                // different reason, weighted by a different D, engaged Msel
                                // differently or applied A a different number of times is a
                                // different solve however near its residual landed.
                                const bool discrete_same =
                                    (f.iters == r.iters) &&
                                    (f.term == r.term) &&
                                    (f.d_weighted == r.d_weighted) &&
                                    (f.msel_engaged == r.msel_engaged) &&
                                    (f.a_applies == r.a_applies) &&
                                    (f.b_dig == r.b_dig) && (f.x0_dig == r.x0_dig);
                                if (!discrete_same) {
                                    discrete_order_invariant = false;
                                    if (worst_order_metric == std::string("none")) {
                                        worst_order_metric = "discrete";
                                    }
                                }
                            }
                        }
                    }

                    auto admissible = [](int t) {
                        using KTR = WRFNewtonKrylovSolver::KrylovTerminationReason;
                        return t == static_cast<int>(KTR::MaxBudget) ||
                               t == static_cast<int>(KTR::ToleranceReached);
                    };
                    bool all_admissible = true, all_finite = true, terms_equal = true,
                         iters_equal = true;
                    for (size_t i = 0; i + 2 < rows.size(); i += 3) {
                        for (size_t k = 0; k < 3; ++k) {
                            all_admissible &= admissible(rows[i + k].term);
                            // All three norms, and finite (P1-4: rho >= 0 admitted +Inf).
                            for (double q : {rows[i + k].rho, rows[i + k].rho_phys,
                                             rows[i + k].rho_D}) {
                                all_finite &= (q >= 0.0) &&
                                              (q < std::numeric_limits<double>::infinity());
                            }
                        }
                        terms_equal &= (rows[i].term == rows[i + 1].term) &&
                                       (rows[i].term == rows[i + 2].term);
                        iters_equal &= (rows[i].iters == rows[i + 1].iters) &&
                                       (rows[i].iters == rows[i + 2].iters);
                    }

                    // P1-4 receipt: rel_reported is the solver's halo-zeroed ratio, rho_S the
                    // probe's. If they disagree past rounding, a halo floor is live and the
                    // absolute numbers are floor-inclusive.
                    double halo_floor_delta = 0.0;
                    for (const auto& r_ : rows) {
                        if (r_.rho > 0.0 && r_.rel >= 0.0f) {
                            halo_floor_delta = std::max(
                                halo_floor_delta, std::abs(r_.rho - r_.rel) / r_.rho);
                        }
                    }
                    // MEASURED, not asserted: every arm digested the b and x0 it was handed,
                    // and they must agree across all of them.
                    bool b_digests_agree = true, x0_digests_agree = true;
                    if (!rows.empty()) {
                        for (const auto& r_ : rows) {
                            b_digests_agree  = b_digests_agree  && (r_.b_dig  == rows[0].b_dig);
                            x0_digests_agree = x0_digests_agree && (r_.x0_dig == rows[0].x0_dig);
                        }
                    } else {
                        b_digests_agree = x0_digests_agree = false;   // nothing ran: not evidence
                    }
                    wrf::sdirk3::AbComparison cmp;
                    // The operator IS one closure object handed to every arm -- by construction,
                    // and stated as such on the record (`ab_evidence=`) rather than implied by a
                    // digest that was never compared.
                    cmp.same_operator = true;
                    cmp.same_rhs = b_digests_agree;
                    cmp.same_x0 = x0_digests_agree;
                    // N2: with precond_refinement_passes > 1 production's M is the
                    // defect-correction wrapper and the arm's make_fresh_M is the bare one --
                    // an "M" arm that is not M. Same path only when there is no wrapper.
                    const int refinement_passes_now =
                        wrf::sdirk3::g_sdirk3_config.precond_refinement_passes;
                    cmp.same_solver_path = (refinement_passes_now <= 1);
                    cmp.same_budget = iters_equal;
                    cmp.early_stop_disabled = early_stop_off;
                    // MEASURED, not asserted (external review P0-1/P0-2).
                    cmp.same_frozen_operator = true;   // one closure, handed to every arm
                    cmp.fresh_wrapper_per_arm = true;  // make_fresh_M() copies it per row
                    cmp.shared_preconditioner_instance = true;  // stated, not hidden
                    cmp.operator_state_unchanged = operator_state_unchanged;
                    cmp.preconditioner_state_unchanged = precond_state_unchanged;
                    cmp.diagnostic_noninterfering = noninterfering;
                    cmp.identity_resolved = identity_resolved_rhs_dir;
                    cmp.jvp_authoritative = jvp_ok && operator_linear && precond_linear;
                    cmp.rho_a_finite = cmp.rho_b_finite = all_finite;
                    cmp.termination_a_admissible = cmp.termination_b_admissible =
                        all_admissible;
                    cmp.termination_a = 0;
                    cmp.termination_b = terms_equal ? 0 : 1;
                    // 1e-6 relative: bit-identical rows give exactly 0; anything larger means
                    // some state survived between passes and the arms were not independent.
                    // Continuous metrics within tolerance AND every discrete one identical.
                    cmp.order_invariant =
                        (worst_order_delta <= 1.0e-6) && discrete_order_invariant;
                    // R13.15 (external review P1-1): the Msel receipt. `msel_engaged` was
                    // printed per row and read by nothing, so a layout mismatch that silently
                    // disabled the row projection still produced ab_valid=1 over rows labelled
                    // Msel. It is now a separate verdict: the M-vs-I comparison does not depend
                    // on Msel having engaged, and the Msel conclusion does.
                    bool msel_rows_present = false, msel_all_engaged = true;
                    for (const auto& r_ : rows) {
                        if (std::string(r_.arm) == "Msel") {
                            msel_rows_present = true;
                            msel_all_engaged = msel_all_engaged && r_.msel_engaged;
                        }
                    }
                    cmp.msel_engaged_measured = msel_rows_present && msel_all_engaged;
                    // R13.15 (external review P1-2): every arm must have weighted by the SAME D,
                    // and a D that was requested must have arrived. rho_D is a headline number
                    // and it is only comparable across arms under both conditions.
                    bool d_same_across_rows = !rows.empty(), d_all_valid = !rows.empty();
                    for (const auto& r_ : rows) {
                        d_same_across_rows = d_same_across_rows && (r_.d_dig == rows[0].d_dig);
                        // Requested and absent is a TRANSFER FAILURE, not "D = I". Not
                        // requested and absent is correct and says nothing about the weight.
                        d_all_valid = d_all_valid && !(r_.d_requested && !r_.d_weighted);
                    }
                    cmp.d_consistent_across_arms = d_same_across_rows && d_all_valid;
                    const auto verdict = wrf::sdirk3::ab_attributable(cmp);
                    // The Msel conclusion needs everything the M-vs-I one needs, plus its own
                    // receipt. Reported separately so a reader cannot borrow one for the other.
                    const auto msel_verdict = wrf::sdirk3::msel_attributable(cmp);

                    std::cerr << "SDIRK3_FROZEN_AB_SYSTEM stage=" << stage
                              << " newton_iter=" << newton_iter
                              << " global_timestep=" << ::sdirk3_host_global_timestep()
                              << " solver_id=" << solver_id_
                              << " rank=" << wrf::sdirk3::diagnostic_mpi_rank()
                              << " ab_valid=" << (verdict.valid ? 1 : 0)
                              << " ab_reason=" << verdict.reason
                              << " b_digest=" << digest(gmres_rhs)
                              << " x0_digest=" << digest(gmres_x0)
                              // R13.14 (round 5, R5-8): the arms are handed the SAME two
                              // objects by reference, so equal digests follow from aliasing,
                              // not from a comparison of independently supplied inputs. The
                              // recheck still has power -- it would catch an in-place mutation
                              // by an arm -- and the label now says exactly that much.
                              << " ab_evidence=b_x0_single_object_digest_rechecked_per_arm"
                                 "/shared_operator_and_preconditioner_fingerprinted"
                              << " operator_state_unchanged="
                              << (operator_state_unchanged ? 1 : 0)
                              << " precond_state_unchanged=" << (precond_state_unchanged ? 1 : 0)
                              << " counters_restored=" << (counters_restored ? 1 : 0)
                              << " pc_event_moved=" << (pc_event_moved ? 1 : 0)
                              << " shared_preconditioner_instance=1 fresh_wrapper_per_arm=1"
                              << " b_digests_agree=" << (b_digests_agree ? 1 : 0)
                              << " x0_digests_agree=" << (x0_digests_agree ? 1 : 0)
                              << " b_norm=" << b_norm
                              << " rho0_S=" << rho0_S
                              << " rho0_phys=" << rho0_phys
                              << " rho0_D=" << rho0_D
                              << " refinement_passes=" << refinement_passes_now
                              << " metric=rho_S_unweighted"
                              << " tolerance_exit_disabled=1"
                              << " early_stop_disabled=" << (early_stop_off ? 1 : 0)
                              << " jvp_fd_fallback_free=" << (jvp_ok ? 1 : 0)
                              << " operator_linear=" << (operator_linear ? 1 : 0)
                              << " e_repeat=" << e_repeat
                              << " e_homogeneity=" << e_hom
                              << " e_additivity=" << e_add
                              << " identity_frac_rand=" << identity_frac_rand
                              // R13.20 (referee claim 2): renamed from `*_krylov`. Same
                              // quantity, a name that no longer asserts an unmeasured
                              // precondition; past logs' `identity_*_krylov` rows are these.
                              << " identity_frac_rhs_dir=" << identity_frac_rhs_dir
                              << " rhs_dir_is_first_arnoldi=" << rhs_dir_is_first_arnoldi
                              << " e_hom_rhs_dir=" << e_hom_rhs_dir
                              << " identity_resolution_rand=" << identity_resolution_rand
                              << " identity_resolution_rhs_dir=" << identity_resolution_rhs_dir
                              << " identity_resolved_rhs_dir=" << (identity_resolved_rhs_dir ? 1 : 0)
                              << " precond_linear=" << (precond_linear ? 1 : 0)
                              << " eM_homogeneity=" << eM_hom
                              << " eM_additivity=" << eM_add
                              << " metric_phys=rho_phys_S_weighted"
                              << " metric_D=rho_D_fgmres_objective"
                              << " probe_noninterfering=" << (noninterfering ? 1 : 0)
                              << " halo_floor_delta=" << halo_floor_delta
                              << " worst_order_delta=" << worst_order_delta
                              << " worst_order_metric=" << worst_order_metric
                              << " d_same_across_rows=" << (d_same_across_rows ? 1 : 0)
                              << " d_all_valid=" << (d_all_valid ? 1 : 0)
                              << " discrete_order_invariant="
                              << (discrete_order_invariant ? 1 : 0)
                              << " msel_valid=" << (msel_verdict.valid ? 1 : 0)
                              << " msel_reason=" << msel_verdict.reason
                              << " order_pairs_compared=" << order_pairs_compared
                              << (verdict.valid
                                    ? "  (attributable: equal Arnoldi dimension, one code"
                                      " path, a FRESH preconditioner per row, and the probe"
                                      " left production's state unchanged)"
                                    : "  (NOT attributable -- rows below are raw numbers and"
                                      " NO comparison between the arms may be drawn from"
                                      " them)")
                              << std::endl;
                    for (size_t i = 0; i < rows.size(); ++i) {
                        std::cerr << "SDIRK3_FROZEN_AB stage=" << stage
                                  << " pass=" << (i < forward_rows ? "MI" : "IM")
                                  << " arm=" << rows[i].arm
                                  << " j=" << rows[i].j
                                  << " rho_S=" << rows[i].rho
                                  << " rho_phys=" << rows[i].rho_phys
                                  << " rho_D=" << rows[i].rho_D
                                  << " A_applies=" << rows[i].a_applies
                                  << " d_weighted=" << (rows[i].d_weighted ? 1 : 0)
                                  << " msel_engaged=" << (rows[i].msel_engaged ? 1 : 0)
                                  << " rel_reported=" << rows[i].rel
                                  << " iters=" << rows[i].iters
                                  << " termination=" << rows[i].term
                                  << " ab_valid=" << (verdict.valid ? 1 : 0)
                                  << std::endl;
                    }
                }

                // FGMRES ROUTING (full-repo review P1-1, mandatory — not a knob):
                // any right-preconditioned solve MUST use FGMRES, because the
                // production preconditioner wrapper can change mid-solve (ratio-guard
                // identity lock / warn_only / defect toggling) and standard GMRES's
                // M_inv(sum yV) reconstruction is then inconsistent with the Arnoldi
                // Hessenberg. Unpreconditioned solves keep solve_gmres bit-for-bit.
                auto gmres_result = gmres_M_inv
                    ? krylov_methods::solve_fgmres(
                          gmres_op,
                          gmres_rhs,
                          gmres_x0,
                          stage,
                          ru_share,
                          effective_restart,
                          krylov_tol_adaptive,
                          effective_max_restarts,
                          gmres_M_inv,  // stored per-step as Z (scaled space, see below)
                          layout_initialized_ ? &cached_layout_ : nullptr,
                          halo_mask_initialized_ ? &halo_mask_ : nullptr,
                          options_.periodic_x,
                          options_.periodic_y,
                          jvp_check_this_iter ? &jvp_check_basis : nullptr,
                          // Handed over only if the weighting was frozen for THIS stage. The
                          // identity check stays here, where the stage identity is known, rather
                          // than inside the Krylov solve which would have to be told it.
                          // Weighted at the Newton linearization point when the probe is on;
                          // the stage-entry capture is the fallback and is labelled as such.
                          // Also needed by the stage-WRMS metric experiment, which USES these
                          // weights as the Krylov objective rather than merely reporting with
                          // them -- gating on the probe alone silently disabled that experiment.
                          // Also handed over for the paired trajectory: without E the report
                          // can print three of its four ratios and the stage gate's own
                          // objective -- the one the comparison exists to make -- comes out
                          // as "unavailable". Handing weights over does NOT change the
                          // objective; only WRF_SDIRK3_KRYLOV_WRMS_METRIC does that.
                          (apinv_probe_armed() ||
                           wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_WRMS_METRIC") ||
                           wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_KRYLOV_TRAJECTORY"))
                              ? newton_weights_for(U_eval, stage, newton_iter)
                              : nullptr,
                          // S, so a physically-weighted defect is computed on physical vectors.
                          // Null when scaling is off, where S = I.
                          scaling_initialized_ ? &S_diag_ : nullptr)
                    : krylov_methods::solve_gmres(
                          gmres_op,
                          gmres_rhs,
                          gmres_x0,
                          stage,
                          ru_share,
                          effective_restart,
                          krylov_tol_adaptive,
                          effective_max_restarts,
                          gmres_M_inv,  // null: unpreconditioned path unchanged
                          layout_initialized_ ? &cached_layout_ : nullptr,
                          halo_mask_initialized_ ? &halo_mask_ : nullptr,
                          options_.periodic_x,
                          options_.periodic_y);
                variable_pc_event_this_newton = *variable_pc_event;

                // PR 9F.9.1 SHADOW: save r_g = b_s - A_s*x for the exact trust model at
                // the later trust-region scope (where gmres_result is gone). r_g is in the
                // SCALED space (gmres_rhs=-S^-1 R, x=S^-1 dK -> r_true=S^-1(-R-A dK)); the
                // trust shadow consumes it as scaled and does NOT re-apply S^-1. Cleared
                // FIRST so a solve that ends before the trust trial cannot leak a stale r_g
                // from a previous iteration into this one. Diagnosis-only (own env flag).
                last_gmres_r_true_ = torch::Tensor();
                if (numerical_shadow_enabled() && gmres_result.r_true.defined()) {
                    last_gmres_r_true_ = gmres_result.r_true.detach();
                }

                // PR 8: per-linear-solve record (opt-in). Reads only —
                // the returned result is used unchanged below.
                if (stage_diag_enabled()) {
                    emit_stage_diag([&](std::ostream& os) {
                    os << "SDIRK3_FGMRES_DIAG ts=" << global_timestep_
                              << " stage=" << stage
                              << " iter=" << newton_iter
                              << " path=" << (gmres_M_inv ? "fgmres" : "gmres")
                              << std::scientific
                              << " rhs_norm=" << diag_norm(gmres_rhs)
                              << " x0_norm=" << diag_norm(gmres_x0)
                              << " tol=" << krylov_tol_adaptive
                              << std::defaultfloat
                              << " restart=" << effective_restart
                              << " max_restarts=" << effective_max_restarts
                              << " iters=" << gmres_result.iterations
                              << " restarts=" << gmres_result.restarts
                              << std::scientific
                              << " final_res=" << gmres_result.final_residual
                              << " rel_err=" << gmres_result.rel_error
                              << std::defaultfloat
                              << " converged=" << (gmres_result.success ? 1 : 0)
                              << " breakdown=" << (gmres_result.breakdown ? 1 : 0)
                              << " stagnation=" << (gmres_result.stagnation ? 1 : 0)
                              // PR 8.1: the EXACT termination reason plus the
                              // detector inputs — the stagnation boolean alone
                              // conflated ArnoldiStagnation with the forced
                              // mid-budget hopeless probe.
                              << " termination_reason="
                              << WRFNewtonKrylovSolver::
                                     krylov_termination_reason_name(
                                         gmres_result.termination_reason)
                              << " ru_share=" << ru_share
                              << " probe_j=" << gmres_result.probe_j
                              << std::scientific
                              << " probe_true_err=" << gmres_result.probe_true_err
                              << " hopeless_floor=" << gmres_result.probe_hopeless_floor
                              << " stag_ratio=" << gmres_result.stag_ratio_used
                              << std::defaultfloat
                              << " stag_count=" << gmres_result.stag_count_final
                              << " dx_finite=" << (diag_all_finite(gmres_result.x) ? 1 : 0)
                              << " variable_pc=" << (variable_pc_event_this_newton ? 1 : 0)
                              << " msg=\"" << gmres_result.message << "\""
                              << "\n";
                    });
                }

                // Unscale solution: dK = S · dK_tilde
                if (scaling_initialized_) {
                    dK = S_diag_ * gmres_result.x;
                } else {
                    dK = gmres_result.x;
                }
                // R9: the ledger's r_g belongs to THIS dK, before the two post-solve
                // mutations below (halo zeroing, and the direct-U override of the ru block).
                if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_NONLINEAR_LEDGER") &&
                    dK.defined()) {
                    torch::NoGradGuard ng_snap;
                    ledger_dK_solve = dK.detach().clone();
                }

                // Zero halo components in dK before K += dK update.
                // v20.14r27g: Halo mask is DISABLED in GMRES operator (v20.14r26),
                // but post-GMRES zeroing is still applied to suppress boundary noise.
                apply_halo_zeroing(dK);

                // v20.14 r49-fix: Direct U Solve — when ru_share > threshold, S_U ≈ I,
                // so the optimal Newton step for U is δK_u = -R_u.
                // Override the GMRES U-block with the direct solution.
                {
                    float du_thresh = wrf::sdirk3::g_sdirk3_config.direct_u_solve_thresh;
                    if (du_thresh > 0.0f && last_ru_share_ > du_thresh &&
                        layout_initialized_ && cached_layout_.blocks.size() >= 1 &&
                        cached_layout_.blocks[0].name == "ru") {
                        torch::NoGradGuard no_grad;
                        const auto& ru_blk = cached_layout_.blocks[0];
                        if (ru_blk.start + ru_blk.size <= dK.numel() &&
                            ru_blk.start + ru_blk.size <= R.numel()) {
                            auto dK_u = dK.slice(0, ru_blk.start, ru_blk.start + ru_blk.size);
                            auto R_u = R.detach().slice(0, ru_blk.start, ru_blk.start + ru_blk.size);
                            dK_u.copy_(-R_u);
                            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 && newton_iter == 0) {
                                float ru_norm = R_u.norm().to(torch::kCPU).item<float>();
                                std::cerr << "[DIRECT U SOLVE] ru_share=" << last_ru_share_
                                          << " ||R_u||=" << ru_norm
                                          << " (replaced GMRES U-block with -R_u)\n";
                            }
                        }
                    }
                }

                if (stage >= 0 && stage < static_cast<int>(gmres_warmstart_stage_.size())) {
                    // Shift current same-stage cache into temporal history first.
                    if (gmres_warmstart_stage_[stage].defined()) {
                        gmres_warmstart_prev_stage_[stage] = gmres_warmstart_stage_[stage].detach().clone();
                        gmres_warmstart_prev_relerr_stage_[stage] = gmres_warmstart_relerr_stage_[stage];
                        gmres_warmstart_prev_varpc_stage_[stage] = gmres_warmstart_varpc_stage_[stage];
                    }
                    gmres_warmstart_stage_[stage] = dK.detach().clone();
                    gmres_warmstart_relerr_stage_[stage] = gmres_result.rel_error;
                    gmres_warmstart_varpc_stage_[stage] = variable_pc_event_this_newton;
                }

                gmres_success = gmres_result.success;

                // v20.14r27g: Keep raw rel_error for diagnostics/trust-region.
                // Clamped value is only for the quadratic prediction model (e² term).
                gmres_raw_rel_error = gmres_result.rel_error;
                gmres_initial_rel_error = gmres_result.initial_rel_error;
                gmres_rel_error = std::clamp(gmres_raw_rel_error, 0.0f, 1.0f);
                if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_NONLINEAR_LEDGER") &&
                    gmres_result.r_true.defined()) {
                    torch::NoGradGuard ng_keep;
                    ledger_r_gmres = gmres_result.r_true.detach();
                }
                stats_.total_krylov_iterations += gmres_result.iterations;
                // R13.19 SELF-REVIEW (round 8 P1-G, extended): set UNCONDITIONALLY, per solve.
                // This flag and the `last_*` receipt were written inside the r0-measured guard,
                // so a solve that did not measure r0 inherited the PREVIOUS iteration's values --
                // and the exit site promotes `last_*` as "the receipt of THIS solve", which is
                // exactly the misattribution the R13.18 P0-4 fix exists to prevent. A stale
                // `gmres_converged_on_entry` would additionally suppress this solve's
                // total-failure flag, a production effect.
                gmres_converged_on_entry =
                    (gmres_result.termination_reason ==
                         WRFNewtonKrylovSolver::KrylovTerminationReason::InitialConverged);
                // R13.14 (round 5, R5-13): the INNER budget these solves were given. The
                // no-progress ratio is budget-dependent -- a healthy operator on 7 Arnoldi
                // vectors reads 0.92 -- so the ratio cannot be read without it, and the
                // justification for the boundary constant cited it as "on the record" when
                // nothing produced it.
                stats_.krylov_restart_budget = effective_restart;
                stats_.krylov_max_restarts = effective_max_restarts;
                // R13.2: how well the LINEAR solve did, kept as the BEST across this stage.
                // Newton cannot converge on top of a solve that does not solve, so this
                // separates "the outer iteration is stuck" from "the operator or the
                // preconditioner is" -- and those point at different work.
                if (std::isfinite(gmres_result.rel_error) && gmres_result.rel_error >= 0.0f &&
                    (stats_.best_krylov_rel_error < 0.0f ||
                     gmres_result.rel_error < stats_.best_krylov_rel_error)) {
                    stats_.best_krylov_rel_error = gmres_result.rel_error;
                }
                // R13.12 (red team R3-2): the same quantity measured against where THIS solve
                // started. ||r||/||b|| answers "is the step predicted to reduce the nonlinear
                // residual"; ||r||/||r0|| answers "did the Krylov solve do anything". The
                // classifier needs the second and was reading the first. Only recorded when r0
                // was measured -- an unmeasured r0 leaves the sentinel, and the classifier
                // falls back rather than inventing a reference of 1.
                if (std::isfinite(gmres_result.rel_error) && gmres_result.rel_error >= 0.0f &&
                    gmres_result.initial_rel_error > 0.0f &&
                    std::isfinite(gmres_result.initial_rel_error)) {
                    const float progress =
                        gmres_result.rel_error / gmres_result.initial_rel_error;
                    if (stats_.best_krylov_rel_error_vs_r0 < 0.0f ||
                        progress < stats_.best_krylov_rel_error_vs_r0) {
                        stats_.best_krylov_rel_error_vs_r0 = progress;
                    }
                    // R13.13 (round 4): the WORST solve, and which iteration it was. "Did the
                    // linear solve stop working" is a max question, not a min one -- a min
                    // answers "did any solve work" and one early success clears a stage of
                    // stalls. It also has no SELECTOR, so no coordinate seam: an earlier attempt
                    // keyed the value to the solve that first tripped the production predicate,
                    // which under the default rule asks the ||b|| question.
                    //
                    // R13.14 (red team round 5, P0): but the max must be over solves that DID
                    // WORK AND DID NOT FINISH, and it was over all of them.
                    //   * InitialConverged does ZERO work and its progress is ~1.0, so under a
                    //     max the best possible outcome would score as a total stall. Round 4
                    //     added initial_rel_error to that return BECAUSE dropping it from a MIN
                    //     manufactured a stall; the same commit then made a max the classifier's
                    //     input, where the same value manufactures the stall directly.
                    //     R13.19 (P0-1) narrowed the exclusion to solves that met BOTH metrics,
                    //     and R13.19 self-review (round 8, P0-B) had to follow it with
                    //     `met_tolerance` including a D-satisfied InitialConverged -- otherwise
                    //     admitting the solve only moved the misclassification one clause over.
                    //   * A solve that REACHED TOLERANCE solved. Its progress is evidence about
                    //     the tolerance, not about whether Krylov works.
                    // Excluding both means a stage where every solve either converged on entry
                    // or reached tolerance yields NO stagnation evidence -- which is correct,
                    // and lets NewtonBudgetExhausted have its case back. That category exists
                    // precisely to stop a falling residual being reported as a stall, and a max
                    // over all solves re-opened that hole through a different door: the later a
                    // Newton run gets, the smaller and noisier its linear RHS, so progress -> 1
                    // and "the closer Newton gets, the more certainly the linear solve is
                    // blamed".
                    // R13.16 (round 6, R6-2): ToleranceReached is NOT evidence that the solve
                    // worked. "Tolerance" here is the Eisenstat-Walker forcing term, and it is
                    // CAPPED AT 0.9 (`ew_eta_max`) -- and it saturates at exactly that cap in
                    // the failing regime, because eta = 0.9 * res_ratio^1.5 and res_ratio -> 1
                    // when the Newton residual stops falling. So a solve can "reach tolerance"
                    // having removed 10% of its residual, which is a stall by this classifier's
                    // own threshold. Excluding those was the exact INVERSE of the round-5 P0:
                    // that one counted a no-op solve as a stall, this one discounted a stall as
                    // a success -- and the fix for the first produced the second.
                    //
                    // Only a solve that did ZERO WORK is excluded. A solve that met a loose
                    // tolerance keeps whatever progress it made, and the fact that it stopped
                    // because it was ASKED to is recorded separately (below) so the classifier
                    // can name the forcing term instead of the operator.
                    // R13.19 (precision review P0-1): an InitialConverged solve is TRIVIAL
                    // only when it satisfied BOTH metrics. One that met the stop objective and
                    // not the S one is not a zero-work success -- it is exactly the objective
                    // mismatch, and excluding it from the progress aggregate hides the state
                    // this classifier exists to name.
                    const bool trivial_solve =
                        (gmres_result.termination_reason ==
                             WRFNewtonKrylovSolver::KrylovTerminationReason::InitialConverged) &&
                        gmres_result.D_tolerance_reached &&
                        gmres_result.S_tolerance_reached;
                    // R13.17 (external review P0-2): `ToleranceReached` is not the only
                    // termination that met a tolerance. `InternalConvergenceStop` means the
                    // D-objective was satisfied -- the loop stopped because it had minimised what
                    // it was asked to -- and reading only the first sent exactly that state back
                    // to KrylovStagnated, the misclassification the category exists to prevent.
                    using KTR_ = WRFNewtonKrylovSolver::KrylovTerminationReason;
                    // R13.19 SELF-REVIEW (round 8, P0-B): `InitialConverged` MET A TOLERANCE
                    // -- the D objective; that branch is gated on `error_tensor < tol`. Leaving it
                    // out made the R13.19 P0-1 fix produce the INVERSION it was written to stop:
                    // a D-satisfied zero-work solve was admitted to the aggregate (correct), then
                    // scored as an unmet solve at the maximum, which trips the tie refusal --
                    // the FIRST clause of the four-way -- so `KrylovObjectiveMismatch`, the
                    // category the fix exists to reach, became UNREACHABLE for exactly the solve
                    // it was aiming at, and the layer emitted was the operator/split. The row even
                    // carried `worst_krylov_D_reached=1 worst_krylov_S_reached=0` beside
                    // `category=krylov_stagnated`: two mutually exclusive readings on one line.
                    const bool met_tolerance =
                        (gmres_result.termination_reason == KTR_::ToleranceReached) ||
                        (gmres_result.termination_reason == KTR_::InternalConvergenceStop) ||
                        (gmres_result.termination_reason == KTR_::InitialConverged &&
                         gmres_result.D_tolerance_reached);
                    // R13.18 (deep review P0-5): MEASURED exhaustion, not the resolver's
                    // default reason. `MaxBudget` is what the resolver keeps when nothing else is
                    // selected, and it coexists with the message "early exit before max restarts",
                    // so it does not establish spent == allowed. A category that says "the budget
                    // ran out" must have seen the budget run out.
                    const bool budget_exhausted =
                        (gmres_result.arnoldi_spent >= 0 &&
                         gmres_result.arnoldi_allowed > 0 &&
                         gmres_result.arnoldi_spent >= gmres_result.arnoldi_allowed);
                    // Where the tolerance came from. A category that names Eisenstat-Walker must
                    // have READ this; the layer string used to assert it.
                    // R13.19 (precision review P1-1): the source must key on what set the
                    // TOLERANCE. `stage_budget_forcing_coupled` is `policy.ew_applied`, which is
                    // whether Eisenstat-Walker changed the RESTART BUDGET -- so a run whose
                    // tolerance came from E-W while the budget multiplier happened to be 1 was
                    // recorded as `Base`. And the INN ramp multiplies krylov_tol_adaptive and was
                    // not in the selector at all, leaving `InnRamp` with no producer. Last writer
                    // wins, which is the order the tolerance is actually built in.
                    // R13.20 (round 9, R9-5), two corrections to this selector:
                    //  (a) `stage_budget_forcing_eta > 0` is DROPPED. It is `policy.ew_eta_used`,
                    //      and `apply_ew` (wrf_sdirk3_stage_krylov_policy.h:114-134) writes the
                    //      restart BUDGET and never `p.tol` -- only apply_stage2/apply_stage3
                    //      write the tolerance, and both also set `tol_overridden`, which is the
                    //      `krylov_tol_stage_override` arm above. So the disjunct could only fire
                    //      with adaptive tolerances OFF and a stage budget knob set, where it
                    //      labelled a plain `options_.krylov_tol` solve `eisenstat_walker`. Same
                    //      category error as the P1-1 defect it replaced, one level up.
                    //  (b) the E-W arm no longer collapses to one enum value: `ew_tol_source`
                    //      carries which of {forcing term, eta clamp, initial floor, base} bound.
                    const int tol_source = static_cast<int>(
                        gmres_inn_tol_ramped
                            ? wrf::sdirk3::KrylovToleranceSource::InnRamp
                            : (krylov_tol_stage_override
                                   ? wrf::sdirk3::KrylovToleranceSource::StageOverride
                                   : ew_tol_source));
                    // R13.18 (deep review P0-4): this solve's receipt, recorded where
                    // gmres_result is in scope. The Newton exit site is later in the same
                    // iteration and outside this scope; it promotes these into exit_* so a
                    // terminal event is subtyped by the solve that ENDED the loop, not by the
                    // stage's largest-ratio solve, which need not be the same iteration.
                    // R13.18 (deep review P0-1 remainder): rho_E, the THIRD metric. The stage
                    // gate accepts or rejects on ||E^-1 R||, and the receipt carried only D and S
                    // -- so rho_D < eta and rho_S < eta with rho_E >= eta, a solve that satisfied
                    // both recorded metrics and is still refused by the gate, was unclassifiable.
                    // E comes from the stage weights (one E per stage, so the sequence is
                    // comparable across iterations), and the residual is mapped back to physical
                    // coordinates first because E is a physical weighting.
                    double rho_E_final_this = -1.0;
                    {
                        torch::NoGradGuard ng_rE;
                        const auto* w_e = stage_weights_for(stage);
                        torch::Tensor E_inv_r;
                        if (w_e != nullptr && layout_initialized_ &&
                            cached_layout_.is_valid() &&
                            gmres_result.r_true.defined() &&
                            cached_layout_.total_size == gmres_result.r_true.numel()) {
                            E_inv_r = wrf::sdirk3::inverse_scale_vector(
                                cached_layout_, w_e->scale, gmres_result.r_true);
                            if (E_inv_r.defined() &&
                                E_inv_r.numel() != gmres_result.r_true.numel()) {
                                E_inv_r = torch::Tensor{};
                            }
                        }
                        if (E_inv_r.defined() && gmres_rhs.defined()) {
                            auto to_phys = [&](const torch::Tensor& v) {
                                return (scaling_initialized_ && S_diag_.defined() &&
                                        S_diag_.numel() == v.numel())
                                           ? (S_diag_ * v) : v;
                            };
                            // R13.19 SELF-REVIEW, ROUND 9 (R9-1): the halo-zeroing added here
                            // was INERT and has been removed. `zero_halo_regions` early-returns
                            // on `t.dim() < 3` and these are 1-D PACKED state vectors, so both
                            // calls returned at the first `if` on every run, for every halo
                            // width and grid. The tree states this in three other places
                            // ("1D packed tensors: raw norm", "zero_halo_regions is no-op on 1D
                            // tensors").
                            //
                            // That also falsifies the premise I accepted from the numerics
                            // referee: rho_D and rho_S are computed on copies produced by the
                            // SAME no-op, so they are equally raw and there was never a halo
                            // asymmetry between the three metrics to correct.
                            const auto e64 = E_inv_r.to(torch::kFloat64);
                            const auto rE =
                                (to_phys(gmres_result.r_true.detach()).to(torch::kFloat64) * e64)
                                    .norm().item<double>();
                            const auto bE =
                                (to_phys(gmres_rhs.detach()).to(torch::kFloat64) * e64)
                                    .norm().item<double>();
                            if (bE > 0.0) rho_E_final_this = rE / bE;
                        }
                    }
                    stats_.last_rho_E_final = rho_E_final_this;
                    stats_.last_E_reached =
                        (rho_E_final_this >= 0.0 &&
                         rho_E_final_this < static_cast<double>(krylov_tol_adaptive));
                    stats_.last_solve_iter = newton_iter;
                    stats_.last_rho_stop_final = gmres_result.rho_D_final;
                    stats_.last_rho_S_final = gmres_result.rho_S_final;
                    stats_.last_D_reached = gmres_result.D_tolerance_reached;
                    stats_.last_S_reached = gmres_result.S_tolerance_reached;
                    stats_.last_stopping_metric = gmres_result.stopping_metric;
                    stats_.last_tolerance_source = tol_source;
                    stats_.last_budget_exhausted =
                        (gmres_result.arnoldi_spent >= 0 &&
                         gmres_result.arnoldi_allowed > 0 &&
                         gmres_result.arnoldi_spent >= gmres_result.arnoldi_allowed);
                    if (!trivial_solve) {
                        // Counted here so the count is over exactly the solves the max is over.
                        stats_.krylov_solves_measured_vs_r0++;
                        // R13.17 (external review P0-2): the TIE SET. A strict `>` update lets
                        // the first arrival name the layer when two solves share the worst ratio
                        // -- not a remote case with eta saturated at its cap. If any solve within
                        // a hair of the worst did NOT meet a tolerance, the forcing-term and
                        // objective-mismatch categories are refused and it reads as a stall.
                        // R13.18 (deep review P0-3): the fold lives in wrf_sdirk3_first_failure.h as a PURE
                        // function, so the order-independence this reducer needs is testable. The streaming
                        // version never dropped the old tie set when a strictly larger worst arrived: A(0.90,
                        // not-met) then B(0.99, met) gave false while B then A gave true -- the same solve set,
                        // two verdicts, and the verdict decides whether the forcing-term / objective-mismatch
                        // categories may be read at all.
                        {
                            wrf::sdirk3::NearWorstFold st;
                            st.worst =
                                static_cast<double>(stats_.worst_krylov_rel_error_vs_r0);
                            st.worst_unmet = stats_.near_worst_unmet;
                            st = wrf::sdirk3::near_worst_accumulate(
                                st, static_cast<double>(progress), met_tolerance);
                            stats_.near_worst_unmet = st.worst_unmet;
                            // Evaluated from two MAXIMA at the end, so the answer cannot depend
                            // on the order the solves arrived in.
                            stats_.all_near_worst_met_tolerance =
                                wrf::sdirk3::near_worst_all_met(st);
                            // R13.19 SELF-REVIEW: and the per-solve MECHANISM, so the layer named
                            // is not whichever solve happened to arrive first at a tied worst.
                            {
                                wrf::sdirk3::KrylovSolveMechanism m;
                                m.progress = static_cast<double>(progress);
                                m.met_tolerance = met_tolerance;
                                m.D_reached = gmres_result.D_tolerance_reached;
                                m.S_reached = gmres_result.S_tolerance_reached;
                                m.budget_exhausted = budget_exhausted;
                                m.tolerance_source = tol_source;
                                stats_.krylov_mechanisms.push_back(m);
                                stats_.near_worst_mechanism_ambiguous =
                                    wrf::sdirk3::near_worst_mechanism_ambiguous(
                                        stats_.krylov_mechanisms);
                            }
                        }
                        if (progress > stats_.worst_krylov_rel_error_vs_r0) {
                            stats_.worst_krylov_rel_error_vs_r0 = progress;
                            stats_.worst_krylov_iter = newton_iter;
                            // Did the WORST solve stop because it was satisfied? That is the
                            // difference between "the operator could not be solved" and "the
                            // forcing term did not ask for more", and they route to opposite
                            // layers. The eta it was given goes on the record beside it.
                            stats_.worst_krylov_met_tolerance = met_tolerance;
                            stats_.worst_krylov_eta = krylov_tol_adaptive;
                            // R13.16 (round 6, R6-14): the budget THIS solve was given. The
                            // stage-level field is assigned per solve and is therefore the LAST
                            // iteration's, while worst_krylov_iter can name an earlier one --
                            // and effective_restart genuinely moves mid-stage (the Krylov policy
                            // overrides it, the hopeless caps clamp it). Pairing the worst ratio
                            // with the stage's last budget was true only by luck; recorded here
                            // it is true by construction.
                            stats_.worst_krylov_restart_budget = effective_restart;
                            stats_.worst_krylov_D_reached =
                                gmres_result.D_tolerance_reached;
                            stats_.worst_krylov_S_reached =
                                gmres_result.S_tolerance_reached;
                            stats_.worst_krylov_tolerance_source = tol_source;
                            stats_.worst_krylov_budget_exhausted = budget_exhausted;
                            // R13.20 (claim 7.4): the same solve in the LADDER's coordinate.
                            stats_.worst_krylov_rho_D = gmres_result.rho_D_final;
                            stats_.worst_krylov_rho_S = gmres_result.rho_S_final;
                        }
                    } else {
                        stats_.krylov_solves_trivial++;
                    }
                }
                // R13.5: divergence is not stagnation. The total-failure predicate folds
                // raw_rel_error > 1 (the residual GREW) together with rel_error >= 0.999 (it
                // did not move), and those point at different work.
                // R13.9: divergence is measured against where the solve STARTED. On a warm
                // start ||r0|| can exceed ||b||, and a solve that reduced it still reports
                // rel_error > 1 -- the em_b_wave failing iteration did exactly that (1.054 ->
                // 0.979) and was classified krylov_diverged. Fall back to the old test only
                // when the initial ratio was not measured.
                // R13.14 (round 5, P1): this guard was `>= 0.0f` while its sibling eleven
                // lines up uses `> 0.0f` -- the same expression written twice with two rules.
                // A MEASURED initial_rel_error of exactly 0 gave ref = 0, so any nonzero
                // residual read as divergence. And `krylov_diverged` is consumed ABOVE the r0
                // max clause, so a solve with unmeasured r0 returned KrylovDiverged from a
                // ||b||-coordinate comparison and the r0 evidence was never reached at all.
                // Divergence is now only declared when the reference it is relative to was
                // measured; an unmeasured one is counted, not substituted.
                {
                    const bool ref_measured =
                        (gmres_result.initial_rel_error > 0.0f &&
                         std::isfinite(gmres_result.initial_rel_error));
                    if (ref_measured) {
                        const float ref = gmres_result.initial_rel_error;
                        if (std::isfinite(gmres_result.rel_error) &&
                            gmres_result.rel_error > ref * (1.0f + 1.0e-4f)) {
                            stats_.krylov_diverged = true;
                        }
                    }
                }
                // R13.8: what "success" was supposed to mean.
                // R13.15 (external review P1-5): InitialConverged ALSO satisfied the tolerance
                // -- it is the case where x0 already did -- and was excluded from the count of
                // solves that reached it. That made "how many linear solves actually finished"
                // an undercount precisely on the iterations where Newton was closest.
                // R13.19 SELF-REVIEW (round 8, P1-B): a consumer left behind when its
                // producer's meaning changed. R13.15 added InitialConverged here because it "did
                // satisfy the tolerance" -- true when that return reported a single metric.
                // R13.19 made it report two, and a solve can now reach this line having met the
                // D objective and NOT the S one. Counting that as a finished solve overstates
                // exactly the case the objective-mismatch work exists to surface.
                if (gmres_result.termination_reason ==
                        KrylovTerminationReason::ToleranceReached ||
                    (gmres_result.termination_reason ==
                         KrylovTerminationReason::InitialConverged &&
                     gmres_result.S_tolerance_reached)) {
                    stats_.gmres_tolerance_reached++;
                }

                // ============================================================
                // PR 9B: opt-in directional consistency check — the checker
                // itself lives in jvp_check::run_directional_consistency_check
                // (review refactor + evidence strengthening; see its docs).
                // The omega_update_ref_per_newton fail-close skip and the
                // snapshot/restore of the loop-local JVP telemetry stay HERE,
                // the only scope with access to those locals.
                // ============================================================
                if (jvp_check_this_iter) {
                    if (wrf::sdirk3::g_sdirk3_config.omega_update_ref_per_newton) {
                        emit_stage_diag([&](std::ostream& os) {
                            os << "SDIRK3_STAGE4_JVP_DIAG ts=" << global_timestep_
                               << " stage=" << stage
                               << " newton_iter=" << newton_iter
                               << " skipped=1 reason=omega_update_ref_per_newton\n";
                        });
                    } else {
                        const int sv_calls = jvp_call_count;
                        const int sv_ad = jvp_ad_calls;
                        const int sv_fd = jvp_fd_calls;
                        const int sv_fdf = jvp_fd_forward_calls;
                        const int sv_fdc = jvp_fd_central_calls;
                        const int sv_ea = jvp_eps_auto_calls;
                        const int sv_em = jvp_eps_manual_calls;
                        const int sv_es = jvp_eps_sample_count;
                        const double sv_esum = jvp_eps_sum;
                        const float sv_emin = jvp_eps_min;
                        const float sv_emax = jvp_eps_max;
                        const double sv_ms = total_jvp_time_ms;
                        jvp_call_count = std::max(jvp_call_count, 1000);
                        {
                            jvp_check::Context ctx;
                            ctx.ts = global_timestep_;
                            ctx.stage = stage;
                            ctx.newton_iter = newton_iter;
                            ctx.dt = dt;
                            ctx.gamma = gamma;
                            {
                                torch::NoGradGuard no_grad;
                                ctx.U_stage = U_stage.detach().clone();
                                ctx.K = K.detach().clone();
                                ctx.U_eval = U_eval.detach().clone();
                                if (dK.defined()) ctx.dK = dK.detach().clone();
                            }
                            ctx.scaled = scaling_initialized_;
                            if (ctx.scaled) {
                                ctx.S_diag = S_diag_;
                                ctx.S_inv_diag = S_inv_diag_;
                            }
                            ctx.layout =
                                layout_initialized_ ? &cached_layout_ : nullptr;
                            ctx.basis = &jvp_check_basis;
                            ctx.compute_rhs = compute_rhs;
                            ctx.apply_jacobian = apply_jacobian;
                            ctx.gmres_op = gmres_op;
                            jvp_check::run_directional_consistency_check(ctx);
                        }
                        jvp_call_count = sv_calls;
                        jvp_ad_calls = sv_ad;
                        jvp_fd_calls = sv_fd;
                        jvp_fd_forward_calls = sv_fdf;
                        jvp_fd_central_calls = sv_fdc;
                        jvp_eps_auto_calls = sv_ea;
                        jvp_eps_manual_calls = sv_em;
                        jvp_eps_sample_count = sv_es;
                        jvp_eps_sum = sv_esum;
                        jvp_eps_min = sv_emin;
                        jvp_eps_max = sv_emax;
                        total_jvp_time_ms = sv_ms;
                    }
                }

                if (!gmres_success) {
                    if (gmres_rel_error > 0.5f) {
                        std::cerr << "[NEWTON] GMRES failed badly (rel_error=" << gmres_rel_error
                                  << "): " << gmres_result.message << std::endl;
                        {
                            // WHY zero progress. rel_error exactly 1 is the signature of a
                            // TRIVIAL least-squares minimiser: if the Krylov solution x is ~0
                            // then b - A*0 = b and the ratio is 1 by construction, which says the
                            // right-hand side is nearly orthogonal to the space the Arnoldi
                            // process built -- a very different diagnosis from "the solve made
                            // progress but not enough". Printing ||x|| against ||b|| separates
                            // them in one line.
                            // A CAUTION FOR ANYONE COMPARING RUNS WITH THIS. A preconditioner
                            // on/off pair is NOT a controlled A/B: stage 1 SOLVES in both cases,
                            // so M changes its solution, which changes the state entering stage 2
                            // and therefore b itself. Measured: the very first failure record
                            // already differs, ||b|| = 464.6 with M against 217.6 without. Any
                            // sound M-on/M-off comparison needs a FIXED (A, b) harness driving
                            // one linear solve, not two model runs.
                            torch::NoGradGuard ng_probe;
                            const double xn =
                                gmres_result.x.defined()
                                    ? gmres_result.x.norm().to(torch::kCPU).item<double>()
                                    : -1.0;
                            // ||b|| recovered from the reported ratio: rel_error = ||r_true||/||b||
                            const double bn = (gmres_result.rel_error > 0.0f)
                                ? static_cast<double>(gmres_result.final_residual) /
                                  static_cast<double>(gmres_result.rel_error)
                                : -1.0;
                            // D3: the Arnoldi/Givens ESTIMATE against the recomputed true
                            // residual. If the estimate falls while the true ratio sits at 1, the
                            // Arnoldi relation is being violated somewhere between the
                            // least-squares solve and the recomputation -- a different diagnosis
                            // from "the operator is hard".
                            // rel_error is the UNSCALED ||b-Ax||/||b||, but GMRES minimises the
                            // BLOCK-SCALED ||D^-1(b - A M^-1 z)||. Different norms -- which is why
                            // this can exceed 1 without violating the least-squares property:
                            // x=0 bounds the SCALED residual, not this one. Reporting progress in
                            // a norm the solver does not optimise is a category error, and every
                            // coefficient comparison in this campaign so far has been read off it.
                            std::cerr << "SDIRK3_GMRES_ESTIMATE_VS_TRUE"
                                      << " internal_iters=" << gmres_result.iterations
                                      << " final_residual=" << gmres_result.final_residual
                                      << " rel_error_UNSCALED=" << gmres_result.rel_error
                                      << " note=minimised_norm_is_block_scaled"
                                      << std::endl;
                            // NOT THE MINIMISED NORM -- retained as telemetry with its status in
                            // the output string. GMRES iterates the S-conjugated operator when
                            // block scaling is on, so what it actually minimises is
                            // ||S^-1(b - Ax)|| / ||S^-1 b||, not the unscaled ratio reported as
                            // rel_error. Reading progress off the unscaled number is a category
                            // error, so both are printed here and the scaled one is the
                            // solver-progress metric.
                            if (scaling_initialized_ && S_diag_.defined() &&
                                gmres_result.r_true.defined() &&
                                gmres_rhs.defined()) {
                                const auto sinv = 1.0 / S_diag_.to(torch::kFloat64);
                                const double rs = (gmres_result.r_true.to(torch::kFloat64) * sinv)
                                                      .norm().to(torch::kCPU).item<double>();
                                const double bs = (gmres_rhs.to(torch::kFloat64) * sinv)
                                                      .norm().to(torch::kCPU).item<double>();
                                std::cerr << "SDIRK3_GMRES_S_SCALE_RESIDUAL_TELEMETRY_ONLY"
                                          << " scaled_rel=" << (bs > 0.0 ? rs / bs : -1.0)
                                          << " num=" << rs << " den=" << bs
                                          << "  (TELEMETRY ONLY -- NOT the minimised norm: the "
                                             "internal convergence check uses D_inv block scaling, "
                                             "not S_diag. Ranking configurations by this number "
                                             "inverted the results twice; use error_tensor.)"
                                          << std::endl << std::flush;
                            }
                            std::cerr << "SDIRK3_GMRES_TRIVIALITY ||x||=" << xn
                                      << " ||b||=" << bn
                                      << " ratio=" << (bn > 0.0 ? xn / bn : -1.0)
                                      << std::endl << std::flush;
                        }
                    } else {
                        std::cerr << "[NEWTON] GMRES did not converge (rel_error=" << gmres_rel_error
                                  << ") but usable for inexact Newton: " << gmres_result.message << std::endl;
                    }
                }

                // v20.11: Per-block GMRES true residual diagnostic
                // Shows which variable blocks contribute most to GMRES plateau.
                // Gated: debug_level >= 1, first newton iter only, layout must be initialized.
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                    newton_iter == 0 && layout_initialized_ &&
                    gmres_result.r_true.defined() &&
                    cached_layout_.total_size == gmres_result.r_true.numel()) {
                    torch::NoGradGuard no_grad;
                    // Apply halo zeroing to r_true for per-block diagnostic.
                    // For 1D packed state: no-op (halo mask disabled, raw basis).
                    // For 3D state: zeroes boundary halos for consistency with GMRES true_err.
                    auto r_halo_zeroed = gmres_result.r_true.clone();
                    apply_halo_zeroing(r_halo_zeroed);
                    auto r_cpu = r_halo_zeroed.to(torch::kCPU).contiguous();
                    auto b_inner = gmres_rhs.detach().clone();
                    apply_halo_zeroing(b_inner);
                    auto b_cpu = b_inner.to(torch::kCPU).contiguous();
                    float b_total_norm = b_cpu.norm().item<float>();
                    std::ostringstream blk_ss;
                    // v20.14r27k: Label clarified — "share" is energy share (r²/||b||², sums to ≤1).
                    // "rel" is per-block relative error (r_block/b_block).
                    // Compare with [GMRES BLOCK r/b] which shows r_block/||b||_total (linear).
                    blk_ss << "[GMRES BLOCK RESIDUAL] ";
                    for (const auto& blk : cached_layout_.blocks) {
                        if (blk.start + blk.size <= r_cpu.numel()) {
                            auto r_blk = r_cpu.slice(0, blk.start, blk.start + blk.size);
                            auto b_blk = b_cpu.slice(0, blk.start, blk.start + blk.size);
                            float r_n = r_blk.norm().item<float>();
                            float b_n = b_blk.norm().item<float>();
                            // v20.14r27h: Use squared fraction so sum(share) ≤ 1
                            float share = (b_total_norm > 0) ? (r_n * r_n) / (b_total_norm * b_total_norm) : 0.0f;
                            float blk_rel = (b_n > 0) ? (r_n / b_n) : 0.0f;
                            blk_ss << blk.name << "=" << std::scientific << std::setprecision(3)
                                   << r_n << "(rel=" << std::fixed << std::setprecision(3) << blk_rel
                                   << ",share=" << share << ") ";
                        }
                    }
                    std::cerr << blk_ss.str() << std::defaultfloat << std::endl;
                }

                // v20.14: Adaptive theta_acoustic_factor adjustment
                // After Newton iter 0, use ph/t pair-relative shares to auto-tune.
                // pair_frac = (r_ph² + r_t²) / r_total² — how much of residual is ph+t.
                // ph_share = r_ph² / (r_ph² + r_t²) — ph share within the ph+t pair.
                // Only adjust when pair_frac > 0.1 (ph+t is meaningful vs ru).
                // Rule: ph_share > high_thresh && t_share < low_thresh → factor += step
                //        t_share > high_thresh && ph_share < low_thresh → factor -= step
                // Clamp to [0.0, 0.35].
                // Lock policy: lock on adjustment OR meaningful no-op (pair_frac > 0.1).
                // Do NOT lock when pair_frac too small — allows retry with better signal.
                {
                    if (!adaptive_tuning_once_per_run_ && newton_iter == 0 && stage == 1 &&
                        wrf::sdirk3::g_sdirk3_config.adaptive_retune_mode == 1 &&
                        preconditioner_ && layout_initialized_ &&
                        gmres_result.r_true.defined() &&
                        cached_layout_.total_size == gmres_result.r_true.numel()) {

                    // v20.14r27y: Skip only when GMRES truly diverged (rel_error > 1).
                    // gmres_success=false merely means tolerance wasn't met; the residual
                    // block shares at rel_error=0.75 are still valid preconditioner signal.
                    if (gmres_result.rel_error > 1.0f) {
                        std::cerr << "[ADAPTIVE TUNING] Skipped: GMRES diverged"
                                  << " (rel_error=" << gmres_result.rel_error
                                  << " > 1.0)" << std::endl;
                    } else {
                        torch::NoGradGuard no_grad;
                        // Halo zeroing: no-op for 1D packed (raw basis), active for 3D.
                        auto r_adapt = gmres_result.r_true.clone();
                        apply_halo_zeroing(r_adapt);
                        auto r_adapt_cpu = r_adapt.to(torch::kCPU).contiguous();

                        // v20.14r27b: Use ph/t pair-relative shares.
                        float r_ph_sq = 0.0f, r_t_sq = 0.0f, r_total_sq = 0.0f;
                        for (const auto& blk : cached_layout_.blocks) {
                            if (blk.start + blk.size <= r_adapt_cpu.numel()) {
                                float r_n = r_adapt_cpu.slice(0, blk.start, blk.start + blk.size).norm().item<float>();
                                float r_sq = r_n * r_n;
                                r_total_sq += r_sq;
                                if (blk.name == "ph") { r_ph_sq = r_sq; }
                                else if (blk.name == "t") { r_t_sq = r_sq; }
                            }
                        }

                        float pair_sq = r_ph_sq + r_t_sq;
                        if (r_total_sq < 1e-20f || pair_sq < 1e-20f) {
                            std::cerr << "[ADAPTIVE TUNING] Skipped: degenerate residual"
                                      << " (r_total_sq=" << r_total_sq
                                      << ", pair_sq=" << pair_sq << ")" << std::endl;
                        } else {
                            float pair_frac = pair_sq / r_total_sq;
                            float ph_frac = r_ph_sq / pair_sq;
                            float t_frac = r_t_sq / pair_sq;
                            auto* unified = dynamic_cast<UnifiedPreconditioner*>(preconditioner_);
                            if (!unified) {
                                std::cerr << "[ADAPTIVE TUNING] Skipped: not UnifiedPreconditioner" << std::endl;
                            } else if (pair_frac < 0.1f) {
                                // v20.14r27z: ru-dominance check runs BEFORE quality gate.
                                // ru-dominance is a structural preconditioner property — valid
                                // even when GMRES rel_error is poor (quality gate would skip).
                                ++ru_dominance_count_;
                                float ru_frac = 1.0f - pair_frac;
                                constexpr int lock_threshold = 2;
                                if (ru_dominance_count_ >= lock_threshold) {
                                    adaptive_tuning_once_per_run_ = true;
                                    std::cerr << "[ADAPTIVE TUNING] Locked (ru dominates, "
                                              << ru_dominance_count_ << " consecutive):"
                                              << " ru_frac=" << std::fixed << std::setprecision(3) << ru_frac
                                              << ", pair_frac=" << pair_frac
                                              << ". ph/t tuning irrelevant."
                                              << std::defaultfloat << std::endl;
                                } else {
                                    std::cerr << "[ADAPTIVE TUNING] ru dominance observed ("
                                              << ru_dominance_count_ << "/" << lock_threshold << "):"
                                              << " ru_frac=" << std::fixed << std::setprecision(3) << ru_frac
                                              << ", pair_frac=" << pair_frac
                                              << ". Deferring lock."
                                              << std::defaultfloat << std::endl;
                                }
                            } else if (pair_frac < 0.3f) {
                                // Guard against branch flapping in the transition band:
                                // keep collecting ru-dominance evidence instead of taking a
                                // one-shot ph/t tuning decision from an ambiguous sample.
                                std::cerr << "[ADAPTIVE TUNING] Ambiguous ph/t signal: "
                                          << "pair_frac=" << pair_frac
                                          << " in [0.1,0.3). Deferring tuning, preserving ru counter ("
                                          << ru_dominance_count_ << ")." << std::endl;
                            } else {
                                // pair_frac >= 0.3: ph+t signal is meaningful enough to tune.
                                ru_dominance_count_ = 0;
                                const auto& acfg = wrf::sdirk3::g_sdirk3_config;

                                // v20.14r27z: Quality gate only gates tuning adjustment,
                                // NOT ru-dominance counting (which already ran above).
                                // Note: this block executes only when adaptive_retune_mode == 1.
                                if (gmres_result.rel_error >= wrf::sdirk3::g_sdirk3_config.adaptive_quality_gate) {
                                    std::cerr << "[ADAPTIVE TUNING] Skipped tuning: quality gate miss"
                                              << " (rel_error=" << gmres_result.rel_error
                                              << " >= gate=" << wrf::sdirk3::g_sdirk3_config.adaptive_quality_gate
                                              << ", pair_frac=" << pair_frac << ")" << std::endl;
                                } else {
                                    float old_factor = unified->get_theta_acoustic_factor();
                                    float new_factor = old_factor;
                                    if (ph_frac > acfg.adaptive_high_threshold && t_frac < acfg.adaptive_low_threshold) {
                                        new_factor = old_factor + acfg.adaptive_step_size;
                                    } else if (t_frac > acfg.adaptive_high_threshold && ph_frac < acfg.adaptive_low_threshold) {
                                        new_factor = old_factor - acfg.adaptive_step_size;
                                    }
                                    new_factor = std::clamp(new_factor, 0.0f, 0.35f);

                                    if (std::abs(new_factor - old_factor) > 1e-6f) {
                                        adaptive_tuning_once_per_run_ = true;
                                        unified->set_theta_acoustic_factor(new_factor);
                                        std::cerr << "[ADAPTIVE TUNING] theta_acoustic_factor: "
                                                  << old_factor << " -> " << new_factor
                                                  << " (ph_share=" << ph_frac
                                                  << ", t_share=" << t_frac
                                                  << ", pair_frac=" << pair_frac << ")" << std::endl;
                                    } else {
                                        std::cerr << "[ADAPTIVE TUNING] No adjustment needed"
                                                  << " (ph_share=" << ph_frac
                                                  << ", t_share=" << t_frac
                                                  << ", pair_frac=" << pair_frac
                                                  << ", factor=" << old_factor
                                                  << "). Keeping adaptive tuning unlocked." << std::endl;
                                    }
                                }
                            }
                        }
                    } // diverged else
                    } // outer guard (iter==0 && layout)
                }
            } catch (const std::exception& e) {
                ERROR_PRINT("ERROR in GMRES: " << e.what());
                stats_.converged = false;
                stats_.final_residual = res_norm_for_stats;
                stats_.final_residual_measured = true;

                // Return failure result
                WRFNewtonKrylovSolver::NewtonResult result;
                result.K = K;
                result.converged = false;
                result.iterations = options_.max_newton_iter;
                result.final_residual = stats_.final_residual;
                result.message = std::string("GMRES exception: ") + e.what();
                // R13.17 self-review: this exit was UNTYPED, and
                // NewtonTerminationReason::Exception had ZERO producers -- an enum value with no
                // writer is the same defect class as a field with no reader, and it was
                // introduced by the commit that added the enum to end reconstruction.
                stats_.newton_termination = static_cast<int>(
                    wrf::sdirk3::NewtonTerminationReason::Exception);
                // JVP call summary even on GMRES failure (for diagnosis)
                std::cerr << "[Newton] JVP calls before exception: " << jvp_call_count
                          << ", total JVP time: " << total_jvp_time_ms << " ms" << std::endl;
                update_stage_predictor_cache(false, result.K);
                return result;
            }
            
            // Krylov counter updated inside try block above (gmres_result scoped there).
            int gmres_iters = stats_.total_krylov_iterations - gmres_start_iter;
            // Always print GMRES+Newton update summary
            {
                torch::NoGradGuard no_grad;
                float dK_n = dK.norm().to(torch::kCPU).item<float>();
                std::cerr << "[Newton] GMRES: " << gmres_iters << " krylov iters, ||dK||=" << dK_n
                         << ", rel_error=" << gmres_rel_error
                         << (gmres_success ? " (OK)" : " (FAIL)")
                         << std::endl;
            }
            // JVP call summary for this Newton iteration
            std::cerr << "[Newton] JVP calls: " << jvp_call_count
                      << ", total JVP time: " << total_jvp_time_ms << " ms"
                      << ", avg: " << (jvp_call_count > 0 ? total_jvp_time_ms / jvp_call_count : 0.0)
                      << " ms/call" << std::endl;
            if (variable_pc_event_this_newton &&
                wrf::sdirk3::g_sdirk3_config.variable_pc_event_log) {
                std::cerr << "[VARIABLE-PC EVENT] stage=" << stage
                          << ", newton=" << newton_iter
                          << " (fallback-lock or refinement-disable detected)" << std::endl;
            }
            if (wrf::sdirk3::g_sdirk3_config.solver_telemetry) {
                const char* jvp_mode_name =
                    (jvp_locked_mode_ == 0) ? "forward" :
                    (jvp_locked_mode_ == 1) ? "central" : "unlocked";
                const char* jvp_fd_mode_name =
                    (jvp_fd_forward_calls > 0 && jvp_fd_central_calls == 0) ? "forward" :
                    (jvp_fd_central_calls > 0 && jvp_fd_forward_calls == 0) ? "central" :
                    (jvp_fd_calls > 0) ? "mixed" : "none";
                const float jvp_eps_mean = (jvp_eps_sample_count > 0)
                    ? static_cast<float>(jvp_eps_sum / static_cast<double>(jvp_eps_sample_count))
                    : 0.0f;
                const float jvp_eps_min_safe = (jvp_eps_sample_count > 0) ? jvp_eps_min : 0.0f;
                const float jvp_eps_max_safe = (jvp_eps_sample_count > 0) ? jvp_eps_max : 0.0f;
                std::cerr << "[SOLVER TELEMETRY] stage=" << stage
                          << ", newton=" << newton_iter
                          << ", gmres_iters=" << gmres_iters
                          << ", gmres_rel=" << gmres_rel_error
                          << ", jvp_calls=" << jvp_call_count
                          << ", jvp_ms=" << total_jvp_time_ms
                          << ", warmstart=" << (gmres_warmstart_used ? "on" : "off")
                          << ", inn_status=" << gmres_inn_status
                          << ", inn_reason_code=" << gmres_inn_reason_code
                          << ", inn_candidate=" << (gmres_inn_candidate_built ? "yes" : "no")
                          << ", inn_used=" << (gmres_inn_used ? "yes" : "no")
                          << ", inn_gate_pass=" << (gmres_inn_gate_pass ? "yes" : "no")
                          << ", inn_q=" << gmres_inn_q
                          << ", inn_r_base=" << gmres_inn_r_base
                          << ", inn_r_cand=" << gmres_inn_r_cand
                          << ", inn_x0_base_norm=" << gmres_inn_x0_base_norm
                          << ", inn_x0_cand_norm=" << gmres_inn_x0_cand_norm
                          << ", inn_x0_delta_norm=" << gmres_inn_x0_delta_norm
                          << ", inn_x0_rel_delta=" << gmres_inn_x0_rel_delta
                          << ", inn_gate_rel_diff=" << gmres_inn_gate_rel_diff
                          << ", inn_gate_non_degrade=" << (gmres_inn_gate_non_degrade ? "yes" : "no")
                          << ", inn_gate_quality_ok=" << (gmres_inn_gate_quality_ok ? "yes" : "no")
                          << ", inn_tol_ramped=" << (gmres_inn_tol_ramped ? "yes" : "no")
                          << ", inn_base_warmstart=" << (gmres_warmstart_used ? "yes" : "no")
                          << ", inn_prev_shape_ok=" << (gmres_inn_prev_shape_ok ? "yes" : "no")
                          << ", inn_prev_quality_ok=" << (gmres_inn_prev_quality_ok ? "yes" : "no")
                          << ", inn_prev_varpc_ok=" << (gmres_inn_prev_varpc_ok ? "yes" : "no")
                          << ", inn_prev_rel=" << gmres_inn_prev_rel
                          << ", inn_ru_share_base=" << gmres_inn_ru_share_base
                          << ", inn_ru_share_cand=" << gmres_inn_ru_share_cand
                          << ", inn_rw_share_base=" << gmres_inn_rw_share_base
                          << ", inn_rw_share_cand=" << gmres_inn_rw_share_cand
                          << ", inn_ph_share_base=" << gmres_inn_ph_share_base
                          << ", inn_ph_share_cand=" << gmres_inn_ph_share_cand
                          << ", variable_pc=" << (variable_pc_event_this_newton ? "yes" : "no")
                          << ", jvp_mode=" << jvp_mode_name
                          << ", jvp_fd_mode=" << jvp_fd_mode_name
                          << ", jvp_ad_calls=" << jvp_ad_calls
                          << ", jvp_fd_calls=" << jvp_fd_calls
                          << ", jvp_eps_mean=" << jvp_eps_mean
                          << ", jvp_eps_min=" << jvp_eps_min_safe
                          << ", jvp_eps_max=" << jvp_eps_max_safe
                          << ", jvp_eps_auto_calls=" << jvp_eps_auto_calls
                          << ", jvp_eps_manual_calls=" << jvp_eps_manual_calls
                          << ", ew_eta_enabled=" << (ew_eta_enabled_this_iter ? "yes" : "no")
                          << ", ew_eta=" << ew_eta_used_this_iter
                          << ", ew_eta_updated=" << (ew_eta_updated_this_iter ? "yes" : "no")
                          << ", krylov_tol_final=" << krylov_tol_adaptive
                          << ", krylov_tol_stage_override=" << (krylov_tol_stage_override ? "yes" : "no")
                          << ", budget_coupled=" << (stage_budget_forcing_coupled ? "yes" : "no")
                          << ", budget_eta=" << stage_budget_forcing_eta
                          << ", budget_scale=" << stage_budget_scale
                          << std::endl;
            }

            // DIAGNOSTIC 2026-02-01 (v17 fix): JVP AD vs FD comparison — fires once when GMRES stalls
            // Triggered by gmres_rel_error > 0.9 (GMRES nearly useless)
            // v17: Uses COMPONENT-SCALED epsilon to avoid float32 precision collapse.
            // Previous version used global ε=1e-4 with unit-norm v_test, which produced
            // per-element perturbations ~9.6e-8 — below float32 ULP for mu (~88000),
            // ph (~100000), t (~290), and even u (~10). Only w (~0.01) was resolved.
            // Result: FD missed all cross-sensitivities, causing false "5.7× μ error".
            // Fix: Scale v_test so each block's perturbation is ~1e-5 relative to ||U_block||.
            {
                if (!jvp_vs_fd_done_ && gmres_rel_error > 0.9f &&
                    wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                    jvp_vs_fd_done_ = true;
                    std::cerr << "\n========== [JVP_VS_FD] GMRES stalled (rel_error="
                              << gmres_rel_error << "), running AD vs FD comparison ==========" << std::endl;
                    try {
                        // Build component-scaled perturbation direction
                        // Each block is scaled so that ||ε * v_block|| / ||U_block|| ≈ target_rel
                        // This ensures every component is well above float32 ULP
                        torch::Tensor v_test;
                        float fd_eps;
                        {
                            torch::NoGradGuard no_grad;
                            v_test = torch::randn_like(U_eval);

                            // Get block layout
                            StateLayout layout;
                            if (layout_initialized_) {
                                layout = cached_layout_;
                            } else {
                                // 9F.D96 (review section 10): NO heuristic fallback. This is a PER-BLOCK
                                // diagnostic -- it attributes residuals to NAMED variables -- and
                                // infer_from_size() guessed the boundaries from hard-coded percentages
                                // (20.2%, 19.9%, ...). A per-block report against guessed boundaries
                                // labels rv values as ru: confidently wrong, and worse for debugging
                                // than no report. Leave the layout EMPTY so the consumer loop below
                                // iterates nothing, and say so once.
                                static std::atomic<bool> warned_no_layout{false};
                                bool expected_no_layout = false;
                                if (warned_no_layout.compare_exchange_strong(expected_no_layout, true)) {
                                    std::cerr << "[JVP CHECK] per-block analysis SKIPPED: no exact "
                                                 "StateLayout (refusing to guess block boundaries)"
                                              << std::endl;
                                }
                            }

                            auto U_cpu = U_eval.detach().to(torch::kCPU).contiguous();

                            // Scale each block so relative perturbation is uniform
                            const float target_rel = 1e-5f;  // Target relative perturbation per block
                            for (const auto& blk : layout.blocks) {
                                auto U_blk = U_cpu.slice(0, blk.start, blk.start + blk.size);
                                auto v_blk = v_test.slice(0, blk.start, blk.start + blk.size);
                                float U_blk_rms = U_blk.norm().item<float>() / std::sqrt(static_cast<float>(blk.size));
                                float v_blk_rms = v_blk.norm().item<float>() / std::sqrt(static_cast<float>(blk.size));
                                // Scale v_blk so that: eps * v_blk_rms ≈ target_rel * U_blk_rms
                                // We'll use eps=1, so v_blk *= target_rel * U_blk_rms / v_blk_rms
                                if (v_blk_rms > 1e-30f) {
                                    float scale = target_rel * std::max(U_blk_rms, 1e-6f) / v_blk_rms;
                                    v_blk.mul_(scale);
                                }
                            }

                            // Use eps=1 since scaling is baked into v_test
                            fd_eps = 1.0f;

                            // Report perturbation quality
                            std::cerr << "[JVP_VS_FD] Component-scaled v_test (target_rel=" << target_rel << "):" << std::endl;
                            for (const auto& blk : layout.blocks) {
                                auto U_blk = U_cpu.slice(0, blk.start, blk.start + blk.size);
                                auto v_blk = v_test.slice(0, blk.start, blk.start + blk.size);
                                float U_rms = U_blk.norm().item<float>() / std::sqrt(static_cast<float>(blk.size));
                                float v_rms = v_blk.detach().to(torch::kCPU).norm().item<float>() / std::sqrt(static_cast<float>(blk.size));
                                float actual_rel = (U_rms > 1e-30f) ? v_rms / U_rms : 0.0f;
                                // Check float32 resolvability: perturbation vs ULP
                                float ulp = U_rms * 1.19e-7f;
                                float margin = (ulp > 0) ? v_rms / ulp : 0.0f;
                                std::cerr << "  " << blk.name
                                          << ": U_rms=" << U_rms
                                          << ", v_rms=" << v_rms
                                          << ", rel=" << actual_rel
                                          << ", margin_over_ULP=" << margin << "x" << std::endl;
                            }
                        }

                        // 1. AD JVP (forward-mode dual)
                        torch::Tensor jvp_ad = compute_jvp_forward_mode(
                            [&](const torch::Tensor& x) { return compute_rhs(x); },
                            U_eval, v_test
                        );

                        // 2. FD JVP (central difference, under NoGradGuard)
                        torch::Tensor jvp_fd;
                        {
                            torch::NoGradGuard no_grad;
                            auto F_plus  = compute_rhs(U_eval + fd_eps * v_test);
                            auto F_minus = compute_rhs(U_eval - fd_eps * v_test);
                            jvp_fd = (F_plus - F_minus) / (2.0f * fd_eps);
                        }

                        // 3. Global comparison
                        {
                            torch::NoGradGuard no_grad;
                            auto ad_cpu = jvp_ad.detach().to(torch::kCPU);
                            auto fd_cpu = jvp_fd.detach().to(torch::kCPU);
                            float ad_norm = ad_cpu.norm().item<float>();
                            float fd_norm = fd_cpu.norm().item<float>();
                            float diff_norm = (ad_cpu - fd_cpu).norm().item<float>();
                            float rel_err = diff_norm / std::max(fd_norm, 1e-12f);
                            float cosine = 0.0f;
                            if (ad_norm > 1e-15f && fd_norm > 1e-15f) {
                                cosine = (ad_cpu * fd_cpu).sum().item<float>() / (ad_norm * fd_norm);
                            }
                            std::cerr << "[JVP_VS_FD] GLOBAL: ||Jv_ad||=" << ad_norm
                                      << ", ||Jv_fd||=" << fd_norm
                                      << ", ||diff||=" << diff_norm
                                      << ", rel_err=" << rel_err
                                      << ", cosine=" << cosine << std::endl;
                        }

                        // 4. Per-block comparison using StateLayout
                        {
                            torch::NoGradGuard no_grad;
                            auto ad_cpu = jvp_ad.detach().to(torch::kCPU).contiguous();
                            auto fd_cpu = jvp_fd.detach().to(torch::kCPU).contiguous();
                            StateLayout layout;
                            if (layout_initialized_) {
                                layout = cached_layout_;
                            } else {
                                // 9F.D96 (review section 10): NO heuristic fallback. This is a PER-BLOCK
                                // diagnostic -- it attributes residuals to NAMED variables -- and
                                // infer_from_size() guessed the boundaries from hard-coded percentages
                                // (20.2%, 19.9%, ...). A per-block report against guessed boundaries
                                // labels rv values as ru: confidently wrong, and worse for debugging
                                // than no report. Leave the layout EMPTY so the consumer loop below
                                // iterates nothing, and say so once.
                                static std::atomic<bool> warned_no_layout{false};
                                bool expected_no_layout = false;
                                if (warned_no_layout.compare_exchange_strong(expected_no_layout, true)) {
                                    std::cerr << "[JVP CHECK] per-block analysis SKIPPED: no exact "
                                                 "StateLayout (refusing to guess block boundaries)"
                                              << std::endl;
                                }
                            }
                            for (const auto& blk : layout.blocks) {
                                auto ad_blk = ad_cpu.slice(0, blk.start, blk.start + blk.size);
                                auto fd_blk = fd_cpu.slice(0, blk.start, blk.start + blk.size);
                                float ad_n = ad_blk.norm().item<float>();
                                float fd_n = fd_blk.norm().item<float>();
                                float diff_n = (ad_blk - fd_blk).norm().item<float>();
                                float rel = diff_n / std::max(fd_n, 1e-12f);
                                float cos_blk = 0.0f;
                                if (ad_n > 1e-15f && fd_n > 1e-15f) {
                                    cos_blk = (ad_blk * fd_blk).sum().item<float>() / (ad_n * fd_n);
                                }
                                std::cerr << "[JVP_VS_FD] " << blk.name
                                          << ": ||ad||=" << ad_n << ", ||fd||=" << fd_n
                                          << ", rel_err=" << rel << ", cos=" << cos_blk << std::endl;
                            }
                        }
                        std::cerr << "========== [JVP_VS_FD] END ==========" << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "[JVP_VS_FD] EXCEPTION: " << e.what() << std::endl;
                    }
                }
            }

            // DIAGNOSTIC 2026-02-01: Near-singularity check for A = (I - dt*γ*J)
            // If ||A*v|| ≈ 0 for v = R/||R||, the operator has a near-null direction
            // aligned with the residual, explaining GMRES stalling at rel_error ≈ 1.
            {
                if (!singularity_check_done_ && gmres_rel_error > 0.9f &&
                    wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                    singularity_check_done_ = true;
                    std::cerr << "\n========== [SINGULAR_CHECK] A = (I - dt*γ*J) near-singularity test ==========" << std::endl;
                    try {
                        // v = R / ||R|| (unit vector in residual direction)
                        torch::Tensor v_r;
                        float R_norm_val;
                        {
                            torch::NoGradGuard no_grad;
                            R_norm_val = R.detach().norm().to(torch::kCPU).item<float>();
                        }
                        if (R_norm_val > 1e-15f) {
                            v_r = R.detach() / R_norm_val;
                        } else {
                            v_r = torch::randn_like(R);
                            torch::NoGradGuard no_grad;
                            float vn = v_r.norm().to(torch::kCPU).item<float>();
                            v_r = v_r / vn;
                        }

                        // A*v = apply_jacobian(v) = v - J*(dt*γ*v)
                        // apply_jacobian already does: result = dK - compute_jvp_forward_mode(compute_rhs, U_eval, dt*γ*dK)
                        auto Av = apply_jacobian(v_r);
                        float Av_norm, vTAv;
                        {
                            torch::NoGradGuard no_grad;
                            auto Av_cpu = Av.detach().to(torch::kCPU);
                            auto v_cpu = v_r.detach().to(torch::kCPU);
                            Av_norm = Av_cpu.norm().item<float>();
                            vTAv = (v_cpu * Av_cpu).sum().item<float>();
                        }
                        float rayleigh = vTAv;  // ||v||=1 so vTAv/vTv = vTAv
                        std::cerr << "[SINGULAR_CHECK] v = R/||R||:"
                                  << " ||A*v||=" << Av_norm
                                  << ", ||v||=1"
                                  << ", Rayleigh λ = vᵀAv/vᵀv = " << rayleigh
                                  << std::endl;

                        // Per-block analysis of Av
                        {
                            torch::NoGradGuard no_grad;
                            auto Av_cpu = Av.detach().to(torch::kCPU).contiguous();
                            auto v_cpu = v_r.detach().to(torch::kCPU).contiguous();
                            StateLayout layout;
                            if (layout_initialized_) {
                                layout = cached_layout_;
                            } else {
                                // 9F.D96 (review section 10): NO heuristic fallback. This is a PER-BLOCK
                                // diagnostic -- it attributes residuals to NAMED variables -- and
                                // infer_from_size() guessed the boundaries from hard-coded percentages
                                // (20.2%, 19.9%, ...). A per-block report against guessed boundaries
                                // labels rv values as ru: confidently wrong, and worse for debugging
                                // than no report. Leave the layout EMPTY so the consumer loop below
                                // iterates nothing, and say so once.
                                static std::atomic<bool> warned_no_layout{false};
                                bool expected_no_layout = false;
                                if (warned_no_layout.compare_exchange_strong(expected_no_layout, true)) {
                                    std::cerr << "[JVP CHECK] per-block analysis SKIPPED: no exact "
                                                 "StateLayout (refusing to guess block boundaries)"
                                              << std::endl;
                                }
                            }
                            for (const auto& blk : layout.blocks) {
                                auto Av_blk = Av_cpu.slice(0, blk.start, blk.start + blk.size);
                                auto v_blk = v_cpu.slice(0, blk.start, blk.start + blk.size);
                                float Av_n = Av_blk.norm().item<float>();
                                float v_n = v_blk.norm().item<float>();
                                float ratio = (v_n > 1e-15f) ? Av_n / v_n : 0.0f;
                                std::cerr << "[SINGULAR_CHECK] " << blk.name
                                          << ": ||Av||=" << Av_n << ", ||v||=" << v_n
                                          << ", ||Av||/||v||=" << ratio << std::endl;
                            }
                        }

                        // Also test with random directions for comparison
                        for (int probe = 0; probe < 3; probe++) {
                            torch::Tensor v_rand;
                            {
                                torch::NoGradGuard no_grad;
                                v_rand = torch::randn_like(R);
                                float vn = v_rand.norm().to(torch::kCPU).item<float>();
                                v_rand = v_rand / vn;
                            }
                            auto Av_rand = apply_jacobian(v_rand);
                            float Av_rand_norm, vTAv_rand;
                            {
                                torch::NoGradGuard no_grad;
                                auto Av_r_cpu = Av_rand.detach().to(torch::kCPU);
                                auto vr_cpu = v_rand.detach().to(torch::kCPU);
                                Av_rand_norm = Av_r_cpu.norm().item<float>();
                                vTAv_rand = (vr_cpu * Av_r_cpu).sum().item<float>();
                            }
                            std::cerr << "[SINGULAR_CHECK] random#" << probe
                                      << ": ||A*v||=" << Av_rand_norm
                                      << ", Rayleigh=" << vTAv_rand << std::endl;
                        }
                        std::cerr << "========== [SINGULAR_CHECK] END ==========" << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "[SINGULAR_CHECK] EXCEPTION: " << e.what() << std::endl;
                    }
                }
            }

            // GMRES solution norm diagnostic.
            // ||dK||/||R|| ≈ ||(I-dt*γ*J)^{-1}|| is the operator's inverse norm.
            // For acoustic modes this can be O(100-10000). This is structural, NOT
            // null-space contamination. The trust-region handles step size control.
            // Do NOT replace with steepest descent — it's catastrophically wrong
            // because -R has ~zero component in the Newton descent direction.
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                torch::NoGradGuard no_grad;
                float dK_n = dK.norm().to(torch::kCPU).item<float>();
                float R_n = R.detach().norm().to(torch::kCPU).item<float>();
                float ratio = (R_n > 1e-15f) ? dK_n / R_n : 0.0f;
                if (ratio > 100.0f) {
                    std::cerr << "[GMRES INFO] ||dK||/||R||=" << ratio
                              << " (operator inverse norm, trust-region controls step)" << std::endl;
                }
            }

            // DEBUG: Check GMRES result (avoid .item() for autodiff compatibility)
            auto dK_norm_tensor = dK.norm();
            auto K_norm_tensor = K.norm();

            // FIX (2025-12-04): Gate debug output behind debug_level >= 1 to avoid .item() overhead
            // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
            // DIAGNOSTIC: Output dK norm for debugging only when enabled
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float dK_norm_val = dK_norm_tensor.to(torch::kCPU).item<float>();
                float K_norm_val = K_norm_tensor.to(torch::kCPU).item<float>();
                float R_norm_val = R.norm().to(torch::kCPU).item<float>();
                std::cerr << "[NEWTON DEBUG] After GMRES:" << std::endl;
                std::cerr << "  ||dK|| = " << dK_norm_val << std::endl;
                std::cerr << "  ||K||  = " << K_norm_val << std::endl;
                std::cerr << "  ||R||  = " << R_norm_val << std::endl;
                std::cerr << "  ||dK||/||K|| = " << (K_norm_val > 1e-14 ? dK_norm_val/K_norm_val : 0) << std::endl;
            }
            
            // Just log for debugging - no modifications
            if (options_.verbose) {
                // Use tensor operations to preserve autodiff graph
                ERROR_PRINT("DEBUG: GMRES update norms (tensors): dK, K, residual");
                ERROR_PRINT("  dt=" << dt << ", gamma=" << gamma << ", dt*gamma=" << dt*gamma);
            }
            
            // JVP CONSISTENCY VALIDATION
            // Compare apply_jacobian output (configured JVP method) vs reference central FD
            // FIX (2025-12-04): Moved to debug_level>=2 - expensive for large domains
            // FIX (2025-12-05): Also gate on !use_autograd to preserve graph in AD mode
            //                   This block uses .item() and NoGradGuard which breaks the graph
            // NOTE: apply_jacobian uses jvp_method from config (FD/autograd/forward-mode)
            //       This validation compares it against an independent central FD reference
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 && newton_iter == 0 &&
                !wrf::sdirk3::g_sdirk3_config.use_autograd) {
                // Guard against zero dK norm before normalization
                auto dK_norm_tensor = dK.norm();
                float dK_norm_val = 0.0f;
                {
                    torch::NoGradGuard no_grad;
                    // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                    dK_norm_val = dK_norm_tensor.to(torch::kCPU).item<float>();
                }

                if (dK_norm_val < 1e-12f) {
                    std::cerr << "\n[JVP VALIDATION] Skipped (||dK|| too small: "
                              << dK_norm_val << ")" << std::endl;
                } else {
                    std::cerr << "\n[JVP VALIDATION] Checking apply_jacobian vs reference central FD..." << std::endl;

                    // Use dK as test vector (it's the Newton direction)
                    torch::Tensor test_vec = dK / dK_norm_tensor;

                    // Compute JVP using configured method: (I - dt*gamma*J)*v
                    torch::Tensor jvp_apply = apply_jacobian(test_vec);

                    // Compute finite-difference Jacobian-vector product (can be in NoGradGuard)
                    torch::Tensor jvp_fd;
                    {
                        torch::NoGradGuard no_grad;
                        float fd_eps = 1e-6f;
                        torch::Tensor K_plus = K + fd_eps * test_vec;
                        torch::Tensor K_minus = K - fd_eps * test_vec;

                        // Evaluate residuals at perturbed states
                        torch::Tensor U_plus = U_stage + dt * gamma * K_plus;
                        torch::Tensor U_minus = U_stage + dt * gamma * K_minus;
                        torch::Tensor F_plus = compute_rhs(U_plus);
                        torch::Tensor F_minus = compute_rhs(U_minus);
                        torch::Tensor R_plus = K_plus - F_plus;
                        torch::Tensor R_minus = K_minus - F_minus;

                        // Central difference: (R(K+ε*v) - R(K-ε*v)) / (2*ε)
                        jvp_fd = (R_plus - R_minus) / (2.0f * fd_eps);
                    }

                    // Compare results (in NoGradGuard since we need .item())
                    {
                        torch::NoGradGuard no_grad;
                        // FIX 2025-12-27: Pre-copy tensors to CPU once for all diagnostics
                        auto jvp_apply_cpu = jvp_apply.to(torch::kCPU);
                        auto jvp_fd_cpu = jvp_fd.to(torch::kCPU);
                        float jvp_apply_norm = jvp_apply_cpu.norm().item<float>();
                        float jvp_fd_norm = jvp_fd_cpu.norm().item<float>();
                        float diff_norm = (jvp_apply_cpu - jvp_fd_cpu).norm().item<float>();
                        float rel_error = (jvp_fd_norm > 1e-12f) ? (diff_norm / jvp_fd_norm) : 0.0f;

                        std::cerr << "  ||(I - dt*gamma*J)*v||_apply  = " << jvp_apply_norm << std::endl;
                        std::cerr << "  ||(I - dt*gamma*J)*v||_ref_fd = " << jvp_fd_norm << std::endl;
                        std::cerr << "  ||difference||                = " << diff_norm << std::endl;
                        std::cerr << "  Relative error                = " << rel_error << std::endl;

                        if (rel_error > 0.1f) {
                            std::cerr << "  [WARNING] Large JVP mismatch (>10%)!" << std::endl;
                            std::cerr << "  This suggests apply_jacobian doesn't match reference FD" << std::endl;

                            // Per-block diagnostic to identify which state variables have discrepancies
                            std::cerr << "\n[JVP BLOCK BREAKDOWN] Analyzing error by state variable:" << std::endl;
                            auto diff = jvp_fd_cpu - jvp_apply_cpu;
                            int offset = 0;
                            for (const auto& block : cached_layout_.blocks) {
                                auto fd_slice = jvp_fd_cpu.slice(0, offset, offset + block.size);
                                auto apply_slice = jvp_apply_cpu.slice(0, offset, offset + block.size);
                                float fd_norm = fd_slice.norm().item<float>();
                                float apply_norm = apply_slice.norm().item<float>();
                                float ratio = (fd_norm > 1e-12f) ? (apply_norm / fd_norm) : 1.0f;
                                std::cerr << "  " << block.name << ": ||ref_fd||=" << fd_norm
                                         << ", ||apply||=" << apply_norm
                                         << ", ratio=" << ratio;
                                if (ratio < 0.1f) {
                                    std::cerr << " [JVP DISCREPANCY!]";
                                } else if (ratio < 0.5f) {
                                    std::cerr << " [PARTIAL]";
                                }
                                std::cerr << std::endl;
                                offset += block.size;
                            }
                        } else if (rel_error > 0.01f) {
                            std::cerr << "  [CAUTION] Moderate Jacobian mismatch (1-10%)" << std::endl;
                        } else {
                            std::cerr << "  [OK] Jacobian validation passed (<1% error)" << std::endl;
                        }
                    }
                }
            }

            // DIAGNOSTIC: Trust region and scaling
            // FIX (2025-12-05): Skip diagnostics in autograd mode to preserve graph
            if (!wrf::sdirk3::g_sdirk3_config.use_autograd) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float dK_norm_val = dK.norm().to(torch::kCPU).item<float>();
                float K_norm_val = K.norm().to(torch::kCPU).item<float>();
                std::cerr << "[TRUST REGION] Before scaling:" << std::endl;
                std::cerr << "  ||dK|| = " << dK_norm_val << std::endl;
                std::cerr << "  ||K|| = " << K_norm_val << std::endl;
            }

            // CRITICAL FIX (2025-10-26): Trust-region must evaluate step quality before acceptance.
            // Implement full accept/reject logic with adaptive radius updates.
            // v20.14r27g: Trust-region is now conditional on config nk_trust_region.
            torch::Tensor dK_scaled = dK;
            torch::Tensor accepted_residual;
            torch::Tensor accepted_residual_norm;
            float alpha = 1.0f;
            float last_rho = 0.0f;
            const int max_trust_attempts = 3;
            bool step_accepted = false;
            // Phase 3C: Combined TR + LS RHS evaluation budget.
            // Trust region (3 evals) + line search (10 evals) = up to 13 RHS per Newton.
            // Cap at 5 total to reduce cost; skip LS when TR already accepted.
            int rhs_budget = 5;
            // Canonical GMRES "total failure" predicate used across trust-off, fallback,
            // and trust-region acceptance branches in this Newton iteration.
            // Referee C7 / red team P1-7: the classifier's divergence rule was moved to r0
            // and THIS predicate -- which drives the recovery attempt, the zero-step handling
            // and gmres_total_failures++ (the classifier's KrylovStagnated trigger) -- still
            // compared against ||b||. A warm start that began at 1.054 and was reduced tripped
            // both clauses. Rule fixed, production consumer reading the old quantity: the
            // eighth instance of that split in this tree.
            //
            // Changing what the solver DOES is not a diagnostic, so the r0 baseline is
            // OPT-IN (default off, baseline byte-identical); the record carries both readings
            // whichever is in force. The fallback when r0 was not measured is the old rule.
            const bool failure_vs_r0 = krylov_failure_vs_r0_;
            // R13.14 (red team round 5, P1): the FALLBACK survived the round-4 fix. R13.13
            // deleted the field that divided by a reference of 1.0 and left the reference, so
            // `total_failure_vs_r0` -- a name ending `_vs_r0`, a signals field ending `_vs_r0`,
            // and a printed `total_failure_vs_r0=` -- still held the ||b|| answer whenever r0
            // was not measured. And under the opt-in flag it changed what the SOLVER DOES: a
            // knob whose stated meaning is "use the r0 rule" silently used the ||b|| rule on any
            // solve that did not measure r0. An unmeasured reference is not a reference of one;
            // the r0 reading is simply UNAVAILABLE, and the record says so.
            const bool r0_measured =
                (gmres_initial_rel_error > 0.0f && std::isfinite(gmres_initial_rel_error));
            // R13.16 (round 6, R6-13): no `: 1.0f`. The guard below short-circuits, so the
            // fallback was already dead -- but leaving the literal kept the constant that
            // produced the round-4 P0 one deleted `r0_measured &&` away from returning.
            const float r0_ref = gmres_initial_rel_error;
            const bool total_failure_vs_b  =
                !gmres_converged_on_entry &&
                (gmres_raw_rel_error > 1.0f || gmres_rel_error >= 0.999f);
            // R13.19 SELF-REVIEW (round 8, P1-A): a solve that CONVERGED ON ENTRY is not a
            // total failure, and after the P0-1 fix it would otherwise be flagged as one
            // UNCONDITIONALLY. `rel_error` now carries rho_S, and `initial_rel_error` is
            // rn0/bn0 taken from the same halo-zeroed r and the same PRE-SCALING b -- the same
            // two norms -- so their ratio is exactly 1 and `raw >= 0.99 * r0_ref` is always
            // true. That would put every InitialConverged solve on the recovery / zero-step
            // path under the opt-in r0 rule: a production side effect of a fix whose commit
            // message claimed only to change what is REPORTED.
            const bool total_failure_vs_r0 =
                r0_measured && !gmres_converged_on_entry &&
                (gmres_raw_rel_error > r0_ref * (1.0f + 1.0e-4f) ||
                 gmres_raw_rel_error >= 0.99f * r0_ref);
            // With the flag on and r0 unmeasured the r0 rule cannot be evaluated. Falling back
            // to ||b|| is the only thing the solver can do, and it is COUNTED so the record
            // never claims a rule it did not apply.
            const bool gmres_total_failure_candidate =
                failure_vs_r0 ? (r0_measured ? total_failure_vs_r0 : total_failure_vs_b)
                              : total_failure_vs_b;
            stats_.total_failure_vs_b_count  += total_failure_vs_b  ? 1 : 0;
            stats_.total_failure_vs_r0_count += total_failure_vs_r0 ? 1 : 0;
            if (!r0_measured) {
                stats_.krylov_r0_unmeasured_solves++;
                if (failure_vs_r0) stats_.krylov_rule_fellback_to_b++;
            }
            stats_.krylov_failure_vs_r0 = failure_vs_r0;
            // ...and that a rule was in force at all. Without this the emitter prints
            // `krylov_failure_rule=b` for a stage whose Newton loop never reached the
            // predicate -- a default read as a measurement, which is the whole defect class.
            stats_.krylov_rule_observed = true;
            // R13.14 (round 5, P1): the r0 no-progress boundary is a JUDGMENT, not a
            // measurement -- the calibration argues for some constant in (0.55, 0.98) and does
            // not select 0.90 over 0.85. The tree's own default-budget run sits 2.3% from it,
            // and rho_vs_r0 is BUDGET-dependent (a healthy operator given 7 Arnoldi vectors on
            // a hard RHS reads 0.92), so the number that decides between the campaign's two
            // competing explanations must be movable without a rebuild.
            // R13.16 (round 6, R6-5): read from OBJECT STATE, set once at construction.
            stats_.krylov_no_progress_threshold = krylov_no_progress_threshold_;
            // Round 5, P2: and that the site was REACHED. Without this, `-1` on the record is
            // ambiguous between "unset, header default applied" and "never reached" -- the same
            // distinction `krylov_rule_observed` twenty lines up exists to make.
            stats_.krylov_threshold_observed = true;
            // R13.12 (red team R3-2): the first iteration whose SOLVE was a total failure,
            // which is what the field name says. `gmres_total_failure` below additionally
            // requires that no step was accepted, so indexing off it made this "the first
            // failure that also produced no step" -- a different event, and one that can
            // never precede a rejection, which silently disabled the time-order clause.
            if (gmres_total_failure_candidate && stats_.first_krylov_failure_iter < 0) {
                stats_.first_krylov_failure_iter = newton_iter;
            }
            // Unified trust-region step bound:
            //   K small  -> use radius floor (max(radius, min_radius))
            //   K large  -> honor relative cap (min(radius, max_rel * ||K||))
            // Keep this identical for both normal and fallback directions.
            auto compute_effective_trust_limit =
                [&](const torch::Tensor& K_norm, const torch::Tensor& step_ref) -> torch::Tensor {
                    const float max_relative_update =
                        wrf::sdirk3::g_sdirk3_config.trust_region_max_relative_update;
                    auto radius_tensor =
                        torch::full({}, trust_radius_, step_ref.options().requires_grad(false));
                    auto relative_limit = K_norm * max_relative_update;
                    auto min_radius_tensor =
                        torch::full({}, trust_radius_min_, step_ref.options().requires_grad(false));
                    bool K_is_small = guarded_item<bool>(relative_limit < radius_tensor * 0.5f);
                    if (K_is_small) {
                        return torch::max(radius_tensor, min_radius_tensor);
                    }
                    return torch::min(radius_tensor, relative_limit);
                };

            if (!wrf::sdirk3::g_sdirk3_config.nk_trust_region) {
                // Trust-region disabled: accept full Newton step directly.
                // GMRES total failure must NOT be treated as accepted zero-step because
                // that bypasses gmres_total_failure handling and creates zero-update loops.
                if (gmres_total_failure_candidate) {
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[TRUST OFF] GMRES failed (rel_error="
                                  << gmres_rel_error << ", raw=" << gmres_raw_rel_error
                                  << "), routing to GMRES total-failure handling."
                                  << std::endl;
                    }
                } else {
                    auto K_trial = K + dK;
                    auto U_trial = U_stage + dt * gamma * K_trial;
                    auto F_trial = compute_rhs(U_trial);
                    auto R_trial = K_trial - F_trial;
                    dK_scaled = dK;
                    accepted_residual = R_trial;
                    accepted_residual_norm = R_trial.norm();
                    step_accepted = true;
                }
            }

            // v20.14r27j: GMRES total failure path.
            // When e ≥ 0.999 (or raw > 1), GMRES dK is often unusable.
            // Before declaring a zero-step failure, try one cheap recovery update:
            //   dK_rec ~= -M^{-1}R (or -R when M unavailable), with the same trust clamp.
            // This avoids immediate zero-update exits in ru-dominant stiff stages.
            bool gmres_total_failure = false;
            if (!step_accepted &&
                gmres_total_failure_candidate) {
                gmres_total_failure = true;
                const auto& cfg = wrf::sdirk3::g_sdirk3_config;
                const float fallback_accept_ratio =
                    cfg.trust_fallback_relax
                        ? std::clamp(cfg.trust_fallback_ratio, 0.0f, 1.0f)
                        : 0.98f;
                if (stage == 2 && newton_iter == 0 &&
                    cfg.stage2_gmres_restart > 0 &&
                    cfg.stage2_max_krylov_restarts == 1) {
                    stage2_hopeless_detected = true;
                }
                if (stage >= 3 && newton_iter == 0 &&
                    last_ru_share_ > 0.98f &&
                    cfg.stage2_gmres_restart > 0 &&
                    cfg.stage2_max_krylov_restarts == 1 &&
                    !variable_pc_event_this_newton) {
                    stage3_hopeless_detected = true;
                } else if (stage >= 3 && newton_iter == 0 &&
                           cfg.debug_level >= 1 &&
                           cfg.stage2_gmres_restart > 0 &&
                           cfg.stage2_max_krylov_restarts == 1 &&
                           variable_pc_event_this_newton) {
                    std::cerr << "[GMRES HOPLESS MODE] Stage 3 detection skipped due to variable preconditioner event.\n";
                }

                bool recovered_with_fallback = false;
                float recovery_ratio = 1.0f;
                float recovery_ru_ratio = 1.0f;
                float recovery_step_norm = 0.0f;
                float base_res_norm = 0.0f;
                float base_ru_norm = -1.0f;
                torch::Tensor dK_recovery = torch::zeros_like(dK);

                {
                    torch::NoGradGuard no_grad;
                    auto R_det = R.detach();
                    base_res_norm = R_det.norm().to(torch::kCPU).item<float>();

                    if (M_inv) {
                        dK_recovery = -M_inv(R_det);
                    } else if (preconditioner_) {
                        dK_recovery = -preconditioner_->apply(R_det);
                    } else {
                        dK_recovery = -R_det;
                    }

                    bool recovery_bad = guarded_item<bool>(torch::any(torch::isnan(dK_recovery))) ||
                                        guarded_item<bool>(torch::any(torch::isinf(dK_recovery)));

                    const bool ru_block_ready =
                        layout_initialized_ &&
                        !cached_layout_.blocks.empty() &&
                        cached_layout_.blocks[0].name == "ru";
                    int64_t ru_start = 0;
                    int64_t ru_size = 0;
                    if (ru_block_ready) {
                        const auto& ru_blk = cached_layout_.blocks[0];
                        if (ru_blk.start + ru_blk.size <= R_det.numel() &&
                            ru_blk.start + ru_blk.size <= dK_recovery.numel()) {
                            ru_start = ru_blk.start;
                            ru_size = ru_blk.size;
                            auto R_u = R_det.slice(0, ru_start, ru_start + ru_size);
                            base_ru_norm = R_u.norm().to(torch::kCPU).item<float>();
                            if (last_ru_share_ > 0.95f) {
                                dK_recovery.slice(0, ru_start, ru_start + ru_size).copy_(-R_u);
                            }
                        }
                    }

                    if (!recovery_bad) {
                        apply_halo_zeroing(dK_recovery);

                        auto dK_norm = dK_recovery.norm();
                        auto K_norm = K.norm();
                        auto effective_limit = compute_effective_trust_limit(K_norm, dK_recovery);
                        if (guarded_item<bool>(dK_norm > effective_limit)) {
                            dK_recovery = dK_recovery * (effective_limit / dK_norm);
                        }

                        recovery_step_norm = safe_tensor_norm(dK_recovery).to(torch::kCPU).item<float>();
                        if (recovery_step_norm > 1e-20f && std::isfinite(recovery_step_norm)) {
                            auto K_trial = K + dK_recovery;
                            auto U_trial = U_stage + dt * gamma * K_trial;
                            auto F_trial = compute_rhs(U_trial);
                            auto R_trial = K_trial - F_trial;
                            auto trial_norm_tensor = R_trial.norm();
                            float trial_res_norm = trial_norm_tensor.to(torch::kCPU).item<float>();
                            recovery_ratio = trial_res_norm / std::max(base_res_norm, 1e-30f);

                            bool ru_improved = false;
                            if (base_ru_norm > 0.0f && ru_size > 0 &&
                                ru_start + ru_size <= R_trial.numel()) {
                                auto R_trial_u = R_trial.slice(0, ru_start, ru_start + ru_size);
                                float trial_ru_norm = R_trial_u.norm().to(torch::kCPU).item<float>();
                                recovery_ru_ratio = trial_ru_norm / std::max(base_ru_norm, 1e-30f);
                                ru_improved = std::isfinite(recovery_ru_ratio) &&
                                              (recovery_ru_ratio <= fallback_accept_ratio);
                            }

                            const bool residual_improved = std::isfinite(recovery_ratio) &&
                                                           (recovery_ratio <= fallback_accept_ratio);
                            if (residual_improved || (last_ru_share_ > 0.95f && ru_improved)) {
                                dK_scaled = dK_recovery;
                                accepted_residual = R_trial;
                                accepted_residual_norm = trial_norm_tensor;
                                step_accepted = true;
                                recovered_with_fallback = true;
                                last_rho = 0.0f;
                            }
                        }
                    }
                }

                if (recovered_with_fallback) {
                    trust_radius_ = std::max(trust_radius_ * 0.8f, trust_radius_min_);
                    if (cfg.debug_level >= 1) {
                        std::cerr << "[TRUST REGION] GMRES total failure recovered by fallback step: "
                                  << "||dK_rec||=" << recovery_step_norm
                                  << ", R_new/R_old=" << recovery_ratio;
                        if (base_ru_norm > 0.0f) {
                            std::cerr << ", ru_new/ru_old=" << recovery_ru_ratio;
                        }
                        std::cerr << ", ratio_gate=" << fallback_accept_ratio
                                  << ", radius=" << trust_radius_ << std::endl;
                    }
                } else {
                    dK_scaled = torch::zeros_like(dK);
                    accepted_residual = R.detach();
                    accepted_residual_norm = R.detach().norm();
                    last_rho = 0.0f;
                    // v20.14r40: Gradual shrink instead of slam to minimum.
                    // Preserves recovery potential for next Newton iteration.
                    trust_radius_ = std::max(trust_radius_ * 0.5f, trust_radius_min_);
                    if (cfg.debug_level >= 1) {
                        std::cerr << "[TRUST REGION] GMRES failed (rel_error="
                                  << gmres_rel_error << ", raw=" << gmres_raw_rel_error
                                  << "), fallback rejected. K unchanged. radius=" << trust_radius_
                                  << std::endl;
                    }
                }
            }

            float prev_candidate_norm_val = -1.0f;  // v20.14r27l: track for short-circuit
            bool forced_scaled_tried = false;  // v20.14r27m: one forced-scale attempt on same-candidate
            for (int attempt = 0; !step_accepted && !gmres_total_failure && attempt < max_trust_attempts && rhs_budget > 0; ++attempt) {
                auto dK_norm = dK.norm();
                auto K_norm = K.norm();

                // Shared trust-region limiter (same rule as fallback path above).
                auto effective_limit = compute_effective_trust_limit(K_norm, dK);

                auto dK_scaled_candidate = dK;
                // FIX (2025-12-05): Use guarded_item for autograd compatibility
                bool needs_scaling = guarded_item<bool>(dK_norm > effective_limit);
                if (needs_scaling) {
                    auto scale_factor = effective_limit / dK_norm;
                    dK_scaled_candidate = dK * scale_factor;
                }

                auto dK_scaled_norm_tensor = dK_scaled_candidate.norm();

                // v20.14r27n: Same-candidate detection with forced-scale fallback.
                // When dK fits within all radii, shrinking the radius doesn't change
                // the candidate. Instead of skipping, try a forced α=0.5 step once.
                // After forced step, keep the ORIGINAL norm as prev_candidate_norm_val
                // so subsequent attempts with the same original candidate are caught.
                {
                    float curr_cand_norm = guarded_item<float>(dK_scaled_norm_tensor);
                    bool just_forced = false;
                    if (prev_candidate_norm_val >= 0.0f &&
                        std::abs(curr_cand_norm - prev_candidate_norm_val) <
                            1e-6f * (prev_candidate_norm_val + 1e-30f)) {
                        if (!forced_scaled_tried) {
                            // v20.14r27t: Force α=0.5 step, but respect effective_limit.
                            // Without clamping, forced step could exceed trust radius
                            // in small-radius situations, violating the trust contract.
                            just_forced = true;
                            forced_scaled_tried = true;
                            float eff_lim_f = guarded_item<float>(effective_limit);
                            float dk_norm_f = guarded_item<float>(dK_norm);
                            float max_alpha = (dk_norm_f > 1e-14f) ? (eff_lim_f / dk_norm_f) : 1.0f;
                            float forced_alpha = std::min(0.5f, max_alpha);
                            dK_scaled_candidate = dK * forced_alpha;
                            dK_scaled_norm_tensor = dK_scaled_candidate.norm();
                            curr_cand_norm = guarded_item<float>(dK_scaled_norm_tensor);
                            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                                std::cerr << "[TRUST REGION] Same candidate on attempt " << attempt
                                          << ", forcing α=" << forced_alpha
                                          << " step (||dK_s||=" << curr_cand_norm
                                          << ", eff_lim=" << eff_lim_f << ")" << std::endl;
                            }
                        } else {
                            // v20.14r40: Already tried forced scale — break entirely.
                            // Further attempts just shrink radius without changing candidate.
                            trust_radius_ = std::max(trust_radius_ * 0.25f, trust_radius_min_);
                            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                                std::cerr << "[TRUST REGION] Break attempt " << attempt
                                          << " (same candidate, forced already tried)" << std::endl;
                            }
                            break;
                        }
                    }
                    // v20.14r27n: Don't update prev_candidate_norm_val after forced step.
                    // Keep original norm so the same original candidate is caught on next attempt.
                    if (!just_forced) {
                        prev_candidate_norm_val = curr_cand_norm;
                    }
                }

                auto K_trial = K + dK_scaled_candidate;
                auto U_trial = U_stage + dt * gamma * K_trial;

                auto F_trial = compute_rhs(U_trial);
                --rhs_budget;  // Phase 3C: TR RHS budget
                auto R_trial = K_trial - F_trial;
                auto res_trial_tensor = R_trial.norm();

                // CRITICAL FIX (2025-11-28): Use GMRES-based predicted reduction instead of -<R, dK>
                // Old formula: predicted = -<R, dK> (WRONG: doesn't account for GMRES quality)
                // New formula: predicted = ||R||² * (1 - e²) where e = gmres_rel_error
                // This correctly reflects the linear model: if GMRES achieves relative error e,
                // then ||R + J*dK|| = e*||R||, so ||R||² - ||R+J*dK||² = ||R||² * (1 - e²)

                float res_old_val = 0.0f;
                float res_new_val = 0.0f;
                [[maybe_unused]] float predicted_val = 0.0f;
                float dK_norm_val = 0.0f;
                float dK_scaled_norm_val = 0.0f;
                [[maybe_unused]] float effective_limit_val = 0.0f;

                // FIX (2025-12-05): Use guarded_item for autograd compatibility
                // These scalar extractions are for trust region control flow.
                // FIX 2026-02-01: When scaling is active, GMRES rel_error is in scaled
                // norm ||S⁻¹·r||, so trust-region actual/predicted must use the same norm
                // to keep rho = actual/predicted consistent.
                // Apply halo mask to both R and R_trial for consistency with GMRES.
                if (scaling_initialized_) {
                    auto R_scaled = S_inv_diag_ * R;
                    auto R_trial_scaled = S_inv_diag_ * R_trial;
                    if (halo_mask_initialized_) {
                        // PR 9F.9.5: force halo cells to 0, matching the shadow merit
                        // (sdirk3_scaled_merit_sq). The old `x.mul_(mask)` propagates a
                        // non-finite halo cell (NaN*0==NaN), so production res_old/new could
                        // go NaN in exactly the case the shadow is defined to tolerate --
                        // diverging the two metrics. masked_fill_ REPLACES halo cells with 0
                        // instead. For a FINITE halo (the operating point) this is
                        // byte-identical: norm() squares each cell, so a halo (-0.0) from mul_
                        // vs (+0.0) here gives the same norm. In-place with no extra
                        // allocation; R_scaled is a fresh product consumed only by the
                        // detached norm below, so (like the original mul_) it is graph-safe.
                        const auto mb_inv = halo_mask_.to(torch::kBool).logical_not();
                        R_scaled.masked_fill_(mb_inv, 0);
                        R_trial_scaled.masked_fill_(mb_inv, 0);
                    }
                    res_old_val = guarded_item<float>(R_scaled.norm());
                    res_new_val = guarded_item<float>(R_trial_scaled.norm());
                } else {
                    // PR 9F.9.2: BOTH raw L2, matching the scaled branch above (||.||_2).
                    // The old code used res_norm_tensor (RMS = ||R||/sqrt(N)) for old but
                    // the raw L2 of R_trial for new, so actual_reduction = RMS(R)^2 -
                    // L2(R_trial)^2 was dimensionally inconsistent and grid-size dependent.
                    // Reached ONLY when scaling never initialized (no layout / zero grid
                    // dims via the public options default), so dt=600 is unaffected.
                    res_old_val = guarded_item<float>(R.norm());
                    res_new_val = guarded_item<float>(res_trial_tensor);
                }
                dK_norm_val = guarded_item<float>(dK_norm);
                dK_scaled_norm_val = guarded_item<float>(dK_scaled_norm_tensor);
                effective_limit_val = guarded_item<float>(effective_limit);

                // FIX 2026-01-29: Correct quadratic model for scaled steps.
                // For full step: R_new ≈ r_gmres, so ||R||²_predicted = e²||R_old||²
                //   predicted_full = ||R_old||² - e²||R_old||² = ||R_old||²(1-e²)
                // For scaled step αδK: R_new ≈ (1-α)R_old + α*r_gmres
                //   ||R_new||² ≈ (1-α)²||R_old||² + α²*e²*||R_old||²  (cross-term ≈ 0)
                //   predicted_scaled = ||R_old||² - [(1-α)² + α²*e²]*||R_old||²
                //                    = ||R_old||² * [1 - (1-α)² - α²*e²]
                //                    = ||R_old||² * α * [2 - α - α*e²]
                // Previous code used linear scaling (predicted_full * α) which is incorrect.
                float e = gmres_rel_error;  // Already clamped to [0,1]
                // v20.14r27h: Renamed from alpha to tr_alpha to avoid shadowing
                // the outer line-search alpha (line 3477).
                float tr_alpha = (dK_norm_val > 1e-14f) ? (dK_scaled_norm_val / dK_norm_val) : 1.0f;
                tr_alpha = std::min(tr_alpha, 1.0f);  // α ∈ [0,1]
                predicted_val = res_old_val * res_old_val * tr_alpha * (2.0f - tr_alpha - tr_alpha * e * e);

                // v20.14r27l: Cap predicted at full-step value (1-e²)*||R||².
                // The quadratic model α*(2-α-αe²) has a maximum at α=1/(1+e²)≈0.5
                // for e≈1, so reducing α can INCREASE predicted — causing rho to
                // drop artificially and reject valid steps. Cap ensures predicted
                // never exceeds what the full step would predict.
                float predicted_full_step = res_old_val * res_old_val * std::max(1.0f - e * e, 0.0f);
                if (predicted_val > predicted_full_step) {
                    predicted_val = predicted_full_step;
                }

                // Guard against degenerate cases
                if (!std::isfinite(predicted_val) || predicted_val < 0.0f) {
                    predicted_val = 0.0f;
                }

                // CRITICAL FIX (2025-11-28): Use squared norm reduction for consistency with predicted
                // Both actual and predicted now measure reduction in ||R||²
                float actual_reduction = res_old_val * res_old_val - res_new_val * res_new_val;

                // STABILITY FIX (2025-11-29): Clamp predicted to avoid rho explosion
                // When GMRES rel_error ≈ 1, predicted ≈ 0 → rho = ±inf
                float predicted_clamped = std::max(predicted_val, 1e-6f * res_old_val * res_old_val);
                float rho_val = actual_reduction / predicted_clamped;

                // PR 9F.9 P1-4 SHADOW (diagnosis-only; env flag WRF_SDIRK3_NUMERICAL_SHADOW).
                // The production predicted reduction ||R||^2*[1-(1-a)^2-a^2*e^2] approximates
                // the fractional-step model in TWO ways: (1) it DROPS the cross term
                // 2a(1-a)<R_s,r_g>, valid only if R_s _|_ r_g -- not guaranteed for the
                // non-normal WRF operator; (2) it uses the SCALAR gmres_rel_error e in place
                // of the true residual VECTOR r_g. (Note: e is NOT unscaled -- the outer
                // Newton hands GMRES the S-scaled system, so e is the S-scaled relative
                // residual; the earlier "unscaled e" note was wrong.) The EXACT model needs
                // no extra JVP: GMRES already returns r_g = b_s - A_s*x (already S-scaled),
                // so R_lin_s(a) = (1-a)R_s - a*r_g and the predicted reduction is
                // ||mask R_s||^2 - ||mask R_lin_s||^2 in the SAME norm as res_old_val. Emit
                // both so the divergence can be MEASURED. Read-only; the acceptance still
                // uses rho_val, so the numerical path is byte-identical.
                if (numerical_shadow_enabled() &&
                    scaling_initialized_ && S_inv_diag_.defined() &&
                    S_inv_diag_.numel() == R.numel() &&
                    last_gmres_r_true_.defined() &&
                    last_gmres_r_true_.numel() == R.numel()) {
                    torch::NoGradGuard no_grad;
                    // FIX (PR 9F.9.1): compute ENTIRELY in the scaled space. r_g
                    // (last_gmres_r_true_) is ALREADY S-scaled (b_s=-S^-1 R, x=S^-1 dK),
                    // so scale R once (R_s = S^-1 R) and do NOT re-scale r_g. The trust
                    // region's res_old/new are ||S^-1 R . mask||_2 (L2 of scaled masked),
                    // so match that norm and reuse res_old_val for perfect consistency.
                    const auto R_s = S_inv_diag_ * R;
                    const auto mask = halo_mask_initialized_ ? halo_mask_
                                                             : torch::Tensor();
                    const auto pred = sdirk3_trust_predicted_reduction(
                        R_s, last_gmres_r_true_, static_cast<double>(tr_alpha), mask);
                    if (!pred.ok()) {
                        // PR 9F.9.3: a contract violation is NOT laundered into a finite
                        // rho. Emit an explicit INVALID marker naming the reason and skip
                        // this trust record. (The parser treats it as a shadow failure,
                        // not evidence.)
                        char inv[128];
                        std::snprintf(inv, sizeof inv,
                            "SDIRK3_TRUST_SHADOW_INVALID stage=%d iter=%d reason=%s\n",
                            stage, newton_iter, trust_prediction_error_name(pred.error()));
                        emit_numerical_shadow_line(inv);
                    } else {
                        // PR 9F.9.6: the helper returns the LINEAR-MODEL reduction AND its two
                        // merits merit_old=m(R_s), merit_model=m((1-a)R_s - a r_g). The ACTUAL
                        // reduction is merit_old MINUS the NONLINEAR trial merit m(R_trial_s);
                        // reuse merit_old from the helper (no recompute). The degeneracy
                        // threshold is scaled by the LINEAR-model merits (assess_trust_model),
                        // NOT the nonlinear trial -- a blown-up trial belongs in `actual`, and
                        // must not inflate the linear model's cancellation floor.
                        const double pred_exact  = pred.reduction();
                        const double merit_old   = pred.merit_old();
                        const double merit_model = pred.merit_model();
                        const auto R_trial_s = S_inv_diag_ * R_trial;
                        const double merit_trial =
                            wrf::sdirk3::detail::scaled_merit_sq_unchecked(R_trial_s, mask);
                        const double actual_exact = merit_old - merit_trial;
                        const auto assess = assess_trust_model(
                            pred_exact, merit_old, merit_model, actual_exact);

                        // PR 9F.9.6 (P1): emit the ACTUAL production ratio, not a recomputed
                        // one. Production accept/reject uses rho_val = actual_reduction /
                        // predicted_clamped with predicted_clamped = max(predicted_val,
                        // 1e-6*||R||^2). The old rho_heur re-divided by predicted_val (no
                        // clamp), so it could differ from the real decision by many orders
                        // when predicted_val is tiny. Emit the production numerator and
                        // denominator too so the decision is reproducible from the record.
                        char tb[512];  // 15 fields incl. %.6e / nan / inf -- generous margin
                        std::snprintf(tb, sizeof tb,
                            "SDIRK3_TRUST_SHADOW stage=%d iter=%d alpha=%.4f e=%.4e "
                            "actual_prod=%.6e pred_prod=%.6e pred_clamped=%.6e rho_prod=%.4f "
                            "actual_exact=%.6e pred_exact=%.6e merit_old=%.6e "
                            "merit_model=%.6e rho_exact=%.4f tol=%.3e status=%s\n",
                            stage, newton_iter, tr_alpha, e,
                            static_cast<double>(actual_reduction),
                            static_cast<double>(predicted_val),
                            static_cast<double>(predicted_clamped),
                            static_cast<double>(rho_val),
                            assess.actual, pred_exact, merit_old, merit_model,
                            assess.rho, assess.prediction_tolerance,
                            trust_assessment_status_name(assess.status));
                        emit_numerical_shadow_line(tb);
                    }
                }

                // force_accept removed (2026-02-16 GR v8 F6: computed but unused since 2026-01-31)
                float rho_accept_threshold = 0.25f;
                float rho_force_accept_min = 0.05f;
                if (stage_budget_active_this_iter &&
                    ew_eta_enabled_this_iter &&
                    std::isfinite(stage_budget_forcing_eta) &&
                    stage_budget_forcing_eta > 0.0f) {
                    const float eta_for_accept =
                        std::clamp(stage_budget_forcing_eta, 1.0e-3f, 1.0f);
                    // Tighter forcing should require slightly better trust agreement.
                    const float strictness =
                        std::clamp((0.30f - eta_for_accept) / 0.25f, 0.0f, 1.0f);
                    rho_accept_threshold += 0.10f * strictness;
                    rho_force_accept_min += 0.03f * strictness;
                }

                // FIX 2026-01-29: Enable trust-region diagnostics for debugging convergence issues
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[TRUST REGION] attempt " << attempt
                              << ": radius=" << trust_radius_
                              << ", gmres_rel_error=" << gmres_rel_error
                              << " (raw=" << gmres_raw_rel_error << ")"
                              << ", ||dK||=" << dK_norm_val
                              << ", ||dK_scaled||=" << dK_scaled_norm_val
                              << ", actual=" << actual_reduction
                              << ", predicted=" << predicted_clamped
                              << ", rho=" << rho_val
                              << ", rho_accept_threshold=" << rho_accept_threshold
                              << ", rho_force_accept_min=" << rho_force_accept_min
                              << std::endl;
                }

                bool accept_step = false;

                // FIX (2025-11-30): Early rejection for very poor GMRES quality
                // When rel_error > threshold, the linear solve quality is poor.
                // v20.9d: Made configurable. Default threshold=1.0 (disabled) so that
                // the trust-region/line-search mechanism can evaluate step quality
                // naturally via actual_reduction and rho.  Previously hard-coded 0.9
                // caused ALL trust-region attempts to reject when GMRES true_err > 0.9,
                // forcing dK_scaled=0 (zero Newton update) at line 3347.
                {
                    float quality_thresh = wrf::sdirk3::g_sdirk3_config.newton_gmres_quality_threshold;
                    if (quality_thresh < 1.0f && gmres_rel_error > quality_thresh) {
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[TRUST REGION] GMRES rel_error=" << gmres_rel_error
                                      << " > " << quality_thresh
                                      << ", rejecting step (poor linear solve quality)" << std::endl;
                        }
                        trust_radius_ = std::max(trust_radius_ * 0.25f, trust_radius_min_);
                        continue;  // Try again with smaller radius
                    }
                }

                // FIX 2026-01-31: NEVER accept steps that increase residual.
                // Previously force_accept=true on final attempt accepted bad steps,
                // causing Newton divergence at timestep 2.
                // Trust-region should REJECT and shrink, not force-accept.
                if (!std::isfinite(rho_val)) {
                    accept_step = false;  // Non-finite rho: always reject
                } else if (gmres_total_failure_candidate) {
                    // v20.14r27h: GMRES diverged (raw > 1) or essentially failed (e ≥ 0.999).
                    // When e ≈ 1, the correction dK carries no useful signal — the predicted
                    // model is unreliable and tiny α steps would be accepted spuriously.
                    accept_step = false;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[TRUST REGION] GMRES failed (rel_error="
                                  << gmres_rel_error << ", raw=" << gmres_raw_rel_error
                                  << "), rejecting" << std::endl;
                    }
                } else if (gmres_rel_error > 0.99f) {
                    // v20.14r35: Near-fail GMRES (0.99 < e < 0.999). The step carries
                    // almost no signal. Require minimum actual residual decrease.
                    // v20.14r40: Scale floor by tr_alpha — trust-region reduced steps
                    // naturally produce proportionally smaller decrease.
                    const float near_fail_floor = wrf::sdirk3::g_sdirk3_config.near_fail_floor;
                    const float scaled_floor = near_fail_floor * std::max(tr_alpha, 0.1f);
                    float actual_decrease_frac = (res_old_val > 1e-12f) ?
                        (res_old_val - res_new_val) / res_old_val : 0.0f;
                    if (actual_decrease_frac < scaled_floor) {
                        accept_step = false;
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[TRUST REGION] Near-fail GMRES (e=" << gmres_rel_error
                                      << "), decrease " << actual_decrease_frac * 100.0f
                                      << "% < " << scaled_floor * 100.0f
                                      << "% floor (base=" << near_fail_floor * 100.0f
                                      << "%, tr_alpha=" << tr_alpha
                                      << "), rejecting" << std::endl;
                        }
                    } else if (rho_val >= 0.1f) {
                        // v20.14r27u: Decrease sufficient + model quality OK → accept.
                        accept_step = true;
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[TRUST REGION] Near-fail GMRES (e=" << gmres_rel_error
                                      << "), decrease " << actual_decrease_frac * 100.0f
                                      << "% >= " << scaled_floor * 100.0f
                                      << "% floor, rho=" << rho_val
                                      << " >= 0.1, accepting" << std::endl;
                        }
                    }
                    // else: decrease OK but rho < 0.1 → reject (accept_step stays false)
                } else if (actual_reduction <= 0.0f) {
                    accept_step = false;  // Residual increased: always reject
                } else {
                    // v20.14r27q: Positive actual reduction — evaluate quality.
                    // Step 1: Rho-based acceptance first (model quality).
                    if (rho_val >= rho_accept_threshold) {
                        accept_step = true;
                    } else {
                        // v20.14r27r: Hard-reject using dimensionless tr_alpha
                        // (step fraction, not absolute norm). Required decrease scales
                        // with how much of the GMRES direction we actually use.
                        // tr_alpha already computed above (line ~3737).
                        bool hard_rejected = false;
                        if (res_old_val > 1e-12f) {
                            float required_decrease = 0.01f * tr_alpha;  // 1% at full step
                            float actual_decrease_frac = (res_old_val - res_new_val) / res_old_val;
                            if (actual_decrease_frac < required_decrease) {
                                hard_rejected = true;
                                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                                    std::cerr << "[TRUST REGION] Rejected (decrease "
                                              << actual_decrease_frac * 100.0f
                                              << "% < required " << required_decrease * 100.0f
                                              << "%, tr_alpha=" << tr_alpha
                                              << ", rho=" << rho_val << ")" << std::endl;
                                }
                            }
                        }
                        if (!hard_rejected) {
                            accept_step = (attempt == max_trust_attempts - 1) && (rho_val >= rho_force_accept_min);
                        }
                    }
                }

                // v20.14 r49: Block-aware check (scaled+halo-masked norm, same as rho)
                if (accept_step) {
                    float ba_thresh = wrf::sdirk3::g_sdirk3_config.trust_region_block_aware_thresh;
                    if (ba_thresh > 0.0f && last_ru_share_ > ba_thresh &&
                        layout_initialized_ && cached_layout_.total_size == R_trial.numel()) {
                        torch::NoGradGuard no_grad;
                        for (const auto& blk : cached_layout_.blocks) {
                            if (blk.name == "ru" && blk.start + blk.size <= R_trial.numel()) {
                                auto ru_old = R.detach().slice(0, blk.start, blk.start + blk.size);
                                auto ru_new = R_trial.detach().slice(0, blk.start, blk.start + blk.size);
                                if (scaling_initialized_) {
                                    auto s_inv = S_inv_diag_.slice(0, blk.start, blk.start + blk.size);
                                    ru_old = ru_old * s_inv;
                                    ru_new = ru_new * s_inv;
                                }
                                if (halo_mask_initialized_) {
                                    auto hm = halo_mask_.slice(0, blk.start, blk.start + blk.size);
                                    ru_old = ru_old * hm;
                                    ru_new = ru_new * hm;
                                }
                                float ru_old_n = ru_old.to(torch::kCPU).norm().item<float>();
                                float ru_new_n = ru_new.to(torch::kCPU).norm().item<float>();
                                if (ru_new_n > ru_old_n * 0.999f) {
                                    accept_step = false;
                                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                                        std::cerr << "[TRUST REGION] Block-aware REJECT: ru_old=" << ru_old_n
                                                  << " ru_new=" << ru_new_n
                                                  << " share=" << last_ru_share_ << "\n";
                                    }
                                }
                                break;
                            }
                        }
                    }
                }

                if (accept_step) {
                    step_accepted = true;
                    dK_scaled = dK_scaled_candidate;
                    accepted_residual = R_trial;
                    accepted_residual_norm = res_trial_tensor;
                    last_rho = rho_val;

                    if (rho_val > 0.75f && dK_scaled_norm_val > 0.8f * trust_radius_) {
                        trust_radius_ = std::min(trust_radius_ * 2.0f, trust_radius_max_);
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                            std::cerr << "[TRUST REGION] Expanding radius to " << trust_radius_ << std::endl;
                        }
                    } else if (rho_val < 0.5f) {
                        trust_radius_ = std::max(trust_radius_ * 0.5f, trust_radius_min_);
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                            std::cerr << "[TRUST REGION] Shrinking radius to " << trust_radius_ << std::endl;
                        }
                    } else {
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
                            std::cerr << "[TRUST REGION] Keeping radius at " << trust_radius_ << std::endl;
                        }
                    }
                    break;
                }

                trust_radius_ = std::max(trust_radius_ * 0.25f, trust_radius_min_);
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[TRUST REGION] Rejecting step, new radius=" << trust_radius_ << std::endl;
                }
            }

            // FIX 2026-01-31: When all trust-region attempts fail, do NOT force-accept.
            // Keep K unchanged (zero update) so Newton can try again next iteration
            // with the same residual. This prevents divergence from accepting bad steps.
            // M6: DID THE STEP HAPPEN? "The stage gate exceeded 1, therefore a large Krylov
            // update grew the residual" does not follow. When every trust attempt is rejected
            // this branch sets dK_scaled = 0 and K is unchanged, so the stage keeps its entering
            // residual -- a gate above 1 then reports a step that was never taken. Opposite
            // diagnoses, and the gate value alone cannot separate them.
            //
            // BUT step_accepted DOES NOT MEAN TRUST ACCEPTED. With nk_trust_region = false the
            // solver takes the full Newton step unconditionally and sets the same flag, having
            // evaluated nothing -- and em_b_wave runs with it FALSE at runtime despite the struct
            // default being true. Reading "accepted" as a trust verdict there is exactly wrong:
            // trust neither accepted nor rejected, it never ran. rho = 0 in every record is the
            // tell, and the trust state is printed alongside so the flag cannot be read alone.
            if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_STEP_LEDGER")) {
                torch::NoGradGuard ng_ledger;
                std::cerr << "SDIRK3_STEP_LEDGER stage=" << stage
                          << " newton_iter=" << newton_iter
                          << " step_accepted=" << (step_accepted ? 1 : 0)
                          << " R_norm=" << R.detach().norm().to(torch::kCPU).item<double>()
                          << " dK_norm=" << dK.detach().norm().to(torch::kCPU).item<double>()
                          << " rho=" << last_rho
                          << " trust_enabled="
                          << (wrf::sdirk3::g_sdirk3_config.nk_trust_region ? 1 : 0)
                          << std::endl << std::flush;
            }

            if (!step_accepted) {
                dK_scaled = torch::zeros_like(dK);
                accepted_residual = R.detach();  // Keep current residual
                accepted_residual_norm = R.detach().norm();
                last_rho = 0.0f;
                // v20.14r27p: Differentiate reject causes in message.
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[TRUST REGION] All attempts rejected. "
                              << "Keeping K unchanged, radius=" << trust_radius_ << std::endl;
                }
            }

            // R13.2: the accounting the first-failure classifier needs. "Every step was
            // rejected" is a trust-region policy statement, and it is indistinguishable from
            // a stalled solve unless the two are counted separately.
            if (step_accepted) {
                stats_.accepted_steps++;
                // Referee C8: the TAYLOR DEFECT of the stage function at the step actually
                // taken. tau = ||G(K+s) - G(K) - A s|| / ||A s||, with A s one production
                // matvec.
                //
                // R13.20 (numerics referee, claim 1B) -- WHAT tau CAN AND CANNOT SEE. `As` is the
                // AD JVP OF `compute_rhs`, and `dR` is a finite difference OF THE SAME
                // `compute_rhs` (R = K - compute_rhs(U); the alpha arm re-evaluates
                // compute_rhs(U_alpha)). So tau measures whether the AD tangent is consistent
                // with the primal it differentiates. It CANNOT see a defect in `compute_rhs`
                // itself: if the implemented RHS is physically wrong, AD differentiates the wrong
                // function faithfully and tau -> 0. That is not hypothetical here -- the
                // campaign's own standing root-cause note (Omega := rom = mu*w where WRF's Omega
                // is mu*d(eta)/dt from calc_ww_cp) is exactly such a defect, and it would leave
                // every tau row untouched. "Jacobian defect" below means AD-vs-primal, nothing
                // wider.
                //
                // (1D) and it is gated on `step_accepted` -- see the coverage fields on the
                // emitted row and on SDIRK3_FIRST_FAILURE. tau << 1: the linear model is faithful here and the INNER solve is
                // the binding constraint. tau = O(1): the model is wrong at this step -- and
                // the half-step arm separates the two ways it can be wrong: nonlinearity over
                // the step (tau falls ~2x at s/2, the relative defect being ~linear in ||s||)
                // from a Jacobian defect (tau does not fall). Two numbers (first, last
                // residual) could not separate three mechanisms; this can. Opt-in: one JVP
                // and one RHS evaluation per accepted iteration.
                if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_TAYLOR_DEFECT") &&
                    R.defined() && accepted_residual.defined() && dK_scaled.defined()) {
                    torch::NoGradGuard ng_tau;
                    const long long fd_fallbacks_before_tau =
                        wrf::sdirk3::g_jvp_fd_fallback_count.load(std::memory_order_relaxed);
                    const auto As   = apply_jacobian(dK_scaled.detach()).detach();
                    const auto dR   = (accepted_residual.detach() - R.detach());
                    const double nAs = As.to(torch::kFloat64).norm().item<double>();
                    const double tau = nAs > 0.0
                        ? (dR - As).to(torch::kFloat64).norm().item<double>() / nAs : -1.0;
                    // The scaled arm. R13.13 (round 4) measured A(alpha*s) with its own matvec
                    // instead of scaling A(s).
                    //
                    // R13.14 (red team round 5, P0): with alpha = 1/2 that was a NO-OP, and the
                    // receipt it produced was a TAUTOLOGY. A forward-AD tangent is a float
                    // expression whose every term is (primal) x (tangent); scaling the input
                    // tangent by a POWER OF TWO scales every intermediate by the same power of
                    // two with identical significands, so under IEEE-754 A(s/2) == 0.5*A(s) BIT
                    // FOR BIT, for any operator. `linearity_residual = 0` therefore measured the
                    // exponent arithmetic, not the Jacobian -- and "tau came out unchanged when
                    // we started measuring the arm" was the signature of a substitution that
                    // could not have changed anything, not corroboration. The FD path was blind
                    // for the same reason: halving ||v|| exactly doubles its epsilon exactly, so
                    // the perturbed vector is IDENTICAL and the quotient scales by exactly 1/2.
                    //
                    // alpha = 1/3 is not dyadic, so neither cancellation happens: the receipt is
                    // a real measurement on both paths. The A/B probe in this file already chose
                    // 2.5 for its homogeneity check for exactly this reason. For a quadratic
                    // (bilinear-RHS) remainder the prediction is tau(alpha)/tau = alpha, so the
                    // discriminator reads 0.333 rather than 0.5 -- a value that cannot be
                    // produced by halving anything.
                    constexpr double kTauAlpha = 1.0 / 3.0;
                    double tau_alpha = -1.0, linearity_residual = -1.0;
                    bool alpha_arm_measured = false;
                    {
                        const auto s_alpha = (kTauAlpha * dK_scaled).detach();
                        const auto K_alpha = K.detach() + s_alpha;
                        const auto U_alpha = U_stage + dt * gamma * K_alpha;
                        const auto R_alpha = (K_alpha - compute_rhs(U_alpha)).detach();
                        const auto dR_alpha = R_alpha - R.detach();
                        const auto As_alpha = apply_jacobian(s_alpha).detach();
                        const double nAs_alpha = As_alpha.defined()
                            ? As_alpha.to(torch::kFloat64).norm().item<double>() : -1.0;
                        // R13.14 (round 5, R5-6): this was hardcoded true, so AlphaArmAssumed
                        // was unreachable from the only production caller -- a constant dressed
                        // as a precondition, the same shape removed one screen away in the same
                        // commit that introduced it. It is now the condition it claims: the arm
                        // was measured iff its matvec produced a usable tensor.
                        alpha_arm_measured =
                            As_alpha.defined() && std::isfinite(nAs_alpha) && nAs_alpha > 0.0;
                        tau_alpha = nAs_alpha > 0.0
                            ? (dR_alpha - As_alpha).to(torch::kFloat64).norm().item<double>()
                                  / nAs_alpha : -1.0;
                        // ||A(alpha*s) - alpha*A(s)|| / ||alpha*A(s)||, with a non-dyadic alpha
                        // so that neither the fwAD exponent cancellation nor the FD epsilon
                        // cancellation can force it to zero.
                        const auto As_scaled = kTauAlpha * As;
                        const double nAs_scaled =
                            As_scaled.to(torch::kFloat64).norm().item<double>();
                        linearity_residual = nAs_scaled > 0.0
                            ? (As_alpha - As_scaled).to(torch::kFloat64).norm().item<double>()
                                  / nAs_scaled : -1.0;
                    }
                    // The verdict, from the rule in wrf_sdirk3_probe_validity.h -- where a
                    // fixture can reject its negation, which a rule spelled out here cannot.
                    // R13.17 (external review P1-1): PER-BLOCK tau. The packed L2 above lets a
                    // dominant block hide a large relative defect in a small one, and rw/ph/mu --
                    // the blocks this campaign is about -- are the small ones. Each block is
                    // normalised by its OWN ||A s||, with a floor so a block the step barely
                    // touches cannot manufacture a huge ratio from noise.
                    double tau_block_max = -1.0;
                    // R13.20 (claim 7.1): the counterfactual, on every row. `tau_block_max` is
                    // gated by the excitation floor; this is the largest RAW block ratio with no
                    // gate at all. On the dt=600 record they disagree -- 0.2008 (`ru`, excited)
                    // vs 0.207042 (`rw`, share 2.16e-04) -- and the second is larger, so quoting
                    // the verdict alone reports the floor's choice as a property of the operator.
                    double tau_block_max_raw = -1.0;
                    std::string tau_block_max_name = "none";
                    std::string tau_block_max_raw_name = "none";
                    std::string tau_block_rows;
                    if (layout_initialized_ && cached_layout_.is_exact &&
                        cached_layout_.total_size == dR.numel()) {
                        const auto dR64 = dR.to(torch::kFloat64).reshape({-1});
                        const auto As64 = As.to(torch::kFloat64).reshape({-1});
                        const double n_As_all = As64.norm().item<double>();
                        for (const auto& blk : cached_layout_.blocks) {
                            const auto d_b = dR64.slice(0, blk.start, blk.start + blk.size);
                            const auto a_b = As64.slice(0, blk.start, blk.start + blk.size);
                            const double na = a_b.norm().item<double>();
                            // Floor: a block carrying <0.1% of ||A s|| is not being exercised by
                            // this step, and its ratio is noise over noise.
                            const double floor_b = tau_excitation_share_ * n_As_all;
                            const bool excited = (na >= floor_b);
                            const double den = std::max(na, floor_b);
                            const double num = (d_b - a_b).norm().item<double>();
                            const double tb = den > 0.0 ? num / den : -1.0;
                            // R13.18 (deep review P1-1): an UNEXCITED block's ratio is normalised
                            // by the FLOOR, not by its own ||A s||, so a small value means "this
                            // step barely touched the block", NOT "the Jacobian is accurate here".
                            // The raw ratio is reported beside it so the two cannot be confused --
                            // measured, rw had share 2.16e-04 (below the 1e-3 floor) and an
                            // emitted 0.0447 whose RAW value is ~0.207, and the campaign doc read
                            // the emitted number as evidence of accuracy.
                            const double tb_raw = na > 0.0 ? num / na : -1.0;
                            if (excited && tb > tau_block_max) {
                                tau_block_max = tb;
                                tau_block_max_name = blk.name;
                            }
                            if (tb_raw > tau_block_max_raw) {
                                tau_block_max_raw = tb_raw;
                                tau_block_max_raw_name = blk.name;
                            }
                            tau_block_rows += " tau_" + std::string(blk.name) + "=" +
                                              std::to_string(tb) +
                                              " tauraw_" + std::string(blk.name) + "=" +
                                              std::to_string(tb_raw) +
                                              " share_" + std::string(blk.name) + "=" +
                                              std::to_string(n_As_all > 0.0 ? na / n_As_all : -1.0) +
                                              " excited_" + std::string(blk.name) + "=" +
                                              (excited ? "1" : "0");
                        }
                    }
                    // R13.17 (external review P1-2): was the step REALIZED, and does the
                    // difference stand above roundoff? At float32 a small step can leave the
                    // stored state unchanged while tau and the ratio still print plausibly.
                    double realized_step_fraction = -1.0, signal_to_roundoff = -1.0;
                    double realized_U_fraction = -1.0;
                    {
                        const auto K64 = K.detach().to(torch::kFloat64);
                        const auto s64 = dK_scaled.detach().to(torch::kFloat64);
                        const double n_s = s64.norm().item<double>();
                        if (n_s > 0.0) {
                            // What the state actually kept of the step we asked for.
                            const auto K_stored = (K.detach() + dK_scaled.detach())
                                                      .to(torch::kFloat64);
                            realized_step_fraction =
                                (K_stored - K64).norm().item<double>() / n_s;
                            // R13.18 (deep review P1-3): the EVALUATION state. The RHS is
                            // evaluated at U_stage + h*gamma*K, and a step that survives in K can
                            // still be quantized away when h*gamma*s is added to a large
                            // background -- the loss happens at that addition, not at K's storage.
                            const auto U_base = (U_stage + dt * gamma * K.detach());
                            const auto U_pert =
                                (U_stage + dt * gamma * (K.detach() + dK_scaled.detach()));
                            const double n_dU =
                                (dt * gamma * dK_scaled.detach())
                                    .to(torch::kFloat64).norm().item<double>();
                            if (n_dU > 0.0) {
                                realized_U_fraction =
                                    (U_pert.to(torch::kFloat64) - U_base.to(torch::kFloat64))
                                        .norm().item<double>() / n_dU;
                            }
                        }
                        const double n_dR = dR.to(torch::kFloat64).norm().item<double>();
                        const double eps32 = 1.1920929e-07;
                        const double roundoff =
                            eps32 * std::max(
                                R.detach().to(torch::kFloat64).norm().item<double>(),
                                accepted_residual.detach().to(torch::kFloat64)
                                    .norm().item<double>());
                        signal_to_roundoff = roundoff > 0.0 ? n_dR / roundoff : -1.0;
                    }
                    wrf::sdirk3::TaylorDefectInputs tin;
                    // R13.20 (round 9, R9-11): declares v2. It does NOT assert that all four
                    // v2 fields were populated -- three of them are conditionally assigned above
                    // (`tau_block_max` on an exact layout and an excited block,
                    // `realized_step_fraction` on n_s > 0, `realized_U_fraction` on n_dU > 0) --
                    // and when one is missing the verdict fails closed to `ReceiptIncomplete`,
                    // which is what P1-3 was written for. The predecessor comment claimed the
                    // unconditional version.
                    tin.receipt_version = 2;
                    tin.tau_block_max = tau_block_max;
                    tin.realized_step_fraction = realized_step_fraction;
                    tin.realized_U_fraction = realized_U_fraction;
                    tin.signal_to_roundoff = signal_to_roundoff;
                    tin.fd_fallback_free =
                        (wrf::sdirk3::g_jvp_fd_fallback_count.load(
                             std::memory_order_relaxed) == fd_fallbacks_before_tau);
                    tin.alpha_arm_measured = alpha_arm_measured;
                    tin.tau = tau;
                    tin.tau_alpha = tau_alpha;
                    tin.alpha = kTauAlpha;
                    tin.linearity_residual = linearity_residual;
                    const auto tau_verdict = wrf::sdirk3::taylor_defect_verdict(tin);
                    const bool tau_measured =
                        (tau_verdict == wrf::sdirk3::TaylorVerdict::Measured);
                    stats_.taylor_probe_last_iter = newton_iter;
                    std::cerr << "SDIRK3_TAYLOR_DEFECT stage=" << stage
                              << " newton_iter=" << newton_iter
                              // R13.20 (numerics referee, claim 1): the probe's own GATE, and the
                              // denominators it implies. This block is inside `if (step_accepted)`,
                              // so a rejected iteration -- and the zero-update exit that ends the
                              // loop at dt=600 -- is never sampled. A run that printed three tau
                              // rows out of twelve iterations looked like a probe that fired three
                              // times, not one that could not see nine of them.
                              << " probe_gate=step_accepted"
                              << " accepted_so_far=" << stats_.accepted_steps
                              << " rejected_so_far=" << stats_.rejected_steps
                              << " tau=" << tau
                              << " alpha=" << kTauAlpha
                              << " tau_alpha=" << tau_alpha
                              << " tau_alpha_over_tau="
                              << ((tau > 0.0 && tau_alpha >= 0.0) ? tau_alpha / tau : -1.0)
                              << " R_k=" << R.detach().to(torch::kFloat64).norm().item<double>()
                              << " R_k1="
                              << accepted_residual.detach().to(torch::kFloat64).norm().item<double>()
                              << " step_norm="
                              << dK_scaled.detach().to(torch::kFloat64).norm().item<double>()
                              << " As_norm=" << nAs
                              << " achieved_eta=" << gmres_raw_rel_error
                              << " alpha_arm_measured=" << (alpha_arm_measured ? 1 : 0)
                              << " fd_fallback_free=" << (tin.fd_fallback_free ? 1 : 0)
                              << " linearity_residual=" << linearity_residual
                              << " tau_excited_block_max=" << tau_block_max
                              << " tau_excited_block_max_name=" << tau_block_max_name
                              // R13.20 (claim 7.1): the floor, its provenance, and what the
                              // verdict would have named without it.
                              << " tau_excitation_share=" << tau_excitation_share_
                              << " tau_excitation_share_observed="
                              << (tau_excitation_share_observed_ ? 1 : 0)
                              << " tau_raw_block_max=" << tau_block_max_raw
                              << " tau_raw_block_max_name=" << tau_block_max_raw_name
                              << " realized_step_fraction=" << realized_step_fraction
                              << " realized_U_fraction=" << realized_U_fraction
                              << " signal_to_roundoff=" << signal_to_roundoff
                              << tau_block_rows
                              << " tau_verdict="
                              << wrf::sdirk3::taylor_verdict_name(tau_verdict)
                              // The conclusion is printed ONLY when the preconditions hold. An
                              // FD matvec at float32 is noise-limited at about the magnitude
                              // tau itself reports, so "tau=0.018, the linearization is
                              // faithful to 2%" and "tau=0.018, we measured the noise floor"
                              // are the same row without this gate.
                              << (tau_measured
                                  ? "  (tau<<1: the AD tangent is faithful TO THE PRIMAL IT"
                                    " DIFFERENTIATES over this accepted step, and the inner solve"
                                    " is binding -- a wrong compute_rhs is invisible here;"
                                    " tau=O(1) with tau_alpha/tau~alpha: nonlinearity over the"
                                    " step; tau=O(1) with tau_alpha/tau~1: AD-vs-primal Jacobian"
                                    " defect)"
                                  : "  (NO CONCLUSION: preconditions not met -- see"
                                    " tau_verdict; tau and the ratio are arithmetic, not"
                                    " evidence about the Jacobian)")
                              << std::endl;
                }
                if (stats_.argmin_residual_iter < 0 ||
                    (!stats_.newton_residuals.empty() &&
                     stats_.newton_residuals.back() <= stats_.min_residual_seen)) {
                    stats_.min_residual_seen = stats_.newton_residuals.empty()
                        ? stats_.min_residual_seen : stats_.newton_residuals.back();
                    stats_.argmin_residual_iter = newton_iter;
                }
            } else {
                stats_.rejected_steps++;
                if (stats_.first_rejection_iter < 0) stats_.first_rejection_iter = newton_iter;
            }
            if (gmres_total_failure) {
                // RETRACTED (red team round 4). R13.12 moved the first-event index here from
                // the predicate on the claim that `gmres_total_failure` "additionally requires
                // that no step was accepted", making it a different event. That conjunction is
                // VACUOUS: `step_accepted` is false at the point `gmres_total_failure` is
                // formed under the trust region (the attempt loop that can set it runs later)
                // and is exactly `!candidate` without it, so `gmres_total_failure ==
                // gmres_total_failure_candidate` in both configurations. The move is a
                // no-op refactor; records taken before it are NOT untrustworthy. Kept at the
                // predicate because that is where the quantity is defined, not because the old
                // site was wrong.
                stats_.gmres_total_failures++;
            } else {
                stats_.gmres_non_total_failures++;
            }


            // FIX (2025-12-05): Conditional diagnostics for autograd compatibility
            // v20.14r27j: Distinguish accepted vs rejected in log label.
            if (!wrf::sdirk3::g_sdirk3_config.use_autograd) {
                torch::NoGradGuard no_grad;
                float accepted_norm = dK_scaled.norm().to(torch::kCPU).item<float>();
                float new_residual = accepted_residual_norm.to(torch::kCPU).item<float>();
                if (step_accepted) {
                    std::cerr << "[TRUST REGION] Accepted step summary: ||dK_scaled||=" << accepted_norm
                              << ", ||R_new||=" << new_residual
                              << ", rho=" << last_rho << std::endl;
                } else {
                    std::cerr << "[TRUST REGION] Rejected (all attempts failed): ||dK_scaled||=" << accepted_norm
                              << ", ||R_kept||=" << new_residual << std::endl;
                }
            }

            // Optional line search on accepted step (backtracking Armijo rule)
            torch::Tensor residual_after_step = accepted_residual;
            // v20.5 FIX: Use scaled RMS norm for line search consistency with trust-region and convergence.
            // Previous bug: residual_after_step_val used unscaled L2 (from accepted_residual_norm)
            // while initial_residual used scaled RMS (from res_norm_tensor). This caused
            // ||R_new|| ≈ 1.7e8 vs target ≈ 3.5, forcing alpha to collapse every iteration.
            float residual_after_step_val = compute_scaled_rms_norm(accepted_residual);

            // v20.14r27g: Skip line search when step is zero (all trust-region attempts rejected)
            // or dK_scaled is negligible. Running Armijo on a zero step wastes 10 RHS evals.
            bool skip_line_search = !step_accepted;
            // Phase 3C: Skip LS when TR accepted (step quality validated by rho) or budget exhausted
            if (step_accepted || rhs_budget <= 0) skip_line_search = true;
            if (!skip_line_search) {
                torch::NoGradGuard no_grad;
                float dK_scaled_norm_check = dK_scaled.norm().to(torch::kCPU).item<float>();
                skip_line_search = (dK_scaled_norm_check < 1e-14f);
            }

            if (options_.use_line_search && !skip_line_search) {
                // Phase 3B: Gate LS diagnostics behind debug_level >= 2
                bool ls_verbose = (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 &&
                                   !wrf::sdirk3::g_sdirk3_config.use_autograd);
                if (ls_verbose) {
                    std::cerr << "[LINE SEARCH] Starting backtracking:" << std::endl;
                }
                // FIX (2025-12-05): Use guarded_item for autograd compatibility
                float initial_residual = guarded_item<float>(res_norm_tensor);
                if (ls_verbose) {
                    std::cerr << "  Initial residual: " << initial_residual << std::endl;
                }

                float rho_ls = 0.5f;
                float c = options_.line_search_alpha;  // v20.14r27g: was hardcoded 1e-4f
                float target_val = initial_residual * (1.0f - c * alpha);

                if (ls_verbose) {
                    std::cerr << "  ls_iter=0: alpha=1"
                              << ", ||R_new||=" << residual_after_step_val
                              << ", target=" << target_val
                              << (residual_after_step_val <= target_val ? " ACCEPT" : " reject") << std::endl;
                }

                if (residual_after_step_val > target_val) {
                    float alpha_trial = alpha;
                    for (int ls_iter = 1; ls_iter < 10 && rhs_budget > 0; ++ls_iter) {
                        alpha_trial *= rho_ls;
                        torch::Tensor K_new = K + alpha_trial * dK_scaled;
                        torch::Tensor U_new = U_stage + dt * gamma * K_new;

                        // Preserve graph only for explicit adjoint windows; otherwise keep no-grad LS.
                        torch::Tensor F_new;
                        if (wrf::sdirk3::g_sdirk3_config.use_autograd && options_.retain_graph_for_adjoint) {
                            // Preserve graph through line search for 4DVAR applications
                            F_new = compute_rhs(U_new);
                        } else {
                            torch::NoGradGuard no_grad_ls;
                            F_new = compute_rhs(U_new);
                        }
                        --rhs_budget;  // Phase 3C: LS RHS budget
                        torch::Tensor R_new = K_new - F_new;

                        // v20.5 FIX: Use scaled RMS norm for line search consistency.
                        // Previous bug: used unscaled L2 norm (R_new.norm()) while initial_residual
                        // and target used scaled RMS, causing alpha collapse.
                        float res_new_val = compute_scaled_rms_norm(R_new);
                        float target_trial = initial_residual * (1.0f - c * alpha_trial);

                        if (ls_verbose) {
                            std::cerr << "  ls_iter=" << ls_iter << ": alpha=" << alpha_trial
                                      << ", ||R_new||=" << res_new_val
                                      << ", target=" << target_trial
                                      << (res_new_val <= target_trial ? " ACCEPT" : " reject") << std::endl;
                        }

                        if (res_new_val <= target_trial) {
                            alpha = alpha_trial;
                            residual_after_step = R_new;
                            residual_after_step_val = res_new_val;
                            break;
                        }

                        if (ls_iter == 9) {
                            alpha = alpha_trial;
                            residual_after_step = R_new;
                            residual_after_step_val = res_new_val;
                            if (ls_verbose) {
                                std::cerr << "[LINE SEARCH] Reached max iterations, using alpha="
                                          << alpha << std::endl;
                            }
                        }
                    }

                    // FIX (2025-12-04): Proportional radius adjustment based on actual step reduction
                    // If line search reduced alpha significantly, adjust trust radius proportionally
                    // This prevents radius from being over-expanded after heavy step reduction
                    if (alpha < 1.0f) {
                        // Proportional reduction: more aggressive for smaller alpha
                        float reduction_factor = (alpha < 0.25f) ? 0.25f :
                                                 (alpha < 0.5f)  ? 0.5f  : 0.75f;
                        trust_radius_ = std::max(trust_radius_ * reduction_factor, trust_radius_min_);
                        if (ls_verbose) {
                            std::cerr << "[TRUST REGION] Line search alpha=" << alpha
                                      << ", proportional radius reduction=" << reduction_factor
                                      << ", new radius=" << trust_radius_ << std::endl;
                        }
                    }
                }
            }

            // OPT Pass33: Gate diagnostic with debug_level >= 2 + sampling (was: unconditional)
            // OPT Pass33+: Use configurable sample period (0=every iteration)
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2 &&
                (wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 ||
                 (newton_iter + 1) % wrf::sdirk3::g_sdirk3_config.debug_sample_period == 0 || newton_iter == 0)) {
                torch::NoGradGuard no_grad;
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                float dK_scaled_norm = dK_scaled.norm().to(torch::kCPU).item<float>();
                std::cerr << "[NEWTON DEBUG] Update step:" << std::endl;
                std::cerr << "  alpha = " << alpha << std::endl;
                std::cerr << "  ||alpha*dK_scaled|| = " << (alpha * dK_scaled_norm) << std::endl;
            }

            // Update K with damped and scaled step
            K = K + alpha * dK_scaled;  // Functional operation to preserve autograd graph

            // ================================================================
            // R9 P0-D: NONLINEAR LEDGER (opt-in, default OFF)
            // ================================================================
            // "The stage failed" conflates two different failures:
            //
            //   (a) the LINEAR system was not solved -- Krylov made no progress, so the step
            //       is not a Newton step at all; the lever is the operator/preconditioner.
            //   (b) the linear system WAS solved and the nonlinear residual still did not
            //       fall -- the local linear model does not describe the map; the lever is
            //       the step (damping, trust radius) or the formulation.
            //
            // Separating them needs the linear model's PREDICTION and the nonlinear TRUTH from
            // the SAME trial, in the norm the stage gate judges:
            //
            //   predicted   (1-a) R - a r_g        (= R + a A dK, since b = -R and r_g = b - A dK)
            //   actual      R(K + a dK)
            //
            // The prediction needs no operator call -- the identity above turns it into the
            // GMRES residual, which the solve already produced. The actual is `accepted_residual`,
            // which production already computed at the trial point. So this costs no RHS and no
            // JVP; it only reads.
            //
            // BUT THE IDENTITY IS ONLY VALID FOR THE STEP a dK. Production applies
            // `alpha * dK_scaled`, and dK_scaled is NOT always dK: the trust region can shrink
            // it, the GMRES-total-failure path zeroes it, and the fallback path replaces it with
            // a DIFFERENT vector (dK_recovery). Predicting for a dK the run never took and
            // printing it beside the actual for the step it did take is the same
            // compare-two-different-things error this review is about.
            //
            // So the applied step is projected onto the dK THE SOLVE RETURNED (snapshotted
            // before the post-solve mutations). If it is parallel -- which covers the
            // full step (c=1), any trust shrink (0<c<1) and the zero step (c=0, where the
            // prediction correctly collapses to R) -- the identity holds with the EFFECTIVE
            // alpha, alpha*c. If it is not parallel, the step is not a multiple of the GMRES
            // solution at all and there IS no free prediction: the row reports -1 and says why,
            // rather than a number that describes a different step.
            if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_NONLINEAR_LEDGER")) {
                torch::NoGradGuard ng_led;
                // The STAGE-ENTRY weights, not a per-iteration recapture.
                //
                // newton_weights_for() re-captures E at the current linearization point, so a
                // ledger built on it would weight each row by a DIFFERENT norm -- valid within
                // a row, silently incomparable ACROSS rows, which is exactly the mistake this
                // whole review is about. One E per stage makes the iteration sequence readable.
                const auto* w_led = stage_weights_for(stage);
                torch::Tensor E_inv;
                if (w_led != nullptr && layout_initialized_ &&
                    cached_layout_.is_valid() && cached_layout_.total_size == R.numel()) {
                    E_inv = wrf::sdirk3::inverse_scale_vector(cached_layout_, w_led->scale, R);
                    if (E_inv.defined() && E_inv.numel() != R.numel()) E_inv = torch::Tensor{};
                }
                auto wnorm = [&](const torch::Tensor& v) -> double {
                    if (!v.defined()) return -1.0;
                    const auto v64 = v.detach().to(torch::kFloat64);
                    const auto z = E_inv.defined() ? v64 * E_inv.to(torch::kFloat64) : v64;
                    return z.norm().item<double>() /
                           std::sqrt(static_cast<double>(z.numel()));
                };
                // r_g lives in the SCALED space the solve runs in; S maps it back.
                torch::Tensor r_g_phys;
                if (ledger_r_gmres.defined()) {
                    r_g_phys = scaling_initialized_ && S_diag_.defined() &&
                               S_diag_.numel() == ledger_r_gmres.numel()
                                   ? S_diag_ * ledger_r_gmres
                                   : ledger_r_gmres;
                }
                // Is the applied step a multiple of the GMRES solution?
                double step_c = 0.0;              // dK_scaled = step_c * dK
                bool   step_parallel = false;
                if (ledger_dK_solve.defined() &&
                    ledger_dK_solve.numel() == dK_scaled.numel()) {
                    const auto a64 = dK_scaled.detach().to(torch::kFloat64);
                    const auto b64 = ledger_dK_solve.to(torch::kFloat64);
                    const double bb = b64.dot(b64).item<double>();
                    const double an = a64.norm().item<double>();
                    if (bb > 0.0) {
                        step_c = a64.dot(b64).item<double>() / bb;
                        const double resid = (a64 - step_c * b64).norm().item<double>();
                        // Scale the tolerance by the step itself; a zero step is exactly parallel.
                        step_parallel = (resid <= 1.0e-6 * std::max(an, 1.0e-30));
                    } else {
                        step_parallel = (an == 0.0);
                    }
                }   // no snapshot -> step_parallel stays false -> pred_valid = 0
                const double alpha_eff = alpha * step_c;
                torch::Tensor R_pred;
                if (step_parallel && r_g_phys.defined() && r_g_phys.numel() == R.numel()) {
                    R_pred = (1.0 - alpha_eff) * R.detach() - alpha_eff * r_g_phys;
                }
                const double pred = wnorm(R_pred);
                const double actual = wnorm(accepted_residual);
                // THE NORM RATIO IS NOT AN ACCURACY CLAIM.
                //
                // ||r_actual|| / ||r_pred|| ~ 1 says the two vectors are the same LENGTH; it
                // says nothing about direction, and two vectors of equal norm can be
                // orthogonal. Reporting only the ratio is how "the linearization is faithful
                // to 0.5%" got written from evidence that does not support it. The defect
                // ||r_actual - r_pred|| / ||r_pred|| is the quantity that does.
                double pred_defect_rel = -1.0;
                if (R_pred.defined() && accepted_residual.defined() &&
                    R_pred.numel() == accepted_residual.numel() && pred > 0.0) {
                    const auto d = accepted_residual.detach() - R_pred.detach();
                    pred_defect_rel = wnorm(d) / pred;
                }
                std::cerr << "SDIRK3_NONLINEAR_LEDGER stage=" << stage
                          << " iter=" << newton_iter
                          << " alpha=" << alpha
                          << " step_over_dK=" << step_c
                          << " alpha_eff=" << alpha_eff
                          << " step_is_multiple_of_dK=" << (step_parallel ? 1 : 0)
                          << " dk_norm=" << dK_scaled.detach().norm()
                                                .to(torch::kFloat64).item<double>()
                          << " E_weighted=" << (E_inv.defined() ? 1 : 0)
                          << " R_before=" << wnorm(R)
                          << " R_pred_linear=" << pred
                          << " R_actual=" << actual
                          << " actual_over_pred=" << ((pred > 0.0 && actual >= 0.0)
                                                          ? actual / pred : -1.0)
                          << " pred_defect_rel=" << pred_defect_rel
                          << " pred_valid=" << (R_pred.defined() ? 1 : 0)
                          << " gmres_rel_error=" << gmres_raw_rel_error
                          << " gmres_iters=" << gmres_iters
                          << std::endl;
            }

            // PR 8: per-iteration update record (opt-in). Emitted after the
            // update is applied; reads only.
            if (stage_diag_enabled()) {
                emit_stage_diag([&](std::ostream& os) {
                os << "SDIRK3_NEWTON_DIAG ts=" << global_timestep_
                          << " stage=" << stage
                          << " iter=" << newton_iter
                          << " event=update"
                          << " accepted=" << (step_accepted ? 1 : 0)
                          << std::scientific
                          << " alpha=" << alpha
                          << " dk_norm=" << diag_norm(dK_scaled)
                          << " res_after=" << diag_norm(accepted_residual)
                          << std::defaultfloat
                          << " gmres_total_failure=" << (gmres_total_failure ? 1 : 0)
                          << " state_finite=" << (diag_all_finite(K) ? 1 : 0)
                          << "\n";
                });
            }

            // FIX 2026-01-29: Krylov iteration counter is now updated at line ~2241
            // via stats_.total_krylov_iterations += gmres_result.iterations

            // FIX 2026-01-31: Detect Newton stagnation (all trust-region attempts rejected).
            // If dK_scaled is zero (no update accepted), count consecutive stagnation.
            // Break early after 3 consecutive stagnating iterations to avoid wasting JVP calls.
            {
                torch::NoGradGuard no_grad;
                float dK_scaled_norm_check = dK_scaled.norm().to(torch::kCPU).item<float>();
                if (dK_scaled_norm_check < 1e-15f) {
                    stagnation_count++;
                    // v7-fix: GMRES total failure → K unchanged → next iter identical.
                    // Break immediately instead of waiting for stall_limit (saves ~4s/iter).
                    if (gmres_total_failure) {
                        // v20.14r62: Stage-2 hopeless promotion at N=0.
                        // Promote capped Stage-2 budget mode immediately so the next
                        // solve enters reduced budget without waiting for end-of-solve state
                        // updates. Do not add an extra Newton retry in the same solve.
                        const bool promote_stage2_hopeless =
                            (stage == 2 &&
                             newton_iter == 0 &&
                             !stage2_intra_step_cap_retry_used &&
                             !stage2_hopeless_budget_mode_ &&
                             wrf::sdirk3::g_sdirk3_config.stage2_gmres_restart > 0 &&
                             wrf::sdirk3::g_sdirk3_config.stage2_max_krylov_restarts == 1);
                        if (promote_stage2_hopeless) {
                            stage2_intra_step_cap_retry_used = true;
                            stage2_hopeless_budget_mode_ = true;
                            stage2_hopeless_streak_ = std::max(stage2_hopeless_streak_, 1);
                            stage2_hopeless_promoted_early = true;
                            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                                std::cerr << "[GMRES HOPLESS MODE] Stage 2 promoted in-step at N=0; "
                                          << "enabling capped budget for next solve (no in-step retry)." << std::endl;
                            }
                        }
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                            std::cerr << "[Newton] GMRES total failure + zero update at iter "
                                      << newton_iter << ". K unchanged → breaking early." << std::endl;
                        }
                        // R13.17: the exit `not_recorded` was reporting at dt=600.
                        // R13.18 (round 7, P0-B) -- NAMED FOR WHAT IT MEASURES. This site is
                        // gated by ||dK|| < 1e-15 (the accepted update is numerically ZERO) AND
                        // `gmres_total_failure`, which under the default configuration is
                        // `raw > 1 || rel >= 0.999` on ||r||/||b||. It therefore does NOT mean
                        // "the linear solve produced nothing": in the 12x-budget run that carried
                        // this stamp the worst solve removed 13.8% of its own residual. It was
                        // called LinearSolveFailure and used to retract a standing claim; the
                        // measurement (both runs break here) stands, the inference did not.
                        stats_.newton_termination = static_cast<int>(
                            wrf::sdirk3::NewtonTerminationReason::ZeroUpdateAfterTotalFailure);
                        // R13.18 (deep review P0-4): the receipt of THIS solve -- the one that
                        // ended the loop -- not the stage's worst. Those need not be the same
                        // iteration, and subtyping a terminal event from another iteration's
                        // evidence is how a category ends up describing a solve that did not
                        // end anything.
                        // Promote THIS iteration's solve receipt (recorded above, where
                        // gmres_result was in scope) as the exit solve's -- and only if it IS
                        // this iteration's. `last_*` is written per solve; a Newton iteration
                        // that took a path without a solve would otherwise donate a stale
                        // receipt to the exit event, labelled "the receipt of THIS solve".
                        if (stats_.last_solve_iter != newton_iter) {
                            // No solve receipt for THIS iteration: the exit event gets none
                            // rather than the previous iteration's, which the classifier would
                            // otherwise subtype from.
                            stats_.exit_krylov_iter = -1;
                        } else {
                            stats_.exit_krylov_iter = stats_.last_solve_iter;
                            stats_.exit_rho_stop_final = stats_.last_rho_stop_final;
                            stats_.exit_rho_S_final = stats_.last_rho_S_final;
                            stats_.exit_D_reached = stats_.last_D_reached;
                            stats_.exit_S_reached = stats_.last_S_reached;
                            stats_.exit_stopping_metric = stats_.last_stopping_metric;
                            stats_.exit_tolerance_source = stats_.last_tolerance_source;
                            stats_.exit_budget_exhausted = stats_.last_budget_exhausted;
                            stats_.exit_rho_E_final = stats_.last_rho_E_final;
                            stats_.exit_E_reached = stats_.last_E_reached;
                        }
                        break;
                    }
                    // v20.14r36: Configurable zero-step stagnation limit (was hardcoded 3).
                    const int stall_limit = wrf::sdirk3::g_sdirk3_config.newton_zero_step_stall_limit;
                    if (stagnation_count >= stall_limit) {
                        std::cerr << "[Newton] STAGNATION: " << stagnation_count
                                  << " consecutive zero-update iterations (limit=" << stall_limit
                                  << "). Trust-region cannot find descent direction. "
                                  << "Breaking at iter=" << newton_iter << std::endl;
                        stats_.newton_termination = static_cast<int>(
                            wrf::sdirk3::NewtonTerminationReason::ZeroStepStall);
                        break;
                    }
                } else {
                    stagnation_count = 0;
                }
            }

            // v20.14r35: Newton residual stall detection.
            // When GMRES is nearly useless (rel_error > 0.9) and Newton residual
            // decreased < stall_threshold from previous iteration, exit early.
            // Threshold aligned with near_fail_floor to prevent accepting steps
            // then immediately stalling (was 5%, now configurable, default 0.3%).
            // Set via env: WRF_SDIRK3_NEWTON_STALL_THRESHOLD
            {
            const float stall_threshold = wrf::sdirk3::g_sdirk3_config.newton_stall_threshold;
            if (newton_iter > 0 && prev_iter_res_norm > 1e-12f && res_norm_for_stats > 0.0f
                && gmres_rel_error > 0.9f) {
                float rel_decrease = (prev_iter_res_norm - res_norm_for_stats) / prev_iter_res_norm;
                if (rel_decrease < stall_threshold) {
                    newton_stall_count++;
                    // v20.14r27j: Require 2 consecutive stalls before early exit.
                    // Single-iteration stall may be transient (e.g., trust-region radius reset).
                    if (newton_stall_count >= 2) {
                        // R13.18 (deep review P1-5): this comment used to say "NOT a
                        // Newton-loop exit ... stamping it here would attribute the loop's exit to
                        // a site that does not end it" -- written when I believed the stall
                        // detector and this break were two different sites. They are one: the
                        // `break` ten lines below ends the Newton loop, and the stamp is correct.
                        // A comment that denies what the code under it does is the same defect
                        // class as a field nothing reads.
                        std::cerr << "[Newton] RESIDUAL STALL: iter " << newton_iter
                                  << ", rel_decrease=" << rel_decrease
                                  << " < " << stall_threshold
                                  << ", gmres_rel_error=" << gmres_rel_error
                                  << ". Exiting early (prev_res=" << prev_iter_res_norm
                                  << ", cur_res=" << res_norm_for_stats << ")." << std::endl;
                        stats_.newton_termination = static_cast<int>(
                            wrf::sdirk3::NewtonTerminationReason::ResidualStall);
                        break;
                    }
                } else {
                    newton_stall_count = 0;
                }
            }
            } // end stall detection scope
            prev_iter_res_norm = res_norm_for_stats;
        }

        // Failed to converge — record actual iterations performed, not max.
        stats_.newton_iterations = actual_newton_iters;
        stats_.converged = false;
        // R13.17 (external review P0-3): only if no site inside the loop already said why, AND
        // the loop actually reached its bound.
        //
        // The first version of this said "nothing claimed it, so it was the budget" -- and the
        // very first verification run printed `newton_exit=budget_exhausted` for a loop that used
        // FOUR of twelve iterations. Absence of a recorded reason is not evidence of one, which
        // is the rule this campaign has applied to every other field; applying it to the field
        // added to fix a reconstruction would have been a reconstruction with a confident name.
        if (stats_.newton_termination ==
            static_cast<int>(wrf::sdirk3::NewtonTerminationReason::NotRecorded) &&
            options_.max_newton_iter > 0 &&
            actual_newton_iters >= options_.max_newton_iter) {
            stats_.newton_termination = static_cast<int>(
                wrf::sdirk3::NewtonTerminationReason::BudgetExhausted);
        }

        // v20.14r61: Update hopeless Stage-2 budget mode state.
        if (stage == 2) {
            if (stage2_hopeless_detected) {
                if (!stage2_hopeless_promoted_early) {
                    stage2_hopeless_streak_ = std::min(stage2_hopeless_streak_ + 1, 1000000);
                    stage2_hopeless_budget_mode_ = true;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES HOPLESS MODE] Stage 2 hopeless pattern detected, streak="
                                  << stage2_hopeless_streak_ << " (enabled)\n";
                    }
                } else if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[GMRES HOPLESS MODE] Stage 2 hopeless pattern detected "
                              << "(already promoted in-step, streak=" << stage2_hopeless_streak_
                              << ")\n";
                }
            } else if (stage2_hopeless_budget_mode_) {
                stage2_hopeless_streak_ = std::max(stage2_hopeless_streak_ - 1, 0);
                if (stage2_hopeless_streak_ == 0) {
                    stage2_hopeless_budget_mode_ = false;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES HOPLESS MODE] Stage 2 recovered, disabling capped budget mode.\n";
                    }
                }
            }
        }

        // v20.14r60: Update hopeless Stage-3 budget mode state.
        if (stage >= 3) {
            if (stage3_hopeless_detected) {
                stage3_hopeless_streak_ = std::min(stage3_hopeless_streak_ + 1, 1000000);
                stage3_hopeless_budget_mode_ = true;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                    std::cerr << "[GMRES HOPLESS MODE] Stage 3 hopeless pattern detected, streak="
                              << stage3_hopeless_streak_ << " (enabled)\n";
                }
            } else if (stage3_hopeless_budget_mode_) {
                stage3_hopeless_streak_ = std::max(stage3_hopeless_streak_ - 1, 0);
                if (stage3_hopeless_streak_ == 0) {
                    stage3_hopeless_budget_mode_ = false;
                    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                        std::cerr << "[GMRES HOPLESS MODE] Stage 3 recovered, disabling capped budget mode.\n";
                    }
                }
            }
        }

        // STATS FIX: Extract final residual accurately even at debug_level=0
        if (!stats_.newton_residuals.empty()) {
            // Have scalar residuals from inner loop (need_scalar=true)
            stats_.final_residual = stats_.newton_residuals.back();
            stats_.final_residual_measured = true;
        } else if (res_norm_detached.defined()) {
            // Fast-path: single .item() on failure (debug_level=0 without adaptive)
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            stats_.final_residual = res_norm_detached.to(torch::kCPU).item<float>();
            stats_.final_residual_measured = true;
        } else {
            // Should never reach here, but fallback to tracked value
            stats_.final_residual = last_res_norm;
            stats_.final_residual_measured = true;
        }
        
        // Debug: print convergence failure
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "\n=== NEWTON SOLVER FAILED TO CONVERGE ===" << std::endl;
            std::cerr << "  Stage: " << stage << std::endl;
            std::cerr << "  Final residual: " << std::scientific << stats_.final_residual << std::defaultfloat << std::endl;
            std::cerr << "  Newton iterations: " << stats_.newton_iterations << std::endl;
            std::cerr << "=======================================" << std::endl;
        }
        
        // Update adaptive control parameters on failure
        if (options_.use_adaptive_timestep) {
            // Simple adaptive timestep reduction on failure
            adaptive_control_.current_dt = adaptive_control_.current_dt * 0.5f;
            if (adaptive_control_.current_dt < adaptive_control_.dt_min) {
                adaptive_control_.current_dt = adaptive_control_.dt_min;
            }
        }
        
        // Save checkpoint if needed (even for failed convergence).
        maybe_save_checkpoint(U_n, stage);
        
        // Return failure result — use actual iterations, not max (v20.14r27g)
        WRFNewtonKrylovSolver::NewtonResult result;
        result.K = K;  // Return best K we have
        result.converged = false;
        result.iterations = actual_newton_iters;
        result.final_residual = stats_.final_residual;
        // R13.10 (N6): the probe was requested and the loop ended before its target.
        if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_FROZEN_MI_AB") &&
            !frozen_ab_fired_this_solve_) {
            std::cerr << "SDIRK3_FROZEN_AB_SYSTEM stage=" << stage
                      << " ab_valid=0 ab_reason=target_iteration_not_reached"
                      << " newton_iters_run=" << actual_newton_iters
                      << "  (the Newton loop ended before the requested iteration; no A/B"
                         " was recorded in this solve)" << std::endl;
        }
        if (actual_newton_iters < options_.max_newton_iter) {
            result.message = "Newton solver exited early (stall/stagnation) at iter "
                           + std::to_string(actual_newton_iters);
        } else {
            result.message = "Newton solver failed to converge within "
                           + std::to_string(options_.max_newton_iter) + " iterations";
        }
        update_stage_predictor_cache(false, result.K);
        return result;
    }
    
    void reset_stats() {
        // R13.10: ALL per-solve fields, through the one function a test can reject.
        WRFNewtonKrylovSolver::reset_per_solve(stats_);

        // PR 9F.9.1: clear the numerical-shadow state at solve entry so a stale S0 or
        // r_g from a PREVIOUS stage/solve of the same size can never be silently reused
        // (the shadow gates only check defined()+numel()). S0 is re-frozen at iter 0.
        S0_inv_diag_ = torch::Tensor();
        last_gmres_r_true_ = torch::Tensor();

        // v20.14r19: Reset per-stage preconditioner fallback stats.
        precond_fallback_count_ = 0;
        precond_total_calls_ = 0;

        // NOTE: adaptive_tuning_once_per_run_ is intentionally NOT reset here.
        // It is a run-lifetime flag (set once, never cleared).
        // Rationale: b_wave residual profile is stationary across timesteps;
        // repeated adjustment would oscillate. See declaration at line ~1173.

        // Reset Jacobian cache
        jacobian_cache_.is_valid = false;
        jacobian_cache_.reuse_count = 0;

        // Reset trust-region radius for next solve (from config, not hardcoded)
        trust_radius_ = wrf::sdirk3::g_sdirk3_config.nk_trust_radius;
    }
};

// Public interface
sdirk3::WRFNewtonKrylovSolver::WRFNewtonKrylovSolver(const WRFNewtonKrylovOptions& options, int mu_size) 
    : pImpl(std::make_unique<Impl>(options, mu_size)) {}

sdirk3::WRFNewtonKrylovSolver::~WRFNewtonKrylovSolver() = default;

sdirk3::WRFNewtonKrylovSolver::NewtonResult sdirk3::WRFNewtonKrylovSolver::solve_stage_with_status(
    const torch::Tensor& U_n,
    const torch::Tensor& K_prev,
    const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
    float dt,
    float gamma,
    int stage) {
    
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cerr << "\n=== WRFNewtonKrylovSolver::solve_stage_with_status WRAPPER ENTRY ===" << std::endl;
        std::cerr << "  pImpl pointer: " << pImpl.get() << std::endl;
        std::cerr << "  U_n size: " << U_n.numel() << std::endl;
        std::cerr << "  dt: " << dt << ", gamma: " << gamma << ", stage: " << stage << std::endl;
        // FIX Round156: Removed flush() - std::endl already flushes
    }
    
    if (!pImpl) {
        std::cerr << "ERROR: pImpl is null!" << std::endl;
        NewtonResult result;
        result.converged = false;
        result.iterations = 0;
        result.final_residual = 1e10f;
        result.message = "ERROR: Newton solver pImpl is null";
        return result;
    }
    
    try {
        auto result = pImpl->solve_stage_impl(U_n, K_prev, compute_rhs,
                                              std::function<torch::Tensor(const torch::Tensor&)>(),
                                              dt, gamma, stage, torch::Tensor());
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "  pImpl->solve_stage_impl returned with convergence: "
                     << (result.converged ? "TRUE" : "FALSE") << std::endl;
        }
        // PR 8: per-stage summary record (opt-in) — one line per SDIRK stage
        // in the SAME format for stages 1/2/3, at the single funnel every
        // stage solve returns through.
        if (stage_diag_enabled()) {
            emit_stage_diag([&](std::ostream& os) {
            os << "SDIRK3_STAGE_DIAG stage=" << stage
                      << " converged=" << (result.converged ? 1 : 0)
                      << " newton_iters=" << result.iterations
                      << std::scientific
                      << " final_res=" << result.final_residual
                      << std::defaultfloat
                      << " msg=\"" << result.message << "\""
                      << "\n";
            });
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Exception in pImpl->solve_stage_impl: " << e.what() << std::endl;
        NewtonResult result;
        result.converged = false;
        result.iterations = 0;
        result.final_residual = 1e10f;
        result.message = std::string("Exception: ") + e.what();
        return result;
    } catch (...) {
        std::cerr << "ERROR: Unknown exception in pImpl->solve_stage_impl" << std::endl;
        NewtonResult result;
        result.converged = false;
        result.iterations = 0;
        result.final_residual = 1e10f;
        result.message = "Unknown exception in Newton solver";
        return result;
    }
}

// Backward compatibility wrapper without F_phys
torch::Tensor sdirk3::WRFNewtonKrylovSolver::solve_stage(
    const torch::Tensor& U_n,
    const torch::Tensor& K_prev,
    const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
    float dt,
    float gamma,
    int stage) {
    
    return solve_stage(U_n, K_prev, compute_rhs,
                       std::function<torch::Tensor(const torch::Tensor&)>(),
                       dt, gamma, stage, torch::Tensor());
}

// Main implementation with F_phys
torch::Tensor sdirk3::WRFNewtonKrylovSolver::solve_stage(
    const torch::Tensor& U_n,
    const torch::Tensor& K_prev,
    const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
    float dt,
    float gamma,
    int stage,
    const torch::Tensor& F_phys) {  // Physical forcing term

    return solve_stage(U_n, K_prev, compute_rhs,
                       std::function<torch::Tensor(const torch::Tensor&)>(),
                       dt, gamma, stage, F_phys);
}

// Extended implementation with fast-mode RHS for predictor-only refinement
torch::Tensor sdirk3::WRFNewtonKrylovSolver::solve_stage(
    const torch::Tensor& U_n,
    const torch::Tensor& K_prev,
    const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
    const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs_fast,
    float dt,
    float gamma,
    int stage,
    const torch::Tensor& F_phys) {  // Physical forcing term
    
    // FIX Round156: Gate with debug_level >= 2
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cout << "\n=== ENTERED WRFNewtonKrylovSolver::solve_stage ===" << std::endl;
    }

    // Forward to implementation with F_phys
    if (!pImpl) {
        throw std::runtime_error("Newton solver not properly initialized");
    }
    
    auto result = pImpl->solve_stage_impl(U_n, K_prev, compute_rhs, compute_rhs_fast,
                                          dt, gamma, stage, F_phys);

    // PR 8: per-stage summary record (opt-in) — the PRODUCTION entry point
    // (the tile solver calls this overload directly, bypassing
    // solve_stage_with_status, so both funnels emit the same record).
    if (stage_diag_enabled()) {
        emit_stage_diag([&](std::ostream& os) {
        os << "SDIRK3_STAGE_DIAG stage=" << stage
                  << " converged=" << (result.converged ? 1 : 0)
                  << " newton_iters=" << result.iterations
                  << std::scientific
                  << " final_res=" << result.final_residual
                  << std::defaultfloat
                  << " msg=\"" << result.message << "\""
                  << "\n";
        });
    }

    // R4: stage accuracy against a CONVERGED reference, not against a bigger budget.
    //
    // A budget sweep answers "does more Krylov change K", which conflates two questions: how
    // far the shipped solve is from the exact stage increment, and how far one budget is from
    // another. The reference here is defined by CONVERGENCE -- if the reference solve does not
    // itself converge there is no reference, and the probe reports that instead of reporting
    // an accuracy it cannot support.
    //
    // The reference solve runs the real solver at the real state, so it advances the same
    // stateful machinery the production solve did (streaks, warm-start cache, trust radius).
    // That perturbs every LATER stage, and the record says so rather than leaving a run that
    // looks like production but is not. Snapshotting "the state" was the alternative and is
    // the ever-growing list this codebase has already rejected once.
    //
    // A reference is an assumption until it certifies itself, so it is computed at TWO
    // budgets: if the two disagree, the larger one is not converged either and the probe
    // reports the disagreement instead of an accuracy resting on it.
    if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_STAGE_REFERENCE") &&
        (stage_reference_target() == 0 || stage_reference_target() == stage)) {
        auto& cfg_ref = wrf::sdirk3::g_sdirk3_config;
        // RAII, because the probe mutates GLOBAL config. The hand-written restore fifty lines
        // below is only reached on the success path: one exception out of solve_stage_impl and
        // the PRODUCTION solver keeps running at budget 200x30, newton_tol=1e-8 and warm start
        // off for the rest of the run -- a diagnostic silently becoming a configuration change.
        struct ScopedSolverBudget {
            wrf::sdirk3::SDIRK3Config& c;
            int r2, m2, r3, m3, ni;
            float t2, t3, nt;
            bool ws;
            explicit ScopedSolverBudget(wrf::sdirk3::SDIRK3Config& cfg)
                : c(cfg),
                  r2(cfg.stage2_gmres_restart), m2(cfg.stage2_max_krylov_restarts),
                  r3(cfg.stage3_gmres_restart), m3(cfg.stage3_max_krylov_restarts),
                  ni(cfg.max_newton_iter),
                  t2(cfg.stage2_krylov_tol), t3(cfg.stage3_krylov_tol),
                  nt(cfg.newton_tol), ws(cfg.gmres_warmstart) {}
            ~ScopedSolverBudget() {
                c.stage2_gmres_restart = r2;
                c.stage2_max_krylov_restarts = m2;
                c.stage2_krylov_tol = t2;
                c.stage3_gmres_restart = r3;
                c.stage3_max_krylov_restarts = m3;
                c.stage3_krylov_tol = t3;
                c.max_newton_iter = ni;
                c.newton_tol = nt;
                c.gmres_warmstart = ws;
            }
            ScopedSolverBudget(const ScopedSolverBudget&) = delete;
            ScopedSolverBudget& operator=(const ScopedSolverBudget&) = delete;
        } scoped_budget(cfg_ref);

        // The two reference solves must not share a warm start. With it on, the second solve
        // starts from the first's answer and returns it unchanged -- and ref_agree=0 then
        // certifies nothing, because the two solves were never independent.
        cfg_ref.gmres_warmstart = false;

        // A budget large enough that failing to converge is a statement about the OPERATOR.
        cfg_ref.stage2_gmres_restart = cfg_ref.stage3_gmres_restart = 120;
        cfg_ref.stage2_max_krylov_restarts = cfg_ref.stage3_max_krylov_restarts = 20;
        cfg_ref.stage2_krylov_tol = cfg_ref.stage3_krylov_tol = 1.0e-10f;
        cfg_ref.max_newton_iter = 30;
        cfg_ref.newton_tol = 1.0e-8f;

        // R13 B2: the arms must be INDEPENDENT, and turning the warm start off was only the
        // most visible way they were not. This solver also carries hopeless-budget streaks, a
        // trust radius, a preconditioner fallback latch and per-stage warm-start slots ACROSS
        // solves, and every one of them alters what the next solve does. Two arms that share
        // them are two points on one trajectory, not two independent solves, and their
        // agreement cannot certify anything.
        //
        // Every arm therefore starts from the state captured at probe entry. That is not the
        // same as a fresh solver -- preconditioner internals and any cached linearization are
        // outside this list -- so the record says which state was reset rather than claiming
        // isolation it does not have.
        const auto arm_entry = capture_carried_state();
        const std::uint64_t arm_entry_digest = carried_state_digest();
        // Whether each arm actually STARTED from the entry state, rather than whether we
        // asked it to. If restoring does not bring the digest back, something outside the
        // snapshot moved and the arms are three points on one trajectory -- which the
        // certification predicate now refuses rather than certifying on numbers alone.
        bool arm_isolation_ok = true;
        auto restore_arm = [&](const CarriedState& a) {
            restore_carried_state(a);
            if (carried_state_digest() != arm_entry_digest) arm_isolation_ok = false;
        };
        // F_E at each arm's solution. Stage increments can agree while the explicit RHS they
        // produce does not, and F_E(Y_s) is what forces the next stage -- so certifying on
        // the increment alone certifies the wrong quantity.
        double explicit_gap_21 = -1.0, explicit_gap_32 = -1.0;

        auto reference = pImpl->solve_stage_impl(U_n, K_prev, compute_rhs, compute_rhs_fast,
                                                 dt, gamma, stage, F_phys);

        // The certifying second reference, at a strictly larger budget.
        restore_arm(arm_entry);
        cfg_ref.stage2_gmres_restart = cfg_ref.stage3_gmres_restart = 200;
        cfg_ref.stage2_max_krylov_restarts = cfg_ref.stage3_max_krylov_restarts = 30;
        auto reference2 = pImpl->solve_stage_impl(U_n, K_prev, compute_rhs, compute_rhs_fast,
                                                  dt, gamma, stage, F_phys);

        // A THIRD arm, tighter again. Two arms can only report that they disagree; three can
        // report whether the sequence is CONVERGING, which is the question a reference has to
        // answer. R12 R4 measured ref_agree=0.284 against rel_err=0.737 and called it settled
        // on the strength of one gap -- with a third arm, "the gaps are shrinking" and "these
        // are two arbitrary unconverged solves" become distinguishable.
        restore_arm(arm_entry);
        cfg_ref.stage2_gmres_restart = cfg_ref.stage3_gmres_restart = 320;
        cfg_ref.stage2_max_krylov_restarts = cfg_ref.stage3_max_krylov_restarts = 45;
        auto reference3 = pImpl->solve_stage_impl(U_n, K_prev, compute_rhs, compute_rhs_fast,
                                                  dt, gamma, stage, F_phys);
        restore_arm(arm_entry);

        // F_E on each arm's stage state. compute_rhs is the caller's explicit-channel
        // evaluator, so this is the same operator the next stage would be forced by.
        {
            torch::NoGradGuard ng_fe;
            auto stage_state = [&](const torch::Tensor& K) {
                return (K.defined() && U_n.defined() && K.numel() == U_n.numel())
                    ? (U_n.detach() + dt * gamma * K.detach()) : torch::Tensor{};
            };
            auto fe_of = [&](const torch::Tensor& K) -> torch::Tensor {
                const auto Y = stage_state(K);
                if (!Y.defined()) return torch::Tensor{};
                try {
                    return compute_rhs(Y).detach().to(torch::kFloat64).reshape({-1});
                } catch (const std::exception&) {
                    // A diagnostic evaluation that throws is a missing measurement, not a
                    // reason to take down the run it is observing.
                    return torch::Tensor{};
                }
            };
            const auto F1 = fe_of(reference.K), F2 = fe_of(reference2.K),
                       F3 = fe_of(reference3.K);
            auto rel_fe = [](const torch::Tensor& a, const torch::Tensor& b) {
                if (!a.defined() || !b.defined() || a.numel() != b.numel()) return -1.0;
                const double n = a.norm().item<double>();
                return n > 0.0 ? (a - b).norm().item<double>() / n : -1.0;
            };
            explicit_gap_21 = rel_fe(F2, F1);
            explicit_gap_32 = rel_fe(F3, F2);
        }

        // (config restore is ScopedSolverBudget's, at scope exit -- including on an exception)

        torch::NoGradGuard ng_ref;
        std::cerr << "SDIRK3_STAGE_REFERENCE stage=" << stage
                  << " ref_converged=" << (reference.converged ? 1 : 0)
                  << " ref_iters=" << reference.iterations
                  << " ref_final_res=" << reference.final_residual
                  << " shipped_converged=" << (result.converged ? 1 : 0)
                  // Both residuals on one line: "the reference converged" means the solver's
                  // own test passed, which is not the same as a residual near zero.
                  << " shipped_final_res=" << result.final_residual
                  << " ref2_converged=" << (reference2.converged ? 1 : 0)
                  << " ref2_final_res=" << reference2.final_residual
                  << " ref3_converged=" << (reference3.converged ? 1 : 0)
                  << " ref3_final_res=" << reference3.final_residual;

        // The certification, from the rule in wrf_sdirk3_probe_validity.h rather than from a
        // sentence in this record. reference_certified existed only in R12's prose; a field
        // that is written in a document and not emitted by the code is a claim.
        //
        // R13.1: and a field the code emits but nothing READS is the same claim wearing a
        // number. R13 computed this verdict inside its own scope and then printed rel_err,
        // K_ref and the per-block errors from a branch that keyed on reference.converged --
        // so a record could carry reference_certified=0 next to a full accuracy report, which
        // is precisely the R12 R4 reading this was built to prevent. The verdict now gates
        // every accuracy field, and the authority is the TIGHTEST arm rather than the first.
        auto flat = [](const torch::Tensor& t) {
            return t.defined() ? t.detach().to(torch::kFloat64).reshape({-1})
                               : torch::Tensor{};
        };
        auto gap = [&](const torch::Tensor& a, const torch::Tensor& b) {
            if (!a.defined() || !b.defined() || a.numel() != b.numel()) return -1.0;
            const double n = a.norm().item<double>();
            return n > 0.0 ? (a - b).norm().item<double>() / n : -1.0;
        };
        const auto K1 = flat(reference.K), K2 = flat(reference2.K), K3 = flat(reference3.K);
        const auto Ks = flat(result.K);
        wrf::sdirk3::StageReferenceArms arms;
        arms.converged[0] = reference.converged;
        arms.converged[1] = reference2.converged;
        arms.converged[2] = reference3.converged;
        arms.isolated[0] = arms.isolated[1] = arms.isolated[2] = arm_isolation_ok;
        arms.fresh_solver_per_arm = false;   // snapshot/restore, not a fresh solver
        arms.residual[0] = reference.final_residual;
        arms.residual[1] = reference2.final_residual;
        arms.residual[2] = reference3.final_residual;
        arms.state_gap_21 = gap(K2, K1);
        arms.state_gap_32 = gap(K3, K2);
        arms.explicit_gap_21 = explicit_gap_21;
        arms.explicit_gap_32 = explicit_gap_32;
        arms.shipped_gap = gap(K3, Ks);
        const auto cert = wrf::sdirk3::certify_stage_reference(arms);

        std::cerr << " reference_certified=" << (cert.certified ? 1 : 0)
                  << " certification=" << cert.reason
                  << " fresh_solver_per_arm=" << (arms.fresh_solver_per_arm ? 1 : 0)
                  << " selected_state_restored=" << (arm_isolation_ok ? 1 : 0)
                  << " state_gap_32=" << arms.state_gap_32
                  << " state_gap_21=" << arms.state_gap_21
                  << " explicit_gap_32=" << arms.explicit_gap_32
                  << " explicit_gap_21=" << arms.explicit_gap_21;

        if (!cert.certified) {
            // No certified reference, so no accuracy -- and the gaps that ARE computable are
            // named for what they are. "error", "accuracy" and "reference" are reserved for
            // numbers measured against something certified; using them here is how R12 R4's
            // rel_err was read as an accuracy for a whole round.
            std::cerr << " accuracy_valid=0"
                      << " uncertified_gap_to_arm1=" << gap(K1, Ks)
                      << " uncertified_gap_to_arm3=" << arms.shipped_gap
                      << "  (NO CERTIFIED REFERENCE: " << cert.reason
                      << " -- these gaps are differences between unconverged solves, not"
                         " accuracies, and no per-block ranking is emitted from them)"
                      << std::endl;
        } else {
            // The authority is arm 3, the tightest. R13 printed arm 1 under the name K_ref
            // while running three arms specifically so the tightest would exist.
            std::cerr << " accuracy_valid=1"
                      << " reference_arm=3"
                      << " rel_err=" << arms.shipped_gap
                      << " K_shipped=" << (Ks.defined() ? Ks.norm().item<double>() : -1.0)
                      << " K_ref=" << (K3.defined() ? K3.norm().item<double>() : -1.0);
            // Per block, because an aggregate cannot say which equation the shipped solve
            // got wrong -- and the campaign's answer has never been "all of them equally".
            const auto& lay = pImpl->cached_layout_;
            if (lay.is_exact && K3.defined() && Ks.defined() &&
                lay.total_size == Ks.numel() && Ks.numel() == K3.numel()) {
                for (const auto& b : lay.blocks) {
                    const auto ds = Ks.slice(0, b.start, b.start + b.size);
                    const auto dr = K3.slice(0, b.start, b.start + b.size);
                    const double bn = dr.norm().item<double>();
                    std::cerr << " " << b.name << "="
                              << (bn > 0.0 ? (ds - dr).norm().item<double>() / bn : -1.0);
                }
            }
            std::cerr << "  (perturbs every LATER stage: the reference solves advanced the"
                         " same stateful machinery)" << std::endl;
        }
    }

    // R9 P0-C: the one link in the stage-entry chain that was inferred rather than measured.
    // The next stage's base is Y_{s+1} = U_n + dt*sum(a_{s+1,j} K_j), so if Y_3 is orders of
    // magnitude larger than Y_2, the accepted K of THIS stage is where that came from -- but
    // "the accepted K must be large" is an inference until the norm is on the record next to
    // the convergence flag it was accepted under.
    if (wrf::sdirk3::read_experiment_flag("WRF_SDIRK3_STAGE_ENTRY_LEDGER")) {
        torch::NoGradGuard ng_exit;
        std::cerr << "SDIRK3_STAGE_EXIT stage=" << stage
                  << " converged=" << (result.converged ? 1 : 0)
                  << " newton_iters=" << result.iterations
                  << " final_res=" << result.final_residual
                  << " K_norm=" << (result.K.defined()
                                        ? result.K.detach().norm()
                                              .to(torch::kFloat64).item<double>()
                                        : -1.0)
                  << " dt_gamma_K_norm=" << (result.K.defined()
                                        ? (dt * gamma * result.K.detach()).norm()
                                              .to(torch::kFloat64).item<double>()
                                        : -1.0)
                  << " msg=\"" << result.message << "\""
                  << std::endl;
    }

    // Warn if convergence failed but still returning K
    if (!result.converged) {
        std::cerr << "WARNING: Newton solver did not converge! Stage " << stage 
                  << ", residual = " << result.final_residual << std::endl;
        std::cerr << "         Message: " << result.message << std::endl;
    }
    
    return result.K;
}

sdirk3::WRFNewtonKrylovSolver::ConvergenceStats sdirk3::WRFNewtonKrylovSolver::get_stats() const {
    ConvergenceStats s = pImpl->stats_;
    // PR 9E (diagnosis-only): materialize the RAW L2 fast-RHS / defect norms
    // exactly ONCE here -- get_stats() is called once per stage solve -- from the
    // tensors retained sync-free during the Newton loop. This keeps the .item()
    // GPU->CPU transfer off the per-iteration hot path.
    if (pImpl->diag_final_F_.defined() && pImpl->diag_final_R_.defined()) {
        torch::NoGradGuard no_grad;
        s.final_fast_rhs_norm = pImpl->diag_final_F_.norm().item<float>();
        s.final_defect_l2_raw = pImpl->diag_final_R_.norm().item<float>();
    }
    // PR 9F: hand back the coherent {K, F, R} triple and its evaluation-point
    // identifiers. CLONE (once per stage solve, here -- NOT per Newton iteration)
    // so the caller can hold an IMMUTABLE snapshot across later stage solves that
    // reuse the solver's K/F/R buffers; the emitter, not this scalar path, is the
    // norm authority. Empty / -1 when the diagnostic was off.
    if (pImpl->diag_final_K_.defined() && pImpl->diag_final_F_.defined() &&
        pImpl->diag_final_R_.defined()) {
        torch::NoGradGuard no_grad;
        s.defect_K = pImpl->diag_final_K_.clone();
        s.defect_F = pImpl->diag_final_F_.clone();
        s.defect_R = pImpl->diag_final_R_.clone();
    }
    s.defect_newton_iter = pImpl->diag_final_newton_iter_;
    s.defect_retry_generation = pImpl->diag_retry_generation_;
    return s;
}

std::uint64_t sdirk3::WRFNewtonKrylovSolver::solver_id() const {
    return pImpl->solver_id_;
}

void sdirk3::WRFNewtonKrylovSolver::reset_per_solve(ConvergenceStats& s) {
    // Value-initialise, then nothing survives that was not meant to. A field-by-field list
    // is exactly what fell behind the struct for three increments.
    s = ConvergenceStats{};
}

sdirk3::WRFNewtonKrylovSolver::CarriedState
sdirk3::WRFNewtonKrylovSolver::capture_carried_state() const {
    torch::NoGradGuard ng;
    CarriedState s;
    s.stage3_warmstart_disabled   = pImpl->stage3_warmstart_disabled_;
    s.stage2_hopeless_budget_mode = pImpl->stage2_hopeless_budget_mode_;
    s.stage2_hopeless_streak      = pImpl->stage2_hopeless_streak_;
    s.stage3_hopeless_budget_mode = pImpl->stage3_hopeless_budget_mode_;
    s.stage3_hopeless_streak      = pImpl->stage3_hopeless_streak_;
    s.precond_fallback_count      = pImpl->precond_fallback_count_;
    s.trust_radius                = pImpl->trust_radius_;
    s.warmstart_relerr            = pImpl->gmres_warmstart_relerr_stage_;
    // Cloned, not aliased: a warm-start slot restored as a VIEW of a tensor the next solve
    // overwrites is not a restore, and this codebase has already shipped one latch that
    // depended on a value it did not own.
    for (const auto& t : pImpl->gmres_warmstart_stage_) {
        s.warmstart_stage.push_back(t.defined() ? t.detach().clone() : torch::Tensor{});
    }
    return s;
}

void sdirk3::WRFNewtonKrylovSolver::restore_carried_state(const CarriedState& s) {
    torch::NoGradGuard ng;
    pImpl->stage3_warmstart_disabled_    = s.stage3_warmstart_disabled;
    pImpl->stage2_hopeless_budget_mode_  = s.stage2_hopeless_budget_mode;
    pImpl->stage2_hopeless_streak_       = s.stage2_hopeless_streak;
    pImpl->stage3_hopeless_budget_mode_  = s.stage3_hopeless_budget_mode;
    pImpl->stage3_hopeless_streak_       = s.stage3_hopeless_streak;
    pImpl->precond_fallback_count_       = s.precond_fallback_count;
    pImpl->trust_radius_                 = s.trust_radius;
    pImpl->gmres_warmstart_relerr_stage_ = s.warmstart_relerr;
    // R13.1: CLONE on the way back too. Assigning the vector shares tensor storage with the
    // snapshot, so the next solve writing a warm-start slot in place reaches back into the
    // snapshot and the arm after it starts somewhere else entirely -- a restore that the
    // solve it exists to undo can edit. R13's contract tested that CAPTURE clones and never
    // tested the other half of the round trip, which is how this survived a test written
    // specifically to catch it.
    pImpl->gmres_warmstart_stage_.clear();
    pImpl->gmres_warmstart_stage_.reserve(s.warmstart_stage.size());
    for (const auto& t : s.warmstart_stage) {
        pImpl->gmres_warmstart_stage_.push_back(
            t.defined() ? t.detach().clone() : torch::Tensor{});
    }
}

std::uint64_t sdirk3::WRFNewtonKrylovSolver::carried_state_digest() const {
    torch::NoGradGuard ng;
    // FNV-1a over the carried values. Order-sensitive on purpose: two states that differ only
    // in which stage slot holds a warm start are different states.
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
        // The avalanche step is LOAD-BEARING, and plain FNV-1a is wrong here for a specific
        // reason. Multiplication mod 2^64 never carries out of bit 63, so a change confined
        // to that bit maps to a change confined to that bit: h' = h ^ 2^63. An IEEE-754 sign
        // flip is exactly such a change, and TWO of them cancel --
        //     h' = ((h ^ 2^63) ^ (b ^ 2^63)) * P = (h ^ b) * P = h
        // Negating a warm-start vector flips the sign of both its sum and its first moment,
        // so v and -v hashed IDENTICALLY under plain FNV-1a. Measured, not reasoned: the
        // contract's negation case failed with the two digests bit-equal while the sums read
        // +10 and -10. Shifting the high bits down makes bit 63 propagate.
        h ^= h >> 29;
    };
    auto mix_double = [&mix](double d) {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(d), "double is not 64-bit");
        std::memcpy(&bits, &d, sizeof(bits));
        mix(bits);
    };
    mix(pImpl->stage3_warmstart_disabled_ ? 1u : 0u);
    mix(pImpl->stage2_hopeless_budget_mode_ ? 1u : 0u);
    mix(static_cast<std::uint64_t>(pImpl->stage2_hopeless_streak_));
    mix(pImpl->stage3_hopeless_budget_mode_ ? 1u : 0u);
    mix(static_cast<std::uint64_t>(pImpl->stage3_hopeless_streak_));
    mix(static_cast<std::uint64_t>(pImpl->precond_fallback_count_));
    mix_double(static_cast<double>(pImpl->trust_radius_));
    for (float r : pImpl->gmres_warmstart_relerr_stage_) {
        mix_double(static_cast<double>(r));
    }
    for (const auto& t : pImpl->gmres_warmstart_stage_) {
        if (!t.defined() || t.numel() == 0) {
            mix(0u);
            continue;
        }
        // R13.1: the norm is not enough. Mixing only norm and numel makes v and -v the same
        // state -- and v and Pv for any norm-preserving P -- so a sign flip or a permutation
        // of a warm start, either of which changes what the next solve does, was invisible.
        //
        // Still not the bytes: a digest that walks every element of every warm-start vector
        // costs a second solve. Three cheap projections that a sign flip and a permutation
        // both break: the sum (odd under negation), the first-moment weighted sum (broken by
        // any reordering) and the norm.
        const auto t64 = t.detach().to(torch::kFloat64).reshape({-1});
        const auto idx = torch::arange(t64.numel(), t64.options());
        mix_double(t64.norm().item<double>());
        mix_double(t64.sum().item<double>());
        mix_double((t64 * idx).sum().item<double>());
        mix(static_cast<std::uint64_t>(t.numel()));
    }
    return h;
}

void sdirk3::WRFNewtonKrylovSolver::set_stage_weights(
    wrf::sdirk3::FrozenStageWeights weights) {
    pImpl->stage_weights_ = std::move(weights);
}

void sdirk3::WRFNewtonKrylovSolver::reset_stats() {
    pImpl->reset_stats();
}

void sdirk3::WRFNewtonKrylovSolver::set_preconditioner(WRFPreconditioner* precond) {
    pImpl->preconditioner_ = precond;
}

WRFPreconditioner* sdirk3::WRFNewtonKrylovSolver::get_preconditioner() {
    return pImpl->preconditioner_;
}

void sdirk3::WRFNewtonKrylovSolver::update_grid_dimensions(
    int nx, int ny, int nz, int nx_u, int ny_v, int nz_w) {
    // Update stored options
    pImpl->options_.nx = nx;
    pImpl->options_.ny = ny;
    pImpl->options_.nz = nz;
    pImpl->options_.nx_u = nx_u;
    pImpl->options_.ny_v = ny_v;
    pImpl->options_.nz_w = nz_w;

    // Re-initialize StateLayout
    if (nx > 0 && ny > 0 && nz > 0 && nx_u > 0 && ny_v > 0 && nz_w > 0) {
        pImpl->cached_layout_ = StateLayout::from_grid_dims(
            nx, ny, nz, nx_u, ny_v, nz_w);
        pImpl->layout_initialized_ = true;

        // Invalidate scaling vectors and halo mask — layout size may have changed,
        // so S_diag_/S_inv_diag_/halo_mask_ must be rebuilt on next solve_stage_impl call.
        pImpl->scaling_initialized_ = false;
        pImpl->halo_mask_initialized_ = false;

        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[NEWTON SOLVER] Updated grid dimensions:" << std::endl;
            std::cerr << "  Grid: " << nx << "x" << ny << "x" << nz << std::endl;
            std::cerr << "  Stagger: " << nx_u << "," << ny_v << "," << nz_w << std::endl;
            std::cerr << "  Total size: " << pImpl->cached_layout_.total_size << std::endl;
            std::cerr << "  Scaling vectors invalidated (will rebuild)" << std::endl;
        }
    }
}

void sdirk3::WRFNewtonKrylovSolver::update_boundary_periodicity(
    bool periodic_x, bool periodic_y) {
    // v20.14r21: Update periodic flags and invalidate halo mask so it
    // is rebuilt with correct periodicity on next GMRES call.
    bool changed = (pImpl->options_.periodic_x != periodic_x ||
                    pImpl->options_.periodic_y != periodic_y);
    pImpl->options_.periodic_x = periodic_x;
    pImpl->options_.periodic_y = periodic_y;
    if (changed && pImpl->halo_mask_initialized_) {
        pImpl->halo_mask_initialized_ = false;
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[NEWTON SOLVER] Periodic BC updated: px="
                      << periodic_x << " py=" << periodic_y
                      << " — halo mask invalidated" << std::endl;
        }
    }
}

void sdirk3::WRFNewtonKrylovSolver::set_physics_scaling(const torch::Tensor& S_diag) {
    torch::NoGradGuard no_grad;
    // v19.1: detach() without clone() — caller's tensor is alive through solve_stage().
    // reciprocal() creates a new tensor, so S_inv_diag_ is independent.
    // CONTRACT: Caller must NOT mutate S_diag after this call. If that invariant
    // changes in future, revert to S_diag.detach().clone().
    pImpl->S_diag_ = S_diag.detach();
    pImpl->S_inv_diag_ = pImpl->S_diag_.reciprocal();
    pImpl->scaling_initialized_ = true;
    pImpl->physics_scaling_set_ = true;
    pImpl->scaling_device_ = pImpl->S_diag_.device();
    pImpl->scaling_dtype_ = pImpl->S_diag_.scalar_type();
    // v19.1: Gate min/max diagnostic behind debug_level >= 2 (full D2H copy)
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        auto S_cpu = pImpl->S_diag_.detach().to(torch::kCPU);
        std::cerr << "[SCALING] Physics scaling set: size=" << S_cpu.numel()
                  << ", min=" << S_cpu.min().item<float>()
                  << ", max=" << S_cpu.max().item<float>() << std::endl;
    }
}

void sdirk3::WRFNewtonKrylovSolver::clear_physics_scaling() {
    pImpl->physics_scaling_set_ = false;
}

std::vector<torch::Tensor> sdirk3::WRFNewtonKrylovSolver::get_saved_trajectory() const {
    std::vector<torch::Tensor> out;
    out.reserve(pImpl->trajectory_.size());
    for (const auto& state : pImpl->trajectory_) {
        out.push_back(state.detach().clone());
    }
    return out;
}

void sdirk3::WRFNewtonKrylovSolver::clear_saved_trajectory() {
    pImpl->trajectory_.clear();
    pImpl->global_timestep_ = 0;
}

size_t sdirk3::WRFNewtonKrylovSolver::get_saved_trajectory_count() const {
    return pImpl->trajectory_.size();
}

int sdirk3::WRFNewtonKrylovSolver::get_saved_global_timestep() const {
    return pImpl->global_timestep_;
}

void sdirk3::WRFNewtonKrylovSolver::maybe_save_trajectory_checkpoint(
    const torch::Tensor& state,
    int stage) {
    pImpl->maybe_save_checkpoint(state, stage);
}

void sdirk3::WRFNewtonKrylovSolver::set_saved_trajectory(
    const std::vector<torch::Tensor>& checkpoints,
    int global_timestep) {
    pImpl->trajectory_.clear();
    pImpl->trajectory_.reserve(checkpoints.size());
    for (const auto& state : checkpoints) {
        pImpl->trajectory_.push_back(state.detach().clone());
    }
    if (global_timestep >= 0) {
        pImpl->global_timestep_ = global_timestep;
    }
}

// Preconditioner implementations
JacobiPreconditioner::JacobiPreconditioner(const torch::Tensor& diagonal) {
    diagonal_inv_ = 1.0f / (diagonal + 1e-8f);
}

torch::Tensor JacobiPreconditioner::apply(const torch::Tensor& r) {
    return diagonal_inv_ * r;
}

BlockJacobiPreconditioner::BlockJacobiPreconditioner(int nx, int ny, int nz) {
    // Initialize with identity
    velocity_block_inv_ = torch::ones( /* CHECK: May need requires_grad */{3, nz, ny, nx}, torch::kFloat32);
    pressure_block_inv_ = torch::ones({2, nz, ny, nx}, torch::kFloat32);
}

torch::Tensor BlockJacobiPreconditioner::apply(const torch::Tensor& r) {
    // Apply block-diagonal preconditioner
    auto r_u = r.slice(0, 0, 3);  // velocity components
    auto r_p = r.slice(0, 3, 5);  // pressure components
    
    auto z_u = velocity_block_inv_ * r_u;
    auto z_p = pressure_block_inv_ * r_p;
    
    return torch::cat({z_u, z_p}, 0);
}

void BlockJacobiPreconditioner::update(const torch::Tensor& state, float dt, float gamma) {
    // Update preconditioner based on current state
    // For now, keep it simple
}

} // namespace sdirk3
} // namespace wrf
