# T1 VJP-gradient dyadic retry prototype

Local timestamp: 2026-09-29 03:56:48 JST (+0900)

This is a local, solver-influencing prototype of retrying one source-defined VJP-gradient direction at three predeclared scales: `1`, `0.5`, and `0.25` of the already clipped direction. An accepted candidate updates `dK_scaled`, the accepted residual, and trust radius. Each candidate used the canonical stage map `Kα=fl32(K+αdK)`, `Uα=fl32(U_stage+hKα)`, `F_implicit(Uα)`, and the realized-ΔU prediction. The actual-reduction sign check and existing `assess_trust_model` rho threshold `0.25` were unchanged. There was no scale search beyond the fixed ladder and no production change or PR.

The strict one-rank 15 s run accepted the full candidate through iter21, used half at iter22, and quarter at iter23. All three scales failed at each of iter24–26. The solve ended in `ZeroStepStall` at iter26 with residual `4.823e-7`, above the `1e-7` tolerance. No forecast step completed.

| Stage-2 Newton base | α=1 actual reduction | α=0.5 actual reduction | α=0.25 actual reduction | Accepted scale |
| ---: | ---: | ---: | ---: | --- |
| 22 | `-6.955e-11` | `+1.364e-9` | not tried | 0.5 |
| 23 | `-2.057e-9` | `-6.76e-10` | `+6.54e-10` | 0.25 |
| 24–26 | `-3.364e-9` | `-1.313e-9` | `-4.183e-10` | none |

Across all bases, the prototype evaluated 27 candidates: 15 accepted and 12 rejected. There were 18 first-scale candidates and 9 extra ladder candidates beyond the old one-candidate fallback. Five of the extra candidates used remaining shared RHS budget; four were over that budget. Each extra candidate added three `compute_rhs` calls (smooth JVP, candidate F, and realized-displacement JVP); total fallback RHS/JVP calls were 117. This local bounded budget overage is measured experimental cost, not production budget policy.

The same executable ran fallback-off and actual-model retry arms with identical input, namelist, diagnostics, and `OMP_NUM_THREADS=1`. They share the first 515 ordered RHS operand digests. Counts were 516 off and 19,288 with retries. The retry trajectory used 16,835 Krylov iterations versus 13,835 in the prior actual-model one-shot run; those whole-run differences include the changed nonlinear and Krylov trajectory, not only retry overhead. Both partial `wrfout` files are byte-identical.

The result shows that the predeclared smaller steps improve the iter22–23 bases, then fail to meet the residual tolerance at later bases. It does not demonstrate convergence or a general benefit. The run stopped before forecast output; no same-setup RK3 field or runtime comparison was performed. No further scale tuning or production promotion is proposed.

## Provenance

The disposable prototype is `/private/tmp/sdirk3-t1-vjp-dyadic-retry-prototype-20260929`, based on PR #259’s local experimental source at `c6ce9e80ba72c31676ab7f63ecb99abb32456911`. Only its Newton solver source was changed. Graphify matched the pre-prototype fallback source and was refreshed after the retry prototype (3,564 nodes / 29,273 extracted edges); generated graph files were kept under `.validation/dyadic_retry/graphify_receipt/`, outside the source tree. `git diff --check` passed. The C++ object used Homebrew Clang, C++17, `-O2`, and strict LibTorch ABI header mode; the WRF run reused the same clean object set and link recipe as the prior local actual-model experiment.

- Prototype Newton source: `927f83e5c328e871e43dcec8e7ba8b349f288e07dcab509b300d0006d69b6595`
- Newton object: `7c0a9715b7493c97e18a9ee9e26c5ccb5a92ae9e0ea387348b6626f6aa10b097`
- LibTorch archive: `4d3b145831c3b281a658654dbd8156462fc27345b83f55fa964c5be19742e2eb`
- Shared executable: `06c63cacdc9aff42a790c816812d17053eb599e2ea8e78e73c3493677793a292`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `namelist.input`: `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`
- Env-off / retry `rsl.error.0000`: `c9cbd97c5dd66fc7cfa460830242d4a351b2a774ec9d2a26e4ca4adf72b23c8c` / `b9f7cfcf66520237d40e3b79ee928edc75723bec7278e95aa4bec6a0e137e927`
- Retry terminal iter26 snapshot: `09d35c4774826066358db1ad704fd93bfd49081ec86aa4598a3eddb1f3363c1b`
- Env-off / retry partial `wrfout`: both `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`

Run files are under `.validation/dyadic_retry/`. The prototype source and artifacts remain local; the report is based on integrated checklist HEAD `0ec2f963d5296aa7a44ea4c6b395d4cbaae79324` and is documentation-only.
