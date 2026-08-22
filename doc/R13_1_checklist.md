# R13.1 — certified-only reference output and true probe isolation

Source: the external review of PR #166, against `main` at `22fc0d7`. Every item was
re-checked in the code before being written down.

**Every item in the review is CONFIRMED.** There are no reversals this round, and that is
the finding: seven of the eight defects were introduced or left by R13 itself. R13 built the
right rules and then did not wire three of them to the thing they were supposed to gate.

Legend: `[ ]` open · `[x]` closed · `[~]` partially closed · `[B]` blocked upstream.

---

## The shape of the failure, stated once

R13's thesis was "no probe may report a verdict its preconditions do not support". It then:

- computed `reference_certified` and **left the accuracy output keyed on something else**;
- named a field `arms_isolated` for a check that is **not** isolation;
- gated the tangent-relation sentence on `e_drop > 0` **instead of on the verdict it had
  just computed**.

Each is the same mistake in a different place: **the rule exists, the consumer does not read
it.** That is exactly the class R13 set out to close — R12 C7's FD-fallback counter was
faulted for precisely this ("the counter measured the degradation and nothing consumed it"),
and R13 then reproduced it three times. The lesson is not "check harder"; it is that a rule
and its consumer must be in the same expression, or a test must reject the pair.

---

## P0 — the verdicts that exist and are not consumed

- [x] **A1 — `reference_certified` does not gate the accuracy output.** (review P0-1)
      CONFIRMED. `cert` is scoped inside its own `{ }` block; the branch below keys on
      `reference.converged` and prints `rel_err`, `K_ref`, `ref_agree` and per-block errors
      regardless. Worse, `K_ref` is **arm 1** (120x20, the loosest) — not `reference3`, the
      arm the third-arm work was added to produce. So a record can read
      `reference_certified=0 certification=state_gap_not_shrinking rel_err=0.73 rw=1.42`,
      which is the R12 R4 reading that R13 exists to prevent.
      FIX: hoist `cert`; emit `accuracy_valid`; on `certified=0` suppress `rel_err`/`K_ref`/
      per-block entirely, or rename them `uncertified_gap_to_armN` — never "error",
      "accuracy" or "reference". On `certified=1` use `reference3` as the authority.

- [x] **A2 — the certification predicate is weaker than the contract it claims.**
      (review P0-2) CONFIRMED, all five sub-points:
      (a) no `isfinite` — three `+Inf` residuals satisfy `>0` and `<=`, so an all-infinite
      arm set certifies; (b) `residual > 0` **rejects an exactly converged arm** (`0.0` reads
      as `residual_unavailable`) — the predicate is hostile to the one case it most wants;
      (c) monotone-only, so `1.000 -> 0.999 -> 0.998` passes; (d) same for the state gap
      (`0.500 -> 0.499`); (e) **no `F_E` criterion at all**, though R13's own completion
      table lists it, and no isolation field, so a shared-state arm set can certify on
      numbers alone.
      FIX: finite checks with `0` admissible; absolute-or-contraction on residual and state;
      add `explicit_gap_21/32`; add `isolated[]` / `fresh_solver_per_arm` to the predicate.

- [x] **A3 — `arms_isolated` names more than it measures.** (review P0-3) CONFIRMED, and in
      three separate ways:
      (a) **`restore_carried_state` does not clone.** `capture` clones; restore assigns the
      vector, sharing tensor handles, so an in-place write by the next solve reaches back
      into the snapshot. R13's own contract tested that *capture* clones and never tested the
      other half of the round trip.
      (b) **The digest cannot see direction.** It mixes `norm` and `numel` only, so `v` and
      `-v` — and `v` and `Pv` for any norm-preserving `P` — are the same state.
      (c) **The name overclaims.** What is checked is "the selected carried state came back";
      preconditioner internals, `jacobian_cache_`, `k2_prev_`/`k3_prev_`, bootstrap flags and
      the stage-3 no-improve streak are all outside the snapshot.
      FIX: clone on restore; digest on exact content; rename to `selected_state_restored`
      with `fresh_solver_per_arm=0` beside it, and stop emitting `arms_isolated=1`.

