#!/usr/bin/env python3
"""Conditional exact-real W-pressure input sensitivity, not native RHS error."""

import argparse
import hashlib
import json
import math
from fractions import Fraction
from pathlib import Path

import numpy as np


def exact(value):
    if (
        isinstance(value, bool)
        or not isinstance(value, (float, int))
        or not math.isfinite(value)
    ):
        raise ValueError("finite numeric input required")
    return Fraction.from_float(float(value))


def compute(coeff, endpoint, n_w, n_total):
    """All inputs are captured/promoted binary values, not floating estimates."""
    if not (0 < n_w and 0 < n_total and 4 * n_w < n_total):
        raise ValueError("one-half RMS factor requires 4*Nw < N")
    nz = len(endpoint["pressure_error"])
    if (
        nz < 2
        or len(coeff["c1f"]) != nz + 1
        or len(coeff["c2f"]) != nz + 1
        or len(coeff["rdn"]) != nz
        or len(coeff["rdnw"]) != nz
    ):
        raise ValueError("vertical coefficient shape mismatch")
    g = exact(coeff["gravity"])
    rdn = [exact(v) for v in coeff["rdn"]]
    rdnw = [exact(v) for v in coeff["rdnw"]]
    e = [exact(v) for v in endpoint["pressure_error"]]
    lo = exact(endpoint["mu_min"]) + exact(endpoint["mub_min"])
    hi = exact(endpoint["mu_max"]) + exact(endpoint["mub_max"])
    if (
        g <= 0
        or any(v < 0 for v in e)
        or any(v <= 0 for v in rdn[1:])
        or any(v <= 0 for v in rdnw)
    ):
        raise ValueError("invalid coefficient or error sign")
    levels = [Fraction(0)]
    for k in range(1, nz + 1):
        c1, c2 = exact(coeff["c1f"][k]), exact(coeff["c2f"][k])
        denom = min(c1 * lo, c1 * hi) + c2
        if denom <= 0:
            raise ValueError("W hybrid denominator not positive")
        if k < nz:
            b = g * rdn[k] * (e[k] + e[k - 1]) / denom
        else:
            b = 2 * g * rdnw[-1] * e[-1] / denom
        levels.append(b)
    max_error = max(levels)
    # Qw is a copy/zero map (infinity norm <= 1); Nw/N < 1/4.
    # A full binary32 ulp above 1 also covers the stated reciprocal allowance.
    wmax = Fraction(1) + Fraction(1, 2**23)
    weighted = wmax * max_error / 2
    return {
        "per_level_error_exact": [str(v) for v in levels],
        "physical_linf_upper_exact": str(max_error),
        "weighted_rms_upper_exact": str(weighted),
        "weight_upper_exact": str(wmax),
        "scope": "exact-real W pressure-input sensitivity only; excludes native W arithmetic, coefficient/state formation, other RHS and norm error",
    }


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_receipt_npy(path, entry, expected_shape):
    if entry["shape"] != expected_shape or entry["source_dtype"] != 7:
        raise ValueError("mu metadata mismatch")
    x = np.load(path, allow_pickle=False)
    if (
        x.dtype != np.dtype("<f8")
        or x.ndim != 1
        or x.size != math.prod(expected_shape)
        or not np.isfinite(x).all()
    ):
        raise ValueError("invalid captured mu array")
    h = 14695981039346656037
    for byte in x.tobytes():
        h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
    if str(h) != entry["fnv64"]:
        raise ValueError("mu FNV mismatch")
    return x


