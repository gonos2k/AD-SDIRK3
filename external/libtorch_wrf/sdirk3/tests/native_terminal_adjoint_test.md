# Native terminal adjoint probe

This is a compile-time test hook for the single-tile WRF `em_b_wave` path. It
is enabled with `WRF_SDIRK3_NATIVE_TERMINAL_TEST`; it has no runtime effect in
builds compiled without that macro. A test build must define the macro for the
preprocessed `dyn_em/solve_em.F`, the SDIRK3 C++ core and interface, and compile
and link `native_terminal_adjoint_test.F` and `native_terminal_adjoint_test.cpp`
into the same WRF executable. Use the normal WRF build and the matching
compiler, MPI, netCDF, and libtorch configuration from the project README.

At timestep 1, after WRF initialization and CQ setup, the hook packs the
current prognostic state, applies the test control seed, and returns that state
to the host path before the native solver call. The initial control therefore
acts on the post-initialization state. The probe requires one tile with the
registered 17×17×16 mass-grid layout and a 256-step maximum horizon. Keep
`dt=0.9375 s`, the dry fixed-coefficient test setup, and the matching namelist
and executable configuration fixed across runs.

## Run roles

Set `WRF_SDIRK3_NATIVE_TERMINAL_MODE`, `WRF_SDIRK3_NATIVE_TERMINAL_ETA`, and
`WRF_SDIRK3_NATIVE_TERMINAL_OUT` for each run. `STEPS` defaults to 256 and may
be shortened in multiples of 32 only for `prefix`. `replay` requires
`WRF_SDIRK3_NATIVE_TERMINAL_REFERENCE`; `prefix` also requires
`WRF_SDIRK3_NATIVE_TERMINAL_CHAIN` (`baseline` or `replay`) and
`WRF_SDIRK3_NATIVE_TERMINAL_LAMBDA32_IN`.

| Mode | Forward and pullback role |
|---|---|
| `baseline` | Run the requested horizon (normally 256); retain only the final 32-step graph and run its checked Fortran FP32 pullback. The historical V5 baseline additionally ran a direct C++ FP64 pullback over that same local window. |
| `prefix` | Start again from the original post-initialization state, run `STEPS`, retain only its last 32 steps, and pull back the supplied FP32 cotangent through Fortran. Check every endpoint against the baseline reference. |
| `replay` | Run all 256 steps with no VJP and compare every checkpoint with the supplied baseline reference. This is a state-only replay. |
| `plus001`, `minus001`, `plus003`, `minus003` (or `fd`) | Run the 256-step perturbed forward with no VJP. Set `ETA` to `0.01`, `-0.01`, `0.03`, or `-0.03`, respectively. |

Every prefix is an actual native forward from the original initial state; it
does not resume from a saved endpoint. A chain of prefix runs composes local
32-step pullbacks by passing each later segment's returned FP32 cotangent as
the earlier prefix's `LAMBDA32_IN`. The probe supplies the local operation and
checkpoint checks; it does not claim an independently computed full-precision
global adjoint. The FP64 gradient output is the bridge's local pre-cast result
for the incoming FP32 cotangent, retained for comparison with the Fortran
FP32 result. It is not an FP64 cotangent chain across the full horizon.

## Objective, direction, and boundaries

The registered terminal objective is a linear functional of physical W
owners, with amplitude scale `0.001 m/s`; its cotangent is FP32. The
deterministic direction spans the six packed blocks U, V, W, PH, T, and MU,
with one equal scaled L2 share per block. Periodic aliases are copied from
their owning points, the north extension follows the test's reflected rule,
the V physical wall faces and W/PH lower boundary are zero, and the top W/PH
level remains active. Objective and direction values are recorded in the run
artifacts.

Warm-prefix steps run without a retained reverse graph. The solver retains a
fixed 32-step graph for the local pullback and captures accepted FP64 endpoints
for exact replay checks; the endpoint ledger is separate from the graph. This
bounds reverse-graph storage to one local window, subject to the probe's 18 GiB
process RSS guard. It does not establish model accuracy, runtime improvement,
or a successful end-to-end 256-step adjoint; record those only with the
corresponding run evidence.

Each `OUT` prefix writes prepare, endpoint, result, checkpoint, state, and
Omega trace artifacts. In result metadata, `fortran_fp32_chain` identifies
the cotangent path, `local_precast_fp64` identifies the scope of the captured
FP64 gradient, and `state_only_replay` identifies replay runs.