- [x] **A4 — an invalid tangent can print "IS the primal derivative".** (review P0-4)
      CONFIRMED, and the failure mode is the one R13 documented itself. The sentence is
      selected by `e_drop > 0.0`, not by `tverdict.valid`. Under an FD fallback the FD
      quotient returns the **primal** tangent (R13's own finding), so `e_drop ~ 0` and the
      record reads `valid=0 reason=fd_fallback e_drop=0 (the operational tangent IS the
      primal derivative)`. An undefined tensor gives `e_drop = -1`, which is also not `> 0`,
      and lands on the same sentence.
      FIX: `e_drop_valid` + `tangent_relation=unavailable|matches_primal|differs_from_primal`,
      gated on the verdict and on `isfinite`, with a tolerance rather than exact zero.

- [x] **A5 — the global-norm comparator passes on NaN/Inf.** (review P0-5) CONFIRMED by
      execution, not by reading: `max(0.0, nan)` is `0.0` in Python — comparison with NaN is
      false, so `max` returns its first argument. Two `Inf` norms give `rel = nan`, `worst`
      stays `0.0`, and the run prints `np-equivalent`. A blow-up in both arms reads as
      agreement.
      FIX: reject non-finite and negative `sumsq` at parse; re-check after summation; use
      `math.fsum` so a different rank count does not change the answer through accumulation
      order.

---

## P1 — what the records identify, and where they are taken

- [x] **B1 — the global norm is taken at the wrong point in the step.** (review §8.3)
      CONFIRMED: the emitter sits at `unifiedStep` entry (impl.cpp:3844, function opens at
      :3816), i.e. **before any solve**. So it compares solver *input*. That is a real check —
      it is the decomposition/ownership check, and it is the one currently passing — but it
      is not evidence that SDIRK3 produces the same answer under MPI, which is what the
      script's own output sentence claims.
      FIX: keep the entry record, tag it `phase=input state_published=0`, and add a
      `phase=output` record after a successful publish carrying `outcome`, `state_published`,
      `global_timestep`, `rk_step`. The comparator requires `phase=output` for an equivalence
      verdict and accepts `phase=input` only for a partition verdict. Split the judgement:
      **owned-box geometry GO / input-partition equivalence conditional GO / SDIRK3 output
      equivalence NO-GO / trajectory equivalence NO-GO.**

- [x] **B2 — the comparator compares one step by default and does not check the step sets.**
      (review §8.1) CONFIRMED: `step = common[0]`. Step 1 can agree while step 2 diverges and
      the run prints `np-equivalent`.
      FIX: default to **every** step, and fail when the two runs' step sets differ.

- [x] **B3 — `step_seq` is not a physical timestep.** (review §8.2) CONFIRMED: a
      `static thread_local` counter incremented per `unifiedStep` call, which the step-map
      probe's recursive arms also consume.
      FIX: carry `global_timestep` and `rk_step` from the caller; suppress the record inside
      probe recursion.

- [x] **B4 — the manifest key is process-local.** (review P1-1) CONFIRMED: `solver_id` is a
      process-wide ordinal, so two separately-launched arms can assign the same id to
      different tiles. Cross-run comparison needs physical identity.
      FIX: key on `(domain_id, global_timestep, rk_step, retry_generation, rank, tile box,
      stage, newton_iter)`; keep `solver_id` as debug only.

- [x] **B5 — the FD epsilon is float32-only.** (review P1-2) CONFIRMED: the helper hardcodes
      `numeric_limits<float>::epsilon()` regardless of the tensor's dtype. On float64 the
      perturbation is far too large (truncation dominates); on FP16/BF16 it can be below the
      representable step.
      FIX: dtype-dispatched machine epsilon, finite checks on the norms and on any override.

---

## Blocked, unchanged from R13

- [B] **dt=600 (and 300/120/60/20) forward completion** — no step reaches `COMPLETE`.
- [B] **successful one-step JVP/VJP**, exact 4D-Var, actual MPI SDIRK3 output equivalence.

The review's §13 names the right next move and R13.1 should prepare it: **stop sweeping dt
and classify the failure.** "Step did not complete" is currently one bucket holding at least
six distinguishable outcomes — `G` undefined at stage entry, `G` finite but Newton diverges,
Newton fine but Krylov stagnates, Krylov fine but the line search rejects, solve fine but
admissibility rejects, state fine but the publish gate rejects. Which one fires first decides
whether the next fix is in the preconditioner, the split, the EOS, the boundary or the gate.
Tracked as **C1** below.

- [ ] **C1 — first-failure classification for the forward.** Emit, per stage, the first gate
      that refuses and the state that reached it, so the failure has a *name* instead of a
      timestep.


---

## Closeout

ctest 57/58 (`Core_Archive_MakeParity` needs the Make archive this worktree does not build;
it passes in CI). Inventory, README count, install manifest, from_blob ratchet and the live
stage-operand self-test (56/56) all verified locally.

No new ctest targets: every fix landed inside an existing contract, which is the right shape
for a round that fixes wiring rather than adding capability. Case counts rose 55 -> 79.

### The one finding neither review contained

Fixing the digest's blindness to direction (A3) did not work. Adding a sum and a first moment
left `v` and `-v` hashing **identically**, with the debug print reading `sum_a=10 sum_n=-10`
and the two digests bit-equal.

The cause was the hash, not the projections. In FNV-1a, multiplication mod 2^64 never carries
out of bit 63, so an input differing only in that bit maps to an output differing only in that
bit: `h' = h ^ 2^63`. An IEEE-754 sign flip is exactly such a change, and negation flips the
sign of **both** the sum and the first moment:

    h' = ((h ^ 2^63) ^ (b ^ 2^63)) * P = (h ^ b) * P = h

**Two sign flips cancel exactly.** Adding a third projection would not have helped — any even
number cancels. The fix is one avalanche step (`h ^= h >> 29`) that lets bit 63 propagate.

Worth keeping because the failure mode is invisible to reasoning: the projections were correct
and the test still failed. Only the measurement — two sums of +10 and -10 producing one digest
— pointed at the mixing function.

### What R13.1 does not change

- **dt=600 forward completion** — still no step reaches `COMPLETE`, at any dt tried.
- **exact 4D-Var, successful one-step JVP/VJP, actual MPI SDIRK3 output equivalence** — all
  blocked on the above. The `phase=output` record now exists and emits nothing, which is the
  honest state: the comparator reports that there is nothing to compare rather than
  reporting the entry-phase data under the output claim.
- **Full arm isolation** — still snapshot/restore. `fresh_solver_per_arm=0` is on every record
  and is now a clause in the certification predicate, so a run cannot certify a reference
  while claiming isolation it does not have.

### Next, per the review's §13

**C1 — first-failure classification.** Stop sweeping dt. "Step did not complete" is one
bucket holding at least six distinguishable outcomes, and which fires *first* decides whether
the next fix belongs in the preconditioner, the split, the EOS, the boundary or the gate.
Not started.
