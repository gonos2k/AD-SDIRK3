#ifndef JVP_AUTOGRAD_H
#define JVP_AUTOGRAD_H

#include <torch/torch.h>
#include <functional>
#include <atomic>  // OPT Pass33+: For diagnostic sampling counter

namespace wrf {
namespace sdirk3 {

/**
 * JVP Method Selection Heuristic (2025-01-25)
 * ============================================
 *
 * Three JVP methods available:
 *   1. Reverse-mode AD (autograd): Accurate, high memory, best for batch_size > 1
 *   2. Forward-mode AD (dual): Accurate, low memory, best for batch_size == 1
 *   3. Finite difference (FD): Fast but O(eps) error, use for debugging/prototyping
 *
 * Selection criteria:
 *
 *   batch_size == 1:
 *     - Forward-AD (dual) is optimal: O(n) memory, O(F) compute
 *     - Reverse-AD: O(n) memory for graph + O(n) for gradients, O(F + backward) compute
 *     - Recommendation: Use compute_jvp_dual() or FD for single JVPs
 *
 *   batch_size > 1:
 *     - Reverse-AD with graph reuse: Build graph once, reuse for all batch elements
 *     - Compute: O(F) + batch_size × O(backward)
 *     - Memory: O(graph) + O(n) per JVP result
 *     - Recommendation: Use compute_jvp_batch_optimized() for GMRES Krylov vectors
 *
 *   batch_size > 16:
 *     - Chunked reverse-AD: Process in chunks of 16 to bound peak memory
 *     - Trade-off: More F evaluations vs bounded memory
 *     - Recommendation: compute_jvp_batch_optimized() handles this automatically
 *
 * FD epsilon sensitivity:
 *   - FP32 optimal eps: ~1e-4 (smaller causes cancellation error)
 *   - FP64 optimal eps: ~1e-7
 *   - See tests/test_jvp_fixes_validation.cpp for detailed analysis
 *
 * GPU Device Handling:
 *   - All JVP functions inherit device from input tensors via .options()/_like()
 *   - No explicit CUDAGuard needed - tensors stay on input device automatically
 *   - Multi-GPU: Ensure all inputs (u, v) are on the same device before calling
 *   - For multi-GPU scenarios with tensors on different devices, caller must
 *     synchronize devices before JVP call (e.g., v.to(u.device()))
 *
 * Multi-GPU Device Guard Checklist:
 *   Before calling any JVP function, verify:
 *   □ u.device() == v.device()           // Input tensors on same device
 *   □ F() creates tensors on u's device  // F uses u.options() or _like(u)
 *   □ F() doesn't call torch::randn()    // Use torch::randn_like(u) instead
 *   □ F() doesn't use torch::zeros({n})  // Use torch::zeros({n}, u.options())
 *   □ No global default device changes   // Avoid set_default_device()
 *
 *   If F() uses default device internally:
 *     c10::cuda::CUDAGuard guard(u.device());  // Force device context
 *     auto jvp = compute_jvp_finite_diff(F, u, v);   // true JVP (J*v)
 *     // (compute_vjp_autograd returns J^T v — reverse mode, NOT a JVP)
 *
 *   Device mismatch symptoms:
 *   - "Expected all tensors to be on the same device"
 *   - Silent wrong results (tensors on different devices don't interact)
 *
 * In-Place Operation Risks (DetectAnomalyGuard):
 *   When detect_autograd_anomaly=true in config, DetectAnomalyGuard catches:
 *   - In-place modifications to tensors that require grad (breaks gradient chain)
 *   - NaN/Inf values in backward pass
 *   - Double backward through non-retained graphs
 *
 *   Common in-place patterns to AVOID inside F():
 *     ❌ tensor[indices] = value        → use index_put_ with clone
 *     ❌ tensor.add_(other)             → use tensor = tensor + other
 *     ❌ tensor.mul_(scalar)            → use tensor = tensor * scalar
 *     ❌ tensor.zero_()                 → use tensor = torch::zeros_like(tensor)
 *     ❌ tensor.fill_(value)            → use tensor = torch::full_like(tensor, value)
 *     ❌ tensor.copy_(source)           → use tensor = source.clone()
 *     ❌ out.narrow(...).copy_(...)     → build output from pieces instead
 *
 *   Safe alternatives:
 *     ✅ out = torch::where(cond, new_val, old_val)  // conditional update
 *     ✅ out = torch::cat({slice1, slice2}, dim)     // build from slices
 *     ✅ out = input.clone(); NoGradGuard ng; out[i] = v; // if grad not needed
 *
 *   To debug, enable: g_sdirk3_config.detect_autograd_anomaly = true
 *   WARNING: ~10x slower - use only during development
 *
 * RNG Handling for JVP/FD Comparison:
 *   If F() contains random operations (dropout, randn, etc.), JVP vs FD comparison
 *   will be unstable because each F() evaluation sees different random values.
 *   NOTE (Codex round-5): restoring RNG state BETWEEN the two helper calls is
 *   NOT sufficient — both helpers are FD-based and evaluate F MULTIPLE times
 *   within a single call (F(u + eps*v) and F(u)); those inner evaluations
 *   would each draw fresh random numbers, so the difference quotient already
 *   compares two different random functions regardless of the outer state.
 *   The randomness must be pinned INSIDE F, per evaluation:
 *   Solutions:
 *     1. Pin the RNG inside F so EVERY evaluation replays the same draws
 *        (within each helper call and across helpers), AND restore the global
 *        generator afterwards so the wrapper does not clobber the caller's
 *        random stream (a bare manual_seed inside F would leave the process
 *        RNG permanently reseeded — Codex round-6). The libtorch C++ API has
 *        no torch::get_rng_state/set_rng_state (Python names; an earlier
 *        revision used them and did not compile) — use the generator handle:
 *        auto F_fixed = [&F](const torch::Tensor& x) {
 *            // at::Generator is a shared handle; the copy refers to the SAME
 *            // global CPU generator but is non-const (set_state needs that).
 *            auto gen = at::globalContext().defaultGenerator(at::kCPU);
 *            // RAII: restore the caller's stream on BOTH normal and exception
 *            // paths — a trailing set_state after F(x) is skipped if F throws,
 *            // leaving the process RNG reseeded (Codex round-7).
 *            struct RngRestore {
 *                at::Generator gen;
 *                at::Tensor saved;
 *                ~RngRestore() { gen.set_state(saved); }
 *            } guard{gen, gen.get_state()};
 *            torch::manual_seed(0);          // identical draws per evaluation
 *            return F(x);                    // guard restores on return OR throw
 *        };
 *        CONCURRENCY: this pattern manipulates the GLOBAL generator and is
 *        single-threaded-validation ONLY. If F uses global-RNG ops
 *        (rand_like etc.), no outer wrapper can make concurrent JVP/FD
 *        comparison safe — threads race on the shared generator. For
 *        concurrent use, F itself must draw from an explicit per-thread
 *        at::Generator passed to the random ops.
 *        auto jvp1 = compute_jvp_dual(F_fixed, u, v);          // true JVP
 *        auto jvp2 = compute_jvp_finite_diff(F_fixed, u, v);   // same fixed noise
 *     2. Use deterministic mode for testing (fixes ALGORITHM nondeterminism,
 *        e.g. atomics ordering — it does NOT freeze random OPS like dropout;
 *        combine with 1 or 3 when F draws random numbers):
 *        at::globalContext().setDeterministicAlgorithms(true, false);
 *     3. Disable randomness in F during validation (e.g., dropout.eval())
 *
 * Autocast/AMP Consistency:
 *   If F() uses torch::autocast internally, the JVP path should match:
 *   - Autocast scope must wrap both F() evaluation and grad() call
 *   - Mismatch causes precision differences (FP16 vs FP32 intermediate results)
 *   - Use scoped autocast around entire JVP computation:
 *       torch::autocast::set_autocast_enabled(torch::kCUDA, true);
 *       auto jvp = compute_jvp_dual(F, u, v);  // true JVP; F sees autocast
 *       torch::autocast::set_autocast_enabled(torch::kCUDA, false);
 *   - Or disable autocast for JVP validation (recommended for accuracy testing)
 *   - v_batch dtype normalization: In autocast scope, v_batch may have different
 *     dtype than x_var (FP16 vs FP32). Normalize v_batch to x.dtype() before JVP:
 *       auto v_normalized = v.to(x.dtype());  // Avoid implicit casting overhead
 *
 * POLICY: Autocast v_batch Normalization
 *   In AMP/autocast contexts, grad_outputs (v_batch) must match output dtype:
 *
 *   Scenario: F(x_var) produces FP16 output (y) under autocast, but v_batch is FP32
 *   Problem: Implicit dtype mismatch → silent precision loss or unexpected behavior
 *
 *   Resolution strategies:
 *   1. Normalize at JVP call site (recommended):
 *      auto v_normalized = v_batch.to(y.dtype());
 *      auto jvp = autograd::grad({y}, {x_var}, {v_normalized}, ...);
 *
 *   2. Ensure v_batch creation respects autocast:
 *      c10::cuda::amp::autocast_mode::is_enabled() → create v in autocast dtype
 *
 *   3. Disable autocast for JVP validation (for accuracy testing):
 *      torch::NoGradGuard to disable autocast temporarily
 *
 *   Implementation in compute_jvp_batch_optimized:
 *   - v_batch dtype/device validated with warnings (not errors for flexibility)
 *   - Explicit .to(x.dtype()) normalization before grad() call
 *   - Test 12 (AMP Consistency) verifies behavior under autocast
 *
 *   Relative tolerance for AMP JVP comparison: ~1e-3 (due to FP16 precision)
 *   If tighter tolerance needed, disable autocast for the JVP computation.
 *
 * F() Purity Requirements for Graph Reuse:
 *   JVPContext and batch JVP assume F is a PURE FUNCTION:
 *   ✅ F depends only on input tensor values
 *   ✅ F produces deterministic output for same input
 *   ❌ F must NOT use global state (counters, caches modified between calls)
 *   ❌ F must NOT use random ops (randn, dropout) without seed control
 *   ❌ F must NOT use time-dependent ops (timers, wall clock)
 *   ❌ F must NOT modify external variables (side effects)
 *   Violation causes: cached graph reuse produces wrong gradients silently.
 *   Detection: Enable detect_autograd_anomaly + compare JVP with FD for sanity.
 *
 * CRITICAL: F() Must Not Use .detach() or .data on Input Path
 *   If F() applies .detach() or .data to x or any intermediate derived from x,
 *   the computation graph is severed and JVP becomes zero:
 *
 *   ❌ WRONG - Graph collapse:
 *      auto F_wrong = [](const torch::Tensor& x) {
 *          auto y = x.detach() * 2;  // severs graph!
 *          return y.sin();           // JVP will be zero
 *      };
 *
 *   ✅ CORRECT - Graph preserved:
 *      auto F_correct = [](const torch::Tensor& x) {
 *          auto y = x * 2;           // graph intact
 *          return y.sin();           // JVP computed correctly
 *      };
 *
 *   Common hidden detach() sources:
 *   - Tensor operations inside NoGradGuard/InferenceMode scope
 *   - Cached tensors that were detached before caching
 *   - .item() calls (implicit detach + scalar conversion)
 *   - In-place operations on views may corrupt graph
 *
 *   Detection:
 *   1. Check y.requires_grad() after F(x_var) - should be true if x_var has grad
 *   2. Run graph collapse test (Test 11) which catches F → JVP=0 pattern
 *   3. Grep for .detach()/.data/.item() in F's implementation path
 *
 * Thread Safety (Concurrency):
 *   JVP functions are NOT thread-safe for shared state:
 *   ❌ Do NOT share x_var/y tensors across threads
 *   ❌ Do NOT share JVPContext across threads
 *   ❌ Do NOT call compute_jvp on same cached graph from multiple threads
 *   ✅ Each thread should have its own JVPContext instance
 *   ✅ Each thread should build its own graph (x_var, y = F(x_var))
 *   Rationale: autograd::grad modifies graph state (retain_graph side effects).
 *
 * allow_unused=true and Zero Gradient Semantics:
 *   When F(u) doesn't depend on some input elements, autograd::grad returns
 *   undefined tensor. We replace undefined → zeros_like(v).
 *   Mathematical justification: ∂F/∂u_i = 0 for unused input u_i.
 *   This is correct because: J*v = Σ (∂F/∂u_i) * v_i, and if ∂F/∂u_i = 0,
 *   that term contributes nothing to the sum.
 *   zeros_like(v) guarantees: same dtype, device, layout, contiguity as v.
 *
 * POLICY: Fallback Zero Tensors Must Be requires_grad=false
 *   When returning zero tensors for undefined gradients or graph collapse:
 *   ✅ Use: torch::zeros(v.sizes(), v.options().requires_grad(false))
 *   ❌ Avoid: torch::zeros_like(v)  // inherits requires_grad from v
 *
 *   Rationale:
 *   - zeros_like(v) inherits requires_grad from v
 *   - If v.requires_grad()=true, the zero tensor becomes a graph node
 *   - This causes "graph pollution" - unnecessary graph nodes accumulate
 *   - Over many JVP calls, this leads to silent memory growth
 *
 *   This policy applies to all fallback paths:
 *   - allow_unused=true undefined gradient handling
 *   - Graph collapse detection (y.requires_grad()=false)
 *   - Exception recovery paths
 *   - Any path returning semantically-zero JVP results
 *
 * Chunk Size Heuristic (batch_jvp_chunk_size):
 *   Default: 16 (good for 8-32GB GPU memory with typical WRF tensor sizes)
 *   Adjustment guidelines:
 *   - Large tensors (>100M elements): Use smaller chunks (8-12)
 *   - Small tensors (<1M elements): Use larger chunks (32-64)
 *   - Memory-constrained: Reduce to 4-8
 *   - Memory-abundant (>64GB): Increase to 32-64
 *   Formula approximation: chunk_size ≈ available_GB * 2 / tensor_GB
 *
 * Non-Contiguous Input Cost:
 *   contiguous() creates a copy if tensor is non-contiguous.
 *   Cost: O(n) memory + O(n) copy time.
 *   Optimization for repeated inputs:
 *   - If same v_batch is used multiple times, pre-normalize once and cache
 *   - GMRES Arnoldi vectors are typically already contiguous (column-built)
 *   - Transposed/sliced tensors from external sources may be non-contiguous
 *
 * Device Verification Checklist (Multi-GPU):
 *   Before any JVP call, verify:
 *   □ x.device() == v.device() (or all v_batch[i].device())
 *   □ F() creates intermediate tensors on x.device()
 *   □ No default device assumptions in F()
 *   □ JVPContext was prepared with same device as current call
 *   Enforcement: compute_jvp_batch_optimized has runtime device checks.
 *   Multi-GPU test: Include 2+ GPU smoke test in CI if hardware available.
 *
 * POLICY: Device Guard for Multi-GPU JVP
 *   If F() internally uses default device (e.g., torch::randn({n})):
 *   - Wrap JVP call with explicit device guard:
 *       c10::cuda::CUDAGuard guard(x.device());  // CUDA
 *       // or: c10::mps::MPSGuard guard(x.device());  // MPS
 *   - Better: F() should use x.options() for all tensor creation
 *   - Symptoms of device mismatch:
 *       "Expected all tensors to be on the same device"
 *       Silent wrong results (cross-device operations)
 *   - Current implementation: Runtime device checks in batch JVP paths
 *
 * Exception Safety (Graph Release):
 *   If exception occurs mid-batch JVP loop:
 *   - C++ RAII ensures x_var/y tensors are destroyed when going out of scope
 *   - Autograd graph is released via tensor destructors
 *   - No explicit cleanup needed in normal exception propagation
 *   - Caveat: If exception is caught and loop continues, ensure retain_graph=false
 *     on final iteration to release graph properly
 *   Test coverage: test_early_termination_safety verifies graph release behavior
 *
 * Nested Autocast Boundaries:
 *   If autocast scopes are nested (e.g., F() has internal autocast):
 *   - Inner autocast overrides outer for tensors created inside
 *   - v_batch dtype may differ from x_var dtype at JVP call site
 *   - Normalize v_batch at JVP call site: v.to(x.dtype())
 *   - Or ensure consistent autocast policy across entire JVP computation
 *   Symptom of mismatch: Unexpected FP16↔FP32 casts in grad computation
 *
 * is_grads_batched Future Optimization:
 *   PyTorch 2.0+ added is_grads_batched parameter to autograd::grad().
 *   Status: Not yet used in this implementation (loop-based approach).
 *   Roadmap for adoption:
 *   □ Verify C++ API support in target PyTorch version
 *   □ Benchmark: batched vs loop for typical GMRES batch sizes (5-50)
 *   □ Test accuracy: ensure batched path matches loop results
 *   □ Memory profile: batched may have different peak memory pattern
 *   Note: Current chunked loop approach is robust and well-tested.
 *
 * Chunk Size Auto-Tune Formula:
 *   Simple heuristic: chunk_size = min(64, max(8, 1e7 / numel))
 *   - For 1M element tensor: chunk = min(64, max(8, 10)) = 10
 *   - For 100K element tensor: chunk = min(64, max(8, 100)) = 64
 *   - For 10M element tensor: chunk = min(64, max(8, 1)) = 8
 *   Currently uses fixed config value; auto-tune can be added if needed.
 *
 * CPU Fallback Paths:
 *   JVP functions do NOT have implicit CPU fallback:
 *   - All computations stay on input tensor's device
 *   - No automatic GPU→CPU transfer for unsupported ops
 *   - If F() contains CPU-only ops, entire JVP runs on CPU
 *   Checklist: □ No CPU fallback in JVP paths (confirmed N/A)
 *
 * ═══════════════════════════════════════════════════════════════════════
 * DEBUG CHECKLIST: JVP Correctness Verification
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Before deployment or after code changes, verify:
 *
 * 1. Graph Construction Check:
 *    □ y = F(x_var) produces y.requires_grad() == true
 *    □ If y.requires_grad() == false → F has graph collapse (NoGradGuard/.detach)
 *    □ Verify: y.grad_fn() is not nullptr (has computation graph)
 *
 * 2. x_var Leaf Isolation Check:
 *    □ All JVP entry points use detach().clone().requires_grad_(true) by default
 *    □ ad_strict_mode=true preserves upstream graph (for higher-order only)
 *    □ grep: "clone().requires_grad_" without "detach()" is suspicious
 *
 * 3. Zero Fallback Policy Check:
 *    □ All undefined gradient fallbacks use requires_grad(false)
 *    □ grep: "zeros_like" in JVP paths → should be "zeros(...requires_grad(false))"
 *    □ Fallback zeros preserve: dtype, device, shape (but NOT requires_grad)
 *
 * 4. v_batch/grad_outputs Validation:
 *    □ v_batch[i].device() == x.device() for all i
 *    □ v_batch[i].dtype() == x.dtype() for all i
 *    □ v_batch[i].requires_grad() == false (warning if true)
 *        - If grad_outputs has requires_grad=true and create_graph=true,
 *          autograd::grad builds higher-order graph (unnecessary for 1st-order JVP)
 *        - If grad_outputs has requires_grad=true but create_graph=false,
 *          no extra graph built but warns about potential misuse
 *        - POLICY: All v_batch tensors should have requires_grad=false
 *    □ In autocast: normalize v_batch to x.dtype() before JVP
 *
 * 5. F() Purity & Graph Integrity:
 *    □ F() has no internal .detach() or .data on input path
 *    □ F() has no NoGradGuard/InferenceMode wrapping computation
 *    □ F() has no .item() calls (implicit detach + sync)
 *    □ grep in F: "detach|\.data\(|NoGradGuard|InferenceMode|\.item\("
 *
 * 6. retain_graph Lifecycle:
 *    □ Single-shot JVP: retain_graph=false
 *    □ Batch JVP loop: retain_graph=true for all but last iteration
 *    □ Exception paths: graph released via RAII (verify with test)
 *
 * 7. AMP/Autocast Consistency:
 *    □ JVP inside autocast scope matches F() autocast scope
 *    □ v_batch dtype normalized at call site
 *    □ Smoke test: JVP(autocast=on) ≈ JVP(autocast=off) within tolerance
 *
 * Quick Grep Commands for Code Review:
 *   rg "clone\(\)\.requires_grad_" --glob "*.cpp" | grep -v detach
 *   rg "zeros_like" --glob "*jvp*.cpp"
 *   rg "\.detach\(\)|\.data\(" --glob "*rhs*.cpp" --glob "*physics*.cpp"
 *   rg "NoGradGuard|InferenceMode" --glob "*.cpp" -C2
 *   rg "autograd::Function|save_for_backward|mark_dirty" --glob "*.cpp"
 *   rg "as_strided" --glob "*.cpp"
 *   rg "from_blob" --glob "*jvp*.cpp"
 *
 * Additional Risk Patterns (verified safe as of 2025-01-25):
 *   - Custom autograd::Function: NOT USED (no save_for_backward/mark_dirty)
 *   - as_strided: NOT USED (all view/reshape are standard contiguous ops)
 *   - from_blob in JVP paths: NOT USED (isolated to Fortran bridge layer)
 *   - Nested autograd::grad: NOT USED (single-level JVP only)
 *
 * Leaf Optimization Note (performance vs safety trade-off):
 *   Current policy: Always use x.detach().clone().requires_grad_(true)
 *   Optimization opportunity (NOT IMPLEMENTED - safety first):
 *     If x.is_leaf() && !x.requires_grad() && no in-place ops in F:
 *       → x.clone().requires_grad_(true) suffices (skip detach)
 *     Rationale: detach().clone() is ~2x memory vs clone() alone
 *     Risk: In-place ops on x_var would corrupt original if not detached
 *     Decision: Keep current safe pattern unless profiling shows bottleneck
 *
 * Test Coverage Requirements:
 *   - Test 1-6: Core JVP correctness
 *   - Test 7: Deterministic reproducibility
 *   - Test 8: Early termination safety (exception path)
 *   - Test 9: RNG state consistency
 *   - Test 10: zeros_like property preservation
 *   - Test 11: Graph collapse detection
 *   - Test 12: AMP on/off consistency
 *
 * ═══════════════════════════════════════════════════════════════════════
 */

// Compute the VECTOR-JACOBIAN product (VJP), J(u)^T * v, via reverse-mode
// autograd (grad_outputs = v). RENAMED from compute_jvp_autograd (full-repo
// review P1-3): the old name and docs claimed J*v, but torch::autograd::grad
// with grad_outputs=v is mathematically J^T v — identical only for SYMMETRIC
// Jacobians, silently wrong for WRF's nonsymmetric operators. No production
// caller ever used it (the Newton/GMRES matvec uses the true forward-mode
// JVP, compute_jvp_fwad_or_fd); the rename prevents future misuse.
// halo_width: Zero perturbations in halo regions to prevent artificial gradients
torch::Tensor compute_vjp_autograd(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    int halo_width = 0
);

// Alternative JVP computation using dual numbers (finite difference)
// halo_width: Zero perturbations in halo regions to prevent artificial gradients
torch::Tensor compute_jvp_dual(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    float epsilon = 1e-6f,
    int halo_width = 0
);

// Batched JVP computation for multiple vectors
torch::Tensor compute_jvp_batch(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& V  // V has shape [batch, ...]
);

// Solve linear system (I - dt*gamma*J)*x = b using JVP
torch::Tensor solve_linear_system_jvp(
    const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
    const torch::Tensor& u_stage,
    const torch::Tensor& b,
    float dt,
    double gamma,
    int max_iter = 100,
    float tol = 1e-6f
);

// Specialized JVP computation for acoustic equations
class AcousticJVP {
public:
    AcousticJVP(float dx, float dy, float dz = 1.0f, 
                float c_s = 340.0f, float rho0 = 1.225f);
    
