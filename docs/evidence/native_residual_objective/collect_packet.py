#!/usr/bin/env python3
"""Collect the frozen residual-objective packet after all eight prefixes complete."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import tempfile
import time
from pathlib import Path
from typing import Any

import numpy as np

E_DEFAULT = Path(__file__).resolve().parent
W = Path("/private/tmp/native-residual-objective-20261021")
RUNNER = W / "external/libtorch_wrf/sdirk3/tests/native_residual_prefix.py"
PROTOCOL_SHA = "b65b2b01bbac7d00ecf18e71e4492701581c1b7059b6db8ac23fabf68f94acfb"
COST_SHA = "b15f647b90967af1c95abd7915d4b496a60d6bbc7e793147c783f2040c143d89"
OLD_PACKET_SHA = "747f8c8347f3659340c691016b5ea5c25e686e86162abbeadb9c37a503d6bd1e"
OLD_MANIFEST_SHA = "79eba3dc569f29b77c1649e689cc5405e1a02c2b4a277d16679899a79b50fed1"
EXE_SHA = "fd8ba93955a7f45fef10eef7646a5e1554cb1b0ba3c8718514f97c1ad346e744"
N = 24531
ENDS = (256, 224, 192, 160, 128, 96, 64, 32)
OLD_KEYS = ("case_labels", "old_eta_f32", "new_prefix_ends_i32", "new_lambda_out_f32")


def _sha(path: Path) -> str:
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def _json(path: Path) -> dict[str, Any]:
    return json.loads(Path(path).read_text())


def _asset(path: Path) -> dict[str, Any]:
    p = Path(path)
    return {"path": str(p), "bytes": p.stat().st_size, "sha256": _sha(p)}


def _load_runner():
    spec = importlib.util.spec_from_file_location(
        "native_residual_prefix_for_packet", RUNNER
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import frozen runner: {RUNNER}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _old_extra(frozen: dict[str, Any]) -> dict[str, np.ndarray]:
    """Read only the pinned old eta, labels, prefix map, and final two-time g0."""
    h = frozen["helper"]
    p = frozen["protocol"]
    packet = Path(p["old_packet"]["path"])
    manifest_path = Path(p["old_manifest"]["path"])
    manifest = _json(manifest_path)
    out: dict[str, np.ndarray] = {}
    with np.load(packet, allow_pickle=False) as z:
        for key in OLD_KEYS:
            h.need(key in z, f"old packet missing {key}")
            a = np.ascontiguousarray(z[key])
            pin = manifest.get("arrays", {}).get(key, {})
            h.need(
                pin.get("shape") == list(a.shape)
                and pin.get("dtype") == a.dtype.str
                and pin.get("sha256") == hashlib.sha256(a.tobytes()).hexdigest(),
                f"old packet array pin mismatch: {key}",
            )
            out[key] = a.copy()
    h.need(
        out["case_labels"].astype(str).tolist()
        == ["baseline", "replay", "plus_0p01", "minus_0p01", "plus_0p03", "minus_0p03"]
        and out["old_eta_f32"].shape == (6,)
        and out["new_prefix_ends_i32"].tolist() == [128, 96, 64, 32]
        and out["new_lambda_out_f32"].shape == (4, N),
        "old packet chain metadata mismatch",
    )
    h.need(
        _sha(packet) == OLD_PACKET_SHA and _sha(manifest_path) == OLD_MANIFEST_SHA,
        "old packet or manifest changed",
    )
    out["old_two_time_g0_f32"] = out["new_lambda_out_f32"][-1].copy()
    return out


def _incoming_path(n: int, dirs: dict[int, Path], frozen: dict[str, Any]) -> Path:
    if n == 256:
        return Path(frozen["seed"])
    if n == 128:
        return dirs[128] / "incoming_lambda128_residual_f32.bin"
    return dirs[n + 32] / "terminal.lambda_out_f32.bin"


def _verify_case(
    n: int,
    frozen: dict[str, Any],
    dirs: dict[int, Path],
    receipts: dict[int, dict[str, Any]],
) -> dict[str, Any]:
    h = frozen["helper"]
    d = dirs[n]
    r = receipts[n]
    a = r.get("actual", {})
    label = f"residual_prefix_{n:03d}"
    rp = d / "run_receipt.json"
    h.need(
        r.get("schema") == "native_residual_prefix_receipt_v1"
        and r.get("label") == label
        and r.get("primary_label") == "residual"
        and r.get("objective_kind") == "residual"
        and r.get("status") == "completed"
        and r.get("protocol_sha256") == frozen["psha"]
        and r.get("launch_binding_sha256") == frozen["bsha"]
        and r.get("runner_sha256") == frozen["binding"]["runner"]["sha256"]
        and r.get("reused_helper_sha256") == frozen["helper_sha"]
        and r.get("cost_helper_sha256") == frozen["cost_sha"]
        and r.get("source_files") == frozen["binding"]["source_files"]
        and r.get("executable") == frozen["binding"]["executable"]
        and r.get("input_sha256") == frozen["input_pins"]["wrfinput_d01"]
        and r.get("expected_steps") == n
        and r.get("expected_implicit_stages") == 3 * n
        and r.get("expected_h_seconds") == 0.9375
        and r.get("new_model_timestep") is False,
        f"{label}: receipt/source/build/input/h/3N pin mismatch",
    )
    nml = d / "namelist.input"
    h.need(
        nml.is_file()
        and nml.read_bytes() == h.prefix_namelist(frozen["source_namelist"], n).encode()
        and _sha(nml) == r.get("namelist_sha256"),
        f"{label}: namelist mismatch",
    )
    for name in ("wrfinput_d01", "diagnostics.txt"):
        pin = frozen["input_pins"][name]
        h.need(
            (d / name).is_file() and _sha(d / name) == pin,
            f"{label}: copied input mismatch: {name}",
        )

    incoming = _incoming_path(n, dirs, frozen)
    ref = frozen["reference"]
    bound = {x.get("role"): x for x in r.get("bound_incoming_files", [])}
    env = r.get("environment", {})
    h.need(
        env.get("WRF_SDIRK3_NATIVE_TERMINAL_LAMBDA32_IN") == str(incoming)
        and Path(bound.get("old_full_baseline_reference", {}).get("path", "")).resolve()
        == ref.resolve()
        and bound.get("old_full_baseline_reference", {}).get("sha256") == _sha(ref)
        and bound.get("incoming_residual_cotangent_f32", {}).get("path")
        == str(incoming)
        and bound.get("incoming_residual_cotangent_f32", {}).get("sha256")
        == _sha(incoming),
        f"{label}: bound Fortran inputs differ from the frozen cotangent chain",
    )
    samples = (
        _json(d / "rss_samples.json") if (d / "rss_samples.json").is_file() else []
    )
    peak = max((x["rss_bytes"] for x in samples), default=0)
    target = frozen["protocol"]["target"]
    h.need(
        samples
        and peak <= target["rss_cap_bytes"]
        and a.get("elapsed_seconds", target["wall_cap_seconds"] + 1)
        <= target["wall_cap_seconds"]
        and a.get("runner_stop_reason") is None
        and a.get("return_code") == 0
        and a.get("native_rss_monitor_has_samples") is True
        and a.get("peak_sampled_native_rss_bytes") == peak,
        f"{label}: RSS/wall/return-code gate failed",
    )
    log = (
        (d / "rsl.error.0000").read_text(errors="replace")
        if (d / "rsl.error.0000").exists()
        else ""
    )
    # Reuse the existing cross-runner validator for 3N, history, finite/physical,
    # checkpoint-prefix, Fortran input/output, and FP64-precast-to-FP32 checks.
    checked = h.collect_result(
        d,
        n,
        ref,
        incoming,
        a["return_code"],
        a["runner_stop_reason"],
        time.monotonic() - float(a["elapsed_seconds"]),
        samples,
        log,
    )
    h.need(
        "validation_error" not in checked
        and checked.get("output_hashes") == a.get("output_hashes"),
        f"{label}: rerun of frozen read-only output validator failed",
    )
    result = a["terminal_result"]
    prep = a["terminal_prepare"]
    endpoint = a["terminal_endpoint"]
    if n > 32:
        h.need(
            result.get("directional_local_precast_f64") is None
            and result.get("directional_vjp_f32_chain") is None,
            f"{label}: partial-window dot product is not null",
        )
    else:
        h.need(
            result.get("directional_local_precast_f64") is not None
            and result.get("directional_vjp_f32_chain") is not None,
            "final full-chain prefix dot product is missing",
        )
    if n == 32:
        h.need(
            prep.get("warm_steps") == 0
            and result.get("initial_control_seed_applied") is False
            and not (d / "terminal.suffix_seed_f64.bin").exists()
            and endpoint.get("suffix_seed_bitwise_match") is True,
            "warm0 prefix32 unexpectedly used a suffix seed",
        )
    if n == 128:
        inj_path = d / "midpoint_injection.json"
        inj = _json(inj_path)
        cp160 = dirs[160] / "terminal.checkpoints_f64.bin"
        with cp160.open("rb") as f:
            f.seek(128 * N * 8)
            state128_sha = hashlib.sha256(f.read(N * 8)).hexdigest()
        extras = {x.get("role"): x for x in r.get("bound_incoming_files", [])}
        h.need(
            inj.get("accepted_step") == 128
            and inj.get("accepted_time_s") == 120
            and inj.get("new_lambda_sha256") == _sha(incoming)
            and inj.get("midpoint_cotangent_sha256") == _sha(frozen["midpoint"])
            and inj.get("baseline_Z128_checkpoint_sha256") == state128_sha
            and extras.get("midpoint_cotangent_f64", {}).get("sha256")
            == _sha(frozen["midpoint"])
            and extras.get("midpoint_injection_receipt", {}).get("sha256")
            == _sha(inj_path),
            "prefix128 injection/state/input receipt binding mismatch",
        )

    return {
        "n": n,
        "label": label,
        "dir": d,
        "receipt_path": rp,
        "receipt": r,
        "incoming": incoming,
        "output_hashes": a["output_hashes"],
    }


def collect(e: Path, frozen: dict[str, Any]) -> dict[str, Any]:
    h = frozen["helper"]
    target = e / "packet"
    h.need(
        frozen["psha"] == PROTOCOL_SHA
        and frozen["cost_sha"] == COST_SHA
        and frozen["binding"]["executable"]["sha256"] == EXE_SHA
        and len(frozen["binding"]["source_files"]) == 10
        and not target.exists(),
        "packet frozen pins fail or packet directory already exists",
    )
    old = {**frozen["arrays"], **_old_extra(frozen)}
    dirs = {n: e / "runs" / f"residual_prefix_{n:03d}" for n in ENDS}
    missing = [n for n, d in dirs.items() if not (d / "run_receipt.json").is_file()]
    h.need(
        not missing,
        f"wait for all eight completed residual receipts; missing {missing}",
    )
    receipts = {n: _json(d / "run_receipt.json") for n, d in dirs.items()}
    h.need(
        all(r.get("status") == "completed" for r in receipts.values()),
        "wait for all eight completed residual prefix cases",
    )
    cases = [_verify_case(n, frozen, dirs, receipts) for n in ENDS]
    comparisons = sum(n + 1 for n in ENDS)
    h.need(comparisons == 1160, "expected 1160 accepted-checkpoint comparisons")
    lin = np.stack(
        [
            np.fromfile(c["dir"] / "terminal.lambda_in_f32.bin", dtype="<f4")
            for c in cases
        ]
    )
    lout = np.stack(
        [
            np.fromfile(c["dir"] / "terminal.lambda_out_f32.bin", dtype="<f4")
            for c in cases
        ]
    )
    pre = np.stack(
        [
            np.fromfile(c["dir"] / "terminal.wrapper_gradient_f64.bin", dtype="<f8")
            for c in cases
        ]
    )
    arrays = {
        "case_labels": old["case_labels"].astype("<U10"),
        "eta_actual_f32": old["old_eta_f32"].astype("<f4"),
        "mid_states_f64": old["old_mid_states_f64"].astype("<f8"),
        "terminal_states_f64": old["old_terminal_states_f64"].astype("<f8"),
        "q_mid_f32": old["q_mid_f32"].astype("<f4"),
        "q_terminal_f32": old["q_terminal_f32"].astype("<f4"),
        "direction_f32": old["direction_v_f32"].astype("<f4"),
        "y_f64": np.asarray(frozen["protocol"]["objective"]["y"], dtype="<f8"),
        "sigma_f64": np.asarray(frozen["protocol"]["objective"]["sigma"], dtype="<f8"),
        "old_terminal_g0_f32": old["old_g0_terminal_f32"].astype("<f4"),
        "old_two_time_g0_f32": old["old_two_time_g0_f32"].astype("<f4"),
        "prefix_ends_i32": np.asarray(ENDS, dtype="<i4"),
        "lambda_in_f32": lin.astype("<f4"),
        "lambda_out_f32": lout.astype("<f4"),
        "local_precast_f64": pre.astype("<f8"),
    }
    h.need(
        arrays["lambda_in_f32"].shape
        == arrays["lambda_out_f32"].shape
        == arrays["local_precast_f64"].shape
        == (8, N),
        "new chain packet array shape mismatch",
    )
    h.need(
        arrays["lambda_in_f32"][0].tobytes() == Path(frozen["seed"]).read_bytes()
        and arrays["lambda_in_f32"][4].tobytes()
        == (dirs[128] / "incoming_lambda128_residual_f32.bin").read_bytes(),
        "packet row0/row4 does not preserve terminal seed/midpoint injection",
    )

    with tempfile.TemporaryDirectory(prefix=".packet_collect_", dir=e) as temp_name:
        temp = Path(temp_name)
        rdir = temp / "receipts"
        rdir.mkdir()
        receipt_pins = []
        for c in cases:
            src = c["receipt_path"]
            dst = rdir / (c["label"] + ".json")
            shutil.copyfile(src, dst)
            h.need(_sha(src) == _sha(dst), f"receipt copy mismatch: {src}")
            receipt_pins.append(
                {
                    "label": c["label"],
                    "source_path": str(src),
                    "receipt_sha256": _sha(src),
                    "receipt_bytes": src.stat().st_size,
                    "path": str(target / "receipts" / dst.name),
                }
            )
        npz = temp / "native_residual_objective.npz"
        with npz.open("wb") as f:
            np.savez_compressed(f, **arrays)
        info = {
            k: {
                "shape": list(a.shape),
                "dtype": a.dtype.str,
                "sha256": hashlib.sha256(np.ascontiguousarray(a).tobytes()).hexdigest(),
            }
            for k, a in arrays.items()
        }
        with np.load(npz, allow_pickle=False) as check:
            for name, pin in info.items():
                a = np.ascontiguousarray(check[name])
                h.need(
                    list(a.shape) == pin["shape"]
                    and a.dtype.str == pin["dtype"]
                    and hashlib.sha256(a.tobytes()).hexdigest() == pin["sha256"],
                    f"packet array self-check failed: {name}",
                )
        p = frozen["protocol"]
        b = frozen["binding"]
        old_manifest = Path(p["old_manifest"]["path"])
        old_packet = Path(p["old_packet"]["path"])
        base_receipt = Path(p["old_base_case"]) / "run_receipt.json"
        obj_receipt = Path(frozen["objective_receipt"])
        raw = []
        for c in cases:
            outputs = c["output_hashes"]
            receipt = c["receipt"]
            actual = receipt["actual"]
            raw.append(
                {
                    "label": c["label"],
                    "steps": c["n"],
                    "input_sha256": receipt["input_sha256"],
                    "namelist_sha256": receipt["namelist_sha256"],
                    "receipt_sha256": _sha(c["receipt_path"]),
                    "elapsed_seconds": actual["elapsed_seconds"],
                    "peak_sampled_native_rss_bytes": actual[
                        "peak_sampled_native_rss_bytes"
                    ],
                    "expected_implicit_stages": 3 * c["n"],
                    "h_seconds": 0.9375,
                    "checkpoints_f64": {
                        "path": str(c["dir"] / "terminal.checkpoints_f64.bin"),
                        **outputs["terminal.checkpoints_f64.bin"],
                    },
                    "lambda_in_f32": {
                        "path": str(c["dir"] / "terminal.lambda_in_f32.bin"),
                        **outputs["terminal.lambda_in_f32.bin"],
                    },
                    "lambda_out_f32": {
                        "path": str(c["dir"] / "terminal.lambda_out_f32.bin"),
                        **outputs["terminal.lambda_out_f32.bin"],
                    },
                    "wrapper_gradient_f64": {
                        "path": str(c["dir"] / "terminal.wrapper_gradient_f64.bin"),
                        **outputs["terminal.wrapper_gradient_f64.bin"],
                    },
                    "incoming_path": str(c["incoming"]),
                }
            )
        manifest = {
            "schema": "native_residual_objective_portable_packet_v1",
            "protocol_sha256": frozen["psha"],
            "helper_sha256": frozen["cost_sha"],
            "npz_path": str(target / npz.name),
            "npz_bytes": npz.stat().st_size,
            "npz_sha256": _sha(npz),
            "arrays": info,
            "receipt_pins": receipt_pins,
            "source_files": b["source_files"],
            "executable_sha256": b["executable"]["sha256"],
            "build_receipt": b["build_receipt"],
            "input_pins": b["input_pins"],
            "reused_runner_helper_sha256": frozen["helper_sha"],
            "source_base": p["source_base"],
            "old_provenance": {
                "old_packet": {**p["old_packet"], "sha256": _sha(old_packet)},
                "old_manifest": {**p["old_manifest"], "sha256": _sha(old_manifest)},
                "reference_checkpoints": {
                    "path": str(frozen["reference"]),
                    "bytes": frozen["reference"].stat().st_size,
                    "sha256": _sha(frozen["reference"]),
                },
                "baseline_receipt": _asset(base_receipt),
                "baseline_objective_receipt": _asset(obj_receipt),
                "old_two_time_g0_array": "new_lambda_out_f32[-1]",
            },
            "run_order": list(ENDS),
            "checkpoint_comparisons": comparisons,
            "all_prefix_checkpoints_byte_equal_baseline": True,
            "lambda_chain": {
                "row0_alphaN_qN_seed_exact": True,
                "row4_once_rounded_midpoint_injection_exact": True,
                "all_other_inputs_equal_previous_f32_output": True,
                "all_fortran_inputs_equal_bound_files": True,
                "all_outputs_equal_cast32_local_precast64": True,
                "partial_dot_fields_null_for_steps_over_32": True,
                "prefix32_warm0_no_suffix_seed": True,
            },
            "raw_case_artifacts": raw,
            "scope": "Old six primal states reuse the pinned portable packet; old full ledgers are not re-parsed. New eight raw case outputs and receipts are verified from disk. Objective formulas are not duplicated.",
        }
        (temp / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        )
        os.replace(temp, target)
    return {
        "packet_dir": str(target),
        "npz_path": str(target / "native_residual_objective.npz"),
        "npz_bytes": (target / "native_residual_objective.npz").stat().st_size,
        "npz_sha256": _sha(target / "native_residual_objective.npz"),
        "manifest_path": str(target / "manifest.json"),
        "manifest_sha256": _sha(target / "manifest.json"),
        "receipts_copied": 8,
        "array_names": list(arrays),
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--evidence-root", type=Path, default=E_DEFAULT)
    args = ap.parse_args()
    runner = _load_runner()
    frozen = runner.load_frozen(args.evidence_root.resolve())
    print(
        json.dumps(
            collect(args.evidence_root.resolve(), frozen), indent=2, sort_keys=True
        ),
        flush=True,
    )


if __name__ == "__main__":
    main()
