#!/usr/bin/env python3
"""Validate EOS provenance and run all-cell directed MPFR alb analysis."""

import argparse
import hashlib
import json
import math
import pathlib
import subprocess
import numpy as np

E = pathlib.Path(__file__).resolve().parent
NAMES = [
    "base_ph",
    "base_t",
    "base_mu",
    "base_alt",
    "base_p_pert",
    "accepted_ph",
    "accepted_t",
    "accepted_mu",
    "accepted_alt",
    "accepted_p_pert",
    "mu_base",
    "alb",
    "p_base",
    "c1h",
    "c2h",
    "rdnw",
]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def fnv(data):
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return str(h)


def encode(b, nz):
    for k in ("linf_upper", "l2_upper"):
        v = b[k]
        assert math.isfinite(v) and v >= 0
        b[k + "_binary64_hex"] = float(v).hex()
        assert float.fromhex(b[k + "_binary64_hex"]) == v
    vals = b["per_level_linf_upper"]
    assert len(vals) == nz and all(math.isfinite(v) and v >= 0 for v in vals)
    b["per_level_linf_upper_binary64_hex"] = [float(v).hex() for v in vals]
    assert all(
        float.fromhex(h) == v
        for h, v in zip(b["per_level_linf_upper_binary64_hex"], vals)
    )


p = argparse.ArgumentParser()
p.add_argument("receipt", type=pathlib.Path)
p.add_argument("raw_dir", type=pathlib.Path)
p.add_argument("theta", type=pathlib.Path)
p.add_argument("reconstruction", type=pathlib.Path)
p.add_argument("output", type=pathlib.Path)
a = p.parse_args()
r = json.loads(a.receipt.read_text())
rec = json.loads(a.reconstruction.read_text())
raw = r["raw_eos"]
sc = r["eos_scalars"]
assert raw["scope"] == "rounded_EOS_subroutine_inputs_and_outputs_only"
items = {x["name"]: x for x in raw["files"]}
assert len(raw["files"]) == len(NAMES) and set(items) == set(NAMES)
shape = items["base_t"]["shape"]
assert len(shape) == 3
ny, nz, nx = shape
for name in NAMES:
    x = items[name]
    expected = (
        [ny, nz + 1, nx]
        if name.endswith("_ph")
        else [ny, nx]
        if name.endswith("_mu") or name == "mu_base"
        else [nz]
        if name in ("c1h", "c2h", "rdnw")
        else [ny, nz, nx]
    )
    assert (
        x["shape"] == expected
        and x["path"] == f"eos_raw/{name}.npy"
        and x["source_dtype"] in (6, 7)
    )
    if name.startswith(("base_", "accepted_")):
        assert x["source_dtype"] == 7
    path = a.raw_dir / (name + ".npy")
    v = np.load(path, allow_pickle=False)
    assert (
        v.dtype == np.dtype("<f8")
        and v.ndim == 1
        and v.flags.c_contiguous
        and v.size == math.prod(expected) == x["elements"]
        and v.nbytes == x["data_bytes"]
        and np.isfinite(v).all()
    )
    if x["source_dtype"] == 6:
        with np.errstate(over="ignore", invalid="ignore"):
            w = v.astype(np.float32).astype(np.float64)
        assert np.isfinite(w).all() and np.array_equal(
            v.view(np.uint64), w.view(np.uint64)
        )
    assert fnv(v.tobytes()) == x["fnv64"]
    x["sha256_verified"] = sha(path)
theta = np.load(a.theta, allow_pickle=False)
assert (
    theta.dtype == np.dtype("<f8")
    and theta.ndim == 1
    and theta.size == ny * nz * nx
    and np.isfinite(theta).all()
    and (theta > 0).all()
)
assert rec["theta_shape"] == shape and rec["theta_dtype"] == "float64"
context = {v["name"]: v["fnv64"] for v in r["base_context_hashes"]}
assert (
    rec["theta_file_sha256"] == sha(a.theta)
    and rec["theta_fnv64"] == fnv(theta.tobytes()) == context["th_base"]
    and rec["receipt_sha256"] == sha(a.receipt)
    and rec["pb_bitwise_equal_to_captured"] is True
)
constants = [sc[k] for k in ("rd", "cv", "cp", "p0", "t0")]
assert all(isinstance(v, (float, int)) and math.isfinite(v) for v in constants)
cmd = [
    str(E / "alb_mpfr"),
    str(ny),
    str(nz),
    str(nx),
    *[repr(v) for v in constants],
    "100000",
    str(a.theta),
    *[str(a.raw_dir / (n + ".npy")) for n in NAMES],
]
p1 = subprocess.run(cmd, text=True, capture_output=True)
assert p1.returncode == 0, p1.stderr
analysis = json.loads(p1.stdout)
assert analysis["cells_per_state"] == ny * nz * nx and len(analysis["states"]) == 2
encode(analysis["upstream_alb"], nz)
for state in analysis["states"]:
    for name in (
        "induced_alpha",
        "induced_al_pert",
        "induced_p_pert",
        "composed_native_alpha",
        "composed_native_p_pert",
    ):
        encode(state[name], nz)
out = {
    "status": "passed",
    "scope": "alb component and induced/composed EOS at captured rounded inputs; excludes state formation, other RHS, norm and exact merit",
    "limitations": [
        "theta reconstruction matches recorded FNV only, original theta bytes unavailable; collision-qualified",
        "native al_pert not retained; induced al_pert is not a composed-native error",
        "trusted 256-bit MPFR directed rounding and faithful captured input provenance",
        "fixed p1000mb=100000 is distinct from EOS p0",
    ],
    "bound_encoding": "RNDU binary64; decimal JSON must be parsed as binary64 and checked against hex",
    "receipt_sha256": sha(a.receipt),
    "theta_sha256": sha(a.theta),
    "reconstruction_sha256": sha(a.reconstruction),
    "checker_source_sha256": sha(E / "alb_mpfr.cpp"),
    "checker_binary_sha256": sha(E / "alb_mpfr"),
    "files": {n: items[n]["sha256_verified"] for n in NAMES},
    "shape": shape,
    "constants": dict(zip(("rd", "cv", "cp", "eos_p0", "t0"), constants)),
    "p1000mb": 100000,
    "analysis": analysis,
}
with a.output.open("x") as f:
    json.dump(out, f, indent=2, allow_nan=False)
    f.write("\n")
