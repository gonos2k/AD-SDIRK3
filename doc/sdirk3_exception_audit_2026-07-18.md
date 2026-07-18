# SDIRK3 production exception-path audit under the real WRF link (2026-07-18)

**Status: dated audit record** (PR 9C.3 / P0-EH commit 3, branch
`pr9c3-mixed-toolchain-eh` off `main` = `19a8ad27`). Companion to the
link-contract discriminator (`external/libtorch_wrf/sdirk3/eh_link_contract/`)
and the restored C++ final link (commit 2 of this series).

Scope caveat (fixed by measurement, do not over-generalize): everything here
is about the CURRENT macOS Apple-clang/libc++ C++ objects + gfortran/mpif90
toolchain. The pre-fix breakage (no C++ unwinding at all under the mpif90
final link — same-function try/catch reached `std::terminate`) and the fix
(clang++ final link with the Fortran/MPI runtimes explicit) are both pinned
by the standing discriminator, shape A 0/7 vs shape B 7/7.

## 1. Seal vocabulary

- **controlled-fatal** — `abort_c_abi_exception` (`wrf_sdirk3_mpi_safety.h`,
  `noexcept [[noreturn]]`): stable marker to stderr, flush, thread-gated
  `MPI_Abort` (PR 9C.3 commit 4), `std::abort`. The W-damping routers reach
  it through the installed `wdamp_contract_fail_handler`.
- **recoverable-status** — a `_checked` extern-"C" entry whose full body is
  try/catch and which returns 0/1 to Fortran.
- **internal-invariant** — a throw deep in the compute path caught by an
  outer seal that records `SDIRK3_STEP_OUTCOME_FATAL_INTERNAL` (=101) on the
  solver object; Fortran queries the outcome and calls `wrf_error_fatal`.
- **test-only** — throw sites exercised only by the clang++-linked contract
  binaries (which have always had working EH).

## 2. Seal-liveness re-verification against the REAL WRF link (measured)

The review asked whether the checked ABI actually returns status or ends in
`std::terminate`. Measured at this head with the restored link, using
`WRF_SDIRK3_EH_SEAL_PROBE=1` (skips the W-damping fail-handler install so
the violation THROWS) on the open-x negative:

```
SDIRK3 v2 ERROR: Exception in sdirk3_tile_unified_step_zerocopy_v2:
  SDIRK3_WDAMP_PARITY_GEOMETRY_UNSUPPORTED: ... open x-boundary ...
SDIRK3 FAIL-CLOSED outcome=101 final_update_aborted=1 ...
-------------- FATAL CALLED ---------------           (wrf_error_fatal)
exit=1;  SDIRK3_C_ABI_EXCEPTION count 0;  uncaught-terminate signatures 0
```

The v2 seal catches, records FATAL_INTERNAL, Fortran terminates cleanly.
Under the pre-fix link the identical throw was a `libc++abi` terminate with
none of the seals firing (three stacked catch probes all silent) — i.e. the
seals were dead code in production until commit 2.

## 3. extern "C" entry seal map (55 entries, 7 files)

Full classification: (a) fully sealed, (b) sealed with a gap, (c) unwrapped
but non-throwing callees, (d) unwrapped and can throw (escape), (e) noexcept.

- `wrf_sdirk3_interface_zerocopy.cpp` (24): the main step path
  `sdirk3_tile_unified_step_zerocopy_v2` is (a) — the try opens before
  `setWRFIndices`/`advanceZeroCopy` and the catch records FATAL_INTERNAL
  (liveness measured, §2). `sdirk3_set_mpi_comm[_checked]`,
  `run_adjoint_replay` (catch → −1) sealed. Gaps: `create_zerocopy` is (b)
  — a `std::call_once(load_from_env)` runs BEFORE the try, and the catch is
  `std::exception`-only; `load_from_env` is atoi/atof-based (non-throwing),
  so the realistic escape is only a `call_once` `system_error`. The
  remaining entries are (c): lock+find+getter / flag+epoch resets,
  including the OMP-parallel TLS reset (whose callables are epoch bumps and
  `.clear()` — no throwing callee exists in that OMP region today).
