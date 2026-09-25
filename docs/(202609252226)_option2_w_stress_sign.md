# Option-2 W stress sign correction

Local timestamp: 2026-09-25 22:26:36 JST

## Finding and change

WRF's hybrid coordinate decreases with vertical index, so `dn(k)` is signed
negative. The C++ metric cache stores positive magnitudes. The W helper already
formed the same stress divergence and terrain correction as Fortran but scaled
with positive `dn`, and unlike the U/V option-2 helpers it did not convert the
assembled tendency back to WRF's signed-coordinate convention.

The option-2 W helper now negates its fully assembled tendency after applying
the terrain correction and top/bottom zero boundary values. This changes only
`diffusion_option == 2`; option 1 and the zero-diffusion result are unchanged.
No metric storage or global RHS sign was altered.

## Validation

- Fresh isolated CMake build used
  `/Users/yhlee/SDIRK3-option2-w-sign-fix/external/libtorch_wrf/sdirk3` as its
  `CMAKE_HOME_DIRECTORY`, with Apple clang 22.1.4 and PyTorch CPU.
- `Scalar_Diffusion_Contract`: passed, 1/1.
- Compiled Fortran W-X Fourier test passed. With `N=8`, `K=2`, `rho=1`, unit
  metrics, and signed Fortran `dn=-1`, the weighted projection was
  `-17.573592210594946` for Fortran and `-17.573592803132197` for C++; maximum
  interior error `7.15256e-7`, predeclared tolerance `2.34315e-6`.
- The `K=0` control returned exactly zero in both Fortran and C++.
- The pre-fix baseline gives the opposite projection sign and fails the same
  oracle; the independent test branch recorded this counterexample.
- Focused Graphify was refreshed after the source and test updates; copied
  implementation/test hashes match the worktree. `git diff --check` passed.

## Limits

This is a flat, one-rank W-X sign/amplitude fixture with prescribed `defor13`,
not a full deformation/terrain producer test. No terrain-mixed W case, V
stress case, MPI run, WRF model run, or RK3 comparison was performed. W
terrain, coefficient variation, and full-step parity remain open.
