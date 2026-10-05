# PR 277 native wave refinement checklist

Updated local timestamp: 2026-10-05 16:01:51 JST
Worktree: `/private/tmp/pr274-ci-20261004`, base revision `8c3adea8065645832a6872f86e6b48eb7ed81f7e`

## Historical pre-fix evidence

- [x] Native 8×6×4 and 16×12×8 canonical source/modal-block and carry checks archived under `pre_fix_*`. These use `implicit_divergence=false, kdamp=0` and do not exercise damping.
- [x] Earlier coefficient-off/implicit-off probes and mixed/half residual results retained as baseline only.
- [x] The earlier 3-test post-sign-fix grouped CTest log is preserved: Scalar_Diffusion_Contract and Horizontal_PGF_Actual_RHS passed, while the then-current native Python gate failed. This is not an overall suite pass; the comparator was corrected and the exact native target passed later.

## Corrected local closure

- [x] Corrected the U/V divergence-damping sign in `wrf_sdirk3_tile_unified_impl.cpp` (source SHA-256 `b997115c8991c26d06f455ea0c01eb01dfb0258e4e511a7912589782bf9e8144`). Green/Red sign reviews are complete.
- [x] Corrected the native test comparator: compare the actual Full-RHS ON-minus-OFF increment with the active analytic damping term and normalize to that increment signal. This is a test-only repair; no additional production change.
- [x] Final `Native_Stable_Wave_Refinement` CTest passed 1/1 in 52.54 s. It checks 17/33 source columns over all six packed outputs, analytic-zero budgets, and the 90-second modal-block propagation.
- [x] Signed U/V damping checks passed. Component full-field increment errors are 2.43×10⁻¹⁶ to 5.61×10⁻¹⁶; signed work/energy rates are −180.572694 and −110.129572. Explicit-only and split-explicit exclusions are zero. The periodic unit-map fixture does not claim WRF seam or general metric/coefficient parity.
- [x] Corrected 240 s `test/em_b_wave` run passed 48/48 stages, finite fields, positive dry mass/thickness, with same-input archived RK3 endpoint comparison recorded.
- [x] Corrected 900 s mixed inverse and half-amplitude probes passed with matched source/executable/archive receipts. The mode-2 W residual is 52.805005 J of 52.890144 J total (99.8390%); half/full mode-2 energy and norm ratios are 0.0625039 and 0.250008.
- [x] Final Graphify source binding verifies all 31 worktree files, including producer `b997115c…`, native C++ `16959eea…`, and Python driver `0df01b26…`; graph has 891 nodes and 2760 edges. Duplicate parser edges are documented in the receipt.
- [x] Green/Red local review is complete. Evidence and receipts are in `docs/evidence/native_wave_refinement/`.

## Remaining release check

- [ ] Exact-head GitHub CI is pending.

The historical option-1 scalar-diffusion NaN VJP remains a separate unexplained issue; later successful tests do not establish its root cause. No commit or merge is recorded here.
