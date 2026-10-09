# PR281 primary fine-grid inverse runtime update

Local timestamp: 2026-10-08T12:58:23+09:00.

The frozen runner at SHA-256 `d2cf625ed2024de8633863bc195dba9fae84639727495fb01f08f0136809d8f4` was validated as an uncommitted follow-up on base HEAD `7c3ce814a655ebef9518b39284a21aa605429188`; its file hash identifies the exact tested runner. The CI workflow remains unchanged at SHA-256 `86bdb28640819372d3d75bd64c643cf437f61eabb513851d4075f1364baa9ee3`; its core job limit is 180 minutes and it uploads the inverse report and artifact directory. CTest keeps the `Native_Physical_Wave_Inverse` timeout at 2,700 seconds.

## Bounded runner change

The runner removes the repeated fine-grid h=10 BFGS diagnostic from the required default gate. It keeps the coarse h=10 optimization and launches the required fine h=5 optimization directly from the transferred coarse optimized controls, with the source fine preconditioner, the existing maximum of 12 iterations, and the existing projected-gradient tolerance of 1e-5. No criterion or solver tolerance was loosened.

At the returned fine h=5 controls, it performs one fine h=10 full-VJP evaluation to compare objective and projected gradient at the same physical initial state. It also checks the fine h=5 objective VJP and observation pullback at the coarse controls using four forward-only perturbation evaluations at 60 steps and 5 s. These keep the existing epsilons 0.01 and 0.005 and the existing 2% relative-error limit. The h=5 directional check reuses the optimization's initial full-VJP evaluation. The report preserves stationary line-search trials separately from the last Armijo-accepted state. The earlier local fine h=10 `line_search_failed` result remains archived and open; the current runner makes no h=10 convergence claim.

## Exact-head CI outcome

GitHub Actions run `37707372699` completed the 129-name expected CTest inventory match. `Native_Physical_Wave_Inverse` timed out at its 2,700-second test limit. CTest reported 128 Passed, which includes the one skipped MPS test in its summary; 127 tests were scientifically exercised and passed, one MPS test was skipped, and the inverse test timed out.

The streamed log records 53 successful native calls before timeout: two descriptor calls and 51 trajectories. The fine h=10 BFGS pass completed 23 evaluations and was interrupted on evaluation 24. The primary fine h=5 optimization was not reached, so this CI run supplies no numerical result for the modified primary gate. The diagnostics artifact SHA-256 is `65487ecb3973f98bb44ad2526f3148f87631d6cc7798f3a89eba465128632cd7`; the raw log is `/private/tmp/pr281-ci-37707372699-failed.log`.

## Graph and validation state

The refreshed Graphify receipt is `docs/evidence/20261008/pr281_graph_refresh_receipt.json` (SHA-256 `bee77a2a371d1cec9d9290d22660d9b7daca35c15d2b8443e5a1e897f329b36a`). The current graph has 1,142 nodes and 2,661 links; all 39 mirrored code source files byte-match the frozen worktree. The CMake test registration, 129-name inventory, and workflow are separately hash-bound. Graphify does not extract CMake/workflow semantics or WRF Fortran integration, and its edges are navigation aids rather than numerical evidence.

### Frozen d2 local replay proof

Local timestamp: 2026-10-08T13:04:57+09:00.

The frozen runner SHA-256 `d2cf625ed2024de8633863bc195dba9fae84639727495fb01f08f0136809d8f4` completed the bounded cold h=5 replay as an uncommitted follow-up on base HEAD `7c3ce814a655ebef9518b39284a21aa605429188`, C++ SHA-256 `d6e9505a994ce765ce58aa51c8af33ebc49fbf95ebe8b60a48c5ca0e42146787`, and executable SHA-256 `18ec3bf79cce55d1dd4108334872b6f3354533d18bb5a44fe0caa59bb3b26e1e`. The full report is `/private/tmp/pr281-fine-wave-v3-replay/full_report.json`, SHA-256 `0a66f42f983728775878febe94e78cea39198d8324358556f283e54e466976ee`; call-level state, observation, payload, and producer hashes are in `/private/tmp/pr281-fine-wave-v3-replay/payload_provenance.json`.

The replay issued 45 calls total: 32 payload reuses on exact state and observation hashes and 13 fresh calls. The cached set comprises two grid descriptors, 28 coarse/parity/finite-difference/fixed-control prefix trajectories, and the two completed cold h=5 evaluations from the loaded 95a runner. The fresh calls used the current d2 runner and C++/executable pair: eight h=5 adjoints, four h=5 forward-only finite-difference perturbations, and one final fixed-control h=10 adjoint. All 13 returned `rc=0`. The interrupted third output from the earlier 95a process was excluded.

The cold h=5 optimization used the coarse h=10 controls and the source fine preconditioner. It returned `converged_stationary_trial` with objective 7.12580487864, down from 438.479713093, and projected-gradient norm 9.66988×10⁻⁷. The optimizer used 10 native evaluations and completed eight Armijo-accepted updates; its ninth line-search trial met the unchanged stationarity gate but was not Armijo-accepted. Its objective is 9.88951×10⁻⁹ above the last accepted Armijo objective 7.12580486875, so the trial and accepted state remain distinct.

At the cold h=5 starting controls, the new 60-step/5-second directional checks passed at ε=0.01 and 0.005 using four forward-only evaluations. Objective relative errors were 3.56×10⁻⁸ and 2.05×10⁻⁸; H-transpose relative errors were 1.51×10⁻⁸ and 1.54×10⁻⁸. Both checks preserved vertical brackets, with face margins about 3.987 m. A single full-VJP h=10 evaluation at the returned h=5 controls gave objective 27.71233197 and gradient norm 1,184.84; h=10 was not optimized. At these fixed controls, the h=10 minus h=5 objective difference was 20.58653, which is time-grid sensitivity evidence and not an accuracy or time-order claim.

The 30-call native prefix in the prior hosted run took 866.785 seconds. For a capacity estimate, the measured hosted 60-step forward call was 47 seconds; the local 60-step h=5 adjoints took about 45 seconds, versus about 40 seconds for the local 30-step h=10 adjoint, while the hosted 30-step fine adjoint took about 77 seconds. Scaling the local h=5 adjoint by that hosted h=10 rate gives an estimated 87 seconds per h=5 adjoint; adding ten such evaluations, four 47-second forward checks, one 77-second h=10 adjoint, and the 866.785-second prefix gives about 2,002 seconds. This is a workload estimate, not a bound on hosted-runner variation. The CTest timeout remains 2,700 seconds and the core job timeout remains 180 minutes; a fresh exact-head CI run is still required to establish completion.

The earlier CI failure remains part of the evidence: run `37707372699` matched the 129-test inventory, then timed out `Native_Physical_Wave_Inverse` at 2,700 seconds after 53 successful native calls. CTest reported 128 Passed, including the MPS unavailable skip; 127 tests were exercised and passed, one was skipped, and the inverse test timed out. No new WRF `test/em_b_wave` run or same-setup RK3 comparison was performed; previously qualified PR279 WRF/RK3 evidence is reused because this change touches only the test runner and report path. No claim of forecast-quality or solver-performance improvement follows from this test-only validation.
