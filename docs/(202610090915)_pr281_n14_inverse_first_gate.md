# PR281 N14 inverse first gate

Local timestamp: 2026-10-09T09:15:22+09:00.

Diagnostic run 37862698779, ZIP SHA `4e973e26d560501aac436551fa70d13ea750d1b64a12dbdcba7cab46d3cfd588`, report SHA `a9bc22afa15a84f4654839d04fabd79cc582298f232a4bd333a875370a2c7a20`, reused exact saved candidate/Bf/observations. Both N13/K10 and N14/K10 roots completed after the Krylov correction. N13 gradient norm 1.734989558e-6, N14 1.646595131e-5, gradient difference 1.810543274e-5. N14 completion closes the old small-RHS refusal; it does not establish stationarity. No synthetic point is accepted into the optimizer.

The actual cold fine inverse now uses N14/K10 on all fine evaluations, FD perturbations and fixed-control time comparisons; coarse stays N12/K8. The raw 1e-5 gate, iteration budget, observations/R and Armijo remain unchanged. Run37862480485 at the intermediate N13 policy was cancelled during build before long tests; it is not a passing CI result.

Required CI evaluates Native_Physical_Wave_Inverse first, then all128other registered tests. A successful run still executes all129exactlyonce. A failed inverse fails the job and leaves the remaining gates explicitly unrun. The first LastTest log is preserved separately; endpoint evidence is uploaded before the remaining long tests so an independent fixed-control precision check can proceed without waiting hours. Heavy tests remain serial; light tests use two workers. Timeouts are unchanged. actionlint and diff checks pass. Full CI and final validation remain open.

Production sources are unchanged from be4077b: the recorded 117-case kernel and WRF byte-parity evidence remains applicable. No new model run or performance claim for this runner/CI-only update.
