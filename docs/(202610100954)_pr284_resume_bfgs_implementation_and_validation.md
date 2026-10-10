# PR284 resume, secant and terminal-gradient implementation

Local timestamp: 2026-10-10 09:54:19 JST (Asia/Tokyo)

## Changes

Continue the existing four-control chart from the accepted PR283 delta2, retaining the literal base and Bcan, fixed observations/R, 960 steps at 0.3125 s, N14/K12 and enabled EW. The explicit predecessor ZIP SHA and archived state/CSV/source/executable receipts are verified before a new native call. Already measured secants initialize the inverse metric; the pending delta1→delta2 secant waits for a fresh delta2 pullback.

The inverse-BFGS expression previously embedded in the fine inverse now has one owning helper, preserving its safe-case arithmetic with finite, symmetry, positive-curvature and SPD checks. Both original and resumed drivers use the same native invocation/receipt/validation helper. Direction cap remains 0.05; only strict actual-cost Armijo steps are accepted. Each newly accepted point receives a gradient, including the terminal point. Exact predictions/checkpoints/brackets are compared between trial forward and subsequent pullback call. Calls are journaled before launch; final accepted state is recorded before its gradient starts.

The additional manual `small-step-continue` choice downloads the explicit accepted artifact. The automatic fast-contracts/core-linux/required jobs remain byte-identical to the merged PR283 workflow. Deployment, full-suite and duplicate builds are excluded.

## Offline validation

See `docs/evidence/20261010/pr284_offline_checks.json`. The pinned predecessor ZIP and four stored CSVs agree with their reports and fixed inputs. Seven native source fingerprints match PR283. The exact-quadratic BFGS check has secant residual 7.85e-17 and SPD output. Python syntax, CLI help, undefined-name lint, whitespace checks and actionlint pass. Red review caught a stale attempt identifier before any long native execution; it was corrected, and a controlled mocked two-update flow now exercises both accepted steps, terminal-gradient reuse and final receipt. This flow checks controller behavior, not physical-model correctness.

Offline artifact validation substituted the expected executable hash only for a non-executed placeholder. It is not proof of Linux binary identity and is not a native run. The actual Linux preflight, short replay and long continuation remain pending. Red review additionally enforced the exact PR283 schema ZIP pin and fixed input-file/report hashes, matched observed values to the pinned input, and selected the linked terminal gradient or accepted trial as final-cost authority. Attempt indices and accepted-update counts are separate, allowing a bounded run with a later rejected trial to resume from its accepted endpoint.

## Model regression scope

No new WRF run or RK3 comparison was performed. Production C++/Fortran and the native probe are unchanged. Reuse PR283's qualified fresh-archive em_b_wave run (48 stages, finite/positive state and byte-identical PR282 outputs) and its same archived RK3 comparison; do not count that as a new PR284 model run.

## Remaining

Run actual continuation and minimum CI; independently recompute the new secants/costs/terminal gradient; obtain Green and Red final review. Full gradient uncertainty Eg, time-robust converged analysis, truth recovery and unused-time forecast remain separate open accuracy conditions.
