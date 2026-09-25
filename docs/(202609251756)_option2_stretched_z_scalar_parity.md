# Option-2 stretched-Z scalar parity

Local timestamp: 2026-09-25 17:56:13 JST  
Source base: `11b2cdd9f1d1995817194ab1fcac0c9cd4e16744` (`agent/option2_stretched_eta`)

## Context

The scalar option-2 helper consumes the physical `rdzw` metric both at each
mass point and in its harmonic average to neighboring horizontal faces. The
existing source-extracted parity cases use uniform physical W-level spacing.
This fixture adds terrain plus mixed X/Y Fourier structure, spatially varying
Kh/rho and map factors, and positive nonuniform physical layer thicknesses.
Eta widths and their `dnw`, `dn`, `fnm`, and `fnp` coefficients remain uniform;
the case isolates stretched physical Z metrics rather than claiming a
nonuniform-eta coordinate.

## Changes

- Added option-2 case 10 to `tools/test_horizontal_diffusion_scalar.py` and
  `external/libtorch_wrf/sdirk3/tests/test_scalar_diffusion_contract.cpp`.
- The source Fortran metric producer derives `rdzw` from the stretched W-level
  heights. The C++ fixture supplies the corresponding layer metrics. Its
  `uniform-depth` counterfactual forces constant `rdzw` to demonstrate that the
  fixture resolves an incorrect uniform-depth assumption.
- The mixed Fourier amplitude is larger in case 10 so the existing fixed
  counterfactual threshold resolves the average-product mutation robustly.

## Validation status

- Python source-extraction driver at `-O0`: default REAL and `-fdefault-real-8`
  both pass all scalar producer/BC/consumer cases, including case 10.
- Focused case-10 Fortran/C++ parity with the C++ O2 and O3 binaries: FP32 and
  FP64 pass across all 192 cells. Fortran source extraction was compiled at O0
  and O2. The `avg-product`, `unit-maps`, and `uniform-depth` counterfactuals
  all exceed ten times the fixed parity budget in both precisions.
- The standalone `test_scalar_diffusion_contract` binary passes at O2.
- The full parity comparison exits earlier on existing case 8 (`terrain_mixed`)
  in FP32: maximum discrepancy `0.0509796`, against budget `0.000244141`. The
  focused case-10 run bypassed earlier option-2 comparisons but used the same
  extracted Fortran output and production C++ helper entry point.
- The required `test/em_b_wave` model run and same-setup RK3 field/runtime
  comparison were not performed for this test-fixture change.

## Next actions

- Review the case-8 discrepancy separately before relying on the full option-2
  parity command.
- Keep a true nonuniform-eta fixture open until `dnw`, `dn`, `fnm/fnp`, and the
  top extrapolation ratio are constructed from one consistent eta coordinate.
