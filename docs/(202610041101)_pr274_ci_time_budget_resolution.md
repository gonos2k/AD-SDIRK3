# PR #274 CI execution budget resolution

Local timestamp: 2026-10-04T11:01:11+09:00.
PR head before fix: c1c4adf9acfd9691298b55e65f81a26d08bd6711.

- [x] Inspect PR reviews and checks: no review comments/threads; failed required gate follows core-linux cancellation.
- [x] Identify actual stopping point in run37119240099:119-test exact inventory passed; tests1–74 passed; timeout occurred while FP64_Fixed_Data_Refinement (#75) was running. No numerical test failure is reported before cancellation.
- [x] Preserve every test, tolerance, parity, ABI and install condition. Only core-linux job timeout changes90→150minutes.
- [x] Reuse source-matched Graphify:19 cached code files match this PR checkout byte-for-byte. YAML workflow dependencies were inspected directly: required still depends on and requires success from fast-contracts/core-linux/build-contract-negatives. YAML is not represented by the C++ AST graph; no numerical source was edited and no numerical graph refresh is needed.
- [x] Validate workflow semantics: only timeout90→150; action-pin self-tests8 and all workflow checks pass; repository ratchets pass.
- [ ] Obtain exact-head mandatory CI success.
- [ ] Final Green/Red review of the change and final evidence.

The previous successful exact numerical source run37114137980 took4345.07s for CTest alone. On the PR runner, the same window forecast took793.95s rather than596.12s; the job's90-minute budget expired at12:48:49UTC after starting at11:18:36UTC. This is observed hosted execution variability, not a demonstrated new numerical defect. Extending wall-clock allowance does not change the equation, Newton/adjoint tolerance, test inventory, executable inputs or success criteria. Cancellation must still fail the required aggregate.

The old temporary checkout lost its git administrative file and was prunable. Its surviving files were preserved. Only the stale registration was pruned, and the same PR branch was checked out at /private/tmp/pr274-ci-20261004. The dirty /Users/yhlee/SDIRK3 source was not edited.

No new WRF build/model/RK3 run was performed for this CI-only change. Reuse the unchanged-source evidence in docs/evidence/fp64_window_forecast/wrf_validation.json: strict15s/240s carry48/48stages; archived same-setup RK3 final RMS U.0084225882,V.00095424146,W.00062570814,PH1.3329873,T.041425747,MU.44902415. Single-run step-log sum6.49975s versusRK3 .26174s is not equal-accuracy or wall-time cost. The completed short dry fixed-K single-tile algorithm scope stays unchanged.

Green and Red independently confirmed the90m13s timeout, first74passes, and retained119-name inventory/criteria. Final completion will be recorded in the PR body and archived CI receipt after the automatic latest-head gate; this dated report preserves the pre-run status without starting a duplicate documentation-only gate.
