#!/usr/bin/env python3
"""Bounded saved-state N14/K12 diagnostic; never an optimizer result."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
import time
from pathlib import Path
from zipfile import ZipFile

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_SHA256 = "642654120277175bc3f32902c3a5982cf348664d9591c40b5178ad0303710121"
RUNNER_SHA256 = "81009bde9076e26f067b65547db9d2a41b7729101bf4709e8ec564237a15fd7c"
CPP_SHA256 = "d6e9505a994ce765ce58aa51c8af33ebc49fbf95ebe8b60a48c5ca0e42146787"
SOLVER_SHA256 = "7e46446b1bbdbc9f86527c786144a5c4b2eadc9bfc08876ec254b0db7d62d205"
AUTOGRAD_SHA256 = "7015fbb99dc425d121936caafca9d444b1ee3ab16f3d981fe345a908aef7a3ef"
KRYLOV_SHA256 = "948a872743205df05adeea2a52d3eae7666dd0ecad3437b752316191668d199e"
FILE_NAMES = {
    "coarse_descriptor": "descriptor_8x6x4.csv",
    "fine_descriptor": "descriptor_16x12x8.csv",
    "observations": "fixed_physical_observations.txt",
    "state": "initial_16x12x8_s60_v40a9c275cfbb_adj.txt",
    "baseline": "result_16x12x8_s60_v40a9c275cfbb_adj.csv",
    "Bf": "Bf.npy",
}
CONFIG_EXPECTED = {
    "kdamp_config": 0.0,
    "implicit_divergence": 0.0,
    "omega_w_blend_config": 1.0,
    "do_curvature_config": 1.0,
    "effective_wrf_omega_ww_cp": 1.0,
    "advection_order_config": 2.0,
    "non_hydrostatic_config": 1.0,
    "map_input_max_deviation": 0.0,
}
FORWARD_KEYS = ("initial_state", "checkpoint_150", "checkpoint_300",
                "predicted_150", "predicted_300")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_runner():
    path = ROOT / "tools/test_fine_wave_inverse.py"
    if sha256(path) != RUNNER_SHA256:
        raise RuntimeError("source helper hash differs from pinned runner 34043a2")
    spec = importlib.util.spec_from_file_location("pr281_exact_wave_runner", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load pinned inverse helper")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def load_vector(path: Path) -> np.ndarray:
    parts = path.read_text().split()
    values = np.asarray([float(x) for x in parts[1:]], dtype=np.float64)
    if len(parts) < 1 or values.size != int(parts[0]):
        raise RuntimeError(f"invalid state vector file: {path}")
    return values


def run_native(exe: Path, grid: tuple[int, int, int], output: Path,
               args: list[str], log_dir: Path, label: str) -> dict:
    command = [str(exe), *(str(n) for n in grid), str(output), *args]
    started = time.monotonic()
    done = subprocess.run(command, capture_output=True, text=True, check=False)
    record = {"label": label, "command": command,
              "return_code": done.returncode,
              "elapsed_seconds": time.monotonic() - started,
              "stdout": done.stdout, "stderr": done.stderr,
              "output_path": str(output),
              "output_sha256": sha256(output) if output.is_file() else None}
    log_dir.mkdir(parents=True, exist_ok=True)
    (log_dir / f"{label}.stdout.log").write_text(done.stdout)
    (log_dir / f"{label}.stderr.log").write_text(done.stderr)
    return record


def prepare(fixture: Path, fixture_zip: Path) -> tuple[object, dict, dict]:
    if not fixture_zip.is_file() or sha256(fixture_zip) != FIXTURE_SHA256:
        raise RuntimeError("outer saved-state fixture ZIP hash mismatch")
    files = {key: fixture / name for key, name in FILE_NAMES.items()}
    manifest_path = fixture / "manifest.json"
    with ZipFile(fixture_zip) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("saved-state fixture ZIP CRC check failed")
        for path in [manifest_path, *files.values()]:
            member = path.name
            if member not in archive.namelist() or hashlib.sha256(archive.read(member)).hexdigest() != sha256(path):
                raise RuntimeError(f"unpacked fixture bytes differ from pinned ZIP member: {member}")
    manifest = json.loads(manifest_path.read_text())
    if (manifest.get("schema") != "pr281-saved-v40a9c2-k12-fixture-v1" or
            manifest.get("source_run_id") != 37863920808 or
            manifest.get("source_artifact_zip_sha256") !=
            "38b6963e4626f77e4af8de9e643a4a95f26a621abaca2b15dc9e3c3d8836fcbe"):
        raise RuntimeError("fixture manifest provenance mismatch")
    for key, path in files.items():
        expected = manifest["files"][path.name]["sha256"]
        if not path.is_file() or sha256(path) != expected:
            raise RuntimeError(f"fixture file hash mismatch: {key}")

    source_pins = {
        "tools/test_fine_wave_inverse.py": RUNNER_SHA256,
        "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp": CPP_SHA256,
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp": SOLVER_SHA256,
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_autograd_utils.h": AUTOGRAD_SHA256,
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_krylov_metrics.h": KRYLOV_SHA256,
    }
    for relative, expected in source_pins.items():
        if sha256(ROOT / relative) != expected:
            raise RuntimeError(f"probe source differs from the pinned Linux production build: {relative}")

    mod = load_runner()
    coarse = mod.reference.build_case(8, 4, native_csv=files["coarse_descriptor"])
    fine = mod.reference.build_case(16, 8, native_csv=files["fine_descriptor"])
    _, basis = mod.profile_basis(coarse, fine)
    fresh_Bf = np.column_stack([mod.pack_source_mode(q, (16, 12, 8)) for q in basis])
    saved_Bf = np.load(files["Bf"], allow_pickle=False)
    if (fresh_Bf.shape != (8480, 4) or saved_Bf.shape != fresh_Bf.shape or
            not np.isfinite(fresh_Bf).all() or not np.isfinite(saved_Bf).all() or
            hashlib.sha256(saved_Bf.tobytes()).hexdigest() != manifest["Bf"]["array_sha256"]):
        raise RuntimeError("pinned saved Bf shape/hash mismatch")
    Bf = saved_Bf
    basis_delta = fresh_Bf-Bf
    basis_column_delta = {"l2": np.linalg.norm(basis_delta, axis=0).tolist(), "max_abs": np.max(np.abs(basis_delta), axis=0).tolist()}

    meta, arrays, scalars, text_meta = mod.read_payload(files["baseline"])
    fine_meta, fine_arrays, fine_scalars, fine_text = mod.read_payload(files["fine_descriptor"])
    state = load_vector(files["state"])
    if (not np.array_equal(state, arrays["initial_state"]) or
            not np.isfinite(state).all() or
            meta.get("physical_wave_newton_tol") != float(np.float32(1e-14)) or
            meta.get("physical_wave_krylov_tol") != float(np.float32(1e-10)) or
            meta.get("trajectory_steps") != 60.0 or meta.get("trajectory_dt_fp32") != 5.0 or
            meta.get("forward_only") != 0.0 or meta.get("pullback_requested") != 1.0 or
            text_meta.get("checkpoint_precision") != "retained_fp64_trajectory" or
            text_meta.get("physical_observation_domain_guard") != "passed"):
        raise RuntimeError("baseline is not the pinned same-state N14/K10 full VJP")
    if not all(np.isfinite(a).all() for a in arrays.values()):
        raise RuntimeError("baseline arrays include nonfinite values")
    if not all(np.isfinite(float(v)) for v in scalars.values()):
        raise RuntimeError("baseline scalars include nonfinite values")
    observation_lines = files["observations"].read_text().splitlines()
    if (len(observation_lines) != 107 or
            observation_lines[0] != "PHYSICAL_WAVE_OBSERVATIONS_V1" or
            observation_lines[1] != "105"):
        raise RuntimeError("fixed observation file has an unexpected schema")
    observation_rows = np.asarray([[float(x) for x in line.split()]
                                   for line in observation_lines[2:]], dtype=np.float64)
    if (observation_rows.shape != (105, 5) or
            not np.array_equal(observation_rows[:, 3], arrays["observed_150"]) or
            not np.array_equal(observation_rows[:, 4], arrays["observed_300"]) or
            meta.get("physical_observation_sigma_m_s") != 3.0e-4):
        raise RuntimeError("baseline observations or sigma differ from frozen fixture inputs")

    projected = Bf.T @ arrays["initial_pullback"]
    gamma_n = Bf.shape[0] * np.finfo(float).eps / (1.0 - Bf.shape[0] * np.finfo(float).eps)
    bound = gamma_n * (np.abs(Bf).T @ np.abs(arrays["initial_pullback"]))
    reported = np.asarray(manifest["baseline"]["projected_gradient"], dtype=np.float64)
    if not np.all(np.abs(projected - reported) <= bound):
        raise RuntimeError("baseline projected gradient fails gamma_n projection guard")
    fresh_projected = fresh_Bf.T @ arrays["initial_pullback"]

    fine_descriptor = {"meta": fine_meta, "arrays": fine_arrays, "scalars": fine_scalars,
                       "text_meta": fine_text, "column": fine, "base": fine_arrays["base"]}
    mod.check_initial_admissibility((16, 12, 8), state, fine_descriptor)
    report = {
        "schema": "pr281-saved-state-n14-k12-precision-diagnostic-v1",
        "status": "preflight_passed_zero_native",
        "diagnostic_only": True,
        "optimizer_return_or_acceptance_claim": False,
        "source": {"head": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                       capture_output=True, text=True, check=True).stdout.strip(),
                   "tree": subprocess.run(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT,
                       capture_output=True, text=True, check=True).stdout.strip(),
                   "source_file_sha256": source_pins},
        "fixture": {"manifest_sha256": sha256(manifest_path),
                    "fixture_zip_sha256": sha256(fixture_zip),
                    "baseline_sha256": sha256(files["baseline"]),
                    "state_file_sha256": sha256(files["state"]),
                    "state_array_sha256": mod.digest(state),
                    "observation_sha256": sha256(files["observations"]),
                    "Bf_file_sha256": sha256(files["Bf"]),
                    "Bf_array_sha256": hashlib.sha256(Bf.tobytes()).hexdigest(),
                    "fresh_Bf_array_sha256": hashlib.sha256(fresh_Bf.tobytes()).hexdigest()},
        "basis_reconstruction_comparison": {"per_column_delta": basis_column_delta,
                    "baseline_projected_gradient_fresh_minus_saved": (fresh_projected-projected).tolist(),
                    "interpretation": "Cross-platform eigenbasis reconstruction difference; gamma_n bounds matrix-product rounding for a fixed basis, not eigensolver basis changes."},
        "baseline": {"objective": float(scalars["objective_physical_w"]),
                     "projected_gradient": projected.tolist(),
                     "projected_gradient_norm": float(np.linalg.norm(projected)),
                     "effective_krylov_tolerance": meta["physical_wave_krylov_tol"],
                     "observation_sigma_m_s": meta["physical_observation_sigma_m_s"],
                     "input_state_exactly_matches_saved_file": True,
                     "projection_gamma_n": gamma_n,
                     "projection_componentwise_bound": bound.tolist(),
                     "projection_guard_passed": True},
        "planned_native_calls": [
            {"label": "descriptor_8x6x4", "type": "descriptor-only"},
            {"label": "descriptor_16x12x8", "type": "descriptor-only"},
            {"label": "same_saved_state_N14_K12_full_vjp", "steps": 60, "dt": 5,
             "newton_tol": "1e-14", "krylov_tol": "1e-12",
             "forward_only": False, "state_sha256": sha256(files["state"])},
        ],
        "actual_native_calls": 0,
    }
    return mod, {"files": files, "manifest": manifest, "Bf": Bf, "state": state,
                 "baseline_meta": meta, "baseline_arrays": arrays,
                 "baseline_scalars": scalars, "baseline_text": text_meta,
                 "baseline_projected_gradient": projected, "fresh_Bf": fresh_Bf,
                 "fine_descriptor": fine_descriptor}, report


def run(fixture: Path, fixture_zip: Path, exe: Path, outdir: Path, dry: bool) -> dict:
    mod, context, report = prepare(fixture, fixture_zip)
    outdir.mkdir(parents=True, exist_ok=True)
    if dry:
        (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        return report
    if not exe.is_file():
        raise RuntimeError(f"native executable not found: {exe}")
    report["source"]["executable_path"] = str(exe)
    report["source"]["executable_sha256"] = sha256(exe)
    cache = exe.parent / "CMakeCache.txt"
    cache_values = {}
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line and not line.startswith("#") and not line.startswith("//") and "=" in line:
                key, value = line.split("=", 1)
                cache_values[key.split(":", 1)[0]] = value
    report["source"]["build_fingerprint"] = {
        "cmake_cache_sha256": sha256(cache) if cache.is_file() else None,
        "cmake_build_type": cache_values.get("CMAKE_BUILD_TYPE"),
        "cmake_cxx_compiler": cache_values.get("CMAKE_CXX_COMPILER"),
        "torch_prefix": cache_values.get("CMAKE_PREFIX_PATH"),
        "wrf_sdirk3_torch_abi": cache_values.get("WRF_SDIRK3_TORCH_ABI"),
    }
    files = context["files"]
    fine_desc_payload = mod.read_payload(files["fine_descriptor"])
    coarse_desc_payload = mod.read_payload(files["coarse_descriptor"])
    report["status"] = "running_diagnostic_only"
    report["actual_native_calls"] = 0
    report["native_call_records"] = []
    (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    logs = outdir / "logs"
    for label, grid, path, reference_payload in (
        ("descriptor_8x6x4", (8, 6, 4), files["coarse_descriptor"], coarse_desc_payload),
        ("descriptor_16x12x8", (16, 12, 8), files["fine_descriptor"], fine_desc_payload),
    ):
        output = outdir / "descriptors" / path.name
        output.parent.mkdir(parents=True, exist_ok=True)
        call = run_native(exe, grid, output, ["--descriptor-only"], logs, label)
        report["native_call_records"].append(call)
        report["actual_native_calls"] = len(report["native_call_records"])
        (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        if call["return_code"] != 0 or not output.is_file():
            raise RuntimeError(f"descriptor call failed: {label}; no retry")
        meta, arrays, scalars, _ = mod.read_payload(output)
        ref_meta, ref_arrays, ref_scalars, _ = reference_payload
        if (not all(k in arrays and np.array_equal(arrays[k], ref_arrays[k]) for k in ref_arrays) or
                not all(meta.get(k) == v and ref_meta.get(k) == v for k, v in CONFIG_EXPECTED.items()) or
                scalars != ref_scalars):
            raise RuntimeError(f"descriptor reproduction failed: {label}; no VJP call")

    output = outdir / "results" / "same_saved_state_N14_K12_full_vjp.csv"
    output.parent.mkdir(parents=True, exist_ok=True)
    call = run_native(exe, (16, 12, 8), output,
        ["--physical-wave-inverse", str(files["state"]), str(files["observations"]),
         "60", "5", "--newton-tol", "1e-14", "--krylov-tol", "1e-12"], logs,
        "same_saved_state_N14_K12_full_vjp")
    report["native_call_records"].append(call)
    report["actual_native_calls"] = len(report["native_call_records"])
    (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if call["return_code"] != 0 or not output.is_file():
        raise RuntimeError("K12 full VJP failed; no retry")
    meta, arrays, scalars, text_meta = mod.read_payload(output)
    if (not np.array_equal(arrays.get("initial_state"), context["state"]) or
            meta.get("physical_wave_newton_tol") != float(np.float32(1e-14)) or
            meta.get("physical_wave_krylov_tol") != float(np.float32(1e-12)) or
            meta.get("trajectory_steps") != 60.0 or meta.get("trajectory_dt_fp32") != 5.0 or
            meta.get("forward_only") != 0.0 or meta.get("pullback_requested") != 1.0 or
            meta.get("retains_tape") != 1.0 or "initial_pullback" not in arrays or
            text_meta.get("checkpoint_precision") != "retained_fp64_trajectory" or
            text_meta.get("physical_observation_domain_guard") != "passed"):
        raise RuntimeError("K12 output does not satisfy the same-state full-VJP contract")
    if (not all(np.isfinite(a).all() for a in arrays.values()) or
            not all(np.isfinite(float(v)) for v in scalars.values())):
        raise RuntimeError("K12 output contains nonfinite payload values")
    if (not np.array_equal(arrays["observed_150"], context["baseline_arrays"]["observed_150"]) or
            not np.array_equal(arrays["observed_300"], context["baseline_arrays"]["observed_300"])):
        raise RuntimeError("K12 output changed frozen observations")
    forward_hashes = {key: {
        "N14_K10_sha256": hashlib.sha256(context["baseline_arrays"][key].tobytes()).hexdigest(),
        "N14_K12_sha256": hashlib.sha256(arrays[key].tobytes()).hexdigest(),
        "exact_equal": np.array_equal(context["baseline_arrays"][key], arrays[key]),
    } for key in FORWARD_KEYS}
    same_forward = all(x["exact_equal"] for x in forward_hashes.values())
    k12_gradient = context["Bf"].T @ arrays["initial_pullback"]
    fresh_k10_gradient = context["fresh_Bf"].T @ context["baseline_arrays"]["initial_pullback"]
    fresh_k12_gradient = context["fresh_Bf"].T @ arrays["initial_pullback"]
    report.update({
        "status": "completed_diagnostic_only",
        "K12_full_vjp_contract": {
            "forward_only": meta["forward_only"], "pullback_requested": meta["pullback_requested"],
            "retains_tape": meta["retains_tape"],
            "effective_newton_tolerance": meta["physical_wave_newton_tol"],
            "effective_krylov_tolerance": meta["physical_wave_krylov_tol"],
            "checkpoint_precision": text_meta["checkpoint_precision"],
            "raw_initial_pullback_sha256": hashlib.sha256(arrays["initial_pullback"].tobytes()).hexdigest(),
        },
        "comparison": {
            "forward_fields_identical": same_forward,
            "forward_field_hashes": forward_hashes,
            "forward_identity_interpretation": ("same forward trajectory; measured difference is in the transpose/VJP solve or its arithmetic"
                if same_forward else "forward trajectories differ; this compares combined forward and transpose precision effects"),
            "N14_K10_objective": float(context["baseline_scalars"]["objective_physical_w"]),
            "N14_K12_objective": float(scalars["objective_physical_w"]),
            "objective_delta_K12_minus_K10": float(scalars["objective_physical_w"] - context["baseline_scalars"]["objective_physical_w"]),
            "N14_K10_projected_gradient": context["baseline_projected_gradient"].tolist(),
            "N14_K12_projected_gradient": k12_gradient.tolist(),
            "projected_gradient_delta": (k12_gradient-context["baseline_projected_gradient"]).tolist(),
            "fresh_Bf_projection_deltas_vs_saved_Bf": {
                "K10": (fresh_k10_gradient-context["baseline_projected_gradient"]).tolist(),
                "K12": (fresh_k12_gradient-k12_gradient).tolist()},
            "N14_K10_projected_gradient_norm": float(np.linalg.norm(context["baseline_projected_gradient"])),
            "N14_K12_projected_gradient_norm": float(np.linalg.norm(k12_gradient)),
        },
        "optimizer_return_or_acceptance_claim": False,
    })
    (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-root", type=Path, required=True)
    parser.add_argument("--fixture-zip", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--dry-preflight", action="store_true")
    args = parser.parse_args()
    if not args.dry_preflight and args.exe is None:
        parser.error("--exe is required unless --dry-preflight is selected")
    outdir = args.output_dir.resolve()
    try:
        report = run(args.fixture_root.resolve(), args.fixture_zip.resolve(),
                     args.exe.resolve() if args.exe else Path(), outdir, args.dry_preflight)
    except Exception as exc:
        summary = outdir / "probe_summary.json"
        if summary.is_file():
            partial = json.loads(summary.read_text())
            partial["status"] = "incomplete_diagnostic_only"
            partial["error"] = repr(exc)
            summary.write_text(json.dumps(partial, indent=2, sort_keys=True) + "\n")
        raise
    print(json.dumps({"status": report["status"],
                      "actual_native_calls": report.get("actual_native_calls", 0)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
