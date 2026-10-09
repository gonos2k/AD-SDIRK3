#!/usr/bin/env python3
"""Compare the completed main fine-H5 returned state at N14/K12 and FD."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path
from zipfile import ZipFile

import numpy as np
import scipy

import pr281_saved_state_k12_probe as common


ROOT = common.ROOT
REFERENCE_RUN_ID = 37873496629
REFERENCE_ARTIFACT_ID = 11593606142
REFERENCE_ARTIFACT_NAME = "physical-inverse-gate-37873496629"
REFERENCE_ARTIFACT_SHA256 = "dfac1516bfcdefb780072311e29cdeb9cf82b0cbccc95c679f5adb0bb8a6830e"
REFERENCE_PRODUCER_COMMIT = "8d8f84d92f5be40bf0230bcc05781274d604f857"
REFERENCE_PR_HEAD = "12c12e7501807681ec5072cb8dd2d1bac2717607"
REFERENCE_PR_BASE = "ee8faadbf728f61fe9db8f92adcf9f8d79a3bac7"
REFERENCE_TREE = "b0c7531636c60634077ad4dde46b7aebc57dcb56"
REFERENCE_RUNNER_SHA256 = "17f00e0edd6ea936523846189d78fd91ffe123b72671d17e6343adda196bfa18"
REFERENCE_CPP_SHA256 = common.CPP_SHA256
REFERENCE_SOURCE_SHA256 = "f16ab33d137b333ffeb0939f6b6ed2bc7f28091f1a246255e3eeaa3aab063431"
REFERENCE_TRANSPORT_SHA256 = "526923f41be9a8225b10a6fa022d11b60d0c60ba071d625cbe6d06dc750fc86f"
REFERENCE_SCIPY_VERSION = "1.15.3"
REQUIREMENTS_CORE_SHA256 = "73a4fe0be541423b2cee7b0f3b1e7943f056865fc73449cfd2a942e0b99ebeae"
EPSILONS = (1.0e-2, 5.0e-3)
FD_DIRECTION = np.asarray([0.78, -0.36, 0.35, 0.24], dtype=np.float64)
FD_DIRECTION /= np.linalg.norm(FD_DIRECTION)


def find_unique(root: Path, name: str) -> Path:
    matches = [p for p in root.rglob(name) if p.is_file()]
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name} under {root}, found {len(matches)}")
    return matches[0]


def verify_reference_receipt(receipt_path: Path, archive_path: Path) -> dict:
    receipt = json.loads(receipt_path.read_text())
    run = receipt["run"]
    artifact = receipt["artifact"]
    merge = receipt["merge_commit"]
    if (receipt.get("schema") != "pr281-early-gate-api-receipt-v1" or
            common.sha256(archive_path) != REFERENCE_ARTIFACT_SHA256 or
            receipt.get("archive_sha256") != REFERENCE_ARTIFACT_SHA256 or
            run.get("id") != REFERENCE_RUN_ID or run.get("event") != "pull_request" or
            run.get("head_branch") != "agent/fine-wave-inverse-20261007" or
            run.get("head_sha") != REFERENCE_PR_HEAD or
            run.get("pull_request_number") != 281 or
            run.get("pull_request_head_sha") != REFERENCE_PR_HEAD or
            run.get("pull_request_base_sha") != REFERENCE_PR_BASE or
            artifact.get("id") != REFERENCE_ARTIFACT_ID or
            artifact.get("name") != REFERENCE_ARTIFACT_NAME or
            artifact.get("digest") != f"sha256:{REFERENCE_ARTIFACT_SHA256}" or
            artifact.get("run_id") != REFERENCE_RUN_ID or
            merge.get("sha") != REFERENCE_PRODUCER_COMMIT or merge.get("tree") != REFERENCE_TREE or
            REFERENCE_PR_HEAD not in merge.get("parents", []) or
            REFERENCE_PR_BASE not in merge.get("parents", [])):
        raise RuntimeError("early-gate run/artifact/merge API receipt mismatch")
    return receipt


def verify_archive_member(root: Path, archive_path: Path, path: Path) -> None:
    member = path.relative_to(root).as_posix()
    with ZipFile(archive_path) as archive:
        if (archive.testzip() is not None or member not in archive.namelist() or
                hashlib.sha256(archive.read(member)).hexdigest() != common.sha256(path)):
            raise RuntimeError(f"extracted early-gate bytes differ from pinned ZIP member: {member}")


def verify_fixture(fixture: Path, fixture_zip: Path) -> tuple[dict, np.ndarray, dict]:
    if common.sha256(fixture_zip) != common.FIXTURE_SHA256:
        raise RuntimeError("pinned Bf fixture ZIP hash mismatch")
    manifest_path = fixture / "manifest.json"
    needed = ("descriptor_8x6x4.csv", "descriptor_16x12x8.csv",
              "fixed_physical_observations.txt", "Bf.npy")
    with ZipFile(fixture_zip) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("Bf fixture ZIP CRC check failed")
        for name in ("manifest.json", *needed):
            path = fixture / name
            if (name not in archive.namelist() or
                    hashlib.sha256(archive.read(name)).hexdigest() != common.sha256(path)):
                raise RuntimeError(f"unpacked fixture differs from pinned ZIP member: {name}")
    manifest = json.loads(manifest_path.read_text())
    if (manifest.get("schema") != "pr281-saved-v40a9c2-k12-fixture-v1" or
            manifest.get("source_run_id") != 37863920808):
        raise RuntimeError("Bf fixture manifest provenance mismatch")
    for name in needed:
        if common.sha256(fixture / name) != manifest["files"][name]["sha256"]:
            raise RuntimeError(f"Bf fixture member hash mismatch: {name}")
    Bf = np.load(fixture / "Bf.npy", allow_pickle=False)
    if (Bf.shape != (8480, 4) or not np.isfinite(Bf).all() or
            hashlib.sha256(Bf.tobytes()).hexdigest() != manifest["Bf"]["array_sha256"]):
        raise RuntimeError("pinned saved Bf shape/hash mismatch")
    return manifest, Bf, {key: fixture / name for key, name in common.FILE_NAMES.items()}


def physical_h_transpose_checks(mod, arrays: dict, descriptor: dict, xyz: np.ndarray) -> dict:
    nx, ny, nz = 16, 12, 8
    sizes = (ny*nz*(nx+1), (ny+1)*nz*nx, ny*(nz+1)*nx,
             ny*(nz+1)*nx, ny*nz*nx, ny*nx)
    offsets = np.r_[0, np.cumsum(sizes[:-1])].astype(int)
    w0, ph0 = int(offsets[2]), int(offsets[3])
    wsl, phsl = slice(w0, ph0), slice(ph0, ph0+sizes[3])
    phb = np.asarray(descriptor["arrays"]["phb"], dtype=np.float64).reshape(ny, nz+1, nx)
    results = {}
    for t in (150, 300):
        independent = mod.numpy_physical_w_pullback(
            (nx, ny, nz), phb, arrays[f"checkpoint_{t}"], xyz,
            arrays[f"observation_cotangent_{t}"], float(descriptor["column"]["g"]))
        native = arrays[f"checkpoint_cotangent_{t}"]
        norm_w, norm_ph = np.linalg.norm(native[wsl]), np.linalg.norm(native[phsl])
        err_w = float(np.linalg.norm(independent[wsl]-native[wsl])/max(norm_w, np.finfo(float).tiny))
        err_ph = float(np.linalg.norm(independent[phsl]-native[phsl])/max(norm_ph, np.finfo(float).tiny))
        if min(norm_w, norm_ph) <= np.finfo(float).tiny or err_w > 2e-10 or err_ph > 2e-10:
            raise RuntimeError(f"independent physical-H W/PH transpose check failed at t={t}")
        results[str(t)] = {"W_relative_error": err_w, "PH_relative_error": err_ph,
                           "native_W_l2": float(norm_w), "native_PH_l2": float(norm_ph)}
    return results


def prepare(fixture: Path, fixture_zip: Path, reference_root: Path,
            reference_zip: Path, receipt_path: Path):
    manifest, Bf, fixture_files = verify_fixture(fixture, fixture_zip)
    receipt = verify_reference_receipt(receipt_path, reference_zip)
    source_pins = {
        "tools/test_fine_wave_inverse.py": common.RUNNER_SHA256,
        "tools/wave_quadratic_reference.py": REFERENCE_SOURCE_SHA256,
        "tools/wave_quadratic_transport.py": REFERENCE_TRANSPORT_SHA256,
        ".github/ci/requirements-core.txt": REQUIREMENTS_CORE_SHA256,
        "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp": common.CPP_SHA256,
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp": common.SOLVER_SHA256,
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_autograd_utils.h": common.AUTOGRAD_SHA256,
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_krylov_metrics.h": common.KRYLOV_SHA256,
    }
    for relative, expected in source_pins.items():
        if common.sha256(ROOT / relative) != expected:
            raise RuntimeError(f"probe source differs from pinned diagnostic build: {relative}")
    mod = common.load_runner()
    report_paths = []
    for path in reference_root.rglob("fine_wave_inverse.json"):
        try:
            data = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        if data.get("schema") == "physical-wave-inverse-v2":
            report_paths.append(path)
    if len(report_paths) != 1:
        raise RuntimeError(f"expected one physical-wave-inverse-v2 report, found {len(report_paths)}")
    report_path = report_paths[0]
    verify_archive_member(reference_root, reference_zip, report_path)
    reference = json.loads(report_path.read_text())
    artifacts = reference.get("artifacts", {})
    if (artifacts.get("source_revision") != REFERENCE_PRODUCER_COMMIT or
            artifacts.get("runner_sha256") != REFERENCE_RUNNER_SHA256 or
            artifacts.get("cpp_sha256") != REFERENCE_CPP_SHA256 or
            artifacts.get("source_reference_sha256") != REFERENCE_SOURCE_SHA256 or
            artifacts.get("transport_reference_sha256") != REFERENCE_TRANSPORT_SHA256):
        raise RuntimeError("returned-point report source revision/runner/CPP binding mismatch")
    opt = reference["fine_h5_optimization"]
    h10 = reference["fine_h10_at_fine_h5_controls"]
    controls = np.asarray(opt["returned_controls"], dtype=np.float64)
    report_gradient = np.asarray(opt["gradient"], dtype=np.float64)
    if (controls.shape != (4,) or not np.isfinite(controls).all() or
            report_gradient.shape != (4,) or not np.isfinite(report_gradient).all() or
            not opt.get("converged") or opt.get("status") not in {
                "converged_at_armijo_state", "converged_stationary_trial",
                "converged_stationarity_refinement"} or
            float(opt["gradient_norm"]) >= 1.0e-5 or
            float(opt["objective"]) >= float(opt["initial_objective"]) or
            not np.array_equal(controls, np.asarray(h10["controls"], dtype=np.float64)) or
            mod.digest(controls) != h10["controls_sha256"]):
        raise RuntimeError("fine-H5 returned controls/status fail report consistency guards")

    csv_name = Path(reference["native_files"]["fine_h5_optimized"]).name
    baseline_path = find_unique(reference_root, csv_name)
    verify_archive_member(reference_root, reference_zip, baseline_path)
    baseline_meta, baseline_arrays, baseline_scalars, baseline_text = mod.read_payload(baseline_path)
    state_name = baseline_path.name.replace("result_", "initial_", 1).replace(".csv", ".txt")
    state_path = find_unique(reference_root, state_name)
    h10_name = Path(reference["native_files"]["fine_h10_at_fine_h5_controls"]).name
    h10_path = find_unique(reference_root, h10_name)
    verify_archive_member(reference_root, reference_zip, h10_path)
    h10_meta, h10_arrays, h10_scalars, h10_text = mod.read_payload(h10_path)
    center_hash = opt["initial_state_sha256"]
    verify_archive_member(reference_root, reference_zip, state_path)
    center = common.load_vector(state_path)
    if (not np.array_equal(center, baseline_arrays["initial_state"]) or
            mod.digest(center) != center_hash or
            float(baseline_scalars["objective_physical_w"]) != float(opt["objective"]) or
            baseline_meta.get("physical_wave_newton_tol") != float(np.float32(1.0e-14)) or
            baseline_meta.get("physical_wave_krylov_tol") != float(np.float32(1.0e-10)) or
            baseline_meta.get("trajectory_steps") != 60.0 or baseline_meta.get("trajectory_dt_fp32") != 5.0 or
            baseline_meta.get("forward_only") != 0.0 or baseline_meta.get("pullback_requested") != 1.0 or
            baseline_text.get("checkpoint_precision") != "retained_fp64_trajectory" or
            baseline_text.get("physical_observation_domain_guard") != "passed"):
        raise RuntimeError("returned point is not the report-bound N14/K10 60x5 full-VJP state")
    if (not np.array_equal(center, h10_arrays["initial_state"]) or
            float(h10_scalars["objective_physical_w"]) != float(h10["objective"]) or
            h10_meta.get("physical_wave_newton_tol") != float(np.float32(1e-14)) or
            h10_meta.get("physical_wave_krylov_tol") != float(np.float32(1e-10)) or
            h10_meta.get("trajectory_steps") != 30.0 or h10_meta.get("trajectory_dt_fp32") != 10.0 or
            h10_meta.get("forward_only") != 0.0 or h10_meta.get("pullback_requested") != 1.0):
        raise RuntimeError("returned-point h10 comparison does not use the exact same saved state")
    if (not all(np.isfinite(a).all() for a in baseline_arrays.values()) or
            not all(np.isfinite(float(x)) for x in baseline_scalars.values())):
        raise RuntimeError("returned-state K10 payload contains nonfinite values")

    coarse_name = Path(reference["grids"]["coarse"]["descriptor"]).name
    fine_name = Path(reference["grids"]["fine"]["descriptor"]).name
    coarse_path = find_unique(reference_root, coarse_name)
    fine_path = find_unique(reference_root, fine_name)
    observations_path = find_unique(reference_root, Path(reference["fixed_observation_file"]).name)
    for path in (coarse_path, fine_path, observations_path):
        verify_archive_member(reference_root, reference_zip, path)
    for key, path in (("descriptor_8x6x4.csv", coarse_path),
                       ("descriptor_16x12x8.csv", fine_path),
                       ("fixed_physical_observations.txt", observations_path)):
        if common.sha256(path) != manifest["files"][key]["sha256"]:
            raise RuntimeError(f"main12c returned-point artifact changed exact input {key}")

    c0 = mod.reference.build_case(8, 4, native_csv=coarse_path)
    cf = mod.reference.build_case(16, 8, native_csv=fine_path)
    _, basis = mod.profile_basis(c0, cf)
    fresh_Bf = np.column_stack([mod.pack_source_mode(q, (16, 12, 8)) for q in basis])
    if fresh_Bf.shape != Bf.shape or not np.isfinite(fresh_Bf).all():
        raise RuntimeError("returned-point reconstructed source basis is invalid")
    fine_meta, fine_arrays, fine_scalars, fine_text = mod.read_payload(fine_path)
    if mod.digest(fine_arrays["base"]) != reference["grids"]["fine"]["base_sha256"]:
        raise RuntimeError("returned-point fine descriptor background differs from its report")
    fine_descriptor = {"meta": fine_meta, "arrays": fine_arrays, "scalars": fine_scalars,
                       "text_meta": fine_text, "column": cf, "base": fine_arrays["base"]}
    mod.check_initial_admissibility((16, 12, 8), center, fine_descriptor)
    mapped_center = fine_arrays["base"] + Bf @ controls

    obs_lines = observations_path.read_text().splitlines()
    obs_rows = np.asarray([[float(x) for x in row.split()] for row in obs_lines[2:]], dtype=np.float64)
    sigma = float(baseline_meta["physical_observation_sigma_m_s"])
    if (len(obs_lines) != 107 or obs_lines[:2] != ["PHYSICAL_WAVE_OBSERVATIONS_V1", "105"] or
            obs_rows.shape != (105, 5) or sigma != 3.0e-4 or
            not np.array_equal(obs_rows[:, 3], baseline_arrays["observed_150"]) or
            not np.array_equal(obs_rows[:, 4], baseline_arrays["observed_300"])):
        raise RuntimeError("returned point changed the frozen observations or sigma")
    xyz = obs_rows[:, :3]
    baseline_h_transpose = physical_h_transpose_checks(mod, baseline_arrays, fine_descriptor, xyz)
    h10_h5_prediction_rms = {str(t): float(np.sqrt(np.mean(
        (baseline_arrays[f"predicted_{t}"]-h10_arrays[f"predicted_{t}"])**2))) for t in (150, 300)}

    g_saved = Bf.T @ baseline_arrays["initial_pullback"]
    g_fresh = fresh_Bf.T @ baseline_arrays["initial_pullback"]
    sigma_gradient = report_gradient
    projection_gamma = fresh_Bf.shape[0]*np.finfo(float).eps/(1.0-fresh_Bf.shape[0]*np.finfo(float).eps)
    projection_bound = projection_gamma*(np.abs(fresh_Bf).T @ np.abs(baseline_arrays["initial_pullback"]))
    report_gradient_delta = g_fresh-sigma_gradient
    projection_guard_passed = bool(np.all(np.abs(report_gradient_delta)<=projection_bound))
    report_gradient_norm = float(opt["gradient_norm"])
    gradient_norm_delta = abs(float(np.linalg.norm(g_fresh))-report_gradient_norm)
    gradient_norm_bound = float(np.linalg.norm(projection_bound))
    gradient_norm_guard_passed = gradient_norm_delta <= gradient_norm_bound
    probe_environment = {"system": platform.system(), "python": platform.python_version(),
                         "numpy": np.__version__, "scipy": scipy.__version__,
                         "openblas_threads": os.environ.get("OPENBLAS_NUM_THREADS"),
                         "omp_threads": os.environ.get("OMP_NUM_THREADS")}
    same_projection_environment = (probe_environment["system"] == "Linux" and
                                   probe_environment["python"] == artifacts.get("python") and
                                   probe_environment["numpy"] == artifacts.get("numpy") and
                                   probe_environment["scipy"] == REFERENCE_SCIPY_VERSION and
                                   probe_environment["openblas_threads"] == "1" and
                                   probe_environment["omp_threads"] == "1")
    if same_projection_environment and not projection_guard_passed:
        raise RuntimeError("main12c returned gradient does not reproduce with the same fresh Bf summation bound")
    if same_projection_environment and not gradient_norm_guard_passed:
        raise RuntimeError("fresh-Bf K10 gradient norm differs from main12c report beyond summation bound")
    mapped_center = fine_arrays["base"]+fresh_Bf@controls
    center_mapping_delta = mapped_center-center
    state_gamma = (fresh_Bf.shape[1]+1)*np.finfo(float).eps/(1.0-(fresh_Bf.shape[1]+1)*np.finfo(float).eps)
    state_mapping_bound = state_gamma*(np.abs(fine_arrays["base"])+np.abs(fresh_Bf)@np.abs(controls))
    center_mapping_exact = bool(np.array_equal(mapped_center, center))
    center_mapping_bound_passed = bool(np.all(np.abs(center_mapping_delta)<=state_mapping_bound))
    center_mapping_status = ("exact" if center_mapping_exact else
        "within_componentwise_operation_bound" if center_mapping_bound_passed else
        "unresolved_basis_reconstruction_difference")
    preflight_status = ("preflight_passed_zero_native" if same_projection_environment and
                        projection_guard_passed and gradient_norm_guard_passed and
                        center_mapping_status != "unresolved_basis_reconstruction_difference" else
                        "preflight_unresolved_cross_environment_basis" if not same_projection_environment else
                        "preflight_unresolved_center_mapping")
    if same_projection_environment and center_mapping_status == "unresolved_basis_reconstruction_difference":
        raise RuntimeError("same-environment fresh Bf does not reproduce the returned state")
    direction = np.asarray(mod.FD_DIRECTION, dtype=np.float64)
    inputs = {"coarse_descriptor": coarse_path, "fine_descriptor": fine_path,
              "observations": observations_path, "state": state_path,
              "baseline": baseline_path}
    preflight = {
        "schema": "pr281-returned-state-n14-k12-fd-v1",
        "status": preflight_status,
        "diagnostic_only": True, "optimizer_return_claim": True,
        "source": {"probe_head": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                    capture_output=True, text=True, check=True).stdout.strip(),
                    "reference_run_id": REFERENCE_RUN_ID,
                    "reference_source_revision": artifacts["source_revision"],
                    "reference_source_tree": receipt["merge_commit"]["tree"],
                    "reference_artifact_id": receipt["artifact"]["id"],
                    "reference_artifact_name": receipt["artifact"]["name"],
                    "reference_artifact_sha256": receipt["archive_sha256"],
                    "reference_api_receipt": receipt,
                    "reference_api_receipt_sha256": common.sha256(receipt_path),
                    "reference_runner_sha256": artifacts["runner_sha256"],
                    "reference_source_sha256": artifacts["source_reference_sha256"],
                    "reference_transport_sha256": artifacts["transport_reference_sha256"],
                    "reference_report_sha256": common.sha256(report_path),
                    "executable_sha256_from_reference": artifacts["executable_sha256"],
                    "reference_environment": {"python": artifacts.get("python"),
                        "numpy": artifacts.get("numpy"), "scipy": REFERENCE_SCIPY_VERSION,
                        "openblas_threads": "1", "omp_threads": "1"},
                    "probe_environment": probe_environment,
                    "same_projection_environment": same_projection_environment,
                    "hardware_equivalence_asserted": False,
                    "source_file_sha256": source_pins},
        "returned_point": {"status": opt["status"], "converged": opt["converged"],
                    "controls": controls.tolist(), "controls_sha256": mod.digest(controls),
                    "state_path": str(state_path), "state_file_sha256": common.sha256(state_path),
                    "state_array_sha256": mod.digest(center), "baseline_csv_sha256": common.sha256(baseline_path),
                    "objective": float(baseline_scalars["objective_physical_w"]),
                    "fresh_Bf_center_reconstruction_exact": center_mapping_exact,
                    "fresh_Bf_center_reconstruction_status": center_mapping_status,
                    "fresh_Bf_center_reconstruction_residual_l2": float(np.linalg.norm(center_mapping_delta)),
                    "fresh_Bf_center_reconstruction_max_error_over_bound": float(np.max(
                        np.divide(np.abs(center_mapping_delta), state_mapping_bound,
                                  out=np.zeros_like(center_mapping_delta), where=state_mapping_bound>0.0))),
                    "fresh_Bf_center_reconstruction_bound_passed": center_mapping_bound_passed,
                    "reported_gradient": sigma_gradient.tolist(),
                    "reported_gradient_norm": float(opt["gradient_norm"])},
        "inputs": {key: {"path": str(path), "sha256": common.sha256(path)} for key, path in inputs.items()},
        "source_basis": {"saved_Bf_sha256": hashlib.sha256(Bf.tobytes()).hexdigest(),
                    "fresh_Bf_sha256": hashlib.sha256(fresh_Bf.tobytes()).hexdigest(),
                    "per_column_l2_delta": np.linalg.norm(fresh_Bf-Bf, axis=0).tolist(),
                    "per_column_max_abs_delta": np.max(np.abs(fresh_Bf-Bf), axis=0).tolist(),
                    "K10_g_saved_Bf": g_saved.tolist(), "K10_g_fresh_Bf": g_fresh.tolist(),
                    "report_gradient_minus_fresh_Bf": report_gradient_delta.tolist(),
                    "report_gradient_norm": report_gradient_norm,
                    "fresh_Bf_gradient_norm": float(np.linalg.norm(g_fresh)),
                    "gradient_norm_delta": gradient_norm_delta,
                    "gradient_norm_bound": gradient_norm_bound,
                    "gradient_norm_guard_passed": gradient_norm_guard_passed,
                    "same_basis_projection_gamma_n": projection_gamma,
                    "same_basis_projection_bound": projection_bound.tolist(),
                    "same_basis_projection_guard_passed": projection_guard_passed,
                    "interpretation": "fresh main12c Bf drives returned-point K10/K12 projections and FD; saved Mac Bf is comparison-only"},
        "baseline": {"observation_sha256": common.sha256(observations_path), "sigma_m_s": sigma,
                    "projected_gradient_fresh_Bf": g_fresh.tolist(),
                    "projected_gradient_norm_fresh_Bf": float(np.linalg.norm(g_fresh)),
                    "saved_Bf_comparison_gradient": g_saved.tolist(),
                    "physical_H_transpose_W_PH": baseline_h_transpose},
        "existing_returned_h10_h5_comparison": {"h10_csv_sha256": common.sha256(h10_path),
                    "h10_objective": float(h10_scalars["objective_physical_w"]),
                    "h5_objective": float(baseline_scalars["objective_physical_w"]),
                    "objective_delta_h10_minus_h5": float(h10_scalars["objective_physical_w"]-
                                                             baseline_scalars["objective_physical_w"]),
                    "prediction_rms_h10_minus_h5": h10_h5_prediction_rms},
        "planned_native_calls": ["one N14/K12 full VJP at literal returned state",
                    "one N14/K10 forward-only at center", "four N14/K10 forward-only FD trials"],
        "actual_native_calls": 0,
    }
    return mod, {"Bf": fresh_Bf, "saved_Bf": Bf, "center": center, "controls": controls,
                 "state_path": state_path, "observations_path": observations_path,
                 "baseline_meta": baseline_meta, "baseline_arrays": baseline_arrays,
                 "baseline_scalars": baseline_scalars, "baseline_text": baseline_text,
                 "baseline_h_transpose": baseline_h_transpose, "xyz": xyz,
                 "baseline_gradient_fresh_Bf": g_fresh, "baseline_gradient_saved_Bf": g_saved,
                 "baseline_gradient_report_norm": report_gradient_norm,
                 "baseline_gradient_norm_bound": gradient_norm_bound,
                 "direction": direction,
                 "sigma": sigma, "fine_descriptor": fine_descriptor}, preflight


def run(fixture: Path, fixture_zip: Path, reference_root: Path, reference_zip: Path,
        receipt_path: Path, exe: Path | None, outdir: Path, dry: bool) -> dict:
    mod, context, report = prepare(fixture, fixture_zip, reference_root, reference_zip, receipt_path)
    outdir.mkdir(parents=True, exist_ok=True)
    if report["status"] != "preflight_passed_zero_native":
        (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True)+"\n")
        if not dry:
            raise RuntimeError("returned-state fresh-Bf preflight is unresolved; no native calls allowed")
        return report
    if dry:
        (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        return report
    if exe is None or not exe.is_file():
        raise RuntimeError("native executable not found")
    report["source"]["probe_executable_sha256"] = common.sha256(exe)
    cache = exe.parent / "CMakeCache.txt"
    report["source"]["probe_cmake_cache_sha256"] = common.sha256(cache) if cache.is_file() else None
    report["status"] = "running_diagnostic_only"
    report["actual_native_calls"] = 0
    report["native_call_records"] = []
    report["results"] = {}
    (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    logs = outdir / "logs"

    def invoke(label: str, state_path: Path, krylov: str, forward_only: bool):
        output = outdir / "results" / f"{label}.csv"
        output.parent.mkdir(parents=True, exist_ok=True)
        args = ["--physical-wave-inverse", str(state_path), str(context["observations_path"]),
                "60", "5", "--newton-tol", "1e-14", "--krylov-tol", krylov]
        if forward_only:
            args.append("--forward-only")
        call = common.run_native(exe, (16, 12, 8), output, args, logs, label)
        report["native_call_records"].append(call)
        report["actual_native_calls"] = len(report["native_call_records"])
        (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        if call["return_code"] != 0 or not output.is_file():
            raise RuntimeError(f"{label} failed; no retry")
        meta, arrays, scalars, text_meta = mod.read_payload(output)
        if not np.array_equal(arrays.get("initial_state"), common.load_vector(state_path)):
            raise RuntimeError(f"{label} output changed its literal input state")
        expected_tol = float(np.float32(float(krylov)))
        if (meta.get("physical_wave_newton_tol") != float(np.float32(1e-14)) or
                meta.get("physical_wave_krylov_tol") != expected_tol or
                meta.get("trajectory_steps") != 60.0 or meta.get("trajectory_dt_fp32") != 5.0 or
                meta.get("forward_only") != (1.0 if forward_only else 0.0) or
                meta.get("pullback_requested") != (0.0 if forward_only else 1.0) or
                meta.get("retains_tape") != 1.0 or
                text_meta.get("checkpoint_precision") != "retained_fp64_trajectory" or
                text_meta.get("physical_observation_domain_guard") != "passed"):
            raise RuntimeError(f"{label} metadata/state contract mismatch")
        if (not all(np.isfinite(a).all() for a in arrays.values()) or
                not all(np.isfinite(float(v)) for v in scalars.values())):
            raise RuntimeError(f"{label} contains nonfinite values")
        if (not np.array_equal(arrays["observed_150"], context["baseline_arrays"]["observed_150"]) or
                not np.array_equal(arrays["observed_300"], context["baseline_arrays"]["observed_300"])):
            raise RuntimeError(f"{label} changed frozen observations")
        return meta, arrays, scalars, text_meta

    baseline = context["baseline_arrays"]
    k12_meta, k12_arrays, k12_scalars, k12_text = invoke("center_N14_K12_full_vjp", context["state_path"], "1e-12", False)
    if "initial_pullback" not in k12_arrays:
        raise RuntimeError("K12 full VJP omitted initial_pullback")
    k10f_meta, k10f_arrays, k10f_scalars, _ = invoke("center_N14_K10_forward_only", context["state_path"], "1e-10", True)
    forward_hashes = {key: {"K10_full_vjp_sha256": hashlib.sha256(baseline[key].tobytes()).hexdigest(),
                     "K10_forward_only_sha256": hashlib.sha256(k10f_arrays[key].tobytes()).hexdigest(),
                     "K12_full_vjp_sha256": hashlib.sha256(k12_arrays[key].tobytes()).hexdigest(),
                     "K10_forward_exact": np.array_equal(baseline[key], k10f_arrays[key]),
                     "K12_forward_exact": np.array_equal(baseline[key], k12_arrays[key])}
                     for key in common.FORWARD_KEYS}
    k12_forward_exact = all(x["K12_forward_exact"] for x in forward_hashes.values())
    if not all(x["K10_forward_exact"] for x in forward_hashes.values()):
        raise RuntimeError("K10 forward-only center differs from the saved full-VJP trajectory")
    if k10f_scalars["objective_physical_w"] != context["baseline_scalars"]["objective_physical_w"]:
        raise RuntimeError("K10 forward-only center objective differs from the saved full-VJP objective")
    k10_gradient = context["baseline_gradient_fresh_Bf"]
    k12_gradient = context["Bf"].T @ k12_arrays["initial_pullback"]
    k12_h_transpose = physical_h_transpose_checks(
        mod, k12_arrays, context["fine_descriptor"], context["xyz"])
    saved_k10_gradient = context["baseline_gradient_saved_Bf"]
    saved_k12_gradient = context["saved_Bf"].T @ k12_arrays["initial_pullback"]
    gradient_delta = k12_gradient-k10_gradient
    k10_gradient_norm = float(np.linalg.norm(k10_gradient))
    k12_gradient_norm = float(np.linalg.norm(k12_gradient))
    gradient_delta_norm = float(np.linalg.norm(gradient_delta))
    directional_vjp = {"K10":float(k10_gradient@context["direction"]),
                       "K12":float(k12_gradient@context["direction"])}

    sigma = context["sigma"]
    center_cotangent = {t: (baseline[f"predicted_{t}"]-baseline[f"observed_{t}"])/(sigma*sigma)
                        for t in (150, 300)}
    center_cotangent_bytes = np.concatenate([center_cotangent[t] for t in (150, 300)])
    fine_desc = context["fine_descriptor"]
    direction = context["direction"]
    runs = {}
    for eps in EPSILONS:
        sides = {}
        for sign, side in ((1.0, "plus"), (-1.0, "minus")):
            delta = sign*eps*direction
            trial_state = context["center"] + context["Bf"] @ delta
            mod.check_initial_admissibility((16, 12, 8), trial_state, fine_desc)
            state_path = outdir / "generated_inputs" / f"fd_{eps:g}_{side}.txt"
            state_path.parent.mkdir(parents=True, exist_ok=True)
            mod.write_vector(state_path, trial_state)
            if not np.array_equal(common.load_vector(state_path), trial_state):
                raise RuntimeError("FD trial-state round trip changed bytes")
            meta, arrays, scalars, _ = invoke(f"fd_K10_{eps:g}_{side}", state_path, "1e-10", True)
            brackets = all(np.array_equal(arrays[f"bracket_index_{t}"], baseline[f"bracket_index_{t}"])
                           for t in (150, 300))
            face = min(meta["minimum_height_to_any_W_face_150_m"], meta["minimum_height_to_any_W_face_300_m"])
            if not brackets or int(meta["bracket_corner_changes_150_to_300"]) != 0 or face <= 0.0:
                raise RuntimeError(f"strict FD bracket/height guard failed at {eps:g}/{side}")
            sides[side] = {"objective": float(scalars["objective_physical_w"]), "arrays": arrays,
                           "state_sha256": mod.digest(trial_state), "face_min_m": face}
        runs[str(eps)] = sides

    fd_rows = {}
    n_terms = sum(baseline[f"predicted_{t}"].size for t in (150, 300))
    gamma = n_terms*np.finfo(float).eps/(1.0-n_terms*np.finfo(float).eps)
    for eps in EPSILONS:
        pair = runs[str(eps)]; plus = pair["plus"]; minus = pair["minus"]
        raw_fd = (plus["objective"]-minus["objective"])/(2.0*eps)
        dual_fd = sum(float(np.dot(center_cotangent[t], plus["arrays"][f"predicted_{t}"]-
                                   minus["arrays"][f"predicted_{t}"])) for t in (150, 300))/(2.0*eps)
        operand = sum(float(0.5*np.sum(((np.abs(pair[side]["arrays"][f"predicted_{t}"])+
                               np.abs(baseline[f"observed_{t}"]))/sigma)**2))
                      for side in ("plus", "minus") for t in (150, 300))
        dual_operand = sum(float(np.sum(np.abs(center_cotangent[t])*(
                               np.abs(plus["arrays"][f"predicted_{t}"])+
                               np.abs(minus["arrays"][f"predicted_{t}"])))) for t in (150, 300))
        fd_rows[str(eps)] = {"raw_objective_fd": raw_fd, "frozen_center_dual_fd": dual_fd,
            "objective_operand_floor": gamma*operand/(2.0*eps),
            "dual_operand_floor": gamma*dual_operand/(2.0*eps),
            "plus_objective": plus["objective"], "minus_objective": minus["objective"],
            "plus_state_sha256": plus["state_sha256"], "minus_state_sha256": minus["state_sha256"],
            "minimum_face_distance_m": min(plus["face_min_m"], minus["face_min_m"])}
    def fd_trend(key, floor_key):
        wide, narrow = fd_rows["0.01"][key], fd_rows["0.005"][key]
        Ewide, Enarrow = fd_rows["0.01"][floor_key], fd_rows["0.005"][floor_key]
        trunc = abs(narrow-wide)/3.0
        roundoff = (4.0*Enarrow+Ewide)/3.0
        estimate = (4.0*narrow-wide)/3.0
        uncertainty = trunc+roundoff
        return {"wide":wide,"narrow":narrow,"richardson_O_eps2_assumption":estimate,
                "truncation_proxy":trunc,"richardson_roundoff_floor":roundoff,
                "combined_uncertainty_arithmetic_plus_truncation_proxy":uncertainty,
                "estimate_minus_directional_vjp":{"K10":estimate-directional_vjp["K10"],
                    "K12":estimate-directional_vjp["K12"]},
                "near_zero_unresolved":bool(abs(estimate)<=uncertainty),
                "relative_accuracy_asserted":False}
    report.update({"status":"completed_diagnostic_only", "actual_native_calls":len(report["native_call_records"]),
        "K12_full_vjp_contract":{"forward_only":k12_meta["forward_only"],
            "pullback_requested":k12_meta["pullback_requested"],"retains_tape":k12_meta["retains_tape"],
            "effective_krylov_tolerance":k12_meta["physical_wave_krylov_tol"],
            "raw_initial_pullback_sha256":hashlib.sha256(k12_arrays["initial_pullback"].tobytes()).hexdigest(),
            "physical_H_transpose_W_PH":k12_h_transpose},
        "K10_forward_only_contract":{"forward_only":k10f_meta["forward_only"],
            "pullback_requested":k10f_meta["pullback_requested"],"objective":float(k10f_scalars["objective_physical_w"])},
        "forward_hashes":forward_hashes,
        "K12_forward_fields_identical":k12_forward_exact,
        "K12_forward_identity_interpretation":(
            "same forward fields; gradient delta is in transpose/VJP solve or arithmetic" if k12_forward_exact
            else "forward fields differ; gradient comparison includes combined forward and transpose sensitivity"),
        "projected_gradients":{"K10_fresh_Bf":k10_gradient.tolist(),
            "K12_fresh_Bf":k12_gradient.tolist(),"K12_minus_K10_fresh_Bf":gradient_delta.tolist(),
            "K10_norm_fresh_Bf":k10_gradient_norm,
            "K10_norm_main12c_report":context["baseline_gradient_report_norm"],
            "K10_norm_report_match_delta":abs(k10_gradient_norm-context["baseline_gradient_report_norm"]),
            "K10_norm_report_match_bound":context["baseline_gradient_norm_bound"],
            "K10_norm_report_match_guard_passed":True,
            "K12_norm_fresh_Bf":k12_gradient_norm,
            "K12_minus_K10_norm_fresh_Bf":gradient_delta_norm,
            "gate_threshold":1.0e-5,
            "K12_stationarity_norm_below_1e_minus_5":bool(k12_gradient_norm<1.0e-5),
            "K12_delta_norm_below_1e_minus_5":bool(gradient_delta_norm<1.0e-5),
            "saved_Bf_comparison":{"K10":saved_k10_gradient.tolist(),"K12":saved_k12_gradient.tolist(),
                "K10_fresh_minus_saved":(k10_gradient-saved_k10_gradient).tolist(),
                "K12_fresh_minus_saved":(k12_gradient-saved_k12_gradient).tolist()}},
        "finite_difference":{"direction":direction.tolist(),"direction_sha256":mod.digest(direction),
            "center_state_sha256":mod.digest(context["center"]),"epsilons":list(EPSILONS),
            "runs":fd_rows,"raw_objective_trend":fd_trend("raw_objective_fd","objective_operand_floor"),
            "frozen_center_dual_trend":fd_trend("frozen_center_dual_fd","dual_operand_floor"),
            "directional_vjp_fresh_Bf":{"K10":directional_vjp["K10"],
                "K12":directional_vjp["K12"],
                "K12_minus_K10":directional_vjp["K12"]-directional_vjp["K10"]},
            "center_cotangent_sha256":mod.digest(center_cotangent_bytes),
            "interpretation":"Richardson assumes centered O(eps^2); unresolved near-zero estimates stay open."},
        "optimizer_return_claim":True})
    (outdir / "probe_summary.json").write_text(json.dumps(report, indent=2, sort_keys=True)+"\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-root", type=Path, required=True)
    parser.add_argument("--fixture-zip", type=Path, required=True)
    parser.add_argument("--finalpoint-artifact-root", type=Path, required=True)
    parser.add_argument("--reference-artifact-zip", type=Path, required=True)
    parser.add_argument("--reference-receipt", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--dry-preflight", action="store_true")
    args = parser.parse_args()
    if not args.dry_preflight and args.exe is None:
        parser.error("--exe is required unless --dry-preflight is selected")
    outdir=args.output_dir.resolve()
    try:
        report=run(args.fixture_root.resolve(),args.fixture_zip.resolve(),
                   args.finalpoint_artifact_root.resolve(),args.reference_artifact_zip.resolve(),
                   args.reference_receipt.resolve(),args.exe.resolve() if args.exe else None,
                   outdir,args.dry_preflight)
    except Exception as exc:
        summary=outdir/"probe_summary.json"
        if summary.is_file():
            partial=json.loads(summary.read_text()); partial["status"]="incomplete_diagnostic_only"
            partial["error"]=repr(exc); summary.write_text(json.dumps(partial,indent=2,sort_keys=True)+"\n")
        raise
    print(json.dumps({"status":report["status"],"actual_native_calls":report.get("actual_native_calls",0)},sort_keys=True))
    return 0 if report["status"] == "preflight_passed_zero_native" else 2


if __name__=="__main__":
    raise SystemExit(main())
