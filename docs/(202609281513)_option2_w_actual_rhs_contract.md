# Option-2 W actual-RHS contract

Local timestamp: 2026-09-28 15:13:56 JST (+0900)

## Context

This test-only change closes the L34 W `computeUnifiedRHS` ON−OFF contract for
native option 2 (`mass_coordinate_mode=1`, `smagorinsky_opt=1`). It compares
physical and packed owned interior W cells against the existing compiled Fortran
extraction, including the source `rk_addtend_dry` normalization and final
velocity-mass conversion. The source oracle is independent of the C++ W helper.

## Changes

- Added `Option2_W_Actual_Rhs_Contract`. It prepares a fixed `TileCase`, runs
  Full RHS with both K controls on/off and each control separately, then checks
  the W ON−OFF delta, linearity, finiteness, physical/packed ownership, and
  exact bottom/top W boundary zeros.
- The fixture uses distinct nonzero `khdif=2/3`, `kvdif=4/3`, map `1.25`,
  `velocity_mass_w=80,000`, and the native dry km-opt gates. The Fortran oracle
  independently supplies `RHS_W`; the expected primitive rate is
  `RHS_W * map / velocity_mass_w`. Separate raw H/V comparisons check
  `HW_RAW/velocity_mass_w` and `VW_RAW/velocity_mass_w`.
- The packed case exposed an oracle-input mismatch: the mass-grid W pulse used
  period `nx=8` while packed mass terrain uses `unique_nx=7`. The actual-W
  oracle now uses period 7 for W while retaining period 8 for the U face field.
  A packed old-period mutation remains as a negative control.
- Updated the exact CTest pin and the repository’s 108-test count references.
  No production numerical source changed.

## Validation

The targeted C++ test target built successfully with AppleClang 21, Release,
and LibTorch from `/opt/homebrew`. Targeted CTest passed both
`Option2_Momentum_Geometry_Source_Parity` (38.37 s) and
`Option2_W_Actual_Rhs_Contract` (5.54 s). The registered CTest inventory and
`.github/ci/expected_ctest_names.txt` both contain 108 names. `git diff
--check` passed.

The extracted Fortran routine SHA was
`6f365e7dcb5c3d55324476ea8210e8fe70b15236bdff5e4df05ffd273ef49a14`. The
physical fixture’s maximum total/H/V rate errors were `5.01e-12`, `4.60e-12`,
and `2.53e-12`; packed errors were `1.01e-11`, `4.97e-12`, and `5.58e-12`.
The independently computed FP32 ON/OFF subtraction budgets were `4.91e-11`
and `5.12e-11`. Fortran normalization residuals were `7.45e-10` and
`1.49e-9`, within the source arithmetic budget. Both W boundary levels were
exactly zero. The old packed `nx` period mutation separated from the corrected
fixture by `1.85e-7`, over 3,600 times the `5.12e-11` acceptance budget.

Graphify was refreshed on the modified target worktree based on HEAD
`e9db243b4ee5443f7086f693a58ae7117b33a0a2`; it reports 3,570 nodes and 6,702
edges and includes `dump_option2_w_actual_rhs()` and `computeUnifiedRHS()`.
Graphify JSON SHA-256: `e973c83e979821becf572d478ba94d4f97ce68aebe822ca7cb645789793d1da6`.

Key artifact SHA-256 values:

| Artifact | SHA-256 |
|---|---|
| `test_scalar_diffusion_contract.cpp` | `ac7add508f682db16f50f15606fa2d39efd2414cb3239bf6ed5c70053f49c9b4` |
| `test_option2_w_actual_rhs.py` | `f48e198202afe42a22ff358374025a9d5355f49d752d497fb7d5dcc66b36cb69` |
| `test_option2_momentum_geometry.py` | `0344a4f93ba27d728fe817b3b0d19499f3ad852cda46a310c3669f4dabcb3201` |
| rebuilt `test_scalar_diffusion_contract` | `412a1cd27cf7291a15ebacc029cac450e67db3501d7b182706fa13c05572cd2b` |

## Validation status and next actions

The standalone source contract and geometry regression pass for physical and
packed owned interior cells. Actual-RHS horizontal seam tendency aliases are
outside this contract. Green and Red reviewed the final source and evidence;
Red found no blocker and noted the owned-interior/seam scope. No WRF `em_b_wave`
model run or same-setup archived RK3 field or runtime comparison was performed.
The next step is exact-head CI and PR review.
