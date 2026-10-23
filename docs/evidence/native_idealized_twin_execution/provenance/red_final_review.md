# Red final twin review

Reviewed: 2026-10-23T17:07:12.878669+09:00

Status: **PASS — declared dry two-control idealized twin closure.**

The final raw 32-step VJP cotangent has SHA256 `74e44d0f098cb702334742e4176fe08ae58a559fb186905bbf3823689b1ec4cb`. Recomputing `P @ lambda0` from that FP32 cotangent reproduces the reported final gradient exactly: `2.34079699076367e-07`, `2.686487945828038e-06`. Recomputing the objective from the accepted native checkpoint states at steps 128 and 256 gives `J=2.609692468942476e-13`, equal to the minimizer record; `J0=10.31924879482684` and `J/J0=2.53e-14`.

The accepted control is `-0.499999996593, 0.500000193986` with relative error `2.74e-07`. Independent held-out composite ratios at steps 64 and 192 are `2.96e-07` and `3.52e-07`. The final normalized gradient infinity norm is `7.82e-08`. The recorded minimizer used one accepted Armijo update, two objective forwards, and two logical gradient calls.

All six final VJP windows completed with RC0. The profiled 192-step window is covered by the exact V5 artifact pin; windows 160, 128, 96, 64, and 32 use the original executable. Cotangents connect byte-for-byte across the chain, including the exact step-128 observation injection. E0 passed its dedicated zero-innovation gate, both-width rank ratios are `0.449` and `0.449`, all 12 finite-difference comparisons passed (maximum relative error `2.34e-08`), and E2 detected all 10 mutations.

The optimizer takes cost/gradient callbacks and an inverse metric; it does not receive the true control. The known twin truth is used to generate observations and for post-run scoring. This closes only the declared idealized twin case. The cause of the original untraced MPI RC1 remains unknown; this evidence does not establish real-observation assimilation, all-direction behavior, or global FP64 reverse-mode fidelity.

Evidence JSON: `red_final_twin_review.json` (SHA256 `41c53ebf88707f91f735bd81eeebb47a6680bc3720129a97aefce75f0c058ef2`).
