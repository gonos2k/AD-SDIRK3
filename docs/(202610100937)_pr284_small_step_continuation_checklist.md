# PR284 small-step inverse continuation checklist

Local timestamp: 2026-10-10 09:37:02 JST (Asia/Tokyo)

## Context

Base: merged PR283 main `0fcde78bacf7ebef85cad49a3d85196e87ffe0c8`. The original user worktree remains untouched; work is isolated in `/private/tmp/pr284-small-step-20261010`.

PR283 closed predictor replay parity and demonstrated two strict Armijo decreases at h=0.3125 s: 62.048869664 → 49.817931614 → 38.991858312. Preserve those results. The last accepted point has no gradient, and convergence, full gradient error Eg, analysis recovery and unused forecast remain open.

## Checklist

- [x] Verify PR283 merged revision and isolate current main.
- [x] Verify cached Graphify source bindings and inspect affected callers before edits (17 hashes exact, cached graph 634 nodes/17,625 links; nested closure extraction gap recorded).
- [ ] Resume the exact accepted delta2 from the hash-pinned PR283 report and native endpoint; retain literal base, Bcan, observations, sigma, h, Newton/Krylov/EW settings.
- [ ] Apply the existing positive-curvature inverse-BFGS formula to current secants; retain direction cap 0.05 and strict actual-cost Armijo.
- [ ] Calculate delta2 gradient on the current replay path and continue accepted updates without repeating prior updates.
- [ ] Calculate and record the terminal gradient; distinguish bounded progress, numerical stop and convergence evidence.
- [ ] Independently recompute accepted costs, directions, secants, per-time residuals and replay receipts; Green and Red review.
- [ ] Run only the existing minimum research CI: five selected CTests, short replay, metadata consumer and syntax checks. No deployment or full-suite expansion.
- [ ] Near convergence, assess gradient uncertainty and final-analysis time sensitivity using the same problem.
- [ ] Evaluate unused-time forecast from the accepted analysis once the optimization basis is established.

## Validation scope

No new production model run or RK3 comparison has been performed in this continuation yet. If changes remain test-driver-only, reuse PR283's explicitly qualified same-setup WRF/RK3 regression without claiming a new run. Source/executable revisions and input hashes must accompany every new numerical result. Raw gradient norm alone does not certify full Eg or meteorological analysis accuracy.
