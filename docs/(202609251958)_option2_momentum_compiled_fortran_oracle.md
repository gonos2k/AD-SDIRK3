# Option-2 U-X compiled Fortran operator oracle

Local timestamp: 2026-09-25 19:58:49 JST

## Context

The stage-terrain regression previously compared the C++ private U helper with a Python transcription of WRF's equations. This increment compiles the exact `cal_titau_11_22_33`, `cal_titau_12_21`, and `horizontal_diffusion_u_2` routine bodies extracted at test runtime from `dyn_em/module_diffusion_em.F`. It exercises the unmodified U diffusion routine, including both normal-stress divergence and the terrain-slope correction. The cross-stress input `defor12` is zero, as required for the isolated U-X fixture; its exact stress routine still runs.

D11 remains independently computed by the existing Python transcription of the source-checked `cal_deform_and_div` equation. The test does not compile `compute_diff_metrics` or `cal_deform_and_div`, and therefore this is an operator-level oracle with source-grounded geometry and deformation inputs, not an end-to-end Fortran stage. The test module pins their equations and the production caller chain. It supplies a minimal config type and WRF constants to make the extracted routines standalone; with `sfs_opt=0` and `m_opt=0`, the production plain-closure branches execute.

## Source and executable provenance

- Parent production revision: `d90e5faed8c38aa5cae9e4aeb58b0c9660e5c9e4`; oracle change: `ae6bd3d1b4bc3b300f1989c5dfdf0777e609fe2a`. The later workflow-only commit does not alter these operator sources.
- `dyn_em/module_diffusion_em.F` SHA-256: `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea`.
- Extracted routine-body SHA-256: `df842e5eac37ec5cc07c8e5af117dd38cacbae28523c91df338eb39466130e22`.
- C++ implementation SHA-256: `811c7f8f7ce206c57bda617efa5ce8e4f3994741c4cb21ccb83414799898923a`.
- C++ test SHA-256: `22b68e520ae521bfd4f0ce4380883bef5245631898e1b7369c0163c38e0f49af`.
- Python oracle SHA-256 after the finite-value gate: `a5cbc67597af53ddec4070a45fb783866e37296a7b3d66fd54cfde78a8f7c68c`.
- C++ reference executable: `/tmp/sdirk3-option2-momentum-impl-build/test_scalar_diffusion_contract`, SHA-256 `106bbd15f6cf7fa17d45563b5b568a26e4ea36bdb25a65f85d7426514e25cefe`.
- Fortran compiler: GNU Fortran 15.2.0, Apple arm64 Darwin; REAL64 cases use `-fdefault-real-8`.

## Validation

Command:

```sh
python3 external/libtorch_wrf/sdirk3/tests/test_option2_momentum_geometry.py \
  --binary /tmp/sdirk3-option2-momentum-impl-build/test_scalar_diffusion_contract
```

The fixture is Nx=8 periodic X, `H=100*cos(2*pi*i/8) m`, `U=[0,0,0,1]`, `K=2`, `rho=1`, `dx=dz=1000 m`, `dnw=-1/4`, and unit maps. All owned U-X cells and both unowned seam slots are reported.

- Compiled extracted Fortran vs Python equation transcription: max absolute errors `9.24e-11` for FP32 `-O0` and `-O2`; `6.78e-21` for REAL64 `-O0`; `2.71e-20` for REAL64 `-O2`.
- C++ raw U tendency vs compiled FP32 Fortran: max absolute error `1.02e-10`.
- C++ and compiled Fortran seam error: exactly zero.
- Result: `PASS option-2 U-X compiled Fortran geometry parity`.
- Python syntax check and `git diff --check` pass.
- The existing `core-linux` workflow now invokes this compiled parity script after the option-1 momentum source comparison; hosted CI for that exact candidate is pending.

At 2026-09-25 20:10:40 JST, the normal local rerun still passed (log SHA-256 `81aa1060c6dbded1bacb4aa07d080e3c8cd22322e43eb9685f7bb9cdc5077349`). A temporary wrapper replaced one non-first owned C++ output with NaN; the test rejected it with `FAIL non-finite C++ output at (1, 1, 2)` (negative-control log SHA-256 `c5ea7322694d7951ec3ae43b043cefc9b10ebe6312c0a0348f8e9a9b560158c1`). The wrapper was not committed. All C++, Python-equation and compiled-Fortran values are now checked for finiteness before reducing errors.
- No WRF `test/em_b_wave` model run or same-setup split-explicit RK3 field/runtime comparison was performed; this oracle validates the isolated operator fixture only.

## Graphify

Before editing, Graphify extracted a focused corpus containing byte-for-byte copies of the source file, C++ implementation, C++ test fixture, and Python oracle from this revision. The initial graph had 317 nodes and 1288 edges. The graph was refreshed after the oracle change (320 nodes, 842 edges, 19 communities). This focused graph was kept under `/tmp`; graph navigation informed caller tracing, while all equation claims were checked against the source and compiled output. The temporary corpus does not embed a revision field; the exact source SHA values above tie its copied inputs to the stated worktree revision.

## Next actions

For an end-to-end stage oracle, compile and execute `compute_diff_metrics` and `cal_deform_and_div` as well, including WRF-equivalent periodic halo and physical boundary handling. That extension is outside this increment. The test also requires a working Fortran compiler; on macOS it forwards the active Xcode SDK path to GNU Fortran's linker.
