# PR284 accepted-analysis time comparison preparation

Local timestamp: 2026-10-10 15:24:16 JST (Asia/Tokyo)

## Context

Optimization on unchanged native source now has eight accepted steps; last verified J=.0007243783, computed gradient norm=.131469. Fourth same-code continuation38026719206 is running. Full gradient uncertainty, numerical termination and final analysis time sensitivity remain open.

## Change

Add one manual accepted-analysis-time choice and a scoped helper selecting the terminal gradient call from an explicitly hash-pinned accepted artifact. The exact bundled initial state is used without regeneration; fixture base/Bcan and local delta are compared only to prove chart consistency. Fixed observations/R, source and executable receipt, context, basis/input hashes and baseline960×.3125 profile are checked. The existing native CLI then performs exactly one480×.625 forward-only call, N14/K12/EW unchanged. Output state/context/observations/brackets and physical/admissibility guards are checked; per-time prediction RMS/max, normalized aggregate difference, costs and stable residual cross+quadratic difference are reported. The0.1sigma RMS criterion is diagnostic-only, not an error bound, Eg or optimum certificate.

The producer revision is recorded separately from the accepted artifact revision. Source C++/Fortran/native probe and existing optimization driver are unchanged. Automatic CI remains the same five selected CTests, one short replay and metadata consumer; fast syntax now includes the new helper. No deployment/full-suite/platform expansion.

## Verification

Green confirmed the CLI already supports480×.625 under bounded-tape-forward-only. Red reviewed selection, inputs/chart/source/exe, profile/guards and stable arithmetic. Actionlint, undefined-name lint, compilation/help and whitespace checks pass. A mocked subprocess/metadata flow with controlled prediction offsets passed; this is plumbing coverage only, not a native or physics result. See pr284_accepted_time_mock_flow.json. No native time comparison has been executed yet; select the actual final accepted point after the running optimizer result.

No new WRF/RK3 run or comparison. Reuse the qualified PR283 regression explicitly because production sources are unchanged. Forecast after300s and full Eg remain separate conditions.
