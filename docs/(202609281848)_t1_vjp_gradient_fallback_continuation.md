# T1 VJP-gradient fallback continuation experiment

Local timestamp: 2026-09-28 18:48:01 JST (+0900)

## Scope and decision boundary

This local, environment-gated experiment extends the iter9 VJP-gradient candidate through the same strict Stage-2 solve. It is not a production PR. The experiment changes no tolerances or solver equations: after all three ordinary trust trials reject, it forms the smooth-merit gradient

```text
q = mask * S_inv^2 * R
g = q - dt*gamma*J_F(U_eval)^T*q
```

then applies the existing halo projection and S-inverse trust clip to `-g`. It evaluates the canonical FP32 candidate and accepts only when the candidate-specific smooth model is valid, the actual merit decreases, the unchanged rho threshold passes, and any configured block-aware RU gate passes. The actual-ΔU JVP is recorded as a secondary diagnostic, not used for acceptance. The fallback runs only with `WRF_SDIRK3_T1_GRADIENT_FALLBACK=1`, split mode 3, `use_autograd=true`, and `omega_update_ref_per_newton=false`; normal runs keep it off.

## Paired strict WRF outcome

The env-off and env-on runs used the same one-rank executable, namelist, diagnostics file, initial input and `OMP_NUM_THREADS=1`. The first 515 ordered RHS operand digests match; divergence follows the first accepted fallback. Both runs stop before a successful forecast step, and their partial `wrfout` files are byte-identical.

The fallback was attempted 15 times at Stage-2 Newton iterations 9 through 23. It accepted 12 candidates at iterations 9–20, then rejected the repeated candidate at iterations 21–23. The accepted steps lowered scaled RMS from `1.273917e-6` at iter9 to `5.681900e-7` at iter21. The remaining three attempts left the residual unchanged, and the solver ended at `ZeroStepStall` at iter23 with tolerance `1e-7`. It did not converge.

Residual sequence at the next Newton base after the fallback attempts:

```text
iter10 1.199315e-6   iter11 1.116877e-6   iter12 1.042822e-6
iter13 9.807410e-7   iter14 9.002321e-7   iter15 8.441278e-7
iter16 7.806719e-7   iter17 7.444629e-7   iter18 6.819162e-7
iter19 6.411200e-7   iter20 5.897077e-7   iter21 5.681900e-7
iter22 5.681900e-7   iter23 5.681900e-7
```

Each fallback spends five `compute_rhs` calls across the VJP and FWAD checks/candidate evaluation; one candidate RHS is charged against the existing per-Newton trust budget. The fallback ran 15 times: 75 RHS calls in those probes and 15 candidate-RHS budget charges. The whole-run digest count was 516 env-off and 14,134 env-on. That difference also includes the changed solve trajectory and added Newton/GMRES work, so it is not a direct estimate of the fallback’s marginal cost.

The last three fallbacks expose a model-coordinate gap. At iter21–23 the same frozen base/candidate repeats: smooth predicted reduction is `1.78e-9`, actual reduction is positive `1.951e-10`, so smooth rho is `0.1096` and the unchanged `0.25` gate rejects. The secondary actual-ΔU model gives rho about `0.9999`. These repeated measurements are one base, not independent directions. They do not prove FP32 reassociation or a precision floor.

The next discriminating T1 probe is to freeze the iter21 base and compare `delta_U_actual = U_trial-U_eval` with nominal `dt*gamma*dK_gradient`, blockwise for U/W/PH, including changed-cell counts and local ULP magnitudes. Evaluate `F(U_nominal)` and `F(U_actual)` in a sidecar, then compare W/full RHS differences and, if useful, apply a JVP to the displacement difference. Use same-binary env-off/on digest and tensor controls; do not feed the result into acceptance.

No forecast fields or runtime comparison against same-setup RK3 was performed. Stage 2 failed before a forecast step, so no time-accuracy, forecast-quality, or equal-accuracy performance claim follows.

## Provenance

Source base: PR #255 head `c6ce9e80ba72c31676ab7f63ecb99abb32456911`. The disposable experiment worktree is `/private/tmp/sdirk3-t1-gradient-fallback-experiment-20260928`; only its local Newton source differs. Graphify on the exact base and after the diagnostic patch reports 3,564 nodes / 6,690 edges. `git diff --check` passes.

The env-off/on pair used the same linked executable and PR246 clean WRF object set; only `wrf_sdirk3_newton_solver.o` differs from the baseline archive, with all 23 other payload objects byte-identical. C++ used Homebrew Clang, C++17 `-O2`, LibTorch, and `_GLIBCXX_USE_CXX11_ABI=0`.

- Instrumented `wrf_sdirk3_newton_solver.cpp`: `260674679cd48fbe74d450c5f090ca1d64e74f08978c55ee696c60bd05fdc627`
- Instrumented Newton object: `ce1b9f3f005bcc5a2ec9107374e70687ba09f10ff2b2294737f4b25c2c21612f`
- Linked archive: `e42882776edcd5dc19539b7507ffd06dec5f3c50fde4b76ebbdce97a548954a9`
- Archive file: `15758e17994c7c75d787d9a81b16d20b35b752ef75e345101c4a523dede37624`
- Shared executable: `fbc350b4c2b697aed6a92a5c2db743a88dbf4bc0c7163479a2eeb2be25238bdb`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `namelist.input`: `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`
- Env-off / env-on `rsl.error.0000`: `4e5233ed66fbba152b3e6271786cfd07424678a2a3dea09ade318ee362415332` / `dfb9c31bf9d86d6db65a554f40f6758fa3acc357e49439943db18fafb347bb12`
- Env-off / env-on partial `wrfout`: both `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`

Run artifacts, archives, link script and logs are under `.validation/gradient_fallback_continuation/`. This result remains local; no code PR is proposed.

## Repository checklist and PR state

GitHub status was checked on 2026-09-28. PRs #253 (W actual RHS contract), #254 (half-clipped retries), #255 (dyadic retry continuation), and #256 (recovery evidence) are OPEN on feature branches, stacked through feature bases; none is merged to `main`. PR #251 is MERGED into the `agent/option2_v_nonuniform_test_p2` feature branch, not `main`. The earlier #233 integration is on `main`; it does not include these newer PRs.

- **S1b:** Partial tile and accepted-stage budget evidence exists, but general physical face/source and endpoint budgets remain open.
- **G1:** Multi-tile/MPI forward and transpose halo/owner equivalence remains open.
- **K1:** Diagnosed coefficient differentiation policy and variable-coefficient stage JVP/VJP remain open.
- **T1:** Strict Stage-2 convergence, time accuracy, stability, forecast quality, and equal-accuracy RK3 performance remain open; this experiment still stalls.
- **A1:** Whole active-step/observation-path adjoint, physical-boundary/MPI transpose, and current-primal WRFPLUS validation remain open.

Green/Red review of this experiment and checkpoint is requested before any production proposal. The fallback should not be promoted unless a subsequent bounded probe supports convergence and a full numerical regression is completed.
