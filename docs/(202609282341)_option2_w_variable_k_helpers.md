# Option-2 W Variable-K Helper Contract

Local timestamp: 2026-09-28 23:41:14 JST

## Context

This work started from PR #253 at source revision `cdf7f173f4b21bf7c9fb07645bd6cb899551a71e` in an isolated worktree. The option-2 native actual-RHS path deliberately rejects nonempty supplied diffusion tensors: its native metric gate requires no supplied K, and the declared native path fails closed otherwise. The repository has no production caller for `setDiffusionCoefficients`. This check therefore stays at the existing W diffusion helper contract and makes no variable-K production RHS claim.

The source contract is that horizontal W stress receives mass-grid `xkmv` and `rho`, interpolates K and density separately with `fnm/fnp`, and multiplies the results. Vertical W stress receives mass-grid `xkmh` and `rho` and forms pointwise `rho*xkmh`; it does not apply `fnm/fnp` there.

## Changes

Extended the existing W actual-RHS CTest with a helper-level fixture using independent positive, nonconstant mass-grid `xkmv`, `xkmh`, and `rho`. It uses nonflat terrain and asymmetric `fnm/fnp`. The C++ fixture directly calls the existing W horizontal and vertical helpers; the extracted Fortran fixture calls `horizontal_diffusion_w_2` and `vertical_diffusion_w_2` on the same fields, including periodic X and replicated Y coefficient/density halos.

The test checks horizontal and vertical W tendencies separately. It also checks wrong-coefficient and scalar-namelist fallback controls, and compiles a Fortran mutation that moves rho into the interpolation before multiplying by K. The existing physical/packed actual-RHS scalar-coefficient controls remain in the same CTest.

The Graphify corpus was refreshed from the edited worktree based on `cdf7f173f4b21bf7c9fb07645bd6cb899551a71e`. Its `built_at_commit` field records the base HEAD; the graph reflects the working-tree files at refresh time, including the uncommitted test edits. The listed edited-file SHA-256 values identify those test inputs. The graph output remains a local generated artifact and is excluded from the change.

## Validation

Standalone build used AppleClang 21.0.0, LibTorch at `/opt/homebrew/lib/libtorch.dylib`, and GNU Fortran 15.2.0. The test executable SHA-256 is `8fde40b43364e061fcecf3ff5702ea53e55a38e82873d3e088f1f4c5e81cb5e9`.

- `Option2_W_Actual_Rhs_Contract`: passed. Physical and packed scalar actual-RHS cases passed. Variable-K helper H/V signals were `0.0110345939` / `0.0203882921`; Fortran parity errors were `2.3284852e-08` / `7.46707678e-09` against relative budgets `4.41e-07` / `8.16e-07`. The preproduct mutant separated from the horizontal reference by `1.99973583e-05`. Wrong-K controls separated by `0.00497878995` / `0.0185593199`; scalar fallback controls separated by `0.00496347435` / `0.0034390129`.
- `Option2_Momentum_Geometry_Source_Parity`: passed.
- `git diff --check`: passed.

Exact edited-file SHA-256 values:

- `tests/test_scalar_diffusion_contract.cpp`: `dd5757305f8de23f7600f7e938887fa7337cbae0303c0d42364ed738cd8af84a`
- `tests/test_option2_momentum_geometry.py`: `613781f0f010c0cbf134e99f402d040e0678f33827823b344753bcc915ea5a38`
- `tests/test_option2_w_actual_rhs.py`: `16271af111c9d3bc8218b39d28440aaa65edb63402448eae24432dfcfc0dc4e5`

No WRF `test/em_b_wave` run or same-setup RK3 field/runtime comparison was performed. No production C++ source was changed.

## Next Actions

Red reviewed the final fixture and source/oracle mapping and found no blocker. Exact-head CI and PR review are next. Keep the variable-K claim limited to the helper contract unless a supported production caller path is established.