    // Compute JVP for acoustic RHS
    torch::Tensor operator()(const torch::Tensor& state, const torch::Tensor& direction);
    
private:
    float dx_, dy_, dz_;
    float c_s_;  // sound speed
    float rho0_; // reference density
    
    torch::Tensor compute_acoustic_rhs_grad(const torch::Tensor& state);
};

// JVP method control
// NOTE: inert for compute_vjp_autograd (always reverse-mode since the Codex
// round-2 fix); kept because sdirk3/tests exercise the setter. Production JVP
// method selection lives in the fwad router.
void set_use_finite_diff_jvp(bool use_fd);
bool get_use_finite_diff_jvp();

// Test function - only available when SDIRK3_ENABLE_DEMO_CODE is defined
#ifdef SDIRK3_ENABLE_DEMO_CODE
void test_jvp_implementation();
#endif // SDIRK3_ENABLE_DEMO_CODE

/**
 * JVPContext: Graph reuse optimization for GMRES
 *
 * Problem: Building computation graph for 1.08M variables takes ~15 sec,
 *          plus ~570 sec for backward pass = ~10 min per JVP call.
 *          With 30 GMRES iters × 50 Newton iters = ~10 days per timestep.
 *
 * Solution: Build graph ONCE per Newton iteration, reuse for all GMRES iters.
 *          Each subsequent JVP only needs backward pass through existing graph.
 *
 * Usage:
 *   JVPContext ctx;
 *   ctx.prepare(compute_rhs, Y);  // Build graph once
 *   for (GMRES iterations) {
 *       auto Jv = ctx.compute_jvp(v);  // Fast: reuse graph
 *   }
 *   ctx.release();  // Free graph memory
 */
class JVPContext {
public:
    JVPContext() = default;
    ~JVPContext() { release(); }

