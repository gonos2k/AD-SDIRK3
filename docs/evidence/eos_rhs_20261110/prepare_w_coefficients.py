#!/usr/bin/env python3
"""Reconstruct coefficient candidates and check available recorded fingerprints."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import xarray as xr


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fnv(array):
    h = 14695981039346656037
    for b in np.ascontiguousarray(array).tobytes():
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return str(h)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("wrfinput", type=Path)
    p.add_argument("receipt", type=Path)
    p.add_argument("raw_dir", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    if (
        sha(a.wrfinput)
        != "c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912"
    ):
        raise ValueError("wrfinput SHA mismatch")
    receipt = json.loads(a.receipt.read_text())
    context = {x["name"]: x["fnv64"] for x in receipt["base_context_hashes"]}
    data = xr.open_dataset(a.wrfinput, decode_times=False, mask_and_scale=False)
    c1, c2 = (np.asarray(data[name].values[0]) for name in ("C1F", "C2F"))
    if (
        c1.shape != (65,)
        or c2.shape != (65,)
        or c1.dtype != np.float32
        or c2.dtype != np.float32
    ):
        raise ValueError("coefficient metadata")
    if fnv(c1) != context["c1f"] or fnv(c2) != context["c2f"]:
        raise ValueError("c1f/c2f fingerprint mismatch")
    mapcore = np.asarray(data["MAPFAC_MY"].values[0])
    if mapcore.shape != (80, 40) or mapcore.dtype != np.float32:
        raise ValueError("map metadata")
    msfty = np.empty((81, 41), dtype=np.float64)
    msfty[:80, :40] = mapcore
    msfty[:80, 40] = msfty[:80, 0]
    msfty[80, :] = msfty[79, :]
    if fnv(msfty) != context["msfty"]:
        raise ValueError("map fingerprint mismatch")
    cqw = np.zeros((81, 64, 41), dtype=np.float32)
    if fnv(cqw) != context["cqw"]:
        raise ValueError("dry cqw fingerprint mismatch")
    rdn = np.abs(np.asarray(data["RDN"].values[0], dtype=np.float32))
    if rdn.shape != (64,):
        raise ValueError("rdn metadata")
    rdn[0] = np.float32(0)
    rdnw = np.load(a.raw_dir / "rdnw.npy", allow_pickle=False)
    rec = next(x for x in receipt["raw_eos"]["files"] if x["name"] == "rdnw")
    if (
        rdnw.dtype != np.dtype("<f8")
        or rdnw.shape != (64,)
        or fnv(rdnw) != rec["fnv64"]
    ):
        raise ValueError("rdnw receipt mismatch")
    # W top explicitly requests float32 rdnw, regardless of recorded FP64 promotion.
    top_rdnw = rdnw.astype(np.float32).astype(np.float64)
    out = {
        "scope": "candidate coefficients, selected fingerprints; not full-context proof",
        "receipt_sha256": sha(a.receipt),
        "wrfinput_sha256": sha(a.wrfinput),
        "c1f": c1.astype(np.float64).tolist(),
        "c2f": c2.astype(np.float64).tolist(),
        "rdn": rdn.astype(np.float64).tolist(),
        "rdnw": top_rdnw.tolist(),
        "gravity": float(np.float32(9.81)),
        "c1f_fnv64": fnv(c1),
        "c2f_fnv64": fnv(c2),
        "msfty_fnv64": fnv(msfty),
        "cqw_fnv64": fnv(cqw),
        "runtime_rdn_identity_verified": False,
        "gravity_runtime_identity_verified": False,
        "limitations": [
            "FNV matches are collision-qualified",
            "RDN and gravity runtime equality are assumptions, not observed",
            "fixed state, dry branch, map and projection assumptions remain conditional",
        ],
    }
    with a.output.open("x") as f:
        json.dump(out, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
