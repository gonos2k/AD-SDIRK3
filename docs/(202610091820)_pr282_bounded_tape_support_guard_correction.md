# Bounded one-step tape replaces unsupported trace capture

Local timestamp: 2026-10-09T18:20:33.272207+09:00.

Diagnostic run 37909883561 failed in the initial setup before the 120-step physical trajectory. The 480-step call never ran. Its artifact SHA-256 is b9429f2f6a0589571be741d1025d9d7a2de70e085e050764c4445f0b8fd9e4c9, independently checked after downloading. The failure is preserved in the evidence directory.

The proposed manual handoff used ArkBudgetTrace to obtain the FP64 final state. That trace enables theta-face capture during slow RHS evaluations, which the declared option-2 metric diffusion contract rejects, including this zero-coefficient setup (implementation line 21811). Local compilation and parser negatives did not exercise that supported-path contract. The failure is a test-path mistake, not evidence of a new physical trajectory defect. No support guard was relaxed and no production source changed.

The correction reuses the existing retained fixed-trajectory API with one step per window. Each step starts from the previous exact FP64 checkpoint, requests one step, advances, verifies one FP64 endpoint and exact FP32 publication, checks finiteness/positive total potential temperature and mass/increasing total geopotential, clones that endpoint, then closes the trajectory. Production carry and retained graph settings stay enabled. There is no ArkBudgetTrace, private state injection or VJP. One retained step is bounded memory, not zero tape; metadata says retains_tape=1 and tape_window_steps=1. The solver overwrites its last-step graph on the next step.

The new explicit flag is --bounded-tape-forward-only. The wrapper still requires exact 120×2.5 s parity against the pinned full-trajectory result, then exact fresh descriptor/context, before one 480×0.625 s call. All data, literal initial state, tolerances, sigma and gates remain fixed. GNU-time wall/RSS are required and recorded. The failed job's 128216 KiB peak is only failed setup memory, not trajectory capacity.

Green implemented and locally rebuilt the C++ test, checked invalid long schedules/conflicting flags, and verified mock wrapper success and stop-on-parity-mismatch. Red approved the one-step trajectory contract and truthful metadata. The current C++ source SHA is e47e3144574ca756489db8564aad0a7886a92cad621807359b17014c592caafd. Graphify was refreshed and source bindings verified; runtime dispatch/context contents remain extraction gaps.

Next: one corrected CPU diagnostic dispatch after source commit. Stop on any parity failure; do not relax the gate or repeat an identical failed run. Time accuracy, the full four-control gradient error and certified termination remain open. No optimization, new WRF/RK3 run, production equation/ABI change, or merge was performed. Qualified PR281 WRF/RK3 comparison remains prior evidence only.