def validate_profile(receipt):
    if receipt.get("mask_scope") != "identity" or receipt.get("N") != 1080491:
        raise ValueError("unsupported halo mask or packed size")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("coefficients", type=Path)
    p.add_argument("analysis", type=Path)
    p.add_argument("receipt", type=Path)
    p.add_argument("raw_dir", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    coeff = json.loads(a.coefficients.read_text())
    alb = json.loads(a.analysis.read_text())
    receipt = json.loads(a.receipt.read_text())
    validate_profile(receipt)
    if (
        coeff["runtime_rdn_identity_verified"] is not False
        or coeff["gravity_runtime_identity_verified"] is not False
    ):
        raise ValueError("unverified coefficient assumptions must remain explicit")
    if (
        coeff["wrfinput_sha256"]
        != "c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912"
        or coeff["gravity"] != float(np.float32(9.81))
        or alb["status"] != "passed"
        or len(alb["analysis"]["states"]) != 2
    ):
        raise ValueError("unsupported coefficient provenance or analysis")
    rdn_candidate = np.asarray(coeff["rdn"], dtype=np.float64)
    if not np.array_equal(
        rdn_candidate.view(np.uint64),
        rdn_candidate.astype(np.float32).astype(np.float64).view(np.uint64),
    ):
        raise ValueError("RDN candidate is not an exact FP32 promotion")
    if coeff["receipt_sha256"] != sha(a.receipt) or alb["receipt_sha256"] != sha(
        a.receipt
    ):
        raise ValueError("analysis or coefficient receipt mismatch")
    if alb["shape"] != [81, 64, 41] or alb["analysis"]["cells_per_state"] != 212544:
        raise ValueError("profile shape mismatch")
    context = {v["name"]: v["fnv64"] for v in receipt["base_context_hashes"]}
    for name in ("c1f", "c2f"):
        candidate = np.asarray(coeff[name], dtype=np.float64)
        f32 = candidate.astype(np.float32)
        if not np.array_equal(
            candidate.view(np.uint64), f32.astype(np.float64).view(np.uint64)
        ):
            raise ValueError("face coefficient is not an exact FP32 promotion")
        h = 14695981039346656037
        for byte in f32.tobytes():
            h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
        if str(h) != context[name] or coeff[name + "_fnv64"] != context[name]:
            raise ValueError("face coefficient fingerprint mismatch")
    if coeff["msfty_fnv64"] != context["msfty"] or coeff["cqw_fnv64"] != context["cqw"]:
        raise ValueError("map or dry fingerprint mismatch")
    entries = {v["name"]: v for v in receipt["raw_eos"]["files"]}
    rdnw_file = a.raw_dir / "rdnw.npy"
    rdnw = np.load(rdnw_file, allow_pickle=False)
    if sha(rdnw_file) != alb["files"]["rdnw"] or not np.array_equal(
        np.asarray(coeff["rdnw"], dtype=np.float64),
        rdnw.astype(np.float32).astype(np.float64),
    ):
        raise ValueError("top rdnw mismatch")
    shape = [81, 41]
    mb = validate_receipt_npy(a.raw_dir / "mu_base.npy", entries["mu_base"], shape)
    result = []
    for state, name in zip(alb["analysis"]["states"], ("base_mu", "accepted_mu")):
        mu = validate_receipt_npy(a.raw_dir / (name + ".npy"), entries[name], shape)
        if (
            sha(a.raw_dir / (name + ".npy")) != alb["files"][name]
            or sha(a.raw_dir / "mu_base.npy") != alb["files"]["mu_base"]
        ):
            raise ValueError("mu analysis SHA mismatch")
        b = state["composed_native_p_pert"]
        errors = b["per_level_linf_upper"]
        if (
            len(errors) != 64
            or any(
                float.fromhex(h) != v
                for h, v in zip(b["per_level_linf_upper_binary64_hex"], errors)
            )
            or len(b["per_level_linf_upper_binary64_hex"]) != 64
        ):
            raise ValueError("pressure bound encoding mismatch")
        endpoint = {
            "mu_min": float(mu.min()),
            "mu_max": float(mu.max()),
            "mub_min": float(mb.min()),
            "mub_max": float(mb.max()),
            "pressure_error": errors,
        }
        result.append(compute(coeff, endpoint, 215865, 1080491))
    out = {
        "status": "conditional_source_profile_bound",
        "assumptions": [
            "runtime RDN equals the reconstructed candidate: NOT verified",
            "runtime gravity equals binary32 9.81: NOT verified",
            "dry cq1=1 and fixed finite source geometry",
            "source-profile FP64 scaling floor and reciprocal allowance imply W inverse weight <= 1+2^-23; not a measurement",
            "Qw infinity norm <=1, H identity, fixed state and EOS input-error enclosures",
            "all exact-real cancellation and input-provenance assumptions hold",
        ],
        "full_rhs_or_native_error_certified": False,
        "inputs_sha256": {
            "coefficients": sha(a.coefficients),
            "analysis": sha(a.analysis),
            "receipt": sha(a.receipt),
        },
        "states": result,
    }
    with a.output.open("x") as f:
        json.dump(out, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
