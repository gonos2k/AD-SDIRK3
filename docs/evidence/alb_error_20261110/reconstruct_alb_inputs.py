#!/usr/bin/env python3
"""Reconstruct Stage3 base theta; compare pressure bytes and logged fingerprints."""

import hashlib
import json
import pathlib
import numpy as np
import xarray as xr

E = pathlib.Path(__file__).resolve().parent


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def fnv(a):
    h = 14695981039346656037
    for b in np.ascontiguousarray(a).tobytes():
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return str(h)


def pad(x):
    assert x.shape == (64, 80, 40) and x.dtype == np.float32
    x = x.transpose(1, 0, 2)
    out = np.empty((81, 64, 41), dtype=np.float32)
    out[:80, :, :40] = x
    out[:80, :, 40] = out[:80, :, 0]
    out[80] = out[79]
    return out


import argparse

parser = argparse.ArgumentParser()
parser.add_argument("wrfinput", type=pathlib.Path)
parser.add_argument("receipt", type=pathlib.Path)
parser.add_argument("raw_dir", type=pathlib.Path)
parser.add_argument("output_dir", type=pathlib.Path)
args = parser.parse_args()
src = args.wrfinput
receipt = args.receipt
assert sha(src) == "c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912"
assert (
    sha(receipt) == "9d252ca255cef90546f34119aefeb8df03c573413115241f9b09252aa956bd56"
)
r = json.loads(receipt.read_text())
context = {v["name"]: v["fnv64"] for v in r["base_context_hashes"]}
ds = xr.open_dataset(src, decode_times=False, mask_and_scale=False)
pb = pad(ds.PB.values[0]).astype("<f8")
theta = (pad(ds.T_INIT.values[0]) + np.float32(300)).astype("<f8")
assert (
    np.isfinite(pb).all()
    and np.isfinite(theta).all()
    and (pb > 0).all()
    and (theta > 0).all()
)
assert fnv(pb) == context["p_base"] and fnv(theta) == context["th_base"]
original = np.load(args.raw_dir / "p_base.npy", allow_pickle=False).reshape(pb.shape)
assert np.array_equal(pb.view(np.uint64), original.view(np.uint64))
args.output_dir.mkdir(parents=True, exist_ok=True)
target = args.output_dir / "theta_base.npy"
with target.open("xb") as f:
    np.save(f, theta.reshape(-1), allow_pickle=False)
out = {
    "scope": "reconstruction only; theta matches recorded FNV, not a retained byte-for-byte original",
    "input_sha256": sha(src),
    "receipt_sha256": sha(receipt),
    "theta_fnv64": fnv(theta),
    "pb_fnv64": fnv(pb),
    "pb_bitwise_equal_to_captured": True,
    "theta_file_sha256": sha(target),
    "theta_shape": [81, 64, 41],
    "theta_dtype": "float64",
    "method": "T_INIT fp32+300 fp32 then exact fp64 promotion; periodic X and symmetric Y copy",
    "limits": [
        "theta original data not retained: FNV match is fingerprint evidence, not collision-free proof",
        "excludes upstream T_INIT/PB formation and full RHS",
    ],
}
with (args.output_dir / "reconstruction.json").open("x") as f:
    json.dump(out, f, indent=2)
    f.write("\n")
print(json.dumps(out, indent=2))
