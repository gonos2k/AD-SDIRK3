# Option-2 vertical stress and current-stage geometry validation

Local timestamp: 2026-09-26 08:59:21 JST (+0900)

## Context and change

This candidate is stacked on draft PR #238. The former nonzero-vertical-diffusion WRF path first failed at 17-versus-16 staggered shapes. After that shape repair, `kvdif=10` could complete a one-minute PC2 run but its U field grew to roughly 29,807 m/s RMS while a same-input RK3 run remained near 1.74 m/s RMS. A direct flat-shear fixture identified the dominant source-path mismatch: the physical W-level inverse height spacing was about 0.001 m⁻¹, but `compute_defor13/23` rejected its mass-grid horizontal extents against staggered U/V and substituted `2*rdnw`, about 15–96 in the model run. A prescribed 1,024 m layer with a 0.5 m/s velocity difference yielded the Fortran shear 0.00048828125 and the old C++ path 10. Passing the current-state stage metric gives the Fortran value exactly.

The correction keeps the stage-local `zx/zy/rdzw/rdz` and physical density diagnosed in Step 9 within the same RHS evaluation and supplies them to Step 10. U/V vertical stress interpolates density and `xkmv` separately with WRF `fnm/fnp`, uses the signed-eta tendency convention, and includes the bottom and top mass layers. W vertical stress uses current-stage physical `rdzw` for D33, `xkmh` rather than `xkmv` for the stress, and signed `dn` represented by negative `g*|1/dn|` for divergence. The default vertical scalar coefficient in the supported constant-coefficient option-2 path is `xkhv=3*xkmv`, from WRF's `prandtl=1/3`; an explicit supplied coefficient is outside this bounded native path. The vertical block activates when its relevant U/V, W, or scalar coefficient is active. Option 1 retains its previous branch.

A shared RHS preflight rejects unsupported native option-2 configurations even when horizontal Step 9 would be skipped. In particular, nonempty supplied `Kh_mom`, `Kv_mom`, or `Kv_scalar` cannot bypass the dry `km_opt=1`, complete one-rank-tile contract merely because namelist coefficients are zero. `km_opt=2` remains rejected even when namelist `khdif=kvdif=0`, because WRF selects `tke_km` and diagnoses a nonzero coefficient from its state; those namelist values do not make the native diffusion operator a no-op.

## Direct validation

- Actual-shape U and V stress helpers no longer hit the 17-versus-16 error. With asymmetric `fnm=3/4`, `fnp=1/4`, varying positive density and diffusivity, and prescribed deformation, the extracted and compiled Fortran U/V stress plus vertical-diffusion routines match C++ at all **4,352 owned mass points per component**, including separately checked bottom and top layers, with maximum error 0. K=0 controls are exactly zero. Before the endpoint repair, U bottom/top errors were 7.061827/14.781460 and V 7.772178/15.893217.
- The actual Step-10 W six-argument mixing overload matches extracted and compiled Fortran `cal_titau_11_22_33` plus `vertical_diffusion_w_2` over **4,335 interior W points**, maximum error 0. The same-state four-argument old-call negative control has maximum error 19.699849; W K=0 is exactly zero. This fixture prescribes D33 and integrates W to match it, so both paths use the same physical state.
- The direct U/V shear fixture reports old no-stage helper output 10 versus the source flat-shear value 0.00048828125; with explicit stage `rdz`, both helpers return that value exactly. It isolates the metric path, not a complete WRF RHS.
- Local CTest passed **102/102** after the final `km_opt=2` zero-namelist rejection test; that test and all three supplied-K rejection checks appear as passes in `Scalar_Diffusion_Contract`. The option-1 Fortran parity script passed FP32/FP64 at O0/O2. The Make archive passed exact 24-member manifest checking, and the new U/V and W source-extracted oracles are wired into the existing CI workflow. Exact-head remote CI remains pending until the draft PR is pushed.

## Same-setup WRF comparison on the exact source

