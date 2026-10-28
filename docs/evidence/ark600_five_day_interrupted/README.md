# Interrupted standard ARK600 five-day attempt

The [report](<../../(202610260948)_ark600_monitor_interruption_partial36h.md>) records an **incomplete** attempt: 251/720 outer-step timing records (41h50 model time), RC1, stopped on 2026-10-25 at 16:50 JST. The controller's process-tree `ps` query timed out after 3 seconds; its exception handler then terminated its owned launcher. The 18-hour cap was not exhausted. This is not evidence of numerical divergence, five-day completion or accuracy acceptance.

The failed attempt is immutable. Its original controller SHA is `1b616268…`, manifest SHA `dd6472e6…`, executable SHA `30b0ac22…`, shared input SHA `c3e56611…`, and receipt SHA `42b9be36…`. The wall/monotonic gap of 13,279 seconds is unexplained. Keep `cost_eligible=false`; neither elapsed time is CPU time or a valid speedup result.

Small public evidence:

- [run_summary.json](run_summary.json): termination cause, clocks, cleanup and incomplete scope.
- [partial_distances.csv](partial_distances.csv): 24h/36h owned-point differences against archived RK3 h600. RK3 is a comparator, not exact truth.
- [saved_state_checks.json](saved_state_checks.json): shared initial state and positive saved-state minima over five/seven frames. This does not validate every unsaved internal state.
- [controller_test_receipt.json](controller_test_receipt.json): future-controller fault-injection results; all model process creation is mocked.
- [array_asset_manifest.json](array_asset_manifest.json): fourteen independently downloadable NPZ assets, each below 1.8 MB, with hashes, shapes and original NetCDF provenance.
- [archive_verification.json](archive_verification.json): audit ZIP and array integrity checks.

Each NPZ contains an owned-point `diff` (ARK−RK3) and `reference_change` (RK3 endpoint−RK3 initial), computed **after promoting public FP32 NetCDF values to FP64**. These are derived arrays, not raw full states or an internal FP64 trajectory. Owner masks exclude the periodic U duplicate and fixed lower W/PH face while retaining the dynamic top and both V walls. The original seven NetCDF files remain preserved locally; hashes and analysis receipts are included. No restricted external observational archive is included.

Recompute all fourteen metric rows with NumPy, without WRF or libtorch:

```sh
python recompute_partial_metrics.py
```

The loader verifies each selected NPZ against the manifest SHA-256 and byte size, then checks both `diff` and `reference_change` for the declared shape, FP64 dtype and finite values. A mismatch stops with a nonzero exit code; the archived inputs are never rewritten. Run the focused checks with `python test_recompute_partial_metrics.py`.

For a bounded download, fetch this script, the small array manifest and just [the 36h W asset](arrays/36h_W.npz), then use:

```sh
python recompute_partial_metrics.py --hours 36 --field W
```

The 36h W RMS distance is `2.464239036e-3 m/s`, versus `4.376337489e-4` at 24h. The relative column is normalized by the RK3 **change from initial state**, not an accuracy budget. No error threshold, convergence order, exact-truth status or same-accuracy cost win is inferred. The closed 240s derivative/twin cases and PR321's permutation result remain closed.

`evidence.zip` contains original runner/input-setting/launch/termination records, quiet RSL logs, the saved-frame analyses, source/graph bindings, prior postprocess failure and recovery, raw controller-test logs and review summaries. It omits executable/static-library binaries, the unchanged common input and full raw NetCDF payloads. Native source hashes remain linked to the earlier accepted build and archived RK3 references. Arrays are separate to avoid requiring one large ZIP download.

## Future execution code, not a rerun

The checked-in [run_standard_wrf.py](../../../tools/run_standard_wrf.py) versions the case controller with three narrowly scoped changes:

1. A missing process-tree sample from `TimeoutExpired` is recorded separately. One/two misses may recover; three consecutive misses stop as `monitoring_unavailable`. Cap/disk checks remain before/after sampling, use the same maximum of monotonic and wall elapsed time, and take precedence over the grace counter. Other query/parse errors fail closed. Authoritative Popen exit is checked before declaring monitoring loss.
2. Cumulative wrapper rows are accepted only when nondecreasing, every failed count is zero and the final row is exactly `3*N/0`. Periodic `300/0`, `600/0` rows do not replace or invalidate the required final `2160/0`. These are wrapper calls, not independently counted Newton stages.
3. The [next case-side observer](execution/next_observer.py) publishes terminal progress from the controller receipt rather than leaving a stale “running” snapshot. It recognizes the actual controller script in the launch record and reads the selected cap from the manifest. It cannot launch, stop or retry a forecast.

Preflight also rejects existing `wrfout_d01*`, `wrfoutReady_d01*` and `wrfrst_d01*` paths, even when no receipt or RSL remains. It preserves those files and refuses to launch; an input/namelist-only directory remains eligible.

Owned-launcher reaping continues even if cleanup sampling fails. Descendants are signaled only after fresh birth/session/exact-executable checks; recycled or unverified PIDs are not signaled. The tool still verifies manifest/executable/input/namelist/helper hashes, actual t0 fields, quiet/error/output gates, resource limits and fresh output paths. It requires explicit `--manifest`, `--manifest-sha256`, `--case`, `--go` and recorded resource authorization. It refuses to overwrite the failed attempt. It does not silently repeat a model or perform a cold restart.

Example for a **separately authorized fresh case**, not the archived attempt:

```sh
python3 -B tools/run_standard_wrf.py \
  --manifest /fresh-run-root/manifest.json \
  --manifest-sha256 SHA256_OF_THAT_MANIFEST --case ark_h600 --go
```

The observer is a case-side template: copy it into the fresh run root's `execution/`, along with a pinned frozen analyzer; preserve its no-model/no-signal boundary. Historical absolute paths in the archived manifest are provenance, not a ready-to-launch configuration for another machine. Inherited six-hour authorization prose was corrected in a separate sidecar without mutating the executed manifest.

The stdlib tests use fake Popen handles, snapshots and clocks. They test controller decisions and cleanup boundaries; fake 720-step log fixtures are **not** real forecast results. The initial unpublished draft's missing resource helper was caught by fault injection and fixed before publication; failed-test evidence is retained. All final tests pass. No model, MPI, solver build, weather regression or performance experiment was run after these control fixes.

The source/header/coefficients/tolerances/filters of the forecast model are unchanged. A fresh continuous five-day run, ARK300/150 sensitivity, physical accuracy acceptance and credible equal-accuracy cost comparison remain open. The old MPI shutdown investigation is separate from this controller-issued termination.
