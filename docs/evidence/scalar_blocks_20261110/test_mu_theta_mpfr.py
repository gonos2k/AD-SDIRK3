#!/usr/bin/env python3
"""Manufactured scalar-block checks; no native data."""

import json
from fractions import Fraction
from pathlib import Path
import subprocess
import tempfile

import numpy as np

E = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    m = n = z = 2
    ny = nx = 3
    nu = ny * z * (nx + 1)
    nv = (ny + 1) * z * nx
    nw = ny * (z + 1) * nx
    nt = ny * z * nx
    nmu = ny * nx
    N = nu + nv + 2 * nw + nt + nmu
    ot = nu + nv + 2 * nw
    om = ot + nt
    B = np.zeros(N)
    K0 = np.zeros(N)
    K1 = np.zeros(N)
    R0 = np.zeros(N)
    R1 = np.zeros(N)
    B[ot:om] = 4
    K1[ot:om] = 0.5
    R1[ot:om] = 0.5
    mub = np.ones(nmu)
    c1 = np.ones(z)
    c2 = np.zeros(z)
    rdnw = np.ones(z)
    arrays = [B, K0, K1, R0, R1, mub, c1, c2, rdnw]

    def save():
        for i, x in enumerate(arrays):
            np.save(p / f"{i}.npy", np.asarray(x, dtype="<f8"))

    save()
    cmd = [str(E / "mu_theta_mpfr"), "2", "2", "2", "1", "1", "1", "1", "1"] + [
        str(p / f"{i}.npy") for i in range(9)
    ]

    def run():
        return subprocess.run(cmd, text=True, capture_output=True)

    result = run()
    assert result.returncode == 0, result.stderr
    data = json.loads(result.stdout)
    assert all(
        s["mu_error"]["linf_upper"] == 0 and s["theta_error"]["linf_upper"] == 0
        for s in data["states"]
    )
    # U seam, nonuniform mass, two eta=-1 layers => Mdot [-5,+5,-5].
    B[0:nu].reshape(ny, z, nx + 1)[:m, :, 1] = 1
    B[om:].reshape(ny, nx)[:, 0] = 1
    B[om:].reshape(ny, nx)[:, 1] = 2
    mdot = np.array([-5.0, 5.0, -5.0])
    mass = np.array([2.0, 3.0, 2.0])
    eps = float(np.float32(1e-10))
    R0[om:] = -np.broadcast_to(mdot, (ny, nx)).reshape(-1)
    R1[om:] = R0[om:]
    R0[ot:om] = (4 * np.broadcast_to(mdot / (mass + eps), (ny, z, nx))).reshape(-1)
    R1[ot:om] = (0.5 + 4.5 * np.broadcast_to(mdot / (mass + eps), (ny, z, nx))).reshape(
        -1
    )
    save()
    result = run()
    assert result.returncode == 0, result.stderr
    data = json.loads(result.stdout)
    assert all(s["mu_error"]["linf_upper"] < 1e-60 for s in data["states"])
    epshi = float(np.float32(1e-4))
    difference = abs(
        Fraction(-20, 1) / (Fraction(2, 1) + Fraction.from_float(epshi))
        - Fraction.from_float(R0[ot])
    )
    assert (
        Fraction.from_float(data["states"][0]["theta_error"]["linf_upper"])
        >= difference
    )
    # Verify nonzero K and a do not silently freeze theta, and bad observed mass is detected.
    bad = R1.copy()
    bad[ot] += 1
    arrays[4] = bad
    save()
    result = run()
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout)["states"][1]["theta_error"]["linf_upper"] > 0.9
    arrays[4] = R1
    mub.fill(-100)
    save()
    assert run().returncode != 0
    print(
        "PASS zero flow, nonuniform mass/product rule, nonzero K, epsilon hull, mismatch and domain rejection"
    )
