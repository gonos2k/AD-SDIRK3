# Assembled six-field RHS comparison and pressure-induced W difference

Local timestamp: 2026-10-12 18:17:52 JST (+0900)

Base main: `21398d601c70d4328d74393e318bc29dd98bc367`. Observer source commit and build HEAD: `2f32859d545bf5dc3e287653d8ebb1cb504df32c`. This closes the bounded PR301 goal of measuring assembled same-call RHS differences for `U,V,W,PH,THM,MU` at the PR300 preserve-W input state. Coverage is complete for this one first-call comparison; numerical identity, forecast accuracy, and a universal acceptance threshold are not asserted.

## Capture and build scope

The two calls used the corrected time-zero input `fc37c8c0c8be8053a16e0d3424609d496f4a023c6c17ee3a06371970639d14df`, with `use_input_w=T`, `h=15 s`, one step, one MPI rank and one OpenMP thread. Both returned zero and reached the success marker. The reused C++ archive is `21f7bc79a131f4608f506259b7c0434f2350749486774f14aea260cc69910565`; the new executable is `78b3c01abcfc3b3e8286930e62198855891ff50c3bd1dd4026cd6fef10fd43d7`.

| Method | Step | Rank/thread | Wall time |
| --- | --- | --- | ---: |
| Fortran RK3 | h=15 s, one step | 1/1 | 2.25127 s |
| C++ mode-3 ARK | h=15 s, one step | 1/1 | 1.23018 s |

These process times include initialization and output for a very short call; they do not compare equal-accuracy cost.

The only production-tree source change was observer serialization in `dyn_em/solve_em.F` (SHA `888b81bcb21791fc89e08d739162f7cedfbff8679cba5453010257a3f31a9be8`). Full snapshot V2 appends `rw_tend`, `ph_tend`, `t_tend`, `c1f`, and `c2f`; the V1 payload bytes after the version word are unchanged. This is a capture-format change only: no Fortran/C++ equation, PDE, pressure arithmetic, precision mode, ABI, or runtime option changed. The observer remains behind `WRF_PARITY_RK_TEND_DUMP=1` and the existing first-timestep/RK1, one-rank/tile, dry, periodic-X/symmetric-Y gate.

Two pre-build attempts failed before producing an object or running a model. The seed guard mistakenly rejected the intended donor `libwrflib.a`; the first preprocessing attempt used blank WRFPLUS/NMM macros and failed before the Fortran compiler. Both logs are preserved. The reviewed historical preprocessing/compile/link commands were then run with explicit zero-valued macros. The successful build regenerated only `solve_em.o`, replaced its one archive member, and relinked; the C++ archive and `module_em` interface remained unchanged. The successful build receipt pins the source, object, archive, executable, command gate and log hashes.

The early C++ `[CONFIG EFFECTIVE] non_hydrostatic=off, do_curvature=off` log is emitted before WRF forwards those dynamics flags. Later setter messages report both as true before tile construction; the actual U-PGF capture header records `native_nonhydrostatic=1`, confirming that branch at the captured call. The source/log order supports curvature enabled, but the capture does not serialize a per-call curvature bit or establish a nonzero curvature contribution.

Fortran V2 is written after `rk_addtend_dry` and optional `spec_bdy_dry`, before later `set_physical_bc3d` updates and before the acoustic step. It therefore records assembled first-call large-step tendencies at that stage point, not post-boundary tendencies or a full timestep update. C++ uses its existing first `Full` result. The V2 capture is 627,340 bytes: 60-byte header, unchanged 478,428-byte V1 array payload, and a 148,852-byte append of 37,213 float32 values. The raw V2 parser validates the schema and finite values.

## State, call and control gates

All six actual call states match the corrected source-60 `U,V,W,PH,THM,MU` arrays bitwise in both solvers. Fortran and C++ states match bitwise, and the PGF call state matches each solver's same-run Full state. The input is a cold time-zero initialization, not a restart with retained solver history. NetCDF `THM` supplies prognostic `t`; diagnostic NetCDF `T` is not substituted. Startup reconstructs call-time `P/AL/ALT`, so file `P` is not a prognostic-state equality condition.

