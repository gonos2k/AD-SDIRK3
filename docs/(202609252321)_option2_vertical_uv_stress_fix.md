# Option-2 vertical U/V stress shape and sign repair

Local timestamp: 2026-09-25 23:21:56 JST

## Change

The vertical U and V stress helpers built `rho_u`/`rho_v` tensors that were
never consumed. Those unused interpolations assumed staggered extents that do
not match the mass-grid density (`U: 17x16x18` against `rho/Kv: 17x16x17`;
`V: 18x16x17` against `rho/Kv: 17x16x17`) and raised a shape error before the
stress tendency was produced. The stress tensors already include interpolated
density, so the extra density interpolation was unnecessary and has been
removed.

For option 2, U/V vertical tendencies now use `-g*delta(tau)*|1/dnw|`. This
matches Fortran's `tendency -= (-g/dnw)*delta(tau)` because WRF's `dnw` is
negative while the C++ metric cache stores its positive magnitude. The sign
conversion is local to the option-2 vertical stress helpers; option 1 and the
global RHS are unchanged.

## Validation

- Fresh CMake build root:
  `/tmp/sdirk3-option2-vertical-fix-build`, with
  `CMAKE_HOME_DIRECTORY=/Users/yhlee/SDIRK3-option2-vertical-fix/external/libtorch_wrf/sdirk3`.
- `Scalar_Diffusion_Contract`: passed, 1/1. Direct staggered shape checks pass
  for both U and V actual extents.
- Compiled Fortran U vertical stress oracle passes with positive, vertically
  and horizontally varying `rho`/`Kv` and exact FP32-binary `defor13` values:
  3,536 owned outputs, Fortran range `0.448716223` to `0.944570959`, maximum
  C++/Fortran error 0, unchanged tolerance `3e-6`, and exact K=0 result.
- The test agent's detached pre-fix baseline reproduced the U and V shape
  aborts even for K=0. No tolerance was changed.
- Focused Graphify was refreshed; source, C++ fixture, and Python oracle hashes
  match the worktree. `git diff --check` passed.

## Limits

The compiled Fortran oracle exercises the exact stress and vertical-U routines
with prescribed deformation inputs; it does not run `cal_deform_and_div` or a
full WRF timestep. V has a direct actual-shape test, but no compiled V
source-parity oracle yet. No WRF model or MPI run was performed after this
patch; full option-2 WRF acceptance remains open.
