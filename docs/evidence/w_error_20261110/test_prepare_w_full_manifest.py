#!/usr/bin/env python3
"""Small projection/digest/profile tests; never uses native data."""

import numpy as np
from prepare_w_full_manifest import (
    fnv,
    project_native,
    validate_digest,
    validate_profile,
)

m = n = z = 2
ny = nx = 3
nu = ny * z * (nx + 1)
nv = (ny + 1) * z * nx
nw = ny * (z + 1) * nx
nt = ny * z * nx
nmu = ny * nx
N = nu + nv + 2 * nw + nt + nmu
B = np.zeros(N, dtype=np.float64)
K = np.zeros(N, dtype=np.float64)
u = B[:nu].reshape(ny, z, nx + 1)
u[0, 0, 0] = 3
u[0, 0, 1] = 5
v = B[nu : nu + nv].reshape(ny + 1, z, nx)
v[0] = 8
v[m] = -9
v[m - 1, 0, 0] = 0.0
w = B[nu + nv : nu + nv + nw].reshape(ny, z + 1, nx)
w[0, 0, 0] = 7
K[nu + nv] = 2
q = project_native(B, K, 0.25, m, n, z)
assert q["u"][0, 0, n] == 3 and q["u"][0, 0, 1] == 5
assert np.all(q["v"][0] == 0) and np.all(q["v"][m] == 0)
assert not np.signbit(q["v"][0]).any() and not np.signbit(q["v"][m]).any()
assert np.signbit(q["v_full"][m + 1, 0, 0])  # negative reflected positive zero
assert q["w"][0, 0, 0] == 7.5 and q["w"][0, 0, n] == 7.5
assert np.array_equal(q["w"][m], q["w"][m - 1])
record = {"dtype": 7, "shape": list(q["u"].shape), "fnv64": fnv(q["u"].tobytes())}
validate_digest(record, q["u"], list(q["u"].shape))
try:
    validate_digest({**record, "fnv64": "0"}, q["u"], list(q["u"].shape))
except ValueError:
    pass
else:
    raise AssertionError("wrong digest accepted")
profile = {
    "implicit_only": True,
    "canonical_horizontal": True,
    "packed": True,
    "mu_horizontal": True,
    "mu_fortran_parity": True,
    "six_maps_unit": True,
    "pre_pgf_rw_zero": True,
    "hevi": False,
    "split_explicit": False,
    "implicit_transport": False,
    "nan_sanitize": 0,
    "wrf_w_damping": 0,
    "qv_status": 1,
    "cqw_status": 1,
}
validate_profile(profile, True)
for bad in (
    {k: v for k, v in profile.items() if k != "six_maps_unit"},
    {**profile, "cqw_status": 0},
):
    try:
        validate_profile(bad, True)
    except (ValueError, KeyError):
        pass
    else:
        raise AssertionError("missing or unsupported profile accepted")
print(
    "PASS native separate formation, periodic seam, V positive-zero walls/negative-zero ghost, W bottom/aliases, bad digest and profile rejection"
)
