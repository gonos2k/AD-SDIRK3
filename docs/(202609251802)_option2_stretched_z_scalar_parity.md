# Option-2 stretched-Z scalar parity

Local timestamp: 2026-09-25 18:02:33 JST
Source base: `00979b1d09e8ac2fd0a4fc850bd5600d7fad4c9a` (`agent/option2_stretched_eta`)

Final tested source revision: `f524c4042def50674c0ed058e27c297a85dd6ed4`.
The local toolchain was Apple Clang 21.0.0 and GNU Fortran 15.2.0. The
independently rerun O2 and O3 C++ test binaries have SHA-256
`ad7661c6b2b54428a422c2cd97cceace9e4246c09a5cecdb922ff08124f689db`
and `8d2a8b3a47d87f8e5f2ad03bbe9b36f454005b40e4d7f885afcd4d08f9867fc9`.
Their unfiltered parity logs are byte-identical (SHA-256
`925e5d94e369daea0df8577ae46a3df2e34142f8e342182beca4dfa971f132df`).

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
- The unfiltered `test_horizontal_diffusion_scalar.py` parity driver passes
  against both C++ O2 and O3 binaries. Existing uniform-Z case outputs retain
  their original heights; case 10 uses its separate W-level layout.
- The no-argument `test_scalar_diffusion_contract` suite passes with both O2
  and O3 binaries.
- The required `test/em_b_wave` model run and same-setup RK3 field/runtime
  comparison were not performed for this test-fixture change.

## Next actions

- Keep a true nonuniform-eta fixture open until `dnw`, `dn`, `fnm/fnp`, and the
  top extrapolation ratio are constructed from one consistent eta coordinate.
