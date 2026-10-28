# ARK600 three-hour-output attempt: disk gate stop

[Status and cause report](../../(202610282022)_ark600_disk_cause_and_pr_status.md) separates verified facts from unresolved attribution.

A fresh official-scale dry `em_b_wave` ARK324 run used the accepted executable and input, h600 for a 720-step/five-day target, three-hour history output, a five-GiB whole-volume free-space floor and an eighteen-hour elapsed cap. Only the history interval changed in the model namelist. There was no retry or cold restart.

The controller stopped its launcher on `disk_resource_cap` after 175 logged outer steps (29h10 model time, about3h19 wall time). Ten history frames through27h remain. The 6h/24h comparison checkpoints were written. This is incomplete, cost-ineligible and not accuracy acceptance or evidence of numerical divergence. The prior transient-process-query stop is a different historical attempt.

- [run_summary.json](run_summary.json): termination, exact hashes, quiet flags, cleanup and scope.
- [output_manifest.json](output_manifest.json): all ten retained raw NetCDF sizes/hashes, plus five shared-time whole-file matches against the prior interrupted ARK run. Raw histories remain local and are not duplicated in this package.
- [checkpoint_metrics.csv](checkpoint_metrics.csv): 6h/24h method distances from archived RK3h600, not exact-truth errors.
- [evidence.zip](evidence.zip): actual manifest, frozen controller/helper/observer, initial gate, raw RSL and monitor/termination receipts, checkpoint analyses and source bindings.
- [archive_verification.json](archive_verification.json): ZIP size/SHA, CRC and every included file's pins.
- [disk_vm_audit.json](disk_vm_audit.json): bounded current VM/open-file/snapshot observations and historical storage-reclamation log summary, with attribution limits.
- [validation.json](validation.json):23 controller mocked tests and8 array-loader tests. These do not certify five-day forecast quality.

The run's history files total44,510,740bytes and the complete run-root regular files at the09:22 inventory total52,663,587bytes; symlink views are not duplicate payload. Detailed JVP output and stage CSV counts were zero. A whole-volume GiB-scale free-space decrease cannot be explained by those files alone. The actual allocation source during00:00–03:19 is not inferred from current disk or swap snapshots; see the accompanying disk audit for what could be verified.

All absolute paths in the raw records are execution provenance, not a portable launch configuration. The observer selects common six-hour views from the41 intended three-hour frames: RK3h600 at intermediate checkpoints and h600/h300/h150 only at completion. No final comparison was reached. The frozen scripts and hashes in the ZIP identify what actually ran; no forecast equation, tolerance or filter is changed by this PR.
