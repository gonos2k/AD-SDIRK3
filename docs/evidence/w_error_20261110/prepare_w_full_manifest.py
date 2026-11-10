#!/usr/bin/env python3
"""Validate a selected native capture and stage inputs for the MPFR W reference.

NumPy binary64 B+a*K and Q here are validation only.  The reference independently
forms exact-real Q(B+aK) with MPFR. Paths are explicit; output directory is exclusive.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil

import numpy as np

REF_NAMES = ("B", "K0", "K1", "R0", "R1", "theta_base", "c1f", "c2f", "mass_rdnw")
EOS_NAMES = (
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
)
PAIR_KEYS = (
    "base_K_hash",
    "base_R_hash",
    "post_K_hash",
    "post_R_hash",
    "S_inv_hash",
    "reference_hash",
    "native_scaled_rms_before",
    "native_scaled_rms_after",
    "N",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return str(value)


def read_records(root, entries, expected_names, subdir):
    records = {x["name"]: x for x in entries}
    if len(records) != len(entries) or set(records) != set(expected_names):
        raise ValueError("invalid capture file inventory")
    values = {}
    for name, entry in records.items():
        if entry["path"] != f"{subdir}/{name}.npy" or entry["source_dtype"] not in (
            6,
            7,
        ):
            raise ValueError("invalid capture path or dtype")
        data = np.load(root / entry["path"], allow_pickle=False)
        if (
            data.dtype != np.dtype("<f8")
            or data.ndim != 1
            or not data.flags.c_contiguous
            or not np.isfinite(data).all()
        ):
            raise ValueError("invalid flat finite FP64 array")
        if (
            data.size != math.prod(entry["shape"])
            or data.size != entry["elements"]
            or data.nbytes != entry["data_bytes"]
            or fnv(data.tobytes()) != entry["fnv64"]
        ):
            raise ValueError("capture data metadata mismatch")
        if entry["source_dtype"] == 6:
            promoted = data.astype(np.float32).astype(np.float64)
            if not np.isfinite(promoted).all() or not np.array_equal(
                data.view(np.uint64), promoted.view(np.uint64)
            ):
                raise ValueError("invalid original FP32 promotion")
        values[name] = data
    return records, values


def project_native(B, K, a, m, n, z):
    """Native-rounding validation surrogate, including V ghost's signed zero."""
    ny, nx = m + 1, n + 1
    nu = ny * z * (nx + 1)
    nv = (ny + 1) * z * nx
    nw = ny * (z + 1) * nx
    nt = ny * z * nx
    nmu = ny * nx
    if len(B) != nu + nv + 2 * nw + nt + nmu or len(K) != len(B):
        raise ValueError("packed vector length")
    # Separate materialized NumPy operations preserve native no-FMA evaluation.
    Y = np.add(B, np.multiply(np.float64(a), K))
    if not np.isfinite(Y).all():
        raise ValueError("nonfinite formed state")
    u = Y[:nu].reshape(ny, z, nx + 1)
    v = Y[nu : nu + nv].reshape(ny + 1, z, nx)
    w = Y[nu + nv : nu + nv + nw].reshape(ny, z + 1, nx)
    ph = Y[nu + nv + nw : nu + nv + 2 * nw].reshape(ny, z + 1, nx)
    t = Y[nu + nv + 2 * nw : nu + nv + 2 * nw + nt].reshape(ny, z, nx)
    mu = Y[-nmu:].reshape(ny, nx)

    def mass(q):
        core = q[:m, ..., :n]
        x = np.concatenate((core, core[..., :1]), axis=-1)
        return np.ascontiguousarray(np.concatenate((x, x[m - 1 : m]), axis=0))

    ucore = u[:m, :, :n]
    uq = np.concatenate((ucore, ucore[:, :, :2]), axis=2)
    uq = np.ascontiguousarray(np.concatenate((uq, uq[m - 1 : m]), axis=0))
    vc = np.array(v[: m + 1, :, :n], copy=True)
    vc[0] = 0.0
    vc[m] = 0.0
    vx = np.concatenate((vc, vc[:, :, :1]), axis=2)
    vq = np.ascontiguousarray(np.concatenate((vx, -vx[m - 1 : m]), axis=0))
    return {
        "Y": Y,
        "u": np.ascontiguousarray(uq[:m, :, : n + 1]),
        "v": np.ascontiguousarray(vq[: m + 1, :, :n]),
        "v_full": vq,
        "w": mass(w),
        "ph": mass(ph),
        "t": mass(t),
        "mu": mass(mu),
    }


