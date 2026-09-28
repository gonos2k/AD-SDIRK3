# T1 bounded dyadic trust retries

Local timestamp: 2026-09-28 17:08:01 JST (+0900)

## Scope and change

The isolated candidate worktree is based on PR #254 HEAD `5babd1ec11aaef022cbd63f0fadd618fc26cd238`. The trust loop has three attempts. When a radius contraction leaves the same clipped candidate, it now tests half and then quarter of that current clipped step across the remaining attempts. A changed unforced clipped norm resets the retry sequence to half. The loop and RHS budgets remain unchanged; prediction, merit, acceptance, and rejection thresholds are untouched.

The retry-scale formula lives in `wrf_sdirk3_trust_model.h`. `Trust_Model_Contract` checks clipped and unclipped half/quarter steps, the trust-radius bound, and half of a changed clip. No production code outside the Newton trust loop and its helper changed.

## Validation

Graphify was updated before editing on the exact 5bab source worktree (3,564 nodes / 6,789 edges) and refreshed after the code change (3,564 nodes / 6,773 edges). The target-only build completed with Homebrew Clang 22.1.4, C++17, Release `-O2`, LibTorch 2.10, and strict ABI selection. `ctest --test-dir /private/tmp/sdirk3-t1-dyadic-build-20260928 -R '^Trust_Model_Contract$' --output-on-failure` passed 1/1; the contract contains 95 assertions.

A paired one-rank strict WRF diagnostic used the same PR254 baseline archive and input setup, replacing only `wrf_sdirk3_newton_solver.o` in the candidate archive. Both runs used the PR246 clean WRF object set and identical link command, namelist, diagnostics file, `wrfinput_d01`, and environment (`OMP_NUM_THREADS=1`, `WRF_SDIRK3_RHS_COUNT=1`, `WRF_SDIRK3_STAGE_DIAG=1`). The extracted archives have 24 payload members; all 23 non-Newton members are byte-identical.

At the first floor duplicate, the candidate evaluated the quarter-of-clipped trial at raw-step fraction `0.01496`: predicted reduction `1.262e-9`, actual reduction `2.701e-9`, and `rho=2.14`, so the existing `rho >= 0.25` gate accepted it. A later quarter retry had actual reduction `-2.085e-9` and was rejected by the unchanged gate. The baseline stopped at Stage 2 Newton iteration 8 with residual `1.316418e-6`; the candidate accepted the additional trial, then stopped at iteration 9 with residual `1.273917e-6`. Both remain strict Stage 2 failures. This demonstrates that the previously skipped dyadic trial can pass at this captured state; it does not establish convergence or an operational improvement.

No forecast comparison or same-setup RK3 run was performed. The WRF diagnostic case ended before a successful forecast step.

## Provenance

The clean WRF link source was `/Users/yhlee/SDIRK3-pr246-cleanbuild`, commit `28f8a62b53a2315dc97e6995a180f91f93e9d7fa`. Its `configure.wrf` SHA-256 is `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`; the recorded full-WRF build log SHA-256 is `e0265bd31a6659e544cf91001b8f7211e3c2678f171d7862cdcf934f6ac7c05c`. The C++ object was compiled with Homebrew Clang, `-std=c++17 -O2`, LibTorch includes, and `_GLIBCXX_USE_CXX11_ABI=0`.

Source hashes after editing:

- `wrf_sdirk3_newton_solver.cpp`: `fe40adb808cfc8f982634d9ca616a2e589c71f82ee2b6fc68e85cd2cacea2fa5`
- `wrf_sdirk3_trust_model.h`: `35c565ba59a8ba5fa322e2c9a05b16e8006f4bc988e003b9426a7445abd07350`
- `test_trust_model_contract.cpp`: `03732881c39c636f8dd03eddc0eda9af8213df501d785328bc7f16501319fe70`
- `wrf_sdirk3_newton_solver.o`: `cb7712308f06cca8dabceb129205917221f6227ccf80e8fe5de560c2f5654106`

Paired run hashes:

- Baseline archive / executable: `1bdacb7bb1153a81bdedbc5c85a52645f33ccaf7f20eacedf9a5a09c46574ac9` / `29457ecd06b7605ce43d1faf2411c1795f73d780e70e450cf32dcfdbb8826707`
- Candidate archive / executable: `1b6f006896198372cf0a3c1f09235d9473d72db2a221fcaa968d860e1a13f7b9` / `1ff1a92e8f71cda6e26e074ccdafaa4537a99710668c5ee86f0780c32d4e6d80`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `namelist.input`: `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`
- Baseline / candidate `rsl.error.0000`: `8487a86466ee257d9409d698e4649d6b54f09e5655b5afc20b99fd28b7ad5c79` / `94b4c280bfac5560d8e0da9b9c420b46d852e688f7263aecaaf8a380738a8640`

The build, archives, link scripts, inputs, outputs, and logs are under `.validation/dyadic_retry/`. This is a bounded production candidate, not convergence evidence; further convergence work remains open.
