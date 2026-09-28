# T1 iter21 realized-versus-nominal displacement probe

Local timestamp: 2026-09-28 23:08:22 JST (+0900)

This disposable, output-only probe froze the repeated rejected Stage-2 VJP-gradient candidate at iter21. It compared the canonical FP32 trial state `U_trial = U_stage + h*(K+dK)` with the nominal state `U_nominal = U_eval + h*dK`, evaluated `F(U_nominal)`, and measured `J_F(U_eval)*(U_trial-U_nominal)`. The solver acceptance path and trust thresholds were unchanged; the diagnostic was wrapped in a nonfatal local `try/catch`. No probe error occurred. No code change is proposed.

For the packed U, W and PH blocks (`ru`, `rw`, `ph`), the canonical-minus-nominal displacement differed markedly for U and PH. Norms below compare `U_trial-U_eval` with `U_nominal-U_eval`; `changed` counts nonzero packed-cell displacement differences, and `max_error_ulp` uses the adjacent FP32 spacing at `U_eval`. Counts include packed halos and are not owner-cell-only counts.

| Block | Actual displacement L2 | Nominal displacement L2 | Difference max | Changed cells | Max local ULP |
| --- | ---: | ---: | ---: | ---: | ---: |
| U (`ru`) | 5.907e-7 | 8.637e-9 | 2.384e-7 | 90 | 43 |
| W (`rw`) | 6.182e-6 | 6.182e-6 | 4.657e-10 | 1,982 | 54 |
| PH (`ph`) | 2.959e-3 | 1.312e-8 | 4.883e-4 | 158 | 16 |

The full and W-block L2 norms of `F(U_trial)-F(U_nominal)` were both 1.079e-4. Applying the JVP to the state displacement difference gave the same rounded full and W norms, 1.079e-4. The linear remainder `F(U_trial)-F(U_nominal)-J_F(U_eval)*(U_trial-U_nominal)` had full L2 5.405e-7 and W L2 6.133e-9. Thus the RHS change is explained to first order by the realized-versus-nominal state displacement in this case; this does not account for the entire trust-ratio gap.

At iter21, the unchanged smooth model predicted reduction `1.78e-9` versus actual reduction `1.951e-10`, giving rho `0.1096` and rejection by the existing `0.25` gate. The secondary realized-ΔU model predicted `1.951e-10` (rho about `0.9999`). Its predicted reduction is lower than the smooth-model prediction by `1.5849e-9`. The same candidate repeats at iter22 and iter23, so these are not independent direction samples. This supports a local model-coordinate mismatch associated with the canonical FP32 stage-state construction. It is not evidence of a global precision floor and does not show convergence.

## Controls and provenance

The detached probe worktree is `/private/tmp/sdirk3-t1-iter21-u-displacement-probe-20260928`, based on PR #255 commit `c6ce9e80ba72c31676ab7f63ecb99abb32456911` plus the previously reviewed local fallback source. Only that disposable worktree received the output instrumentation. Graphify was verified against the copied pre-instrument source hash, then refreshed after instrumentation; it reported 3,564 nodes / 21,366 extracted edges. `git diff --check` passed.

The same linked executable ran env-off and env-on with `OMP_NUM_THREADS=1`, `WRF_SDIRK3_RHS_COUNT=1`, and the identical one-rank namelist, diagnostics and initial input. Their first 515 ordered RHS input digests match. The probe emitted two additional RHS digests at totals 10715 and 10716 for the nominal RHS and displacement-error JVP; removing those two records restores the entire 14,134-entry env-on stream from the earlier continuation run. The 31 tensors in the terminal iter23 snapshot are bitwise equal to that earlier run. The iter21 candidate input digest and acceptance metrics are unchanged. The run still ends at `ZeroStepStall` on iter23. Both partial `wrfout` files have the same SHA; no forecast step completed.

- Diagnostic Newton source: `98508b640ac711564896dd970f335b8bdddb15514e20dc8485d9277a00746797`
- Newton object: `027b060b58f6ff51dfff9b128682aa7ae32d312d050d84ad0c0d686f30dbd6aa`
- LibTorch archive: `99a9f9b04f762b5384eec9593afef3695b6d675afc8d801f519283006fd5863c`
- Shared executable: `1b43a0834444a3394488abaed9b018980136c8f53841694f26c174d79bff43e6`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `namelist.input`: `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`
- Env-off / env-on `rsl.error.0000`: `12f485a5e3d9b52feaeeae73fc6fc9712963eac753cccf733d2544e3535f4df9` / `6299c01bbecf1636264dec4c4522f7284612cc86197fb49b78ea27acb264fb79`
- Env-off / env-on partial `wrfout`: both `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`
- Terminal iter23 snapshot: `586d9ae7dc1853ebbb7d49b07dddee7897a7c73c0ff605f9a8615254831d1fe6` (31 tensors; bitwise equal to prior run)

No forecast fields or runtime comparison against same-setup RK3 was performed. Stage 2 stalled before a forecast step; time accuracy, forecast quality, and equal-accuracy performance remain unmeasured. A bounded follow-up may compare the smooth and realized-ΔU trust predictions uniformly for eligible fallback candidates, with canonical trial evaluation and all existing gates unchanged; no per-candidate model switching or tolerance changes.