The current implementation file SHA-256 is `ddec376d3c398502571d8d43ea2d97c764e2e9eaea2d19b81c706c8b0389dba5`. An exact-source Make rebuild produced archive SHA-256 `397c8da007e3757e2d3012f7b183ad7ca150a49ff68b2b0342effb8161978005`; its 24 members match the manifest. It was linked with the unchanged full-built WRF object set from integration revision `11b2cdd9` into a separate executable, SHA-256 `87f24858113ab0e42ca339cc8dbec8bc3ebae05a302e315f431556c5eb582405`. This is an incremental relink, not a fresh full-WRF rebuild of this candidate. The copied archived initial field has SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`. Both integrators used that executable and input; within each coefficient setting, their namelists differ only in `time_integration_scheme` (6 for PC2, 0 for RK3).

The dry `em_b_wave` configuration is one MPI rank, one thread, `diff_opt=2`, `km_opt=1`, `damp_opt=0`, `khdif=1000`, `time_step=60 s`, one step. Four runs, with `kvdif=10` or `0` for PC2 and RK3, all reported `SUCCESS COMPLETE`; PC2 reported three converged implicit stages and zero failed stages. Each output contains two records and 174 finite floating-point variables (378,352 float values). Initial U/V/W/PH/T/MU frames are identical within each PC2–RK3 pair.

| Coefficients and comparison | U RMS | V RMS | W RMS | PH RMS | T RMS | MU RMS |
|---|---:|---:|---:|---:|---:|---:|
| `kvdif=10`: PC2 − RK3 | 0.00209013 m/s | 0.00002227 m/s | 0.00063003 m/s | 0.291640 m²/s² | 0.009957 K | 0.041502 Pa |
| `kvdif=0`: PC2 − RK3 | 0.00208892 m/s | 0.00002209 m/s | 0.00008734 m/s | 0.032548 m²/s² | 0.00003122 K | 0.041493 Pa |

The `kvdif=0` case now exercises the W vertical `xkmh=khdif` path even though U/V vertical `xkmv` is zero. The same-executable PC2 `kvdif=10` minus `kvdif=0` difference is nonzero (W RMS 0.00063676 m/s, T RMS 0.0101404 K), confirming the vertical coefficient setting changes the prediction. The one-run WRF internal main times for `kvdif=10` are 0.46390 s PC2 and 0.08047 s RK3; for `kvdif=0`, 0.42494 s and 0.09054 s. These timings are not repeated equal-accuracy performance measurements.

The exact-source output is close to the preceding candidate's result but not byte-identical: for `kvdif=10`, the two PC2 U outputs differ by RMS 1.94e-9 m/s and maximum 1.19e-7 m/s; PH differs by RMS 7.40e-6 m²/s². This is why the new executable and output hashes are retained rather than inheriting the earlier receipt.

## Provenance and limits

The exact-source link receipt, four run receipts, output hashes and field statistics are in `.validation/option2_vertical_exact/`. The field-comparison JSON SHA-256 is `eb7ae0119df6ec5df9cf3ee1b7eb4aa22d3d54f6ff90ad81b5001ee8f7ff7541`; link receipt SHA-256 is `10e6267d35224bcf5b8797590680c47f7545afc4521c306efda86180586604cd`. The final 102/102 CTest log SHA-256 is `7b1d32b8cc348527be1c66c8ceac0d0f569098873874c8e42a1302ac8423385b`. The focused Graphify mirror's implementation, header and test hashes match the current source; refreshed graph JSON SHA-256 is `84abe6cccd5690904a55e8ae604ce5a110d36a19fd5b25094c8554081305fc0d`. Graph edges are navigation aids, not numerical evidence.

These checks close the bounded option-2 vertical-stress and stage-metric defects. They do **not** establish a predeclared forecast-error tolerance, third-order time accuracy, long-run stability, multi-rank halo parity, variable/physics-supplied diffusion coefficients, terrain/general-map U/V/W parity, or the full active-step adjoint. PC2–RK3 closeness is a diagnostic comparison, not an exact-solution error. No full clean WRF rebuild was performed after this candidate change.

Next: run exact-head remote CI for the draft PR, then evaluate a fixed-state complete U/V/W/scalar RHS against Fortran under the declared coefficient/metric contract. Only after that should the broader temporal, budget and adjoint checklist be advanced.
