# T1 iter9 recovery-direction scale probe

Local timestamp: 2026-09-28 17:37:34 JST (+0900)

## Question and scope

This bounded diagnostic asks whether a source-defined preconditioned-residual recovery direction can lower the strict Stage-2 residual at the terminal iter9 base, after the Newton Krylov direction failed at full, half, and quarter scales. The probe is output-only: it evaluates the existing `dK_rec = -M_inv(R.detach())` recovery direction, including the fallback RU override, halo zeroing, and S-metric trust-radius clip, then applies fixed scales `[1.0, 0.5, 0.25]` to that clipped vector. Each candidate uses the canonical FP32 map:

```text
K_alpha = fl32(K + alpha*dK_rec_clipped)
U_alpha = fl32(U_stage + dt*gamma*K_alpha)
R_alpha = K_alpha - F(U_alpha)
```

The solver never adopts the probe candidates or changes its thresholds.

## Result

All measured actual reductions are negative. The sidecar actual-ΔU model also predicts negative reductions, so its rho is undefined. The ordinary trust threshold remained `0.25`; this recovery direction was not submitted to that production rho gate. The production recovery path uses `recovery_step_is_acceptable` and its fallback-ratio rule. No JVP finite-difference fallback occurred; actual-displacement predictions match measured reductions to about `2e-14`.

| Scale of clipped recovery direction | PH cells changed | W RHS delta L2 | Actual-ΔU predicted reduction | Measured reduction | Sidecar rho |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1.00 | 873 | `2.8355248e-4` | `-1.2311940679e-8` | `-1.2311949148e-8` | undefined |
| 0.50 | 718 | `2.6121765e-4` | `-8.6255796072e-9` | `-8.6255936954e-9` | undefined |
| 0.25 | 528 | `2.1861294e-4` | `-2.0711993811e-9` | `-2.0712183270e-9` | undefined |

The alpha-1 candidate archive reproduces the prior one-direction recovery probe bitwise for the clipped direction, `K`, `U`, `F`, `R`, actual `delta_U`, JVP, and linearized residual. In env-off and env-on runs of the same instrumented executable, the first 515 ordered RHS input digests match. The probe adds exactly six records (candidate RHS then actual-displacement JVP for each scale); removing them restores the rest of the ordered stream. All 31 terminal snapshot tensors are bitwise equal. Both runs end at the same Stage-2 iter9 `ZeroStepStall`, with scaled RMS residual `1.273917e-6`; neither advances to a successful forecast step.

This rejects the tested recovery-direction samples as descents at this base. It does not establish a precision floor or rule out an attainable descent along another direction. The next useful probe needs a predeclared, independent live Krylov direction or a different equation-derived correction, evaluated through the same canonical map and actual-`delta_U` JVP.

## Provenance and reproducibility

The diagnostic branch is based on PR #255 HEAD `c6ce9e80ba72c31676ab7f63ecb99abb32456911`; the report is on a separate docs-only branch. A disposable worktree added only an env-gated solver probe at Stage-2 iter9/attempt2. Graphify was refreshed on that target before and after instrumentation (3,564 nodes / 6,771 edges both times). `git diff --check` passed.

The C++ Newton object used Homebrew Clang, C++17 `-O2`, LibTorch includes, and `_GLIBCXX_USE_CXX11_ABI=0`. It replaced only the Newton object in the baseline archive; the other 23 object members were byte-identical. WRF was linked using the same PR246 clean object set and link recipe in both env arms. The run pair used `OMP_NUM_THREADS=1`, `WRF_SDIRK3_RHS_COUNT=1`, `WRF_SDIRK3_STAGE_DIAG=1`; only the probe arm set `WRF_SDIRK3_T1_RECOVERY_SWEEP=1`.

Hashes:

- Base and probe `wrf.exe`: `1f40d491f1190c4ed2514c871b6378d9a7629c7d4da861e76f3101a66f5f082e`
- Instrumented Newton object: `2dfc596fdc7dffdb598076973554a5a2d98389c4330ad5d7b3107691928541f9`
- Probe C++ archive: `4a38dca1c3cb9ceebefa3854b9bf665a73f3b14c488011095327c79053644363`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `namelist.input`: `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`
- Probe log: `afef30ca8dada30d491f91b77d5ebbdb02c9744a748a446e0797e090a65c0183`
- Probe tensor archive: `7a196d1c578af1ab4e5dad81c6174d9c36b86feb5199ad794a9aded34eb1eb45`

The base/probe run artifacts and scale-sweep archive are under `.validation/t1_recovery_scale/` in `/private/tmp/sdirk3-t1-iter9-recovery-probe-20260928`. This was a strict WRF diagnostic run, not a forecast comparison. No same-setup RK3 field or runtime comparison was performed because Stage 2 failed.
