# T1 realized-displacement trust-model comparison

Local timestamp: 2026-09-28 23:24:18 JST (+0900)

This bounded experiment compared the existing smooth prediction with the realized-ΔU prediction for every eligible, local VJP-gradient fallback candidate. Both modes used the same binary and inputs. Candidate generation, canonical FP32 `K/U/F/R`, positivity check on actual reduction, `assess_trust_model`, the 0.25 acceptance threshold, block-aware and AD-fallback gates, per-Newton RHS budget, and maximum 40 Newton iterations were unchanged. Only the prediction selected for each fallback assessment and its rho-based radius bookkeeping changed. The local environment selector defaults to smooth; `WRF_SDIRK3_T1_ACTUAL_DELTA_MODEL=1` selects realized-ΔU. This is not production configuration or a proposed code PR.

| Mode | Fallback attempts | Accepted / rejected | Ordered RHS digests | Added fallback RHS calls | Terminal Stage-2 result |
| --- | ---: | ---: | ---: | ---: | --- |
| Smooth | 15 | 12 / 3 | 14,134 | 75 | ZeroStepStall, iter23, residual 5.6819e-7 |
| Realized ΔU | 16 | 13 / 3 | 15,843 | 80 | ZeroStepStall, iter24, residual 5.61148e-7 |

At iter21, the same candidate had actual reduction `1.951e-10`. Smooth predicted `1.78e-9`, rho `0.1096`, and rejected against `0.25`; realized ΔU predicted `1.951e-10`, rho about `0.9999`, and accepted. At the next base, iter22–24 candidates had predicted and actual reductions near `-6.952e-11` and `-6.955e-11`, so the unchanged trust assessment rejected them. Stage 2 still failed against the `1e-7` residual tolerance.

Do not attribute the final residual difference solely to iter21. Selecting realized-ΔU changes rho-based radius bookkeeping on earlier accepted fallback candidates, so the solver trajectories diverged before iter21. The same-binary runs share the first 3,871 ordered RHS input digests. Smooth mode reproduces the entire previous smooth digest stream and its 31 terminal tensors exactly. The realized-ΔU run used 80 fallback RHS calls versus 75 in smooth mode; whole-run counts differ by 1,709 RHS records and the realized-ΔU trajectory uses about 1,500 more Krylov iterations, including trajectory-dependent solver work. This comparison shows a bounded change in the stalled trajectory, not convergence or an established general benefit.

Both runs stop in Stage 2 before a forecast step. Their partial `wrfout` files are byte-identical. No forecast fields or runtime comparison against same-setup RK3 was performed; time accuracy, forecast quality, and equal-accuracy performance remain unmeasured. No further T1 model tuning is planned from this result.

## Provenance

The disposable worktree `/private/tmp/sdirk3-t1-actual-vs-smooth-model-choice-20260928` is based on PR #255 commit `c6ce9e80ba72c31676ab7f63ecb99abb32456911`, plus the previously reviewed local fallback and the environment-selected model comparison. Only that local worktree was modified. Graphify was checked on the fallback source and refreshed after the selector edit (3,564 nodes / 21,366 extracted edges); `git diff --check` passed. C++ used Homebrew Clang, C++17, `-O2`, strict ABI header mode, and the same LibTorch archive for both runs.

- Experimental Newton source: `f3353c558130d05d90767ec752b84cbc6756b38ff4dac8169962c0fd035b1e96`
- Newton object: `bdccfb887f03d440cda0481409ec570fb0765eb2efa22043cf8dc8bb4f604b49`
- LibTorch archive: `84168595264aa623b855e052892ee7ceaf497147e94adc15a823392b3a9a0e92`
- Shared executable: `7fb89a2a570403f2105a5c69a228d6bc47c4017a6e57421a801f92cd66b6d4e3`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `namelist.input`: `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`
- Smooth / realized-ΔU `rsl.error.0000`: `2347ca6b464c29ff195472d9406fe8758b5a6df1fd72607370d3d681e304e0cb` / `d0e3eb56cd21ff02853b0fe835dfef32ba8f16be30a2963e37c7665c311099e8`
- Smooth iter23 / realized-ΔU iter24 terminal snapshots: `586d9ae7dc1853ebbb7d49b07dddee7897a7c73c0ff605f9a8615254831d1fe6` / `7549f03c2d9b40c3801b863da0f5cb9643ce7d37c35530b05d7882bda19d5142`
- Smooth / realized-ΔU partial `wrfout`: both `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`

Run artifacts are under `.validation/model_choice/`. The experimental source remains local; no production model PR is proposed.
