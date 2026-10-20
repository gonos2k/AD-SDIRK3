#!/usr/bin/env python3
"""Portable NumPy recomputation for a native terminal-adjoint evidence packet.

This reads the compact NPZ, manifest, and copied JSON receipts only. It does not
launch WRF, open the producing executable, or require raw checkpoint binaries.
Replay identity is verified from receipt-pinned ledger digests; this script does
not independently compare the omitted full checkpoint streams.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any

import numpy as np

N = 24531
CASES = ("baseline", "replay", "plus_0p01", "minus_0p01", "plus_0p03", "minus_0p03")
PREFIXES = (224, 192, 160, 128, 96, 64, 32)
CHAIN = ("baseline",) + tuple(f"baseline_prefix_{n:03d}" for n in PREFIXES)
RECEIPTS = CASES + CHAIN[1:]
BASELINE_SOURCE_SET, P6_SOURCE_SET = "baseline_v5", "fortran_chain_v6"
BASELINE_EXE, P6_EXE = "baseline_v5", "fortran_chain_v6"
BASELINE_PROTOCOL = "b96cbe382b1e992c64e21b2ef3f413cf3aa7ba6b7bfaa31e0a43b91390c9ce45"
P6_PROTOCOL = "9971cb56d28022951dfe00c1b7707532cf53c9cc71ca88983c738812751e2650"
REGISTERED_INPUT_SHA = "e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9"
ARRAYS = {
    "case_order": ("text", (6,)),
    "direction_f32": ("f4", (N,)),
    "terminal_cotangent_f32": ("f4", (N,)),
    "control_base_f64": ("f8", (N,)),
    "seeds_f64": ("f8", (6, N)),
    "terminal_states_f64": ("f8", (6, N)),
    "eta_actual_f32": ("f4", (6,)),
    "chain_inputs_f32": ("f4", (8, N)),
    "chain_outputs_f32": ("f4", (8, N)),
    "chain_local_precast_f64": ("f8", (8, N)),
}


def need(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def array_sha(x: np.ndarray) -> str:
    return digest(np.ascontiguousarray(x).tobytes())


def same(a: np.ndarray, b: np.ndarray) -> bool:
    a, b = np.ascontiguousarray(a), np.ascontiguousarray(b)
    return a.shape == b.shape and a.dtype == b.dtype and a.tobytes() == b.tobytes()


def finite_num(x: float) -> float | None:
    return float(x) if math.isfinite(float(x)) else None


def load_npz(path: Path, manifest: dict[str, Any], manifest_path: Path) -> dict[str, np.ndarray]:
    pin = manifest.get("npz", {})
    packet_dir = manifest_path.resolve().parent
    require_path = pin.get("path")
    need(isinstance(require_path, str) and (packet_dir / require_path).resolve() == path.resolve(),
         "NPZ path differs from packet manifest")
    need(path.is_file() and file_sha(path) == pin.get("sha256"), "NPZ SHA-256 mismatch")
    need(pin.get("bytes") == path.stat().st_size, "NPZ byte count mismatch")
    with np.load(path, allow_pickle=False) as archive:
        need(set(archive.files) == set(ARRAYS), f"NPZ keys mismatch: {sorted(archive.files)}")
        data = {k: np.asarray(archive[k]).copy() for k in archive.files}
    for name, (kind, shape) in ARRAYS.items():
        x = data[name]
        if kind == "text":
            need(x.dtype.kind in "US" and tuple(x.tolist()) == CASES, "NPZ case_order mismatch")
        else:
            need(x.shape == shape and x.dtype.kind == "f" and x.dtype.itemsize == int(kind[1]),
                 f"{name}: expected {kind} {shape}, got {x.dtype} {x.shape}")
            need(np.isfinite(x).all(), f"{name}: non-finite values")
    return data


def verify_receipts(manifest: dict[str, Any], manifest_path: Path) -> dict[str, dict[str, Any]]:
    packet_dir = manifest_path.resolve().parent
    executables = {x["id"]: x for x in manifest.get("executables", [])}
    source_sets = {x["id"]: x["files"] for x in manifest.get("source_sets", [])}
    need(executables and source_sets, "manifest must pin executable and source sets")
    entries = {x["label"]: x for x in manifest.get("receipts", [])}
    need(set(entries) == set(RECEIPTS), f"receipt labels mismatch: {sorted(entries)}")
    verified = {}
    for label in RECEIPTS:
        entry = entries[label]
        is_baseline = label == "baseline"
        source_id = BASELINE_SOURCE_SET if is_baseline else P6_SOURCE_SET
        exe_id = BASELINE_EXE if is_baseline else P6_EXE
        protocol_sha = BASELINE_PROTOCOL if is_baseline else P6_PROTOCOL
        need(entry.get("source_set_id") == source_id and entry.get("executable_id") == exe_id,
             f"wrong producer binding for frozen role {label}")
        path = (packet_dir / entry.get("path", "")).resolve()
        need(path.is_relative_to(packet_dir) and path.is_file() and
             file_sha(path) == entry.get("sha256"), f"receipt pin mismatch: {label}")
        r = json.loads(path.read_text())
        need(r.get("status") == "completed" and r.get("label") == label,
             f"receipt incomplete or mislabeled: {label}")
        need(r.get("protocol_sha256") == protocol_sha,
             f"receipt protocol does not match frozen producer role: {label}")
        need(r.get("input_sha256") == REGISTERED_INPUT_SHA,
             f"registered WRF input hash mismatch: {label}")
        n = entry.get("expected_steps")
        stages = 3 * n if isinstance(n, int) else -1
        actual = r.get("actual", {})
        need(n > 0 and r.get("expected_steps") == n and r.get("expected_implicit_stages") == stages and
             actual.get("step_telemetry_count") == n and
             actual.get("newton_convergence_record_count") == stages and
             entry.get("actual_steps") == n and entry.get("actual_implicit_stages") == stages,
             f"step/stage count mismatch: {label}")
        need(r.get("expected_h_seconds") == 0.9375 and r.get("new_model_timestep") is False,
             f"registered timestep mismatch: {label}")
        exe = executables.get(entry.get("executable_id"), {})
        actual_exe = r.get("executable", {})
        need(exe and (actual_exe.get("sha256"), actual_exe.get("bytes"), actual_exe.get("path")) ==
             (exe.get("sha256"), exe.get("bytes"), exe.get("path")),
             f"receipt executable does not match its producer pin: {label}")
        src = source_sets.get(entry.get("source_set_id"), [])
        receipt_src = r.get("source_files", [])
        need(src and len(src) == len(receipt_src), f"source set missing/mismatched: {label}")
        for want, got in zip(src, receipt_src):
            rel = want.get("repo_relative_path", "")
            need(rel and got.get("path", "").replace("\\", "/").endswith("/" + rel) and
                 want.get("sha256") == got.get("sha256"), f"source pin mismatch: {label} {rel}")
        outputs = actual.get("output_hashes", {})
        checkpoint = outputs.get("terminal.checkpoints_f64.bin", {})
        expected_bytes = (n + 1) * N * 8
        need(checkpoint.get("bytes") == expected_bytes and
             checkpoint.get("sha256") == entry.get("checkpoint_sha256"),
             f"checkpoint receipt pin/count mismatch: {label}")
        verified[label] = {"entry": entry, "receipt": r, "outputs": outputs,
                           "checkpoint_sha256": checkpoint["sha256"],
                           "checkpoint_bytes": expected_bytes}

    for label, n in zip(CHAIN, (256,) + PREFIXES):
        need(entries[label]["expected_steps"] == n, f"wrong horizon for {label}")
    for label in CASES:
        need(entries[label]["expected_steps"] == 256, f"primary horizon is not 256: {label}")
    primary_namelist = verified["baseline"]["receipt"].get("namelist_sha256")
    need(primary_namelist and all(verified[label]["receipt"].get("namelist_sha256") == primary_namelist
                                  for label in CASES),
         "primary case namelist hashes differ from the preserved baseline")
    base, replay = verified["baseline"]["checkpoint_sha256"], verified["replay"]["checkpoint_sha256"]
    ledger = manifest.get("replay_ledger", {})
    need(base == replay and ledger.get("bytewise_equal") is True and
         ledger.get("baseline_sha256") == base and ledger.get("replay_sha256") == replay and
         ledger.get("checkpoint_count") == 257, "replay ledger receipt SHA equality failed")

    for n in PREFIXES:
        label = f"baseline_prefix_{n:03d}"
        r = verified[label]["receipt"]
        prep = r.get("actual", {}).get("terminal_prepare", {})
        end = r.get("actual", {}).get("terminal_endpoint", {})
        result = r.get("actual", {}).get("terminal_result", {})
        need(r.get("primary_label") == "baseline" and r.get("prefix_reexecution") is True and
             prep.get("mode") == "prefix" and prep.get("chain") == "baseline" and prep.get("eta") == 0 and
             prep.get("expected_steps") == n,
             f"prefix role/prepare metadata mismatch: {label}")
        need(end.get("endpoint_reference_exact") is True and
             end.get("reference_checkpoints_expected") == n + 1 and
             end.get("reference_checkpoints_matched") == n + 1 and
             end.get("reference_prefix_fnv1a64") == end.get("actual_prefix_fnv1a64"),
             f"prefix endpoint/reference parity metadata mismatch: {label}")
        need(result.get("mode") == "prefix" and result.get("chain") == "baseline" and
             result.get("expected_steps") == n and result.get("reference_checkpoints_expected") == n + 1 and
             result.get("reference_checkpoints_matched") == n + 1 and
             result.get("endpoint_reference_exact") is True,
             f"prefix result role/reference metadata mismatch: {label}")
        incoming = r.get("bound_incoming_files", [])
        previous = "baseline" if n == 224 else f"baseline_prefix_{n + 32:03d}"
        prev_lambda = verified[previous]["outputs"].get("terminal.lambda_out_f32.bin", {})
        need(len(incoming) == 2 and incoming[0].get("path") == end.get("reference_path") and
             Path(incoming[0].get("path", "")).name == "terminal.checkpoints_f64.bin" and
             incoming[0].get("sha256") == base and incoming[0].get("bytes") == 257 * N * 8 and
             Path(incoming[1].get("path", "")).name == "terminal.lambda_out_f32.bin" and
             incoming[1].get("sha256") == prev_lambda.get("sha256") and
             incoming[1].get("bytes") == prev_lambda.get("bytes"),
             f"prefix incoming reference/cotangent binding mismatch: {label}")
    return verified


def recompute(a: dict[str, np.ndarray]) -> dict[str, Any]:
    d, lam, base = a["direction_f32"], a["terminal_cotangent_f32"], a["control_base_f64"]
    seeds, states, eta = a["seeds_f64"], a["terminal_states_f64"], a["eta_actual_f32"]
    incoming, outgoing, local64 = (a[k] for k in
        ("chain_inputs_f32", "chain_outputs_f32", "chain_local_precast_f64"))
    frozen_eta = np.array([0, 0, .01, -.01, .03, -.03], dtype=np.float32)
    need(same(eta, frozen_eta), "actual float32 eta values differ from frozen six-case set")
    need(same(seeds[0], base) and same(seeds[1], base),
         "zero-eta seeds must byte-copy control base (including signed zero)")
    d64 = d.astype(np.float64)
    for i in range(2, 6):
        need(same(seeds[i], base + float(eta[i]) * d64), f"seed formula mismatch: {CASES[i]}")
    need(same(incoming[0], lam), "chain input 0 differs from objective cotangent")
    for i in range(8):
        if i:
            need(same(incoming[i], outgoing[i - 1]), f"FP32 cotangent chain breaks at block {i}")
        need(same(outgoing[i], local64[i].astype(np.float32)),
             f"FP32 output is not the cast of local pre-cast VJP at block {i}")
    narrowing = []
    for block in range(8):
        delta = outgoing[block].astype(np.float64) - local64[block]
        norm = float(np.linalg.norm(local64[block]))
        narrowing.append({"block": block, "max_abs": float(np.max(np.abs(delta))),
            "rms": float(np.sqrt(np.mean(delta * delta))),
            "relative_l2": finite_num(float(np.linalg.norm(delta) / norm)) if norm else None})

    # Each registered objective uses the same once-rounded FP32 cotangent promoted to FP64.
    J = states @ lam.astype(np.float64)
    dAD = float(np.dot(d64, outgoing[7].astype(np.float64)))
    widths = {}
    j0 = float(J[0])
    lambda_norm = float(np.linalg.norm(lam.astype(np.float64)))
    baseline_state_norm = float(np.linalg.norm(states[0]))
    for tag, p, m in (("0p01", 2, 3), ("0p03", 4, 5)):
        ep, em = float(eta[p]), float(eta[m])
        need(ep > 0 and em < 0 and ep == -em, f"{tag}: actual eta pair is not symmetric")
        jp, jm = float(J[p]), float(J[m])
        fd = (jp - jm) / (ep - em)
        shared = max(abs(fd), abs(dAD))
        rel = abs(fd - dAD) / shared if shared else math.inf
        signal = abs(jp - jm)
        signal_scale = max(abs(jp), abs(jm), lambda_norm * baseline_state_norm)
        floor = 100 * np.finfo(np.float64).eps * signal_scale
        onep, onem = (jp - j0) / ep, (j0 - jm) / (-em)
        one_scale = max(abs(onep), abs(onem))
        gap = abs(onep - onem) / one_scale if one_scale else math.inf
        widths[tag] = {"eta_plus_actual": ep, "eta_minus_actual": em, "J_plus": jp,
            "J_minus": jm, "J0": j0, "central_fd": fd,
            "directional_vjp_chained_f32_promoted": dAD,
            "central_vs_vjp_relative_shared_signal": finite_num(rel),
            "signal_abs_Jplus_minus_Jminus": signal, "signal_floor": floor,
            "signal_above_frozen_floor": bool(signal > floor), "one_sided_plus": onep,
            "one_sided_minus": onem, "one_sided_relative_gap": finite_num(gap),
            "central_agrees_with_vjp_at_frozen_1_percent": bool(rel <= .01)}
    fd0, fd1 = widths["0p01"]["central_fd"], widths["0p03"]["central_fd"]
    scale = max(abs(fd0), abs(fd1), abs(dAD))
    width_gap = abs(fd0 - fd1) / scale if scale else math.inf
    unresolved = all(widths[t]["one_sided_relative_gap"] is not None and
                     widths[t]["one_sided_relative_gap"] > .01 for t in ("0p01", "0p03"))
    return {"schema": "native_terminal_adjoint_offline_recompute_v1", "N": N,
        "case_order": list(CASES), "J_f64": [float(x) for x in J],
        "actual_eta_f32": [float(x) for x in eta],
        "seed_sha256": [array_sha(x) for x in seeds],
        "terminal_state_sha256": [array_sha(x) for x in states],
        "checks": {"zero_eta_bytecopy": True, "perturbed_seed_formula": True,
            "eight_block_fp32_chain": True, "all_outputs_equal_local_precast_cast": True},
        "per_block_local_narrowing": narrowing,
        "directional_vjp_chained_f32_promoted": dAD,
        "scope": {"actual_initial_gradient_chain_computed": True,
            "all_directions_certified": False, "global_exact_fp64_chain_certified": False,
            "local_precast_f64_is_per_block_diagnostic": True,
            "replay_is_state_only_receipt_ledger_check": True},
        "finite_difference": {"formula": "frozen v3 full_fd; actual float32 eta promoted to float64",
            "relative_denominator": "max(abs(dFD),abs(dAD)); no floor",
            "signal_floor_scale": "max(abs(Jplus),abs(Jminus),norm(lambda)*norm(ZN_baseline))",
            "widths": widths, "central_width_agreement_relative_shared_signal": finite_num(width_gap),
            "central_widths_agree_at_frozen_1_percent": bool(width_gap <= .01),
            "one_sided_gap_over_1_percent_at_both_widths": unresolved,
            "classical_directional_derivative_status":
                "unresolved_one_sided_gap_at_both_widths" if unresolved else
                "not_unresolved_by_frozen_one_sided_gap_rule"}}


def validate_packet(manifest: dict[str, Any], arrays: dict[str, np.ndarray],
                    receipts: dict[str, dict[str, Any]]) -> None:
    cases = manifest.get("cases", {})
    need(set(cases) == set(CASES), "manifest case metadata mismatch")
    for i, case in enumerate(CASES):
        item = cases[case]
        need(item.get("receipt_label") == case and
             np.float32(item.get("actual_eta_f32")).view(np.uint32) == arrays["eta_actual_f32"][i].view(np.uint32),
             f"case eta/receipt metadata mismatch: {case}")
        seed_hash, terminal_hash = array_sha(arrays["seeds_f64"][i]), array_sha(arrays["terminal_states_f64"][i])
        need(item.get("seed_sha256") == seed_hash and item.get("terminal_state_sha256") == terminal_hash,
             f"case array hash mismatch: {case}")
        outputs = receipts[case]["outputs"]
        prepare = receipts[case]["receipt"]["actual"].get("terminal_prepare", {})
        need(np.float32(prepare.get("eta")).view(np.uint32) == arrays["eta_actual_f32"][i].view(np.uint32),
             f"actual eta differs from receipt: {case}")
        need(outputs.get("terminal.x0_internal_f64.bin", {}).get("sha256") == seed_hash and
             outputs.get("terminal.terminal_state_f64.bin", {}).get("sha256") == terminal_hash,
             f"seed/final-state receipt hash mismatch: {case}")
        need(outputs.get("terminal.terminal_f32.bin", {}).get("sha256") == array_sha(arrays["terminal_cotangent_f32"]),
             f"terminal cotangent receipt hash mismatch: {case}")

    for i, label in enumerate(CHAIN):
        outputs = receipts[label]["outputs"]
        for array, filename in (("chain_inputs_f32", "terminal.lambda_in_f32.bin"),
                                ("chain_outputs_f32", "terminal.lambda_out_f32.bin"),
                                ("chain_local_precast_f64", "terminal.wrapper_gradient_f64.bin")):
            need(outputs.get(filename, {}).get("sha256") == array_sha(arrays[array][i]),
                 f"chain row {i} is not pinned to {label} receipt output {filename}")


def self_test() -> None:
    d = np.zeros(N, np.float32); d[0] = 1
    lam = np.zeros(N, np.float32); lam[0] = 1
    base = np.zeros(N, np.float64)
    eta = np.array([0, 0, .01, -.01, .03, -.03], np.float32)
    seeds = np.repeat(base[None, :], 6, axis=0)
    for i in range(2, 6): seeds[i] = base + float(eta[i]) * d.astype(np.float64)
    states = np.zeros((6, N), np.float64); states[:, 0] = 4 + 2.5 * eta.astype(np.float64)
    ci = np.zeros((8, N), np.float32); co = np.zeros_like(ci); pre = np.zeros((8, N), np.float64)
    ci[0] = lam
    for i in range(8):
        pre[i, 0] = 2.5; co[i] = pre[i].astype(np.float32)
        if i < 7: ci[i + 1] = co[i]
    a = {"case_order": np.asarray(CASES), "direction_f32": d, "terminal_cotangent_f32": lam,
         "control_base_f64": base, "seeds_f64": seeds, "terminal_states_f64": states,
         "eta_actual_f32": eta, "chain_inputs_f32": ci, "chain_outputs_f32": co,
         "chain_local_precast_f64": pre}
    r = recompute(a)
    need(abs(r["directional_vjp_chained_f32_promoted"] - 2.5) < 1e-15 and
         all(abs(x["central_fd"] - 2.5) < 1e-12 for x in r["finite_difference"]["widths"].values()),
         "synthetic linear control failed")
    broken = {k: v.copy() for k, v in a.items()}; broken["chain_inputs_f32"][4, 1] = 1
    rejected = False
    try: recompute(broken)
    except ValueError: rejected = True
    need(rejected, "synthetic broken-chain negative case was not rejected")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--npz", type=Path)
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test(); print("synthetic recompute checks passed")
        if args.npz is None and args.manifest is None: return
    need(args.npz is not None and args.manifest is not None, "--npz and --manifest are required")
    manifest_path, npz_path = args.manifest.resolve(), args.npz.resolve()
    manifest = json.loads(manifest_path.read_text())
    need(manifest.get("schema") == "native_terminal_adjoint_chain_manifest_v1" and
         manifest.get("N") == N and manifest.get("case_order") == list(CASES), "manifest schema/map mismatch")
    arrays = load_npz(npz_path, manifest, manifest_path)
    receipt_info = verify_receipts(manifest, manifest_path)
    validate_packet(manifest, arrays, receipt_info)
    report = recompute(arrays)
    report["packet"] = {"npz_sha256": file_sha(npz_path), "manifest_sha256": file_sha(manifest_path),
        "receipt_count": len(receipt_info), "all_3N_stage_counts_verified": True,
        "receipt_pinned_replay_ledger_sha256": receipt_info["baseline"]["checkpoint_sha256"],
        "receipt_checkpoint_sha256": {k: v["checkpoint_sha256"] for k, v in receipt_info.items()}}
    rendered = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(rendered)
        print(json.dumps({"output": str(args.output.resolve()), "sha256": file_sha(args.output)}, indent=2))
    else: print(rendered, end="")


if __name__ == "__main__": main()
