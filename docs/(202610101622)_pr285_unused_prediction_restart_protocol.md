# PR285 unused-time forecast comparison protocol

Local timestamp: 2026-10-10 16:22:30 JST (Asia/Tokyo)

## Context

Ten strict cost-decreasing updates on the same960×.3125 problem reached the computed four-control gradient norm3.0798e-6 (<1e-5), with J=.000723708. A same-literal-state480×.625 forward comparison passed the0.1sigma RMS diagnostic (150/300 ratios.0188/.0202), but the objective changes by.04256 and fullEg remains open.

## Bounded prediction check

Select actual native checkpoint300 from the final accepted delta10 artifact and from the initial saved-analysis delta0 PR283 artifact. Both archives and the native executable are hard-pinned. Reuse the same fixed physical context, solver tolerances/EW and original observation file; no optimization or new assimilation observations.

Advance each literal state300→600 and600→900 with exactly two existing960×.3125 forward-only calls. The second segment receives the first segment's checkpoint300 value exactly. Original150/300 cost from the CLI is stale for these restarts and is ignored; only endpoint predictions are labeled absolute600/900.

Reference targets use the same coarse/fine descriptors, original four truth coefficients and independent source matrix/physical-height sampler. Before any call, source150/300 values must reproduce the pinned observations to roundoff. Future references are then formed at600/900, not refit from native results. Complex qtruth receipt hashes real and imaginary data, with exact source-module hashes.

This is a state-only cold-restart protocol for both branches. Files omit Newton predictor snapshots; the experiment is not bitwise continuation of an uninterrupted warm trajectory. No new solver/API or production source changes.

## Verification and scope

Green source reconstruction matched pinned xyz exactly and150/300 values within2.31e-15m/s. Red reviewed state/source/input/relative-time/protocol guards. An offline four-call flow with intercepted subprocesses exercised exact synthetic handoffs/labels and metrics; it is plumbing coverage, not a physics result. Complex-hash omission was caught and corrected before any native run. Syntax, undefined-name lint and actionlint pass. Actual forecast is pending.

Manual forecast lane has a60-minute ceiling for four targeted calls. Automatic CI remains five selected CTests,16-step replay/metadata consumer and syntax only. No deployment/full-suite/platform expansion.

No new WRF/RK3 run. Production C++/Fortran is unchanged; reuse PR283's qualified regression explicitly. Full gradient uncertainty, general forecast accuracy and general WRF/4D-Var remain separate claims.
