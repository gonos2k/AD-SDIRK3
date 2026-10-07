# PR281 fine-grid physical-wave inverse status

Local timestamp: 2026-10-08T06:39:17+09:00.

This follow-up adds a test-only physical-W inverse fixture on the existing 16×12×8 fine grid, with the same 40 km × 30 km domain, 105 observation locations at 150/300 seconds, σ = 3×10⁻⁴ m/s, and four source-profile controls as the coarse fixture. The native adapter runs retained FP64 trajectories and returns the selected initial-state VJP. The source-only observation model generates the fixed targets from the fine-grid linear wave operator. No production RHS, WRF executable, namelist, or model input changed.

## Final local report

The full default runner was assembled offline from exact state/observation-bound cached payloads for its coarse and h=10 prefix, then ran the eight h=5 optimization evaluations on the current C++ test executable. The offline assembly made zero native calls for cached prefixes and recorded 53 exact payload reuses; the eight new h=5 calls ran with the default Newton 10⁻¹² and Krylov 10⁻⁸ settings. Their per-call raw stdout/stderr, input hashes, payload hashes, and checkpoint/prediction hashes are in `attempt12c` under `/private/tmp/pr281-fine-wave-full/`.

The exact runner report is `/private/tmp/pr281-fine-wave-full/attempt12c/full_report_offline_replay.json`, SHA-256 `f724c02912c0290c14384df209db9853a1b7919f0e3035253d3d0506866bdb10`; its producer bindings are in `attempt12c/payload_provenance.json`. Cached coarse/h=10 native payloads came from test source/executable pairs `d69b888…`/`4c7b5c35…`; the reused h=5 starting point came from `d6e9505…`/`18ec3bf7…`; all eight new h=5 payloads came from the latter pair. The report marks this mixed provenance explicitly rather than treating every cached result as freshly produced by the current binary.

The coarse h=10 inverse converged at an Armijo-accepted point: objective 25,580.2562 → 8.20941555 and projected gradient norm 2.35694×10⁻⁶. The fine h=10 inverse remains diagnostic-only and is not reported as converged: its bounded search ended with `line_search_failed` at objective 8.20785393 and projected gradient norm 9.36034×10⁻⁵. Its optimality remains open.

The primary fine h=5 inverse reached the new typed `converged_stationary_trial` status after eight Armijo updates. Its terminal trial has objective 7.12580486882, down from 23.6238701829, and projected gradient norm 2.95530×10⁻⁶. This trial was not accepted by Armijo: its objective is 4.31853×10⁻⁹ above the last Armijo state. The report preserves that distinction, the accepted history, and every line-search trial. Face margins remain about 3.98 m, with zero bracket changes; no observation or grid-knot crossing is implicated.

The h=5 result is a reoptimized inverse on the same fixed observation set. At the h=10 starting controls, the h=5 objective was 23.62387 versus 8.20785 at h=10; this fixed-control comparison is time-grid sensitivity evidence, not an accuracy claim. No global time order or solver-performance claim follows.

## Numerical floor and source audit

The m=3 finite-difference checker uses adjacent widths (0.08, 0.04), retains all five width observations, and leaves the inherited 0.002 sensitivity budget unchanged. Its selected finite-upwind-dominance interpretation remains limited to the stated direction; complete cubic closure is open. The operand-floor evidence was reanalyzed from the saved endpoints, without rerunning the quadratic native suite.

The objective recomputed from each native prediction and fixed observation matches the native scalar within about 2×10⁻¹⁵. Tightening Newton from 10⁻¹² to 10⁻¹³ changed predictions and objective but did not resolve the forward/VJP directional mismatch. A stricter Krylov tolerance left the forward checkpoints and predictions bit-identical and changed only the pullback slightly. At Newton 10⁻¹⁴, both selected points hit the existing unconverged-root guard, so no residual criteria were relaxed. The detailed precision receipts and raw logs are under `attempt6` in `/private/tmp/pr281-fine-wave-full/`.

The bounded h=5 optimizer evidence, including the exact cached initial point, 19 fresh candidate calls, offline accepted-history replay, final line-search failure, and source-before/after hashes, is in `attempt10`. `attempt11` records the failed offline replay when the updated BFGS trajectory requested a state without an exact cached payload; it stopped before any native call. The final `attempt12c` replay used the current source and exact available payloads, with every new h=5 call executed natively.

## Graph, registration, and remaining validation

The scoped Graphify refresh is bound to `docs/evidence/20261008/pr281_graph_refresh_receipt.json`. It contains 1,142 nodes and 2,660 edges, maps all 39 extracted code sources byte-for-byte to the worktree, and manually binds CMake, the expected CTest inventory, and the CI workflow. The prior graph SHA `2cefaac…` remains preserved. The structural diff removed 394 edges and 124 nodes while adding 147 edges and 69 nodes; the old edges were not forced back into the refreshed graph. CMake, Fortran, and workflow semantics remain extraction gaps, so the graph serves as navigation evidence only.

CMake now registers `Native_Physical_Wave_Inverse` with the default bounded runner. The exact inventory is 129 names, matched by `ctest --show-only=json-v1`; the configured timeout is 2,700 seconds. The serial sum of measured stages for 61 unique native calls is 1,823.85 seconds, so the timeout adds roughly 48% margin for hosted-runner variation. This timing is a test-capacity basis, not a performance result.

The required exact-head GitHub CI run remains pending. No new WRF `test/em_b_wave` run or same-setup RK3 comparison was performed; the previously qualified PR279 WRF/RK3 receipts are reused because production solver code and model inputs are unchanged. Do not merge until the exact-head CI and final Green/Red report reviews pass.
