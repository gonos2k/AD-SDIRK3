# FP64 window forecast pilot evidence

Local timestamp: 2026-10-03 18:55:46 JST (+0900)

This report records completed synthetic inverse pilots and the current production WRF carry validation. The numerical source revision is `a3f0ee3af7704ca8580e7b7b2f0c55d4801ac608`. The evidence checkout is at `ddc59e1f4a4620b3b0f33e350003cfb86242d36b`; that later commit changes only the `core-linux` CI timeout from 45 to 90 minutes, based on the measured 1748.51-second fixed-data refinement. No numerical source change separates these revisions.

The pilot executable predates removal of the budget-probe path and the all-theta gate comment. Its logged values are preserved as executed evidence. The hashes of the final committed source and rebuilt final-validation binaries (not a reconstructed hash of the earlier pilot executable) are in [source_receipt.json](evidence/fp64_window_forecast/source_receipt.json). The structured values and scope caveats are in [pilot_summary.json](evidence/fp64_window_forecast/pilot_summary.json).

The owned-cell budget uses the solver’s `isPackedPeriodicDomain()` metadata. In this fixture, `ids=1`, `ide=9`, `nx=8` makes the predicate false, so all 48 mass cells count. The 7×5 W observation sites are only an observation subset. The dry-layer measure is `dx*dy/(map^2*g) * abs(deta) * (c1h*(MUB+MU)+c2h)`. The theta integral uses `300 K + packed perturbation theta`.

For the neutral 310 K case, 8 analysis steps plus 4 forecast steps at `dt=0.25 s` reduced withheld W error norm from `0.0236798` to `0.000276338 m s⁻¹`. The 16+8 window reduced it from `0.0382321` to `0.000165214 m s⁻¹`. The analysis prefixes and forecast replays were exact. These are synthetic self-twin results because truth and forecasts use the same model.

The all-theta budget is an engineering gate for these fixtures, not an exact RK invariant theorem. Its allowance is 512 machine epsilons times absolute endpoint anomaly products, plus 300 times the mass allowance; it does not subtract or predict the RK bilinear defect for nonuniform theta. The measured 16+8 neutral truth theta drift was `0.0182106 kg K`, against an allowance of `961.219 kg K`. The stable nonuniform case drift was `1.15510 kg K`, against `564.717 kg K`. Treat those checks as scoped guards and do not claim general Qθ conservation from them.

The stable profile spans 310–316 K and gives `N²=1.63203e-5` to `2.86105e-5 s⁻²`. Its 16-step, 4-second equilibrium drift was `1.35926e-14`, below the `3.07721e-10` gate. The initial Eulerian thermodynamic buoyancy tendency matched `-N²W` to relative error `1.96364e-16`, with geopotential-derived height rate matching W to `1.73e-18 m s⁻¹`. This verifies the initial restoring tendency; the full wave period was not measured. The fixture is flat-terrain and sets `Kv=0`, so the pilot does not exercise active vertical scalar mixing or nonzero-terrain theta diffusion.

The fixed-data refinement reused identical observations at 2 and 4 seconds, comparing 16 steps at `dt=0.25 s` with 32 at `dt=0.125 s`, both forecasting to 6 seconds. The run exited successfully after `1748.51 s`. It logged a control difference of `1.52003e-7`, objective difference `1.19184e-8`, baseline gradient difference `3.95149e-4`, zero solve-gradient difference, and a resolved time signal. The withheld forecast difference was `1.00731e-8 m s⁻¹` for W. Since the fine and coarse paths use the same SDIRK model, this is a temporal refinement comparison, not independent model validation.

The completed current `em_b_wave` validation is recorded separately in [wrf_validation.json](evidence/fp64_window_forecast/wrf_validation.json). It used a clean affected-C++ rebuild with preserved Fortran/WRF objects, matching clang 22 `-O2` ABI 1, and incremental relink. The WRF-parity carry run completed 16 steps and 48 implicit stages, had maximum scaled residual `5.1523e-9`, and produced three frames with finite floating fields, positive dry column mass and positive layer thickness. Its single-run step-log sum was `6.49975 s`; the archived RK3 step-log sum was `0.26174 s`. This timing is not wall time or an equal-accuracy comparison. The FP32-off arm failed at the archived first-failure marker with residual `1.274e-6`, as expected; it is not a successful FP32 run.

Historical archived RK3 setup and field metadata remain in [wrf_validation.json](evidence/fp64_carry_adjoint/wrf_validation.json). That reference is separate from the synthetic 0.25-second inverse windows and does not establish numerical parity for the new window workflow.

The final 119-test CTest log last showed 118 tests completed: `FP64_Window_Forecast` passed in `405.49 s` and `FP64_Stable_Column_Inverse` passed in `109.58 s`; `FP64_Fixed_Data_Refinement` was still running, with no final CTest summary yet. No claim is made that the full suite passed. The log is `/private/tmp/fp64-window-final-ctest.log`.

Final local validation (2026-10-03 19:14 JST): numeric source a3f0ee3,
119 registered/119 passed/0 skipped/0 failed on macOS, including actual MPS.
New window/refinement/stable cases take405.49/1752.63/109.58s. Suite wall2083.30s.
Exact per-test results and stdout hash: `docs/evidence/fp64_window_forecast/local_suite.json`.
Earlier pending snapshots above are superseded. Remote Linux final gate remains pending.

Remote final gate completed successfully on ddc59e1, run37114137980, all four
required jobs. Integration ea7f56d has the identical tree d713d007; later
evidence-only documentation does not change validated numerical sources.
Final local119/119 results supersede the historical pending paragraph above.
