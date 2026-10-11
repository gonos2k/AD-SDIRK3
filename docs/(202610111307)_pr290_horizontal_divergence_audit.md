# Rotating source-coordinate energy correction and horizontal diagnostics

Local timestamp: 2026-10-11 13:07:49 JST (+0900)

## Context

The PR290 worktree is at base revision `5baf7b4acebb7e02688372a4cd0432a75b5dc785`; its source-coordinate energy-metric correction is in the worktree. Existing Graphify evidence is in `/Users/yhlee/AD-SDIRK3-review-evidence/PR290-20261011/`. I directly checked the source packing in `tools/wave_rotating_reference.py`: native packed fields place U on `(ny,nz,nx+1)` faces and V on `(ny+1,nz,nx)` faces, with periodic U's final x face an alias of its first. The divergence operator follows those stagger locations and computes D on `(ny,nz,nx)` mass cells.

## Changes

Copied the PR289 `rotating_vorticity_audit.py` into `/Users/yhlee/AD-SDIRK3-review-evidence/PR290-20261011/rotating_vorticity_audit.py`. Added `D=DxU+DyV`, per-time native and frozen-source divergence statistics and native-minus-source errors for both control branches, plus terminal/start RMS ratios for native and source divergence at each audited time. Divergence and its errors use `s^-1`; ratios are dimensionless. Existing vorticity outputs and accepted source-pin checks remain intact. No production source was changed for this audit task.

Added an exact staggered mathematical fixture using a periodic U face field and a linear-in-y V face field. It checks the expected `D=[0,-2,-2,0] s^-1` at every mass row/level, the `(2,2,4)` result shape, and finite inputs/outputs.

The metric correction in `tools/wave_rotating_reference.py` is coordinated with one coordinate-difference regression test and a minimal CI step. It changes the Python source-reference metric only; it does not change the production native solver.

## Validation status

- Graph navigation: the refreshed post-edit Graphify verification records the same base revision, verifies 45 current source files with zero hash mismatches, and reports 1,522 nodes and 3,533 edges. Graph edges remain navigation only.
- Direct source review: confirmed native state packing, U periodic-face alias, V wall faces, and the fixed 8480-state block layout used by the copied audit.
- Math fixture: passed when run directly from the copied audit script; exact array equality and finite checks passed for all 16 owned cells.
- Audit script: parsed successfully with Python AST. The archived inverse/forecast audit was not run in this task because no accepted ZIP inputs were supplied in this evidence directory.
- Metric descriptor audit: `/Users/yhlee/AD-SDIRK3-review-evidence/PR290-20261011/metric_descriptor_audit.json` records same-host old/fixed runs with all four input files byte-identical and the mode array identical. The corrected frequency is `0.0025701755450031286`; overlap changes from `0.998404519520722` to `0.9981666482617931`. Corrected source energy and transformed physical energy are both `783458273.7297355` (relative difference 0); the old metric's energy ratio is `516.4555951103581`.
- The coordinate-difference test passed locally with `python3 -B tools/test_rotating_energy_metric.py` (1 test, 0.005 s). A minimal CI step was added; no CI run result is claimed here.
- Green mathematical review passed. Its in-memory mutation restoring the old metric fails as expected (`source_energy / physical_energy = 943.8387185006278`, expected 1), confirming the regression test detects the former coordinate use. The same review records the source/physical transformation identity and its scope.
- The archived Linux cross-host artifacts are not byte-identical to the corrected same-host inputs: for example, the basis differs by max absolute `1.0718190224245916e-11` and relative L2 `2.659743113629561e-11`. Keep the archived provenance and report this as numerical agreement evidence, not byte identity.
- At the initial status read, existing long run `38106368123` was still in progress. This task started no model run and made no new same-setup WRF/RK3 field or runtime comparison.

## Next actions

After the archived run completes, run this copied audit against the accepted inverse and forecast archives with their recorded hashes, then review the emitted divergence errors and terminal/start ratios. Keep the rotating inverse evidence open until the completed inverse and unused-time forecast archives have been independently reviewed. General WRF/RK3 validation remains separate. This report does not claim completion of the long-run or RK3 comparison evidence.
