# Native idealized twin: Green closing review

Local timestamp: 2026-10-23 17:12:29 JST

## Scope and provenance

This review closes the completed synthetic two-control E0/E1/E2 idealized-twin evidence. It does not establish real-observation assimilation, all-direction behavior, or global FP64 reverse-mode fidelity.

- Worktree: `/private/tmp/native-idealized-twin-20261022`, HEAD `e111d2bea3a2c3fa34eb117b131cb03cf89eda5f`.
- Frozen protocol SHA256: `ee21ce1734542a2b558903a4bf45c6d6f41a6ff923991b41c0336732321e546d`.
- Launch binding SHA256: `112defb345990633f18a06734324ff4930a1a7e1d941b3823006bc73d33abfe8`.
- Base native executable SHA256: `7fdb04fd124ea1d25250081b16d0ce35f1b23ce849fd615ff156bfff092ab6c4`.
- V5 controller SHA256: `4c0e30d8b25142e3696bea9f465aaaf844b1012538886313f2686fba01a277dc`; problem module SHA256: `ca82a65cb6b1cc8c1f60cdfff4b58184e887f4a10606826214629bc01d8b8b91`.
- Input pins: `namelist.input` `a6ea8f8ab62a74d72840ea46de4ac39a20d5d4247db77ebced0b84a1433dbdc5`; `wrfinput_d01` `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`; `diagnostics.txt` `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`.
- Completed summary SHA256: `a579c12b1798f42930bd2b460cb869963200df7a860b8e231f102fc31b70321a`.

Full source, build, source-case, artifact-manifest, and input-pin records are in `portable_twin/manifest.json`. The selected-state NPZ retains checkpoint rows only; full native checkpoint ledgers and raw receipts remain under the original evidence `runs/` directory.

## Green recomputation

The recovered control is `[-0.49999999659290667, 0.5000001939857451]`. One update was accepted at alpha 1.0, with two objective forwards and two logical gradient calls. Recomputed terminal cost is `2.6096924689424757e-13`, the final-to-initial cost ratio is `2.528955857960073e-14`, normalized final gradient infinity norm is `7.820345620803177e-08`, and control relative error is `2.743795823310935e-07`.

E0 passed its zero-innovation gate with J=0, gradient `[0,0]`, and zero accepted updates. E2 detected all ten declared malformed-input mutations. The independent FP32 lambda-chain checks preserve the two failed attempts and route the logical 096 and 192 windows through their explicitly pinned completed replacements.

The two FD observation-sensitivity matrices have singular-value ratios `0.44896537144675336` and `0.4489653714291307`; their 2-norm difference is `2.913508882971655e-08`. All 12 finite-difference gradient comparisons pass, with maximum relative error `2.3358955757682184e-08`. The exported H/Hᵀ dot-product checks have relative errors `2.0118234002889807e-16` at step 128 and `1.2243527162120896e-16` at step 256.

The final/background six-field withheld composite ratios are `2.9583362274687663e-07` at step 64 and `3.520915757497751e-07` at step 192. The corresponding unobserved-owner ratios at observed times 128 and 256 are `3.2437922545047927e-07` and `3.3983076781221076e-07`. All accepted and preserved-failed receipts report finite fields and positive physical minima; across the 41 attempts, minima ranged from 302.2520499229431 K for theta, 9247.3818359375 Pa for layer thickness, and 89088.62293672562 Pa for column mass.

Accounting is 41 actual native attempts and 6,880 attempted model steps: 39 completed RC=0 receipts and two preserved RC=1 receipts. The failures `e1_grad_000_096` and `e1_grad_001_192` remain counted but are excluded from accepted chains.

## Profiled 192-step substitution

The one copied proof, `portable_twin/proof_192_equivalence.json`, has SHA256 `405942141aeada1146cecff380b0e4b0a44463a1b8c5a87f9f1202548f439f7d`. It pins the failed 192-step call, profiled replacement receipt, exact checkpoint/F64-gradient/F32-gradient payload equality, trace, source/link/core artifact pins, and unchanged protocol. The trace records a finalized return. This proof applies only to the saved `e1_grad_001_192_mpi_trace` artifact; the prior MPI failure cause remains unknown. The later native calculations, including the final 32-step window, used the original base executable.

## Independent portable verification

`portable_twin/recompute.py` runs from the portable directory with Python 3 and NumPy only. It verifies the exported array and protocol pins, recomputes H/Hᵀ and their dot identity, J, P·lambda0, finite-difference sensitivities and gradients, rank, Gauss–Newton H/H0, E0/E2 gates, optimizer trace values, physical receipt summaries, and withheld/unobserved scores. It passes and writes `portable_twin/portable_verification.json`.

The main collector `green_postprocess_native_twin.py` is specific to the external raw evidence paths and is not the portable recomputation command. Its final SHA256 is `ceff226d9ac09aa7d549f6cae8632617422ba1d847621fc367662029cc1be9a6`.

Red's final review is `red_final_twin_review.json`, status `PASS_DECLARED_TWO_CONTROL_IDEALIZED_TWIN_CLOSURE`, SHA256 `41c53ebf88707f91f735bd81eeebb47a6680bc3720129a97aefce75f0c058ef2`.

No new RK3 run or same-setup RK3 field/runtime comparison was performed in this closure. No runtime or speed claim is made. The archived RK3 comparison context remains separate.
