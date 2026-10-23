# Native idealized twin: completed E0–E2

The declared dry `em_b_wave` two-control, 240 s native experiment is complete.
The fixed forward setting is h=0.9375 s, 256 steps, Newton absolute 1e-9 / relative
0, Krylov 1e-7, EWT 1e-6, one MPI rank / one OMP thread, internal FP64 carry and
actual Fortran FP32 cotangent handoffs. Production equations, defaults and public
ABI were not changed.

| Independent gate | Result |
|---|---:|
| E0 cost / control gradient / update | 0 / 0 / 0 |
| Minimum singular-value ratio, both widths | 0.4489653714 |
| Maximum relative FD–VJP difference, 12 comparisons | 2.3358952e-8 |
| E2 deliberate errors detected | 10/10 |
| Final / initial observation cost | 2.528955858e-14 |
| Normalized full control-gradient infinity norm | 7.820345621e-8 |
| Relative control recovery error | 2.743795823e-7 |
| Unused 60 / 180 s state-error ratios | 2.958336227e-7 / 3.520915757e-7 |

There was one accepted optimizer update, with two E1 cost forwards and two
logical E1 gradient evaluations. The overall ledger counts **41 native attempts /
6,880 model steps**, including **39 RC0 completions and two preserved RC1
failures**. It remains within the original 165-attempt / 28,992-step caps.

## Recompute without WRF

With Python and NumPy installed, run from any working directory:

```sh
python3 docs/evidence/native_idealized_twin_execution/recompute.py
```

Or run `python3 recompute.py` within this directory. It writes
`portable_verification.json`. The portable verifier reads only the included
files: no WRF executable, repository source, original absolute paths or full raw
checkpoint ledgers are needed. A separate-copy test denied reads of the original
worktree/evidence folders and still passed; see
[the isolation record](provenance/portable_isolation_test.json).

The NPZ contains selected FP64 state rows, the represented initial states and
full packed control modes, observation slots/values/sigma, and actual FP32
cotangents. The verifier reconstructs canonical owner masks and point selectors,
H/H-transpose, objectives, P-times-lambda0, both-width whitened sensitivity
matrices A and A-transpose-A, FD/rank, control recovery and full six-field
withheld/unobserved state RMS. E0/E2 decisions, optimizer trace and physical
receipt summaries are explicitly metadata checks; they do not rerun the model.
The state-error epsilon scales are fixed and separate from observation R.

`objective_by_case.csv` and `fd_gradient_checks.csv` are small numerical tables.
`recomputed.json` and `portable_verification.json` retain the detailed physical
RMS, including unobserved positions at 120/240 s. `protocol.json` is the frozen
execution policy; its readiness status and the historical plan are not rewritten
to mimic a result. `manifest.json` binds the arrays, records and original native
receipts. The [384-row observation ledger](provenance/observations.json) records
native variable, stagger, coordinates, owner ordinal, global index and packed
slot. No observation values, sigma, masks, control modes or thresholds were
adjusted after the results.

## MPI failures and the saved 192-step artifact

The [original failed-launch logs and paired run receipts](provenance/mpi_attempt_records/)
are preserved. Both original MPI launcher return-code-1 records remain failed and count toward
the attempted-work budget. They are never normalized to success. One matching
096-step base-executable recovery completed RC0 with identical payloads. The
second matching 192-step diagnostic used a separate, post-computation MPI
observer build. Its 193 FP64 checkpoints and FP64/FP32 gradient payloads are
byte-identical to the failed run, while its trace records exactly one PMPI
forward, `ierr=0`, `finalized=1` and process RC0. The observer does not synthesize
status or add an MPI_Finalize call.

The [case-specific proof](proof_192_equivalence.json) and V5 caller permit only
that **one saved artifact** at its exact inputs/control point. This is not a
general executable-equivalence claim. The remaining five VJP windows completed
on the original executable. The cause of the historical MPI failures remains
unknown; no MPI deployment-reliability claim follows from this study.

The source of the optional observer is
[`native_mpi_finalize_trace.c`](../../../external/libtorch_wrf/sdirk3/tests/native_mpi_finalize_trace.c).
The [build/link records](provenance/) preserve its exact extra-object link delta,
common numerical objects, compiler/MPI/libtorch stack and executable hashes.
Full native ledgers, original receipts, log/seed/reference/cotangent files,
archived caller revisions and unsuccessful preparation records remain at
`/Users/yhlee/AD-SDIRK3-review-evidence/native-idealized-twin-20261022`.
The portable subset does not replace those raw records or independently replay
all model steps.

## Scope and validation records

The control starts after WRF initialization/CQ. This is a noiseless, two-mode
numerical twin with U/V/dry total-theta point observations at 120/240 s, no prior
penalty and fixed research R. It is not real-observation assimilation, arbitrary
initial-field recovery, all-direction differentiation, an independent global
FP64 reverse map or a forecast test beyond 240 s. Optional E3 and broader physics,
MPI configurations, larger timesteps and deployment remain separate.

[Green](provenance/green_final_review.md) and
[Red](provenance/red_final_review.md) independently checked actual native evidence.
[Graphify provenance](provenance/graph_post_v5.json) records navigation/source
coverage and extraction gaps; graph edges are not numerical proof.

No new RK3 model run, field subtraction or runtime comparison was performed.
The [preserved same-input RK3 record](../research_forward_setting/stock_rk3_secondary_comparison.json)
is context only: different filtering prevents treating it as accuracy truth,
and its timings do not support a speed ratio for this inverse instrumentation.
Existing standalone CTest/replay CI results are reported separately and do not
replace this native recovery experiment.

## Source-pinned standalone CI

Numerical source commit `0e57da8e262afec7293d1be744870904fa1e7a02` passed
[SDIRK3 CI run 39924066107](https://github.com/gonos2k/AD-SDIRK3/actions/runs/39924066107).
The [preserved ZIP and verification](provenance/ci_source_0e57/) bind the GitHub
artifact digest/CRC, five passed selected CTests and the direct FP64 byte
comparison of the two 8,480-component retained/replay initial pullbacks.
Sixteen endpoint arrays are reported equal by CI metadata; their array bytes
are not independently supplied/compared in that ZIP. This standalone fixture
is distinct from the 24,531-component native twin.

The [source-commit attestation](provenance/numerical_source_commit_attestation.json)
checks the committed numerical/helper bytes against the frozen execution and
cached Graphify records. Subsequent CI-record-only documentation changes do not
change that numerical source revision or require another native experiment.

The repository review's documentation-ignore packaging note was resolved by
explicitly staging the report and complete evidence bundle in numerical-source
commit `0e57da8e`; the data files are tracked even though `docs/` is globally
ignored for unselected investigation output.
