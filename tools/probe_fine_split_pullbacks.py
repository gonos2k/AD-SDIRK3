#!/usr/bin/env python3
"""Compare split h5 time pullbacks with pinned, already-computed per-time FD responses."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import subprocess
import sys
import zipfile

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import test_fine_wave_inverse as inverse  # noqa: E402

FIXTURE_SHA = "e170be5d1e2b0d87efcdc9ebe0f4dd1d381aceb9e0777dcd074e0dcfe1329d8c"
CPP_SHA = "e1cb768ac81f10872817af3c05ed34da5ce6b5ca818b0eba462e12dcf05e2e3a"
HEADER_SHA = "244970b6c874ef50d3fc85477d463bc846c32b440a9ff834740590728cb9439f"
IMPL_SHA = "c0fc5c3fd7aa0e46c0f9f044e8405fc0f203209156f1a15a4629622a5cfac99c"
H5_CENTER_SHA = "c21feea5512d8c4974c3031a6b4f98e57d3a03f1280c0095d844272a6f0415d7"
FD_ZIP_SHA = "a21609ddb1ec0839279dbd712a53fae5bdfd26e426d7570f70fa55158be0d7c8"
FD_REPORT_SHA = "27f0b7b951827147bebb1906fb9617785ba4dd5f57ab8fa5f0330b246e15ae8f"
FD_BASIS_FILE_SHA = "46ad1ad64ee645aedaab81d505c9e1467e384784d17a0adc1eebd1ec80b5e686"
FD_CPP_SHA = "a058e570e1527b5167c0f999188ecc3f520c48a6421cf18c8f2c7098320b9967"
GRID = (16, 12, 8)
SIGMA = 3.0e-4
EPSILONS = (0.005, 0.0025)
TIMES = (150, 300)
CONTEXT_ARRAYS = ("base", "phb", "pbase", "thbase_perturb", "mubase")
def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    return sha_bytes(path.read_bytes())


def write_report(path, report):
    path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")


def read_literal_vector(path):
    words = path.read_text().split()
    values = np.asarray([float(value) for value in words[1:]], dtype=np.float64)
    if int(words[0]) != values.size or not np.isfinite(values).all():
        raise ValueError(f"invalid literal state vector: {path}")
    return values


def read_fd_bundle(fd_zip, outdir, state):
    if sha(fd_zip) != FD_ZIP_SHA:
        raise ValueError("unexpected pinned h5 component-FD artifact ZIP")
    with zipfile.ZipFile(fd_zip) as archive:
        if archive.testzip() is not None:
            raise ValueError("component-FD artifact ZIP failed CRC verification")
        report_bytes = archive.read("measurement/report.json")
        if sha_bytes(report_bytes) != FD_REPORT_SHA:
            raise ValueError("unexpected component-FD report")
        report = json.loads(report_bytes)
        if (report.get("schema") != "fine-returned-gradient-components-v1" or
                report.get("status") != "completed_four_coordinate_forward_diagnostic" or
                report.get("fixture_sha256") != FIXTURE_SHA or
                report.get("h5_reference_csv_sha256") != H5_CENTER_SHA or
                report.get("source_cpp_sha256") != FD_CPP_SHA or
                report.get("epsilons") != list(EPSILONS) or
                report.get("coordinate_count") != 4 or
                report.get("optimizer_run") is not False or
                report.get("observations_changed") is not False or
                report.get("completed_perturbations") != 16):
            raise ValueError("component-FD report provenance or scope mismatch")

        basis_bytes = archive.read("measurement/inputs/canonical_basis.npy")
        if sha_bytes(basis_bytes) != FD_BASIS_FILE_SHA:
            raise ValueError("unexpected archived canonical basis file")
        basis = np.load(io.BytesIO(basis_bytes), allow_pickle=False)
        if (basis.shape != (8480, 4) or basis.dtype != np.float64 or
                not np.isfinite(basis).all() or
                inverse.digest(basis) != report.get("canonical_basis_sha256")):
            raise ValueError("archived canonical basis array pin mismatch")
        expected = {f"eps{eps:g}_j{j}_{side}" for eps in EPSILONS
                    for j in range(basis.shape[1]) for side in ("plus", "minus")}
        calls = {row["label"]: row for row in report.get("calls", []) if "label" in row}
        if not expected.issubset(calls):
            raise ValueError("component-FD report is missing pinned endpoints")

        endpoints = {}
        reference_dir = outdir / "fd_reference"
        reference_dir.mkdir(parents=True, exist_ok=True)
        for label in sorted(expected):
            raw = archive.read(f"measurement/{label}.csv")
            if sha_bytes(raw) != calls[label].get("output_sha256"):
                raise ValueError(f"component-FD endpoint SHA mismatch: {label}")
            path = reference_dir / f"{label}.csv"
            path.write_bytes(raw)
            meta, arrays, scalars, _ = inverse.read_payload(path)
            eps_text, j_text, side = label.split("_")
            eps, j = float(eps_text[3:]), int(j_text[1:])
            delta = (1.0 if side == "plus" else -1.0) * eps * basis[:, j]
            if (not np.array_equal(arrays.get("initial_state"), state + delta) or
                    meta.get("trajectory_steps") != 60 or meta.get("trajectory_dt_fp32") != 5.0 or
                    meta.get("physical_observation_sigma_m_s") != SIGMA or
                    meta.get("forward_only") != 1 or meta.get("pullback_requested") != 0 or
                    "initial_pullback" in arrays or not all(np.isfinite(v).all() for v in arrays.values()) or
                    not all(np.isfinite(v) for v in scalars.values())):
                raise ValueError(f"component-FD endpoint contract mismatch: {label}")
            for t in TIMES:
                if not np.isfinite(arrays[f"predicted_{t}"]).all():
                    raise ValueError(f"nonfinite component-FD prediction: {label}, t={t}")
            endpoints[(eps, j, side)] = arrays

    first = endpoints[(EPSILONS[0], 0, "minus")]
    for values in endpoints.values():
        for t in TIMES:
            if (not np.array_equal(values[f"observed_{t}"], first[f"observed_{t}"]) or
                    not np.array_equal(values[f"bracket_index_{t}"], first[f"bracket_index_{t}"])):
                raise ValueError(f"component-FD observation/bracket drift: t={t}")
        for key in CONTEXT_ARRAYS:
            if not np.array_equal(values[key], first[key]):
                raise ValueError(f"component-FD context drift: {key}")
    return report, basis, endpoints


def verify_fd_center(endpoints, center_arrays):
    for (eps, j, side), values in endpoints.items():
        label = f"eps{eps:g}_j{j}_{side}"
        for t in TIMES:
            if (not np.array_equal(values[f"observed_{t}"], center_arrays[f"observed_{t}"]) or
                    not np.array_equal(values[f"bracket_index_{t}"], center_arrays[f"bracket_index_{t}"])):
                raise ValueError(f"component-FD observation/bracket mismatch: {label}, t={t}")
        for key in CONTEXT_ARRAYS:
            if not np.array_equal(values[key], center_arrays[key]):
                raise ValueError(f"component-FD context mismatch: {label}, {key}")

def main(exe, fixture_zip, outdir, fd_zip):
    if sha(fixture_zip) != FIXTURE_SHA:
        raise ValueError("unexpected pinned returned-state fixture ZIP")
    if sha(ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp") != CPP_SHA:
        raise ValueError("native split-pullback source SHA mismatch")
    if (sha(ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified.h") != HEADER_SHA or
            sha(ROOT / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp") != IMPL_SHA):
        raise ValueError("native solver header/implementation source SHA mismatch")
    outdir.mkdir(parents=True, exist_ok=True)
    inputs = outdir / "inputs"
    inputs.mkdir(exist_ok=True)
    with zipfile.ZipFile(fixture_zip) as archive:
        if archive.testzip() is not None:
            raise ValueError("returned-state fixture ZIP failed CRC verification")
        manifest = json.loads(archive.read("manifest.json"))
        if manifest.get("schema") != "pr282-fixed-literal-incremental-controls-v3":
            raise ValueError("unexpected literal-control fixture schema")
        for name, record in manifest["files"].items():
            raw = archive.read(name)
            if sha_bytes(raw) != record["sha256"]:
                raise ValueError(f"fixture member SHA mismatch: {name}")
            (inputs / name).write_bytes(raw)

    state = read_literal_vector(inputs / "initial_state.txt")
    fd_report, basis, endpoints = read_fd_bundle(fd_zip, outdir, state)
    if (basis.shape != (8480, 4) or basis.dtype != np.float64 or
            not np.isfinite(basis).all() or
            inverse.digest(state) != manifest["initial_array_sha256"]):
        raise ValueError("literal state or canonical basis failed its pin")
    baseline_path = inputs / "baseline_h5.csv"
    if sha(baseline_path) != H5_CENTER_SHA:
        raise ValueError("unexpected pinned h5 center VJP CSV")
    _, baseline_arrays, baseline_scalars, _ = inverse.read_payload(baseline_path)

    out_csv = outdir / "h5_split_pullbacks.csv"
    command = [str(exe), *map(str, GRID), str(out_csv), "--physical-wave-inverse",
               str(inputs / "initial_state.txt"), str(inputs / "observations.txt"),
               "60", "5.0", "--newton-tol", "1e-14", "--krylov-tol", "1e-12",
               "--split-observation-pullbacks"]
    (outdir / "native.command.json").write_text(json.dumps(command, indent=2) + "\n")
    result = subprocess.run(command, capture_output=True, text=True)
    (outdir / "native.stdout.log").write_text(result.stdout)
    (outdir / "native.stderr.log").write_text(result.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"split h5 center run failed with exit {result.returncode}")
    meta, arrays, scalars, text_meta = inverse.read_payload(out_csv)
    if (meta.get("trajectory_steps") != 60 or meta.get("trajectory_dt_fp32") != 5.0 or
            meta.get("physical_observation_sigma_m_s") != SIGMA or
            meta.get("forward_only") != 0 or meta.get("retains_tape") != 1 or
            meta.get("pullback_requested") != 1 or meta.get("internal_fp64") != 1 or
            meta.get("internal_fp64_state_carry") != 1 or
            meta.get("retain_graph_for_adjoint") != 1 or
            meta.get("split_observation_pullbacks") != 1 or
            text_meta.get("checkpoint_precision") != "retained_fp64_trajectory"):
        raise ValueError("native h5 split-pullback mode metadata mismatch")
    if not np.array_equal(arrays["initial_state"], state):
        raise ValueError("native run did not use the exact pinned literal initial state")
    if not np.array_equal(baseline_arrays["initial_state"], state):
        raise ValueError("historical baseline initial state differs from the pinned literal state")
    verify_fd_center(endpoints, arrays)
    required_pullbacks = ("initial_pullback", "initial_pullback_150", "initial_pullback_300")
    for key in required_pullbacks:
        if key not in arrays or not np.isfinite(arrays[key]).all():
            raise ValueError(f"native output is missing a finite {key}")

    split_sum = arrays["initial_pullback_150"] + arrays["initial_pullback_300"]
    sum_delta = split_sum - arrays["initial_pullback"]
    sum_error_l2 = float(np.linalg.norm(sum_delta))
    sum_error_rel = sum_error_l2 / max(float(np.linalg.norm(arrays["initial_pullback"])), np.finfo(float).tiny)
    if not np.isfinite(sum_error_l2) or not np.isfinite(sum_error_rel):
        raise ValueError("split pullback sum diagnostic is nonfinite")

    projected_ad = {
        str(t): (basis.T @ arrays[f"initial_pullback_{t}"]).tolist()
        for t in TIMES
    }
    report = {
        "schema": "pr282-h5-split-observation-pullbacks-v1",
        "status": "completed_native_split_pullbacks",
        "source_git_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_cpp_sha256": CPP_SHA,
        "source_header_sha256": HEADER_SHA,
        "source_implementation_sha256": IMPL_SHA,
        "executable_sha256": sha(exe),
        "fixture_sha256": FIXTURE_SHA,
        "h5_center_csv_sha256": sha(out_csv),
        "pinned_h5_center_csv_sha256": H5_CENTER_SHA,
        "grid": list(GRID), "steps": 60, "dt": 5.0, "sigma": SIGMA,
        "local_chart": "literal_saved_state + Bcan @ delta; no regenerated center or basis",
        "historical_baseline_scope": "input-state pin only; no cross-revision forecast/objective parity asserted",
        "canonical_basis_sha256": inverse.digest(basis),
        "projected_ad_gradient_by_time": projected_ad,
        "projected_ad_gradient_sum": (basis.T @ split_sum).tolist(),
        "projected_ad_gradient_combined": (basis.T @ arrays["initial_pullback"]).tolist(),
        "pullback_sum_consistency": {"l2_error": sum_error_l2, "relative_error": sum_error_rel,
                                     "native_diagnostic_l2_error": scalars["split_pullback_sum_error_l2"],
                                     "native_diagnostic_relative_error": scalars["split_pullback_sum_error_relative"]},
        "fd_comparison": None,
        "optimizer_run": False,
        "new_fd_runs": 0,
        "gradient_certification": "not claimed; split-time diagnostic only",
        "claim": "per-time adjoint and cross-revision FD diagnostic; no full Eg or stationarity certification",
    }

    if fd_zip is not None:
        fd_by_time = {}
        response_vectors = {}
        for t in TIMES:
            residual = arrays[f"predicted_{t}"] - arrays[f"observed_{t}"]
            eps_rows = {}
            response_vectors[str(t)] = {}
            for eps in EPSILONS:
                fd_gradient = []
                response_by_axis = []
                for j in range(basis.shape[1]):
                    p_plus = endpoints[(eps, j, "plus")][f"predicted_{t}"]
                    p_minus = endpoints[(eps, j, "minus")][f"predicted_{t}"]
                    response = (p_plus - p_minus) / (2.0 * eps)
                    fd_gradient.append(float(np.dot(residual, response) / (SIGMA ** 2)))
                    response_by_axis.append({"axis": j, "l2": float(np.linalg.norm(response)),
                                             "max_abs": float(np.max(np.abs(response))),
                                             "values": response.tolist()})
                fd_vec = np.asarray(fd_gradient, dtype=np.float64)
                ad_vec = np.asarray(projected_ad[str(t)], dtype=np.float64)
                eps_rows[str(eps)] = {"frozen_center_residual_fd_gradient": fd_vec.tolist(),
                    "ad_gradient": ad_vec.tolist(), "fd_minus_ad": (fd_vec-ad_vec).tolist(),
                    "fd_minus_ad_l2": float(np.linalg.norm(fd_vec-ad_vec))}
                response_vectors[str(t)][str(eps)] = response_by_axis
            coarse = np.asarray(eps_rows[str(EPSILONS[0])]["frozen_center_residual_fd_gradient"])
            fine = np.asarray(eps_rows[str(EPSILONS[1])]["frozen_center_residual_fd_gradient"])
            eps_rows["width_difference"] = {"fine_minus_coarse": (fine-coarse).tolist(),
                                              "scope": "empirical width sensitivity only; not an error bound"}
            fd_by_time[str(t)] = eps_rows
        sum_fd = {str(eps): sum(np.asarray(fd_by_time[str(t)][str(eps)]["frozen_center_residual_fd_gradient"])
                                for t in TIMES).tolist() for eps in EPSILONS}
        ad_norms = {str(t): float(np.linalg.norm(projected_ad[str(t)])) for t in TIMES}
        total_ad = np.asarray(projected_ad["150"]) + np.asarray(projected_ad["300"])
        report["fd_comparison"] = {
            "fd_zip_sha256": sha(fd_zip),
            "fd_report_sha256": FD_REPORT_SHA,
            "fd_execution_run": 37913010518,
            "historical_fixture_producer_run": fd_report["baseline_provenance"]["source_run"],
            "fd_source_revision": fd_report["source_revision"],
            "fd_executable_sha256": fd_report["executable_sha256"],
            "existing_endpoint_calls_reused": 16,
            "new_fd_runs": 0,
            "fd_source_cpp_sha256": FD_CPP_SHA,
            "fd_comparison_scope": "cross-revision directional diagnostic at exact literal state/context/observations; not same-executable certification",
            "fd_gradient_by_time": fd_by_time,
            "fd_gradient_sum_by_width": sum_fd,
            "observation_response_tangent_by_time_and_axis": response_vectors,
            "ad_cancellation_ratio": float((ad_norms["150"]+ad_norms["300"])/max(float(np.linalg.norm(total_ad)), np.finfo(float).tiny)),
            "frozen_residual_definition": "r_t(center) dot (p_t(x+eps e_j)-p_t(x-eps e_j))/(2 eps sigma^2)",
            "fd_claim": "existing fixed-state endpoints only; width drift is diagnostic, not a rigorous bound",
        }
    write_report(outdir / "report.json", report)
    print(json.dumps({"status": report["status"], "report": str(outdir / "report.json"),
                      "split_sum_error_l2": sum_error_l2, "fd_loaded": fd_zip is not None}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--fd-zip", type=Path, required=True,
                        help="required pinned four-coordinate FD artifact; no FD calls are run")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    main(args.exe.resolve(), args.fixture.resolve(), args.out.resolve(), args.fd_zip.resolve())
