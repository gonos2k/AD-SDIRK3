# Terminal native forward-AD JVP observation

Exactly one registered h600 native attempt reached stage4/Newton0/ordinal5,
measured the actual original JVP plus five repeats and exited86 without resuming
Newton/Fortran. Every repeat matches the original primal and tangent bytes,
uses one implicit RHS and no FD/fallback, and keeps the observed fixed context.
This is a bounded context/direction observation, not full Tile replay or safe
continuation. No forecast step completed.

**The registered runner outcome remains INCONCLUSIVE.** Its global wall-clock
minus monotonic gap is -1.7937879022s, outside the predeclared one-second limit.
`run/terminal_jvp_probe.json` reports the narrower native block passed;
`run/result.json` preserves the global failure. Local steady/getrusage times are
descriptive; `cost_eligible=false`. There was no retry, P0/P1 cost acceptance,
new RK3 forecast/runtime comparison, noise remedy, or five-day validation.

`analysis/samples.csv`, `analysis/summary.json` and the timing figure contain all
six observations. In a scratch copy, run `python3 analyze_terminal.py .` to
recompute these from the two public run JSON files; Matplotlib/NumPy are needed
for plotting and PNG bytes can vary with plotting versions. Do not overwrite
the sealed evidence when reproducing. `manifest.json` fixes every public input
and image. The summary explicitly separates all six samples from five repeats.

Native scripts retain their executed filenames, relative layout and hardcoded
original roots: `/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102`
and `/private/tmp/terminal-jvp-replay-20261102`. Native rebuilding/running additionally
requires the pinned source08b21c11, prior A9 control objects/archive, Fortran proof,
raw input/RK3 T0 and compiler/MPI/netCDF/LibTorch stack listed by registration/build
receipts. Executables, objects, raw NetCDF and large first-point artifacts are
not public here. `compare_prior_point.py` also needs the pinned prior A9 NPYs;
its hash agreement is a fingerprint, not a direct old/new byte comparison.

Focused two-source tests were compiled and run separately; their exact commands,
source hashes and earlier link-development failures are retained in the receipts.
Default CI does not enable the terminal macro. Production equations/tolerances,
filters, h600, original RHS wrapper and forward-mode router remain the same;
the new primal-out reads the same native dual evaluation without another RHS.

The runner's `converged_stages: []` reads only rsl.out. The separate audit and
selected rsl.error lines show stages2 and3 completed; the original runner/result
were not changed. The fifth solve did not finish. Four byte-identical diagnostic/
T0 files were hardlinked to already-readonly A9 canonical files, preserving all
bytes and artifact hashes; no physical APFS saving is asserted.
