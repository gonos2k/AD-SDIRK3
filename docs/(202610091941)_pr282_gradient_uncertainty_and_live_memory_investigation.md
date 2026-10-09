# Final-point gradient and retained-memory investigation

Local timestamp: 2026-10-09T19:41:28.958273+09:00.

## Completed fixed-state evidence

Run 37911606374 passed exact full h=2.5 retained/bounded-tape parity and then completed h=0.625. Its artifact ZIP SHA is 57d87230ec8f3400c38abe331024a05bdb8b406101ba00bd468e1a7bcbf977ac. Independent raw recomputation gives J=59.06273322516124, and prediction increments relative to h=1.25 of 0.1538828837 sigma at 150 s and 0.2130048479 sigma at 300 s. Their ratios to the previous increments are 0.31862 and 0.45760. These improve but do not meet the 0.1 sigma engineering diagnostic; they are not rigorous integration-error bounds. The unchanged h5-fitted center can absorb time/model error, so a rising J is not proof the finer timestep is less accurate.

Run 37913010518 completed the h5 four-coordinate probe. Its artifact ZIP SHA is a21609ddb1ec0839279dbd712a53fae5bdfd26e426d7570f70fa55158be0d7c8. There were 18 calls: one descriptor, one exact bounded h5-center parity, and 16 plus/minus trials at widths 0.005 and 0.0025. Total child wall time was 665.05 s; maximum child RSS was 1023980 KiB. Same state chart, observations, R and vertical brackets were maintained.

Canonical AD norm is 5.4564142e-6. The two-width symmetric-cost Richardson norm is 2.6213082e-5, with AD difference norm 2.8298568e-5. Smaller widths did not converge to AD. Independent frozen-center-residual recomputation separates the exact finite-width curvature term from central cost differences, but those model-dual vectors also remain width-unstable. Per-time vector norms are about 13.86 and cancel by roughly 1.5–3.1 million. Operand-formation/dot proxies near 8e-11 do not account for the discrepancy. This does not identify a production VJP defect or certify full gradient error. No optimization was repeated.

## Measured memory and rejected hypotheses

The same bounded-tape executable measured 1884040 KiB RSS at 120 steps and 7038272 KiB at 480. A one-step trajectory window therefore does not establish a bound on total process memory.

Separate 30×10 local Mac scratch A/B experiments preserved byte-identical primal arrays/cost while refreshing U_ref at each step, clearing last-step graph slots, or releasing invalid Newton Jacobian graph/callback slots. All had negligible RSS differences and were rejected as dominant explanations. No repository production edits followed these guesses.

The allocator probe measured live in-use bytes rising from 88,922,896 at step 1 to 528,801,312 at step 30, about 15.16 MB per step. This is live allocation growth, not merely free allocator pages. A three-step graph probe counted exactly 7,175 reachable output nodes each step, three ConvergedStage nodes and four AccumulateGrad nodes; prior output-root weak pointers expired. The output DAG is not growing across steps. Mac leaks tooling could not obtain a task port; it executed no model trajectory.

A scratch retention-off native FP64 carry experiment stayed near 53.9 MB live allocation and peaked at 118,931,456 bytes RSS. Its primal was extremely close but not bitwise equal: J differed by 7.30e-8 and physical predictions by up to 9.92e-13 m/s. It is not a replacement for the exact parity gate, a gradient certification, or an approved production change. The retained-mode allocation owner remains unidentified.

## Next bounded diagnostic

A manual memory-smoke workflow now builds a temporary source derived from exact test CPP a058e570…, with an early return after three physical bounded steps. The partial CSV is removed; no forecast result is emitted or claimed. Compile/link flags come from CMake/Ninja, scratch quoted includes resolve against the original test directory, and source/scratch/object/archive/executable hashes and commands are recorded. It runs CPU Valgrind with explicit N14/K12, default CPU capability and one thread. Capture completion requires successful process exit, the three-step marker, absent CSV, and heap/leak summaries. A complete capture is not a leak-clean or numerical pass. Unsupported syntax/tools and incomplete capture fail closed and preserve logs.

Green prepared the helper; Red reviewed source pinning, command rewrites, cleanup and workflow selection. Syntax, actionlint and whitespace checks pass. Graphify includes exact affected-source bindings; runtime dispatch, generated scratch code and heap ownership remain extraction gaps. No diagnostic dispatch has occurred for this new mode at this timestamp.

A separate synthetic 80-digit EOS audit found that an algebraically equivalent split/expm1/log1p pressure perturbation formula can improve small-difference precision. It does not establish the runtime cause of gradient uncertainty, so no EOS implementation change has been made.

Production C++/Fortran and ABI remain unchanged. No new WRF run or RK3 field/runtime comparison was performed; qualified PR281 WRF/RK3 evidence remains prior evidence only. Full four-control Eg, time accuracy and error-separated termination remain open. No merge.
