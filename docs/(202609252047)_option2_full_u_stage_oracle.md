# Full option-2 U-X producer-consumer Fortran oracle

Local timestamp: 2026-09-25 20:47:24 JST

## Context and scope

This increment extends the option-2 U-X oracle from an operator-only fixture to a compiled producer-consumer path. At runtime, the test extracts the exact unchanged bodies of `compute_diff_metrics`, `cal_deform_and_div`, `cal_titau_11_22_33`, `cal_titau_12_21`, and `horizontal_diffusion_u_2` from `dyn_em/module_diffusion_em.F`, then compiles them into a small standalone Fortran module.

The driver forms stage PH/PHB, runs the source metrics routine, prepares only the needed periodic-X and Y-constant halo values analytically, runs the source deformation routine, then runs the source stress and U diffusion routines. It does not call or validate WRF's `set_physical_bc3d`, halo exchange, or complete RK stage. The fixture's selected Y rows are constant and its U-X calculation needs only the provided neighboring rows; this keeps the boundary treatment bounded and explicit.

The old operator-only reference remains in the same test as a separate control. It injects D11 from the source-checked Python equation transcription and supplies analytic `zx`/`rdzw`; it does not consume full-chain producer outputs. The full path compares compiled metric and D11 outputs against independent Python source equations, then compares raw U tendency against the C++ helper and the Python operator reference.

## Fixture and ranges

The fixture uses Nx=8 periodic-X unique mass columns, Ny=6, four mass layers plus the top W level, `H=100*cos(2*pi*i/8) m`, `U=[0,0,0,1]`, `K=2`, `rho=1`, `dx=dz=1000 m`, signed `dnw=-1/4`, and unit maps. Fortran mass columns are i=1..8; stage PH is filled through periodic halos i=0/9 (and backing extent through i=10). Y halos repeat the same column. PHB is zero.

Metrics run with the east endpoint included (`ite=ide=9`) so the exact metrics source writes both periodic `zx` endpoints. The deformation routine runs over i=1..8 and j=2..6, producing D11 from the actual source routine and computing the north row of defor12 that U2's cross-stress halo may read. The U2 consumer remains restricted to i=2..8; those map to C++ owned U faces 1..7, while i=1/9 seam slots stay unmodified and are checked as zero. The selected output is Fortran k=2.

A minimal test module supplies `g`, `P_m11/P_m12`, `P_r12/P_r13/P_r23`, and only the `grid_config_rec_type` fields referenced by the extracted routines. It uses `sfs_opt=0` and `m_opt=0`. `defor12` is produced by `cal_deform_and_div` and checked against zero for the Y-constant U-X field; the test never assigns it to zero after deformation.

## Source and executable provenance

- Worktree HEAD during the run: `c85887f5d79565ff2ff3244b30ea2680e339d62d` (the oracle changes were uncommitted during this validation).
- `dyn_em/module_diffusion_em.F` SHA-256: `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea`.
- Concatenated exact extracted routine bodies SHA-256: `65257c3d8c9b2bb82f20ac3329e79b246993d88d1d2435bbf7e47dff7f6f03c9`.
- C++ implementation SHA-256: `811c7f8f7ce206c57bda617efa5ce8e4f3994741c4cb21ccb83414799898923a`.
- C++ test SHA-256: `22b68e520ae521bfd4f0ce4380883bef5245631898e1b7369c0163c38e0f49af`.
- Python test SHA-256: `0ffdc6d71c2f45efc544e8289741b004704e66fb54edd0a1c750ba11aaf9b9da`.
- C++ reference executable: `/tmp/sdirk3-option2-momentum-impl-build/test_scalar_diffusion_contract`, SHA-256 `106bbd15f6cf7fa17d45563b5b568a26e4ea36bdb25a65f85d7426514e25cefe`.
- Fortran compiler: GNU Fortran 15.2.0 on Apple arm64 Darwin. Four builds use FP32/REAL64 and `-O0`/`-O2` (`-fdefault-real-8` for REAL64).

## Validation

Command:

```sh
python3 -m py_compile external/libtorch_wrf/sdirk3/tests/test_option2_momentum_geometry.py
python3 external/libtorch_wrf/sdirk3/tests/test_option2_momentum_geometry.py \
  --binary /tmp/sdirk3-option2-momentum-impl-build/test_scalar_diffusion_contract
```

