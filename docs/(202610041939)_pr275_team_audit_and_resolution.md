# PR275 independent Green/Red audit

Local timestamp: 2026-10-04T19:39:54+09:00.
Review baseline: PR275 HEAD `4b39fb4f759834a3bf4ba1b327e02562253a3e71`.
The original `/Users/yhlee/SDIRK3` checkout was not modified.

Four fresh gpt-6-luna/high agents independently reviewed production indexing,
wave mathematics/meteorology, artifact provenance, and CI/portability. Cached
Graphify source bytes matched the reviewed HEAD; its relationships were used
for navigation and checked against actual source and artifacts.

No additional production numerical defect was demonstrated. The U HPG slice
and packed periodic neighbor correction match the physical and packed layouts
and the original Fortran equation. The following lower-severity omissions are
being resolved; these change tests/evidence, not the production operator.

- [x] Inventory digest serialization: `28f23e...` is SHA256 of raw CTest JSON
  stdout, while the archived Python-pretty-printed JSON hashes to `b4229f...`.
  Both parse to exactly the same inventory. The receipt now labels each digest
  explicitly. Test-count and passing-partition claims were independently checked.
- [x] Fortran dependency documentation: GNU Fortran was already required by
  the existing source-extracted option-2 oracles. It is not a new configure/build
  regression. The README now distinguishes the C++ core compiler from the GNU
  Fortran executable needed for the full CTest suite.
- [ ] Eigenmode qualification: an Nmax frequency cutoff alone does not prove
  an internal-gravity branch. The test now labels the selected mode as a coupled
  sub-Nmax oscillation, checks its scale-free eigenpair residual, and checks
  finite physical kinetic/available-potential participation over its two
  quadratures. A pure velocity or displacement quadrature may have a zero
  reservoir; positivity is required for the pair, not both reservoirs at each
  individual phase. No eigenspectrum, coefficient or observed frequency was fit.
- [ ] Energy-interchange gate: the old OR admitted change in only one reservoir
  and its self-normalization could overflow for almost-zero initial APE. The
  new gate requires finite, material opposite-signed changes in total kinetic
  and APE in both the reference and native selected mode, at the same sampled
  quarter time. A common initial reservoir scale retains the 10% materiality
  condition. An explicit amplitude/decay gate uses the existing 1% component
  comparison scale. Total-energy and top-boundary-work closure remain unclaimed.
- [ ] Nonperiodic guard coverage: the original actual-RHS fixture disabled
  `mass_pgf_bc_guard`. The additional fixture enables the existing nonperiodic
  one-sided branch, checks every interior face and both boundary slots with an
  independent EOS/Fortran-formula oracle. Existing periodic and packed cases
  are retained. No new boundary capability is being implemented.
- [ ] Rebuild and run the affected HPG/wave checks; keep the original passing
  data as historical snapshots, append new exact source/executable receipts,
  refresh source-matched Graphify, and obtain final cross-review before push.

The original production WRF archive/executable/input evidence is unaffected by
these test-only changes: FP64 ON completed 48/48 stages and OFF rejected the
first timestep's Stage2. The archived same-setup RK3 comparisons remain valid;
no new WRF run or full clean build is warranted by these audit changes.

At this snapshot, old-HEAD CI run37194609801 has successful fast-contract and
negative-contract jobs; the Linux core CTest job is running. This is not a
claim of final CI success or validation of the new, still-uncommitted assertions.


## Final local verification and closure addendum

Local timestamp: 2026-10-04T20:22:10+09:00.

The final stable-wave CTest selection passed both registered cases: forward in
135.39 s and inverse in 291.13 s (426.54 s total). The complete
`LastTest.log` was copied before any subsequent CTest could overwrite it. The
before-run source/executable receipt and archive tie the run to the exact test
sources, reference, producer, executable, and static library. The wave test
selected `coupled_sub_Nmax_mode`, had a scale-free eigenpair residual of
3.44e-17, and showed opposite-signed, material kinetic/available-potential
changes in both native and source-reference quarter-period samples. This is a
modal energy-exchange check; total-energy conservation and top-boundary work
remain unclosed. The withheld inverse forecast scored all 105 sampled physical
W points, with relative W error 2.99157e-4 against the independent reference.

The final `test_horizontal_pgf_actual_rhs` target was built and run directly
without using CTest's shared log. Physical, packed-periodic, and guarded
nonperiodic layouts all passed the independent EOS/Fortran-formula oracle. The
guarded case uses open X, Omega off, and test-only `precond_type=0` set before
fixture construction so the setup step does not enter the WRFParity-only raw
preconditioner. It checks 168 owned interior samples and both one-sided
boundary slots. Its max RHS error was 1.37074e-8 m/s² against a 4.39059e-6
m/s² budget. This validates the horizontal pressure-gradient RHS branch; it
does not claim calc_ww_cp support for open-X WRF integration.

The earlier guarded-case aborts were test-fixture setup mismatches. Legacy with
the default raw-principal selector was rejected before the oracle; skipping
fixture priming left an internal RHS tensor undefined; and setting the
no-preconditioner selector after solver construction did not replace the
already selected preconditioner. Moving that test-only selector before
construction restored the supported setup path. No numerical tolerance was
changed, no production guard was removed, and no production C++/Fortran source
was edited.

Graphify now has the two final C++ test-file bytes in its 25-file code corpus
(563 nodes, 1,444 edges); the source-extracted AST delta contained 141 nodes and
398 edges, with no new graph topology on the final label-only refresh. The
component README digest is recorded in the resolution receipt; this refresh
remained code-only. The wave source used for execution differs from the current
file by one success-message label string only. The full byte diff is archived
in the before-run receipt, and formulas, checks, and control flow match.

No new WRF model run was performed. The same-setup `em_b_wave`/RK3 field and
runtime comparison in the validation report remains the existing evidence.
The old-head GitHub run 37194609801 was last observed in progress: both
`fast-contracts` and `build-contract-negatives` passed, while `core-linux`
remained on CTest step 14. That run predates these final test-only sources and
is not evidence for them. Exact local receipts, test logs, graph hashes, and
failed-setup classifications are in
`docs/evidence/stable_wave_coupling/team_audit_resolution.json`.
