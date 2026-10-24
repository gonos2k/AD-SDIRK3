# Optional native shutdown trace

For the declared standalone DM-parallel, non-coupled native case, this diagnostic records the WRF shutdown path, the F77 MPI initialization and
finalization calls, and the launcher's owned-child wait. It does not alter model
equations, solver settings, MPI status, or acceptance of a run. It is opt-in and
compiled out of the default WRF build.

## What is recorded

With `WRF_NATIVE_EXIT_TRACE` defined for `main/wrf.F`,
`main/module_wrf_top.F`, and `frame/wrf_shutdown.F`, the C observer emits these
events to the file named by `NATIVE_MPI_FINALIZE_TRACE`:

| Event | Location / meaning |
| --- | --- |
| `observer_loaded` | Observer object constructor ran. |
| `mpi_init_enter`, `mpi_init_return` | F77 `MPI_Init` wrapper entry and return. The wrapper forwards once to `pmpi_init_`. |
| `model_run_return` | Immediately before `wrf_finalize`. |
| `io_cleanup_enter`, `io_cleanup_return` | Bracket `med_shutdown_io`, including its dataset closes and `wrf_ioexit`. |
| `framework_finalize_enter`, `framework_finalize_return` | Bracket `WRFU_Finalize` on the non-coupler shutdown path. |
| `wrf_shutdown_enter`, `wrf_shutdown_return` | Bracket `wrf_shutdown` in DM-parallel builds. The MPI wrapper events occur inside this interval. |
| `mpi_finalize_enter`, `mpi_finalize_return` | F77 `MPI_Finalize` wrapper entry and return. The wrapper forwards once to `pmpi_finalize_` and records the actual returned `ierr`. |
| `wrf_finalize_return` | `wrf_finalize` returned to the main program. |
| `observer_destructor` | Observer destructor ran during runtime teardown. This is not proof of OS process exit. |

Each observer line includes the local PID, a `pthread_self()` token, and a
`CLOCK_MONOTONIC` sample taken at the trace call. These identify local ordering
and the calling thread. The observer deliberately makes no MPI state or thread
queries; it does not certify `MPI_THREAD_SINGLE`, `MPI_THREAD_FUNNELED`, or
another thread-support level. It does not record hostnames or MPI ranks, so do
not assume a shared multi-rank trace file is collision-free or rank-complete.

`SINGLE` promises that only one thread executes. `FUNNELED` permits multiple
threads, with MPI calls restricted to the initialization thread. A recorded
calling-thread token alone does not prove either condition or the provided
MPI support level. Do not switch to `MULTIPLE` as a substitute for diagnosis.

When its debug level permits, the existing WRF message
`wrf: back from med_shutdown_io` indicates that I/O cleanup returned. The trace brackets that call,
so a missing `io_cleanup_return` localizes a stall or termination to that call
or before the record was written. `SUCCESS COMPLETE WRF` is emitted after I/O
cleanup and before time-manager/driver shutdown; it is not a process-success or
MPI-finalize marker. In coupler mode, `cpl_finalize` is used instead of the
`WRFU_Finalize` / `wrf_shutdown` branch, so those latter phase events are not
expected. The optional `no_shutdown` path also skips the framework and driver
shutdown calls.

The trace timestamps are sampled when each record is prepared. They are not
function-entry timestamps supplied by MPI and do not measure the later file
append/flush duration. `native_exit_trace.py` separately records its own
launcher PID/thread, monotonic and wall-clock samples around spawn and
`Popen.wait()`. Keep the clocks labeled: compare event order directly; do not
subtract wall-clock values from monotonic values. C `CLOCK_MONOTONIC` and Python
`time.monotonic()` are separate clock domains on this stack: do not subtract
their absolute samples across languages, even on one host. Use intervals within
one domain and causally ordered markers. Cross-host comparability is not established.

The observer destructor can run before the OS finishes process teardown. The
launcher receipt's `launcher_wait_return` records when Python reaped its direct
child. Neither signal alone proves process-group emptiness, completion of every
MPI rank, or a scientific diagnostic pass.

## Build a separate diagnostic executable

