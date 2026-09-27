# PR #247 current CTest count gate repair

Local timestamp: 2026-09-27 06:45:28 JST (+0900)

Context: manual CI run `36273573836` on PR #247 HEAD `d0acbad60f3807e17b40077c71f5d6892e150706` failed the `fast-contracts` step `README ctest count matches the pinned inventory`. The pinned inventory contains 106 tests, and all three current-state claimants (`README.md`, `external/libtorch_wrf/sdirk3/README.md`, `external/sdirk3_lib/CLAUDE.md`) state 106. The discovery also found `docs/(202609262309)_t1_stage2_rejection_snapshot.md`, whose 104-test statement accurately records its historical run. Rewriting that evidence as 106 would be false.

Change: `.github/workflows/sdirk3-ci.yml` now excludes only timestamped `docs/(YYYYMMDDHHMM)_...` reports from the current-count discovery. It still scans untimestamped Markdown, including all three current claimants and future current-state docs in `docs/`. No solver, test, or pinned inventory file changed in this commit.

Local validation: the exact shell claimant discovery returns the three current files, each 106 against the 106-name pinned inventory. A pattern control retains `docs/current_verification.md` while excluding a timestamped report. `actionlint .github/workflows/sdirk3-ci.yml` and `git diff --check` pass. Graphify's code-only update found no code-graph topology change; its extractor does not provide evidence for this YAML gate, which was checked against the authoritative workflow text. The next action is an exact-head CI rerun after pushing this change.

No WRF build, `em_b_wave` model run, or RK3 field/runtime comparison was repeated for this workflow-only change. The last same-setup PC2/RK3 evidence remains the byte-identical one-step pair in `(202609270635)_integrated_option2_u_fixed_mass_validation.md` and `(202609270631)_option2_u_fullrhs_pr246_wrf_receipt.md` at the unchanged production source hash `cbf9dcbbbcb4ae7be00b0da5680d96020df141fd56613e3b3b053c3ec75f22f3`.
