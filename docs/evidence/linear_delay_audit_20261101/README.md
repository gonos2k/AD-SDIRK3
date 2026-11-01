# Matched solve/JVP CPU and elapsed windows

This is one diagnostic-only two-case h600 block, not a speed acceptance test. It keeps Direct P1 default-off, the numerical equations and original tolerances unchanged. Both models completed and each arm matched174 numeric arrays from its own A8 archive. The original OFF runner remains INCONCLUSIVE due a missing Python import; the same saved-output checks passed in a separate pinned offline completion. No model rerun occurred. ON completed the diagnostic validation directly.

The fifth solve has32 AD JVP/RHS calls, FD/fallback0 in both. OFF7.723/7.721 s wall/process CPU versus ON14.302/11.669 s. The nested JVP interval is5.312/5.310 versus9.915/8.182 s. CPU and arithmetic wall-minus-CPU both increase; that arithmetic is not a measured wait or specific cause. The captured Ueval/reference/direction/JVP differ, while recorded geometry/scales/boundaries match. Bounded hashes do not prove a full equal operator. The old45.072-second JVP sample was not reproduced.

The entire rank retires fewer instructions in ON despite greater CPU/elapsed time and more involuntary context switches. These are aggregate counters, not fifth-solve hardware counters or a core/frequency/scheduler/app/swap diagnosis. A current-SDK binding was approved before models: SDK27.2 vs historical27.0, unchanged linked paths/compatibility and minOS26.0 <= host26.7. Exact historical compiler binary is not established. No backward causal claim is made.

Large raw first-point artifacts and the four additional fifth-point FP64 NPY files remain local, pinned by original paths/hashes. This public set has summaries, scripts, figures and receipts, not complete restart/Tile-operator replay state. Reproduction requires the pinned raw fixtures and current stack. Public scripts use readable names/whitespace-only normalization; executed original hashes remain. Existing diagnostics were preserved; no giant graph or per-iteration dump was added.

See [REPRODUCING.md](REPRODUCING.md) for the archived filenames, hashes, and local prerequisites.
