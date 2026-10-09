#!/usr/bin/env python3
"""Run bounded diagnostic-only h=5 inverse-Hessian correction probes."""
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
CORRECTION_REFERENCE_RUN_ID = "37790210254"
CORRECTION_REFERENCE_ARTIFACT_SHA256 = "d1d16b869eeaac18c94529a4a77850dda708a762217ab3af298d9791b5d53505"
CORRECTION_REFERENCE_REPORT_SHA256 = "6af412a609c804dad6cf0cd74e81b006839c41019df6a127c4f59b89583fbf98"
CORRECTION_STATE_SHA256 = {
    "default_N12_K8": "ac6634eb6761eddb4011707f843f6e303ef3790f8f7788741a6bd3bffcbf0e80",
    "krylov_only_N12_K10": "92ed9288aa79df4a73c769831b573c8f13e712305c57e6c980caec70bdc77ddf",
}
N13_REFERENCE_RUN_ID = "37803412710"
N13_REFERENCE_ARTIFACT_SHA256 = "9d5057dffd148cee3729054738c4fb6ecde942925b869ab0ca51385f6df8f20f"
N13_REFERENCE_REPORT_SHA256 = "b73869e2a197a0158ae374c4afca30a7e21a0fc63a521f69793730f7efba2e58"
FIXED_N13_CORRECTED_STATE_SHA256 = "2e9afb468189e463cad11a602e38fec7f60de2a0af44fbac03cea3ded9e4c8bc"
N13_CANDIDATE_SOURCE_COMMIT = "7a7b8286ad00ff421d584f4b34c1cc9f9996a36a"
PR_HEAD = "38c05f7542136ed035197f7a0a0335e71b6fda83"
SOURCE_COMMIT = "e5a5fe500dcdcce225700556362ed6a0e9db1835"
SOURCE_TREE = "90dd29e901968e9f7eacf28144581022a1eb4c2b"
EXPECTED_TREE = "90dd29e901968e9f7eacf28144581022a1eb4c2b"
RUNNER_SHA256 = "81009bde9076e26f067b65547db9d2a41b7729101bf4709e8ec564237a15fd7c"
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
        recomputed_gradient = Bf.T @ old_arrays["initial_pullback"]
        reported_gradient = np.asarray(old["projected_gradient"], dtype=np.float64)
        gradient_reconstruction_delta = recomputed_gradient - reported_gradient
        if not np.allclose(recomputed_gradient, reported_gradient, rtol=1.0e-10, atol=5.0e-12):
            raise RuntimeError(f"historical projected gradient does not reconstruct for {key}")
        old_gradient = reported_gradient.copy()
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
            "projected_gradient_reconstruction_max_abs_delta": float(
                np.max(np.abs(gradient_reconstruction_delta))),
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

def residual_log_summary(path: Path) -> dict:
    lines = path.read_text().splitlines()
    return {
        "path": str(path), "sha256": sha256(path),
        "newton_converged": [line for line in lines if line.startswith("[Newton] CONVERGED")],
        "gmres_converged": [line for line in lines if line.startswith("[GMRES] CONVERGED")],
        "newton_gmres": [line for line in lines if line.startswith("[Newton] GMRES:")],
    }