The captured U-PGF header records `native_nonhydrostatic=1`. Early C++ configuration lines show defaults before WRF's explicit flag setters; later setters forward `non_hydrostatic=true` and `do_curvature=true` before tile construction. The per-call PGF bit directly confirms nonhydrostatic mode. Curvature is supported by source/log order but is not serialized as a per-call bit, and no particular nonzero curvature contribution is inferred.

## Physical rates and measured differences

Fortran rates were converted using the source equations and captured mass, metric, `MU` product-rule and forcing inputs. With `mu_dot = DMDT + mu_tend` (`DMDT` is signed vertical-integrated RU/RV mass-flux divergence), U/V use the established coupled conversions `u_dot=(ru_tend*msfuy-u*c1h*mu_u_dot)/(c1h*muu+c2h)` and the analogous V equation using `msfvx/muv`. The additional conversions are `w_dot=(msfty*rw_tend-w*c1f*mu_dot)/(c1f*mut+c2f)`, `t_dot=(msfty*t_tend-t*c1h*mu_dot)/(c1h*mut+c2h)`, and `ph_dot=msfty*ph_tend/(c1f*mut+c2f)`; THM's product term uses prognostic perturbation `t`, without adding the 300 K base offset, and PH has no PH-times-mass-rate product term. C++ Full outputs are already state-space rates. `MU` uses physical mass cells. W and PH use vertical interfaces 1–16: the fixed bottom plane is excluded and the dynamic top (`top_lid=false`) is included. THM/t uses all mass levels. The signed fieldwise difference is C++ minus Fortran; calculations widen captured float32 values before subtraction.

| Field | Owners | RMS difference | Max absolute | Relative RMS | Units |
| --- | ---: | ---: | ---: | ---: | --- |
| U | 4,096 | 8.39972e-8 | 3.74253e-7 | 2.28190e-4 | m/s² |
| V | 3,840 | 8.48934e-8 | 3.33823e-7 | 1.48330e-3 | m/s² |
| W | 4,096 | 1.20095e-5 | 4.01232e-5 | 4.48498e-1 | m/s² |
| PH | 4,096 | 3.80669e-5 | 9.82649e-5 | 1.35226e-2 | m²/s³ |
| THM/t | 4,096 | 2.50972e-7 | 1.35600e-6 | 1.72089e-2 | K/s |
| MU | 256 | 3.36932e-8 | 1.25158e-7 | 8.93155e-6 | Pa/s |

Relative RMS is the difference RMS divided by the Fortran signal RMS. The bottom PH plane is fixed at zero in both solvers. The 3,840 interior PH owners have RMS difference `3.93153e-5 m²/s³`; the 256 dynamic-top PH owners have `5.05888e-10 m²/s³`. The 256 dynamic-top W owners have `1.00811e-5 m/s²` RMS difference.

An independent raw-array audit propagated the captured float32 operand half-ULPs through the field conversions and included C++ output storage half-ULP. The resulting RMS intervals and counts outside those intervals were: U `9.27173e-11` (4,094/4,096); V `1.42732e-11` (3,839/3,840); W `6.61548e-12` (4,096/4,096); PH `7.03014e-10` (3,897/4,096); THM `5.19394e-11` (3,778/4,096); MU `7.23404e-8` (10/256), in each field's units. These intervals bound only propagated raw-input storage uncertainty, not internal arithmetic; they are not acceptance thresholds or total error bounds.

