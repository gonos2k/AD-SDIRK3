from fractions import Fraction

from w_pressure_sensitivity import compute, validate_profile

coeff = {
    "gravity": 10.0,
    "c1f": [1.0, 1.0, 1.0],
    "c2f": [0.0, 0.0, 0.0],
    "rdn": [0.0, 2.0],
    "rdnw": [1.0, 3.0],
}
endpoint = {
    "mu_min": 0.0,
    "mu_max": 0.0,
    "mub_min": 10.0,
    "mub_max": 10.0,
    "pressure_error": [1.0, 2.0],
}
r = compute(coeff, endpoint, n_w=1, n_total=5)
assert [Fraction(x) for x in r["per_level_error_exact"]] == [0, 6, 12]
assert Fraction(r["weighted_rms_upper_exact"]) == 6 * (1 + Fraction(1, 2**23))
for replacement in ({"mub_min": 0.0, "mub_max": 0.0}, {"pressure_error": [-1.0, 2.0]}):
    try:
        compute(coeff, {**endpoint, **replacement}, 1, 5)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid input accepted")
try:
    compute(coeff, endpoint, 1, 4)
except ValueError:
    pass
else:
    raise AssertionError("invalid 1/2 RMS factor accepted")
print(
    "PASS interior/top exact rational, weight allowance, nonpositive denominator/error, RMS size gate"
)

validate_profile({"mask_scope": "identity", "N": 1080491})
for invalid in (
    {"mask_scope": "masked", "N": 1080491},
    {"mask_scope": "identity", "N": 1080490},
):
    try:
        validate_profile(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid receipt profile accepted")
for n_w, n_total in ((0, 5), (1, 0), (-1, 5)):
    try:
        compute(coeff, endpoint, n_w, n_total)
    except ValueError:
        pass
    else:
        raise AssertionError("nonpositive count accepted")
print("PASS receipt H/N and positive-count gates")
