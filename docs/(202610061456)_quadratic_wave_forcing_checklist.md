# Quadratic wave forcing closeout checklist

Local timestamp: 2026-10-06T14:56:17+09:00.

Base `0ae8ae2d6cc700b1c70428cf1fccecdbd2c2ac59`, worktree `/private/tmp/pr274-ci-20261004`.

- [x] Derive fixture-scope m=1-to-m=2 self/cross quadratic channels from WRF source equations, including EOS/PGF, nonhydrostatic pressure, transport/mass flux, geopotential, and active curvature.
- [x] Compare native signed coefficients on 8x6x4 and 16x12x8; global scaled errors are below `5.8e-7`, U component relative errors below `7.7e-7`, W absolute component budgets pass with the pressure roundoff floor reported, and V is explicitly checked zero.
- [x] Integrate full/half signed amplitudes through `L(m=2)` for 30 s at `dt=1.0` and `0.5` on each grid. Source integration tightening is below `1e-6`; shared eta remapping is piecewise-linear C0 and does not prove second-order spatial convergence.
- [x] Check the six-step, six-second m=2-W objective VJP against centered finite differences: relative error `9.95e-5` within `2e-3`; native AD supplies the VJP only.
- [x] Run affected CTests: `Native_Stable_Wave_Refinement` and `Native_Quadratic_Wave_Forcing` passed, 2/2, total `217.25 s`.
- [x] Bind final report, input, pre/post source, executable, CTest, and raw-artifact hashes in `docs/evidence/20261006/quadratic_wave_forcing/`. Raw cell payloads stay in the external archive.
- [x] Refresh the external scoped Graphify code graph after preserving its prior graph; all 39 extracted sources plus CMake/CI config hash bindings match the worktree. Manual caller citations and CMake/Fortran extraction gaps are recorded.
- [x] State that no new WRF or RK3 model run was performed and reuse only qualified PR277 em_b_wave/RK3 evidence for unchanged production source. Runtime comparison remains descriptive.
- [ ] Complete final Green/Red review of the frozen report and receipts.
- [ ] Run exact-head GitHub CI after PR commit/push; 128 tests are expected (127 runnable, one MPS-only skip on Linux). This checklist does not claim full CI completion.
