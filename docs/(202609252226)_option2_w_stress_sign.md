# Option-2 W stress sign correction

Local timestamp: 2026-09-25 22:26:36 JST

## Finding and change

WRF's hybrid coordinate decreases with vertical index, so `dn(k)` is signed
negative. The C++ metric cache stores positive magnitudes. The W helper already
formed the flat-grid stress divergence with positive `dn`, and unlike the U/V
option-2 helpers it did not convert the assembled tendency back to WRF's
signed-coordinate convention. The terrain contribution is included in the
same final sign conversion, but mixed-terrain W parity remains untested.

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
stress case, or MPI run was included in the helper-level test. W terrain,
coefficient variation, and full-step parity remain open.

## Incremental WRF follow-up

Local timestamp: 2026-09-25 22:40:43 JST. After the helper-level result above,
an incremental WRF relink was run with production HEAD
`00c36d6878d9d3bfb960bfbfcc7975215bdd21af`. Receipt SHA-256:
`73d815efc144ec373b881f27b13068d9bb82a1876032ec000f794b5212526080`.
The new executable SHA-256 is
`ededc3a92d009b8113f2079839039a5e356d0a8a1c2b4018b455a1ad3d0723e2`; the
pre-fix executable is `b11298ced43cec7f54cdb923dd6856181ac96299c782e866ee463b7f2b1eb1cb`.
All four runs used identical `wrfinput_d01` SHA-256
`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`.

- PC2, `diff_opt=2`, `khdif=kvdif=1000`, 60-second step/one-minute duration:
  both old and new executables fail at the first native RHS call with the same
  `17` versus `16` tensor-size error in the pre-existing
  `compute_vertical_diffusion_u_stress` path. Both artifacts contain only the
  initial frame (174 finite float fields, output hash
  `00f8794f668a455a56c09bb2f2395cd74adae50add71eaa9d6e5c24a227f0c6c`). The
  failure occurs in Step 10 after Step 9 has evaluated horizontal W diffusion.
  The RHS and stage are then aborted, so this run provides no accepted-step
  or output-level validation of the sign correction.
- PC2 with `kvdif=0` completes two frames with 174 finite float fields and is
  byte-equal to the prior zero-W-diffusion output (`3dcf6ae1362d55dce9f18b2e50a1b9b041a6660453f0304a60d080afef943650`). Since W diffusion is
  disabled, this is only a regression control.
- RK3 with `diff_opt=2`, `khdif=kvdif=1000` completes two frames with 174
  finite float fields (output hash
  `fd303f76ab81d70232214a88622abef05c08ec3e6a9af17a57e3e7a9e6b97034`). This
  is a separate Fortran-scheme run, not a PC2 forecast comparison.

The incremental relink therefore provides no active-W WRF acceptance result.
The pre-existing vertical U-stress shape blocker must be handled separately;
this W sign change does not modify that path. No same-setup PC2/RK3 field or
runtime comparison was possible because the active-W PC2 run aborted.

Additional validation on this source revision: full local CTest passed 102/102
(log SHA-256 `7aa94daabbf71b45c32cbf7c37e645017f4f2b0f2eff58a8a8a17d1ba961a3b0`);
the Make-built core archive contains exactly the 24 manifest members (archive
SHA-256 `35b698f1687543315f65bc6174f840bef005ce9d26692577e5a2fa01d2e47a24`).
These checks do not replace the unavailable active-W model execution.

### Fortran oracle build-isolation follow-up

Local timestamp: 2026-09-25 22:42:02 JST. The extracted W oracle now runs its
Fortran compiler subprocess with the per-run temporary directory as its working
directory, isolating generated `.mod` files. Two concurrent invocations passed
at HEAD `00c36d6878d9d3bfb960bfbfcc7975215bdd21af`, each with the same source,
routine-body, C++ binary hashes and result: projection Fortran
`-17.573592210594946`, C++ `-17.573592803132197`, maximum interior error
`7.15256e-7` under tolerance `2.34315e-6`, and K=0 exactly zero on both paths.
The test does not leave module files in the source worktree.
