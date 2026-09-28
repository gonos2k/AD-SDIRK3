# Stage-2 half-clipped trust retry prototype

Local timestamp: 2026-09-28 16:24:27 JST (+0900)

## Context

This bounded prototype uses feature HEAD
`e9db243b4ee5443f7086f693a58ae7117b33a0a2`. Graphify was built on this exact
source revision before the edit (3,564 nodes, 6,690 edges; `built_at_commit`
matches e9db243). It links `unifiedStep()` to `computeUnifiedRHS()`; its C++
extraction does not identify the trust-policy helper, so the candidate equation
and call path were checked directly in `wrf_sdirk3_newton_solver.cpp`.

The duplicate retry at the Stage-2 radius floor reuses its prior clipped step.
For the saved failing candidate, the radius limit divided by the unscaled step
norm is `0.0586827`. Attempt 0 rejects with actual reduction `−2.5774e−10` and
`rho=−0.04976`. Since the radius is already at its `1e−6` floor, attempt 1's
`min(0.5,max_alpha)` again chooses `0.0586827`; attempt 2 detects the duplicate
and stops.

## Change and regression

The isolated candidate changes only the retry fraction: it applies
`0.5*min(1,effective_limit/step_norm)`. This halves the currently clipped
candidate when it is radius-limited and halves the full step when it fits. The
canonical residual map, merit, accept threshold, radius contraction, and
duplicate break remain unchanged.

The existing `Trust_Model_Contract` test now exercises both invariants. For an
unscaled norm of `17.04e−6` and effective limit `1e−6`, it checks that the retry
is half the clipped step and remains within the limit. For an unclipped
`0.5e−6` step, it checks a `0.5` retry fraction. The existing duplicate-attempt
break is also observed in the strict WRF run logs.

## Validation

The `test_trust_model_contract` target built with Clang 22.1, Release `-O2`,
LibTorch, and strict ABI headers. Targeted CTest passed:

```text
1/1 Test #41: Trust_Model_Contract ... Passed (0.81 sec)
100% tests passed, 0 tests failed
```

The strict one-rank WRF runs used the same `wrfinput_d01` SHA-256
`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9` and
`namelist.input` SHA-256
`51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`. The
baseline and candidate linked binaries were respectively
`1a9e16afdedd7fce45d394826ec0f89945400e54e89b2e4519d16ad60105b427` and
`e03bd456ce77081c5e30b1026f299a2fae65cc6572aade5c0cb78a3e33ca3af1`. Their
static archives differ in only `wrf_sdirk3_newton_solver.o`; the other 23
object members are byte-identical. Both runs used the archived PR #251 WRF
objects and `WRF_SDIRK3_RHS_COUNT=1`.

The baseline first reaches ZeroStepStall at Newton iteration 6 with residual
`1.36145e−6`. The candidate accepts the half-clipped retries at iterations 4
and 5: the first has `actual=2.156e−9`, `predicted=2.629e−9`, and `rho=0.8201`;
the second has `actual=8.018e−10`, `predicted=2.56e−9`, and `rho=0.3133`. The
candidate then reaches a new ZeroStepStall at iteration 8 with residual
`1.31642e−6`; the next half-clipped retry has `actual=−5.191e−10` and
`rho=−0.2072`. Both runs stop before an accepted Stage-2 solution and produce
no forecast step. This change repairs a duplicate retry, not Stage-2 convergence.

No WRF forecast-field or runtime comparison against the archived RK3 reference
was performed because Stage 2 still fails. The Stage-2 convergence and forecast
quality checklist items remain open. Full CTest and final Red review of this
three-file candidate are the next steps before any PR decision.

Key source hashes:

| File | SHA-256 |
|---|---|
| `wrf_sdirk3_newton_solver.cpp` | `514516389fe948ddb4d0f588641b637fad7abf69ef4a2632a5be72621a49571a` |
| `wrf_sdirk3_trust_model.h` | `a71c3eff2f3e228f1fef4cb75cd03e8e316a17bf7c0ea2c86cb2d71a0e0ab2e4` |
| `test_trust_model_contract.cpp` | `c723b38a20d3425e0af5244cd433d3b1b7c05c0bb697910b2fc1bbf357361b19` |
| refreshed Graphify `graph.json` | `320591719df43b0028102293265adc5227f01a1638a5548eeb18cd8aed022e36` |
