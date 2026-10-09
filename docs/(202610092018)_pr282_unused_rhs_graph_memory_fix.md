# Remove unused RHS graph-retaining work

Local timestamp: 2026-10-09T20:18:08.481590+09:00.

The retained path's process RSS rose from 1884040 KiB at 120 steps to 7038272 KiB at 480 despite closing one-step trajectories. Live allocator statistics rose by approximately 15.16 MB per step; output graphs remained at 7175 nodes and prior output roots expired. Reference-state, last-step graph and invalid Jacobian-cache scratch A/Bs were negative. Linux Valgrind capture 37920084928 observed no lost blocks at object destruction and still-reachable allocations of 357008 bytes. Its 217 dtype-conversion uninitialized-value warnings remain unresolved; it was a capture, not a clean numerical/Memcheck verdict.

Source and a single-field A/B isolate the owner: t_w_work_ was zeroed and filled in place from differentiable temperature, but the interpolation had no consumer. Clearing only that unused field after each close preserved the complete 30×10 CSV bytes while reducing RSS from 604880896 to 153436160 bytes and flattening live allocation near 74.3 MB. Thus this issue is owned retained memory, not an unfreed lost block or growing returned-output DAG.

The smallest fix removes exactly the unused interpolation block and private member. No live state equation, boundary, option, convergence threshold, test tolerance or gradient is detached/changed. Red confirmed no consumer, accessor or external ABI dependency. Private C++ layout changes, so the core and test callers were freshly rebuilt with matching headers. Source hashes: header 244970b6c874ef50d3fc85477d463bc846c32b440a9ff834740590728cb9439f; implementation c0fc5c3fd7aa0e46c0f9f044e8405fc0f203209156f1a15a4629622a5cfac99c.

Fresh 27-target Release build and 30×10 N14/K12 smoke passed with byte-identical baseline CSV and peak RSS 141475840 bytes. Fresh archive a12e8c68fd326a90e7d1ec23f6d5dabb78083054c214d6ef96258c976c7e6b49 was relinked into WRF. Same-input one-rank em_b_wave completed 48 stages, all 174 floating variables were finite and full mass/thickness positive, with three files byte-identical to prior PR281. The exact archived RK3 field/runtime comparison is in (202610092012)_pr282_wrf_regression_fresh_archive.md. No speedup/equal-accuracy claim follows from a single run.

Graphify was refreshed and exact source bindings checked; runtime/generated scratch ownership and context contents remain extraction gaps. No lost-block diagnosis or EOS precision hypothesis was promoted into a production change.

Next bounded diagnostic is one same-literal-state 960×0.3125 forward, comparing to the actual pinned h=0.625 CSV. Same observations, positions, sigma and tolerances remain fixed. The h=2.5 exact parity/context gate still precedes the selected long call. The additional gzip has deterministic mtime=0 and independently checked raw/provenance hashes; the original fixture is unchanged. This run measures forward time sensitivity only, not a new VJP, optimization or stationary-point certificate.

Required full CPU CI and the 0.3125 diagnostic are pending at this timestamp. They will use separate branch references to avoid cancelling each other under workflow concurrency. Full gradient Eg/error-separated termination remain open after the width-unstable four-coordinate probe. No merge and no original dirty-worktree edits.