The W difference is dominated by the native pressure-buoyancy term. With common captured mass, coefficients and geometry, reevaluating the existing dry `pg_buoy_w` source expression in FP64 to each solver's captured pressure gives pressure-only W delta RMS `1.2009458476e-5 m/s²`, versus full W delta `1.2009458687e-5 m/s²`; the residual is `4.30667661e-11 m/s²` RMS and cosine similarity is `0.999999999994`. The input is dry (`QVAPOR=0`, so `cqw=0`). This is an offline component calculation from captured pressure operands, not an EOS rerun or a finding about why the pressures differ. It supports pressure-term localization for this W difference without choosing a pressure correction, precision downgrade, or production improvement.

PH and THM differences remain measured and unassigned. Their relative RMS differences are 1.35226% and 1.72089%, respectively. The six-field comparison establishes coverage, not a numerical pass: no fixed per-field budget was selected. The raw-storage envelope does not bound internal Fortran/C++ arithmetic.

## Relation to earlier U and residual measurements

The preserve-W PR300 U component accounting remains separately bound: `R=DeltaFullU-DeltaHdiff-DeltaVdiff` has RMS `8.39940782e-8 m/s²`; the measured U-PGF delta is `8.39314798e-8 m/s²`; `R-DeltaPGF` is `3.06910511e-9 m/s²` RMS. This is a component budget with a measurable, unassigned remainder, not complete RHS parity or forecast improvement.

The old PR298 mixed-time post-PGF residual and the new preserve-W residual differ by only `1.95113129e-11 m/s²` RMS as vectors (cosine `0.99997979`). This is a descriptive shape comparison across different captured inputs; the old THM was mixed-time and does not have the new source-60 six-state gate. It does not isolate W or establish a cause.

## Scope boundary

Six same-call assembled RHS comparisons are now available for this one dry first-call state. The output is before acoustic work and later physical-boundary updates; no `+15 s` field-history comparison was recorded, and these one-step process times are not an equal-accuracy runtime benchmark. There is no new forecast, optimizer, or accuracy threshold. Frozen V3 still fails all 24 rows and remains unqualified; project-wide accuracy, complete nonlinear/semidiscrete parity, and integrated observation-window JVP/VJP remain open. The rotating inverse remains closed.

## Delivery and final verification

The [evidence ZIP](evidence/20261012/pr301_full_six_rhs_evidence.zip) contains 116 hashed payloads: the two raw native captures, corrected input/source60 frame, prior V1 prefix reference, exact build/source/command receipts, independent Green/Red audits, and portable verification/replay scripts. Executable/library bytes are excluded; recorded hashes are runtime observations. Source formulas are bound to committed observer code `2f32859d`. Graphify reused 76 verified bindings, updated only the changed source binding/manual write edges, and explicitly retained the Fortran extraction gap.

[Fresh-path verification](evidence/20261012/pr301_portable_verification.json) passes all payload SHA-256/CRC checks, input/source and six-call-state/PGF-to-Full gates, V2 decoding, and exact V1 body continuity. The [fresh replay summary](evidence/20261012/pr301_fresh_replay_summary.json) additionally records bitwise equality of all 24 physical rate/delta/mask arrays and all 13 pressure-accounting arrays after relocation on the recorded Python stack. This is an offline data replay, not new WRF integration. The bundle includes the report body frozen before this delivery appendix.

ZIP SHA-256: `e5ca3633a90d3aaae172d376f1377179e5ebc2063f2b9e108e3c2e5acadaf6e3`. Readable [six-rate statistics](evidence/20261012/pr301_six_rhs_analysis.json), [capture binding](evidence/20261012/pr301_capture_receipt.json), [W pressure accounting](evidence/20261012/pr301_w_pressure_analysis.json), [control-flag chronology](evidence/20261012/pr301_control_flag_provenance.json), and [build/run audit](evidence/20261012/pr301_build_run_audit.json) accompany the ZIP. Green/Red final reviews found no blocking factual, unit, mask or scope claim. The W pressure remainder is a measured accounting residual, not a certified rounding/error bound. Automatic CI status is reported in the PR against its final head; the C++ CI lane does not replace the local Fortran rebuild and two native calls.
