# Corrected cold-input W preservation and actual RHS comparison

Local timestamp: 2026-10-12 16:00:27 JST (+0900)

Base main for the investigation: `303fd8e22dc6908a0493ca87b49b2b6b2b3e9456`. The W-preservation source fix was then committed as `c26a0f9a86d4ca83642d2cf82326399f8c79fe8a`. This report distinguishes the original default-off cold-start probe from the later opt-in W-preservation pair. Five 15-second native calls were completed across those three groups; they are operator diagnostics, not an accuracy/runtime benchmark or forecast experiment.

## Finding and narrow correction

The corrected input `fc37c8c0c8be8053a16e0d3424609d496f4a023c6c17ee3a06371970639d14df` copies `U,V,W,PH,T,THM,MU,P` from the unique 60-second RK3 output frame. Registry `THM` is prognostic `t`; NetCDF `T` is a separate diagnostic. The cold-start input is timestamped at time zero and has no warm numerical history.

In the original default `use_input_w=.false.` path, `start_em` checked only surface W. This case has zero surface W even though the archived 60-second column contains nonzero interior W. On the executed default-off baseline, the active cold-start path selected the existing `set_w_surface(fill_w_flag=.true.)` fallback, and actual first-call W was zero. Actual U/V/PH/THM/MU matched source-60, but W did not. We did not run the old executable with `use_input_w=.true.`; source inspection predicts its same surface-only heuristic would also classify this zero-surface/nonzero-interior case as missing input W, but that hypothetical route is not a measured result. The logged `set_w_surface` call is instead at the end of `solve_em` (`fill_w_flag=.false.`), after the first PGF capture; it only reapplies the surface boundary.

The narrow opt-in correction changes only the existing `start_em` test. With `use_input_w=.true.`, it scans the full W column for an effectively zero field; when interior input W is present, it skips the cold `set_w_surface` overwrite. With the default false, the historical surface-only test remains. When the selected W range is effectively zero, the existing fallback still initializes W. No pressure, RHS, physics, precision, ABI, or observation equation changed.

The opt-in `use_input_w=.true.` pair confirms the intended state route: all six actual first-call prognostic fields `U,V,W,PH,THM,MU` match the archived 60-second source bitwise in both Fortran and C++; the FT and C++ call states also match each other. The input remains a time-zero cold start, not a checkpoint: this proves preservation of the selected field values at the first call, not trajectory or predictor/history continuity.

## Five same-setup launches

All runs used the same corrected input, one MPI rank, one OpenMP thread, `h=15 s`, a single step, and the same diagnostics. The first pair reused the original PR298 executable; the preserve-W pair and the default-off regression used the rebuilt executable. Wall time is process launch-to-exit for these short startup-heavy runs and does not compare equal-accuracy cost.

| Run | Binary | `use_input_w` | Method | Wall time |
| --- | --- | --- | --- | ---: |
| Original corrected baseline | PR298 `4201790d…e22ec3` | F | Fortran RK3 | 1.21389 s |
| Original corrected baseline | PR298 `4201790d…e22ec3` | F | C++ mode-3 ARK | 1.36550 s |
| W-preservation pair | rebuilt `5589c1a0…b557338` | T | Fortran RK3 | 0.61625 s |
| W-preservation pair | rebuilt `5589c1a0…b557338` | T | C++ mode-3 ARK | 1.28306 s |
| Default-off regression | rebuilt `5589c1a0…b557338` | F | Fortran RK3 | 1.93720 s |

The modified authoritative source is `dyn_em/start_em.F`, SHA-256 `f3cf233cb8223601629a5779625a421bdd92cad639d9f7f7eef2502fdc5d8fd6`, matching the later committed fix. The source-only rebuild regenerated `start_em.o`, updated the WRF archive and relinked the executable; the C++ archive and `module_em` interface were unchanged. The build receipt's source HEAD predates the fix commit, so the build-to-commit binding is by exact source bytes, not by relabeling the build as having occurred at that commit. Generated `.f90` and build artifacts are pinned separately, not treated as authoritative committed source.

## Actual state and default-off checks

The opt-in pair passes the six-field source-frame gate and same-call Fortran/C++ state gate, including exact periodic U seam ownership. Its `PGF` state equals its same-run Full RHS state. Input `P` is copied from the 60-second output but startup reconstructs call-time `P/AL/ALT`; call-time pressure is reported separately and is not part of the six prognostic-state gate. The same-input, time, compile, executable, and run receipts are pinned in the Green execution records.

The new-executable default-off control matches the original binary baseline bitwise on physical state/RHS owners and on the captured PGF, horizontal-diffusion and vertical-diffusion observers for this one RK3 case. Strict raw whole-file byte parity remains failed: 109 off-domain `mu_tend` ghost entries differ, with maximum absolute difference `1.56536e-40`; no owned mass cell differs. The independent source-loop audit bounds this result to physical owners in this configuration and does not claim ghost reads are irrelevant in other configurations. This is a one-case default-off physical regression check, not a general regression proof.

The original `off_regression_runs.json` retains its strict raw-byte `failed` status and is not rewritten. The append-only `pr300_default_off_acceptance.json` separately records that the run completed and passed physical-owner acceptance, with the 109-value ghost-only raw mismatch disclosed.

## Full RHS and U-component accounting

