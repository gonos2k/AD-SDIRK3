#!/usr/bin/env python3
"""Validate a prereviewed capture manifest and run the exact-real W reference."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

import numpy as np

E = Path(__file__).resolve().parent
NAMES = (
    "B",
    "K0",
    "K1",
    "R0",
    "R1",
    "theta_base",
    "pbase",
    "mub",
    "c1h",
    "c2h",
    "c1f",
    "c2f",
    "mass_rdnw",
    "eos_rdnw",
    "pressure_rdn",
    "top_rdnw",
)
GATES = (
    "mode3_implicit_only",
    "single_tile",
    "fp64",
    "dry_cqw_zero",
    "unit_maps",
    "periodic_x_symmetric_y",
    "mu_parity",
    "identity_halo",
    "reference_update_off",
    "reuse_off",
    "hevi_off",
    "wdamp_off",
    "sanitize_off",
    "transport_off",
    "fixed_context",
    "accepted_pair_verified",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_equal_rdnw(mass_rdnw, eos_rdnw):
    if not np.array_equal(mass_rdnw.view(np.uint64), eos_rdnw.view(np.uint64)):
        raise ValueError("EOS and mass-flux RDNW differ; shared reference unsupported")


def validate(args):
    manifest = json.loads(args.manifest.read_text())
    if manifest["scope"] != "native_stage3_w_reference_inputs_v1":
        raise ValueError("unsupported capture scope")
    if manifest["grid"] != {
        "m": 80,
        "n": 40,
        "nz": 64,
        "packed_N": 1080491,
        "W_count": 215865,
    }:
        raise ValueError("unsupported physical grid/layout")
    if any(manifest["profile"].get(name) is not True for name in GATES):
        raise ValueError("unsupported or unverified profile")
    records = manifest["files"]
    if set(records) != set(NAMES):
        raise ValueError("missing or extra input files")
    sizes = {
        "B": 1080491,
        "K0": 1080491,
        "K1": 1080491,
        "R0": 1080491,
        "R1": 1080491,
        "theta_base": 212544,
        "pbase": 212544,
        "mub": 3321,
        "c1h": 64,
        "c2h": 64,
        "c1f": 65,
        "c2f": 65,
        "mass_rdnw": 64,
        "eos_rdnw": 64,
        "pressure_rdn": 64,
        "top_rdnw": 1,
    }
    for name in NAMES:
        record = records[name]
        path = args.input_dir / (name + ".npy")
        if record["path"] != name + ".npy" or sha(path) != record["sha256"]:
            raise ValueError("input SHA or path mismatch: " + name)
        data = np.load(path, allow_pickle=False)
        if (
            data.dtype != np.dtype("<f8")
            or data.ndim != 1
            or not data.flags.c_contiguous
            or data.size != sizes[name]
            or not np.isfinite(data).all()
        ):
            raise ValueError("invalid flat FP64 input: " + name)
        source_dtype = record["source_dtype"]
        if source_dtype not in (6, 7) or (
            name
            in (
                "B",
                "K0",
                "K1",
                "R0",
                "R1",
                "theta_base",
                "pbase",
                "mub",
                "mass_rdnw",
                "eos_rdnw",
            )
            and source_dtype != 7
        ):
            raise ValueError("source dtype unsupported: " + name)
        if source_dtype == 6:
            with np.errstate(over="ignore", invalid="ignore"):
                promoted = data.astype(np.float32).astype(np.float64)
            if not np.isfinite(promoted).all() or not np.array_equal(
                data.view(np.uint64), promoted.view(np.uint64)
            ):
                raise ValueError("not exact FP32 promotion: " + name)
    mass_rdnw = np.load(args.input_dir / "mass_rdnw.npy", allow_pickle=False)
    eos_rdnw = np.load(args.input_dir / "eos_rdnw.npy", allow_pickle=False)
    validate_equal_rdnw(mass_rdnw, eos_rdnw)
    weights = manifest["W_inverse_weight_extrema"]
    lo, hi = weights["min"], weights["max"]
    if not (math.isfinite(lo) and math.isfinite(hi) and 0 <= lo <= hi and hi > 0):
        raise ValueError("invalid observed W inverse weight extrema")
    constants = manifest["constants"]
    keys = ("a", "rdx", "rdy", "g", "rd", "cv", "cp", "p0", "t0")
    if set(constants) != set(keys) or any(
        not isinstance(constants[k], (int, float)) or not math.isfinite(constants[k])
        for k in keys
    ):
        raise ValueError("invalid scalar constants")
    return manifest, hi, constants


def main():
    p = argparse.ArgumentParser()
    p.add_argument("manifest", type=Path)
    p.add_argument("input_dir", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    manifest, weight, constants = validate(args)
    if args.output.exists():
        raise FileExistsError(args.output)
    with tempfile.TemporaryDirectory(prefix="w_mpfr_weight_") as temp:
        derived = Path(temp) / "weight_upper.npy"
        np.save(derived, np.full(215865, weight, dtype="<f8"), allow_pickle=False)
        command = [
            str(E / "w_full_mpfr"),
            "80",
            "40",
            "64",
            *[
                repr(constants[k])
                for k in ("a", "rdx", "rdy", "g", "rd", "cv", "cp", "p0", "t0")
            ],
            *[str(args.input_dir / (n + ".npy")) for n in NAMES if n != "eos_rdnw"],
            str(derived),
        ]
        run = subprocess.run(command, text=True, capture_output=True)
    if run.returncode:
        raise RuntimeError(run.stderr)
    result = json.loads(run.stdout)
    if (
        result["W_count_per_state"] != 215865
        or result["packed_N"] != 1080491
        or len(result["states"]) != 2
    ):
        raise ValueError("reference output size mismatch")
    for state in result["states"]:
        for group in (state["physical_error"], state):
            keys = (
                ("linf_upper", "l2_upper")
                if group is state["physical_error"]
                else ("weighted_math_rms_upper",)
            )
            for key in keys:
                value = group[key]
                if not math.isfinite(value) or value < 0:
                    raise ValueError("invalid upper bound")
                group[key + "_binary64_hex"] = float(value).hex()
                if float.fromhex(group[key + "_binary64_hex"]) != value:
                    raise ValueError("bound hex mismatch")
        physical = state["physical_error"]
        if len(physical["per_level_linf_upper"]) != 65:
            raise ValueError("invalid vertical bound count")
        physical["per_level_linf_upper_binary64_hex"] = [
            float(v).hex() for v in physical["per_level_linf_upper"]
        ]
    out = {
        "status": "passed_conditional",
        "scope": "W residual at exact Q(B+aK), captured fixed coefficients and ideal EOS; excludes prior formation of B/K, other components and native norm",
        "manifest_sha256": sha(args.manifest),
        "reference_source_sha256": sha(E / "w_full_mpfr.cpp"),
        "reference_binary_sha256": sha(E / "w_full_mpfr"),
        "input_sha256": {n: sha(args.input_dir / (n + ".npy")) for n in NAMES},
        "weight": "uniform upper derived from observed W inverse-weight maximum; not a captured weight vector",
        "encoding": "MPFR RNDU binary64; JSON decimals require binary64 parsing and matching exact hex",
        "assumptions": manifest["assumptions"],
        "analysis": result,
    }
    with args.output.open("x") as f:
        json.dump(out, f, indent=2, allow_nan=False)
        f.write("\n")


if __name__ == "__main__":
    main()
