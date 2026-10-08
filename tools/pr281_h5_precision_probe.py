#!/usr/bin/env python3
"""Run four diagnostic-only same-point h=5 inverse precision probes."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
RUN_ID = "37726282791"
ARTIFACT_SHA256 = "ffdb55bdc86050e8f6b5f7bedc9e8761fc951529a749688e9ac2116737a7b0fe"
FIXTURE_SHA256 = "a7db3585c51457ba29a9b0af0f93f2817ba198d24c88aaeebaf99a4af73c3045"
PREVIOUS_RUN_ID = "37757922139"
PREVIOUS_ARTIFACT_SHA256 = "3248f9bcb908bdeaeef4662bb45c736a4c6ff3b18ca580ed60faad840a3318c9"
PREVIOUS_REPORT_SHA256 = "4f45bf5bf9430054639f49451ca670d1a26e4cd1c3618a99f55f69cbcf9090ad"
PR_HEAD = "38c05f7542136ed035197f7a0a0335e71b6fda83"
SOURCE_COMMIT = "e5a5fe500dcdcce225700556362ed6a0e9db1835"
SOURCE_TREE = "90dd29e901968e9f7eacf28144581022a1eb4c2b"
EXPECTED_TREE = "90dd29e901968e9f7eacf28144581022a1eb4c2b"
RUNNER_SHA256 = "d2cf625ed2024de8633863bc195dba9fae84639727495fb01f08f0136809d8f4"
CPP_SHA256 = "d6e9505a994ce765ce58aa51c8af33ebc49fbf95ebe8b60a48c5ca0e42146787"
EXPECTED_DESCRIPTORS = {
    "descriptor_8x6x4.csv": "19c38d8f95d08340dd6fd52cef7e452f7ad49291847ea3627e8d780f278b496b",
    "descriptor_16x12x8.csv": "08b8e4060ca341e2bf739d91154a8587e592c902609635c82f723d532dfa8f1f",
}
OBSERVATION_SHA256 = "1c9c447171ce349125af4d2708ec96352db4dd4600f11b412937b169e0a6e523"
POINTS = {
    "last_armijo": {
        "filename": "last_armijo_state.txt",
        "sha256": "e1264fe029f3a207aefb72d87a7a0f2c4b296ccaee2c8b4e0fd739d181c4a3c2",
    },
    "first_iteration9_rejected": {
        "filename": "full_step_rejected_state.txt",
        "sha256": "d2c6ed9a8b42349232d5d70b6d07c5e15873592516859148060c83bf46d1dced",
    },
}
CONFIG_EXPECTED = {
    "kdamp_config": 0.0, "implicit_divergence": 0.0,
    "omega_w_blend_config": 1.0, "do_curvature_config": 1.0,
    "effective_wrf_omega_ww_cp": 1.0, "advection_order_config": 2.0,
    "non_hydrostatic_config": 1.0, "map_input_max_deviation": 0.0,
}
NEW_PRECISIONS = (
    ("krylov_only_N12_K10", "1e-12", "1e-10"),
    ("newton_only_N13_K8", "1e-13", "1e-8"),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_runner_module():
    module_path = ROOT / "tools/test_fine_wave_inverse.py"
    if sha256(module_path) != RUNNER_SHA256:
        raise RuntimeError("frozen inverse runner hash differs from CI source")
    spec = importlib.util.spec_from_file_location("fine_wave_inverse_probe", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load source helper module {module_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def find_unique(root: Path, filename: str) -> Path:
    found = list(root.rglob(filename))
    if len(found) != 1:
        raise RuntimeError(f"expected one {filename} under {root}, found {len(found)}")
    return found[0]


def run_native(exe: Path, nx: int, ny: int, nz: int, out: Path,
               args: list[str], log_dir: Path, label: str) -> dict:
    command = [str(exe), str(nx), str(ny), str(nz), str(out), *args]
    started = time.monotonic()
    done = subprocess.run(command, capture_output=True, text=True, check=False)
    elapsed = time.monotonic() - started
    stdout = log_dir / f"{label}.stdout.log"
    stderr = log_dir / f"{label}.stderr.log"
    stdout.write_text(done.stdout)
    stderr.write_text(done.stderr)
    record = {
        "label": label,
        "command": command,
        "return_code": done.returncode,
        "elapsed_seconds": elapsed,
        "stdout_path": str(stdout),
        "stdout_sha256": sha256(stdout),
        "stderr_path": str(stderr),
        "stderr_sha256": sha256(stderr),
        "output_path": str(out),
        "output_sha256": sha256(out) if out.exists() else None,
    }
    print(json.dumps({k: record[k] for k in
                      ("label", "return_code", "elapsed_seconds", "output_sha256")}),
          flush=True)
    return record


def descriptor_guard(mod, exe: Path, ci_root: Path, outdir: Path,
                     report: dict) -> None:
    descriptor_dir = outdir / "descriptors"
    logs = outdir / "logs"
    descriptor_dir.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    guard = {}
    for name, grid in (("descriptor_8x6x4.csv", (8, 6, 4)),
                       ("descriptor_16x12x8.csv", (16, 12, 8))):
        ci_path = find_unique(ci_root, name)
        if sha256(ci_path) != EXPECTED_DESCRIPTORS[name]:
            raise RuntimeError(f"unexpected CI descriptor bytes: {ci_path}")
        local_path = descriptor_dir / name
        call = run_native(exe, *grid, local_path, ["--descriptor-only"], logs,
                          f"descriptor_{grid[0]}x{grid[1]}x{grid[2]}")
        if call["return_code"] != 0 or not local_path.is_file():
            guard[name] = {"ci_path": str(ci_path), "ci_csv_sha256": sha256(ci_path),
                           "local_path": str(local_path), "passed": False,
                           "native_call": call}
            report["descriptor_guard"] = guard
            write_report(report, outdir)
            raise RuntimeError(f"descriptor native call failed for {name}")
        ci_meta, ci_arrays, ci_scalars, ci_text = mod.read_payload(ci_path)
        local_meta, local_arrays, local_scalars, local_text = mod.read_payload(local_path)
        array_checks = {}
        for key in sorted(ci_arrays.keys() | local_arrays.keys()):
            same_key = key in ci_arrays and key in local_arrays
            same_values = (same_key and ci_arrays[key].shape == local_arrays[key].shape
                           and np.array_equal(ci_arrays[key], local_arrays[key]))
            array_checks[key] = {
                "present_in_both": same_key,
                "ci_shape": list(ci_arrays[key].shape) if key in ci_arrays else None,
                "local_shape": list(local_arrays[key].shape) if key in local_arrays else None,
                "exact_equal": bool(same_values),
                "ci_array_sha256": (hashlib.sha256(ci_arrays[key].tobytes()).hexdigest()
                                    if key in ci_arrays else None),
                "local_array_sha256": (hashlib.sha256(local_arrays[key].tobytes()).hexdigest()
                                       if key in local_arrays else None),
            }
        meta_checks = {key: {
            "expected": expected,
            "ci": ci_meta.get(key), "local": local_meta.get(key),
            "ci_matches_expected": ci_meta.get(key) == expected,
            "local_matches_expected": local_meta.get(key) == expected,
        } for key, expected in CONFIG_EXPECTED.items()}
        scalars_equal = ci_scalars == local_scalars
        arrays_equal = all(v["exact_equal"] for v in array_checks.values())
        metadata_equal = all(v["ci_matches_expected"] and v["local_matches_expected"]
                             and v["ci"] == v["local"] for v in meta_checks.values())
        passed = arrays_equal and metadata_equal and scalars_equal
        guard[name] = {
            "ci_path": str(ci_path), "ci_csv_sha256": sha256(ci_path),
            "local_path": str(local_path), "local_csv_sha256": sha256(local_path),
            "ci_native_compiler": ci_text.get("native_compiler_version"),
            "ci_native_torch": ci_text.get("native_torch_version"),
            "local_native_compiler": local_text.get("native_compiler_version"),
            "local_native_torch": local_text.get("native_torch_version"),
            "arrays": array_checks, "config_meta": meta_checks,
            "scalars_exact_equal": scalars_equal,
            "passed": passed, "native_call": call,
        }
        report["descriptor_guard"] = guard
        write_report(report, outdir)
        if not passed:
            raise RuntimeError(f"descriptor equality guard failed for {name}")


def write_report(report: dict, outdir: Path) -> None:
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / "probe_summary.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")


def compare_payload(base: dict, candidate: dict, Bf: np.ndarray) -> dict:
    base_gradient = Bf.T @ base["arrays"]["initial_pullback"]
    candidate_gradient = Bf.T @ candidate["arrays"]["initial_pullback"]
    comparison = {
        "objective_delta_candidate_minus_default": candidate["objective"] - base["objective"],
        "default_projected_gradient": base_gradient.tolist(),
        "candidate_projected_gradient": candidate_gradient.tolist(),
        "projected_gradient_delta": (candidate_gradient - base_gradient).tolist(),
        "projected_gradient_delta_norm": float(np.linalg.norm(candidate_gradient - base_gradient)),
    }
    for key in ("initial_pullback", "predicted_150", "predicted_300",
                "checkpoint_150", "checkpoint_300"):
        delta = candidate["arrays"][key] - base["arrays"][key]
        comparison[key] = {
            "default_sha256": hashlib.sha256(base["arrays"][key].tobytes()).hexdigest(),
            "candidate_sha256": hashlib.sha256(candidate["arrays"][key].tobytes()).hexdigest(),
            "delta_rms": float(np.sqrt(np.mean(delta * delta))),
            "delta_max_abs": float(np.max(np.abs(delta))),
        }
    return comparison


def run_probe(exe: Path, ci_root: Path, previous_root: Path, outdir: Path) -> dict:
    runner = ROOT / "tools/test_fine_wave_inverse.py"
    cpp = ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"
    if sha256(runner) != RUNNER_SHA256 or sha256(cpp) != CPP_SHA256:
        raise RuntimeError("runner or C++ source hash differs from frozen probe binding")
    current_head, current_tree = subprocess.run(
        ["git", "rev-parse", "HEAD", "HEAD^{tree}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.splitlines()
    pr_head_tree = subprocess.run(
        ["git", "rev-parse", f"{PR_HEAD}^{{tree}}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.strip()
    if pr_head_tree != EXPECTED_TREE:
        raise RuntimeError(f"unexpected PR source tree: {pr_head_tree}")
    context_path = find_unique(ci_root, "probe_context.json")
    context = json.loads(context_path.read_text())
    context_expected = {
        "ci_run": int(RUN_ID),
        "ci_artifact_zip_sha256": ARTIFACT_SHA256,
        "ci_checkout_commit": SOURCE_COMMIT,
        "ci_tree": SOURCE_TREE,
        "pr_head": PR_HEAD,
        "runner_sha256": RUNNER_SHA256,
        "cpp_sha256": CPP_SHA256,
        "observation_file_sha256": OBSERVATION_SHA256,
    }
    mismatches = {key: (context.get(key), expected)
                  for key, expected in context_expected.items()
                  if context.get(key) != expected}
    descriptor_receipt = context.get("ci_descriptor_sha256", {})
    if descriptor_receipt.get("coarse") != EXPECTED_DESCRIPTORS["descriptor_8x6x4.csv"]:
        mismatches["coarse_descriptor_sha256"] = descriptor_receipt.get("coarse")
    if descriptor_receipt.get("fine") != EXPECTED_DESCRIPTORS["descriptor_16x12x8.csv"]:
        mismatches["fine_descriptor_sha256"] = descriptor_receipt.get("fine")
    if mismatches:
        raise RuntimeError(f"compact CI fixture provenance mismatch: {mismatches}")
    previous_report_path = find_unique(previous_root, "probe_summary.json")
    if sha256(previous_report_path) != PREVIOUS_REPORT_SHA256:
        raise RuntimeError(f"unexpected previous probe report bytes: {previous_report_path}")
    previous_report = json.loads(previous_report_path.read_text())
    if (previous_report.get("status") != "completed_diagnostic_only" or
            previous_report.get("native_call_count") != 6 or
            previous_report.get("source", {}).get("pr_head") != PR_HEAD or
            previous_report.get("source", {}).get("runner_sha256") != RUNNER_SHA256 or
            previous_report.get("source", {}).get("cpp_sha256") != CPP_SHA256):
        raise RuntimeError("previous diagnostic report does not match the frozen PR281 context")
    obs = find_unique(ci_root, "fixed_physical_observations.txt")
    if sha256(obs) != OBSERVATION_SHA256:
        raise RuntimeError(f"unexpected fixed CI observation bytes: {obs}")
    for point in POINTS.values():
        p = find_unique(ci_root, point["filename"])
        if sha256(p) != point["sha256"]:
            raise RuntimeError(f"unexpected CI initial-state bytes: {p}")

    inputs = outdir / "ci_inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    input_paths = {"observations": obs}
    for name in EXPECTED_DESCRIPTORS:
        input_paths[name] = find_unique(ci_root, name)
    for key, point in POINTS.items():
        input_paths[key] = find_unique(ci_root, point["filename"])
    copied_inputs = {}
    for key, source in input_paths.items():
        target = inputs / source.name
        shutil.copyfile(source, target)
        copied_inputs[key] = {
            "source_path": str(source), "source_sha256": sha256(source),
            "copied_path": str(target), "copied_sha256": sha256(target),
        }
    if any(v["source_sha256"] != v["copied_sha256"] for v in copied_inputs.values()):
        raise RuntimeError("a copied CI input differs from its source bytes")

    mod = load_runner_module()
    historical_results_dir = outdir / "historical_results"
    historical_results_dir.mkdir(parents=True, exist_ok=True)
    previous_report_copy = historical_results_dir / "prior_probe_summary.json"
    shutil.copyfile(previous_report_path, previous_report_copy)
    historical_payloads = {}
    historical_records = []
    historical_precision_names = {"default_N12_K8", "tight_N13_K10"}
    for old in previous_report["probes"]:
        point_name = old["point"]
        precision_name = old["precision"]
        if point_name not in POINTS or precision_name not in historical_precision_names:
            raise RuntimeError(f"unexpected probe in prior report: {point_name}/{precision_name}")
        if old["input_state_sha256"] != POINTS[point_name]["sha256"]:
            raise RuntimeError(f"prior probe state mismatch: {point_name}/{precision_name}")
        if old["observations_sha256"] != OBSERVATION_SHA256:
            raise RuntimeError(f"prior probe observation mismatch: {point_name}/{precision_name}")
        old_filename = Path(old["native_call"]["output_path"]).name
        old_source = find_unique(previous_root, old_filename)
        if sha256(old_source) != old["native_call"]["output_sha256"]:
            raise RuntimeError(f"prior raw payload hash mismatch: {old_filename}")
        old_copy = historical_results_dir / old_filename
        shutil.copyfile(old_source, old_copy)
        old_meta, old_arrays, old_scalars, _ = mod.read_payload(old_copy)
        old_newton = float(np.float32(float(old["requested_newton_tolerance"])))
        old_krylov = float(np.float32(float(old["requested_krylov_tolerance"])))
        if (old_meta.get("physical_wave_newton_tol") != old_newton or
                old_meta.get("physical_wave_krylov_tol") != old_krylov):
            raise RuntimeError(f"prior effective tolerance mismatch: {old_filename}")
        historical_payloads[(point_name, precision_name)] = {
            "arrays": old_arrays,
            "objective": float(old_scalars["objective_physical_w"]),
            "path": str(old_copy),
        }
        historical_records.append({
            "point": point_name,
            "precision": precision_name,
            "input_state_sha256": old["input_state_sha256"],
            "observations_sha256": old["observations_sha256"],
            "objective": float(old_scalars["objective_physical_w"]),
            "projected_gradient": old["projected_gradient"],
            "projected_gradient_norm": old["projected_gradient_norm"],
            "raw_payload_path": str(old_copy),
            "raw_payload_sha256": sha256(old_copy),
            "source_run_id": PREVIOUS_RUN_ID,
        })
    if len(historical_payloads) != 4:
        raise RuntimeError(f"expected four historical probes, found {len(historical_payloads)}")
    report = {
        "schema": "pr281-h5-same-point-precision-probe-v1",
        "status": "running_diagnostic_only",
        "diagnostic_only": True,
        "interpretation": "Not a convergence gate, not an Armijo acceptance change, and not required-success validation.",
        "source": {
            "pr_head": PR_HEAD,
            "pr_head_tree": pr_head_tree,
            "ci_checkout_commit": SOURCE_COMMIT,
            "ci_checkout_tree": SOURCE_TREE,
            "diagnostic_commit": current_head,
            "diagnostic_worktree_tree": current_tree,
            "expected_tree": EXPECTED_TREE,
            "runner_sha256": sha256(runner), "cpp_sha256": sha256(cpp),
            "executable": str(exe), "executable_sha256": sha256(exe),
            "python": platform.python_version(), "numpy": np.__version__,
        },
        "ci_source_artifact": {
            "run_id": RUN_ID, "artifact_name": "core-linux-diagnostics",
            "artifact_zip_sha256": ARTIFACT_SHA256,
            "compact_fixture_sha256": FIXTURE_SHA256,
            "fixture_context_path": str(context_path),
            "fixture_context_sha256": sha256(context_path),
        },
        "previous_diagnostic": {
            "run_id": PREVIOUS_RUN_ID,
            "artifact_zip_sha256": PREVIOUS_ARTIFACT_SHA256,
            "summary_path": str(previous_report_copy),
            "summary_sha256": sha256(previous_report_copy),
            "reused_without_rerun": ["default_N12_K8", "tight_N13_K10"],
            "historical_probes": historical_records,
        },
        "fixed_observations": {
            "path": str(copied_inputs["observations"]["copied_path"]),
            "sha256": copied_inputs["observations"]["copied_sha256"],
            "rows": 105,
            "source_values_regenerated": False,
        },
        "ci_inputs": copied_inputs,
        "descriptor_guard": {},
        "native_calls": [],
        "probes": [],
        "comparisons_vs_historical_default": {},
    }
    write_report(report, outdir)

    try:
        descriptor_guard(mod, exe, ci_root, outdir, report)
    except Exception as exc:
        report["status"] = "incomplete_diagnostic_only"
        report["error"] = repr(exc)
        write_report(report, outdir)
        raise

    ci_coarse = find_unique(ci_root, "descriptor_8x6x4.csv")
    ci_fine = find_unique(ci_root, "descriptor_16x12x8.csv")
    c0 = mod.reference.build_case(8, 4, native_csv=ci_coarse)
    cf = mod.reference.build_case(16, 8, native_csv=ci_fine)
    _, basis_f = mod.profile_basis(c0, cf)
    Bf = np.column_stack([mod.pack_source_mode(q, (16, 12, 8)) for q in basis_f])
    obs_copy = Path(copied_inputs["observations"]["copied_path"])
    logs = outdir / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    current_payloads = {}
    for point_name in POINTS:
        initial = Path(copied_inputs[point_name]["copied_path"])
        for precision_name, newton, krylov in NEW_PRECISIONS:
            label = f"{point_name}_{precision_name}"
            output = outdir / "results" / f"{label}.csv"
            output.parent.mkdir(parents=True, exist_ok=True)
            try:
                call = run_native(
                    exe, 16, 12, 8, output,
                    ["--physical-wave-inverse", str(initial), str(obs_copy), "60", "5",
                     "--newton-tol", newton, "--krylov-tol", krylov],
                    logs, label)
                report["native_calls"].append(call)
                report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
                write_report(report, outdir)
                if call["return_code"] != 0 or not output.is_file():
                    raise RuntimeError(f"native probe call failed for {label}")
                meta, arrays, scalars, text = mod.read_payload(output)
                effective_newton = float(np.float32(float(newton)))
                effective_krylov = float(np.float32(float(krylov)))
                reported_newton = meta.get("physical_wave_newton_tol")
                reported_krylov = meta.get("physical_wave_krylov_tol")
                if reported_newton != effective_newton or reported_krylov != effective_krylov:
                    raise RuntimeError(
                        f"effective tolerances for {label} differ from requested float32 values: "
                        f"Newton {reported_newton} != {effective_newton}; "
                        f"Krylov {reported_krylov} != {effective_krylov}")
            except Exception as exc:
                report["status"] = "incomplete_diagnostic_only"
                report["error"] = repr(exc)
                write_report(report, outdir)
                raise
            gradient = Bf.T @ arrays["initial_pullback"]
            current_payloads[(point_name, precision_name)] = {
                "arrays": arrays,
                "objective": float(scalars["objective_physical_w"]),
                "path": str(output),
            }
            result = {
                "point": point_name,
                "precision": precision_name,
                "requested_newton_tolerance": newton,
                "requested_krylov_tolerance": krylov,
                "effective_physical_wave_newton_tol": reported_newton,
                "effective_physical_wave_krylov_tol": reported_krylov,
                "tolerance_metadata_matches_requested_float32": True,
                "grid": [16, 12, 8], "steps": 60, "dt_s": 5,
                "input_state_path": str(initial),
                "input_state_sha256": sha256(initial),
                "observations_sha256": sha256(obs_copy),
                "objective": float(scalars["objective_physical_w"]),
                "projected_gradient": gradient.tolist(),
                "projected_gradient_norm": float(np.linalg.norm(gradient)),
                "initial_pullback_sha256": hashlib.sha256(
                    arrays["initial_pullback"].tobytes()).hexdigest(),
                "prediction_150_sha256": hashlib.sha256(
                    arrays["predicted_150"].tobytes()).hexdigest(),
                "prediction_300_sha256": hashlib.sha256(
                    arrays["predicted_300"].tobytes()).hexdigest(),
                "checkpoint_150_sha256": hashlib.sha256(
                    arrays["checkpoint_150"].tobytes()).hexdigest(),
                "checkpoint_300_sha256": hashlib.sha256(
                    arrays["checkpoint_300"].tobytes()).hexdigest(),
                "minimum_face_distance_m": min(
                    meta["minimum_height_to_any_W_face_150_m"],
                    meta["minimum_height_to_any_W_face_300_m"]),
                "bracket_changes": int(meta["bracket_corner_changes_150_to_300"]),
                "checkpoint_precision": text.get("checkpoint_precision"),
                "native_call": call,
            }
            report["probes"].append(result)
            write_report(report, outdir)
    for point_name in POINTS:
        default = historical_payloads[(point_name, "default_N12_K8")]
        joint_tight = historical_payloads[(point_name, "tight_N13_K10")]
        krylov_only = current_payloads[(point_name, "krylov_only_N12_K10")]
        newton_only = current_payloads[(point_name, "newton_only_N13_K8")]
        report["comparisons_vs_historical_default"][point_name] = {
            "historical_joint_tight_N13_K10_vs_default_N12_K8":
                compare_payload(default, joint_tight, Bf),
            "krylov_only_N12_K10_vs_default_N12_K8":
                compare_payload(default, krylov_only, Bf),
            "newton_only_N13_K8_vs_default_N12_K8":
                compare_payload(default, newton_only, Bf),
            "newton_only_N13_K8_vs_krylov_only_N12_K10":
                compare_payload(krylov_only, newton_only, Bf),
        }
    write_report(report, outdir)
    report["status"] = "completed_diagnostic_only"
    report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
    write_report(report, outdir)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--ci-artifact-root", required=True, type=Path)
    parser.add_argument("--previous-artifact-root", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    try:
        report = run_probe(args.exe.resolve(), args.ci_artifact_root.resolve(),
                           args.previous_artifact_root.resolve(), args.output_dir.resolve())
        print(json.dumps({"status": report["status"], "probe_count": len(report["probes"]),
                          "descriptor_guard_passed": all(
                              v["passed"] for v in report["descriptor_guard"].values())},
                         sort_keys=True))
        return 0
    except Exception as exc:
        failure = {"schema": "pr281-h5-same-point-precision-probe-v1",
                   "status": "failed_before_or_during_diagnostic_only_probe",
                   "diagnostic_only": True, "error": repr(exc)}
        (args.output_dir / "probe_failure.json").write_text(
            json.dumps(failure, indent=2, sort_keys=True) + "\n")
        raise


if __name__ == "__main__":
    raise SystemExit(main())
