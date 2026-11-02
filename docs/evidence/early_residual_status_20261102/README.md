# Early failure residual evidence

Source base: 696def3b (PR338/339). Final production/test revision: c7a282e3.
Green/Red model: GPT-6.1-Terra medium.

Final focused tests: Trust 101, classification 210, early residual 12: all pass. Native h600
A9/P0 research configuration completed one step; three stages converge and all 174 numeric
endpoint arrays match archived A9/P0. This is not the default native configuration, a cost
acceptance test, a long forecast, or a global convergence guarantee. No new RK3 was run.

`native_regression/result.json` is the immutable runner result before field comparisons;
`native_validation.json` is the separate successful postvalidation. `runtime_observations.json`
contains unpaired RSL timings and cannot support a speedup claim. Raw NetCDF/NPY files,
archives, executables and baseline compiler/Fortran fixtures remain in the external evidence
folders named by receipts; they are not shipped here.

`attempt1` preserves the failed overflow fixture: LibTorch's checked float conversion threw
before a scalar residual was observed. It was not an Inf receipt or a numerical forecast
failure. The follow-up fixes unwinding statistics and tests standard/non-standard exceptions
without manufacturing a final measurement. `build_driver.log` preserves a source-change
safety guard (test-only edits during the initial ten-TU build). `finish_build.py` checks those
edits and assembles already compiled identical production sources. `rebuild_newton.py`
then recompiles Newton at c7, reusing the other nine affected objects after hash checks.
See `build_receipt_notes.json` for the earlier inherited receipt field; raw receipts are unchanged.

Reproduction requires the exact external baseline fixtures and stack. Keep the filenames
unchanged and restore this folder at the recorded external E path and sources at W. The
active scripts are `link_native_c7.py`, `run_native_regression_c7.py`,
`verify_rebuilt_archive.py`, and `validate_native.py`, not the initial drafts. Commands,
arguments, source/object/library/input hashes and original paths are in the receipts.
This folder alone does not enable a portable independent WRF rebuild or replay.

Mixed-rank execution, rank-local exception coordination, MPI collective failure handling,
and exceptions in a production public wrapper after Impl returns remain unverified. Shared
three-state merge/abort rules are unit tested; the native regression uses one rank.
