# Final session audit and publication

Local timestamp: 2026-10-11 14:39:43 JST (+0900)

## Context

PR #290 is merged at main `1b0ccea8371d70d6de95a124ef389beff3ee31df`. A fresh Green/Red session audit found no P0/P1 scientific issue in the completed dry, flat, constant-f, two-control CPU-tile case. The original producer `746ad78d029c6cf92c498288daee445fe4b3c22d`, source/input hashes and raw inverse/forecast artifacts remain unchanged. No native solver or Fortran code changed in this follow-up.

## Resolved omissions

- Publish the ignored local closure record, all 16 source-backed error comparisons, fresh Green/Red audit receipts, and raw archive with a verified snapshot manifest.
- Replace local-only links in the published document with repository-relative links and rewrite the PR description around the completed case.
- Separate the historical monitor failure from the clean final index, update the latest-progress pointer, and archive the dead PID lock. Original failure logs are preserved.
- Record measured old/fixed source frequency and its tiny host-dependent difference from the earlier independent decimal. The same-host branch, eigenvector and four input files are unchanged.

[Closure record](<(202610111426)_pr290_narrow_rotating_forecast_closure.md>) · [Audit resolutions](evidence/20261011/pr290_final_audit_resolutions.json) · [Green audit](evidence/20261011/pr290_final_audit_green.json) · [Red audit](evidence/20261011/pr290_final_audit_red.json)

## Validation and scope

The inverse and four forecast calls already passed raw archive, source/executable/input, profile, finite-state and exact cold-restart checks. All eight quantities improve at both 600/900 seconds. This documentation/evidence follow-up does not rerun optimization, gradients, energy tests or native forecasts. Package CRC and every snapshot member hash passed. Documentation links, JSON/CSV arithmetic and repository diff checks are checked before publication.

No new WRF `em_b_wave` or same-setup RK3 field/runtime comparison was performed. General WRF/RK3 validation, formal Eg, full-state stationarity, stronger rotation, moist/terrain/MPI generalization are separate from this completed case.

## Separate historical item

GitHub run `31118092164` has been queued since August on the unrelated `agent/9f-d125-digest-cannot-lie` branch. It is recorded without cancellation; no current inverse/forecast run is active and it does not affect this case.

## Next action

Publish this follow-up PR for review. No further numerical computation is required for the declared case.
