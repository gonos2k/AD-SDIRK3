# PR280 CTest timeout follow-up

Local timestamp: 2026-10-07T16:28:00+09:00.

The first exact-head CI run, [run 37575671475](https://github.com/gonos2k/AD-SDIRK3/actions/runs/37575671475), failed only because `Native_Quadratic_Wave_Forcing` reached its configured 900-second CTest timeout at 900.03 seconds. The core Linux job reported 126 passes, one expected MPS skip, and this one timeout. The failure artifact contains the test receipt and CTest failure record, but no completed quadratic report or CSV, so it does not establish which later numerical gates ran or passed on Linux. No numeric assertion or runner traceback was captured before termination.

The same test passed in 545.31 seconds on the prior PR279 CI head. PR280 adds three new 240-step h=1.25 trajectories and the finite-upwind and selected m3 diagnostics to that runner. The bounded fix changes only the CTest timeout for this test from 900 to 1800 seconds; no numerical tolerances, budgets, source equations, or workflow parallelism changed. Local focused validation and the offline m3 replay remain as separately documented; the fresh exact-head CI run must establish the full Linux gate.

The Graphify refresh receipt was updated to bind the changed CMake file manually because CMake registration is outside its AST extraction. After reconfiguring `/private/tmp/stable-wave-build-20261004`, `ctest --show-only=json-v1` reported `TIMEOUT: 1800.0` for this test; no native test was run. The original failed CI log and diagnostics artifact remain preserved outside the repository; no model run or RK3 comparison was performed for this timeout-only follow-up.
