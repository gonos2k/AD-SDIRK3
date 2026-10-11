# Same-state WRF U/V/MU component RHS diagnostic

Local timestamp: 2026-10-12 00:16:47 JST (+0900)

## Result and completion boundary

Both raw snapshot pairs have exact equality of all six packed state blocks and all physical input faces, including the periodic U closing face. Complete U/V/MU component rates can now be compared in common physical units. Measured U/V residuals remain; this record does **not** certify equality of all WRF spatial equations. The completed rotating inverse/forecast case stays closed.

No numerical equation, tolerance, EOS or mixing-profile behavior changed. The two source edits add opt-in I/O using `WRF_PARITY_RK_TEND_DUMP=1`. Fortran writes after `rk_addtend_dry` and the boundary calls, preserving the old pre-add dump. C++ writes the already computed first `Full` RHS in the existing debug-level-2 split diagnostic: the new flag adds no RHS evaluations. Collection is limited to serial, one full-domain tile, periodic X, symmetric Y and zero external C++ forcing. Write failures abort collection.

## Exact instance and actual runs

Source base: `3e707c05a93283ce8043344251ec14dcce36524c` plus the two SHA-pinned diagnostic edits. The build and start-to-end checks pin six authoritative formula/source files; each run receipt explicitly repeats five of those hashes. The sixth, `module_big_step_utilities_em.F`, remains pinned by the build/end checks. Original producer receipts are retained; the [post-commit receipt](evidence/20261012/pr295_committed_source_equivalence.json) confirms that all six pinned source bytes equal the committed blobs. It records source commit `7365cc38e3c9c54a65692603abca7aa690cbf8f3`; later documentation-only commits preserve those bytes.

- Dry, flat 16×16×16 `test/em_b_wave`, option-2 diffusion (`khdif=1000`, `kvdif=10`), `mix_full_fields=false`, `phi_adv_z=1`; original input SHA `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`.
- One MPI rank and one OpenMP thread; GNU Fortran 15.2, Apple Clang 21, Open MPI 5.0.9, netCDF 4.9.3/netCDF-Fortran 4.6.2, LibTorch 2.10. Full `em_b_wave` build succeeded, including affected objects.
- Effective C++ configuration prints `internal_fp64=true`, `internal_fp64_state_carry=true`, and `enable_ad_halo_exchange=0`. The snapshot writer explicitly exports FP32 state/RHS arrays; Fortran fluxes are FP32. Precision effects are not separately attributed by this comparison.
- Executable SHA: `5e6358add0d857b036adcfc2b05f133276ddb21516e1558918f1d0e3ba8bd9d3`.
- Three actual 240-s runs: RK3 h=3.75 s with collection, ARK mode 3 h=15 s with collection ON/OFF; all debug level 2.
- Two actual 15-s runs at h=15 s use a common saved RK3 60-s state as time-zero input. U/V/W/PH/T/MU and P are copied with hashes; other input arrays are checked unchanged. These autonomous reinitialized diagnostics are not warm-restart equivalence tests.

Six state arrays match **after actual initialization**, not merely in staged input files. Packed storage includes an extra horizontal mass endpoint: 17×17×16 mass storage versus physical 16×16×16 cells (`isPackedPeriodicDomain`, `momentum_m/n`). Both full-storage equality and physical input-face equality are audited; no correlation alignment or mismatched physical boundary input is dropped.

## Common units and measured differences

The captured Fortran RU/RV are total mass-coupled fluxes. Reconstruct mass rate from their signed-`dnw` column divergence plus `mu_tend`, then stagger it with the `calc_mu_uv` boundary rules. Convert Fortran coupled tendencies exactly once:

\[
\dot u=\frac{\mathrm{RU}_t m_y-u c_{1h}\dot M_u}{c_{1h}M_u+c_{2h}},\qquad
\dot v=\frac{\mathrm{RV}_t m_x-v c_{1h}\dot M_v}{c_{1h}M_v+c_{2h}}.
\]

