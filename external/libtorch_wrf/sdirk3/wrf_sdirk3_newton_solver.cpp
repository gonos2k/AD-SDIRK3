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
#include "wrf_sdirk3_stage_history_diag.h"  // PR 9F P2: shared emit_sdirk3_diag_line
#include "wrf_sdirk3_unified_preconditioner.h"  // v20.5: For set_stage_state()
#include <torch/torch.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>  // std::min
#include <stdexcept>
#include <vector>
#include <cstring>  // PR 9B: std::strcmp in the checker direction loop
#include <map>      // PR 9B: per-block best-epsilon tracking in the checker
#include <mutex>    // PR 8.1: emit_stage_diag line-atomic output mutex
#include <sstream>  // PR 8.1: per-record ostringstream (libstdc++ needs it explicit)
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

    if (used_fd_fallback && wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[JVP FWAD] Fallback to finite-difference JVP (fwAD unavailable/failed)"
                  << std::endl;
    }
    return jvp;
}

// Helper: State vector layout with exact offsets
// State ordering: [ru, rv, rw, ph, t, mu]
struct StateLayout {
    struct Block {
        std::string name;
        int64_t start;
        int64_t size;
    };
    std::vector<Block> blocks;
    int64_t total_size;
    bool is_exact;  // True if computed from grid dims, false if heuristic

    // Compute exact layout from grid dimensions
    // Layout: ru (ny*nz*nx_u), rv (ny_v*nz*nx), rw (ny*nz_w*nx),
    //         ph (ny*nz_w*nx), t (ny*nz*nx), mu (ny*nx)
    // All arithmetic in int64_t to prevent overflow on large domains.
    static StateLayout from_grid_dims(int nx, int ny, int nz,
                                      int nx_u, int ny_v, int nz_w) {
        StateLayout layout;
        layout.is_exact = true;

        int64_t nx64 = nx, ny64 = ny, nz64 = nz;
        int64_t nx_u64 = nx_u, ny_v64 = ny_v, nz_w64 = nz_w;

        int64_t size_u  = ny64 * nz64 * nx_u64;
        int64_t size_v  = ny_v64 * nz64 * nx64;
        int64_t size_w  = ny64 * nz_w64 * nx64;
        int64_t size_ph = ny64 * nz_w64 * nx64;  // ph on w-stagger
        int64_t size_t  = ny64 * nz64 * nx64;    // t on mass points
        int64_t size_mu = ny64 * nx64;            // mu is 2D

        layout.total_size = size_u + size_v + size_w + size_ph + size_t + size_mu;

        layout.blocks = {
            {"ru", 0, size_u},
            {"rv", size_u, size_v},
            {"rw", size_u + size_v, size_w},
            {"ph", size_u + size_v + size_w, size_ph},
            {"t",  size_u + size_v + size_w + size_ph, size_t},
            {"mu", size_u + size_v + size_w + size_ph + size_t, size_mu}
        };

        return layout;
    }

    // DEAD CODE: Never called — layout always from from_grid_dims(). Retained for reference.
    [[deprecated("Use from_grid_dims() instead")]]
    static StateLayout infer_from_size(int64_t state_size) {
        StateLayout layout;
        layout.total_size = state_size;
        layout.is_exact = false;  // Heuristic, not exact

        // Heuristic proportions for typical WRF grids
        // WARNING: Will be wrong for nested grids, varying nz, different moisture slots
        int64_t size_mu = (state_size * 3) / 1000;  // ~0.3%
        int64_t size_3d_total = state_size - size_mu;

        // Distribute 3D fields with stagger-aware proportions
        int64_t size_u  = (size_3d_total * 202) / 1000;  // ~20.2%
        int64_t size_v  = (size_3d_total * 199) / 1000;  // ~19.9%
        int64_t size_w  = (size_3d_total * 200) / 1000;  // ~20.0%
        int64_t size_ph = (size_3d_total * 200) / 1000;  // ~20.0%
        int64_t size_t  = size_3d_total - (size_u + size_v + size_w + size_ph);  // ~19.7%

        layout.blocks = {
            {"ru", 0, size_u},
            {"rv", size_u, size_v},
            {"rw", size_u + size_v, size_w},
            {"ph", size_u + size_v + size_w, size_ph},
            {"t",  size_u + size_v + size_w + size_ph, size_t},
            {"mu", size_u + size_v + size_w + size_ph + size_t, size_mu}
        };

        return layout;
    }

