# Option-2 stage-local momentum geometry

Timestamp: 2026-09-25 19:09 JST

## Context

The option-2 momentum deformation and stress-divergence helpers were using
`zx_`, `zy_`, `rdzw_3d_`, and `rdz_3d_` cached from base-state setup, while the
same RHS evaluation already had current-stage geopotential and rebuilt stage
metrics for scalar diffusion. This made stress geometry stale during stage
evaluations. The implementation is deliberately gated to the existing dry,
canonical, isotropic `km_opt=1`, `damp_opt=0` path; unsupported option-2
configurations continue to fail closed. Option 1 and legacy helper calls retain
their cached-metric behavior.

## Changes

- Build one immutable option-2 metric snapshot in Step 9 from the current
  `ph_full_for_diff`, using the existing scalar metric definitions and packed
  periodic-X/symmetric-Y layout.
- Pass explicit `zx`, `zy`, `rdzw`, and `rdz` tensors through the U/V/W stress
  helpers and deformation routines. The base-state metric caches are not
  mutated during RHS evaluation.
- Compute U stress vertical averages for both explicit-stage and legacy
  geometry. Only the old cached-slope interpolation remains on the legacy
  branch.
- For explicit-stage shear, interpolate mass-grid `rdz` to U faces by
  adjacent-cell averaging with periodic seam closure, and to V faces by
  adjacent-row averaging with symmetric-Y wall closure. Legacy cached-metric
  fallback is unchanged.
- Keep one U and one V member-function signature by appending default-empty
  geometry arguments, preserving existing callers and avoiding overloaded
  private-member test accessors.

## Validation

- Built `test_scalar_diffusion_contract` in
  `/tmp/sdirk3-option2-momentum-impl-build`.
- `Scalar_Diffusion_Contract`: passed, 1/1.
- The option-2 U-X geometry comparison passed against the source-equation
  transcription: Fortran-oracle maximum tendency `1.67467175e-4`, C++ maximum
  difference `7.77656e-11`, tolerance `2e-6`, periodic seam error `0`.
- `git diff --check`: passed.
- Refreshed the focused Graphify corpus after the source edits; it contains 296
  nodes and 1235 edges.

The Fortran comparison is an explicit numeric transcription pinned to
`dyn_em/module_diffusion_em.F`; it did not compile or execute the Fortran
routine. The local checks did not include a WRF rebuild/run, MPI, a direct V/W
shear oracle, or general option-2 configurations. Those remain open for the
parent integration validation.

## Next actions

Run the full local CTest suite and source-extracted Fortran parity checks on the
integrated branch, then perform the planned incremental WRF relink and compare
the supported one-step case. Keep general option-2, moisture, other `km_opt`,
and noncanonical boundary claims out of scope until separately validated.