- `wrf_sdirk3_halo_exchange.cpp` (5): all (a)/(e) — `_checked` entries
  return 0/1; void entries route to `abort_c_abi_exception`. All 48
  TORCH_CHECK sites in the file sit inside these seals.
- `wrf_sdirk3_mpi_safety_impl.cpp` (9): (a)/(c)/(e); the `MPIExchangeScope`
  thread-contract throws are constructed only within sealed paths.
- `wrf_sdirk3_tile_unified_interface.cpp` (5): create is (a) (catch →
  nullptr); the rest (c).
- `wrf_sdirk3_base_state_zerocopy.cpp` (2): `set_base_state_zerocopy` is
  (b) — `catch (const std::exception&)` with NO `catch (...)`, and the
  failure is swallowed with no status back to Fortran; no non-std throw
  exists on the path today, but the seal is narrower than its peers. The v2
  variant is a stub (c).
- `wrf_sdirk3_config.cpp` (9): the six string-keyed setters/loader/printer
  are (d) — the ONLY genuinely unsealed class. No explicit throw and no
  stoi/stof (atoi/atof used); the escape is limited to `std::bad_alloc`
  from `std::string`/`std::stringstream` construction. The three worker
  flag entries are (c) (atomics).
- `wrf_sdirk3_tile_unified.cpp` (1): deliberate fail-closed `std::abort`.

## 4. Deep-path throw classification (~154 throw + ~97 TORCH_CHECK)

Structural guarantee: `wrf_sdirk3_tile_unified_impl.cpp` (68 throw +
49 TORCH_CHECK), `wrf_sdirk3_newton_solver.cpp` (11 throw), the AD/boundary
TUs, and all throwing headers define no extern "C" of their own — their only
ABI reachability is through solver methods (`advanceZeroCopy`,
`setBaseState`, `runAdjointReplay`, halo prepare/exchange) that are invoked
inside the seals above. Classification:

- internal-invariant (step seal → FATAL_INTERNAL): the entire compute path
  (tile RHS, Newton/GMRES guards, AD strict modes, tensor validation,
  zero-copy view checks).
- controlled-fatal: the W-damping contract routers (handler-installed), the
  MPI rc check (`SDIRK3_MPI_CALL_FAILED`), the void halo ABIs, the
  `MPIExchangeScope` thread contract.
- recoverable-status: the `_checked` halo/notify/comm entries.
- test-only: the injection throws (`WRF_SDIRK3_TEST_FAIL_*`) and everything
  the clang++-linked contract binaries exercise directly.
- No TORCH_CHECK is reachable from an unsealed extern-"C" boundary; the
  unsealed (config) entries reach no tensor ops at all.

## 5. Residual gaps (enumerated; recommended follow-ups, none load-bearing)

1. Six `wrf_sdirk3_*config*` entries can leak `std::bad_alloc` into Fortran
   (unwrapped). Realistic only under allocation exhaustion at init time.
   Recommendation: wrap each in try/catch → `abort_c_abi_exception`.
2. `sdirk3_tile_set_base_state_zerocopy`: add `catch (...)` and surface a
   status/abort instead of a silent swallow.
3. `sdirk3_tile_solver_create_zerocopy`: move the `call_once` inside the
   try; widen the catch.
4. `establish_mpi_baseline_thread` is `noexcept` but takes a `lock_guard`
   (`system_error` → terminate). Marginal (OS mutex failure at init).
5. The uniform marginal class: pre-try `lock_guard`s in otherwise
   non-throwing void entries (`system_error` only).
6. Audit boundary noted honestly: dynamic reachability of every deep throw
   solely through sealed methods rests on the no-extern-"C"-in-TU
   structural argument plus traced call paths, not a per-site dynamic
   proof; `MPIExchangeScope` construction sites were traced for the
   exchange/step/notify paths, not exhaustively for every future caller.

## 6. Thread/callback boundaries

No `std::thread`/`std::async`/`future` in production TUs. Two
`std::call_once` sites (one pre-try — gap 3 above; one inside the step
seal). One OMP parallel region in an extern-"C" entry with verified
non-throwing callables. One `atexit` cleanup (process-exit context). The
worker-thread FATAL policy itself (who may call MPI_Abort) is commit 4's
contract with its standing children (baseline-abort / worker-abort).
