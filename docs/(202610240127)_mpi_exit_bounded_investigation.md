# Bounded MPI exit investigation — cause unresolved

Local timestamp: 2026-10-24T01:27:43.553205+09:00 (Asia/Tokyo).

The PR #316 dry 240 s, two-control twin remains complete. This separate engineering investigation does not establish why its two original launcher attempts returned 1. No solver/runtime defaults or acceptance rules were changed, and no new WRF integration was performed.

## Context and exact scope

The audited repository base is `0816c32393c7a0b12081e1d4c4e5f8b0ad46c9b3` (tree `0de07ff435a2621447d8b5f5cf4ee8426533aef2`). The archived numerical executable SHA-256 is `7fdb04fd124ea1d25250081b16d0ce35f1b23ce849fd615ff156bfff092ab6c4`. Its old 41 attempts remain 39 RC0 and two RC1, with the exact narrow replacement evidence preserved. The new probes use Open MPI 5.0.9, PMIx 5.0.9 and PRRTE 3.0.12 on this Mac; they contain no WRF/PDE/Torch computation. They are not model-executable equivalence tests.

## Findings

- The active linked WRF shutdown path reaches `wrf_dm_shutdown -> MPI_Finalize`. The linked ESMF stub object has no MPI imports, excluding the proposed second finalize through that stub in this executable. I/O quilting is inactive. WRF's success text precedes shutdown and cannot certify the launcher exit status.
- For the 39 old RC0 runs, last NetCDF mtime to process completion is 0.1399–0.5338 s; the old failed 192-step attempt has 18.0523 s and the failed 96-step attempt 1045.8116 s. File mtimes locate a post-output interval, not a particular MPI or I/O-close phase. Host sleep affected the first failure's timing; it does not establish either failure's cause.
- The archived Fortran path imports `MPI_Init`, with recorded thread level `MPI_THREAD_SINGLE`. Custom AD halo callbacks have baseline-thread checks; some compiled stage MPI queries/reductions do not. No actual off-thread call was observed, so this source risk is not attributed as the RC1 cause. AD halo exchange was off in the old case.
- Tagged PMIx source waits for a finalize acknowledgement or its timer and can reach a success return after timer wakeup. Open MPI has a preceding finalize fence. These distinguish API completion from job-exit classification; a 10 s default and an 18 s historical delay do not identify a timeout cause. No timeout or MPI thread-level setting was changed. See [PMIx v5.0.9 source](https://github.com/openpmix/openpmix/blob/v5.0.9/src/client/pmix_client.c) and [Open MPI v5.0.9 finalize source](https://github.com/open-mpi/ompi/blob/v5.0.9/ompi/runtime/ompi_mpi_finalize.c).

## New runtime evidence and failed diagnostic setup

| Probe scope | Actual attempts | Result and limit |
|---|---:|---|
| Tiny MPI lifecycle, C/F77/F77+PMPI observer | 32 total | 30 valid runs: 10 of each, all launcher RC0, no timeout. Two earlier setup invocations have unrecorded RCs and remain unknown. |
| Direct PMIx controlled-hold diagnostic | 4 total | Three stops before GO/SIGSTOP; final 1 s hold has PMIx return 0 and launcher RC0, but the strict driver gate remains inconclusive. |

The three direct-PMIx setup stops were a missing readiness helper, Darwin's truncated combined `ps` command-name field, and an invalid assumption that PRTE's application PGID must equal the launcher PGID. All used sources and receipts are retained. Their cleanup-induced RC1 statuses are not reproductions of the historical MPI failure.

The final driver verifies client ancestry to its own isolated launcher and checks target PID/PPID/PGID, installed executable and birth time before STOP/CONT. A recorded-process fixture rejects foreign ancestry and foreign target groups. The post-GO hold-loop window was 1.02855 s; the recorded SIGSTOP-to-SIGCONT request interval, including ownership queries, was 1.61505 s on the same Python monotonic clock. The receipt field `actual_hold_seconds` measures only that loop, not the full signal interval. `PMIx_Finalize` took 1.44036 s, returned 0, and the OS-waited launcher returned 0. The client-written `app_exit.json` is only an imminent-return marker, not an independently waited grandchild status.

After SIGCONT the driver saw the exiting launcher as a zombie (`comm=<defunct>`) and failed its resume identity guard. The preserved status is `baseline_inconclusive_phase2_skipped`; the clean-baseline gate is false. The final process group is empty and the known client/launcher PIDs subsequently disappeared. This is an additional diagnostic guard limitation, not evidence of a PMIx failure or a clean harness pass. No receipt was normalized.

The initial two-probe diagnostic plan was revised during setup recovery to count at most four total launches; the 75 s cumulative wall cap and the numerical case budget were not changed. Each revision and earlier setup attempt is retained. The final four-launch cap is exhausted. Cumulative recorded direct-PMIx execution/build time is 30.1091 s within the 75 s cap. The 12.5 s hold was **not executed**; there is no measured finalize-timeout mechanism result. No further launch was added to bypass the guard. Earlier setup revisions and their caps are preserved rather than hidden.

## Preservation and validation

All 67 originally pinned closed-case files match their hashes; this check covers those files, not every archive member. E0–E2 thresholds, observations, controls, source and results remain unchanged. The archived [summary](evidence/mpi_exit_audit/engineering_summary.json) and [evidence bundle](evidence/mpi_exit_audit/audit_evidence.zip) retain receipt statuses, probe sources, small logs, original timing rows, source/object audits and graph bindings. The bundle is an audit snapshot, not an installed diagnostic feature; its absolute paths identify the original local evidence root.

Graphify navigation was bound to this worktree and relevant source hashes, refreshing the targeted C++ corpus and each diagnostic source revision. Fortran extraction produced no nodes, so its shutdown edges were checked manually against authoritative sources and linked objects. Graph edges are navigation aids, not numerical or process-identity proof.

The final Python probe passed Ruff undefined-name checks and captured-ancestry fixture checks before its one launch. Independent Green and Red reviews cover the evidence and limitations. No new production build, WRF run, CTest, forecast-field comparison, runtime comparison against archived same-setup split-explicit RK3, or E0–E2 rerun was performed; production code was not changed.

## Disposition

The bounded engineering audit is recorded; the historical MPI RC1 cause and recurrence prevention remain **unresolved**. Keep nonzero launcher exits failed. Do not enable a global artifact-based acceptance exception, increase timeouts, change MPI thread support, or reopen the completed numerical case on this evidence. A future investigation should instrument the first failing shutdown phase on an actual recurrence, separately from the numerical acceptance record.