    // Validate layout matches actual state size
    bool is_valid() const {
        int64_t computed = 0;
        for (const auto& b : blocks) {
            computed += b.size;
        }
        return computed == total_size;
    }
};

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

    // v20.14 r50: GMRES block-scaling (left-preconditioning with D⁻¹).
    // D[block] = ||r0[block]||₂. After scaling, each block contributes exactly 1 to ||D⁻¹r0||².
    // This prevents phi/theta O(10⁴) from masking u O(1-10) in GMRES's L2 minimization.
    // GMRES now solves: min ||D⁻¹(b - AM⁻¹z)|| — same solution x, different search path.
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
        WRFNewtonKrylovSolver::GMRESResult res{
                x, true, 0, r_true_norm, error_val,
                "Initial residual already converged",
                r_true.detach().clone(), 0, false, false};
        res.termination_reason =
            WRFNewtonKrylovSolver::KrylovTerminationReason::InitialConverged;
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
            const bool aggressive_budget_stag_gate =
                (!no_early_stop &&
                 stage_id >= 2 &&
                 ru_share_hint > 0.98f &&
                 cfg_local.stage2_gmres_restart > 0 &&
                 cfg_local.stage2_max_krylov_restarts == 1);
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
                j++;
                break;  // Exit Arnoldi loop with current best solution (NOT marked converged)
            }
            
            V.push_back(w / H[j + 1][j]);
            
            // AUTOGRAD FIX: Apply Givens rotations using tensor operations
            for (int i = 0; i < j; ++i) {
                auto h_i_j = H[i][j].clone();
                auto h_ip1_j = H[i + 1][j].clone();
                H[i][j] = cs[i] * h_i_j + sn[i] * h_ip1_j;
                H[i + 1][j] = -sn[i] * h_i_j + cs[i] * h_ip1_j;
            }
            
            // Compute new Givens rotation using tensor operations
            auto h_j_tensor = H[j][j];
            auto h_jp1_tensor = H[j + 1][j];
            auto r_givens_tensor = torch::sqrt(h_j_tensor * h_j_tensor + h_jp1_tensor * h_jp1_tensor);
            // AUTOGRAD FIX: Keep r_givens as tensor to preserve gradient flow
            // DEVICE-AWARE: Use pre-created eps_safe constant on correct device
            auto r_givens_safe = torch::where(r_givens_tensor > 1e-8f, r_givens_tensor, eps_safe);

            // PERFORMANCE FIX: Keep cs/sn as tensors to avoid .item() syncs (was causing 12 syncs per iteration!)
            // DEVICE-AWARE: Use pre-created constants on correct device (no CPU-GPU sync)
            auto safe_mask = (r_givens_tensor > 1e-8f);
            cs[j] = torch::where(safe_mask, h_j_tensor / r_givens_safe, one_tensor);
            sn[j] = torch::where(safe_mask, h_jp1_tensor / r_givens_safe, zero_tensor);
            
            // Apply new Givens rotation
            H[j][j] = r_givens_tensor;
            H[j + 1][j] = 0.0f;
            
            // AUTOGRAD FIX: Apply to RHS vector using tensor operations
            auto s_j = s[j].clone();
            auto s_jp1 = s[j + 1].clone();
            s[j] = cs[j] * s_j + sn[j] * s_jp1;
            s[j + 1] = -sn[j] * s_j + cs[j] * s_jp1;

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
            if (stage_id >= 2 &&
                wrf::sdirk3::g_sdirk3_config.stage2_gmres_restart > 0) {
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
        if (breakdown_occurred && j <= 2) {
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[GMRES] Early breakdown with j=" << j
                          << ", checking if residual converged..." << std::endl;
            }
            // Return x as-is — x hasn't been updated in this restart cycle.
            // FIX 2026-01-31: Reuse r_true instead of recomputing b - A(x) (saves 1 JVP).
            // r_true was computed at cycle start and x hasn't changed (update is after j-loop).
            auto r_final = r_true;
            // FIX 2026-01-28: Apply halo zeroing consistently for error calculation
            auto r_final_inner = r_final.clone();
            zero_halo_regions(r_final_inner, halo_width, periodic_x, periodic_y);

            // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
            // v20.14 r50: Use unscaled bnorm for return value (trust-region compatibility)
            auto error_final = safe_tensor_norm(r_final_inner) / bnorm_unscaled;
            float r_norm = guarded_item<float>(safe_tensor_norm(r_final_inner));
            float error_val = guarded_item<float>(error_final);

            // CRITICAL FIX 2026-01-28: Only report success if actually converged
            bool actually_converged = (error_val < tol);

            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[GMRES] Returning solution: ||x|| = "
                          << guarded_item<float>(x.norm())
                          << ", ||r_true|| = " << r_norm
                          << ", error = " << error_val
                          << ", converged = " << (actually_converged ? "YES" : "NO") << std::endl;
            }
            // v20.14r37: Include current restart's j in the total count.
            // total_arnoldi_iters only accumulates at end of restart (line ~1065),
            // so early return here would under-count by j Arnoldi vectors.
            WRFNewtonKrylovSolver::GMRESResult res{
                    x, actually_converged, total_arnoldi_iters + j, r_norm,
                    error_val,
                    actually_converged ? "Early breakdown, already converged"
                                       : "Early breakdown, NOT converged",
                    r_final.detach().clone(), iter, true, false};
            res.termination_reason = KTR::HappyBreakdown;
            return res;
        }

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
    KrylovBasisCapture* basis_capture) {
    
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

    // v20.14 r50: GMRES block-scaling (left-preconditioning with D⁻¹).
    // D[block] = ||r0[block]||₂. After scaling, each block contributes exactly 1 to ||D⁻¹r0||².
    // This prevents phi/theta O(10⁴) from masking u O(1-10) in GMRES's L2 minimization.
    // GMRES now solves: min ||D⁻¹(b - AM⁻¹z)|| — same solution x, different search path.
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
        WRFNewtonKrylovSolver::GMRESResult res{
                x, true, 0, r_true_norm, error_val,
                "Initial residual already converged",
                r_true.detach().clone(), 0, false, false};
        res.termination_reason =
            WRFNewtonKrylovSolver::KrylovTerminationReason::InitialConverged;
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
            const bool aggressive_budget_stag_gate =
                (!no_early_stop &&
                 stage_id >= 2 &&
                 ru_share_hint > 0.98f &&
                 cfg_local.stage2_gmres_restart > 0 &&
                 cfg_local.stage2_max_krylov_restarts == 1);
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
                j++;
                break;  // Exit Arnoldi loop with current best solution (NOT marked converged)
            }
            
            V.push_back(w / H[j + 1][j]);
        if (basis_capture) capture_basis_vector(basis_capture->V, V.back());
            
            // AUTOGRAD FIX: Apply Givens rotations using tensor operations
            for (int i = 0; i < j; ++i) {
                auto h_i_j = H[i][j].clone();
                auto h_ip1_j = H[i + 1][j].clone();
                H[i][j] = cs[i] * h_i_j + sn[i] * h_ip1_j;
                H[i + 1][j] = -sn[i] * h_i_j + cs[i] * h_ip1_j;
            }
            
            // Compute new Givens rotation using tensor operations
            auto h_j_tensor = H[j][j];
            auto h_jp1_tensor = H[j + 1][j];
            auto r_givens_tensor = torch::sqrt(h_j_tensor * h_j_tensor + h_jp1_tensor * h_jp1_tensor);
            // AUTOGRAD FIX: Keep r_givens as tensor to preserve gradient flow
            // DEVICE-AWARE: Use pre-created eps_safe constant on correct device
            auto r_givens_safe = torch::where(r_givens_tensor > 1e-8f, r_givens_tensor, eps_safe);

            // PERFORMANCE FIX: Keep cs/sn as tensors to avoid .item() syncs (was causing 12 syncs per iteration!)
            // DEVICE-AWARE: Use pre-created constants on correct device (no CPU-GPU sync)
            auto safe_mask = (r_givens_tensor > 1e-8f);
            cs[j] = torch::where(safe_mask, h_j_tensor / r_givens_safe, one_tensor);
            sn[j] = torch::where(safe_mask, h_jp1_tensor / r_givens_safe, zero_tensor);
            
            // Apply new Givens rotation
            H[j][j] = r_givens_tensor;
            H[j + 1][j] = 0.0f;
            
            // AUTOGRAD FIX: Apply to RHS vector using tensor operations
            auto s_j = s[j].clone();
            auto s_jp1 = s[j + 1].clone();
            s[j] = cs[j] * s_j + sn[j] * s_jp1;
            s[j + 1] = -sn[j] * s_j + cs[j] * s_jp1;

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
            if (stage_id >= 2 &&
                wrf::sdirk3::g_sdirk3_config.stage2_gmres_restart > 0) {
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
        if (breakdown_occurred && j <= 2) {
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[GMRES] Early breakdown with j=" << j
                          << ", checking if residual converged..." << std::endl;
            }
            // Return x as-is — x hasn't been updated in this restart cycle.
            // FIX 2026-01-31: Reuse r_true instead of recomputing b - A(x) (saves 1 JVP).
            // r_true was computed at cycle start and x hasn't changed (update is after j-loop).
            auto r_final = r_true;
            // FIX 2026-01-28: Apply halo zeroing consistently for error calculation
            auto r_final_inner = r_final.clone();
            zero_halo_regions(r_final_inner, halo_width, periodic_x, periodic_y);

            // FWD-AD FIX 2026-01-28: Use safe_tensor_norm() for forward-mode AD compatibility
            // v20.14 r50: Use unscaled bnorm for return value (trust-region compatibility)
            auto error_final = safe_tensor_norm(r_final_inner) / bnorm_unscaled;
            float r_norm = guarded_item<float>(safe_tensor_norm(r_final_inner));
            float error_val = guarded_item<float>(error_final);

            // CRITICAL FIX 2026-01-28: Only report success if actually converged
            bool actually_converged = (error_val < tol);

            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1) {
                std::cerr << "[GMRES] Returning solution: ||x|| = "
                          << guarded_item<float>(x.norm())
                          << ", ||r_true|| = " << r_norm
                          << ", error = " << error_val
                          << ", converged = " << (actually_converged ? "YES" : "NO") << std::endl;
            }
            // v20.14r37: Include current restart's j in the total count.
            // total_arnoldi_iters only accumulates at end of restart (line ~1065),
            // so early return here would under-count by j Arnoldi vectors.
            WRFNewtonKrylovSolver::GMRESResult res{
                    x, actually_converged, total_arnoldi_iters + j, r_norm,
                    error_val,
                    actually_converged ? "Early breakdown, already converged"
                                       : "Early breakdown, NOT converged",
                    r_final.detach().clone(), iter, true, false};
            res.termination_reason = KTR::HappyBreakdown;
            return res;
        }

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
    // v20.14r27f: CURRENTLY UNUSED — build_halo_mask() is never called.
    // halo_mask_initialized_ stays false, so all mask branches are no-ops.
    // Reason: halo masking degraded single-tile accuracy (rel_error 0.5940 → 0.7984).
    // Retained for potential multi-tile production use with enable_stage_halo_exchange.
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
    // 1D packed tensors: mul by halo_mask_ IF mask was built (currently never — see build_halo_mask).
    // When halo_mask_initialized_=false (current default), 1D path is a no-op.
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
                    eta_k = std::max(ew_eta_min_stage, std::min(ew_eta_max_stage, eta_k));

                    krylov_tol_adaptive = eta_k;
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
                        krylov_tol_adaptive = std::max(static_cast<float>(options_.krylov_tol), ew_eta_initial);
                        krylov_tol_adaptive = std::max(ew_eta_min_stage, std::min(ew_eta_max_stage, krylov_tol_adaptive));
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

                // STATS FIX: Extract final residual accurately
                if (need_scalar) {
                    stats_.final_residual = res_norm_for_stats;
                } else {
                    // Fast-path: single .item() on exit only (debug_level=0 without adaptive)
                    // GRADIENT FIX: Use guarded_item instead of manual NoGradGuard
                    stats_.final_residual = guarded_item<float>(res_norm_detached);
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
                                    float U_blk_norm = u_blk_norm_cpu[static_cast<long>(bi)].item<float>();
                                    float v_blk_norm = v_blk_norm_cpu[static_cast<long>(bi)].item<float>();
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
                            float blk_norm = v_blk_norm_cpu[static_cast<long>(bi)].item<float>();
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

                float v_norm_val = bench_v.norm().to(torch::kCPU).item<float>();
                if (!(v_norm_val > 1e-20f)) {
                    bench_v = torch::ones_like(K).detach();
                    v_norm_val = bench_v.norm().to(torch::kCPU).item<float>();
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
            if (stage >= 2) {
                auto& cfg = wrf::sdirk3::g_sdirk3_config;
                if (cfg.stage2_gmres_restart > 0) effective_restart = cfg.stage2_gmres_restart;
                if (cfg.stage2_max_krylov_restarts > 0) effective_max_restarts = cfg.stage2_max_krylov_restarts;
                // v20.14 r49: stage2_krylov_tol is defined as a true override in config.h
                if (cfg.stage2_krylov_tol > 0.0f) {
                    krylov_tol_adaptive = cfg.stage2_krylov_tol;
                    krylov_tol_stage_override = true;
                }
                const bool stage_budget_active =
                    (cfg.stage2_gmres_restart > 0 ||
                     cfg.stage2_max_krylov_restarts > 0 ||
                     cfg.stage2_krylov_tol > 0.0f ||
                     cfg.stage3_gmres_restart > 0 ||
                     cfg.stage3_max_krylov_restarts > 0 ||
                     cfg.stage3_krylov_tol > 0.0f);
                stage_budget_active_this_iter = stage_budget_active;
                // Couple EW forcing and Krylov budget only when stage-aware budget is explicitly active.
                // This keeps baseline runs regression-neutral.
                if (stage_budget_active && ew_eta_enabled_this_iter && !krylov_tol_stage_override) {
                    float eta_for_budget = ew_eta_used_this_iter;
                    if (!(eta_for_budget > 0.0f) || !std::isfinite(eta_for_budget)) {
                        eta_for_budget = krylov_tol_adaptive;
                    }
                    if (std::isfinite(eta_for_budget) && eta_for_budget > 0.0f) {
                        eta_for_budget = std::clamp(eta_for_budget, 1.0e-3f, 1.0f);
                        float budget_scale = 1.0f;
                        if (eta_for_budget <= 0.08f) {
                            budget_scale = 1.25f;  // tighter forcing: allow more Krylov work
                        } else if (eta_for_budget <= 0.15f) {
                            budget_scale = 1.10f;
                        } else if (eta_for_budget >= 0.40f) {
                            budget_scale = 0.75f;  // loose forcing: avoid over-solving
                        } else if (eta_for_budget >= 0.30f) {
                            budget_scale = 0.85f;
                        }
                        int restart_before = effective_restart;
                        int maxr_before = effective_max_restarts;
                        effective_restart = std::max(
                            2, static_cast<int>(effective_restart * budget_scale + 0.5f));
                        if (budget_scale > 1.0f) {
                            const float maxr_scale = std::min(budget_scale, 1.20f);
                            effective_max_restarts = std::max(
                                1, static_cast<int>(effective_max_restarts * maxr_scale + 0.5f));
                        } else if (budget_scale < 1.0f) {
                            const float maxr_scale = std::max(budget_scale, 0.70f);
                            effective_max_restarts = std::max(
                                1, static_cast<int>(effective_max_restarts * maxr_scale + 0.5f));
                        }
                        stage_budget_forcing_eta = eta_for_budget;
                        stage_budget_scale = budget_scale;
                        stage_budget_forcing_coupled =
                            (effective_restart != restart_before ||
                             effective_max_restarts != maxr_before);
                        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 1 &&
                            newton_iter == 0 && stage_budget_forcing_coupled) {
                            std::cerr << "[GMRES COUPLING] Stage " << stage
                                      << ": eta=" << eta_for_budget
                                      << ", scale=" << budget_scale
                                      << ", restart=" << restart_before
                                      << "->" << effective_restart
                                      << ", max_restarts=" << maxr_before
                                      << "->" << effective_max_restarts
                                      << std::endl;
                        }
                    }
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
                    if (cfg.stage3_gmres_restart > 0) effective_restart = cfg.stage3_gmres_restart;
                    if (cfg.stage3_max_krylov_restarts > 0) effective_max_restarts = cfg.stage3_max_krylov_restarts;
                    if (cfg.stage3_krylov_tol > 0.0f) {
                        krylov_tol_adaptive = cfg.stage3_krylov_tol;
                        krylov_tol_stage_override = true;
                    }

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
                          jvp_check_this_iter ? &jvp_check_basis : nullptr)
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
                gmres_rel_error = std::clamp(gmres_raw_rel_error, 0.0f, 1.0f);
                stats_.total_krylov_iterations += gmres_result.iterations;

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

                // Return failure result
                WRFNewtonKrylovSolver::NewtonResult result;
                result.K = K;
                result.converged = false;
                result.iterations = options_.max_newton_iter;
                result.final_residual = stats_.final_residual;
                result.message = std::string("GMRES exception: ") + e.what();
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
                                layout = StateLayout::infer_from_size(U_eval.numel());
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
                                layout = StateLayout::infer_from_size(U_eval.numel());
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
                                layout = StateLayout::infer_from_size(v_r.numel());
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
            const bool gmres_total_failure_candidate =
                (gmres_raw_rel_error > 1.0f || gmres_rel_error >= 0.999f);
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
                        break;
                    }
                    // v20.14r36: Configurable zero-step stagnation limit (was hardcoded 3).
                    const int stall_limit = wrf::sdirk3::g_sdirk3_config.newton_zero_step_stall_limit;
                    if (stagnation_count >= stall_limit) {
                        std::cerr << "[Newton] STAGNATION: " << stagnation_count
                                  << " consecutive zero-update iterations (limit=" << stall_limit
                                  << "). Trust-region cannot find descent direction. "
                                  << "Breaking at iter=" << newton_iter << std::endl;
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
                        std::cerr << "[Newton] RESIDUAL STALL: iter " << newton_iter
                                  << ", rel_decrease=" << rel_decrease
                                  << " < " << stall_threshold
                                  << ", gmres_rel_error=" << gmres_rel_error
                                  << ". Exiting early (prev_res=" << prev_iter_res_norm
                                  << ", cur_res=" << res_norm_for_stats << ")." << std::endl;
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
        } else if (res_norm_detached.defined()) {
            // Fast-path: single .item() on failure (debug_level=0 without adaptive)
            torch::NoGradGuard no_grad;
            // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
            stats_.final_residual = res_norm_detached.to(torch::kCPU).item<float>();
        } else {
            // Should never reach here, but fallback to tracked value
            stats_.final_residual = last_res_norm;
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
        stats_.newton_iterations = 0;
        stats_.total_krylov_iterations = 0;
        stats_.newton_residuals.clear();
        stats_.final_residual = 0.0f;
        stats_.initial_unscaled_residual = 0.0f;
        stats_.initial_residual_vector = torch::Tensor();
        stats_.condition_number = 0.0f;
        stats_.converged = false;

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
