# Independent DQ2 sensitivity focus: progress status

Local timestamp: 2026-10-06T21:28:00+09:00.

The selected six-second DQ2 sensitivity check is archived at `/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261006/weakly_nonlinear_wave/independent_dq2_focus/`. Its immutable report and raw CSV/TXT payloads are accompanied by `focus_manifest.json`, the frozen source receipt, and the independent solver-tolerance receipt.

For the selected m=2-W terminal objective and control direction, source-polarized `DQ2` predicts `-1.4848316632865e-8`; the native amplitude/timestep Richardson sensitivity is `-1.4848241737879e-8`. Their absolute difference is `7.4895e-14`, within the `5.4904e-13` accepted budget. The normalized native VJP also agrees with centered finite differences at `9.9541e-5` relative error against a `2e-3` budget. This closes only that selected six-second sensitivity focus; it establishes no global Hessian bound.

The complete attempt-4 quadratic CTest failed at the 900-second mean-W endpoint: residual `2.69027e-7` exceeded the `2.02224e-7` budget. Its 30-second checks and `Native_Stable_Wave_Refinement` passed, but the full weakly nonlinear gate remains open. Preserve this failure and the focused sensitivity success as separate evidence.

Exact candidate source, executable, and library identities are bound in `attempt4_source_binding_pending.json`. The pre-Q0/Q2 and pre-guard-fix Graphify snapshots remain archived; defer the final scoped graph refresh until the source owner completes any narrower long-horizon check. No source, test, or PR files were changed for this focus receipt.
