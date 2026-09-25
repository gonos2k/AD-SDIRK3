# Variable-coefficient option-2 scalar metric stencil parity

Timestamp: 2026-09-25 13:53:27 JST (+0900)

## Context and change

This test-only candidate is stacked on PR #231 head `59c863faed1446b7aa9637d9396e56e1c1676e75`. The existing option-2 scalar helper and bounded production Step 9 path are validated primarily with constant Kh, unit map factors and density varying by level. Fortran `horizontal_diffusion_s` separately averages physical Kh and rho onto faces and uses direct U/V face maps plus distinct mass-point X/Y maps. Constant or unit inputs cannot distinguish `avg(K)*avg(rho)` from `avg(K*rho)` or a misplaced map factor.

Case 9 extends the existing source-extracted Fortran versus production-library private-helper comparison. It prescribes positive, different spatial and vertical patterns in Kh and rho, independently varying positive nonunit `msftx`, `msfty`, `msfux` and `msfvy`, and nonzero mixed X/Y terrain and Fourier scalar structure. The Fortran driver fills periodic-X and symmetric-Y halos analytically. The C++ contract receives independently constructed matching inputs; all 192 owned mass cells, including vertical end layers and physical horizontal edges, are compared in FP32 and FP64. This case retains uniform eta spacing (`dnw=dn=-0.25`, `fnm=fnp=0.5`); stretched eta remains separate.

## Validation

The existing `tools/test_horizontal_diffusion_scalar.py` extracts actual `compute_diff_metrics`, `set_physical_bc3d` and `horizontal_diffusion_s` bodies, compiles them with GNU Fortran, and invokes the production-library C++ private helper. Case 9 has signal `0.583196` in both precisions. The FP32 maximum C++–Fortran difference is `4.76837e-7` against the predeclared `2.44141e-4` engineering budget; FP64 is `1.11022e-15` against `4.54747e-13`. Replacing the separate face averages with `avg(K*rho)` gives a mismatch of `0.0048048`, and replacing map factors by unity gives `0.201493`; both exceed ten precision budgets. Existing flat, hybrid and terrain controls continue to pass. The complete local transcript is `.validation/option2-variable/parity.log`, SHA-256 `80c0174d0333bd07feef49ff52096ccd28346d17db7e3089f3f6281f6e07c4e4`. The affected `Scalar_Diffusion_Contract` CTest passed 1/1. No production numerical formula changed.

The focused Graphify corpus was verified against this worktree before editing and refreshed after the changed test files were copied byte-for-byte: 738 nodes, 1,542 edges and 27 communities. Its extracted edges aided navigation; the Fortran source and execution above supply numerical evidence. No WRF model run or new RK3 field/runtime comparison was performed for this test-only change; the same-source reduced one-step comparison remains PR #230's evidence.

## Remaining checklist

This closes the **private helper's variable K/rho/map stencil subcheck**, not the production ABI or physical coefficient ownership. The bounded Step 9 path in #230 still accepts only constant `km_opt=1` Kh and a dry single tile; it does not receive space-varying diagnosed `xkhh` or moist rho, nor does it support MPI/internal tile halos. Stretched eta, other physical boundaries, option-2 U/V/W stress, full time order and full observation-path adjoint remain open. The next production extension must specify when Kh/rho are evaluated within each ARK stage and whether their state dependence is differentiated; passing a tensor of the right shape without that contract would not complete parity.
