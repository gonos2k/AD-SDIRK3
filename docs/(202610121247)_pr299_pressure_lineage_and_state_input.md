# First-PGF pressure lineage and common-state input correction

Local timestamp: 2026-10-12 12:47:41 JST (+0900)

Base main: `583e2a64c1879372c2e0a1b9f4770298a05ffd75`, same tree as PR298 HEAD `622b43fc`. This investigation reuses the two preserved PR298 input/call pairs. It adds no production C++/Fortran equation change, pressure injection, PGF scaling, precision downgrade or filter tuning.

## First-call pressure producer

`input_wrf.F:1022–1039` identifies ideal input and sets grid/config/namelist hypsometric option 1. All four preserved RSL logs record the ideal override. The earlier namelist print of 2 predates this effective reset: there is no actual option 2/option 1 disagreement for these calls.

For the single-domain nonrestart path, `start_em.F:690–703` reconstructs PB/ALB from MUB/T_INIT and the base EOS, copies prognostic t2 into t1 at 669, reconstructs option 1 AL at 814–822, then writes pressure at 851–864:

`P′ = p0*[Rd*(t0+t1)*qvf/(p0*(AL′+ALB))]^(cp/cv) − PB`.

USE_THETA_M=1 and dry QV mean qvf=1. Default REAL is 4 bytes, with gfortran 15.2 flags `-O2 -ftree-vectorize -funroll-loops`, no REAL promotion or fast-math. At `solve_em.F:933–1009`, rk_step_prep reads pressure and rk_phys_bc_dry_1 copies its periodic-X/symmetric-Y halos. The owned mass cells retain the initialization EOS values. The first rk_tendency/PGF call at 1159 precedes small_step_prep/calc_p_rho(step=0) at 1511. Later acoustic filtering and stage-end calc_p_rho_phi are different producers and do not explain this first-call P.

Cycling=false, rebalance=0, restart=false, DFI=0 and obs_nudge_opt=0 exclude the alternative writer routes for these preserved captures. This is source-order attribution tied to actual call-state/data/log gates, not a newly emitted assignment trace. It does not cover restart, later stages, DFI, cycling or nudging.

C++ mode3 Full computes base ALB from cached T_INIT+300/PB with compute_inverse_density, then computes option 1 AL/ALT/P from current PH/THM/MU in calc_p_rho_wrf. Base ALB is not derived from PHB and is not a copied Fortran ALB pointer. Its pressure helper promotes state arithmetic to FP64 and preserves the state derivative. Both paths use the same dry option 1 equation family; their arithmetic paths are distinct.

## Independent startup EOS reproduction

A standalone scalar Fortran program evaluates the actual startup EOS grouping with default REAL32, source constants and the captured THM/ALT/PB. It was compiled once with the recorded compiler/FP flags; a link-only explicit Xcode SDK sysroot resolves the installed compiler's stale SDK specification. It is not WRF integration, a new model run, or a bitwise compiler emulator for other operators.

| Preserved input | Mass owners | Captured Fortran P matched | RMS/max mismatch Pa |
| --- | ---: | ---: | ---: |
| original input | 4096 |4096 bitwise |0 /0 |
| mixed-time stored input |4096 |4096 bitwise |0 /0 |

This closes reproduction of the pressure value consumed by these first PGF calls. The compiled oracle takes observed ALT; it does not independently certify every intermediate ALB/AL computation or attribute all error to the final large-pressure subtraction. P, PB, AL/ALT, source constants and compiler pins remain separately recorded.

## Newly found state-input omission

Registry.EM_COMMON220–222 maps NetCDF T to diagnostic th_phy_m_t0, while NetCDF THM maps to prognostic t1/t2. The old one-off stored-input recipe copied U/V/W/PH/T/MU/P from the 60-second RK3 output but retained initial THM. Its actual call-local t matches this retained THM exactly. Compared with the60-second source THM, it differs at 4056 of 4096 values: RMS 8.7125e−4 K, max 6.2294e−3 K.

