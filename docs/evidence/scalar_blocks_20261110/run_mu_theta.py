#!/usr/bin/env python3
"""Run selected mu/theta reference on a verified prior W capture."""

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import struct
from types import SimpleNamespace

E = Path(__file__).resolve().parent
VALIDATOR_SHA = "5ae180bad34712bb1f8aaadfdef65fd10ad59391227c73ca808c5989e28a2b32"
MANIFEST_SHA = "1737005ed45133e7a2b84d6c115d23f8d876d816cc10c062faa0b719f4d906c6"
PACKET_SHA = "5ba7abf1e36ddf631ad41e837387b46c928413168f997cb9f3510478f327149e"
NAMES = ("B", "K0", "K1", "R0", "R1", "mub", "c1h", "c2h", "mass_rdnw")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def add_hex(group, nz):
    for name in ("linf_upper", "l2_upper"):
        value = group[name]
        if not math.isfinite(value) or value < 0:
            raise ValueError("invalid interval upper")
        group[name + "_binary64_hex"] = float(value).hex()
        if float.fromhex(group[name + "_binary64_hex"]) != value:
            raise ValueError("invalid binary64 upper encoding")
    values = group["per_level_linf_upper"]
    if len(values) != nz or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("invalid per-level upper")
    group["per_level_linf_upper_binary64_hex"] = [float(v).hex() for v in values]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("manifest", type=Path)
    p.add_argument("input_dir", type=Path)
    p.add_argument("packet", type=Path)
    p.add_argument("w_validator", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    if a.output.exists():
        raise FileExistsError(a.output)
    if (
        sha(a.manifest) != MANIFEST_SHA
        or sha(a.packet) != PACKET_SHA
        or sha(a.w_validator) != VALIDATOR_SHA
    ):
        raise ValueError("prior W manifest, packet or validator SHA mismatch")
    spec = importlib.util.spec_from_file_location("prior_w_validation", a.w_validator)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    manifest, _, constants = module.validate(
        SimpleNamespace(manifest=a.manifest, input_dir=a.input_dir)
    )
    if manifest["origins"]["packet_sha256"] != sha(a.packet):
        raise ValueError("manifest and packet mismatch")
    packet = json.loads(a.packet.read_text())
    if packet["mask_scope"] != "identity" or packet["N"] != 1080491:
        raise ValueError("unsupported mask or packed layout")
    blocks = {v["name"]: v for v in packet["w_reference"]["layout"]}
    expected = {"t": (864626, 212544), "mu": (1077170, 3321)}
    weights = {}
    for name, (start, count) in expected.items():
        b = blocks[name]
        if (b["start"], b["size"]) != (start, count) or not (
            0 < b["sinv_min"] <= b["sinv_max"] and math.isfinite(b["sinv_max"])
        ):
            raise ValueError("invalid scalar block metric")
        weights[name] = b["sinv_max"]
    command = [
        str(E / "mu_theta_mpfr"),
        "80",
        "40",
        "64",
        repr(constants["a"]),
        repr(constants["rdx"]),
        repr(constants["rdy"]),
        repr(weights["t"]),
        repr(weights["mu"]),
        *[str(a.input_dir / (name + ".npy")) for name in NAMES],
    ]
    run = subprocess.run(command, text=True, capture_output=True)
    if run.returncode:
        raise RuntimeError(run.stderr)
    data = json.loads(run.stdout)
    if (
        data["packed_N"] != 1080491
        or data["t_count"] != 212544
        or data["mu_count"] != 3321
        or len(data["states"]) != 2
    ):
        raise ValueError("reference output count mismatch")
    low = struct.unpack("<f", struct.pack("<f", 1e-10))[0]
    high = struct.unpack("<f", struct.pack("<f", 1e-4))[0]
    if data["eps_theta_low"] != low or data["eps_theta_high"] != high:
        raise ValueError("epsilon interval does not match source binary32 choices")
    for state in data["states"]:
        add_hex(state["theta_error"], 64)
        add_hex(state["mu_error"], 1)
        for key in ("mu_weighted_math_rms_upper", "theta_weighted_math_rms_upper"):
            value = state[key]
            if not math.isfinite(value) or value < 0:
                raise ValueError("invalid weighted upper")
            state[key + "_binary64_hex"] = float(value).hex()
    result = {
        "status": "passed_conditional",
        "scope": "mu/T residual for each fixed eps in source-choice hull at exact Q(B+aK); captured B,K and C treated as exact; excludes prior formation, other residual blocks and native norm",
        "epsilon_scope": "uniform enclosure for every fixed eps in [binary32(1e-10),binary32(1e-4)]; actual autocast state not observed",
        "manifest_sha256": sha(a.manifest),
        "packet_sha256": sha(a.packet),
        "prior_validator_sha256": sha(a.w_validator),
        "source_sha256": sha(E / "mu_theta_mpfr.cpp"),
        "binary_sha256": sha(E / "mu_theta_mpfr"),
        "input_sha256": {name: sha(a.input_dir / (name + ".npy")) for name in NAMES},
        "weight_upper_from_observed_block_extrema": weights,
        "encoding": "MPFR RNDU binary64, decimal JSON requires binary64 parse and companion exact hexadecimal representation",
        "assumptions": manifest["assumptions"]
        + [
            "source theta epsilon equals one of the two float32 source choices, enclosed by reported interval",
            "pre-capture formation of B/K/C excluded; native residual and exact-real Q(B+aK) compared at captured given operands",
        ],
        "analysis": data,
    }
    with a.output.open("x") as f:
        json.dump(result, f, indent=2, allow_nan=False)
        f.write("\n")


if __name__ == "__main__":
    main()
