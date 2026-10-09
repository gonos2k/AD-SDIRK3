# Fixed-state time accuracy probe with manual FP64 handoff

Local timestamp: 2026-10-09T18:12:20.628071+09:00.

## Context and measured limits

The same literal 16×12×8 returned state, 105 physical observations at 150 and 300 seconds, and sigma = 3e-4 m/s remain fixed. No observations, initial state, optimizer, production equations, or production tolerances are changed.

The completed diagnostic runs are 37900843624 (h=2.5 s) and 37905770290 (h=1.25 s). Their costs are 19.42246325875459 and 45.448407679881626. The successive prediction differences at 150/300 s are respectively 0.45980/0.41901 sigma and 0.48297/0.46549 sigma. These do not meet the predeclared 0.1 sigma engineering diagnostic. A rising cost at this fixed point does not establish that a finer timestep is less accurate; the h=5 s fitted initial state can absorb model and time-discretization differences. Full four-control gradient accuracy and certified optimization termination remain open.

Run 37903388504 failed before integration at a fresh eigenbasis mapping assertion. It is preserved, not counted as a model run. The later probes use an exact saved physical center and pinned incremental basis. That basis is not claimed bit-identical to the old Linux absolute-control basis.

## Small test-only change

The existing physical CLI now accepts an explicit --fp64-handoff-forward-only mode. It disables retained adjoint graphs and solver-owned carry, passes the previous accepted FP64 state through the existing test-only one-shot input, and uses the same unified step. It captures the boundary-projected final FP64 state and checks exact FP32 host publication, finiteness, positive total potential temperature and column mass, and increasing total geopotential in every owned column, initially and after each step. Only the two required endpoints are retained. This is manual test handoff, not a new production carry feature or a checkpoint framework.

Schedules 480×0.625 and 960×0.3125 are allowed only for this mode. The current planned job uses 0.625 s only. Existing retained schedules are unchanged.

The wrapper first runs 120×2.5 s and requires bitwise parity with the pinned actual retained baseline: initial state, both FP64 checkpoints, both physical predictions, observations, vertical brackets, and objective. A mismatch stops before the 480-step call. It then checks the fresh descriptor/context before one 480×0.625 s forward call. There is no VJP, gradient, optimization, or stationarity claim from this new mode. GNU time records actual per-process elapsed time and maximum RSS; the solver's placeholder tensor-size estimate is not treated as process memory.

Fixture ZIP SHA-256: e170be5d1e2b0d87efcdc9ebe0f4dd1d381aceb9e0777dcd074e0dcfe1329d8c. It includes the exact h=1.25 CSV (d1554e15ccfff09a3625f03a564b5ca8bd9ad6f469a1b4dac965e1ae5e28feec), sourced from the completed artifact ZIP 82bbe0c32ff4934afa1958cf6435b685d58a473b7725eb3510fa81d721c41f28.

## Validation before dispatch

Green implemented the bounded C++ path and wrapper; Red reviewed state offsets, physical checks, no-retention configuration, fixture hashes and fail-closed ordering. The affected C++ test compiled and linked locally; invalid long taped schedules and conflicting flags were rejected before output. Python syntax and mock success/mismatch ordering passed. GNU-time parsing was checked with tab-indented output and its colon-containing elapsed label. Workflow actionlint and diff whitespace checks pass.

Graphify was inspected and refreshed with nine exact source/mirror bindings: 378 nodes, 947 edges, 15 communities. Runtime CLI/YAML dispatch and numerical context contents remain extraction gaps. The graph is navigation evidence, not numerical proof.

The next action is one CPU diagnostic workflow dispatch on the isolated diagnostic branch. Its parity and h=0.625 results are pending at this timestamp. It is not the required full regression suite.

## Scope and next actions

- Confirm exact retained/manual h=2.5 forward parity before the long call.
- Evaluate unchanged-state h=0.625 prediction increments and stable residual-based cost differences; record measured memory and wall time.
- Keep time accuracy, full four-control gradient uncertainty, and error-separated termination open until their evidence meets scope.

Production C++/Fortran and ABI are unchanged in this work. No new WRF model run or same-setup RK3 field/runtime comparison was performed; qualified PR281 48-stage WRF and archived RK3 comparison remain prior evidence, not a new result. The original dirty worktree is untouched. No merge.