def validate_profile(pr, mass_rdnw_matches_eos):
    for key in (
        "implicit_only",
        "canonical_horizontal",
        "packed",
        "mu_horizontal",
        "mu_fortran_parity",
        "six_maps_unit",
        "pre_pgf_rw_zero",
    ):
        if pr.get(key) is not True:
            raise ValueError("unsupported profile " + key)
    for key in ("hevi", "split_explicit", "implicit_transport"):
        if pr.get(key) is not False:
            raise ValueError("unsupported profile " + key)
    if (
        pr["nan_sanitize"] != 0
        or pr["wrf_w_damping"] != 0
        or pr["qv_status"] not in (0, 1)
        or pr["cqw_status"] != 1
        or mass_rdnw_matches_eos is not True
    ):
        raise ValueError("unverified dry/fixed branch")


def validate_digest(record, array, expected_shape):
    if (
        record["dtype"] != 7
        or record["shape"] != expected_shape
        or fnv(array.tobytes()) != record["fnv64"]
    ):
        raise ValueError("native projected input digest mismatch")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("packet", type=Path)
    p.add_argument("result", type=Path)
    p.add_argument("run_dir", type=Path)
    p.add_argument("old_packet", type=Path)
    p.add_argument("old_raw_dir", type=Path)
    p.add_argument("runtime_packet", type=Path)
    p.add_argument("output_dir", type=Path)
    p.add_argument("source_root", type=Path)
    p.add_argument("build_receipt", type=Path)
    a = p.parse_args()
    packet = json.loads(a.packet.read_text())
    result = json.loads(a.result.read_text())
    old = json.loads(a.old_packet.read_text())
    runtime_old = json.loads(a.runtime_packet.read_text())
    build = json.loads(a.build_receipt.read_text())
    attempt = json.loads((a.run_dir / "attempt.json").read_text())
    expected_source = "d05130c0f30b5ce0397b2090a7103bf3aba7fcb3"
    if (
        build["source_commit"] != expected_source
        or attempt["target_head"] != expected_source
        or attempt["build_receipt_sha256"] != sha(a.build_receipt)
    ):
        raise ValueError("build or source revision mismatch")
    for source, digest in build["source_hashes"].items():
        if sha(a.source_root / source) != digest:
            raise ValueError("source SHA mismatch: " + source)
    artifacts = result["artifacts"]
    for artifact in ("accepted_step_probe.json", "attempt.json"):
        if sha(a.run_dir / artifact) != artifacts[artifact]["sha256"]:
            raise ValueError("native result artifact SHA mismatch: " + artifact)
    if (
        result["status"] != "diagnostic_completed"
        or result["errors"]
        or result["original_pair_target"] != "matched"
        or result["diagnostic"] != packet
        or result["steps"] != 0
        or result["stage_3_completed"] is not False
        or result["return_code"] != 86
        or result["forecast_complete"] is not False
        or result["performance_eligible"] is not False
        or result["logged_converged_stages"] != [2]
    ):
        raise ValueError("native run did not meet diagnostic receipt contract")
    if sha(a.old_packet) != result["original_pair_sha256"] or any(
        packet[k] != old[k] for k in PAIR_KEYS
    ):
        raise ValueError("original accepted pair mismatch")
    if (
        packet["base_context_hashes"] != old["base_context_hashes"]
        or packet["eos_scalars"] != old["eos_scalars"]
    ):
        raise ValueError("original fixed-context or EOS scalar receipt mismatch")
    if (
        packet["mask_scope"] != "identity"
        or packet["N"] != 1080491
        or packet["stage"] != 3
        or packet["newton"] != 0
        or packet["timestep"] != 0
        or packet["receipt_fingerprints_unchanged"] is not True
        or packet["profile"] != "mode3_single_tile_fp64_ref_update_off_reuse_off"
        or packet["outcome"] not in ("accepted_trust", "accepted_recovery")
        or packet["torch_threads"] != 1
        or packet["torch_interop_threads"] != 1
        or any(
            packet[k] != 0
            for k in (
                "receipt_rhs_delta",
                "receipt_jvp_delta",
                "receipt_preconditioner_delta",
            )
        )
    ):
        raise ValueError("native stage/profile mismatch")
    ref = packet["w_reference"]
    pr = ref["profile"]
    validate_profile(pr, ref["mass_rdnw_matches_eos"])
    if (
        ref["scope"] != "native_stage3_w_reference_inputs_v1"
        or ref["format"] != "flat_npy_little_endian_float64"
    ):
        raise ValueError("unsupported reference format")
    refmeta, values = read_records(a.run_dir, ref["files"], REF_NAMES, "w_reference")
    eosmeta, eos = read_records(
        a.run_dir, packet["raw_eos"]["files"], EOS_NAMES, "eos_raw"
    )
    oldmeta = {x["name"]: x for x in old["raw_eos"]["files"]}
    for record in ref["files"] + packet["raw_eos"]["files"]:
        artifact = record["path"]
        if (
            artifact not in artifacts
            or sha(a.run_dir / artifact) != artifacts[artifact]["sha256"]
        ):
            raise ValueError("native array artifact SHA mismatch: " + artifact)
    for name in EOS_NAMES:
        if eosmeta[name] != oldmeta[name] or sha(
            a.run_dir / eosmeta[name]["path"]
        ) != sha(a.old_raw_dir / (name + ".npy")):
            raise ValueError("old/new EOS data mismatch: " + name)
    for name, key in (
        ("B", "reference_hash"),
        ("K0", "base_K_hash"),
        ("K1", "post_K_hash"),
        ("R0", "base_R_hash"),
        ("R1", "post_R_hash"),
    ):
        if (
            refmeta[name]["shape"] != [1080491]
            or refmeta[name]["source_dtype"] != 7
            or refmeta[name]["fnv64"] != packet[key]
        ):
            raise ValueError("packed state fingerprint mismatch")
    contexts = {x["name"]: x for x in packet["base_context_hashes"]}
    for name, key in (("theta_base", "th_base"), ("c1f", "c1f"), ("c2f", "c2f")):
        value = (
            values[name]
            if refmeta[name]["source_dtype"] == 7
            else values[name].astype("<f4")
        )
        if (
            contexts[key]["defined"] is not True
            or fnv(value.tobytes()) != contexts[key]["fnv64"]
        ):
            raise ValueError("fixed input fingerprint mismatch")
    if not np.array_equal(
        values["mass_rdnw"].view(np.uint64), eos["rdnw"].view(np.uint64)
    ):
        raise ValueError("EOS/mass RDNW mismatch")
    blocks = ref["layout"]
    expected = [
        ("ru", 0, 217728),
        ("rv", 217728, 215168),
        ("rw", 432896, 215865),
        ("ph", 648761, 215865),
        ("t", 864626, 212544),
        ("mu", 1077170, 3321),
    ]
    if [(x["name"], x["start"], x["size"]) for x in blocks] != expected:
        raise ValueError("layout mismatch")
    metric = packet["w_runtime"]["metric"]
    rw = blocks[2]
    if (
        metric["block"] != "rw"
        or metric["start"] != rw["start"]
        or metric["count"] != rw["size"]
        or metric["dtype"] != 7
        or metric["full_S_inv_fnv64"] != packet["S_inv_hash"]
        or metric["min"] != rw["sinv_min"]
        or metric["max"] != rw["sinv_max"]
        or not (0 < metric["min"] <= metric["max"])
    ):
        raise ValueError("W metric mismatch")
    previous = runtime_old["w_runtime"]
    current = packet["w_runtime"]
    if current != previous or current["fixed_coefficients_equal"] is not True:
        raise ValueError("same-pair W runtime mismatch")
    b = current["base"]
    c = current["accepted"]
    if any(
        b[k] != c[k]
        for k in ("rdn_values", "rdn_fnv64_f32", "gravity_f32", "top_rdnw_f32")
    ):
        raise ValueError("runtime coefficient change")
    rdn = np.asarray(b["rdn_values"], dtype="<f4")
    if (
        rdn.shape != (64,)
        or fnv(rdn.tobytes()) != b["rdn_fnv64_f32"]
        or any(float(rdn[i]) != b["rdn_values"][i] for i in range(64))
        or b["rdn_used_begin"] != 1
        or b["rdn_used_end_exclusive"] != 64
    ):
        raise ValueError("runtime RDN metadata")
    a_coeff = packet["a"]
    B = values["B"]
    old_shapes = {
        "u": [80, 64, 41],
        "v": [81, 64, 40],
        "mu": [80, 40],
        "mub": [80, 40],
        "w": [81, 65, 41],
    }
    endpoint_proofs = {}
    for label, kname, prefix in (
        ("base_input_hashes", "K0", "base"),
        ("accepted_input_hashes", "K1", "accepted"),
    ):
        q = project_native(B, values[kname], a_coeff, 80, 40, 64)
        q["mub"] = eos["mu_base"].reshape(81, 41)[:80, :40].copy()
        q["mu_core"] = q["mu"][:80, :40].copy()
        expected = {v["name"]: v for v in ref[label]}
        if len(ref[label]) != 5 or set(expected) != set(old_shapes):
            raise ValueError("input digest inventory")
        for name in old_shapes:
            array = q["mu_core"] if name == "mu" else q[name]
            validate_digest(expected[name], array, old_shapes[name])
        for name in ("ph", "t", "mu"):
            if not np.array_equal(
                q[name].reshape(-1).view(np.uint64),
                eos[prefix + "_" + name].view(np.uint64),
            ):
                raise ValueError("projected EOS operand mismatch")
        if prefix == "accepted" and fnv(q["Y"].tobytes()) != packet["trial_input_hash"]:
            raise ValueError("trial input hash mismatch")
        endpoint_proofs[prefix] = {
            "input_hashes_match": True,
            "eos_ph_t_mu_bitwise_match": True,
            "full_formed_y_fnv64": fnv(q["Y"].tobytes()),
            "v_ghost_constructed_by_signed_negation": True,
        }
    if (
        any(not math.isfinite(float(packet[k])) for k in ("a", "dt", "gamma"))
        or packet["a"] != packet["dt"] * packet["gamma"]
    ):
        raise ValueError("coefficient mismatch")
    scal = packet["eos_scalars"]
    constants = {
        "a": a_coeff,
        "rdx": ref["rdx_f32"],
        "rdy": ref["rdy_f32"],
        "g": b["gravity_f32"],
        "rd": scal["rd"],
        "cv": scal["cv"],
        "cp": scal["cp"],
        "p0": scal["p0"],
        "t0": scal["t0"],
    }
    staged = {
        name: (a.run_dir / "w_reference" / f"{name}.npy", refmeta[name]["source_dtype"])
        for name in REF_NAMES
    }
    staged.update(
        {
            dest: (a.run_dir / "eos_raw" / f"{src}.npy", eosmeta[src]["source_dtype"])
            for dest, src in (
                ("pbase", "p_base"),
                ("mub", "mu_base"),
                ("c1h", "c1h"),
                ("c2h", "c2h"),
                ("eos_rdnw", "rdnw"),
            )
        }
    )
    # Fixed coefficients sampled as FP32 are promoted exactly; no invented top prefix.
    for key in ("rdx", "rdy", "g", "rd", "cv", "cp", "p0", "t0"):
        if (
            not math.isfinite(float(constants[key]))
            or float(np.float32(constants[key])) != constants[key]
        ):
            raise ValueError("constant not exact FP32 promotion: " + key)
    a.output_dir.mkdir(parents=True, exist_ok=False)
    files = {}
    for name, (src, dtype) in staged.items():
        dst = a.output_dir / (name + ".npy")
        shutil.copyfile(src, dst)
        files[name] = {"path": dst.name, "sha256": sha(dst), "source_dtype": dtype}
    for name, arr in (
        ("pressure_rdn", rdn.astype("<f8")),
        ("top_rdnw", np.asarray([b["top_rdnw_f32"]], dtype="<f8")),
    ):
        if not np.isfinite(arr).all() or not np.array_equal(
            arr.view(np.uint64), arr.astype("<f4").astype("<f8").view(np.uint64)
        ):
            raise ValueError("runtime scalar not exact FP32")
        dst = a.output_dir / (name + ".npy")
        with dst.open("xb") as f:
            np.save(f, arr, allow_pickle=False)
        files[name] = {"path": dst.name, "sha256": sha(dst), "source_dtype": 6}
    gates = (
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
    out = {
        "scope": "native_stage3_w_reference_inputs_v1",
        "grid": {"m": 80, "n": 40, "nz": 64, "packed_N": 1080491, "W_count": 215865},
        "profile": {k: True for k in gates},
        "files": files,
        "constants": constants,
        "W_inverse_weight_extrema": {"min": metric["min"], "max": metric["max"]},
        "endpoint_proofs": endpoint_proofs,
        "origins": {
            "packet_sha256": sha(a.packet),
            "build_receipt_sha256": sha(a.build_receipt),
            "source_revision": expected_source,
            "result_sha256": sha(a.result),
            "old_packet_sha256": sha(a.old_packet),
            "runtime_packet_sha256": sha(a.runtime_packet),
            "old_new_raw_count_bitwise_identical": 16,
        },
        "assumptions": [
            "native binary64 multiply then add and Q copy semantics match NumPy validation",
            "all fixed context flags refer only to the observed guarded subset",
            "captured B,K and fixed C are exact binary target inputs; prior formation excluded",
            "EOS and mass RDNW same bits; native W runtime getter operands from same accepted pair",
            "all maps exactly unit, dry CQW present and zero, H identity; no unsupported branch",
        ],
        "validation_scope": "Native Q digests are matching checks only. MPFR independently forms exact-real Q(B+aK).",
    }
    with (a.output_dir / "manifest.json").open("x") as f:
        json.dump(out, f, indent=2)
        f.write("\n")
    print(
        json.dumps(
            {
                "status": "normalized",
                "manifest_sha256": sha(a.output_dir / "manifest.json"),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
