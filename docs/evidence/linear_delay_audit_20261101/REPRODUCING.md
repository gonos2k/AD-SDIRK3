# Restoring archived paths

This public folder is not self-contained. The scripts contain hardcoded local paths
and require the pinned source, build stack, prior archives, raw fixtures, and run
inputs recorded in `registration.json`, the pin files, and `manifest.json`.
For the original layout, the evidence directory was
`/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101` and the
worktree was `/private/tmp/linear-delay-audit-20261101`. Restore or explicitly
adapt those paths and verify every relevant pin before attempting a reproduction.
The original `observer_smoke_v5.draft.py` source is not public in this folder;
its recorded hash alone cannot supply it.

The manifest records the original paths. These public script names must be restored
at the evidence directory under their original names for scripts that reference them:

| Public name | Original filename |
| --- | --- |
| `original_build_pair.py` | `build_pair.draft.py` |
| `build_candidate_current_sdk.py` | `build_candidate_current_sdk.draft.py` |
| `finalize_control_current_sdk.py` | `finalize_control_current_sdk.draft.py` |
| `original_run_pair_case.py` | `run_pair_case.draft.py` |
| `validate_saved_case.py` | `validate_saved_case.draft.py` |
| `observer_checks.py` | `observer_checks.draft.py` |
| `original_pin_case.py` | `pin_case.draft.py` |

For each manifest record, `sha256` identifies the public file and
`original_sha256`, when present, identifies the original archived file. Check
`normalization` before comparing them: for example, the two CSV files were
converted from CRLF to LF. A public normalized hash is not the hash of the
original executed or archived bytes. Script renames do not by themselves change
content; use the recorded original hash to check the actual bytes.

The original OFF runner's INCONCLUSIVE result is preserved. The separate offline
saved-output validation passed; it did not rerun the model or replace the original
result. This guide does not establish a new model run or reproduce the result.
