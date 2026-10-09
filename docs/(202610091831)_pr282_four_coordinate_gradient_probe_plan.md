# Four-coordinate final-point gradient diagnostic

Local timestamp: 2026-10-09T18:31:54.069536+09:00.

The original final-point scalar directional derivative combines large opposite-sign contributions from 150 and 300 seconds. Observation transpose and the recorded K10/K12 linear-solve tightening are already qualified. Neither proves the absolute model-stage gradient accuracy. A single directional comparison cannot certify the full four-control vector.

The new bounded CPU diagnostic uses the pinned local chart Z0(delta)=literal_saved_state+Bcan*delta, with four coordinate axes and predeclared widths 0.005 and 0.0025. This is not a new absolute-control/truth-coordinate equivalence. All 105 observations, times, sigma, context and h=5 s / N14 / K12 settings remain unchanged. It first checks the fresh descriptor, then requires bitwise bounded-tape h5 center parity against the actual saved K12 CSV. Only then does it evaluate 16 plus/minus forward trajectories. No new VJP or optimizer is run; the saved K12 pullback is projected onto the same Bcan.

Every perturbation requires exact serialized/native initial state, fixed observations, unchanged height brackets, finite state/scalars and per-step admissibility metadata. GNU-time elapsed/RSS and calls are saved incrementally, including failure. The central cost difference uses each time's average residual dotted with prediction difference, divided by 2*epsilon*sigma²; it avoids subtracting two close scalar costs. Per-time and summed four-vectors, two-width Richardson values, differences to AD and operand-floor/width-change proxies are reported.

The operand proxy covers only FP64 operand formation and dot reduction; the width difference is empirical. Neither includes a proven bound for stage root/RHS/conditioning error. The script explicitly declines full Eg or stationary-point certification. Smoothness is local to the fixed interpolation brackets; no general Hessian or higher-order differentiation engine is introduced.

Green implemented and mocked success/failure ordering; Red independently approved the formulas, local chart and corrected bounded-mode parity gate. Python syntax, actionlint and diff whitespace checks pass. Graphify includes this caller and exact source bindings; runtime CLI/YAML/context contents remain extraction gaps. The time probe 37911606374 is still in progress; no gradient probe dispatch has occurred at this timestamp. Dispatch this diagnostic only after the current bounded-time path passes its parity contract.

Production sources/ABI are unchanged. No new WRF run or RK3 field/runtime comparison was performed; prior PR281 WRF/RK3 evidence remains prior evidence only. Time accuracy, full gradient error and certified optimization termination remain open. No merge.
