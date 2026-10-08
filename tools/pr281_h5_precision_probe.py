#!/usr/bin/env python3
"""Run four diagnostic-only h=5 inverse-Hessian correction probes."""
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
PREVIOUS_RUN_ID = "37784434765"
PREVIOUS_ARTIFACT_SHA256 = "659c93f037741fe8a55e36524d33f502a9f83cdf3de51c337720f15f82381277"
PREVIOUS_REPORT_SHA256 = "0e9d08ebec77dafd8b46c0f452243bb41b61359ba9b2d9516341d427cb003d94"
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
CORRECTION_POINT = {
    "name": "first_iteration9_rejected",
    "filename": "full_step_rejected_state.txt",
    "sha256": "d2c6ed9a8b42349232d5d70b6d07c5e15873592516859148060c83bf46d1dced",
}
CONFIG_EXPECTED = {
    "kdamp_config": 0.0, "implicit_divergence": 0.0,
    "omega_w_blend_config": 1.0, "do_curvature_config": 1.0,
    "effective_wrf_omega_ww_cp": 1.0, "advection_order_config": 2.0,
    "non_hydrostatic_config": 1.0, "map_input_max_deviation": 0.0,
}


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


def read_vector(path: Path) -> np.ndarray:
    values = path.read_text().split()
    count = int(values[0])
    result = np.asarray([float(value) for value in values[1:]], dtype=np.float64)
    if result.size != count:
        raise RuntimeError(f"state length mismatch in {path}: {result.size} != {count}")
    return result