Fortran assembled U/V rates are converted with the mass product rule and flux-derived `MU` tendency; C++ Full U/V rates are already physical accelerations. `MU` tendency is compared in Pa/s. Isolated PGF and H/V increments are normalized separately and do not receive the Full-rate `MU` product-rule term. All cross-language differences are elementwise on the stated masks; RMS values are never subtracted.

| Full C++ minus Fortran | Owners | RMS | Max absolute | Unit |
| --- | ---: | ---: | ---: | --- |
| U | 4,096 | 8.39972220e-8 | 3.74252656e-7 | m/s² |
| V | 3,840 | 8.48934369e-8 | 3.33822831e-7 | m/s² |
| MU | 256 | 3.36932240e-8 | 1.25158237e-7 | Pa/s |

For U define `R = DeltaFullU - DeltaHdiff - DeltaVdiff` and then form `R - DeltaPGF` elementwise. This gives:

| U owner-vector quantity | RMS |
| --- | ---: |
| Horizontal-diffusion delta | 6.52200831e-10 m/s² |
| Vertical-diffusion delta | 1.53667319e-12 m/s² |
| R after both diffusion deltas | 8.39940782e-8 m/s² |
| Isolated C++ minus Fortran U-PGF | 8.39314798e-8 m/s² |
| R after PGF delta | 3.06910511e-9 m/s² |

The post-PGF remainder has max absolute value `3.43781005e-8 m/s²`; subtracting the measured PGF component reduces the squared norm of `R` by `99.8664862%`. This is a measured component accounting for the two first-call states, not a production error reduction, complete RHS parity, proof of pressure-only causation, or a forecast improvement. The residual remains measurable and unassigned.

The reused C++ observer isolates U-PGF only. Full V is compared above, but no isolated V-PGF result is claimed. Assembled W, PH, THM RHS parity, the other U/V/MU terms, integrated JVP/VJP, and a whole forecast remain open.

## Scope boundary

The W-reset baseline pair remains valid evidence for the executed default-off cold-start route; it is not overwritten by the preserve-W pair. The new pair is a first-call common-state operator check. None of the five launches runs a 240-second forecast or compares a +15-second field; runtime values are not an efficiency result. No pressure coefficient was tuned.

Frozen V3 reference qualification still fails all 24 rows; the reference remains unqualified. No project-wide accuracy threshold or acceptance goal is adopted. Forecast accuracy, timestep refinement, complete six-equation parity and integrated observation-window derivatives remain open; the rotating inverse remains closed.

| Closure gate | Result |
| --- | --- |
| Opt-in first-call `U,V,W,PH,THM,MU` versus source-60 | 0 field differences |
| Opt-in Fortran versus C++ first-call six-field state | 0 field differences |
| Each PGF call state versus its same-run Full RHS state | exact |
| Default-off rebuilt versus baseline physical-owner fields and PGF/H/V observers | 0 differences in this one case |
| Default-off raw dump parity | fail: 109 off-domain `mu_tend` ghost values |
| Full assembled six-equation RHS / forecast qualification | open |

These are source-state and operator-observation gates, not an accuracy target. The measured U-PGF output delta remains nonzero as reported above.

## Evidence records

The preserved machine-readable records include the [actual six-field source-state gate](evidence/20261012/pr300_actual_state_gate.json), [default-off physical-owner acceptance and raw-ghost limitation](evidence/20261012/pr300_default_off_acceptance.json), [Full U/V/MU comparison](evidence/20261012/pr300_full_uvmu_rhs.json), [U-PGF/H/V component residual](evidence/20261012/pr300_u_residual.json), and [committed-source/build binding](evidence/20261012/pr300_committed_source_binding.json). The source binding states that the build predates commit `c26a0f9` but its `start_em.F` bytes match that commit exactly.

## Delivered evidence and final audit

The [evidence ZIP](evidence/20261012/pr300_corrected_state_evidence.zip) contains 187 hashed payloads: five run records/raw captures, corrected input and archived source frame, source/build/graph receipts, explicit default-off raw failure and physical acceptance, and independent Green/Red numerical audits. Executable/library bytes are excluded; their recorded hashes are runtime observations. Auxiliary original scripts retain historical paths; `verify_pr300_bundle.py --root <unpacked-root> --out <new-result.json>` is the portable entrypoint (NumPy/netCDF4 required).

[Fresh-extraction verification](evidence/20261012/pr300_portable_verification.json) passes SHA-256/CRC for all 187 payloads, raw actual six-field source60 and FT/CPP/PGF-to-Full gates for the opt-in pair, default-off physical masks, and both elementwise residual identities. The baseline W-zero source gate remains false as documented. During verifier preparation, its MU comparison was corrected to remove the parser's singleton vertical axis before comparing the NetCDF column field; native arrays/results were unchanged. The ZIP contains the report body frozen before this delivery appendix.

ZIP SHA-256: `ad7583f9523439e1bc5a994458d63e82968734e899c610bd1c44e1c6f2d4bfc8`. Exact changed-source bytes match commit `c26a0f9a86d4ca83642d2cf82326399f8c79fe8a`; the separate [source binding](evidence/20261012/pr300_committed_source_binding.json) preserves the pre-commit build-head caveat. Graphify's 76 source bindings are current; Fortran/Registry caller edges are explicitly manual because of extraction gaps, not numerical proof. Green and Red teams found no blocking equation, state mapping, unit, mask or completion-scope claim in this bounded change. Automatic CI is reported separately in the PR against its final head.
