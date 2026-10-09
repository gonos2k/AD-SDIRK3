# One-step tape cleanup and positive execution check

Local timestamp: 2026-10-09T18:28:55.576574+09:00.

Run 37910691516 completed the 120×2.5 s physical trajectory, then aborted because the common return path called closeFixedTrajectory after the bounded mode had already closed its last one-step window. Its ZIP SHA-256 is 7134545ddd9fd40ad90f033229f27af3e049fe527e967f5f1a0aa5b1f993e009. The artifact is preserved; the 480-step call did not run.

Only complete rows can be reused from the aborted CSV. Independent extraction found exact equality to the pinned baseline for the literal initial state, both FP64 checkpoints, both physical predictions and observed_150. Observed_300 was truncated to six entries and the objective was absent, so this is not a completed parity gate. The required corrected run will repeat all parity checks without lowering any threshold.

The C++ test now calls the common final close only outside bounded-tape mode. Each bounded iteration still closes its own one-step trajectory. Final source SHA: a058e570e1527b5167c0f999188ecc3f520c48a6421cf18c8f2c7098320b9967. Green rebuilt locally and completed an actual 30×10 s Mac native smoke with the archived literal inputs in 25.03 s; the complete CSV has both checkpoints/predictions and the expected bounded-tape metadata. This smoke verifies completion only, not Linux descriptor/bit parity. Red independently approved the cleanup contract.

The Linux aborted process measured 1883608 KiB maximum RSS over 72.06 s. Source inspection finds no growing trajectory-step vector: each window is cleared, per-step stage vectors are local, and the previous step graph is overwritten. This establishes a one-step tape window, not a bound on total allocator/caches/process memory. The 480-step capacity claim remains pending measured successful execution.

The next action is one corrected parity-gated CPU diagnostic at unchanged state/data/R/tolerances, then h=0.625 s only if exact h=2.5 s parity succeeds. No optimization, VJP, production C++/Fortran/ABI change, new WRF/RK3 comparison, or merge. Earlier WRF/RK3 regression remains qualified prior evidence only.
