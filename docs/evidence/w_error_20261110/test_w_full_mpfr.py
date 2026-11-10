#!/usr/bin/env python3
"""Synthetic two-by-two C-grid tests, no native data."""

import json
import math
from pathlib import Path
import subprocess
import tempfile
import numpy as np

E = Path(__file__).resolve().parent
power_test = subprocess.run(
    [str(E / "w_full_mpfr"), "--selftest-pow"], text=True, capture_output=True
)
assert power_test.returncode == 0, power_test.stderr
print(power_test.stdout.strip())
NAMES = [
    "B",
    "K0",
    "K1",
    "R0",
    "R1",
    "theta",
    "pbase",
    "mub",
    "c1h",
    "c2h",
    "c1f",
    "c2f",
    "mass_rdnw",
    "pressure_rdn",
    "top_rdnw",
    "sinv_w",
]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    m = n = z = 2
    ny = nx = 3
    nu = ny * z * (nx + 1)
    nv = (ny + 1) * z * nx
    nw = ny * (z + 1) * nx
    nt = ny * z * nx
    nmu = ny * nx
    total = nu + nv + 2 * nw + nt + nmu
    ow = nu + nv
    B = np.zeros(total)
    K0 = np.zeros(total)
    K1 = np.zeros(total)
    R0 = np.zeros(total)
    R1 = np.zeros(total)
    u = B[:nu].reshape(ny, z, nx + 1)
    u[:m, :, 1] = 1
    v = B[nu : nu + nv].reshape(ny + 1, z, nx)
    v[0, :, :] = 1000
    v[m, :, :] = -1000
    w = B[ow : ow + nw].reshape(ny, z + 1, nx)
    w.fill(1)
    K1[ow : ow + nw] = 0.5
    # Unit masses, unit metrics and two levels: east/west divergence is +1/-1;
    # eta=-1 on each level, so Mdot=-2/+2. Q duplicates x=0 at endpoint.
    mdot = np.array([-2.0, 2.0, -2.0])
    R0[ow : ow + nw] = np.broadcast_to(mdot, (ny, z + 1, nx)).reshape(-1)
    R1[ow : ow + nw] = (0.5 + 1.5 * np.broadcast_to(mdot, (ny, z + 1, nx))).reshape(-1)
    assert R0[ow] == -2  # bottom product rule is active
    values = [
        B,
        K0,
        K1,
        R0,
        R1,
        np.ones(nt),
        np.full(nt, 100000.0),
        np.ones(nmu),
        np.zeros(z),
        np.ones(z),
        np.ones(z + 1),
        np.zeros(z + 1),
        np.ones(z),
        np.array([0.0, 1.0]),
        np.ones(1),
        np.ones(nw),
    ]
    for name, value in zip(NAMES, values):
        np.save(p / (name + ".npy"), np.asarray(value, dtype="<f8"))
    cmd = [
        str(E / "w_full_mpfr"),
        str(m),
        str(n),
        str(z),
        "1",
        "1",
        "1",
        "1",
        "100000",
        "1",
        "1",
        "100000",
        "1",
    ] + [str(p / (x + ".npy")) for x in NAMES]

    def run():
        return subprocess.run(cmd, text=True, capture_output=True)

    result = run()
    assert result.returncode == 0, result.stderr
    data = json.loads(result.stdout)
    assert all(s["physical_error"]["linf_upper"] < 1e-60 for s in data["states"]), data
    # Nonzero vertical pressure difference checks interior sign and top factor two.
    pressure_B = B.copy()
    t_offset = ow + 2 * nw
    pressure_B[t_offset : t_offset + nt].reshape(ny, z, nx)[:, 1, :] = 1.0
    pressure_R0 = R0.copy().reshape(-1)
    pressure_R1 = R1.copy().reshape(-1)
    for rr in (pressure_R0, pressure_R1):
        view = rr[ow : ow + nw].reshape(ny, z + 1, nx)
        view[:, 1, :] += 100000.0
        view[:, 2, :] -= 200000.0
    np.save(p / "B.npy", pressure_B)
    np.save(p / "R0.npy", pressure_R0)
    np.save(p / "R1.npy", pressure_R1)
    pressure_result = run()
    assert pressure_result.returncode == 0, pressure_result.stderr
    assert all(
        s["physical_error"]["linf_upper"] < 1e-55
        for s in json.loads(pressure_result.stdout)["states"]
    )
    np.save(p / "B.npy", B)
    np.save(p / "R0.npy", R0)
    np.save(p / "R1.npy", R1)
    # A wrong observed residual, including at bottom, must be detected.
    wrong = R1.copy()
    wrong[ow] += 1
    np.save(p / "R1.npy", wrong)
    result = run()
    assert result.returncode == 0, result.stderr
    corrupted = json.loads(result.stdout)
    assert corrupted["states"][1]["physical_error"]["linf_upper"] >= 1
    assert corrupted["states"][1]["weighted_math_rms_upper"] >= math.sqrt(nw / total)
    # Perturbing K changes exact Q(B+aK), and stale observed residual no longer matches.
    changed = K1.copy()
    changed[ow] += 0.25
    np.save(p / "K1.npy", changed)
    np.save(p / "R1.npy", R1)
    result = run()
    assert result.returncode == 0, result.stderr
    assert (
        json.loads(result.stdout)["states"][1]["physical_error"]["linf_upper"] >= 0.25
    )
    # Unsupported mass floor and invalid pressure-domain inputs fail closed.
    np.save(p / "K1.npy", K1)
    np.save(p / "mub.npy", np.full(nmu, -1.0))
    assert run().returncode != 0
    np.save(p / "mub.npy", np.ones(nmu))
    np.save(p / "pbase.npy", np.full(nt, -1.0))
    assert run().returncode != 0
    print(
        "PASS continuity/seam/V-wall, pressure interior/top, bottom product rule, nonzero K formation, residual mismatch, domain rejection"
    )

# Reject unsupported capture before reading any native inputs.
from types import SimpleNamespace
from run_w_full_mpfr import validate, validate_equal_rdnw

with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "manifest.json"
    manifest = {
        "scope": "native_stage3_w_reference_inputs_v1",
        "grid": {"m": 80, "n": 40, "nz": 64, "packed_N": 1080491, "W_count": 215865},
        "profile": {"mode3_implicit_only": False},
    }
    path.write_text(json.dumps(manifest))
    try:
        validate(SimpleNamespace(manifest=path, input_dir=Path(tmp)))
    except ValueError:
        pass
    else:
        raise AssertionError("unsupported profile accepted")
print("PASS unsupported profile capture rejected")

validate_equal_rdnw(np.array([1.0, 2.0]), np.array([1.0, 2.0]))
try:
    validate_equal_rdnw(np.array([1.0, 2.0]), np.array([1.0, 3.0]))
except ValueError:
    pass
else:
    raise AssertionError("mismatched EOS/mass RDNW accepted")
print("PASS EOS/mass RDNW exact equality gate")
