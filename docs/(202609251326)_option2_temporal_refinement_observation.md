# Exploratory time-step refinement with active dry option-2 diffusion

Timestamp: 2026-09-25 13:26:23 JST (+0900)

## Context and runs

The final PR #230 executable SHA-256 `0dd150397d0dc13efe48268f789d2a77a4b48ed4ed6699646b43c98d1466787e` was built from C++ source SHA-256 `8b492ff72b379fac5924b2bb79978429e3742eec5795fbc0aa19ca3f427c16f0` and Fortran source SHA-256 `231ee33571b8f7ed9020bb3bb568665674ccc00d040d064e9d830107550284bd`, using an affected-component build and copied archived WRF objects. The reduced 17×17×16 dry `em_b_wave` input SHA-256 is `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`. All cases use `diff_opt=2`, `km_opt=1`, `khdif=1000`, `kvdif=0`, one MPI rank and one thread. PC2 and split-explicit RK3 were run with the same executable and initial field at `dt=60,30,15 s` to fixed final times of 60 s and 240 s. Every run completed, and all saved floating fields were finite.

At each final time the RMS self-difference ratio was calculated per output variable as `p=log2(||X_60-X_30||/||X_30-X_15||)`. These are **implementation self-differences**, not errors relative to an exact atmospheric solution. The source/executable/input/namelist/output hashes and all raw differences are in `.validation/option2-step9/wrf/temporal_receipt.json` (60 s, SHA-256 `1b4cd1c6029093e255cf7d97f62731887f891bb6bc1b0891521fdb026e175656`) and `temporal_4min_receipt.json` (240 s, SHA-256 `e992f8ec70317345079bdfb4e9326e97fc136ad11f7655e788466b7d4806392a`).

| Final time | Scheme | U | V | W | PH | T | MU |
|---|---|---:|---:|---:|---:|---:|---:|
| 60 s | PC2 | 0.62 | 0.55 | 1.39 | 1.40 | −0.36 | 2.50 |
| 60 s | RK3 | −0.98 | −0.89 | 0.24 | 0.83 | 0.15 | 0.90 |
| 240 s | PC2 | −0.35 | −0.23 | 0.25 | 1.16 | −0.36 | 0.52 |
| 240 s | RK3 | −0.96 | −0.75 | −0.29 | 0.25 | 0.62 | 0.12 |

The tested regime does not show a resolved common third-order slope. Several field differences are small enough that FP32 output rounding, solver tolerances, coupling and boundary/forcing update timing may contaminate the ratio. The negative or low slopes do **not** by themselves identify which mechanism dominates, and RK3 is not an exact solution. Neither time order nor same-accuracy performance is accepted from these runs. The next discriminating step is to hold the same space/physics/forcing while tightening nonlinear/linear tolerances and using a smooth manufactured WRF-compatible state with a known or demonstrably asymptotic reference; stage-wise forcing/metric refresh must be recorded. No numerical source was modified for this experiment.
