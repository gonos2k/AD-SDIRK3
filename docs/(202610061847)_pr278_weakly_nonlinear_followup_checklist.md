# PR278 review follow-up: weakly nonlinear wave evidence

Local timestamp: 2026-10-06T18:47:15+09:00.

Current worktree: `/private/tmp/pr274-ci-20261004`, branch `agent/weakly-nonlinear-wave-20261006`, HEAD `04b298b9c9fd6cf71fb6996261922d47253e9a72`, tree `aec0d443b7d1d7d648060338daf8fcd3c540610a`. This tree matches the reviewed PR278 merge tree. Graph cache verification is recorded in [the navigation receipt](evidence/20261006/quadratic_wave_forcing/weakly_nonlinear_graph_navigation_receipt.json).

## Accepted PR278 scope: closed

- [x] Source-transcribed fixture-scope (Q_2) matched native signed m=2 coefficients for the source-selected modes packed into native fields, including the stated pressure, transport, mass, geopotential, and curvature channels.
- [x] Checked coefficient phase, self/cross responses, expected-zero V response, amplitude behavior, and 30-second native/source trajectories on the two declared column grids.
- [x] Checked the six-second native m=2-W VJP against centered finite differences; final exact-head CI passed the 128-test inventory with 127 passing, one MPS skip, and zero failures.
- [x] Preserved the fixture boundary: this evidence makes no generic full-tile Hessian or time/space convergence claim.

## Open reviewer follow-up

- [ ] **P2 phase control:** Replace the sign-negation-only phase check with an actual quarter-period translation of the source wave and a negative control that demonstrates the check rejects a wrong phase/translation mapping.
- [ ] **Q0 + Q2 forward:** Extend the weakly nonlinear expansion to carry the quadratic mean correction (Q_0) alongside (Q_2), including mean state and column-mass effects, then compare the resulting coupled forward trajectory with the native solver.
- [ ] **Independent polarized sensitivity:** Derive the directional derivative of the source-transcribed quadratic forcing by polarization and compare its sensitivity with the native adjoint along the same declared trajectory.
- [ ] **Longer fixed-control forecast:** Hold the control vector fixed over a longer forecast and measure residual degree and time behavior, recording the interval and norm used for each conclusion.

The follow-up should report its equation domain, state variables, and trajectory bounds explicitly. It should keep separate the established PR278 coefficient/trajectory/VJP checks from the broader weakly nonlinear questions above. No general Hessian, global time-order, or spatial-order conclusion follows from the current fixture.