    // OPT Pass33+: Explicit non-copyable/non-movable
    // Reason: std::atomic diagnostic sampling counter (prepare_diag_call_counter_)
    JVPContext(const JVPContext&) = delete;
    JVPContext& operator=(const JVPContext&) = delete;
    JVPContext(JVPContext&&) = delete;
    JVPContext& operator=(JVPContext&&) = delete;

    // Prepare computation graph (call once per Newton iteration)
    // Returns true if successful, false if graph build failed
    bool prepare(
        const std::function<torch::Tensor(const torch::Tensor&)>& F,
        const torch::Tensor& u,
        int halo_width = 0);

    // Compute JVP using prepared graph (call for each GMRES direction vector)
    // Much faster than compute_vjp_autograd as graph is already built
    // retain_graph_for_next: true if more JVP calls expected
    torch::Tensor compute_jvp(const torch::Tensor& v, bool retain_graph_for_next = true);

    // Release graph memory (call after GMRES completes)
    void release();

    // Check if context is prepared
    bool is_prepared() const { return is_prepared_; }

    // Get the last F(u) evaluation (useful for debugging)
    const torch::Tensor& get_F_u() const { return F_u_; }

private:
    torch::Tensor u_var_;      // Input with requires_grad=true
    torch::Tensor F_u_;        // F(u_var_), the computed output
    int halo_width_ = 0;
    bool is_prepared_ = false;

    // OPT Pass33+: Diagnostic sampling counter for prepare() verbose output
    // Reduces D2H overhead when debug_level >= 1
    // Pattern: (period == 0) || ((counter % period) == 0) || (counter == 1)
    mutable std::atomic<uint64_t> prepare_diag_call_counter_{0};
};

// Fast finite difference JVP (no autograd overhead)
torch::Tensor compute_jvp_finite_diff(
    const std::function<torch::Tensor(const torch::Tensor&)>& F,
    const torch::Tensor& u,
    const torch::Tensor& v,
    float epsilon = 1e-5f,
    int halo_width = 0);

} // namespace sdirk3
} // namespace wrf

#endif // JVP_AUTOGRAD_H