All four full-chain and operator-only Fortran runs passed. The metric and deformation budgets were declared from the fixture scales before inspecting results: 32 machine epsilons for FP32 and 256 machine epsilons for REAL64, scaled by the maximum terrain slope, reciprocal layer depth, or D11 scale. The existing raw tendency tolerance remains `2e-6 * max(1e-12, signal)`.

| Quantity | FP32 max error | FP32 budget | REAL64 max error | REAL64 budget |
| --- | ---: | ---: | ---: | ---: |
| `zx` | `2.5612e-8` | `2.70e-7` | `6.4532e-16` | `4.02e-15` |
| `rdzw` | `2.8033e-10` | `3.81e-9` | `4.3368e-19` | `5.68e-17` |
| D11 | `4.9011e-12` | `2.70e-10` | `8.1315e-20` | `4.02e-18` |
| D12 | `0` | `2.70e-10` | `0` | `4.02e-18` |

The `-O0` and `-O2` values matched for each precision. Full-chain FP32 raw U differed from C++ by at most `5.7946e-11`; the operator-only FP32 control differed from C++ by `1.0160e-10`. Both paths' seam errors were exactly zero. The full-chain raw output remained within the existing Python operator reference tolerance (`4.8712e-11` max FP32 error; `7.3184e-19` REAL64 error). Result: `PASS option-2 U-X compiled Fortran geometry parity`.

Two temporary, uncommitted negative controls were rejected as expected:

- A compiler wrapper injected `defor11=0.` immediately after the exact `cal_deform_and_div` call and before both `D11_RAW` diagnostics and the consumer's `horizontal_diffusion_u_2` call. Thus the mutated producer array fed both checks. D11 error was `7.0711e-5` against the predeclared FP32 budget `2.6974e-10`; full raw U error was `1.67467e-4` against the existing tolerance `3.35e-10`; C++ versus mutated full U also differed by `1.67467e-4`. The operator-only path remained at its baseline `1.0160e-10` C++ difference. Wrapper SHA-256: `8f96c3c3a5c6e8e521a1b9cb3d122ad35e5b166acfddeab67d7f26cca909acfa`; injected generated Fortran source SHA-256: `921fe0b15dbcdc11f4091e91aa340f10d670a31b7c47793f3fab6fcc7c4801a2`; log SHA-256: `785bf0141c3f8dfb4dc21ea17549599d3d10b953dc9a89943a59c2a9d18a7b53`.
- A binary wrapper removed C++ seam outputs; the test failed with `FAIL missing C++ seam outputs`. Wrapper was not committed. Log SHA-256: `42cbf3dd1c1d1e43d4b8db852f85c4fa439e8e414c7fce989178a75eb26cf8fd`.

The normal test log SHA-256 is `319d842e169b6d26d338be37b06362ee565284540bf693dc75c4b2240791549d`; the source was unchanged by this documentation-only follow-up. `git diff --check` and Python syntax compilation passed. No WRF `test/em_b_wave` model run or same-setup split-explicit RK3 field/runtime comparison was performed.

### Mutation follow-up

Local timestamp: 2026-09-25 21:50:34 JST. The producer-array mutation was repeated after review. It changed only the generated temporary Fortran driver; committed test/source and budgets were unchanged. It was compiled by GNU Fortran 15.2.0 and run against C++ executable SHA-256 `106bbd15f6cf7fa17d45563b5b568a26e4ea36bdb25a65f85d7426514e25cefe` and module source SHA-256 `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea`.

## Graphify

Before editing, the focused Graphify corpus had 374 nodes and 1469 edges. After the implementation it was freshly extracted from six byte-for-byte copies of the current worktree inputs and had 374 nodes and 1470 edges. SHA-256 checks confirmed the copied `module_diffusion_em.F`, RK caller, boundary module, Python test, C++ implementation, and C++ test all matched the worktree files. The graph shows the RK caller edges to `compute_diff_metrics` and `cal_deform_and_div`, plus `horizontal_diffusion_2` to `horizontal_diffusion_u_2`. The graph is stored under `/tmp`; source correctness claims are based on source bodies and numeric validation, not graph edges.

## Next actions

This remains a standalone interior-stage oracle with analytically prepared halos. If boundary implementation coverage is needed, add a separate bounded extraction/test of WRF `set_physical_bc3d` and the associated caller exchange sequence. Do not treat the present result as an end-to-end WRF model or boundary-condition test.