C++ `Full` already returns physical U/V rates. Flux identities pass within eight FP32 eps of their flux scales. Manufactured nonunit-mass and nonzero-mass-rate examples distinguish mass-flux divergence from velocity divergence and single from double conversion. Reconstruction uses FP64 accumulation of captured FP32 fluxes; it is not a captured post-acoustic stock-WRF `MUDF` value.

RHS norms use 4,096 unique U faces, 3,840 interior V faces and 256 mass cells. Duplicate U and fixed V faces are excluded from **norms**, retained in input/boundary audits. U/V units are m/s²; MU is Pa/s.

| Input | Component | RMS difference | Max difference | Fortran signal RMS | Relative RMS |
|---|---|---:|---:|---:|---:|
| Initial state | U | 7.746295884e-07 | 2.784448336e-05 | 0.0003685416617 | 0.2101878% |
| Initial state | V | 6.663245523e-08 | 2.04721806e-07 | 5.738254798e-05 | 0.1161197% |
| Initial state | MU | 0 | 0 | 0 | undefined (zero signal) |
| Common saved 60-s state | U | 7.740717466e-07 | 2.781233283e-05 | 0.0003681697808 | 0.2102486% |
| Common saved 60-s state | V | 7.62176296e-08 | 2.649709511e-07 | 5.725161762e-05 | 0.1331275% |
| Common saved 60-s state | MU | 3.369322404e-08 | 1.251582374e-07 | 0.003772382595 | 0.0008932% |

Initial MU is a zero-signal check; the saved-state check is nonzero. No project RHS error budget is inferred from these measurements.

## Validation and first remaining discrepancy

All five native runs completed. New RK3 matches archived RK3 h=3.75 exactly in six fields at 0/60/120/180/240 s. ARK collection ON/OFF also matches exactly in those fields/times. Timings are preserved for provenance; debug-level-2 overhead makes these controls unsuitable for claiming efficiency superiority.

The existing instantaneous split diagnostic reports norm(Full−Fast−Slow) below 4×10⁻¹⁸ at the first paired inputs. This is not proof for every later stage or an integrated trajectory.

About 97.6% of saved-state U squared discrepancy is on the periodic X seam; the maximum is at zero-based (j,k,i)=(15,10,0). This prioritizes seam-term inspection. Pressure/advection/diffusion attribution and causation of the 240-s endpoint error remain **open**. Captured Fortran west pressure ghosts equal the last physical mass column in both snapshots; stale pressure ghosts were not observed.

Direct prior NetCDF reanalysis confirms the reviewed U error geometry: old/new error RMS 0.00766436594/0.00765610593 m/s, update RMS 0.000339642735 m/s, cosine(error_old, update)=-0.046463874. This is raw-array confirmation, not causal attribution to one code edit.

Green source/graph review and Red independent raw-array recomputation found no remaining storage, parsing or conversion defect in the final comparison. Tooling startup syntax and storage/physical-extent mistakes are preserved as superseded records, not production numerical failures. Graphify binds 55 current sources with zero SHA mismatches; extraction gaps are explicit. No new CTest execution was needed for this observational change; native controls and arithmetic checks validate collection, and prior component-test receipts remain separate.

## Evidence and next action

[Evidence ZIP](evidence/20261012/wrf_same_state_uvmu_rhs.zip): 105 SHA-pinned payloads; CRC and all payload hashes pass. ZIP SHA `56247f7552d1fdf62aca8f29697188e654d871501c6e1796b06fa44bbd6ed8d3`. Executable/library bytes are preserved locally and recorded by SHA, not included in the ZIP. Its README gives portable NumPy reanalysis commands.

The [problem definition](PROBLEM_DEFINITION.md) remains authoritative. Forecast window, physical scales and budgets are **unselected**; this 240-s diagnostic does not adopt project acceptance targets. Efficiency remains optional. W/PH/theta RHS equality and integrated JVP/VJP acceptance are not established here.

Next: inspect the periodic U seam contribution at these retained inputs; address reference/time-map sensitivity only as needed under a selected target. Do not reopen the rotating optimizer or add an unbounded timestep ladder.