The archived stored input is therefore a mixed-time cold probe, not a complete 60-second prognostic checkpoint. The prior P2/RHS/PGF pressure comparisons remain valid for the actual paired call states; the old full60-second interpretation is withdrawn. Existing inputs, receipts and residual arrays are preserved. In particular, the stored post-PGF remainder stays 3.0690e−9 m/s² RMS, measurable and unassigned; it is not replaced with results from a corrected future fixture.

The new common-state input helper copies THM alongside T and the other seven state/pressure fields, requires common PB/PHB/MUB and compatible source arrays, and creates a new output without overwriting an archive. It is state-only cold initialization. Startup recomputes diagnostic P/AL/ALT; this does not preserve Newton predictors or acoustic history and is not a warm restart.

## Validation scope

No new WRF build, native em_b_wave call, forecast refinement, inverse, optimizer or same-setup forecast/runtime comparison was performed. Existing PR298 native/source/executable/input and PGF comparisons are reused. Only offline source/data analysis, standalone startup EOS evaluation and input-generation checks are in this session's new evidence.

The frozen V3 reference gate remains failed in all 24 rows. Project-wide accuracy is not newly adopted, full six-equation parity/integrated JVP/VJP remain open, and the rotating inverse remains closed. Identifying the startup pressure producer is not an explanation of the separate RK3 timestep-sensitivity failure.

## Same equation family, distinct numerical diagnostics

The captured Fortran pressure is reproduced by its REAL32 startup expression, while C++ pressure is reproduced by its FP64 EOS on its own captured operands (RMS 1.23e−12 / 1.18e−12 Pa). The base PB and ALB source equations also reproduce the preserved FP32 input values exactly. Effective C++ ALB, inferred from captured ALT−AL′, matches the FP64 base EOS from T_INIT/PB to about 4.8e−17 m³/kg RMS. That inference is not a newly recorded internal base tensor.

The following is an exact pointwise telescope. Starting with actual C++ P, successively replace its numerical evaluation, captured operand set, scalar exponent and FP64 evaluation with the observed Fortran startup result. Component RMS values are not added; the vectors add and the identity residual is zero.

| Pressure component | Original RMS Pa | Mixed-time RMS Pa |
| --- | ---: | ---: |
| actual C++ − own FP64 replay |1.2281e−12 |1.1763e−12 |
| captured operand substitution on common C++ FP64 formula |0.00362885 |0.00404432 |
| C++ double exponent − widened Fortran float exponent |0.000516803 |0.000516803 |
| same FT operands, FP64 − compiled REAL32 startup oracle |0.00310151 |0.00304055 |
| compiled REAL32 oracle − actual FT |0 |0 |
| actual C++ − actual FT, all components combined |0.00480874 |0.00514604 |

T/THM is exact across each pair. The operand contribution is mainly the base-alpha path; AL′-only pressure substitutions are smaller. This assigns diagnostic differences without fitting a pressure offset or PGF multiplier. It does not claim that total pressure is rounded before PB is subtracted: the standalone program's compiled expression contracts the final multiply/subtract. Host powf+fmaf reproduces all 8192 recorded oracle outputs, whereas forced separate FP32 multiply/subtract only matches 18 and 8 values. This explains that software-emulation discrepancy for these operands; it is not a universal compiler or full-WRF instruction-trace claim.

Pressure norms alone are insufficient for PGF interpretation. With captured FT ALT held fixed, the ALT×Dx(P′) component of the actual pressure change is RMS 6.485e−8 / 7.486e−8 m/s². The exponent-only shift has about 5.17e−4 Pa pressure RMS but is nearly horizontally uniform: its corresponding U acceleration is only 5.34e−16 / 3.94e−15 m/s². This is a fixed-ALT B-term sensitivity, not the entire PGF, nor a changed production force.

