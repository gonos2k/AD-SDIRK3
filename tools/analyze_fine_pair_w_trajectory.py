#!/usr/bin/env python3
"""Compare a fine/coarse W trajectory pair under the frozen PR308 protocol.

This is an offline analysis tool. It reads WRF history files and receipts,
imports the hash/time helpers and CSV reader already used by the repository,
and never calls the model. All differences are descriptive; they are neither
acceptance results nor error bounds.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
from fractions import Fraction
import tempfile
from typing import Any

import numpy as np
from netCDF4 import Dataset

try:
    from benchmark_wrf_accuracy_cost import _sha256, _time_text
    from test_native_wave_refinement import read_payload
    from analyze_accepted_ark_w_stage import analyze as analyze_accepted_step, _blocks, _state_block
except ImportError:  # package import path
    from tools.benchmark_wrf_accuracy_cost import _sha256, _time_text
    from tools.test_native_wave_refinement import read_payload
    from tools.analyze_accepted_ark_w_stage import analyze as analyze_accepted_step, _blocks, _state_block


PROTOCOL_SCHEMA = "pr308_fine_pair_w_trajectory_protocol_v3"
ANALYSIS_SCHEMA = "pr308_fine_pair_w_trajectory_analysis_v1"
EPSILON_W_RMS_M_S = 7.7944398e-6
TRIGGER_M_S = 2.5981466e-6
HORIZON_S = 15.0
COARSE_H_S = 0.9375
FINE_H_S = 0.46875
COARSE_STEPS = 16
FINE_STEPS = 32
PHYSICAL_NX = 16
PHYSICAL_NY = 16
PHYSICAL_NZ = 16
W_OWNERS = 4096


def _resolve(base: Path, value: str) -> Path:
    p = Path(value).expanduser()
    return p.resolve() if p.is_absolute() else (base / p).resolve()


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _rms(x: np.ndarray) -> float:
    a = np.asarray(x, dtype=np.float64)
    if a.size == 0 or not np.isfinite(a).all():
        raise ValueError("RMS input is empty or non-finite")
    return float(np.sqrt(np.mean(a * a)))


def _stats(x: np.ndarray) -> dict[str, float | int]:
    a = np.asarray(x, dtype=np.float64)
    if a.size == 0 or not np.isfinite(a).all():
        raise ValueError("metric input is empty or non-finite")
    return {"count": int(a.size), "rms": float(np.sqrt(np.mean(a * a))),
            "max_abs": float(np.max(np.abs(a))), "mean": float(np.mean(a))}


def _receipt(run: dict, plan_dir: Path) -> tuple[dict, Path, Path]:
    label = run.get("label")
    if label not in ("coarse_h0p9375", "fine_h0p46875"):
        raise ValueError(f"unexpected run label {label!r}")
    receipt_path = _resolve(plan_dir, run["receipt"])
    receipt = json.loads(receipt_path.read_text())
    if receipt.get("status") != "completed":
        raise ValueError(f"{label}: run receipt is not completed")
    process = receipt.get("process_result", {})
    if process.get("return_code") != 0 or process.get("success_marker") is not True:
        raise ValueError(f"{label}: producer receipt does not show successful completion")
    case = receipt.get("case", {})
    expected_h = COARSE_H_S if label.startswith("coarse") else FINE_H_S
    expected_steps = COARSE_STEPS if label.startswith("coarse") else FINE_STEPS
    expected_den = 16 if label.startswith("coarse") else 32
    if (int(case.get("dt_numerator_seconds", -1)), int(case.get("dt_denominator", -1))) != (15, expected_den):
        raise ValueError(f"{label}: receipt timestep mismatch")
    if float(case.get("dt_seconds", -1.0)) != expected_h or \
       int(case.get("model_steps_expected", -1)) != expected_steps or \
       float(case.get("run_seconds", -1.0)) != HORIZON_S:
        raise ValueError(f"{label}: receipt duration/step-count mismatch")
    run_dir = _resolve(plan_dir, run["path"])
    if Path(receipt.get("working_directory", "")).resolve() != run_dir:
        raise ValueError(f"{label}: run directory does not match receipt working_directory")
    return receipt, receipt_path, run_dir


def _read_accepted_run(run: dict, plan_dir: Path) -> dict[str, Any]:
    receipt, receipt_path, run_dir = _receipt(run, plan_dir)
    expected_steps = COARSE_STEPS if run["label"].startswith("coarse") else FINE_STEPS
    paths = sorted(run_dir.glob("accepted_ark_w_step_*.csv"),
                   key=lambda p: int(p.stem.rsplit("_", 1)[1]))
    if len(paths) != expected_steps:
        raise ValueError(f"{run['label']}: accepted-step CSV count {len(paths)} != {expected_steps}")
    declared_csvs={x["filename"]:x["sha256"] for x in
                   receipt.get("actual",{}).get("accepted_stage_csvs",[])}
    if set(declared_csvs)!={p.name for p in paths}:
        raise ValueError(f"{run['label']}: receipt CSV manifest does not match accepted-step files")
    for p in paths:
        if _sha(p)!=declared_csvs[p.name]:
            raise ValueError(f"{run['label']}: accepted-step CSV hash mismatch: {p.name}")
    input_value = receipt.get("inputs", {}).get("path")
    input_path = _resolve(plan_dir, input_value) if input_value else run_dir / "wrfinput_d01"
    input_sha = receipt.get("inputs", {}).get("sha256")
    if not input_path.is_file() or not input_sha or _sha(input_path) != input_sha:
        raise ValueError(f"{run['label']}: receipt-bound initial input is absent or has a hash mismatch")

    nx, ny, nz = 17, 17, 16
    offsets = _blocks(nx, ny, nz)
    w_start, wshape = offsets["w"]
    w_size = math.prod(wshape)
    owner_shape = (PHYSICAL_NZ, PHYSICAL_NY, PHYSICAL_NX)
    step_records = []
    initial_state = None
    previous_projected = None
    continuity = []
    for step_index, path in enumerate(paths, 1):
        meta, arrays, _, text_meta = read_payload(path)
        if text_meta.get("schema") != "accepted_ark_w_stage_v2" or \
           text_meta.get("packed_order") != "U_V_W_PH_T_MU":
            raise ValueError(f"{run['label']} step {step_index}: unsupported stage CSV schema/order")
        if int(meta.get("step_seq", -1)) != step_index or \
           int(meta.get("host_timestep_token", -1)) != step_index:
            raise ValueError(f"{run['label']} step {step_index}: non-contiguous step/token sequence")
        if float(meta.get("dt_seconds", -1.0)) != float(run["h_seconds"]):
            raise ValueError(f"{run['label']} step {step_index}: dt differs from plan")
        if meta.get("accepted") != 1.0 or meta.get("accepted_outcome_code") != 0.0:
            raise ValueError(f"{run['label']} step {step_index}: not an accepted step")
        if int(meta.get("physical_owners_nx", -1)) != PHYSICAL_NX or \
           int(meta.get("physical_owners_ny", -1)) != PHYSICAL_NY:
            raise ValueError(f"{run['label']} step {step_index}: wrong physical owner dimensions")
        if (int(meta.get("nx", -1)), int(meta.get("ny", -1)), int(meta.get("nz", -1))) != (17,17,16):
            raise ValueError(f"{run['label']} step {step_index}: wrong packed state dimensions")
        def state(name: str) -> np.ndarray:
            if name not in arrays:
                raise ValueError(f"{run['label']} step {step_index}: missing {name}")
            x = np.asarray(arrays[name], dtype=np.float64)
            if x.size != offsets["_total"][0] or not np.isfinite(x).all():
                raise ValueError(f"{run['label']} step {step_index}: invalid packed state {name}")
            return x
        current_input = state("input")
        projected = state("projected_final")
        raw_final = state("raw_final")
        if initial_state is None:
            initial_state = current_input.copy()
        if previous_projected is not None:
            exact = np.array_equal(current_input, previous_projected)
            continuity.append({"step": step_index, "input_equals_previous_projected_all6_exact": exact,
                               "W_difference_max_abs_m_s": 0.0 if exact else float(np.max(np.abs(
                                   _state_block(current_input,"w",offsets)-
                                   _state_block(previous_projected,"w",offsets))))})
            if not exact:
                raise ValueError(f"{run['label']} step {step_index}: next input differs from previous projected FP64 state")
        # Internal state block is [j,k_face,i]; the frozen M0 direction and
        # NetCDF W are [k_face,j,i], so transpose explicitly before metrics.
        w_input = np.ascontiguousarray(_state_block(current_input, "w", offsets)[:PHYSICAL_NY,1:,:PHYSICAL_NX].transpose(1,0,2))
        w_raw = np.ascontiguousarray(_state_block(raw_final, "w", offsets)[:PHYSICAL_NY,1:,:PHYSICAL_NX].transpose(1,0,2))
        w_projected = np.ascontiguousarray(_state_block(projected, "w", offsets)[:PHYSICAL_NY,1:,:PHYSICAL_NX].transpose(1,0,2))
        if any(x.shape != owner_shape or not np.isfinite(x).all() for x in (w_input,w_raw,w_projected)):
            raise ValueError(f"{run['label']} step {step_index}: physical W owner crop invalid")

        # Reuse the source-authoritative accepted-step analyzer for G, K-FI,
        # same-Y pressure/buoyancy work and top closure. Its temporary NPZ is
        # discarded after its compact returned dictionary has been copied.
        with tempfile.TemporaryDirectory(prefix="pr308_step_analysis_") as temp:
            step_result = analyze_accepted_step([path], Path(temp)/"step.json",
                                                input_path=input_path, receipt_path=None)
        bweights = np.asarray(arrays["tableau_bI"], dtype=np.float64)
        stages = step_result["stages"]
        g_rows=[]; kfi_rms=[]; bound_complete=True
        for stage in stages:
            G=stage["G_residual"]
            g_rows.append({"stage":stage["stage"],"available":G.get("available"),
                           "rms_m_s":G.get("w_owner_stats",{}).get("rms"),
                           "max_abs_m_s":G.get("w_owner_stats",{}).get("max_abs"),
                           "K_minus_FI_RK_m_s2":G.get("method_K_minus_FI_RK",{}).get("w_owner_difference_m_s2"),
                           "full_minus_FI_FE_m_s2":G.get("full_remainders",{}).get("actual_FI_plus_FE_remainder_w_owner_m_s2")})
            val=G.get("method_K_minus_FI_RK",{}).get("w_owner_difference_m_s2",{}).get("rms")
            if stage.get("explicit_stage"):
                # Explicit stage 1 has no Newton K-FI defect; it contributes
                # zero to the implicit-stage postprocess bound by definition.
                kfi_rms.append(0.0)
            elif val is None:
                bound_complete=False
                kfi_rms.append(math.nan)
            else:
                kfi_rms.append(float(val))
        increment_bound=(float(run["h_seconds"]*np.sum(np.abs(bweights)*kfi_rms))
                         if bound_complete and bweights.size==4 else None)
        step_records.append({"step":step_index,"step_seq":int(meta["step_seq"]),
                             "host_timestep_token":int(meta["host_timestep_token"]),
                             "elapsed_seconds_fraction":str((Fraction(15,16) if step_index else 0)*step_index) if run["label"].startswith("coarse") else str(Fraction(15,32)*step_index),
                             "dt_seconds":float(run["h_seconds"]),"csv_path":str(path),"csv_sha256":_sha(path),
                             "G_W_and_FI_summary":g_rows,
                             "direct_increment_norm_bound_from_K_minus_FI_m_s":increment_bound,
                             "accepted_update":step_result["accepted_update"],
                             "source_pressure_buoyancy_power_w":step_result["physical_pressure_work"].get("stage_power_w"),
                             "source_pressure_buoyancy_tableau_work_j":step_result["physical_pressure_work"].get("tableau_weighted_power_time_integral_j"),
                             "physical_pressure_work_available":step_result["physical_pressure_work"].get("available")})
        step_records[-1]["_runtime"] = step_result["runtime"]
        step_records[-1]["_w_input_kji"] = w_input
        step_records[-1]["_w_raw_kji"] = w_raw
        step_records[-1]["_w_projected_kji"] = w_projected
        previous_projected=projected

    return {"label":run["label"],"h_seconds":float(run["h_seconds"]),"steps":expected_steps,
            "receipt":receipt,"run_receipt":receipt,"run_receipt_path":str(receipt_path),
            "run_receipt_sha256":_sha(receipt_path),"run_dir":str(run_dir),
            "input_path":str(input_path),"input_sha256":input_sha,
            "executable_sha256":receipt["producer"]["executable_sha256"],
            "impl_sha256":receipt["producer"].get("v2_impl_sha256",
                receipt["producer"].get("source_hashes", {}).get("impl")),
            "initial_state":initial_state,"step_records":step_records,"continuity":continuity}


def _m0_projection(direction_npz: Path, expected_sha: str) -> dict[str, np.ndarray | float]:
    actual = _sha(direction_npz)
    if actual != expected_sha:
        raise ValueError(f"frozen direction SHA mismatch: {actual} != {expected_sha}")
    data = np.load(direction_npz, allow_pickle=False)
    e1 = np.asarray(data["e1_dimensionless_kji"], dtype=np.float64)
    m0 = np.asarray(data["m0_physical_dry_w_dual_mass_kg_kji"], dtype=np.float64)
    a0 = np.asarray(data["a0_stage1_w_m_s2_kji"], dtype=np.float64)
    owner = np.asarray(data["owner_mask_kji"], dtype=bool)
    if e1.shape != (PHYSICAL_NZ, PHYSICAL_NY, PHYSICAL_NX) or \
       m0.shape != e1.shape or a0.shape != e1.shape or owner.shape != e1.shape:
        raise ValueError("frozen direction artifact has unexpected arrays/shapes")
    if not np.all(owner) or not np.isfinite(e1).all() or not np.isfinite(m0).all() or \
       not np.isfinite(a0).all() or np.any(m0 <= 0.0):
        raise ValueError("frozen direction artifact has invalid values or owner mask")
    rms_e1 = float(np.sqrt(np.sum(m0 * e1 * e1) / np.sum(m0)))
    if abs(rms_e1 - 1.0) > 1e-12:
        raise ValueError(f"frozen e1 M0 RMS is not 1: {rms_e1}")
    if not np.allclose(e1, a0 / float(np.sqrt(np.sum(m0*a0*a0)/np.sum(m0))),
                       rtol=1e-13, atol=1e-13):
        raise ValueError("frozen direction does not match the declared a0/M0 construction")
    return {"e1": e1, "m0": m0, "a0": a0, "sha256": actual,
            "rms_m0_a0_m_s2": float(np.sqrt(np.sum(m0 * a0 * a0) / np.sum(m0)))}


def _weighted_rms(x: np.ndarray, m0: np.ndarray) -> float:
    return float(np.sqrt(np.sum(m0 * x * x) / np.sum(m0)))


def _time_metrics(wc: np.ndarray, wf: np.ndarray, m0: np.ndarray,
                  e1: np.ndarray) -> dict[str, Any]:
    d = wf - wc
    rms_coarse, rms_fine, rms_delta = _rms(wc), _rms(wf), _rms(d)
    denom = float(np.sum(m0))
    pc = float(np.sum(m0 * wc * e1) / denom)
    pf = float(np.sum(m0 * wf * e1) / denom)
    pd = float(np.sum(m0 * d * e1) / denom)
    m0_rms_c, m0_rms_f, m0_rms_d = (_weighted_rms(wc, m0), _weighted_rms(wf, m0),
                                    _weighted_rms(d, m0))
    c_d_c = None
    norm_product = float(np.sqrt(np.sum(m0*d*d) * np.sum(m0*wc*wc)))
    if norm_product > 0:
        c_d_c = float(np.sum(m0 * d * wc) / norm_product)
    c_d_e = float(pd / m0_rms_d) if m0_rms_d > 0 else None
    parallel = pd * e1
    perpendicular = d - parallel
    return {
        "W_coarse_m_s": _stats(wc), "W_fine_m_s": _stats(wf), "delta_fine_minus_coarse_m_s": _stats(d),
        "m0_rms_coarse_m_s": m0_rms_c, "m0_rms_fine_m_s": m0_rms_f,
        "m0_rms_delta_m_s": m0_rms_d,
        "cosine_delta_vs_coarse_m0": c_d_c,
        "signed_projection_coarse_m_s": pc, "signed_projection_fine_m_s": pf,
        "signed_projection_delta_m_s": pd,
        "m0_cosine_delta_vs_e1": c_d_e,
        "parallel_delta_m0_rms_m_s": abs(pd),
        "perpendicular_delta_m0_rms_m_s": _weighted_rms(perpendicular, m0),
        "trigger_epsilon_over_3_m_s": TRIGGER_M_S,
        "diagnostic_trigger_exceeded": rms_delta > TRIGGER_M_S,
    }


def _json_safe_step(step: dict) -> dict:
    return {k:v for k,v in step.items() if not k.startswith("_")}


def _published_endpoint_w(run: dict[str, Any]) -> dict[str, Any]:
    receipt=run["receipt"]
    expected_times=receipt["case"]["expected_wrf_times"]
    found={}; files=[]
    for item in receipt.get("actual",{}).get("wrfout_files",[]):
        path=Path(run["run_dir"])/item["name"]
        if _sha(path)!=item["sha256"]:
            raise ValueError(f"{run['label']}: published NetCDF hash mismatch: {path}")
        with Dataset(path,"r") as ds:
            if "W" not in ds.variables or "Times" not in ds.variables:
                raise ValueError(f"{run['label']}: endpoint NetCDF lacks W/Times")
            raw=np.ma.asarray(ds.variables["W"][:])
            if raw.dtype!=np.dtype("float32"):
                raise ValueError(f"{run['label']}: published W must be FP32, got {raw.dtype}")
            if np.ma.isMaskedArray(raw): raw=np.asarray(raw.filled(np.nan),dtype=np.float32)
            if not np.isfinite(raw).all(): raise ValueError(f"{run['label']}: published W has NaN/Inf")
            times=[_time_text(row) for row in ds.variables["Times"][:]]
            for i,t in enumerate(times):
                if t in expected_times:
                    found[t]=np.ascontiguousarray(raw[i,1:,0:PHYSICAL_NY,0:PHYSICAL_NX])
            files.append({"name":item["name"],"sha256":item["sha256"],"times":times})
    if set(found)!=set(expected_times):
        raise ValueError(f"{run['label']}: exact t0/t15 WRF Times records are missing")
    return {"frames":found,"files":files}


def analyze(plan_path: Path, protocol_path: Path, out_path: Path) -> dict[str, Any]:
    plan = json.loads(plan_path.read_text())
    protocol = json.loads(protocol_path.read_text())
    if protocol.get("schema") != PROTOCOL_SCHEMA:
        raise ValueError("not the frozen PR308 protocol v3")
    protocol_sha = _sha(protocol_path)
    if plan.get("protocol_sha256") != protocol_sha:
        raise ValueError("plan/protocol SHA mismatch")
    plan_dir = plan_path.resolve().parent
    run_plan = plan.get("runs", [])
    if not isinstance(run_plan, list) or len(run_plan) != 2:
        raise ValueError("plan must contain exactly two run records")
    by_label = {r.get("label"): r for r in run_plan}
    if len(by_label) != len(run_plan) or set(by_label) != {"coarse_h0p9375", "fine_h0p46875"}:
        raise ValueError("plan labels differ from frozen run roles")
    coarse = _read_accepted_run(by_label["coarse_h0p9375"], plan_dir)
    fine = _read_accepted_run(by_label["fine_h0p46875"], plan_dir)
    for key in ("input_sha256", "executable_sha256", "impl_sha256"):
        if coarse[key] != fine[key]:
            raise ValueError(f"fine/coarse {key} mismatch")
    gate_spec = plan.get("initial_state_gate")
    if not isinstance(gate_spec, dict):
        raise ValueError("plan must bind the completed all-fields initial-state gate")
    initial_gate_path = _resolve(plan_dir, gate_spec["path"])
    if _sha(initial_gate_path) != gate_spec.get("sha256"):
        raise ValueError("initial-state gate hash mismatch")
    initial_gate = json.loads(initial_gate_path.read_text())
    if initial_gate.get("protocol_sha256") != protocol_sha or initial_gate.get("all_gates_pass") is not True:
        raise ValueError("common initial-state gate is not tied to protocol or did not pass")
    if initial_gate.get("packed_six_state_input", {}).get("bitwise_equal") is not True or \
       initial_gate.get("physical_initial_W", {}).get("both_bitwise_zero") is not True or \
       initial_gate.get("netcdf_times_t0_t15_both") is not True:
        raise ValueError("common initial-state gate lacks exact packed-state/W/time checks")
    netcdf_gate = initial_gate.get("netcdf_t0_fields", {})
    if set(netcdf_gate) != {"U", "V", "W", "PH", "T", "THM", "MU", "MUB"} or \
       any(x.get("bitwise_exact") is not True or x.get("finite_both") is not True for x in netcdf_gate.values()):
        raise ValueError("NetCDF initial eight-field parity gate is incomplete")
    if initial_gate.get("run_receipt_sha256") != [coarse["run_receipt_sha256"], fine["run_receipt_sha256"]]:
        raise ValueError("initial-state gate does not bind current pair receipts")
    if coarse["executable_sha256"] != protocol["producer"]["executable_sha256"]:
        raise ValueError("run executable differs from frozen producer pin")
    frozen_namelists = {x["label"]: x for x in protocol["case_pins"]["case_namelists"]}
    for run in (coarse, fine):
        receipt_label = run["receipt"].get("label")
        if receipt_label not in frozen_namelists:
            raise ValueError(f"{run['label']}: receipt label has no frozen namelist pin")
        frozen_nml_sha = frozen_namelists[receipt_label]["namelist_sha256"]
        inputs = run["receipt"].get("inputs", {})
        if inputs.get("sha256") != protocol["input"]["sha256"]:
            raise ValueError(f"{run['label']}: receipt input SHA differs from frozen input pin")
        nml_path_value = inputs.get("namelist_path")
        if inputs.get("namelist_sha256") != frozen_nml_sha or not nml_path_value:
            raise ValueError(f"{run['label']}: receipt namelist differs from frozen protocol hash")
        nml_path = Path(nml_path_value)
        if not nml_path.is_file() or _sha(nml_path) != frozen_nml_sha:
            raise ValueError(f"{run['label']}: receipt-bound namelist bytes fail SHA check")
        run["namelist_path"] = str(nml_path)
        run["namelist_sha256"] = frozen_nml_sha
        producer = run["receipt"].get("producer", {})
        if producer.get("cpp_archive_sha256") != protocol["producer"]["cpp_archive_sha256"]:
            raise ValueError(f"{run['label']}: CPP archive differs from frozen build pin")
    pins = protocol["case_pins"]
    for run in (coarse, fine):
        case = run["receipt"]["case"]
        pairs = (("scheme", "time_integration_scheme"), ("imex_mode", "imex_mode"),
                 ("mpi_ranks", "mpi_ranks"),
                 ("omp_threads", "omp_threads"), ("debug_level", "debug_level"))
        for actual, expected in pairs:
            if case.get(actual) != pins.get(expected):
                raise ValueError(f"{run['label']}: case pin mismatch for {actual}")
        if not case.get("stage_operand_diag"):
            raise ValueError(f"{run['label']}: stage observer is disabled")
    coarse_runtime = [x["_runtime"] for x in coarse["step_records"]]
    fine_runtime = [x["_runtime"] for x in fine["step_records"]]
    if any(x != coarse_runtime[0] for x in coarse_runtime[1:]) or any(x != fine_runtime[0] for x in fine_runtime[1:]) or coarse_runtime[0] != fine_runtime[0]:
        raise ValueError("effective runtime settings changed within/across runs")
    initial_exact = np.array_equal(coarse["initial_state"], fine["initial_state"])
    if not initial_exact:
        raise ValueError("initial six packed prognostic vectors are not exact-equal")
    wc = np.stack([coarse["step_records"][0]["_w_input_kji"]] +
                  [x["_w_projected_kji"] for x in coarse["step_records"]])
    wf = np.stack([fine["step_records"][0]["_w_input_kji"]] +
                  [x["_w_projected_kji"] for x in fine["step_records"]])
    expected_coarse = (17, PHYSICAL_NZ, PHYSICAL_NY, PHYSICAL_NX)
    expected_fine = (33, PHYSICAL_NZ, PHYSICAL_NY, PHYSICAL_NX)
    if wc.shape != expected_coarse or wf.shape != expected_fine:
        raise ValueError("accepted projected W arrays have wrong dimensions/counts")
    common_indices = list(range(0, 33, 2))
    common_times = [Fraction(15, 16) * j for j in range(17)]
    fine_times = [Fraction(15, 32) * k for k in common_indices]
    if common_times != fine_times or common_times[-1] != Fraction(15, 1):
        raise AssertionError("frozen rational time map is inconsistent")
    elapsed = np.asarray([float(x) for x in common_times])
    wf_common = wf[common_indices]
    direction_path = _resolve(protocol_path.resolve().parent.parent,
                              protocol["frozen_direction"]["artifact_npz"])
    direction = _m0_projection(direction_path, protocol["frozen_direction"]["artifact_npz_sha256"])
    e1, m0 = direction["e1"], direction["m0"]
    metrics = []
    for j, t in enumerate(common_times):
        row = _time_metrics(wc[j], wf_common[j], m0, e1)
        row.update({"sample_index": j, "elapsed_seconds": float(t),
                    "elapsed_seconds_exact": str(t), "coarse_step_seq": j,
                    "fine_step_seq": 2*j})
        metrics.append(row)
    initial_w_exact = np.array_equal(wc[0], wf[0])
    if not initial_w_exact:
        raise ValueError("initial internal physical W differs")
    published = {"coarse": _published_endpoint_w(coarse), "fine": _published_endpoint_w(fine)}
    endpoint_checks = {}
    for label, run, trajectory in (("coarse", coarse, wc), ("fine", fine, wf)):
        expected_times = run["receipt"]["case"]["expected_wrf_times"]
        rows = {}
        for stamp, index in ((expected_times[0], 0), (expected_times[-1], -1)):
            cast = trajectory[index].astype(np.float32)
            actual = published[label]["frames"][stamp]
            if not np.array_equal(cast.view(np.uint32), actual.view(np.uint32)):
                raise ValueError(f"{label}: internal FP64 endpoint differs from FP32 publication at {stamp}")
            rows[stamp] = "projected FP64 endpoint cast to FP32 is bitwise equal"
        endpoint_checks[label] = {"frames": rows, "files": published[label]["files"]}
    delta_rms = [float(x["delta_fine_minus_coarse_m_s"]["rms"]) for x in metrics]
    crossings = [j for j in range(1, 17) if delta_rms[j] > TRIGGER_M_S]
    if crossings:
        j = crossings[0]
        detail = {"rule": "first preregistered epsilon/3 crossing",
                  "start_s": float(common_times[j-1]), "end_s": float(common_times[j]),
                  "coarse_step_seq": j, "fine_step_seq": [2*j-1, 2*j]}
    else:
        j = 16
        detail = {"rule": "fixed final interval fallback; no trigger crossing",
                  "start_s": 14.0625, "end_s": 15.0, "coarse_step_seq": 16,
                  "fine_step_seq": [31, 32]}
    safe_coarse = [_json_safe_step(x) for x in coarse["step_records"]]
    safe_fine = [_json_safe_step(x) for x in fine["step_records"]]
    result = {
        "schema": ANALYSIS_SCHEMA,
        "scope": "accepted-step FP64 internal W trajectories over the fixed 0-15 s pair; descriptive only",
        "protocol": {"path": str(protocol_path), "sha256": protocol_sha, "schema": protocol["schema"]},
        "direction": {"npz_path": str(direction_path), "sha256": direction["sha256"],
                      "manifest_path": str(_resolve(protocol_path.resolve().parent.parent, protocol["frozen_direction"]["artifact_manifest"])),
                      "manifest_sha256": protocol["frozen_direction"]["artifact_manifest_sha256"],
                      "a0_rms_m0_m_s2": direction["rms_m0_a0_m_s2"], "e1_m0_rms": 1.0},
        "provenance": {"coarse_receipt": coarse["run_receipt_path"], "coarse_receipt_sha256": coarse["run_receipt_sha256"],
                       "fine_receipt": fine["run_receipt_path"], "fine_receipt_sha256": fine["run_receipt_sha256"],
                       "input_sha256": coarse["input_sha256"], "executable_sha256": coarse["executable_sha256"],
                       "impl_sha256": coarse["impl_sha256"],
                       "coarse_namelist_path": coarse["namelist_path"], "coarse_namelist_sha256": coarse["namelist_sha256"],
                       "fine_namelist_path": fine["namelist_path"], "fine_namelist_sha256": fine["namelist_sha256"],
                       "common_initial_state_gate_path": str(initial_gate_path),
                       "common_initial_state_gate_sha256": gate_spec["sha256"]},
        "pair": {"coarse_dt_s": COARSE_H_S, "coarse_steps": COARSE_STEPS,
                 "fine_dt_s": FINE_H_S, "fine_steps": FINE_STEPS, "horizon_s": HORIZON_S,
                 "common_samples": 17, "common_times_exact_s": [str(x) for x in common_times],
                 "fine_common_step_indices": common_indices,
                 "time_source": "accepted CSV step_seq and exact rational dt, not NetCDF Times",
                 "endpoint_netcdf_publication_checks": endpoint_checks,
                 "initial_six_state_vectors_exact": initial_exact, "initial_W_owners_exact": initial_w_exact,
                 "physical_W_owners": W_OWNERS, "physical_shape_kji": [PHYSICAL_NZ, PHYSICAL_NY, PHYSICAL_NX],
                 "fixed_bottom_excluded": True, "dynamic_top_included": True},
        "effective_runtime": coarse_runtime[0],
        "trigger": {"epsilon_w_rms_m_s": EPSILON_W_RMS_M_S, "epsilon_over_3_m_s": TRIGGER_M_S,
                    "max_delta_rms_m_s": max(delta_rms),
                    "max_delta_rms_elapsed_s": elapsed[int(np.argmax(delta_rms))],
                    "crossing_sample_indices": [x["sample_index"] for x in metrics if x["diagnostic_trigger_exceeded"]],
                    "detail_interval": detail,
                    "meaning": "fixed descriptive trigger/inspection selector only, not acceptance or an error bound"},
        "common_time_metrics": metrics,
        "per_step_operator_summaries": {"coarse": safe_coarse, "fine": safe_fine},
        "step_continuity": {"coarse": coarse["continuity"], "fine": fine["continuity"]},
        "selected_detail_step_summaries": {
            "coarse": _json_safe_step(coarse["step_records"][detail["coarse_step_seq"]-1]),
            "fine": [_json_safe_step(fine["step_records"][n-1]) for n in detail["fine_step_seq"]]},
        "limits": {"no_order_or_truth_bound": True, "no_accuracy_acceptance": True,
                   "no_frequency_or_causality_claim": True,
                   "pressure_buoyancy_work_not_exact_total_energy_budget": True},
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    npz_path = out_path.with_suffix(".npz")
    np.savez_compressed(npz_path, coarse_w_projected_m_s=wc, fine_w_projected_m_s=wf,
        fine_common_w_m_s=wf_common, delta_fine_minus_coarse_w_m_s=wf_common-wc,
        coarse_w_raw_final_m_s=np.stack([x["_w_raw_kji"] for x in coarse["step_records"]]),
        fine_w_raw_final_m_s=np.stack([x["_w_raw_kji"] for x in fine["step_records"]]),
        coarse_w_step_input_m_s=np.stack([x["_w_input_kji"] for x in coarse["step_records"]]),
        fine_w_step_input_m_s=np.stack([x["_w_input_kji"] for x in fine["step_records"]]),
        coarse_initial_state_packed=coarse["initial_state"], fine_initial_state_packed=fine["initial_state"],
        m0_w_kg=m0, e1_w_dimensionless=e1, a0_w_m_s2=direction["a0"],
        owner_w=np.ones(m0.shape, dtype=bool), elapsed_seconds=elapsed)
    csv_path = out_path.with_suffix(".csv")
    columns = ("sample_index", "elapsed_seconds_exact", "elapsed_seconds", "delta_W_rms_m_s",
        "delta_W_max_abs_m_s", "delta_W_M0_rms_m_s", "cos_delta_vs_coarse_M0", "p_coarse_m_s",
        "p_fine_m_s", "p_delta_m_s", "cos_delta_vs_e1_M0", "delta_parallel_M0_rms_m_s",
        "delta_perpendicular_M0_rms_m_s", "epsilon_over_3_trigger_exceeded")
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        for row in metrics:
            writer.writerow({"sample_index": row["sample_index"], "elapsed_seconds_exact": row["elapsed_seconds_exact"],
                "elapsed_seconds": row["elapsed_seconds"], "delta_W_rms_m_s": row["delta_fine_minus_coarse_m_s"]["rms"],
                "delta_W_max_abs_m_s": row["delta_fine_minus_coarse_m_s"]["max_abs"],
                "delta_W_M0_rms_m_s": row["m0_rms_delta_m_s"], "cos_delta_vs_coarse_M0": row["cosine_delta_vs_coarse_m0"],
                "p_coarse_m_s": row["signed_projection_coarse_m_s"], "p_fine_m_s": row["signed_projection_fine_m_s"],
                "p_delta_m_s": row["signed_projection_delta_m_s"], "cos_delta_vs_e1_M0": row["m0_cosine_delta_vs_e1"],
                "delta_parallel_M0_rms_m_s": row["parallel_delta_m0_rms_m_s"],
                "delta_perpendicular_M0_rms_m_s": row["perpendicular_delta_m0_rms_m_s"],
                "epsilon_over_3_trigger_exceeded": row["diagnostic_trigger_exceeded"]})
    step_csv = out_path.with_name(out_path.stem + "_steps.csv")
    step_columns = ("run", "step_seq", "elapsed_seconds_fraction", "dt_seconds",
        "direct_increment_norm_bound_from_K_minus_FI_m_s", "pressure_buoyancy_tableau_work_j",
        "pressure_work_available", "G_W_by_stage", "K_minus_FI_by_stage")
    with step_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=step_columns)
        writer.writeheader()
        for run_label, run in (("coarse", coarse), ("fine", fine)):
            for row in run["step_records"]:
                writer.writerow({"run": run_label, "step_seq": row["step_seq"],
                    "elapsed_seconds_fraction": row["elapsed_seconds_fraction"], "dt_seconds": row["dt_seconds"],
                    "direct_increment_norm_bound_from_K_minus_FI_m_s": row["direct_increment_norm_bound_from_K_minus_FI_m_s"],
                    "pressure_buoyancy_tableau_work_j": row["source_pressure_buoyancy_tableau_work_j"],
                    "pressure_work_available": row["physical_pressure_work_available"],
                    "G_W_by_stage": json.dumps([{"stage":g["stage"],"rms_m_s":g["rms_m_s"],"max_abs_m_s":g["max_abs_m_s"]}
                                                 for g in row["G_W_and_FI_summary"]], separators=(",",":")),
                    "K_minus_FI_by_stage": json.dumps([{"stage":g["stage"],"m_s2":g["K_minus_FI_RK_m_s2"]}
                                                       for g in row["G_W_and_FI_summary"]], separators=(",",":"))})
    result.update({"endpoint_npz": str(npz_path), "endpoint_npz_sha256": _sha(npz_path),
                   "summary_csv": str(csv_path), "summary_csv_sha256": _sha(csv_path),
                   "step_summary_csv": str(step_csv), "step_summary_csv_sha256": _sha(step_csv),
                   "analysis_script_sha256": _sha(Path(__file__).resolve())})
    out_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def _sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--protocol", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.plan, args.protocol, args.out)
    print(json.dumps({"out": str(args.out), "npz": result["endpoint_npz"],
                      "samples": result["pair"]["common_samples"],
                      "W_owners": result["pair"]["physical_W_owners"],
                      "max_delta_rms_m_s": result["trigger"]["max_delta_rms_m_s"]},
                     sort_keys=True))


if __name__ == "__main__":
    main()
