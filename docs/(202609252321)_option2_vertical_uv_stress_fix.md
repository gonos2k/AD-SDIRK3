# Option-2 vertical U/V stress correction and active-WRF limit

Local timestamp: 2026-09-25 23:21:56 JST
Updated: 2026-09-26 00:13:00 JST

## Context and change

The previous `kvdif>0` WRF run stopped on 17-versus-16 staggered-array operations. Two unused density interpolations in the U/V vertical stress helpers caused the shape error; removing them lets the existing density already embedded in the stress be used. Option 2 now also uses the signed-eta tendency convention, interpolates density and physical `Kv` **separately** with Fortran's `fnm/fnp` weights, and applies the closed-W-face stress difference to every mass level, including the bottom and top. Option 1 keeps its prior path.

For option 2, the matched local operator is

`tau_W = -I_f(rho) I_f(Kv) D13_or_D23`,
`R_k = -g |1/dnw_k| (tau_{k+1} - tau_k)`, with `tau_0=tau_nz=0`.

The source-extracted Fortran routines are `cal_titau_13_31`, `cal_titau_23_32`, `vertical_diffusion_u_2`, and `vertical_diffusion_v_2`. The test feeds prescribed deformation into those routines; it does not prove the production deformation producer or complete WRF RHS is correct.

## Direct checks

- Actual-shape U `[17,16,18]` and V `[18,16,17]` helper calls no longer throw; prior helper failed at U dimension 2 and V dimension 0 even with K=0.
- `fnm=3/4,fnp=1/4`, positive varying rho/Kv, and exact-binary deformation discriminate weighted interpolation. The prior uniform-average source had U interior max error `0.0028253794` at unchanged `3e-6` tolerance; the weighted source has max error zero.
- Full-vertical Fortran extraction uses `kte=kde=17`, `ktf=16`, and compares all 16 mass levels. Before the boundary fix, U bottom/top errors were `7.061827/14.781460`, V `7.772178/15.893217`, while interiors matched. After the fix, U and V each match all **4,352 owned values** with max error zero; bottom, interior, and top errors are separately zero. K=0 controls are exactly zero.
- Full local CTest passed **102/102** after the source change. The option-1 Fortran parity script passed FP32/FP64 at O0/O2 after setting the macOS SDK path. Existing option-2 U geometry and W stress scripts passed. The Make archive has the exact expected 24 members.
- CI workflow now runs the new source-extracted vertical U/V oracle after the existing option-2 U/W oracle steps. CI for this candidate HEAD remains to be run.

Final tested C++ source SHA-256: `4774f534bbe5a3636a660b2993e5827986d58a1dcaedab42c6d774b7a274262c`. Fortran source SHA-256: `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea`. The all-level oracle log is `.validation/option2_vertical/uv_all_level_oracle.log` (SHA-256 `3cbc1010ad023974dd9fa5f5f1caec1aaddedcf334ea9353b34dad765f77bd2a`). The final CTest log is `.validation/option2_vertical/full_ctest_after_boundary.log` (SHA-256 `8d77d870a49a54d3e700ad10f21c9d5e6960a8a32e3c24efb43addde099ce8b8`). Focused Graphify source mirror SHA matches this source; graph JSON SHA-256 is `b01e72cdfd9d4e65a0a111c0b73f571a29f29054a74a85100c4de196b56a628b`.

## Same-setup WRF result: execution passes, numerical acceptance fails

An incremental relink reused the unchanged full-built WRF object set from integration revision `11b2cdd9`. In the dry, single-rank `em_b_wave` case (`diff_opt=2`, `km_opt=1`, `khdif=1000`, `kvdif=10`, 60-second step), both PC2 and RK3 reached `SUCCESS COMPLETE` with the same executable and archived initial field. All 174 floating-point output variables were finite. This is not a clean full-WRF rebuild.

The **large wind difference predates** the endpoint correction. With the pre-boundary C++ source SHA `318dd63e…`, PC2 minus same-executable RK3 had final U RMS difference `26,579 m/s`, maximal difference `218,619 m/s` at interior level k=10. After the endpoint correction, the same-setup difference is U `29,807 m/s`, V `8.088 m/s`, W `2.621 m/s`, PH `19.613 m2/s2`, T `0.3981 K`, MU `1.511 Pa` RMS. The endpoint correction additionally changes top/bottom PC2 wind levels. The solver's finite output and convergence report therefore **must not** be treated as forecast-quality acceptance. Comparison and run receipts are under `.validation/option2_vertical_weighted_wrf_postboundary/`; the pre-boundary receipt remains under `.validation/option2_vertical_weighted_wrf/`.

A diagnostic run using the same executable and `WRF_SDIRK3_DEBUG_LEVEL=3` produced byte-identical PC2 output. It confirmed physical `rdz_3d_` exists at approximately `0.001 m^-1`, yet Step 10 calls `compute_defor13/23` without the current-stage option-2 geometry. Their legacy C-grid shape guard rejects the mass-grid metric width/height against staggered U/V, so the vertical-shear term falls back to `2*rdnw` (eta inverse, order 15–96) instead of inverse physical height. This is a **confirmed source-path scaling error and strong explanation candidate** for the large wind growth; its exact share of the forecast difference needs a same-state component comparison. The Step 9 `use_3d=1` log is for horizontal diffusion and does not validate Step 10.

Source audit also identified Step 10 using `p/(Rd*theta)` instead of the current-state physical density and passing `kvdif` to W vertical mixing where Fortran uses horizontal `xkmh`. Both are separate open coefficient/state-definition mismatches. They should be repaired with the same stage-local physical state, then rerun the fixed-state oracle and the WRF comparison. No tolerance relaxation or empirical rescaling is justified.

## Remaining scope

The local U/V stress-consumer contract is closed for this fixture. The complete active vertical-diffusion producer-to-consumer chain, fixed-state Fortran RHS parity, terrain/variable-map/variable-coefficient cases, MPI tiles, time order, whole-step adjoint, and numerical WRF acceptance remain open. No operational forecast or data-assimilation qualification follows from this change.