Completed narrow scope: actual first-call pressure writer and effective branch identified; observed FT startup pressure reproduced; same equation family and numerical paths separated. No production equation correction is justified by these data. The stored post-PGF remainder, later-stage pressure/history, other assembled equations and RK3 reference qualification remain separate open questions.

## State-input helper verification

Six focused NetCDF tests pass: exact eight-field copy plus preservation/time0/no-overwrite, missing THM, QVAPOR mismatch/presence mismatch, each base-field mismatch, and invalid leading Time. Negative tests protect the corrected field contract; they do not replace model validation.

The frozen helper has SHA 81a4215a1a08712b6a3b162e7cbf0e85703c3659f253a30c5acd9638a18ddf6f; test SHA 931d290587a516825f56381d88dbf68658fb8be58409d014b42b28952eb6c83a. The actual archived source is SHA 9b657c04aeb278afe9d822f75ee8ced5d668f664f7c3ec201a2249643e79efdb, unique 60-second frame index 1. A new cold input produced with the frozen helper has SHA fc37c8c0c8be8053a16e0d3424609d496f4a023c6c17ee3a06371970639d14df. Its eight copied fields exactly match that frame; 169 other variables are preserved, PB/PHB/MUB and zero QVAPOR match, and Times remains zero.

The corrected input was not used for a native call. The old mixed-time probe retains its original hash and remains the input to all pressure/residual claims above. Do not silently substitute the new file into old receipts or claim a new 60-second call-state/forecast result.

Green/Red teams reviewed startup/Registry semantics, actual call-state linkage, scalar compiler arithmetic, array axes, helper/fixture bytes and scope. An ancillary offline PB-replay argument-order error and earlier T-versus-t mapping were corrected before final claims; historical reports are marked superseded. Final graph source bindings cover 76 exact files, with Fortran/generated-I/O extraction gaps explicit. Graph navigation is not the numerical proof.


## Delivered evidence and final audit

The [evidence ZIP](evidence/20261012/pr299_pressure_lineage_evidence.zip) contains 172 hashed payloads, paired original calls/source pins, original and mixed-time inputs, corrected cold input, archived 60-second source frame, scalar oracle/source/input/output, arithmetic records, graph bindings and Green/Red audit notes. No model executable/library bytes are distributed; recorded native hashes are observations, while bundled raw data are directly rehashed.

[Fresh-extraction verification](evidence/20261012/pr299_portable_verification.json) passes all hashes/CRC; 8192 recorded oracle pressure bits and both THM→call-state checks match; both pressure telescope identities have zero residual; the corrected file regenerates byte-for-byte; six tests pass. A first verifier implementation incorrectly viewed a parser's widened FP64 values as FP32 words; it was fixed to compare at the original FP32 storage dtype before this final extraction. The scientific inputs/results were unchanged.

Input helper source/test and all 76 graph pins match committed blobs at `ed1089bd78c880ae2357149fc4809136cd18b4b1`. Readable [pressure statistics](evidence/20261012/pr299_pressure_eos_replay.json), [compiled oracle receipt](evidence/20261012/pr299_compiled_eos_oracle.json), and [corrected fixture receipt](evidence/20261012/pr299_common_state_fixture.json) accompany the ZIP. The bundled verifier and helper/tests are portable entrypoints; original auxiliary analysis scripts retain historical machine paths. Verification portability here is fresh-path relocation on the recorded stack, not universal compiler/math-library bit identity.

Final Green/Red reviews found no blocking equation, unit, state-mapping or scope claim. The source-method distinction and mixed-time-input limitation are explicit. Automatic minimum CI will be linked in the PR description after its final-head result; local six-test/standalone-oracle evidence is not silently merged into the selected remote CTest list.

Delivered ZIP SHA-256: `7fa5885c986e185b4d7dc045ca60fe1574b4b90611fa9dfdf35a1fc27d538175`.
