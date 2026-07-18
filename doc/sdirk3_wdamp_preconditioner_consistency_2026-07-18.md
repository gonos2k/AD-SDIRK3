# W-damping operator/preconditioner consistency at dt=600 (2026-07-18)

**Status: dated evidence record** (PR 9D, branch
`pr9d-wdamp-preconditioner-consistency` off `main` = `12adab6`). Same
configuration as every prior dt=600 record (em_b_wave, 2 steps, single rank,
OMP=2, IMEX mode 3, `WRF_SDIRK3_STAGE_DIAG=1`).

## 1. What changed

The preconditioner treated `implicit_wdamp` as a physical W-damping mirror
(`eff_wdamp = implicit_wdamp || precond_extra_wdamp`) and, when set, added the
scalar `w_damp_alpha` to every interior W diagonal level. PR 9D removes that:
the WRF-parity W-damping term has NO direct W-diagonal Jacobian in the smooth
region — its tangent flows through `ww`/`mu`; the physical `w` enters only
through the hard SIGN, whose derivative is 0 away from `w == 0`
(`WDamp_Tangent_Contract`'s pure-`w` case measures an exactly zero production
tangent). So the physical W direct diagonal is now **always 0**; the scalar
survives only as an explicit `precond_extra_wdamp` regularization.

## 2. Paired legacy-M vs consistent-M at dt=600, Stage-3 (measured)

`SDIRK3_PRECOND_WDAMP_DIAG` at the dt=600 build (`coefficient_generation=1`):

| | physical_w_direct_diag | legacy_scaled | final_wdiag range |
|---|---|---|---|
| consistent-M (default) | 0 | 3.36761e-06 | [1, **523.113**] |
| legacy-M (`WRF_SDIRK3_PRECOND_LEGACY_WDAMP=1`) | 0.3 | 3.36761e-06 | [1, **523.114**] |

The retired legacy scalar `w_damp_alpha = 0.3` enters the W diagonal as
`0.3 / momentum_coupling_k ≈ 0.3 / 89083.9 ≈ 3.4e-6`, utterly negligible next
to the acoustic-Schur-dominated `D_w ≈ 523`. The D_w max differs by ~1e-3.

**Operator-side records identical** (legacy-M vs consistent-M — the operator
`A = I − dt·γ·J` is the same in both, only `M` differs):

```
SDIRK3_STAGE_DIAG stage=2 converged=1 ... final_res=2.345264e-02
SDIRK3_NEWTON_DIAG ts=0 stage=3 iter=0 event=residual r0=5.004250e-01 res_l2=1.760896e+03
SDIRK3_FGMRES_DIAG ts=0 stage=3 iter=0 rhs_norm=5.201751e+02 ... final_res=5.202402e+02 rel_err=1.000125e+00 termination_reason=max_budget
```

**Stage-3 FGMRES trajectory: byte-identical** between the two — same
`rhs_norm=5.201751e+02`, same `final_res=5.202402e+02`, same
`rel_err=1.000125e+00`, same `termination_reason=max_budget`. Not "no
measurable change" — literally the same digits.

## 3. Bounded conclusion

> The stale W-diagonal mismatch was real (a genuine operator/preconditioner
> inconsistency: `M` modeled a 0.3 W diagonal the operator no longer
> contained), but removing it produced no measurable Stage-3 solver change at
> this operating point — the 0.3 scalar scales to 3.4e-6 against a D_w of ~523
> and the FGMRES trajectory is identical. It is therefore **not** the observed
> Stage-3 failure mechanism.

This is the full extent of the claim. It does **not** show the preconditioner
is "fixed" or "correct", and it does **not** show the mismatch "caused" Stage
3. The identical trajectory means only that this coefficient was **not
load-bearing** at this operating point. (This reconciles the 2026-06
precond-inc2 observation: the "over-damped W diagonal that was load-bearing"
was the acoustic-Schur `D_w ≈ 523`, NOT the 0.3 W-damping scalar — the two
were conflated before this measurement separated them.)

The real smooth Jacobian of the W-damping term is the `u/v/mu → rw`
cross-block, which a 1-D W diagonal cannot represent; implementing that
cross-block preconditioner is separate, out-of-scope design work.

## 4. Three-configuration acceptance (dt=600)

| config | namelist / env | rhs_config_enabled | physical_w_direct_diag | extra_regularization | extra_alpha |
|---|---|---|---|---|---|
| A default | `wrf_w_damping=0`, `precond_extra_wdamp=false` | 0 | 0 | 0 | 0 |
| B WRF enabled | `WRF_SDIRK3_WRF_W_DAMPING=1` | 1 | **0** | 0 | 0 |
| C explicit reg | `WRF_SDIRK3_PRECOND_EXTRA_WDAMP=1` | 0 | 0 | 1 | 0.3 |

- **B is the load-bearing check**: enabling the RHS W-damping term does NOT
  bring back a blanket W diagonal (`physical_w_direct_diag=0`).
- **C** carries `source=extra_regularization` in the log; the scalar is the
  explicit, deliberately non-operator regularization, not a physics mirror.
- Split path: 6 stage norms **bit-identical** to the tracked golden.
- The default (A) dt=600 records are **unchanged** from the post-9C baseline
  (stage-3 `final_res=5.202402e+02 rel_err=1.000125` — the removal is
  numerically negligible on the default path, §2).

## 5. Reproduction

```bash
cd test/em_b_wave   # built at this branch head; 2 steps at dt=600
# consistent-M (default):
env OMP_NUM_THREADS=2 WRF_SDIRK3_STAGE_DIAG=1 ./wrf.exe
# legacy-M (paired comparison; env-only, test-only):
env OMP_NUM_THREADS=2 WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_PRECOND_LEGACY_WDAMP=1 ./wrf.exe
grep "SDIRK3_PRECOND_WDAMP_DIAG dt=600" rsl.error.0000
```