def run_n13_stability_probe(exe: Path, ci_root: Path, reference_root: Path,
                            outdir: Path) -> dict:
    runner = ROOT / "tools/test_fine_wave_inverse.py"
    cpp = ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"
    if sha256(runner) != RUNNER_SHA256 or sha256(cpp) != CPP_SHA256:
        raise RuntimeError("runner or C++ source hash differs from frozen probe binding")
    pr_head_tree = subprocess.run(
        ["git", "rev-parse", f"{PR_HEAD}^{{tree}}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.strip()
    if pr_head_tree != EXPECTED_TREE:
        raise RuntimeError(f"unexpected PR source tree: {pr_head_tree}")

    reference_path = find_unique(reference_root, "probe_summary.json")
    if sha256(reference_path) != CORRECTION_REFERENCE_REPORT_SHA256:
        raise RuntimeError(f"unexpected N12 correction reference report bytes: {reference_path}")
    reference = json.loads(reference_path.read_text())
    context = reference["correction_context"]
    if (reference.get("status") != "completed_diagnostic_only" or
            reference.get("source", {}).get("diagnostic_commit") != "af2ec4e8344a6f074b5d8281a4b03ba5253d29c0" or
            reference.get("source", {}).get("pr_head") != PR_HEAD or
            reference.get("ci_artifact", {}).get("compact_fixture_sha256") != FIXTURE_SHA256 or
            context.get("x_c_mapping_exactly_matches_saved_initial") is not False or
            context.get("x_c_optimizer_mapping_max_abs") != 9.094947017729282e-13):
        raise RuntimeError("N12 correction reference context is not the pinned diagnostic")
    reference_dir = reference_path.parent
    reference_copy = outdir / "reference_inputs" / "n12_correction_reference_summary.json"
    reference_copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(reference_path, reference_copy)
    if sha256(reference_copy) != CORRECTION_REFERENCE_REPORT_SHA256:
        raise RuntimeError("copied N12 correction reference summary hash changed")
    mod = load_runner_module()

    input_specs = {
        "coarse_descriptor": ("descriptor_8x6x4.csv", EXPECTED_DESCRIPTORS["descriptor_8x6x4.csv"]),
        "fine_descriptor": ("descriptor_16x12x8.csv", EXPECTED_DESCRIPTORS["descriptor_16x12x8.csv"]),
        "observations": ("fixed_physical_observations.txt", OBSERVATION_SHA256),
    }
    inputs = outdir / "reference_inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    copied_inputs = {}
    for key, (filename, expected_hash) in input_specs.items():
        source = reference_dir / "ci_inputs" / filename
        current_fixture = find_unique(ci_root, filename)
        if (not source.is_file() or sha256(source) != expected_hash or
                sha256(current_fixture) != expected_hash):
            raise RuntimeError(f"literal N12/reference input hash changed: {filename}")
        target = inputs / filename
        shutil.copyfile(source, target)
        copied_inputs[key] = {
            "source_path": str(source), "source_sha256": sha256(source),
            "copied_path": str(target), "copied_sha256": sha256(target),
        }
        if sha256(target) != expected_hash:
            raise RuntimeError(f"copied N12/reference input hash changed: {filename}")

    c0 = mod.reference.build_case(8, 4, native_csv=inputs / "descriptor_8x6x4.csv")
    cf = mod.reference.build_case(16, 8, native_csv=inputs / "descriptor_16x12x8.csv")
    _, basis_f = mod.profile_basis(c0, cf)
    Bf = np.column_stack([mod.pack_source_mode(q, (16, 12, 8)) for q in basis_f])
    fresh_bf_sha256 = hashlib.sha256(Bf.tobytes()).hexdigest()
    reference_bf_sha256 = context["Bf_sha256"]
    bf_sha_different = fresh_bf_sha256 != reference_bf_sha256
    dot_length = Bf.shape[0]
    dot_gamma = (dot_length * np.finfo(np.float64).eps) / (
        1.0 - dot_length * np.finfo(np.float64).eps)
    H = np.asarray(context["H_before_iteration9"], dtype=np.float64)
    if hashlib.sha256(H.tobytes()).hexdigest() != context["H_sha256"]:
        raise RuntimeError("iteration-9 H differs from the hashed N12 correction context")

    points = {item["gradient_source"]: item for item in reference["correction_points"]}
    if set(points) != set(CORRECTION_STATE_SHA256):
        raise RuntimeError("N12 corrected-state records are incomplete or unexpected")
    states_dir = outdir / "generated_inputs"
    baselines_dir = outdir / "n12_baselines"
    states_dir.mkdir(parents=True, exist_ok=True)
    baselines_dir.mkdir(parents=True, exist_ok=True)
    baseline_records, baseline_payloads = {}, {}
    for method, expected_hash in CORRECTION_STATE_SHA256.items():
        point = points[method]
        state_source = reference_dir / "generated_inputs" / Path(point["optimizer_coordinate_state_path"]).name
        if (not state_source.is_file() or sha256(state_source) != expected_hash or
                point["optimizer_coordinate_state_sha256"] != expected_hash):
            raise RuntimeError(f"pinned N12 corrected-state bytes changed for {method}")
        state_path = states_dir / state_source.name
        shutil.copyfile(state_source, state_path)
        if sha256(state_path) != expected_hash:
            raise RuntimeError(f"copied N12 corrected-state bytes changed for {method}")

        record = next((item for item in reference["correction_results"]
                       if item["correction_method"] == method and
                       item["precision"] == "default_N12_K8"), None)
        if record is None or record["input_state_sha256"] != expected_hash:
            raise RuntimeError(f"N12/K8 baseline missing for corrected state {method}")
        call = record["native_call"]
        payload_source = reference_dir / "results" / Path(call["output_path"]).name
        if not payload_source.is_file() or sha256(payload_source) != call["output_sha256"]:
            raise RuntimeError(f"N12/K8 baseline payload hash changed for {method}")
        payload_path = baselines_dir / payload_source.name
        shutil.copyfile(payload_source, payload_path)
        meta, arrays, scalars, payload_text = mod.read_payload(payload_path)
        if not np.array_equal(read_vector(state_path), arrays["initial_state"]):
            raise RuntimeError(f"N12/K8 baseline did not use exact corrected state for {method}")
        fresh_gradient = Bf.T @ arrays["initial_pullback"]
        reference_gradient = np.asarray(record["projected_gradient"], dtype=np.float64)
        projection_delta = fresh_gradient - reference_gradient
        projection_roundoff_bound = dot_gamma * (
            np.abs(Bf).T @ np.abs(arrays["initial_pullback"]))
        projection_guard = np.abs(projection_delta) <= projection_roundoff_bound
        if not np.all(projection_guard):
            raise RuntimeError(f"N12/K8 projection delta exceeds componentwise dot-roundoff bound for {method}")
        log_records = {}
        for stream in ("stdout", "stderr"):
            source = reference_dir / "logs" / Path(call[f"{stream}_path"]).name
            if not source.is_file() or sha256(source) != call[f"{stream}_sha256"]:
                raise RuntimeError(f"N12/K8 {stream} log hash changed for {method}")
            target = baselines_dir / source.name
            shutil.copyfile(source, target)
            log_records[stream] = (residual_log_summary(target) if stream == "stderr" else {
                "path": str(target), "sha256": sha256(target), "text": target.read_text(),
            })
        baseline_payloads[method] = {
            "arrays": arrays, "objective": float(scalars["objective_physical_w"]),
            "path": str(payload_path), "gradient": fresh_gradient,
            "meta": meta, "text": payload_text,
        }
        baseline_records[method] = {
            "precision": "default_N12_K8", "input_state_path": str(state_path),
            "input_state_sha256": expected_hash, "payload_path": str(payload_path),
            "payload_sha256": sha256(payload_path),
            "objective": float(scalars["objective_physical_w"]),
            "projected_gradient": record["projected_gradient"],
            "projected_gradient_norm": record["projected_gradient_norm"],
            "projected_gradient_fresh_Bf": fresh_gradient.tolist(),
            "projected_gradient_delta_fresh_minus_reference": projection_delta.tolist(),
            "projected_gradient_componentwise_dot_roundoff_bound": projection_roundoff_bound.tolist(),
            "projected_gradient_componentwise_dot_roundoff_guard": projection_guard.tolist(),
            "projected_gradient_max_abs_delta": float(np.max(np.abs(projection_delta))),
            "projected_gradient_max_delta_to_bound_ratio": float(np.max(
                np.abs(projection_delta) / np.maximum(projection_roundoff_bound,
                                                       np.finfo(np.float64).tiny))),
            "stdout": log_records["stdout"], "solver_residual_log": log_records["stderr"],
        }

    current_head, current_tree = subprocess.run(
        ["git", "rev-parse", "HEAD", "HEAD^{tree}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.splitlines()
    report = {
        "schema": "pr281-h5-correction-n13-stability-diagnostic-v1",
        "status": "running_diagnostic_only", "diagnostic_only": True,
        "interpretation": "N13/K8 stability comparison on the two exact N12 correction states. No optimizer acceptance or promotion claim is made.",
        "promotion_note": "Results remain diagnostic; a projected-gradient norm below 1e-5 does not promote either point.",
        "source": {
            "pr_head": PR_HEAD, "pr_head_tree": pr_head_tree,
            "diagnostic_commit": current_head, "diagnostic_worktree_tree": current_tree,
            "runner_sha256": sha256(runner), "cpp_sha256": sha256(cpp),
            "executable": str(exe), "executable_sha256": sha256(exe),
            "python": platform.python_version(), "numpy": np.__version__,
        },
        "reference": {
            "run_id": CORRECTION_REFERENCE_RUN_ID,
            "artifact_zip_sha256": CORRECTION_REFERENCE_ARTIFACT_SHA256,
            "report_path": str(reference_path), "report_sha256": sha256(reference_path),
            "copied_report_path": str(reference_copy), "copied_report_sha256": sha256(reference_copy),
            "status": reference["status"],
            "x_c_mapping_exactly_matches_saved_initial": context["x_c_mapping_exactly_matches_saved_initial"],
            "x_c_mapping_max_abs_residual": context["x_c_optimizer_mapping_max_abs"],
            "Bf_shape": list(Bf.shape),
            "reference_Bf_sha256": reference_bf_sha256,
            "fresh_Bf_sha256": fresh_bf_sha256,
            "Bf_sha256_different_from_reference": bf_sha_different,
            "Bf_componentwise_projection_dot_roundoff_gamma": float(dot_gamma),
            "H_sha256": hashlib.sha256(H.tobytes()).hexdigest(),
            "H_before_iteration9": context["H_before_iteration9"],
            "x_c_controls": context["x_c_controls"], "g_sources": context["g_sources"],
            "corrected_states": [
                {"method": method, "path": str(states_dir / Path(point["optimizer_coordinate_state_path"]).name),
                 "sha256": sha256(states_dir / Path(point["optimizer_coordinate_state_path"]).name),
                 "gradient": point["gradient"], "control_delta_minus_Hg": point["control_delta_minus_Hg"],
                 "corrected_controls": point["corrected_controls"],
                 "exact_optimizer_coordinate_claim": point["exact_optimizer_coordinate_claim"]}
                for method, point in points.items()
            ],
            "literal_inputs": copied_inputs, "n12_baselines": baseline_records,
        },
        "requested_tolerances": {"newton": "1e-13", "krylov": "1e-8"},
        "effective_float32_tolerances": {
            "newton": float(np.float32(1.0e-13)), "krylov": float(np.float32(1.0e-8)),
        },
        "descriptor_guard": {}, "native_calls": [], "results": [], "comparisons_vs_n12_k8": {},
    }
    write_report(report, outdir)
    try:
        descriptor_guard(mod, exe, ci_root, outdir, report)
        observations = inputs / "fixed_physical_observations.txt"
        logs = outdir / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        for method in CORRECTION_STATE_SHA256:
            point = points[method]
            state_path = states_dir / Path(point["optimizer_coordinate_state_path"]).name
            output = outdir / "results" / f"{method}_N13_K8.csv"
            output.parent.mkdir(parents=True, exist_ok=True)
            label = f"{method}_N13_K8"
            call = run_native(
                exe, 16, 12, 8, output,
                ["--physical-wave-inverse", str(state_path), str(observations), "60", "5",
                 "--newton-tol", "1e-13", "--krylov-tol", "1e-8"], logs, label)
            report["native_calls"].append(call)
            report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
            write_report(report, outdir)
            if call["return_code"] != 0 or not output.is_file():
                raise RuntimeError(f"N13/K8 native call failed for {method}")
            meta, arrays, scalars, payload_text = mod.read_payload(output)
            if (not all(np.isfinite(array).all() for array in arrays.values()) or
                    not all(np.isfinite(float(value)) for value in scalars.values())):
                raise RuntimeError(f"N13/K8 output contains non-finite values for {method}")
            effective_newton, effective_krylov = float(np.float32(1.0e-13)), float(np.float32(1.0e-8))
            if (meta.get("physical_wave_newton_tol") != effective_newton or
                    meta.get("physical_wave_krylov_tol") != effective_krylov):
                raise RuntimeError(f"effective N13/K8 tolerance metadata mismatch for {method}")
            baseline = baseline_payloads[method]
            bracket_checks = {str(t): bool(np.array_equal(
                arrays[f"bracket_index_{t}"], baseline["arrays"][f"bracket_index_{t}"]))
                for t in (150, 300)}
            minimum_face = min(meta["minimum_height_to_any_W_face_150_m"],
                               meta["minimum_height_to_any_W_face_300_m"])
            bracket_guard = (all(bracket_checks.values()) and
                             int(meta["bracket_corner_changes_150_to_300"]) == 0 and minimum_face > 0.0)
            gradient = Bf.T @ arrays["initial_pullback"]
            baseline_record = baseline_records[method]
            record = {
                "correction_method": method, "precision": "N13_K8",
                "requested_newton_tolerance": "1e-13", "requested_krylov_tolerance": "1e-8",
                "effective_physical_wave_newton_tol": meta["physical_wave_newton_tol"],
                "effective_physical_wave_krylov_tol": meta["physical_wave_krylov_tol"],
                "input_state_path": str(state_path), "input_state_sha256": sha256(state_path),
                "observations_sha256": sha256(observations),
                "objective": float(scalars["objective_physical_w"]),
                "projected_gradient": gradient.tolist(),
                "projected_gradient_norm": float(np.linalg.norm(gradient)),
                "below_1e5_gate_is_diagnostic_only": bool(np.linalg.norm(gradient) < 1.0e-5),
                "bracket_index_match_to_n12_k8": bracket_checks,
                "bracket_guard_passed": bracket_guard, "minimum_face_distance_m": minimum_face,
                "bracket_changes_150_to_300": int(meta["bracket_corner_changes_150_to_300"]),
                "checkpoint_precision": payload_text.get("checkpoint_precision"),
                "native_call": call,
                "solver_residual_logs": {
                    "n12_k8": baseline_record["solver_residual_log"],
                    "n13_k8": residual_log_summary(Path(call["stderr_path"])),
                },
            }
            report["results"].append(record)
            candidate = {"arrays": arrays, "objective": float(scalars["objective_physical_w"]),
                         "gradient": gradient, "path": str(output)}
            comparison = compare_payload(baseline, candidate, Bf)
            comparison["objective_delta_n13_minus_n12"] = (
                float(scalars["objective_physical_w"]) - baseline["objective"])
            comparison["bracket_guard_passed"] = bracket_guard
            comparison["n12_k8_gradient_reconstruction_max_abs_delta"] = float(np.max(np.abs(
                baseline["gradient"] - np.asarray(baseline_record["projected_gradient"], dtype=np.float64))))
            report["comparisons_vs_n12_k8"][method] = comparison
            write_report(report, outdir)
        all_brackets = all(item["bracket_guard_passed"] for item in report["results"])
        report["status"] = "completed_diagnostic_only" if all_brackets else "completed_diagnostic_only_bracket_guard_failed"
        report["all_brackets_unchanged_vs_n12_k8"] = all_brackets
        report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
        write_report(report, outdir)
        return report
    except Exception as exc:
        report["status"] = "incomplete_diagnostic_only"
        report["error"] = repr(exc)
        write_report(report, outdir)
        raise


def run_n13_correction_n14_stability_probe(exe: Path, ci_root: Path,
                                            reference_root: Path, outdir: Path) -> dict:
    """Re-evaluate the exact saved 378034 candidate with the corrected solver."""
    runner = ROOT / "tools/test_fine_wave_inverse.py"
    cpp = ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"
    if sha256(runner) != RUNNER_SHA256 or sha256(cpp) != CPP_SHA256:
        raise RuntimeError("runner or C++ source hash differs from frozen probe binding")
    if subprocess.run(["git", "rev-parse", f"{PR_HEAD}^{{tree}}"], cwd=ROOT,
                      capture_output=True, text=True, check=True).stdout.strip() != EXPECTED_TREE:
        raise RuntimeError("PR_HEAD reference baseline tree changed")

    reference_path = find_unique(reference_root, "probe_summary.json")
    if sha256(reference_path) != N13_REFERENCE_REPORT_SHA256:
        raise RuntimeError(f"unexpected saved-candidate report bytes: {reference_path}")
    saved = json.loads(reference_path.read_text())
    candidate = saved.get("candidate", {})
    saved_reference = saved.get("reference", {})
    if (saved.get("status") != "incomplete_diagnostic_only" or
            saved.get("source", {}).get("diagnostic_commit") != N13_CANDIDATE_SOURCE_COMMIT or
            saved.get("source", {}).get("pr_head") != PR_HEAD or
            saved.get("source", {}).get("pr_head_tree") != EXPECTED_TREE or
            candidate.get("state_sha256") != FIXED_N13_CORRECTED_STATE_SHA256 or
            candidate.get("admissibility_passed") is not True or
            saved_reference.get("Bf_sha256_different_from_reference") is not True or
            not all(saved_reference.get("source_n13_k8_gradient_componentwise_dot_roundoff_guard", []))):
        raise RuntimeError("run 378034 does not contain the pinned admissible candidate")

    reference_dir = reference_path.parent
    source_state = reference_dir / "generated_inputs" / "default_N12_K8_n13_gradient_correction_state.txt"
    if not source_state.is_file() or sha256(source_state) != FIXED_N13_CORRECTED_STATE_SHA256:
        raise RuntimeError("saved run-378034 candidate state bytes changed")
    saved_n13 = next((item for item in saved.get("results", [])
                      if item.get("precision") == "N13_K10"), None)
    if (saved_n13 is None or saved_n13.get("input_state_sha256") != FIXED_N13_CORRECTED_STATE_SHA256 or
            saved_n13.get("native_call", {}).get("return_code") != 0):
        raise RuntimeError("run 378034 lacks a successful N13/K10 result on the saved candidate")

    inputs = outdir / "reference_inputs"
    inputs.mkdir(parents=True, exist_ok=True)
    state_dir = outdir / "generated_inputs"
    state_dir.mkdir(parents=True, exist_ok=True)
    candidate_path = state_dir / source_state.name
    shutil.copyfile(source_state, candidate_path)
    if sha256(candidate_path) != FIXED_N13_CORRECTED_STATE_SHA256:
        raise RuntimeError("copied saved candidate state changed")

    saved_bf = reference_dir / "reference_inputs" / "fresh_Bf.npy"
    if (not saved_bf.is_file() or
            sha256(saved_bf) != saved_reference.get("fresh_Bf_file_sha256")):
        raise RuntimeError("saved run-378034 Bf.npy bytes changed")
    Bf = np.load(saved_bf, allow_pickle=False)
    bf_digest = hashlib.sha256(Bf.tobytes()).hexdigest()
    if (list(Bf.shape) != saved_reference.get("fresh_Bf_shape") or
            bf_digest != saved_reference.get("fresh_Bf_sha256")):
        raise RuntimeError("saved run-378034 Bf array does not match its pinned digest")
    bf_copy = inputs / "fresh_Bf.npy"
    shutil.copyfile(saved_bf, bf_copy)
    if sha256(bf_copy) != sha256(saved_bf):
        raise RuntimeError("copied Bf.npy changed")

    literal_inputs = {}
    for filename, expected_hash in {
        "descriptor_8x6x4.csv": EXPECTED_DESCRIPTORS["descriptor_8x6x4.csv"],
        "descriptor_16x12x8.csv": EXPECTED_DESCRIPTORS["descriptor_16x12x8.csv"],
        "fixed_physical_observations.txt": OBSERVATION_SHA256,
    }.items():
        source = reference_dir / "reference_inputs" / filename
        ci_path = find_unique(ci_root, filename)
        if (not source.is_file() or sha256(source) != expected_hash or
                sha256(ci_path) != expected_hash):
            raise RuntimeError(f"saved/CI input hash mismatch: {filename}")
        target = inputs / filename
        shutil.copyfile(source, target)
        if sha256(target) != expected_hash:
            raise RuntimeError(f"copied literal input hash changed: {filename}")
        literal_inputs[filename] = {"path": str(target), "sha256": sha256(target)}

    mod = load_runner_module()
    fine_path = inputs / "descriptor_16x12x8.csv"
    fine_case = mod.reference.build_case(16, 8, native_csv=fine_path)
    fine_meta, fine_arrays, fine_scalars, fine_text = mod.read_payload(fine_path)
    fine_descriptor = {
        "meta": fine_meta, "arrays": fine_arrays, "scalars": fine_scalars,
        "text_meta": fine_text, "column": fine_case, "base": fine_arrays["base"],
    }
    candidate_state = read_vector(candidate_path)
    mod.check_initial_admissibility((16, 12, 8), candidate_state, fine_descriptor)

    old_call = saved_n13["native_call"]
    old_payload_path = reference_dir / "results" / Path(old_call["output_path"]).name
    if not old_payload_path.is_file() or sha256(old_payload_path) != old_call["output_sha256"]:
        raise RuntimeError("saved N13/K10 candidate control payload hash changed")
    _, old_arrays, old_scalars, _ = mod.read_payload(old_payload_path)
    if not np.array_equal(candidate_state, old_arrays["initial_state"]):
        raise RuntimeError("saved N13/K10 result used different candidate-state bytes")
    baseline_brackets = {t: old_arrays[f"bracket_index_{t}"] for t in (150, 300)}

    source_head, source_tree = subprocess.run(
        ["git", "rev-parse", "HEAD", "HEAD^{tree}"], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.splitlines()
    tracked_sources = (
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_autograd_utils.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_krylov_metrics.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_config.h",
        "external/libtorch_wrf/sdirk3/CMakeLists.txt",
        "external/libtorch_wrf/sdirk3/test_krylov_metric_coordinate_contract.cpp",
        "tools/pr281_h5_precision_probe.py",
        ".github/workflows/sdirk3-ci.yml",
    )
    report = {
        "schema": "pr281-saved-candidate-n13-n14-stability-v1",
        "status": "running_diagnostic_only", "diagnostic_only": True,
        "interpretation": "Re-evaluate the exact saved run-378034 corrected state at N13/K10 and N14/K10 with the small-RHS solver correction. No candidate regeneration, Armijo, or acceptance claim.",
        "promotion_note": "Diagnostic only. Keep the 1e-5 projected-gradient gate and strict solver, bracket, finite, and admissibility failures.",
        "reference_baseline": {"pr_head": PR_HEAD, "tree": EXPECTED_TREE},
        "source": {
            "candidate_head": source_head, "candidate_tree": source_tree,
            "candidate_file_sha256": {name: sha256(ROOT / name) for name in tracked_sources},
            "runner_sha256": sha256(runner), "native_test_cpp_sha256": sha256(cpp),
            "executable": str(exe), "executable_sha256": sha256(exe),
            "reference_artifact_run_id": N13_REFERENCE_RUN_ID,
            "reference_artifact_zip_sha256": N13_REFERENCE_ARTIFACT_SHA256,
            "reference_report_sha256": sha256(reference_path),
            "reference_report_source_commit": saved["source"]["diagnostic_commit"],
        },
        "saved_candidate": {
            "state_path": str(candidate_path), "state_sha256": sha256(candidate_path),
            "controls": candidate.get("candidate_controls"),
            "H9_sha256": candidate.get("H9_sha256"),
            "source_gradient_norm": candidate.get("g_source_norm"),
            "saved_n13_k10_objective": saved_n13.get("objective"),
            "saved_n13_k10_result_sha256": old_call["output_sha256"],
            "candidate_regenerated": False,
        },
        "basis": {
            "path": str(bf_copy), "file_sha256": sha256(bf_copy),
            "array_sha256": bf_digest, "shape": list(Bf.shape),
            "reference_Bf_array_sha256": saved_reference.get("reference_Bf_sha256"),
            "different_from_reference_Bf": saved_reference.get("Bf_sha256_different_from_reference"),
            "componentwise_projection_delta": saved_reference.get("source_n13_k8_gradient_projection_delta_fresh_minus_reference"),
            "componentwise_roundoff_bound": saved_reference.get("source_n13_k8_gradient_componentwise_dot_roundoff_bound"),
            "roundoff_guard": saved_reference.get("source_n13_k8_gradient_componentwise_dot_roundoff_guard"),
        },
        "literal_inputs": literal_inputs,
        "adjoint_residual_logs_available": False,
        "adjoint_residual_logs_note": "The standalone Grid/Tile native test bypasses the zero-copy environment loader; no transpose-residual instrumentation is enabled.",
        "requested_tolerances": [
            {"name": "N13_K10", "newton": "1e-13", "krylov": "1e-10"},
            {"name": "N14_K10", "newton": "1e-14", "krylov": "1e-10"},
        ],
        "effective_float32_tolerances": {
            "N13_K10": {"newton": float(np.float32(1e-13)), "krylov": float(np.float32(1e-10))},
            "N14_K10": {"newton": float(np.float32(1e-14)), "krylov": float(np.float32(1e-10))},
        },
        "descriptor_guard": {}, "native_calls": [], "native_call_count": 0,
        "results": [], "comparison_n13_k10_vs_n14_k10": {},
    }
    write_report(report, outdir)
    try:
        descriptor_guard(mod, exe, ci_root, outdir, report)
        logs = outdir / "logs"
        logs.mkdir(parents=True, exist_ok=True)
        paired = {}
        for precision, newton in (("N13_K10", "1e-13"), ("N14_K10", "1e-14")):
            output = outdir / "results" / f"saved_candidate_{precision}.csv"
            output.parent.mkdir(parents=True, exist_ok=True)
            call = run_native(
                exe, 16, 12, 8, output,
                ["--physical-wave-inverse", str(candidate_path),
                 str(inputs / "fixed_physical_observations.txt"), "60", "5",
                 "--newton-tol", newton, "--krylov-tol", "1e-10"], logs,
                f"saved_candidate_{precision}")
            report["native_calls"].append(call)
            report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
            write_report(report, outdir)
            if call["return_code"] != 0 or not output.is_file():
                raise RuntimeError(f"{precision} failed; preserve strict failure, no retry")
            meta, arrays, scalars, payload_text = mod.read_payload(output)
            if (not all(np.isfinite(array).all() for array in arrays.values()) or
                    not all(np.isfinite(float(value)) for value in scalars.values())):
                raise RuntimeError(f"{precision} payload contains non-finite values")
            if not np.array_equal(candidate_state, arrays["initial_state"]):
                raise RuntimeError(f"{precision} did not use the exact saved candidate bytes")
            if (meta.get("physical_wave_newton_tol") != float(np.float32(float(newton))) or
                    meta.get("physical_wave_krylov_tol") != float(np.float32(1e-10))):
                raise RuntimeError(f"effective float32 tolerance metadata mismatch at {precision}")
            bracket_checks = {str(t): bool(np.array_equal(
                arrays[f"bracket_index_{t}"], baseline_brackets[t])) for t in (150, 300)}
            minimum_face = min(meta["minimum_height_to_any_W_face_150_m"],
                               meta["minimum_height_to_any_W_face_300_m"])
            bracket_guard = (all(bracket_checks.values()) and
                             int(meta["bracket_corner_changes_150_to_300"]) == 0 and
                             minimum_face > 0.0)
            gradient = Bf.T @ arrays["initial_pullback"]
            item = {
                "precision": precision, "input_state_sha256": sha256(candidate_path),
                "objective": float(scalars["objective_physical_w"]),
                "projected_gradient": gradient.tolist(),
                "projected_gradient_norm": float(np.linalg.norm(gradient)),
                "below_1e5_gate_is_diagnostic_only": bool(np.linalg.norm(gradient) < 1e-5),
                "bracket_index_match_to_saved_n13_k10": bracket_checks,
                "bracket_guard_passed": bracket_guard,
                "minimum_face_distance_m": minimum_face,
                "bracket_changes_150_to_300": int(meta["bracket_corner_changes_150_to_300"]),
                "checkpoint_precision": payload_text.get("checkpoint_precision"),
                "native_call": call,
                "solver_residual_log": residual_log_summary(Path(call["stderr_path"])),
            }
            report["results"].append(item)
            paired[precision] = {"arrays": arrays, "objective": item["objective"]}
            write_report(report, outdir)

        report["comparison_n13_k10_vs_n14_k10"] = compare_payload(
            paired["N13_K10"], paired["N14_K10"], Bf)
        all_brackets = all(item["bracket_guard_passed"] for item in report["results"])
        report["all_brackets_match_saved_candidate_control"] = all_brackets
        report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
        report["status"] = "completed_diagnostic_only" if all_brackets else "completed_diagnostic_only_bracket_guard_failed"
        write_report(report, outdir)
        return report
    except Exception as exc:
        report["status"] = "incomplete_diagnostic_only"
        report["error"] = repr(exc)
        report["native_call_count"] = len(report["descriptor_guard"]) + len(report["native_calls"])
        write_report(report, outdir)
        raise

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--ci-artifact-root", required=True, type=Path)
    parser.add_argument("--previous-artifact-root", type=Path)
    parser.add_argument("--n13-reference-artifact-root", type=Path)
    parser.add_argument("--n13-correction-reference-artifact-root", type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    try:
        if args.n13_correction_reference_artifact_root:
            report = run_n13_correction_n14_stability_probe(
                args.exe.resolve(), args.ci_artifact_root.resolve(),
                args.n13_correction_reference_artifact_root.resolve(), args.output_dir.resolve())
        elif args.n13_reference_artifact_root:
            report = run_n13_stability_probe(
                args.exe.resolve(), args.ci_artifact_root.resolve(),
                args.n13_reference_artifact_root.resolve(), args.output_dir.resolve())
        elif args.previous_artifact_root:
            report = run_probe(args.exe.resolve(), args.ci_artifact_root.resolve(),
                               args.previous_artifact_root.resolve(), args.output_dir.resolve())
        else:
            parser.error("provide a reference artifact root for one diagnostic mode")
        print(json.dumps({"status": report["status"],
                          "result_count": len(report.get("correction_results", report.get("results", []))),
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
