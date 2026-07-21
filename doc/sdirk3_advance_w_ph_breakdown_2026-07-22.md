# `advance_w` geopotential-update breakdown

Date: 2026-07-22

This diagnostic is the measurement step after the `ph_tend` dimensional
contract. It changes no production numerical policy. The default path remains
the existing single call to `advance_w`; the additional tensor work is entered
only when

```text
WRF_SDIRK3_ADVANCE_W_PH_DIAG=1
```

is present before the first acoustic substep.

## Exact measured identity

For the current split-explicit `em_b_wave` path, whose acoustic implementation
is explicitly the unit-map-factor specialization, `advance_w` constructs

```text
mf_old = c1f*mut  + c2f
mf_new = c1f*muts + c2f
```

where `muts` is the mass already updated by `advance_mu_t` in the same acoustic
substep. On prognostic full levels `k=1..nzw-1`, the emitted decomposition is

```text
physical_ph_tend = ph_tend / mf_old
frozen           = dts * ph_tend / mf_old
old_w            = 0.5*dts*g*(1-epssm) * w_old / mf_old
vertical_adv     = -dts * restagger(ww*d_eta(ph_save+phb)) / mf_old
new_w            = 0.5*dts*g*(1+epssm) * w_new / mf_new

actual_delta     = ph_new - ph_old
sum_delta        = frozen + old_w + vertical_adv + new_w
closure          = actual_delta - sum_delta
```

The bottom full level is a prescribed zero boundary in `advance_w` and is
excluded from all reported RMS/max statistics. The top full level is included.
The current diagnostic does not claim a general packed active-domain mask: for
the single-tile `em_b_wave` experiment it reports all horizontal cells on those
prognostic full levels.

`closure` is a first-class contract. It should be near floating-point
reassociation error. A material closure residual means the diagnostic formula
or its indexing is wrong and no physical interpretation should be made.

## Output contract

Each acoustic substep emits one line:

```text
SDIRK3_ADVANCE_W_PH_DIAG sequence=... small_step=... dts=...
```

followed by FP64-accumulated `*_max` and `*_rms` fields for the coupled and
physical tendency, previous `ph`, four update arms, actual/summed delta, and
closure. It also records the old/new mass-denominator extrema. Floating-point
fields use `std::numeric_limits<double>::max_digits10`, so FP64 diagnostic
values round-trip through the text record.

`sequence` is a per-thread acoustic-loop sequence number incremented when
`small_step==1`; it is not asserted to be the RK-stage identifier. Correlate it
with the existing split-explicit stage log in the same line-atomic output.

If shape or arithmetic preparation fails, the solver result is preserved and
an explicit

```text
SDIRK3_ADVANCE_W_PH_DIAG_INVALID
```

record is emitted. The parser fails closed on such records by default and
preserves the complete failure reason, including spaces and `=` characters.
To preserve the one-record/one-line contract, embedded newline, carriage-return,
and tab characters from exception text are escaped as `\n`, `\r`, and `\t`;
other ASCII control characters are replaced with `?`.

## Run and summarize

From the repository root, run the case in its normal WRF directory and parse
its RSL output with the repository-relative parser path:

```bash
export WRF_SDIRK3_ADVANCE_W_PH_DIAG=1
export WRF_SDIRK3_DEBUG_LEVEL=1
(
  cd test/em_b_wave
  ./wrf.exe
)

python3 external/libtorch_wrf/sdirk3/tests/parse_advance_w_ph_diag.py \
  test/em_b_wave/rsl.error.* \
  --tsv-out test/em_b_wave/advance_w_ph.tsv \
  --json-out test/em_b_wave/advance_w_ph.json
```

The parser derives:

- the largest RMS arm among `frozen`, `old_w`, `vertical_adv`, and `new_w`;
- closure RMS/max relative to the actual update;
- the sum of component RMS values relative to actual RMS, used as a cancellation
  indicator rather than an additive energy identity;
- old/new mass-denominator spans.

Its no-WRF contract can be run directly:

```bash
python3 external/libtorch_wrf/sdirk3/tests/parse_advance_w_ph_diag.py --self-test
```

## Interpretation order

1. Require small relative closure first.
2. Compare `physical_rms`, not raw coupled `ph_tend`, across stage/substep.
3. Identify the dominant physical arm and whether large component RMS values
   cancel in the actual update.
4. Compare the same arm at the same operand against dyn_em parity output.
5. Modify numerical code only after a paired mismatch is localized.

Possible outcomes:

| Observation | Next target |
|---|---|
| `frozen` mismatches dyn_em | `rhs_ph` construction or coupled-tendency handoff |
| `vertical_adv` mismatches | `ww`, `rdnw`, `fnm/fnp`, or vertical indexing |
| `old_w`/`new_w` mismatches | off-centering or old/new mass denominator |
| every arm matches but stage result diverges | RK handoff/substep accumulation |
| split arms match yet implicit FGMRES plateaus | JVP/operator/preconditioner diagnosis |

This PR is intentionally stacked on the mass-coupling contract PR. It does not
blindly rescale `ph_tend`, enable block-max convergence, or alter the
split-explicit policy.
