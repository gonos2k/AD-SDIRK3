# PR283 최소 연구 CI 및 진단 필드 수정

Local timestamp: 2026-10-10T08:29:15.267798+09:00.
User instruction: deployment is out of scope; retain only minimum CI. Source baseline322ba86be12249ce28ebe5ec8f080ae0169623ec; original dirty worktree untouched.

Run37999436670 failed in Native_Physical_Wave_Inverse because Python expected physical_wave_newton_tol/physical_wave_krylov_tol, while the native producer emits newton_tol_config/krylov_tol_config. The native evaluation itself returned success; its actual CSV records the requested N14/K10 values. The consumer now uses one canonical checker shared by evaluate and --check-metadata. Existing failed CSV and short N14/K12 gate pass offline; incorrect values and legacy-only fields are rejected. No model/numerical tolerance was changed and no long optimization was rerun.

Automatic CI now builds the real CMake core once through six selected executables and runs five short CTests: Diagnostic_Observer_Noninterference_Contract, Fixed_Trajectory_Derivative, Fixed_Trajectory_Lifecycle, FP64_State_Handoff_Contract, Full_Tile_Step_Adjoint. One16-step block replay follows, then the SAME CSV is checked by the actual metadata consumer. Python/shell syntax and actionlint are the fast lane. Selected-test inventory is checked against the explicit list; --no-tests=error remains.

Removed from automatic CI: duplicate Make archive, second no-MPI build, installation/packaging checks, negative-build matrix, full129 execution, expensive Native_Physical_Wave_Inverse/long refinement/quadratic cases, and historical heavy-result reuse. Repository tests remain available for explicit manual execution; the existing diagnostic workflow is preserved. The required aggregate checks fast-contracts/core-linux; branch protection already requires only required, and no repository settings were changed.

This is five selected test passes plus short replay/metadata coverage, NOT a new129-test claim. Previous same-source WRF48-stage/byteidentity and two h0.3125 Armijo updates remain qualified evidence; no WRF or RK3 rerun was performed for the workflow/Python-only change. Green/Red independent source review and local syntax/offline metadata checks completed; hosted minimum CI is pending at this timestamp. Eg/final-gradient/convergence/unused forecast remain open. No merge.
