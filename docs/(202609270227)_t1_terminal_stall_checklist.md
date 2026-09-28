# T1 terminal zero-step stall follow-up checklist

Local timestamp: 2026-09-27 02:27:59 JST (+0900)

This checkpoint is for the T1 terminal-stall capture extension on exact PR #245 base `9147bbd`. Detailed provenance and limits are in `(202609270227)_t1_terminal_zero_step_stall_capture.md`.

| Item | Status | Evidence / remaining limit |
|---|---|---|
| First Stage-2 common-trust rejection | **Preserved** | Existing one-shot schema-2 `.pt`/JSON remains at its first common decision path; earlier quality-gate and recovery paths are excluded. |
| Terminal `ZeroStepStall` candidate | **Captured for matching iter-6 case** | Schema-3 artifact holds the latest cloned common-path rejected candidate at iter 6 / attempt 1 and the terminal event at iter 6 / three zero updates. A candidate whose stage or iteration differs from the terminal event is refused and no terminal file is written. |
| Frozen operands / stale-candidate control | **Passed** | Round-trip test mutates source tensors after cloning and verifies frozen archive values; a stale iter-6 candidate offered for terminal iter 7 publishes no files. |
| Default-off / solver behavior | **Regression-neutral for tested case** | OFF and ON both exit at the same Stage-2 failure; 104/104 CTest passes; normalized solver decisions/JVP telemetry and 301 Krylov total match; partial output hashes match. No extra solve evaluation is present in the capture path. |
| Terminal operand subtraction check | **Completed; interpretation limited** | FP64 subtraction of captured FP32 K/F operands changes the scaled W-block norm by a relative `2.31e-10` at the terminal base and `1.16e-10` for the rejected trial. This is not FP64 RHS evidence and does not identify the cause of stagnation. |
| Full coupled RHS and replay | **Open** | Mutable tile/forcing/operator state is not captured; no faithful offline alpha ladder or FP64 RHS was run. Model-mismatch versus precision cause remains unresolved. |
| Clean WRF build / forecast acceptance | **Open** | Validation uses an archive-first C++ relink against clean #244 WRF Fortran objects. The strict case aborts as designed; no successful full forecast or time-order claim follows. |

Next T1 closure remains a faithful same-state RHS/replay capture with mutable geometry and forcing state plus a discriminating model-mismatch control. No solver equation, tolerance, or acceptance rule changed in this checkpoint.
