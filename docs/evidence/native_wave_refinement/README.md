# PR 277 native wave evidence

Worktree `/private/tmp/pr274-ci-20261004`, base revision `8c3adea8065645832a6872f86e6b48eb7ed81f7e`. This bundle separates the final corrected native/mixed/half/WRF evidence from the historical pre-fix baseline. Full native and inverse logs remain in the external archive; this folder contains structured results, receipts, hashes, selected logs, and filtered diagnostics.

## Final post-sign-fix native evidence

`native_wave_refinement.json` is the final all-six-output, 17/33-source-column result plus signed U/V damping probes. The exact CTest passed 1/1 in 52.54 s. Corrected producer SHA-256 is `b997115c8991c26d06f455ea0c01eb01dfb0258e4e511a7912589782bf9e8144`; native test source `16959eeabf0ed94f286c3b09b61b083ac1a673c0a6d2aac22bb588ecbb9daa35`; Python driver `0df01b267e12ff212906dff63a5265ce09a1562273565e191beb70ccd199f022`; executable `1a4b4979a70a17718297789cb79ed408f30b56d3c7c8dabad143639b26c539bd`; static library `4c7aa4f098999b0aa5fcef577c532aeba7d051143f8bf6c0e70d466569fd4607`. Before/after receipts bind the files and CTest result. Filtered output is in `native_wave_refinement.filtered.log`.

The enabled U/V damping tests compare the actual full-RHS ON-minus-OFF increment against the active analytic field. This fixes a test-only comparator that had compared the raw OFF tangent with the active term. Final component increment errors are 2.43e-16 to 5.61e-16; signed work rates are -180.572694 (U) and -110.129572 (V). Explicit-only and split-explicit exclusions are exactly zero. Scope is the periodic unit-map native fixture; no WRF seam or general metric/coefficient parity claim is made.

`native_refinement_ctest_group_earlier.log` retains an earlier failed 3-test grouped invocation: Scalar_Diffusion_Contract and Horizontal_PGF_Actual_RHS passed, but the native Python damping gate failed before the comparator correction. The later exact native target passed in its own final invocation. Do not treat the earlier group as an overall pass.

## Post-sign-fix evidence

`postfix_residual_scaling_summary.json` records the corrected 900-second mixed inverse and half-amplitude probe, created at 2026-10-05 15:33:10 JST. The mixed inverse used frozen test source SHA-256 `66acedad1984a97901a122de180b63ee9c57b61dc3ed161f791cdee7bd2b9829`, corrected producer SHA-256 `b997115c8991c26d06f455ea0c01eb01dfb0258e4e511a7912589782bf9e8144`, static library SHA-256 `4c7aa4f098999b0aa5fcef577c532aeba7d051143f8bf6c0e70d466569fd4607`, and executable SHA-256 `e4e3279006f3d64595fdcad5b2974a58efe0089701bb393ed84da7f8f1f72e74`. The completion receipt binds exit status and log SHA. The half-amplitude probe reused the recovered accepted controls without re-optimization; its receipt also binds the endpoint hash. Full run logs and endpoint are retained under `/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261005/native_wave_refinement/post_sign_fix/`.

The corrected mixed residual has 52.805005 J in its mode-2 pair out of 52.890144 J total W residual energy (99.8390%) on 192 owned points. Full-owned relative error is 0.00600255; the separate 105-point objective error is 0.00637248. Denominator cancellation ratio is 0.938421. The half/full mode-2 energy and norm ratios are 0.0625039 and 0.250008. This supports quadratic generation for this residual component, not correctness of every nonlinear coefficient. The selected mode-1 physical projection is not a full-field energy partition, and the two references differ in m=1 amplitude only.

`postfix_wrf_receipt.json` binds the same corrected producer to a 240 s `test/em_b_wave` forecast at `dt=15 s` with internal FP64 carry. All 48/48 stages converged, all 174 floating output variables were finite in each frame, max scaled stage residual was `5.152e-9`, and minimum dry-column mass/layer thickness were 89088.63 Pa / 942.72 m. The candidate and archived RK3 used the same input hash. Endpoint RMS differences and build provenance are in that receipt and summarized in the algorithm report. Candidate timing 7.73706 s versus RK3 0.26754 s is descriptive: candidate used CMake Apple clang 21 Release `-O3`, while archived RK3 used Make/clang 22 `-O2`. Existing Fortran objects were retained because the C++ ABI was unchanged.

## Pre-fix baseline

Files prefixed `pre_fix_` record the 8×6×4 and 16×12×8 native canonical source/modal-block test, carry VJP checks, coefficient-off/implicit-off probes, 4/4 affected scientific CTests, and earlier mixed/half runs. The native fixture sets `implicit_divergence=false, kdamp=0`; it does not exercise damping. The old positive damping probe measured the magnitude of `-kdamp ∂x(div_h)`, which contributes positive modal growth for a cosine U wave and exposed the sign defect. It did not validate physical damping. The earlier mixed and half runs are superseded by the corrected results above.

`mixed_to_half_test_source.json` shows that the test-source delta adds observation-subset denominator diagnostics and a standalone amplitude probe; it does not change the operator, optimizer, accepted controls, or reference in the mixed run. The receipts retain each exact source and executable hash.

## Open gates

Local native, mixed, WRF, Graphify, and Green/Red gates are complete. `graph_receipt.json` (SHA-256 `adff52066608b02e219cce8a40261169b58687121d99156d5261329eec427f12`) binds all 31 worktree source files, including corrected producer `b997115c…`, native C++ `16959eea…`, and Python driver `0df01b26…`; it records 891 nodes and 2760 edges with duplicate parser edges documented. Exact-head GitHub CI is the remaining external check.

## Key packaged hashes

| File | SHA-256 |
|---|---|
| `postfix_residual_scaling_summary.json` | `0124492e58a42a83658b8b6aa3ac5587d351482dd790ecd1efcd57bf486489ac` |
| `postfix_mixed_residual.filtered.log` | `d16612253615f0eb8f311def9c633e90c13f07d1bc93f3e1c4fa952eb3d6cf7e` |
| `postfix_half_amplitude.filtered.log` | `523923b68bbdca36dce2ef08f2ad253f5797670a9e6d7368de42a7bf59b020a5` |
| `postfix_wrf_receipt.json` | `04d0c505b7dffeffb138a45c58e83a0ee96e086344238b4b5d76fd924bb36c30` |
| `native_wave_refinement.json` | `de771c7f2f1495ffa61f1928f55ecf9017c4866f59d06cb7e2958119afe18b8d` |
| `native_wave_refinement.filtered.log` | `5a275444569dbe046d92762f22e1eb0b9041587459e308724a13f634e63d7f9c` |
| `native_refinement_receipt_after_run.json` | `a9b1ae5616dd1378224df448d855d08164bdb7bc5bbb1b56f204be28dd440678` |
| `native_refinement_ctest.log` | `a0ab52301aac67f8b904c39aae0e0a115643b7a1dc45857d5486930d966f7cc5` |
| `native_refinement_ctest_group_earlier.log` | `3763fb02e6428abdf2416e69238248238995c0e3844762f8ac5eeea2a5fcc5b8` |
| `graph_receipt.json` | `adff52066608b02e219cce8a40261169b58687121d99156d5261329eec427f12` |
| `pre_fix_native_wave_refinement.json` | `e93d7cdc87d11e8afe87d1228d6943ace5943cbfe93a3c8b8a7f301f58dd6cde` |
| `mixed_to_half_test_source.json` | `b1d40bb334833376f5fb001f41f0502f3029836c8ae90e9c573d89e32ac43519` |
