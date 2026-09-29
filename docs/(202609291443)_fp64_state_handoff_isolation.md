# FP64 state handoff isolation on an autonomous ARK324 tile

Local timestamp: 2026-09-29 14:43:40 JST (Asia/Tokyo).

## Context and scope

Base source is merged PR #266, `main` at `aec3088cf919f991a854edf56bb2919dfadf8b4c`. That PR completed the dry strict `em_b_wave` 240 s forecast with FP64 calculations inside each step but still returned FP32 WRF state after every step. Its 15/7.5/3.75/1.875 s WRF field differences did not establish time order. The A/B experiment below isolates the inter-step FP32 handoff in the existing autonomous, dry, fixed-coefficient, one-CPU-tile ARK324 mode-3 path; it excludes Fortran-side physics/boundary refresh and MPI. A separate same-input WRF run below checks that the inactive test hook does not alter the previous forecast. No new same-setup RK3 field or runtime comparison was performed.

Both arms use the same `unifiedStep` stages and RHS, `internal_fp64=true`, `khdif=1000`, `kvdif=0`, one fixed 5 km grid, zero external tendency arrays, and the same initial hydrostatic fixture. Arm A feeds each published FP32 state to the next call. Arm B feeds the preceding FP64 `projected_final` to the next call. A one-shot private test hook allows B only when its FP32 projection exactly equals the state already published to the caller; it rejects fixed-trajectory/retained-adjoint use, wrong dtype/shape/device, or mismatched input. The hook is undefined by default and has no C/Fortran entry. Both arms capture the same stage trace, and their first internal step must be bitwise equal. At every step, the FP32 publication must equal the FP64 result rounded to FP32, all six external tendency arrays remain zero, and the state stays finite with positive full dry mass and potential temperature.

## Independent numerical contrast

The common final time is 4 s; each arm uses 4, 8, 16, and 32 steps (h = 1, 0.5, 0.25, 0.125 s). The table reports RMS differences of adjacent-resolution *final states*, not error against an exact solution. The rates are `log2(D_h/D_{h/2})` in successive intervals.

| Field | FP32 handoff rates | Continuous FP64 rates | Finest-step A–B RMS |
| --- | ---: | ---: | ---: |
| U | 2.679, 0.993 | 3.032, 3.021 | `9.69e-8` |
| V | 3.041, 3.114 | 3.050, 3.033 | `8.04e-9` |
| W | 3.023, 3.103 | 3.024, 3.020 | `1.65e-8` |
| PH | 2.932, 2.794 | 2.930, 2.966 | `1.23e-6` |
| T | −0.831, −0.678 | 2.965, 2.984 | `3.37e-8` |
| MU | 2.975, 2.814 | 2.969, 2.986 | `4.05e-6` |

For continuous FP64, the one-step versus two half-steps defect at h = 1, 0.5, 0.25 s has observed local rates U 4.178/4.066 and T 3.969/3.992. All first-step A/B states were bitwise equal. The contract requires continuous global rates between 2.6 and 3.4 for each field, local U/T rates between 3.5 and 4.5, and a distinguishable T handoff signal; it does not fix an exact FP32-arm rate, which is sensitive to the phase of rounding. The CTest contract passed on this host.

This is direct evidence that repeated FP32 publication can mask or distort time-order measurements for U and T in this fixture. V and W still exhibit near-third-order differences under FP32 handoff here, so the result is field-specific. It does **not** prove that the earlier full-WRF non-monotone differences have this sole cause.

## Source and validation

The changed tile implementation SHA-256 is `c8f942d0b019e10d0bc7897b9a1e6ac58d49d71f83a6feedb49f3b8db0b817b6`; the new test source is `c0d44e71ce1b487727f3b997c6678fb611dc52f85226c79f1697f4f469258318`. A new CMake build at `/private/tmp/sdirk3-fp64-trajectory-build-20260929` linked test executable `34dbee92ca6d3b99748feeeb81bb26a2ae8c07826c0c3017734c4236946c6485` against core archive `41826b3a84445e9122d564244b351e7e9108b9b87a25fda65bc53118b6806552`. CTest registered 111 names matching the pinned inventory exactly. Six focused tests passed: the new `FP64_State_Handoff_Contract`, ARK composition, two-step tile adjoint, full-tile default/FP64 and temporal-order tests. The focused CTest log SHA-256 is `8272e91fb2c9ae0f1d9708f3cce8b0225585bb39f91e8ec54c2323590941effb`.

## Inactive-hook WRF regression

The validation copy `/private/tmp/sdirk3-fp64-trajectory-wrf-20260929` reused the configured, Registry-consistent PR #266 WRF build, then rebuilt the affected C++ archive and relinked `em_b_wave`. No Fortran source or C/Fortran bridge ABI changed; the private C++ class layout did change and its consumers were rebuilt together. The resulting archive SHA-256 is `3343ecdb2e8c6d191a837d6f61f5bea63d5e8d5c5d81fdaa386e3c88f7b7f384`, `wrf.exe` is `4fd525ca664a65082b84d15b70207c0655a2512c1fd7be2af606c301280dde81`, and the compile log is `534ccf3991af222bba1720117b359543a3cfb05c9b741a8c3169bcc817a586db`.

With the previous strict 15 s namelist (`a4434efb24786212cf4e12d512c6b19a8edc59c796c15f7523a1c24c9e8f55d9`) and archived initial field (`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`), the new singleton WRF executable completed 240 s: 16 steps, 48/48 implicit stages converged, largest reported scaled residual `5.1523e-9`, and no logged JVP FD fallback. The three `wrfout` files at 0, 120, and 240 s were each byte-identical to the PR #266 validation outputs (`cd61f4bfa76b0f69fe0baf79a2330d2731c8299c62d2bf0273519cf4a852cccc`, `7697324a36b9eb6918e71d7609d1f1d189b3526c2f5685c85c7fd3fee8a57c49`, `1df2e0bebf473b958553c9f1b2f77b60bf404fb19b876db489ce048186f13d73`). The run log `rsl.error.0000` SHA-256 is `ee7e9a3a8a2e8d6c3066051aa54e8be53b74c6e8fbbd6e836d769a5c8fe8372c`. This validates default-hook neutrality on the declared WRF case, not FP64 state carry in WRF.

## Next actions

For production WRF, first compare the published state with the next caller input around Fortran physics, periodic/physical boundaries, and diagnostic updates. Reusing B's FP64 state in WRF without those updates would integrate a different system. If host-side updates are active, define how they are applied in the same state coordinates before considering persistent FP64 ownership. Separately compare the physical RHS at the same state, time and context across h; then retest a one-step/two-half-step defect and full-forecast refinement. Only after that forward path is fixed should its active W/PH/MU multi-step adjoint be tested.
