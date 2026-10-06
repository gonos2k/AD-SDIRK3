# Quadratic wave forcing evidence checklist

Local timestamp: 2026-10-06T14:42:00+09:00.

Branch/worktree: `agent/quadratic-wave-forcing-20261006` at `/private/tmp/pr274-ci-20261004`; current recorded base `0ae8ae2d6cc700b1c70428cf1fccecdbd2c2ac59`. This checklist is provisional until the root agent finishes the final exact-head CTest and freezes receipts.

- [x] Reuse the qualified native build and reference the source-transcribed Python equations in `tools/wave_quadratic_reference.py` and `tools/wave_quadratic_transport.py`; keep raw payloads external to the checkout.
- [x] Check self and cross interactions of selected native m=1 modes with signed full/half amplitudes; keep complex m=2 responses, mean/mode-1 leakage, owned-cell diagnostics, and U aliases distinct in the machine report.
- [x] Compare source-transcribed instantaneous quadratic coefficients with native RHS on 8x6x4 and 16x12x8. Both reported scaled errors are below `5.8e-7`; half-shift parity error is zero. Component-specific pressure roundoff floors remain explicit in the JSON report.
- [x] Integrate plus/minus full-amplitude and half-amplitude cases for 30 physical seconds at `dt=1.0` and `dt=0.5` on both grids. Treat temporal/amplitude scatter as fixture evidence, not a spatial-order proof: both grids use common eta profiles with piecewise-linear C0 remapping.
- [x] Validate a finite-amplitude six-second, six-step m=2-W objective pullback. Relative directional error `9.95e-5` is within `2e-3`; native AD supplies the VJP only, with no producer-derived Hessian used as independent reference.
- [x] Third native CTest run passed: 1/1 `Native_Quadratic_Wave_Forcing`, 184.10 s; see `/private/tmp/pr278-quadratic-third-ctest.log` and `/private/tmp/stable-wave-build-20261004/quadratic_wave_forcing/wave_quadratic_forcing_report.json`.
- [ ] Freeze after final root edits/run; refresh exact worktree, executable, test-source, and input-payload hashes. Provisional report currently identifies executable SHA-256 `7b4e5ee04281730337902c86bb3fb7942df27651d5d58ac767328057cf313ce1` and test-source SHA-256 `54f4fd0b8e5a7e39e0646dc100ad2207d52557d3490d76b90bbcc10ec1f3541e`.
- [ ] Archive compact receipts and CTest evidence in `docs/evidence/20261006/quadratic_wave_forcing/`; do not copy large per-point CSV payloads into Git.
- [ ] Refresh scoped external Graphify after validating it against final source hashes; retain the historical graph and document AST extraction limitations for CMake and Fortran plus manual source/caller inspection.
- [ ] Record independent Green/Red review and exact-head required CI status. Do not infer CI completion from expected test registration alone.
- [ ] No new WRF `test/em_b_wave` or RK3 model comparison has been run for this work. If production remains unchanged, cite the qualified PR277 result as reused evidence and state its runtime comparison is not a controlled performance claim.

Reference machine report: `/private/tmp/stable-wave-build-20261004/quadratic_wave_forcing/wave_quadratic_forcing_report.json`. The artifact records the coefficient, component, trajectory, and adjoint details; this checklist contains only compact status summaries.