Use the same WRF source revision, GNU Fortran/Open MPI stack, preprocessing
flags, and dependent libraries as the executable under investigation. The
observer assumes the registered GNU Fortran ABI: default `INTEGER` is 32-bit
and compatible with C `int`; the MPI F77 symbols are `mpi_init_`,
`pmpi_init_`, `mpi_finalize_`, and `pmpi_finalize_`. Compile the C observer
with the same Open MPI headers and ABI as the WRF link:

```sh
mpicc -std=c11 -Wall -Wextra -pthread \
  -c external/libtorch_wrf/sdirk3/tests/native_mpi_finalize_trace.c \
  -o /tmp/native_mpi_finalize_trace.o
```

For a diagnostic WRF build, define `WRF_NATIVE_EXIT_TRACE` only while
preprocessing the three authoritative Fortran sources named above, and add
`native_mpi_finalize_trace.o` to the final WRF link before the MPI libraries.
Use the repository's normal WRF build entry point and the installed compiler,
MPI, netCDF, and libtorch configuration described in `README.md`. Keep the
result at a new executable path. Record hashes for the three changed
preprocessed source inputs, observer source/object, executable, compiler/link
commands, and stack. Do not overwrite or relabel the original executable.

This object is specifically for GNU Fortran with the matching Open MPI F77
profiling ABI; a successful compile against some `mpi.h` is not enough to
establish compatibility. Do not use `-fdefault-integer-8` unless the observer
ABI is deliberately revised and revalidated. The repository's source-extracted
fake-PMPI and Fortran-oracle tests check forwarding, default-off preprocessing,
and phase order without starting WRF/MPI. They do not validate a full WRF
rebuild, the actual installed MPI ABI, or deployment behavior; those remain
untested until a future diagnostic rebuild and run.

The resulting executable has a different hash from the frozen native-twin
executable. It cannot be substituted into a run whose binding pins the original
binary. Preserve that binding and create a separate diagnostic launch record
that pins the new executable and observer object.

## Run one explicitly pinned diagnostic launch

`native_exit_trace.py` takes an explicit command, the executable that command
will run, and a new output directory. Its stdout/stderr are inherited by the
terminal or enclosing job. The recorder sets `NATIVE_MPI_FINALIZE_TRACE` to a
new `native.trace` file, writes launch events to `launcher.json`, hashes the
launcher, native executable, and recorder, and waits through its owned
`subprocess.Popen` handle.

```sh
python3 external/libtorch_wrf/sdirk3/tests/native_exit_trace.py \
  --native-executable /absolute/path/wrf_exit_trace.exe \
  --output /absolute/path/new-evidence/exit-trace-001 \
  -- /absolute/path/to/mpirun -np 1 --bind-to none \
     /absolute/path/wrf_exit_trace.exe
```

Choose an output path that does not already exist. The command must explicitly
contain the pinned native executable. The recorder starts its child in a new
session to avoid terminal Ctrl-C delivery followed by a duplicate forward. It
does not signal process groups or implement a timeout policy. This path targets
the noninteractive native case; interactive/controlling-terminal behavior is not qualified.
On `KeyboardInterrupt` it sends
`SIGINT` only to its direct child, if that child is still running, then waits
for that child. It does not guess process IDs from a process listing or claim
that descendants have exited.

`launcher_return_code == 0` means only that the direct launcher child returned
zero to the recorder. It is not a diagnostic pass: inspect the phase sequence,
trace presence and contents, WRF logs and artifacts, and applicable numerical
validation independently. An empty trace can mean the linked observer was not
loaded or `NATIVE_MPI_FINALIZE_TRACE` was not honored; it is not success.

## Current evidence and limits

The local fake-PMPI/source-extracted tests have passed after a macOS SDK
`-syslibroot` linker adjustment. No full WRF rebuild or deployment run of these
new hooks has been performed. The previously archived PR #316 finalize trace
belongs to an earlier observer and separately linked executable; it confirms
that run's finalize return only and does not validate these new phase hooks or
explain another launcher failure. No timeout workaround, MPI thread-level
change, physics change, or solver configuration change is part of this
diagnostic.

Trace file I/O changes timing. The source/fake tests do not establish native field
or runtime parity, observer neutrality, or the cause of an older failure.