def run_probe(exe: Path, ci_root: Path, previous_root: Path, outdir: Path) -> dict:
    runner = ROOT / "tools/test_fine_wave_inverse.py"
    cpp = ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"
    if sha256(runner) != RUNNER_SHA256 or sha256(cpp) != CPP_SHA256:
        raise RuntimeError("runner or C++ source hash differs from frozen probe binding")
    pr_head_tree = subprocess.run(
        ["git", "rev-parse", f"{PR_HEAD}^{{tree}}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.strip()
    if pr_head_tree != EXPECTED_TREE:
        raise RuntimeError(f"unexpected PR source tree: {pr_head_tree}")

    context_path = find_unique(ci_root, "probe_context.json")
    context = json.loads(context_path.read_text())
    context_expected = {
        "ci_run": int(RUN_ID), "ci_artifact_zip_sha256": ARTIFACT_SHA256,
        "ci_checkout_commit": SOURCE_COMMIT, "ci_tree": SOURCE_TREE,
        "pr_head": PR_HEAD, "runner_sha256": RUNNER_SHA256,
        "cpp_sha256": CPP_SHA256, "observation_file_sha256": OBSERVATION_SHA256,
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

    reconstruction_path = find_unique(ci_root, "full_h5_armijo_reconstruction.json")
    reconstruction = json.loads(reconstruction_path.read_text())
    bfgs = reconstruction["BFGS"]
    h5_point = bfgs["full_step_rejected_candidate"]
    H = np.asarray(bfgs["H_at_failed_iteration"], dtype=np.float64)
    if not np.array_equal(H, np.asarray(context["inverse_H_before_iteration9"], dtype=np.float64)):
        raise RuntimeError("CI iteration-9 H disagrees between reconstruction and fixture context")
    v_c = np.asarray(h5_point["controls"], dtype=np.float64)
    context_vc = np.asarray(context["target_evaluations"]["first_full_step_rejected"]["controls"],
                            dtype=np.float64)
    if not np.array_equal(v_c, context_vc):
        raise RuntimeError("CI first-rejected controls disagree between reconstruction and context")
    J_optimizer_initial = float(bfgs["initial"]["objective"])

    obs = find_unique(ci_root, "fixed_physical_observations.txt")
    if sha256(obs) != OBSERVATION_SHA256:
        raise RuntimeError(f"unexpected fixed CI observation bytes: {obs}")
    saved_xc_path = find_unique(ci_root, CORRECTION_POINT["filename"])
    if sha256(saved_xc_path) != CORRECTION_POINT["sha256"]:
        raise RuntimeError(f"unexpected CI correction-state bytes: {saved_xc_path}")
    saved_xc = read_vector(saved_xc_path)

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

    ci_coarse_path = find_unique(ci_root, "descriptor_8x6x4.csv")
    ci_fine_path = find_unique(ci_root, "descriptor_16x12x8.csv")
    if (sha256(ci_coarse_path) != EXPECTED_DESCRIPTORS["descriptor_8x6x4.csv"] or
            sha256(ci_fine_path) != EXPECTED_DESCRIPTORS["descriptor_16x12x8.csv"]):
        raise RuntimeError("exact CI descriptor source hashes changed")
    mod = load_runner_module()
    c0 = mod.reference.build_case(8, 4, native_csv=ci_coarse_path)
    cf = mod.reference.build_case(16, 8, native_csv=ci_fine_path)
    _, basis_f = mod.profile_basis(c0, cf)
    Bf = np.column_stack([mod.pack_source_mode(q, (16, 12, 8)) for q in basis_f])
    base_ci = np.asarray(mod.read_payload(ci_fine_path)[1]["base"], dtype=np.float64)
    x_c_from_controls = base_ci + Bf @ v_c
    mapping_residual = x_c_from_controls - saved_xc

    previous_results = list(previous_report["previous_diagnostic"]["historical_probes"])
    previous_results.extend(previous_report["probes"])
    expected_previous = {
        (CORRECTION_POINT["name"], precision)
        for precision in ("default_N12_K8", "tight_N13_K10",
                          "krylov_only_N12_K10", "newton_only_N13_K8")
    }
    previous_payloads = {}
    previous_raw_records = []
    historical_dir = outdir / "past_axis_results"
    historical_dir.mkdir(parents=True, exist_ok=True)
    previous_summary_copy = historical_dir / "prior_tolerance_axis_probe_summary.json"
    shutil.copyfile(previous_report_path, previous_summary_copy)
    for old in previous_results:
        key = (old["point"], old["precision"])
        if key in previous_payloads:
            raise RuntimeError(f"duplicate historical probe {key}")
        if old["point"] != CORRECTION_POINT["name"]:
            continue
        if old["input_state_sha256"] != CORRECTION_POINT["sha256"]:
            raise RuntimeError(f"historical state mismatch for {key}")
        if old["observations_sha256"] != OBSERVATION_SHA256:
            raise RuntimeError(f"historical observation mismatch for {key}")
        if "native_call" in old:
            old_filename = Path(old["native_call"]["output_path"]).name
            expected_payload_sha = old["native_call"]["output_sha256"]
        else:
            old_filename = Path(old["raw_payload_path"]).name
            expected_payload_sha = old["raw_payload_sha256"]
        old_source = find_unique(previous_root, old_filename)
        if sha256(old_source) != expected_payload_sha:
            raise RuntimeError(f"historical raw payload hash mismatch for {key}")
        old_copy = historical_dir / old_filename
        shutil.copyfile(old_source, old_copy)
        old_meta, old_arrays, old_scalars, _ = mod.read_payload(old_copy)
        old_gradient = Bf.T @ old_arrays["initial_pullback"]
        reported_gradient = np.asarray(old["projected_gradient"], dtype=np.float64)
        if not np.array_equal(old_gradient, reported_gradient):
            raise RuntimeError(f"historical projected gradient does not reconstruct for {key}")
        previous_payloads[key] = {
            "arrays": old_arrays,
            "objective": float(old_scalars["objective_physical_w"]),
            "path": str(old_copy),
            "gradient": old_gradient,
            "metadata": old_meta,
        }
        previous_raw_records.append({
            "point": old["point"], "precision": old["precision"],
            "objective": float(old_scalars["objective_physical_w"]),
            "projected_gradient": old_gradient.tolist(),
            "projected_gradient_norm": float(np.linalg.norm(old_gradient)),
            "raw_payload_path": str(old_copy), "raw_payload_sha256": sha256(old_copy),
        })
    if set(previous_payloads) != expected_previous:
        raise RuntimeError(f"historical tolerance results are incomplete: {set(previous_payloads)}")

    default_xc = previous_payloads[(CORRECTION_POINT["name"], "default_N12_K8")]
    g_sources = {
        "default_N12_K8": default_xc["gradient"],
        "krylov_only_N12_K10": previous_payloads[(CORRECTION_POINT["name"], "krylov_only_N12_K10")]["gradient"],
    }
    if not np.array_equal(default_xc["arrays"]["initial_state"], saved_xc):
        raise RuntimeError("default historical native input differs from literal CI x_c")

    inputs = outdir / "ci_inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    input_paths = {
        "coarse_descriptor": ci_coarse_path, "fine_descriptor": ci_fine_path,
        "observations": obs, "x_c_first_rejected": saved_xc_path,
    }
    copied_inputs = {}
    for key, source in input_paths.items():
        target = inputs / source.name
        shutil.copyfile(source, target)
        copied_inputs[key] = {
            "source_path": str(source), "source_sha256": sha256(source),
            "copied_path": str(target), "copied_sha256": sha256(target),
        }
    if any(v["source_sha256"] != v["copied_sha256"] for v in copied_inputs.values()):
        raise RuntimeError("a copied literal CI input differs from its source bytes")

    current_head, current_tree = subprocess.run(
        ["git", "rev-parse", "HEAD", "HEAD^{tree}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.splitlines()
    pr_head_tree = subprocess.run(
        ["git", "rev-parse", f"{PR_HEAD}^{{tree}}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.strip()
    if pr_head_tree != EXPECTED_TREE:
        raise RuntimeError(f"unexpected PR source tree: {pr_head_tree}")
    report = {
        "schema": "pr281-h5-correction-linear-model-diagnostic-v1",
        "status": "running_diagnostic_only",
        "diagnostic_only": True,
        "interpretation": "Synthetic H*g corrections at the saved iteration-9 rejected x_c. J values are compared with the optimizer initial objective only; no Armijo or descent claim is made.",
        "promotion_note": "If any corrected point has projected-gradient norm below 1e-5, it remains diagnostic only; N13-only at that same corrected state is required before any promotion.",
        "source": {
            "pr_head": PR_HEAD, "pr_head_tree": pr_head_tree,
            "ci_checkout_commit": SOURCE_COMMIT, "ci_checkout_tree": SOURCE_TREE,
            "diagnostic_commit": current_head, "diagnostic_worktree_tree": current_tree,
            "runner_sha256": sha256(runner), "cpp_sha256": sha256(cpp),
            "executable": str(exe), "executable_sha256": sha256(exe),
            "python": platform.python_version(), "numpy": np.__version__,
        },
        "ci_artifact": {
            "source_run_id": RUN_ID, "source_zip_sha256": ARTIFACT_SHA256,
            "compact_fixture_sha256": FIXTURE_SHA256,
            "probe_context_sha256": sha256(context_path),
            "full_reconstruction_sha256": sha256(reconstruction_path),
        },
        "past_tolerance_axis_results": {
            "run_id": PREVIOUS_RUN_ID, "artifact_zip_sha256": PREVIOUS_ARTIFACT_SHA256,
            "summary_path": str(previous_summary_copy),
            "summary_sha256": sha256(previous_summary_copy),
            "results_reused_without_rerun": previous_raw_records,
        },
        "literal_inputs": copied_inputs,
        "optimizer_initial_objective": J_optimizer_initial,
        "correction_context": {
            "point": CORRECTION_POINT["name"],
            "H_source": "full_h5_armijo_reconstruction.json::BFGS.H_at_failed_iteration",
            "H_sha256": hashlib.sha256(H.tobytes()).hexdigest(),
            "H_before_iteration9": H.tolist(),
            "x_c_controls": v_c.tolist(),
            "x_c_state_sha256": sha256(saved_xc_path),
            "x_c_optimizer_mapping_residual_l2": float(np.linalg.norm(mapping_residual)),
            "x_c_optimizer_mapping_max_abs": float(np.max(np.abs(mapping_residual))),
            "x_c_optimizer_mapping_sha256": hashlib.sha256(x_c_from_controls.tobytes()).hexdigest(),
            "x_c_mapping_exactly_matches_saved_initial": bool(np.array_equal(x_c_from_controls, saved_xc)),
            "g_sources": {
                name: {"gradient": value.tolist(), "norm": float(np.linalg.norm(value)),
                       "source_run_id": PREVIOUS_RUN_ID,
                       "source_precision": name}
                for name, value in g_sources.items()
            },
            "Bf_shape": list(Bf.shape),
            "Bf_sha256": hashlib.sha256(Bf.tobytes()).hexdigest(),
        },
        "correction_points": [], "correction_results": [],
        "native_calls": [], "comparisons": {},
    }
    write_report(report, outdir)

    try:
        descriptor_guard(mod, exe, ci_root, outdir, report)
        descriptors = {}
        for name, path in (("coarse", ci_coarse_path), ("fine", ci_fine_path)):
            meta, arrays, scalars, text = mod.read_payload(path)
            column = mod.reference.build_case(8 if name == "coarse" else 16,
                                              4 if name == "coarse" else 8,
                                              native_csv=path)
            descriptors[name] = {"meta": meta, "arrays": arrays, "scalars": scalars,
                                 "text_meta": text, "column": column, "base": arrays["base"]}

        logs = outdir / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        correction_states = {}
        for method, gradient in g_sources.items():
            delta_controls = -(H @ gradient)
            corrected_controls = v_c + delta_controls
            optimizer_state = base_ci + Bf @ corrected_controls
            literal_delta_state = saved_xc + Bf @ delta_controls
            state_path = outdir / "generated_inputs" / f"{method}_optimizer_coordinate_state.txt"
            state_path.parent.mkdir(parents=True, exist_ok=True)
            mod.write_vector(state_path, optimizer_state)
            check_state = read_vector(state_path)
            if not np.array_equal(check_state, optimizer_state):
                raise RuntimeError(f"generated correction state round-trip differs for {method}")
            mod.check_initial_admissibility((16, 12, 8), check_state, descriptors["fine"])
            correction_states[method] = {
                "method": "base_CI + Bf @ (v_c - H @ g_c)",
                "gradient_source": method,
                "gradient": gradient.tolist(), "gradient_norm": float(np.linalg.norm(gradient)),
                "H_sha256": hashlib.sha256(H.tobytes()).hexdigest(),
                "control_delta_minus_Hg": delta_controls.tolist(),
                "control_delta_norm": float(np.linalg.norm(delta_controls)),
                "x_c_controls": v_c.tolist(), "corrected_controls": corrected_controls.tolist(),
                "saved_x_c_state_sha256": sha256(saved_xc_path),
                "x_c_from_controls_sha256": hashlib.sha256(x_c_from_controls.tobytes()).hexdigest(),
                "x_c_mapping_max_abs_difference_from_saved_state": float(np.max(np.abs(mapping_residual))),
                "optimizer_coordinate_state_path": str(state_path),
                "optimizer_coordinate_state_sha256": sha256(state_path),
                "literal_saved_state_plus_Bf_delta_sha256": hashlib.sha256(literal_delta_state.tobytes()).hexdigest(),
                "max_abs_between_two_state_constructions": float(np.max(np.abs(optimizer_state-literal_delta_state))),
                "initial_admissibility_passed": True,
                "exact_optimizer_coordinate_claim": bool(np.array_equal(x_c_from_controls, saved_xc)),
                "interpretation": "Uses base_CI + Bf@(v_c-H@g_c); saved-state-plus-delta is separately hashed because x_c reconstruction may differ by roundoff.",
            }
        report["correction_points"] = list(correction_states.values())
        write_report(report, outdir)

        baseline_brackets = {
            t: default_xc["arrays"][f"bracket_index_{t}"] for t in (150, 300)
        }
        native_payloads = {}
        for method, correction in correction_states.items():
            state_path = Path(correction["optimizer_coordinate_state_path"])
            for precision_name, newton, krylov in (
                ("default_N12_K8", "1e-12", "1e-8"),
                ("krylov_only_N12_K10", "1e-12", "1e-10"),
            ):
                label = f"{method}_{precision_name}"
                output = outdir / "results" / f"{label}.csv"
                output.parent.mkdir(parents=True, exist_ok=True)
                try:
                    call = run_native(
                        exe, 16, 12, 8, output,
                        ["--physical-wave-inverse", str(state_path),
                         copied_inputs["observations"]["copied_path"], "60", "5",
                         "--newton-tol", newton, "--krylov-tol", krylov],
                        logs, label)
                    report["native_calls"].append(call)
                    report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
                    write_report(report, outdir)
                    if call["return_code"] != 0 or not output.is_file():
                        raise RuntimeError(f"native correction call failed for {label}")
                    meta, arrays, scalars, text = mod.read_payload(output)
                    effective_newton = float(np.float32(float(newton)))
                    effective_krylov = float(np.float32(float(krylov)))
                    reported_newton = meta.get("physical_wave_newton_tol")
                    reported_krylov = meta.get("physical_wave_krylov_tol")
                    if reported_newton != effective_newton or reported_krylov != effective_krylov:
                        raise RuntimeError(f"effective tolerance metadata mismatch for {label}")
                    bracket_checks = {str(t): bool(np.array_equal(
                        arrays[f"bracket_index_{t}"], baseline_brackets[t])) for t in (150, 300)}
                    bracket_guard_passed = all(bracket_checks.values()) and int(
                        meta["bracket_corner_changes_150_to_300"]) == 0
                    minimum_face = min(meta["minimum_height_to_any_W_face_150_m"],
                                       meta["minimum_height_to_any_W_face_300_m"])
                    if minimum_face <= 0.0:
                        bracket_guard_passed = False
                    gradient = Bf.T @ arrays["initial_pullback"]
                    native_payloads[(method, precision_name)] = {
                        "arrays": arrays,
                        "objective": float(scalars["objective_physical_w"]),
                        "path": str(output),
                        "gradient": gradient,
                    }
                    record = {
                        "correction_method": method,
                        "precision": precision_name,
                        "requested_newton_tolerance": newton,
                        "requested_krylov_tolerance": krylov,
                        "effective_physical_wave_newton_tol": reported_newton,
                        "effective_physical_wave_krylov_tol": reported_krylov,
                        "tolerance_metadata_matches_requested_float32": True,
                        "input_state_path": str(state_path),
                        "input_state_sha256": sha256(state_path),
                        "observations_sha256": OBSERVATION_SHA256,
                        "objective": float(scalars["objective_physical_w"]),
                        "objective_delta_vs_optimizer_initial_only": float(scalars["objective_physical_w"])-J_optimizer_initial,
                        "projected_gradient": gradient.tolist(),
                        "projected_gradient_norm": float(np.linalg.norm(gradient)),
                        "below_1e5_gate_is_diagnostic_candidate_only": bool(np.linalg.norm(gradient)<1.0e-5),
                        "initial_pullback_sha256": hashlib.sha256(arrays["initial_pullback"].tobytes()).hexdigest(),
                        "prediction_150_sha256": hashlib.sha256(arrays["predicted_150"].tobytes()).hexdigest(),
                        "prediction_300_sha256": hashlib.sha256(arrays["predicted_300"].tobytes()).hexdigest(),
                        "checkpoint_150_sha256": hashlib.sha256(arrays["checkpoint_150"].tobytes()).hexdigest(),
                        "checkpoint_300_sha256": hashlib.sha256(arrays["checkpoint_300"].tobytes()).hexdigest(),
                        "minimum_face_distance_m": minimum_face,
                        "bracket_index_match_to_x_c_first_rejected": bracket_checks,
                        "bracket_guard_passed": bracket_guard_passed,
                        "bracket_changes_150_to_300": int(meta["bracket_corner_changes_150_to_300"]),
                        "checkpoint_precision": text.get("checkpoint_precision"),
                        "native_call": call,
                    }
                    report["correction_results"].append(record)
                    write_report(report, outdir)
                except Exception as exc:
                    report["status"] = "incomplete_diagnostic_only"
                    report["error"] = repr(exc)
                    write_report(report, outdir)
                    raise
        for method in correction_states:
            default = native_payloads[(method, "default_N12_K8")]
            krylov_only = native_payloads[(method, "krylov_only_N12_K10")]
            report["comparisons"][method] = compare_payload(default, krylov_only, Bf)
        all_brackets_passed = all(x["bracket_guard_passed"] for x in report["correction_results"])
        report["status"] = ("completed_diagnostic_only" if all_brackets_passed
                             else "completed_diagnostic_only_bracket_guard_failed")
        report["all_face_brackets_unchanged_vs_x_c"] = all_brackets_passed
        report["promotion_requires_N13_at_same_corrected_state"] = True
        write_report(report, outdir)
        return report
    except Exception as exc:
        report["status"] = "incomplete_diagnostic_only"
        report["error"] = repr(exc)
        write_report(report, outdir)
        raise

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
        print(json.dumps({"status": report["status"],
                          "correction_result_count": len(report["correction_results"]),
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
