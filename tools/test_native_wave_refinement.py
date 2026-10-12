#!/usr/bin/env python3
"""Independent source-column gate for the dynamic native-W refinement fixture.

The C++ driver writes a deliberately small CSV protocol (`M` scalars, `A`
arrays).  This file imports the existing source-transcribed wave equations,
rebuilds A from the native serialized FP32 PHB faces, and checks modal tangent
and propagation behavior at the requested horizontal grids.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import platform
import re
import subprocess
import tempfile
from pathlib import Path
import struct

import numpy as np
import scipy
from scipy.linalg import expm


ROOT = Path(__file__).resolve().parents[1]
REF_PATH = ROOT / "tools" / "wave_energy_spatial_reference.py"
LEGACY_PHASE_B_PACKET_SHA256 = "8816c7fd7f3ed67a0ecb5488576ff825c9c04403786347af09ef996ed2d5781b"
SPEC = importlib.util.spec_from_file_location("wave_energy_spatial_reference", REF_PATH)
assert SPEC and SPEC.loader
REF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REF)
RUN_LOG: list[str] = []


SOURCE_PROFILE_FIELDS = (
    "state", "pbase", "p", "t_init_perturb", "phbase", "mubase", "al", "alb",
    "rdnw", "rdn", "dnw", "dn", "fnm", "fnp",
    "c1f", "c2f", "c1h", "c2h", "mapm", "mapu", "mapv",
    "f", "e", "sina", "cosa", "t_base", "u_base", "v_base",
)


def _periodic_pad_2d(a: np.ndarray, ny: int, nx: int) -> np.ndarray:
    """Pad source physical cells to the solver's packed closing row/column."""
    if a.shape != (ny - 1, nx - 1):
        raise AssertionError(f"expected physical horizontal shape {(ny-1, nx-1)}, got {a.shape}")
    out = np.empty((ny, nx), dtype=np.float32)
    out[:-1, :-1] = a
    out[:-1, -1] = a[:, 0]
    out[-1, :-1] = a[-1, :]
    out[-1, -1] = a[-1, 0]
    return out


def _periodic_pad_3d(a: np.ndarray, ny: int, nx: int) -> np.ndarray:
    """Convert NetCDF [k,j,i] to packed solver [j,k,i] with X wrap/Y close."""
    if a.ndim != 3 or a.shape[1:] != (ny - 1, nx - 1):
        raise AssertionError(f"expected [k,{ny-1},{nx-1}] profile, got {a.shape}")
    src = np.ascontiguousarray(a.transpose(1, 0, 2), dtype=np.float32)
    out = np.empty((ny, src.shape[1], nx), dtype=np.float32)
    out[:-1, :, :-1] = src
    out[:-1, :, -1] = src[:, :, 0]
    out[-1, :, :-1] = src[-1, :, :]
    out[-1, :, -1] = src[-1, :, 0]
    return out


def _memory_pad_2d(a: np.ndarray) -> np.ndarray:
    """Add one halo row/column to a packed 17x17 mass map buffer."""
    if a.shape != (17, 17):
        raise AssertionError(f"expected packed mass map (17,17), got {a.shape}")
    out = np.empty((18, 18), dtype=np.float32)
    out[:17, :17] = a
    out[:17, 17] = a[:, 1]
    out[17, :] = out[15, :]
    return out


def write_actual_source_profile(nc_path: Path, phase_b_snapshot: Path,
                                state_gate: Path, ft_pgf_reader: Path, output: Path,
                                ft_pgf_snapshot: Path | None = None) -> dict:
    """Validate and serialize one corrected WRF source60 state/context bundle."""
    from netCDF4 import Dataset

    gate = json.loads(state_gate.read_text())
    if gate.get("status") != "all_source60_input_call_state_checks_exact" or \
            gate.get("call_state_fields_exact") != ["U", "V", "W", "PH", "THM", "MU"]:
        raise AssertionError("source60 gate does not certify exact six-field state")
    packet = phase_b_snapshot.read_bytes()
    if len(packet) < 48:
        raise AssertionError("Phase-B component packet is truncated")
    h = struct.unpack("<12i", packet[:48])
    version, nx, ny, nz, nxu, nyv, nzw, count, flags, native_set, phi, vorder = h
    if (version, nx, ny, nz, nxu, nyv, nzw, count, flags, native_set, phi, vorder) != \
            (2, 17, 17, 16, 18, 18, 17, 24531, 127, 1, 1, 3):
        raise AssertionError(f"unexpected Phase-B header: {h}")
    state = np.frombuffer(packet, dtype="<f4", count=count, offset=48).copy()
    if not np.isfinite(state).all():
        raise AssertionError("Phase-B packed input state contains nonfinite values")
    # Existing source gate binds the call state to the corrected source60 frame;
    # packet hash and gate are kept beside this bundle as provenance.
    with Dataset(nc_path) as ds:
        if sha256_file(nc_path) != gate.get("corrected_input_fixture", {}).get("sha256"):
            raise AssertionError("source60 NetCDF hash does not match the state-gate receipt")
        if len(ds.dimensions["west_east"]) != 16 or len(ds.dimensions["south_north"]) != 16 or len(ds.dimensions["bottom_top"]) != 16:
            raise AssertionError("corrected source file dimensions do not match the pinned 16x16x16 case")
        def var(name: str) -> np.ndarray:
            if name not in ds.variables:
                raise AssertionError(f"source profile is missing {name}")
            a = np.asarray(ds.variables[name][0], dtype=np.float32)
            if not np.isfinite(a).all():
                raise AssertionError(f"source profile {name} contains nonfinite values")
            return a
        # C-contiguous solver blocks use [j,k,i] / [j,i].
        pbase = _periodic_pad_3d(var("PB"), ny, nx)
        t_base = var("T_BASE")
        thbase = _periodic_pad_3d(var("T_INIT"), ny, nx)
        phbase = _periodic_pad_3d(var("PHB"), ny, nx)
        mubase = _periodic_pad_2d(var("MUB"), ny, nx)
        alb = _periodic_pad_3d(var("ALB"), ny, nx)
        mapm = _memory_pad_2d(_periodic_pad_2d(var("MAPFAC_M"), ny, nx))
        raw_u, raw_v = var("MAPFAC_U"), var("MAPFAC_V")
        mapu = np.empty((ny + 1, nx + 1), dtype=np.float32)
        mapu[:ny-1, :nx] = raw_u
        mapu[:ny-1, nx] = raw_u[:, 1]
        mapu[-2, :] = mapu[-3, :]
        mapu[-1, :] = mapu[-2, :]
        mapv = np.empty((ny + 1, nx + 1), dtype=np.float32)
        mapv[:ny, :nx-1] = raw_v
        mapv[:ny, nx-1] = raw_v[:, 0]
        mapv[:ny, nx] = raw_v[:, 1]
        mapv[-1, :] = mapv[-2, :]
        f = _memory_pad_2d(_periodic_pad_2d(var("F"), ny, nx))
        e = _memory_pad_2d(_periodic_pad_2d(var("E"), ny, nx))
        sina = _memory_pad_2d(_periodic_pad_2d(var("SINALPHA"), ny, nx))
        cosa = _memory_pad_2d(_periodic_pad_2d(var("COSALPHA"), ny, nx))
        vec = lambda name, n: np.pad(var(name), (0, max(0, n - var(name).size)), mode="edge")[:n]
        vectors = {name: vec(name, n) for name, n in (
            ("RDNW", 17), ("RDN", 16), ("DNW", 16), ("DN", 16),
            ("FNM", 17), ("FNP", 17), ("C1F", 17), ("C2F", 17),
            ("C1H", 16), ("C2H", 16))}
        # The runtime Full call records its actual W-level interpolation
        # tensors. Prefer these over an assumed top extrapolation from input.
        component_payload_offset = 48 + 2 * count * 4
        component_f64 = np.frombuffer(packet, dtype="<f8", offset=component_payload_offset)
        ph3_count = ny * nzw * nx
        mass2_count = ny * nx
        ph_fnm_offset = (3 * ph3_count + nzw + 7 * ph3_count + 1 +
                         6 * ph3_count + mass2_count + 2 * nzw)
        ph_fnm = component_f64[ph_fnm_offset:ph_fnm_offset + nzw]
        ph_fnp = component_f64[ph_fnm_offset + nzw:ph_fnm_offset + 2 * nzw]
        if ph_fnp.size != nzw or ph_fnm.size != nzw or \
                not np.isfinite(ph_fnm).all() or not np.isfinite(ph_fnp).all():
            raise AssertionError("same-call Full PH FNM/FNP tensors are missing or nonfinite")
        vectors["FNM"] = ph_fnm.astype(np.float32)
        vectors["FNP"] = ph_fnp.astype(np.float32)
        source_cf = [float(ds.variables[n][0]) for n in ("CF1", "CF2", "CF3")]
        dx, dy = float(ds.getncattr("DX")), float(ds.getncattr("DY"))
        run_dir = phase_b_snapshot.parent
        namelist = (run_dir / "namelist.input").read_text()
        rsl = (run_dir / "rsl.error.0000").read_text()
        runtime_cf, runtime_cf_source, cf_record_count = select_runtime_cf_coefficients(
            rsl, phase_b_snapshot)
        def nml_float(name: str) -> float:
            match = re.search(rf"(?im)^\s*{name}\s*=\s*([-+0-9.eEdD]+)", namelist)
            if not match:
                raise AssertionError(f"Phase-B namelist is missing {name}")
            return float(match.group(1).replace("D", "E").replace("d", "e"))
        config = re.search(r"Grid config: rdx=([^,]+), rdy=([^,]+), kdamp=([^,]+), khdif=([^,]+), kvdif=([^\s]+)", rsl)
        smag = re.search(r"Smagorinsky: opt=(\d+), c_s=([^,]+), c_k=([^\s]+)", rsl)
        precond = re.search(r"precond_type\s*=\s*(\d+)", rsl)
        newton = re.search(r"(?m)^\s*newton_tol:\s*([-+0-9.eEdD]+)", rsl)
        krylov = re.search(r"g_sdirk3_config\.krylov_tol\s*=\s*([-+0-9.eEdD]+)", rsl)
        max_newton = re.search(r"(?m)^\s*max_newton_iter:\s*(\d+)", rsl)
        max_krylov = re.search(r"g_sdirk3_config\.max_krylov_iter\s*=\s*(\d+)", rsl)
        restart = re.search(r"GMRES restart:\s*(\d+)", rsl)
        if not (config and smag and precond and newton and krylov and max_newton and max_krylov and restart):
            raise AssertionError("Phase-B runtime log lacks effective WRF grid/Smagorinsky/preconditioner settings")
        rdx, rdy, kdamp, effective_kh, effective_kv = map(float, config.groups())
        smag_opt, cs, ck = smag.groups()
        c_s, c_k = float(cs), float(ck)
        khdif, kvdif = nml_float("khdif"), nml_float("kvdif")
        diff_opt = int(nml_float("diff_opt"))
        newton_tol = float(newton.group(1).replace("D", "E").replace("d", "e"))
        krylov_tol = float(krylov.group(1).replace("D", "E").replace("d", "e"))
        max_newton_iter, max_krylov_iter, gmres_restart = int(max_newton.group(1)), int(max_krylov.group(1)), int(restart.group(1))
        if abs(rdx - 1.0 / dx) > 1e-10 or abs(rdy - 1.0 / dy) > 1e-10 or \
                effective_kh != khdif or effective_kv != kvdif or int(smag_opt) != 1 or int(precond.group(1)) != 2:
            raise AssertionError("Phase-B effective runtime settings disagree with the source/namelist profile")
        operands_path = run_dir / "port_u_pgf_operands_snapshot.bin"
        operand_bytes = operands_path.read_bytes()
        if len(operand_bytes) < 20:
            raise AssertionError("same-call Full PGF operand sidecar is truncated")
        operand_header = struct.unpack("<5i", operand_bytes[:20])
        if operand_header != (1, 17, 16, 17, 17):
            raise AssertionError(f"unexpected same-call Full PGF operand header {operand_header}")
        operand_count, operand_ph_count = 17 * 16 * 17, 17 * 17 * 17
        operand_data = np.frombuffer(operand_bytes, dtype="<f8", offset=20)
        if operand_data.size != 3 * operand_count + operand_ph_count:
            raise AssertionError("same-call Full PGF operand sidecar has an unexpected payload length")
        cpp_p_call = operand_data[:operand_count].astype(np.float32)
        cpp_al_call = operand_data[operand_count:2 * operand_count].astype(np.float32)
        alt_call = operand_data[2 * operand_count:3 * operand_count].astype(np.float32)
        ph_call = operand_data[3 * operand_count:].astype(np.float32)
        if not all(np.isfinite(x).all() for x in (cpp_p_call, cpp_al_call, alt_call, ph_call)):
            raise AssertionError("same-call Full PGF operands contain nonfinite values")
        inferred_ft_pgf = run_dir.parent / "pr302_phaseB_fortran_rk3_h15" / "wrf_u_pgf_snapshot.bin"
        ft_pgf_path = ft_pgf_snapshot.resolve() if ft_pgf_snapshot else inferred_ft_pgf
        if not ft_pgf_path.is_file():
            raise AssertionError(f"same-call Fortran PGF snapshot is missing: {ft_pgf_path}; pass --ft-pgf-snapshot for non-legacy capture layouts")
        ft_reader_path = ft_pgf_reader.resolve()
        spec = importlib.util.spec_from_file_location("pr302_ft_pgf_reader", ft_reader_path)
        if not spec or not spec.loader:
            raise AssertionError(f"cannot load authoritative Fortran PGF reader: {ft_reader_path}")
        ft_reader = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ft_reader)
        ft_pgf = ft_reader.read_ft_pgf(ft_pgf_path)
        ft_p = np.asarray(ft_reader.actual_phys(ft_pgf, "P"), dtype=np.float32)
        ft_al = np.asarray(ft_reader.actual_phys(ft_pgf, "AL"), dtype=np.float32)
        if ft_p.shape != (16, 16, 16) or ft_al.shape != (16, 16, 16):
            raise AssertionError(f"unexpected post-start Fortran P/AL shapes: {ft_p.shape} / {ft_al.shape}")
        # Fortran's authoritative same-call inputs are cropped as [j,k,i]; the
        # native bundle stores [j,k,i] packed values with periodic-X/Y closing.
        p_call = _periodic_pad_3d(ft_p.transpose(1, 0, 2), ny, nx).reshape(-1)
        al_call = _periodic_pad_3d(ft_al.transpose(1, 0, 2), ny, nx).reshape(-1)
        if not np.isfinite(p_call).all() or not np.isfinite(al_call).all():
            raise AssertionError("post-start Fortran P/AL source operands contain nonfinite values")
        blocks = [state, pbase.reshape(-1), p_call, thbase.reshape(-1), phbase.reshape(-1),
                  mubase.reshape(-1), al_call, alb.reshape(-1)] + [vectors[n] for n in
                  ("RDNW", "RDN", "DNW", "DN", "FNM", "FNP", "C1F", "C2F", "C1H", "C2H")] + \
                 [mapm.reshape(-1), mapu.reshape(-1), mapv.reshape(-1), f.reshape(-1), e.reshape(-1),
                  sina.reshape(-1), cosa.reshape(-1), t_base, var("U_BASE"), var("V_BASE")]
        # Versioned little-endian bundle: header, sixteen scalar controls, then
        # fixed-order FP32 blocks with lengths implied by 17x17x16 packed dimensions.
        dims = (1, nx, ny, nz, nxu, nyv, nzw, count, flags, native_set, phi, vorder)
        with output.open("wb") as out:
            out.write(struct.pack("<12i", *dims))
            out.write(struct.pack("<16f", dx, dy, c_s, c_k, khdif, kvdif, *runtime_cf, kdamp,
                                  newton_tol, krylov_tol, max_newton_iter, max_krylov_iter,
                                  gmres_restart, diff_opt))
            for block in blocks:
                out.write(np.asarray(block, dtype="<f4").tobytes(order="C"))
    return {"bundle": str(output), "bundle_sha256": sha256_file(output),
            "phase_b_snapshot_sha256": sha256_file(phase_b_snapshot),
            "pgf_operand_snapshot_sha256": sha256_file(operands_path),
            "pgf_operand_header": list(operand_header),
            "cpp_pgf_operand_snapshot_sha256": sha256_file(operands_path),
            "fortran_pgf_snapshot": {"path": str(ft_pgf_path),
                                     "sha256": sha256_file(ft_pgf_path),
                                     "bounds": ft_pgf["bounds"],
                                     "path_source": "explicit_cli" if ft_pgf_snapshot else "legacy_sibling_inference",
                                     "seed_source": "actual_phys(read_ft_pgf, P/AL)"},
            "fortran_pgf_reader": {"path": str(ft_reader_path),
                                   "sha256": sha256_file(ft_reader_path)},
            "pgf_operand_max_abs": {"p_ft": float(np.max(np.abs(p_call))),
                                    "al_ft": float(np.max(np.abs(al_call))),
                                    "p_cpp_diag": float(np.max(np.abs(cpp_p_call))),
                                    "al_cpp_diag": float(np.max(np.abs(cpp_al_call))),
                                    "alt": float(np.max(np.abs(alt_call))),
                                    "ph": float(np.max(np.abs(ph_call)))},
            "full_call_phi_fnm_fnp_max_abs": {"fnm": float(np.max(np.abs(ph_fnm))),
                                              "fnp": float(np.max(np.abs(ph_fnp)))},
            "source_nc_sha256": sha256_file(nc_path), "source_gate": str(state_gate),
            "header": list(h), "field_order": list(SOURCE_PROFILE_FIELDS),
            "bundle_format": "native-source-profile-v1-f32-le",
            "runtime_config": {"diffusion_option": diff_opt, "khdif": khdif, "kvdif": kvdif,
                               "kdamp": kdamp, "smagorinsky_opt": int(smag_opt),
                               "c_s": c_s, "c_k": c_k, "precond_type": int(precond.group(1)),
                               "newton_tol": newton_tol, "krylov_tol": krylov_tol,
                               "gmres_restart": gmres_restart, "max_krylov_iter": max_krylov_iter,
                               "max_newton_iter": max_newton_iter,
                               "native_cf1_cf2_cf3": runtime_cf,
                               "native_cf_source": runtime_cf_source,
                               "native_cf_record_count": cf_record_count,
                               "native_runtime_log": str(run_dir / "rsl.error.0000"),
                               "native_runtime_log_sha256": sha256_file(run_dir / "rsl.error.0000"),
                               "wrfinput_cf1_cf2_cf3": source_cf}}


def compare_source_rhs_to_phase_b(cpp_rhs: np.ndarray, projected_state: np.ndarray,
                                 profile_bundle: Path, phase_b_packet: Path,
                                 full_snapshot: Path) -> dict:
    """Fail closed on exact six-field physical-owner parity before temporal steps."""
    raw = full_snapshot.read_bytes()
    if len(raw) < 28:
        raise AssertionError("Phase-B Full RHS snapshot is truncated")
    header = struct.unpack("<7i", raw[:28])
    if header != (1, 17, 17, 16, 18, 18, 17):
        raise AssertionError(f"unexpected Phase-B Full RHS snapshot header: {header}")
    expected_payload = np.frombuffer(raw, dtype="<f4", offset=28)
    count = 24531
    if expected_payload.size != 2 * count:
        raise AssertionError(f"Phase-B Full RHS payload has {expected_payload.size} values, expected {2*count}")
    with profile_bundle.open("rb") as stream:
        profile_header = struct.unpack("<12i", stream.read(48))
        stream.seek(48 + 64)
        input_state = np.frombuffer(stream.read(count * 4), dtype="<f4")
    if profile_header[0] != 1 or input_state.size != count:
        raise AssertionError("source profile bundle header/state is malformed")
    captured_state, expected_rhs = expected_payload[:count], expected_payload[count:]
    if not exact_fp32_bits(captured_state, input_state):
        raise AssertionError("Phase-B Full snapshot state differs from the gated component-packet input_state")
    packet_data = phase_b_packet.read_bytes()
    if len(packet_data) < 48 + 2 * count * 4:
        raise AssertionError("Phase-B component packet lacks its projected-state block")
    packet_header = struct.unpack("<12i", packet_data[:48])
    if packet_header[0] != 2 or packet_header[7] != count:
        raise AssertionError(f"unexpected Phase-B component packet header: {packet_header}")
    expected_projected = np.frombuffer(packet_data, dtype="<f4", count=count,
                                       offset=48 + count * 4)
    actual_projected = np.asarray(projected_state, dtype=np.float32).reshape(-1)
    if actual_projected.size != count or not exact_fp32_bits(actual_projected, expected_projected):
        max_abs = float(np.max(np.abs(actual_projected.astype(np.float64) -
                                      expected_projected.astype(np.float64)))) if actual_projected.size == count else float("inf")
        raise AssertionError(f"native projectStateBoundaries result differs from same-call Phase-B projected_state: max_abs={max_abs:.9g}")
    actual = np.asarray(cpp_rhs, dtype=np.float32).reshape(-1)
    if actual.size != count:
        raise AssertionError(f"native ingestion RHS has {actual.size} values, expected {count}")
    sizes = (4896, 4896, 4913, 4913, 4624, 289)
    shapes = ((17, 16, 18), (18, 16, 17), (17, 17, 17),
              (17, 17, 17), (17, 16, 17), (17, 17))
    names = ("U", "V", "W", "PH", "THM", "MU")
    owner_slices = ((slice(0,16),slice(None),slice(0,16)),
                    (slice(1,16),slice(None),slice(0,16)),
                    (slice(0,16),slice(1,17),slice(0,16)),
                    (slice(0,16),slice(1,17),slice(0,16)),
                    (slice(0,16),slice(None),slice(0,16)),
                    (slice(0,16),slice(0,16)))
    report = {}
    off = 0
    for name, size, shape, owner in zip(names, sizes, shapes, owner_slices):
        ref_block = expected_rhs[off:off + size]
        act_block = actual[off:off + size]
        if ref_block.size != size:
            raise AssertionError(f"Phase-B Full RHS is missing {name} storage values")
        ref_owner = ref_block.reshape(shape)[owner]
        act_owner = act_block.reshape(shape)[owner]
        if not np.isfinite(ref_owner).all() or not np.isfinite(act_owner).all():
            raise AssertionError(f"source ingestion Full RHS {name} owner contains NaN or Inf")
        exact = exact_fp32_bits(ref_owner, act_owner)
        delta = act_owner.astype(np.float64) - ref_owner.astype(np.float64)
        max_abs = float(np.max(np.abs(delta)))
        report[name] = {"owner_count": int(ref_owner.size), "exact_float32": exact,
                        "max_abs": max_abs,
                        "rms": float(np.sqrt(np.mean(delta * delta))),
                        "mismatch_count": int(np.count_nonzero(
                            np.ascontiguousarray(ref_owner, dtype=np.float32).view(np.uint32) !=
                            np.ascontiguousarray(act_owner, dtype=np.float32).view(np.uint32))),
                        "snapshot_storage_values_missing": 0}
        off += size
    all_exact = all(field["exact_float32"] for field in report.values())
    return {"status": "exact_six_field_owner_parity" if all_exact else "rhs_owner_mismatch",
            "exact_six_field_owner_parity": all_exact, "snapshot_header": list(header),
            "snapshot_sha256": sha256_file(full_snapshot),
            "component_packet_sha256": sha256_file(phase_b_packet),
            "projected_state_exact": True,
            "fields": report,
            "owner_policy": "U unique periodic faces i=0..15; V interior symmetric-Y faces j=1..15; W/PH dynamic k=1..16; THM/MU physical mass owners"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def select_runtime_cf_coefficients(rsl_text: str, phase_b_packet: Path) -> tuple[list[float], str, int]:
    """Use runtime-forwarded CFs; allow defaults only for the pinned old packet."""
    transport_lines = [line for line in rsl_text.splitlines()
                       if "[NATIVE TRANSPORT CONFIG]" in line]
    pattern = re.compile(r"\[NATIVE TRANSPORT CONFIG\][^\r\n]*?\bcf1=([-+0-9.eEdD]+)\s+"
                         r"cf2=([-+0-9.eEdD]+)\s+cf3=([-+0-9.eEdD]+)")
    if transport_lines:
        rows = []
        for line in transport_lines:
            match = pattern.search(line)
            if not match:
                raise AssertionError("NATIVE TRANSPORT CONFIG line lacks cf1/cf2/cf3")
            values = [float(value.replace("D", "E").replace("d", "e"))
                      for value in match.groups()]
            if not np.isfinite(values).all():
                raise AssertionError("runtime NATIVE TRANSPORT CONFIG has nonfinite CF coefficients")
            rows.append(values)
        if any(row != rows[0] for row in rows[1:]):
            raise AssertionError("runtime NATIVE TRANSPORT CONFIG CF tuples differ across emitted tiles")
        return rows[0], "captured_rsl_native_transport_config", len(rows)
    if sha256_file(phase_b_packet) == LEGACY_PHASE_B_PACKET_SHA256:
        return [0.5, 0.0, 0.0], "legacy_phaseB_packet_constructor_defaults_no_cf_record", 0
    raise AssertionError("runtime log has no NATIVE TRANSPORT CONFIG CF record; defaults are allowed only for the pinned legacy Phase-B packet")


def exact_fp32_bits(a: np.ndarray, b: np.ndarray) -> bool:
    """Compare IEEE-754 FP32 bit patterns, including signed zero."""
    x = np.ascontiguousarray(a, dtype=np.float32)
    y = np.ascontiguousarray(b, dtype=np.float32)
    return x.shape == y.shape and np.array_equal(x.view(np.uint32), y.view(np.uint32))


def read_payload(path: Path) -> tuple[dict[str, float], dict[str, np.ndarray], dict[str, float], dict[str, str]]:
    meta: dict[str, float] = {}
    arrays: dict[str, np.ndarray] = {}
    scalars: dict[str, float] = {}
    text_meta: dict[str, str] = {}
    with path.open(newline="") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0] == "M":
                # Exact legacy compatibility for already captured C++ rows
                # written as M before source_profile_mode became a text row.
                if row[1] == "source_profile_mode" and row[2] in ("retained_replay", "branch_fd"):
                    text_meta[row[1]] = row[2]
                else:
                    meta[row[1]] = float(row[2])
            elif row[0] == "A":
                count = int(row[2])
                value = np.asarray([float(x) for x in row[3:]], dtype=np.float64)
                if value.size != count:
                    raise AssertionError(f"{row[1]} has {value.size} values, expected {count}")
                arrays[row[1]] = value
            elif row[0] == "S":
                scalars[row[1]] = float(row[2])
            elif row[0] == "T":
                text_meta[row[1]] = row[2]
    return meta, arrays, scalars, text_meta


def parse_converged_stage_rho(path: Path, repetitions: int) -> dict:
    """Read converged implicit-stage rho, falling back to Newton's unconditional marker."""
    newton_pattern = re.compile(r"\[Newton\] CONVERGED stage=(\d+)\s+iters=(\d+)\s+"
                                r"\|\|S⁻¹R\|\|_rms=([^\s]+)")
    summary_matches = []
    newton_matches = []
    if path.is_file():
        for line_number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if "SDIRK3_STAGE_HISTORY_SUMMARY" in line and \
                    "convergence_applicable=1" in line and "converged=1" in line:
                stage = re.search(r"target_stage=(\d+)", line)
                residual = re.search(r"scaled_final_residual=([^\s]+)", line)
                if not stage or not residual:
                    continue
                try:
                    value = float(residual.group(1))
                except ValueError:
                    continue
                if math.isfinite(value):
                    summary_matches.append({"line": line_number, "stage": int(stage.group(1)),
                                            "scaled_final_residual": value,
                                            "marker": "stage_history_summary"})
            newton = newton_pattern.search(line)
            if newton:
                try:
                    value = float(newton.group(3))
                except ValueError:
                    continue
                if math.isfinite(value):
                    newton_matches.append({"line": line_number, "stage": int(newton.group(1)),
                                           "iterations": int(newton.group(2)),
                                           "scaled_final_residual": value,
                                           "marker": "newton_converged"})
    matches = summary_matches if summary_matches else newton_matches
    source = "stage_history_summary" if summary_matches else (
        "newton_converged_marker" if newton_matches else "unavailable")
    expected_ids = [2, 3, 4] * repetitions
    sequence_valid = [x["stage"] for x in matches] == expected_ids
    partitions = None
    if sequence_valid:
        if repetitions == 5:
            partitions = {"retained_baseline": matches[0:6],
                          "full_replay": matches[6:12],
                          "step1_snapshot_replay": matches[12:15]}
        elif repetitions == 2:
            partitions = {"two_step_branch": matches[0:6]}
        else:
            partitions = {"sequence": matches}
    return {"source_log": str(path), "source": source,
            "converged_applicable_stage_count": len(matches),
            "expected_stage_ids": expected_ids,
            "observed_stage_ids": [x["stage"] for x in matches],
            "sequence_status": "validated" if sequence_valid else "unknown",
            "run_partitions": partitions, "stages": matches,
            "scope": "nonlinear convergence diagnostic only; not a state or gradient error bound"}


def source_column(nz: int, nx: int, phb_faces: np.ndarray) -> dict:
    """Use the existing source-transcribed A with the actual imported PHB faces."""
    return REF.column(nz=nz, nx=nx, lx=40000.0, ly=30000.0,
                      theta_gradient="analytic", imported_phb_faces=phb_faces)


def native_mode_state(column: dict, base: np.ndarray, nx: int, ny: int, nz: int,
                      offsets: tuple[int, ...]) -> tuple[np.ndarray, float]:
    """Build one small real wave from the independently selected sub-Nmax eigenmode."""
    values, vectors = np.linalg.eig(column["A"])
    nmax = float(np.sqrt(np.max(column["N2_analytic"])))
    candidates = [i for i, value in enumerate(values)
                  if value.imag > 1.0e-7 and value.imag <= nmax]
    if not candidates:
        raise AssertionError(f"no oscillatory source mode below Nmax={nmax}")
    selected = max(candidates, key=lambda i: values[i].imag)
    q = vectors[:, selected].astype(np.complex128)
    pivot = nz + (nz - 1)
    q *= np.exp(-1j * np.angle(q[pivot]))
    unit_scale = max(np.max(np.abs(q[:nz])), np.max(np.abs(q[nz:2*nz])),
                     np.max(np.abs(q[2*nz:3*nz])) / 100.0,
                     np.max(np.abs(q[3*nz:4*nz])), abs(q[-1]) / 100.0)
    q *= 5.0e-3 / unit_scale

    state = base.copy()
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    nu, nw = nx + 1, nz + 1
    dx, wave_k = column["dx"], column["k_physical"]
    phase_u = np.exp(1j * wave_k * np.arange(nu) * dx)
    phase_c = np.exp(1j * wave_k * (np.arange(nx) + 0.5) * dx)
    u = state[o_u:o_v].reshape(ny, nz, nu)
    w = state[o_w:o_ph].reshape(ny, nw, nx)
    ph = state[o_ph:o_t].reshape(ny, nw, nx)
    theta = state[o_t:o_mu].reshape(ny, nz, nx)
    mu = state[o_mu:].reshape(ny, nx)
    for k in range(nz):
        u[:, k, :] += np.real(q[k] * phase_u)[None, :]
        w[:, k + 1, :] += np.real(q[nz + k] * phase_c)[None, :]
        ph[:, k + 1, :] += np.real(q[2*nz + k] * phase_c)[None, :]
        theta[:, k, :] += np.real(q[3*nz + k] * phase_c)[None, :]
    mu += np.real(q[-1] * phase_c)[None, :]
    return state, float(values[selected].imag)


def unpack_projection(z: np.ndarray, nx: int, ny: int, nz: int, lx: float,
                      offsets: tuple[int, ...]) -> dict[str, np.ndarray]:
    """Return Fourier amplitudes of C-grid U/W/PH/theta/MU using physical coordinates."""
    nu, nv, nw = nx + 1, ny + 1, nz + 1
    su, sv, sw = ny * nz * nu, nv * nz * nx, ny * nw * nx
    st, sm = ny * nz * nx, ny * nx
    o_u, o_v, o_w, o_ph, o_t, o_m = offsets
    k = 2.0 * np.pi / lx
    def mode(a: np.ndarray, xface: bool) -> np.ndarray:
        n = a.shape[-1]
        x = np.arange(n, dtype=float) * (lx / nx) + (0.0 if xface else 0.5 * lx / nx)
        # Real-part convention: Re(q exp(ikx)) has projected amplitude q.
        return 2.0 * np.mean(a * np.exp(-1j * k * x), axis=-1)
    u = z[o_u:o_u + su].reshape(ny, nz, nu).mean(axis=0)
    v = z[o_v:o_v + sv].reshape(nv, nz, nx).mean(axis=0)
    w = z[o_w:o_w + sw].reshape(ny, nw, nx).mean(axis=0)
    ph = z[o_ph:o_ph + sw].reshape(ny, nw, nx).mean(axis=0)
    th = z[o_t:o_t + st].reshape(ny, nz, nx).mean(axis=0)
    mu = z[o_m:o_m + sm].reshape(ny, nx).mean(axis=0)
    return {"u": mode(u[:, :nx], True), "v": mode(v, False),
            "w": mode(w, False), "ph": mode(ph, False),
            "theta": mode(th, False), "mu": mode(mu, False)}


def modal_source_vector(p: dict[str, np.ndarray], nz: int) -> np.ndarray:
    # Source order is [U,W,PH(1..nz),THETA,MU].
    q = np.zeros(4 * nz + 1, dtype=np.complex128)
    q[:nz] = p["u"]
    q[nz:2 * nz] = p["w"][1:nz + 1]
    q[2 * nz:3 * nz] = p["ph"][1:nz + 1]
    q[3 * nz:4 * nz] = p["theta"]
    q[-1] = p["mu"].mean()
    return q


def reconstruct_modal_rhs(q: np.ndarray, nx: int, ny: int, nz: int, lx: float,
                          offsets: tuple[int, ...]) -> np.ndarray:
    """Reconstruct a real C-grid Fourier RHS over every horizontal row/cell."""
    total = offsets[-1] + ny * nx
    result = np.zeros(total, dtype=np.float64)
    o_u, o_v, o_w, o_ph, o_t, o_mu = offsets
    nw = nz + 1
    wave_k = 2.0 * np.pi / lx
    face_phase = np.exp(1j * wave_k * np.arange(nx + 1) * (lx / nx))
    center_phase = np.exp(1j * wave_k * (np.arange(nx) + 0.5) * (lx / nx))
    u = result[o_u:o_v].reshape(ny, nz, nx + 1)
    v = result[o_v:o_w].reshape(ny + 1, nz, nx)
    w = result[o_w:o_ph].reshape(ny, nw, nx)
    ph = result[o_ph:o_t].reshape(ny, nw, nx)
    theta = result[o_t:o_mu].reshape(ny, nz, nx)
    mu = result[o_mu:].reshape(ny, nx)
    for k in range(nz):
        u[:, k, :] = np.real(q[k] * face_phase)[None, :]
        w[:, k + 1, :] = np.real(q[nz + k] * center_phase)[None, :]
        ph[:, k + 1, :] = np.real(q[2*nz + k] * center_phase)[None, :]
        theta[:, k, :] = np.real(q[3*nz + k] * center_phase)[None, :]
    mu[:, :] = np.real(q[-1] * center_phase)[None, :]
    return result


def block_error(actual: np.ndarray, expected: np.ndarray, label: str,
                floor: float, budget: float) -> float:
    err = float(np.linalg.norm(actual - expected))
    scale = max(float(np.linalg.norm(expected)), floor * math.sqrt(expected.size))
    rel = err / scale
    if not np.isfinite(rel) or rel > budget:
        raise AssertionError(f"{label} scaled error {rel:.6g} exceeds {budget:.6g}; abs={err:.6g}")
    return rel


def require_zero_response(error: float, budget: float, label: str) -> None:
    if not np.isfinite(error) or error > budget:
        raise AssertionError(f"{label} analytic-zero response {error:.6g} exceeds absolute FD roundoff budget {budget:.6g}")


def run_grid(exe: Path, nx: int, ny: int, nz: int) -> dict:
    tmp = tempfile.TemporaryDirectory(prefix="native-wave-")
    payload = Path(tmp.name) / "native.csv"
    initial_file = Path(tmp.name) / "initial_state.txt"
    def invoke(initial: Path | None = None, descriptor_only: bool = False):
        command = [str(exe), str(nx), str(ny), str(nz), str(payload)]
        if initial is not None:
            command.append(str(initial))
        elif descriptor_only:
            command.append("--descriptor-only")
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
        if completed.returncode:
            tail = "\n".join((completed.stdout + completed.stderr).splitlines()[-80:])
            raise RuntimeError(f"native wave executable failed ({completed.returncode}):\n{tail}")
        RUN_LOG.append(f"COMMAND: {' '.join(command)}\n{completed.stdout}{completed.stderr}\n")
        return read_payload(payload)

    meta, first, first_scalars, first_build_info = invoke(descriptor_only=True)
    if (int(meta["nx"]), int(meta["ny"]), int(meta["nz"])) != (nx, ny, nz):
        raise AssertionError("native driver returned a mismatched dynamic grid")
    phb3 = first["phb"].reshape(ny, nz + 1, nx)
    phb_faces = phb3[0, :, 0]
    if not np.array_equal(phb3, np.broadcast_to(phb_faces[None, :, None], phb3.shape)):
        raise AssertionError("native base PHB is not a horizontally uniform imported column")
    c = source_column(nz, nx, phb_faces)
    offsets = (0, ny * nz * (nx + 1),
               ny * nz * (nx + 1) + (ny + 1) * nz * nx,
               ny * nz * (nx + 1) + (ny + 1) * nz * nx + ny * (nz + 1) * nx,
               ny * nz * (nx + 1) + (ny + 1) * nz * nx + 2 * ny * (nz + 1) * nx,
               ny * nz * (nx + 1) + (ny + 1) * nz * nx + 2 * ny * (nz + 1) * nx + ny * nz * nx)
    pbase = first["pbase"].reshape(ny, nz, nx)
    thbase = first["thbase_perturb"].reshape(ny, nz, nx)
    mubase = first["mubase"].reshape(ny, nx)
    eta = 1.0 - (np.arange(nz, dtype=np.float64) + 0.5) / nz
    expected_pbase = (REF.PTOP + REF.MUB * eta).astype(np.float32).astype(np.float64)
    if not np.array_equal(pbase[0, :, 0], expected_pbase):
        raise AssertionError("native base pressure does not match ptop=20kPa/MUB=80kPa sigma profile")
    if np.any(thbase != 0.0) or not np.all(mubase == REF.MUB):
        raise AssertionError("native setBaseState must retain theta-base=300K and MUB=80kPa")
    ph0 = offsets[3]
    phpert = first["base"][ph0:ph0 + ny * (nz + 1) * nx].reshape(ny, nz + 1, nx)
    target_theta = 317.0 - 8.0 * eta
    target_pressure = REF.PTOP + REF.M_TOTAL * eta
    alpha_base = REF._alpha(np.full(nz, 300.0), REF.PTOP + REF.MUB * eta)
    alpha_target = REF._alpha(target_theta, target_pressure)
    expected_ph = np.r_[0.0, np.cumsum((REF.M_TOTAL * alpha_target - REF.MUB * alpha_base) / nz)]
    ph_error = float(np.max(np.abs(phpert[0, :, 0] - expected_ph)))
    ph_budget = 64.0 * np.finfo(np.float64).eps * max(1.0, float(np.max(np.abs(expected_ph))))
    if ph_error > ph_budget or not np.allclose(phpert, expected_ph[None, :, None], rtol=0.0, atol=ph_budget):
        raise AssertionError(f"native PH perturbation misses analytic EOS target: {ph_error} > {ph_budget}")
    th0 = offsets[4]
    theta_pert = first["base"][th0:th0 + ny * nz * nx].reshape(ny, nz, nx)
    expected_theta_pert = (target_theta - 300.0).astype(np.float64)
    if not np.allclose(theta_pert, expected_theta_pert[None, :, None], rtol=0.0, atol=2e-14):
        raise AssertionError("native theta perturbation does not represent 317−8η K over the 300 K base")
    # Select a true source-column wave, serialize its packed initial condition,
    # and rerun the actual native forward/VJP path with that wave.
    initial_state, frequency = native_mode_state(c, first["base"], nx, ny, nz, offsets)
    with initial_file.open("w") as f:
        f.write(f"{initial_state.size}\n")
        f.write(" ".join(f"{v:.17g}" for v in initial_state))
        f.write("\n")
    meta, a, scalars, build_info = invoke(initial_file)
    if not np.array_equal(a["phb"], first["phb"]):
        raise AssertionError("native PHB changed between descriptor and eigenmode runs")
    if first_build_info != build_info:
        raise AssertionError("native build information changed between descriptor and eigenmode runs")
    meta["mode_frequency"] = frequency
    # The serialized tangent is a same-grid directional derivative. Remove the
    # irrelevant meridional V block before comparing the source's 5-block mode.
    actual = modal_source_vector(unpack_projection(a["tangent"], nx, ny, nz, 40000.0, offsets), nz)
    actual_narrow = modal_source_vector(unpack_projection(a["tangent_narrow"], nx, ny, nz, 40000.0, offsets), nz)
    actual_projection = unpack_projection(a["tangent"], nx, ny, nz, 40000.0, offsets)
    direction = modal_source_vector(unpack_projection(a["direction"], nx, ny, nz, 40000.0, offsets), nz)
    expected = c["A"] @ direction
    floors = np.r_[np.ones(nz), np.ones(nz), np.full(nz, 100.0), np.ones(nz), 100.0]
    sizes = [nz, nz, nz, nz, 1]
    names = ["U", "W", "PH", "THETA", "MU"]
    errs, start = {}, 0
    for name, size, floor in zip(names, sizes, floors[np.cumsum([0] + sizes[:-1])]):
        errs[name] = block_error(actual[start:start + size], expected[start:start + size],
                                 f"{nx}x{ny}x{nz} tangent {name}", float(floor), 1.0e-8)
        errs[f"{name}_fd_width_closure"] = block_error(
            actual[start:start + size], actual_narrow[start:start + size],
            f"{nx}x{ny}x{nz} {name} FD-width closure", float(floor), 1.0e-8)
        start += size
    v_error = block_error(actual_projection["v"], np.zeros(nz, dtype=np.complex128),
                          f"{nx}x{ny}x{nz} tangent V", 1.0, 1.0e-8)
    errs["V"] = v_error
    # Compare every actual owned cell to a reconstructed C-grid field. This
    # keeps y-boundary and off-mode leakage visible instead of averaging it out.
    output_blocks = [("U", 0, ny*nz*(nx+1), 1.0),
                     ("V", ny*nz*(nx+1), (ny+1)*nz*nx, 1.0),
                     ("W", ny*nz*(nx+1)+(ny+1)*nz*nx, ny*(nz+1)*nx, 1.0),
                     ("PH", ny*nz*(nx+1)+(ny+1)*nz*nx+ny*(nz+1)*nx, ny*(nz+1)*nx, 100.0),
                     ("THETA", ny*nz*(nx+1)+(ny+1)*nz*nx+2*ny*(nz+1)*nx, ny*nz*nx, 1.0),
                     ("MU", ny*nz*(nx+1)+(ny+1)*nz*nx+2*ny*(nz+1)*nx+ny*nz*nx, ny*nx, 100.0)]
    canonical_worst = {name: 0.0 for name, *_ in output_blocks}
    canonical_scatter = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero_wide = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero_scatter = {name: 0.0 for name, *_ in output_blocks}
    canonical_zero_budget = {name: 0.0 for name, *_ in output_blocks}
    canonical_columns = []
    zero_violations = []
    exact_zero_threshold = 64.0 * np.finfo(float).eps
    rhs_fd_h = {"wide": 1.0e-3, "narrow": 5.0e-4}
    for col in range(4 * nz + 1):
        input_q = modal_source_vector(
            unpack_projection(a[f"direction_col_{col}"], nx, ny, nz, 40000.0, offsets), nz)
        expected_col = c["A"] @ input_q
        expected_full = reconstruct_modal_rhs(expected_col, nx, ny, nz, 40000.0, offsets)
        actual_full = a[f"tangent_col_{col}"]
        narrow_full = a[f"tangent_narrow_col_{col}"]
        col_records = {}
        for name, start, size, unit in output_blocks:
            sl = slice(start, start + size)
            expected_block = expected_full[sl]
            actual_block = actual_full[sl]
            narrow_block = narrow_full[sl]
            scaled_error = block_error(actual_block, expected_block,
                f"{nx}x{ny}x{nz} full-grid A column {col}->{name}", unit, 1.0e-8)
            width_scatter = block_error(actual_block, narrow_block,
                f"{nx}x{ny}x{nz} full-grid FD-width column {col}->{name}", unit, 1.0e-8)
            canonical_worst[name] = max(canonical_worst[name], scaled_error)
            canonical_scatter[name] = max(canonical_scatter[name], width_scatter)

            # Entries whose independent source value is analytically zero get
            # an absolute FD-roundoff bound derived from the actual +/- RHS
            # operand magnitudes and declared physical units. This prevents a
            # large PH/MU normalization floor from hiding a false coupling.
            zero_tol = exact_zero_threshold * max(1.0, float(np.max(np.abs(expected_block))))
            zero_mask = np.abs(expected_block) <= zero_tol
            zero_wide_error = float(np.max(np.abs(actual_block[zero_mask]))) if zero_mask.any() else 0.0
            zero_narrow_error = float(np.max(np.abs(narrow_block[zero_mask]))) if zero_mask.any() else 0.0
            zero_fd_scatter = float(np.max(np.abs(actual_block[zero_mask]-narrow_block[zero_mask]))) if zero_mask.any() else 0.0
            op_wide = scalars[f"rhs_operand_{col}_wide_{name}"]
            op_narrow = scalars[f"rhs_operand_{col}_narrow_{name}"]
            roundoff_bound = 16.0 * np.finfo(float).eps * max(
                op_wide / rhs_fd_h["wide"], op_narrow / rhs_fd_h["narrow"])
            unit_roundoff_bound = 64.0 * np.finfo(float).eps * unit
            zero_budget = max(roundoff_bound, unit_roundoff_bound, 2.0 * zero_fd_scatter)
            if not np.isfinite(zero_narrow_error) or zero_narrow_error > zero_budget:
                zero_violations.append({"source_column": col, "output_block": name,
                    "wide_error_abs": zero_wide_error, "narrow_error_abs": zero_narrow_error,
                    "fd_width_scatter_abs": zero_fd_scatter, "roundoff_budget_abs": zero_budget,
                    "rhs_operand_max_abs_wide": op_wide, "rhs_operand_max_abs_narrow": op_narrow})
            canonical_zero[name] = max(canonical_zero[name], zero_narrow_error)
            canonical_zero_wide[name] = max(canonical_zero_wide[name], zero_wide_error)
            canonical_zero_scatter[name] = max(canonical_zero_scatter[name], zero_fd_scatter)
            canonical_zero_budget[name] = max(canonical_zero_budget[name], zero_budget)
            col_records[name] = {
                "full_grid_scaled_error": scaled_error,
                "full_grid_fd_width_scatter": width_scatter,
                "expected_full_field_max_abs": float(np.max(np.abs(expected_block))),
                "analytic_zero_entry_count": int(zero_mask.sum()),
                "analytic_zero_response_max_abs_wide": zero_wide_error,
                "analytic_zero_response_max_abs_narrow": zero_narrow_error,
                "analytic_zero_fd_width_scatter_abs": zero_fd_scatter,
                "fd_roundoff_budget_abs": zero_budget,
                "roundoff_from_rhs_operands_abs": roundoff_bound,
                "physical_unit_roundoff_abs": unit_roundoff_bound,
                "rhs_operand_max_abs_wide": op_wide,
                "rhs_operand_max_abs_narrow": op_narrow,
            }
        canonical_columns.append({"source_column": col, "input_modal_signal": [
            [float(q.real), float(q.imag)] for q in input_q], "outputs": col_records})
    for name, *_ in output_blocks:
        errs[f"A_full_grid_{name}"] = canonical_worst[name]
        errs[f"A_full_grid_fd_width_{name}"] = canonical_scatter[name]
        errs[f"A_analytic_zero_{name}"] = canonical_zero[name]
        errs[f"A_analytic_zero_budget_{name}"] = canonical_zero_budget[name]
    if not np.isfinite(a["rhs_base"]).all() or not np.isfinite(a["vjp"]).all():
        raise AssertionError("native RHS or state-carry pullback is nonfinite")
    sizes = [ny * nz * (nx + 1), (ny + 1) * nz * nx,
             ny * (nz + 1) * nx, ny * (nz + 1) * nx, ny * nz * nx, ny * nx]
    physical_units = [1.0, 1.0, 1.0, 100.0, 1.0, 100.0]
    start = 0
    equilibrium_scaled = {}
    for name, size, unit in zip(("U", "V", "W", "PH", "THETA", "MU"), sizes, physical_units):
        value = float(np.max(np.abs(a["rhs_base"][start:start + size])) / unit)
        equilibrium_scaled[name] = value
        if value > 1.0e-12:
            raise AssertionError(f"native base {name} RHS scaled residual {value:.6g} exceeds 1e-12")
        start += size
    for steps in (1, 2):
        for block in ("W", "PH", "MU"):
            dot = scalars[f"dot{steps}_{block}"]
            fd_wide = scalars[f"fd{steps}_wide_{block}"]
            fd_narrow = scalars[f"fd{steps}_narrow_{block}"]
            scale = max(abs(dot), abs(fd_wide), abs(fd_narrow), 1.0e-12)
            adjoint_error = abs(dot - fd_narrow) / scale
            width_error = abs(fd_wide - fd_narrow) / scale
            floor = 256.0 * np.finfo(float).eps * max(abs(dot), abs(fd_wide), abs(fd_narrow), 1.0)
            if (not np.isfinite(adjoint_error) or max(abs(dot), abs(fd_wide), abs(fd_narrow)) <= floor or
                    adjoint_error > 1.0e-8 or width_error > 1.0e-8):
                raise AssertionError(f"{steps}-step {block} carry VJP/FD mismatch: dot={dot} wide={fd_wide} narrow={fd_narrow} adjoint_rel={adjoint_error} width_rel={width_error}")
            errs[f"{steps}step_{block}_vjp_fd"] = adjoint_error
            errs[f"{steps}step_{block}_fd_width"] = width_error
    adjoint_records = {}
    for steps in (1, 2):
        for block in ("W", "PH", "MU"):
            dot = scalars[f"dot{steps}_{block}"]
            wide = scalars[f"fd{steps}_wide_{block}"]
            narrow = scalars[f"fd{steps}_narrow_{block}"]
            scale = max(abs(dot), abs(wide), abs(narrow), 1.0e-12)
            adjoint_records[f"{steps}step_{block}"] = {
                "adjoint_dot": dot, "fd_wide_h_0_1": wide, "fd_narrow_h_0_05": narrow,
                "adjoint_relative_error": abs(dot - narrow) / scale,
                "fd_width_relative_scatter": abs(wide - narrow) / scale,
                "nonzero_signal_floor": 256.0 * np.finfo(float).eps *
                    max(abs(dot), abs(wide), abs(narrow), 1.0),
            }
    output_blocks = ("U", "W", "PH", "THETA", "MU")
    canonical = {
        name: {"scaled_source_error": canonical_worst[name],
               "fd_width_scatter": canonical_scatter[name], "budget": 1.0e-8}
        for name in output_blocks
    }
    mutation_budget = max(canonical_zero_budget.values())
    try:
        require_zero_response(2.0 * mutation_budget, mutation_budget, "manufactured zero-leak mutation")
        mutation_falsifier_passed = False
    except AssertionError:
        mutation_falsifier_passed = True
    if not mutation_falsifier_passed:
        raise AssertionError("absolute zero-response gate failed to reject a manufactured leak")
    evidence = {
        "dimensions": {"nx": nx, "ny": ny, "nz": nz, "lx_m": 40000.0, "ly_m": 30000.0},
        "native_build_info": build_info,
        "native_reciprocal_spacing_fp32_bits": {
            "rdx": build_info["rdx_fp32_bits"], "rdy": build_info["rdy_fp32_bits"],
        },
        "imported_phb_faces_float32_bits_hex": [
            f"0x{int(v):08x}" for v in np.asarray(phb_faces, dtype=np.float32).view(np.uint32)],
        "base_arrays": {"pbase_Pa_float32": first["pbase"].tolist(),
                        "thbase_perturb_K_float32": first["thbase_perturb"].tolist(),
                        "mubase_Pa_float32": first["mubase"].tolist()},
        "eos_gate": {"pressure_max_error_Pa": meta["eos_pressure_error"],
                     "alpha_relative_error": meta["eos_alpha_relative_error"],
                     "geometry_relative_error": meta["eos_geometry_relative_error"]},
        "divergence_damping": {"config_kdamp": meta["kdamp_config"],
                               "grid_kdamp_readback": meta["kdamp_grid"],
                               "implicit_divergence": bool(meta["implicit_divergence"])},
        "source_column": {"kappa_m_inv": float(c["kappa"]),
                          "theta_K": c["theta"].tolist(),
                          "layer_mass_kg_m2": c["layer_mass"].tolist(),
                          "N2_analytic_s_inv2": c["N2_analytic"].tolist(),
                          "layer_height_m": c["height"].tolist(),
                          "positive_layer_weights_kg_m2": c["layer_mass"].tolist(),
                          "selected_mode_omega_s_inv": frequency},
        "F0_physical_scaled_max": equilibrium_scaled,
        "canonical_source_columns_4N_plus_1": {
            "summary": canonical,
            "full_packed_field_columns": canonical_columns,
        "zero_response_gate": {
                "max_abs_error_narrow_by_output_block": canonical_zero,
                "max_abs_error_wide_by_output_block": canonical_zero_wide,
                "max_fd_width_scatter_by_output_block": canonical_zero_scatter,
                "max_abs_budget_by_output_block": canonical_zero_budget,
                "violations": zero_violations,
                "budget_formula": "max(16*eps64*max(rhs_plus_minus_wide)/h_wide,16*eps64*max(rhs_plus_minus_narrow)/h_narrow,64*eps64*declared_component_unit,2*zero_entry_fd_width_scatter_abs)",
            },
            "manufactured_zero_leak_rejected": mutation_falsifier_passed,
        },
        "carry_vjp_fd": adjoint_records,
        "input_state_sha256": hashlib.sha256(
            np.ascontiguousarray(a["initial"], dtype=np.float64).tobytes()).hexdigest(),
        "component_unit_floors": {"U_m_s": 1.0, "V_m_s": 1.0, "W_m_s": 1.0,
                                  "PH_m2_s2": 100.0, "THETA_K": 1.0, "MU_Pa": 100.0},
    }
    result = {"c": c, "nx": nx, "ny": ny, "meta": meta, "arrays": a, "scalars": scalars,
            "offsets": offsets, "equilibrium_scaled": equilibrium_scaled, "tangent_error": errs,
            "evidence": evidence,
            "canonical_columns": canonical_columns,
            "zero_violations": zero_violations,
            "canonical_metrics": {"source_error": canonical_worst, "fd_width_scatter": canonical_scatter,
                                  "analytic_zero_response_max_abs": canonical_zero,
                                  "analytic_zero_response_wide_max_abs": canonical_zero_wide,
                                  "analytic_zero_fd_width_scatter_max_abs": canonical_zero_scatter,
                                  "analytic_zero_fd_roundoff_budget_abs": canonical_zero_budget,
                                  "analytic_zero_violations": zero_violations,
                                  "manufactured_zero_leak_rejected": mutation_falsifier_passed},
            "project": lambda x: modal_source_vector(unpack_projection(x, nx, ny, nz, 40000.0, offsets), nz)}
    tmp.cleanup()
    return result


def run_damping_probe(exe: Path, component: str) -> dict:
    """Check signed damping in one U/V direction and its actual RHS adjoint dots."""
    with tempfile.TemporaryDirectory(prefix="native-damp-probe-") as tmp:
        payload=Path(tmp)/"probe.csv"
        command=[str(exe),"8","6","4",str(payload),"--damping-probe",component]
        completed=subprocess.run(command,check=False,capture_output=True,text=True)
        RUN_LOG.append(f"COMMAND: {' '.join(command)}\n{completed.stdout}{completed.stderr}\n")
        if completed.returncode:
            tail="\n".join((completed.stdout+completed.stderr).splitlines()[-80:])
            raise RuntimeError(f"damping probe {component} failed ({completed.returncode}):\n{tail}")
        meta,arrays,scalars,text_meta=read_payload(payload)
    expected_kdamp=float(np.float32(0.2))
    if text_meta.get("damping_component")!=component or meta["kdamp_config"]!=expected_kdamp or \
            meta["kdamp_grid"]!=expected_kdamp or meta["implicit_divergence"]!=1:
        raise AssertionError(f"{component} damping probe did not read back kdamp=.2/implicit_divergence=on")
    expected=arrays["damping_expected"]
    su,sv=6*4*9,7*4*8
    block=("U" if component=="U" else "V")
    start,size,unit=(0,su,1.0) if component=="U" else (su,sv,1.0)
    sl=slice(start,start+size)
    term_metrics={}
    for variant,fd_suffix,wide_name,narrow_name in (
        ("implicit_toggle","implicit","damping_implicit_path_wide","damping_implicit_path_narrow"),
        ("coefficient_toggle","coefficient","damping_coefficient_off_wide","damping_coefficient_off_narrow")):
        wide_delta=arrays[wide_name][sl]
        narrow_delta=arrays[narrow_name][sl]
        if variant=="coefficient_toggle":
            # The payload holds the raw coefficient-OFF tangent; compare the
            # active increment, just as the scalar VJP/FD contractions do.
            wide_delta=arrays["damping_on_wide"][sl]-wide_delta
            narrow_delta=arrays["damping_on_narrow"][sl]-narrow_delta
        roundoff_floor=64.0*np.finfo(float).eps*unit
        error=block_error(wide_delta,expected[sl],f"{component} {variant} signed damping increment",roundoff_floor,1.0e-8)
        width=block_error(wide_delta,narrow_delta,f"{component} {variant} damping increment FD width",roundoff_floor,1.0e-8)
        rate=float(scalars[f"damping_energy_rate_{variant}"])
        vjp=float(scalars[f"damping_vjp_dot_{variant}"])
        fdw=float(scalars[f"damping_fd_dot_{fd_suffix}_wide"])
        fdn=float(scalars[f"damping_fd_dot_{fd_suffix}_narrow"])
        jvp=float(scalars["damping_jvp_dot"])
        scale=max(abs(jvp),abs(rate),abs(vjp),abs(fdw),abs(fdn),1.0e-300)
        relative=max(abs(rate-jvp),abs(vjp-jvp),abs(fdw-jvp),abs(fdn-jvp))/scale
        if jvp>=0.0 or rate>=0.0 or vjp>=0.0 or fdw>=0.0 or fdn>=0.0 or relative>1.0e-8:
            raise AssertionError(f"{component} {variant} damping energy/JVP/VJP/FD sign or dot failed: "
                f"jvp={jvp} rate={rate} vjp={vjp} fd={fdw}/{fdn} rel={relative}")
        term_metrics[variant]={"fullfield_increment_relative_error":error,"fd_width_increment_relative_scatter":width,
            "increment_relative_budget":1.0e-8,"roundoff_unit_floor":roundoff_floor,
            "negative_energy_rate":rate,"analytic_jvp_dot":jvp,"vjp_dot":vjp,
            "fd_dot_wide":fdw,"fd_dot_narrow":fdn,"relative_dot_error":relative,
            "analytic_jvp_dot_budget":1.0e-8}
    full_implicit_abs=float(scalars["damping_full_vs_implicit_abs"])
    full_implicit_budget=float(scalars["damping_full_vs_implicit_budget"])
    if full_implicit_abs>full_implicit_budget:
        raise AssertionError(f"{component} Full/ImplicitOnly damping mismatch {full_implicit_abs} > {full_implicit_budget}")
    exclusions={}
    for mode in ("explicit_only","split_explicit"):
        error=float(scalars[f"damping_{mode}_abs"])
        budget=float(scalars[f"damping_{mode}_budget"])
        if error>budget:
            raise AssertionError(f"{component} {mode} did not exclude divergence damping: {error} > {budget}")
        exclusions[mode]={"max_abs_response":error,"fd_roundoff_budget":budget,
                          "fd_width_scatter":float(scalars[f"damping_{mode}_width_scatter"])}
    return {"component":component,"config_kdamp":meta["kdamp_config"],
        "grid_kdamp_readback":meta["kdamp_grid"],"implicit_divergence":bool(meta["implicit_divergence"]),
        "term_variants":term_metrics,
        "full_implicit_only_consistency":{"max_abs_error":full_implicit_abs,"fd_roundoff_budget":full_implicit_budget},
        "damping_exclusions":exclusions,
        "derivative_semantics":{"jvp":"independent analytical discrete damping operator",
                                 "vjp":"autograd of actual Full RHS ON minus OFF",
                                 "fd":"central finite difference of actual Full RHS ON minus OFF at two widths"}}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True, help="built test_native_wave_refinement executable")
    parser.add_argument("--output", type=Path, help="optional compact JSON evidence output")
    parser.add_argument("--source60-netcdf", type=Path,
                        help="opt in to the pinned corrected source60 WRF input")
    parser.add_argument("--phase-b-snapshot", type=Path,
                        help="PR302 Phase-B Full component packet whose input_state seeds the profile")
    parser.add_argument("--source60-gate", type=Path,
                        help="existing source60 six-field call-state gate receipt")
    parser.add_argument("--ft-pgf-reader", type=Path,
                        help="authoritative parser for the same-call Fortran P/AL PGF capture")
    parser.add_argument("--ft-pgf-snapshot", type=Path,
                        help="same-call Fortran PGF snapshot; use this when the legacy sibling path is absent")
    parser.add_argument("--diagnostic-2step", action="store_true",
                        help="run a separately labeled source-profile diagnostic map after a failed exact RHS gate")
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        raise SystemExit(f"native wave executable not found: {exe}")
    actual_args = (args.source60_netcdf, args.phase_b_snapshot, args.source60_gate,
                   args.ft_pgf_reader)
    if any(actual_args) or args.ft_pgf_snapshot:
        if not all(actual_args):
            raise SystemExit("--source60-netcdf, --phase-b-snapshot, --source60-gate, and --ft-pgf-reader must be supplied together")
        output_path = (args.output or Path("native-source-profile-result.json")).resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        bundle = output_path.with_suffix(".bundle.bin")
        receipt = write_actual_source_profile(args.source60_netcdf.resolve(),
                                              args.phase_b_snapshot.resolve(),
                                              args.source60_gate.resolve(),
                                              args.ft_pgf_reader.resolve(), bundle,
                                              args.ft_pgf_snapshot.resolve() if args.ft_pgf_snapshot else None)
        full_snapshot = args.phase_b_snapshot.resolve().parent / "port_full_rhs_snapshot.bin"
        run_dir = output_path.with_suffix(".native")
        run_dir.mkdir(parents=True, exist_ok=True)
        ingest_csv = run_dir / "ingestion.csv"
        command = [str(exe), "17", "17", "16", str(ingest_csv),
                   "--source-profile", str(bundle), "0", "15"]
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
        RUN_LOG.append(f"COMMAND: {' '.join(command)}\n{completed.stdout}{completed.stderr}\n")
        (run_dir / "ingestion.log").write_text(completed.stdout + completed.stderr)
        if completed.returncode:
            raise RuntimeError(f"native actual-source profile ingestion failed ({completed.returncode}); see {run_dir}")
        meta, arrays, scalars, text_meta = read_payload(ingest_csv)
        if text_meta.get("source_profile_status") != "ingested" or meta.get("source_profile_steps") != 0:
            raise AssertionError(f"native source-profile ingestion did not stop before stepping; raw output: {ingest_csv}")
        try:
            rhs_gate = compare_source_rhs_to_phase_b(
                arrays["rhs_base"], arrays["source_profile_projected_input_state"],
                bundle, args.phase_b_snapshot.resolve(), full_snapshot)
        except Exception as error:
            failed = {**receipt, "status": "source_ingestion_structural_gate_failed",
                      "ingestion_csv": str(ingest_csv), "phase_b_full_snapshot": str(full_snapshot),
                      "failure": str(error), "source_profile_steps_run": 0,
                      "exact_gate_relaxed": False}
            output_path.write_text(json.dumps(failed, indent=2, sort_keys=True) + "\n")
            output_path.with_suffix(".log").write_text("\n".join(RUN_LOG))
            raise AssertionError(f"source ingestion structural gate failed; diagnostic mode cannot bypass it; raw files preserved under {run_dir}: {error}") from error
        gate_error = None if rhs_gate["exact_six_field_owner_parity"] else rhs_gate["status"]
        if gate_error:
            failed = {**receipt, "status": "source_ingestion_gate_failed",
                      "ingestion_csv": str(ingest_csv), "phase_b_full_snapshot": str(full_snapshot),
                      "failure": gate_error, "source_profile_steps_run": 0,
                      "baseline_rhs_gate": rhs_gate, "exact_gate_relaxed": False}
            output_path.write_text(json.dumps(failed, indent=2, sort_keys=True) + "\n")
            output_path.with_suffix(".log").write_text("\n".join(RUN_LOG))
            if not args.diagnostic_2step:
                raise AssertionError(f"source ingestion exact RHS parity failed before temporal stepping; raw CSV/log preserved under {run_dir}")
            fields = rhs_gate["fields"]
            if any(not fields[name]["exact_float32"] for name in ("W", "PH", "THM", "MU")) or \
                    any(not math.isfinite(fields[name][metric]) for name in ("U", "V")
                        for metric in ("max_abs", "rms")):
                raise AssertionError("diagnostic 2-step mode permits only finite U/V exact-bit mismatches; W/PH/THM/MU must remain exact")
        if gate_error and not args.diagnostic_2step:
            raise AssertionError("diagnostic mode is required to continue after a failed exact RHS gate")
        csv_path = run_dir / "two_step.csv"
        command = [str(exe), "17", "17", "16", str(csv_path),
                   "--source-profile", str(bundle), "2", "15"]
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
        RUN_LOG.append(f"COMMAND: {' '.join(command)}\n{completed.stdout}{completed.stderr}\n")
        (run_dir / "two_step.log").write_text(completed.stdout + completed.stderr)
        if completed.returncode:
            raise RuntimeError(f"native actual-source two-step probe failed ({completed.returncode}); see {run_dir}")
        meta, arrays, scalars, text_meta = read_payload(csv_path)
        if text_meta.get("source_profile_status") != "accepted_with_retained_and_replay_vjp" or \
                meta.get("source_profile_probe") != 1 or meta.get("source_profile_steps") != 2:
            raise AssertionError("native source-profile output did not certify two accepted ARK steps and replay")

        # One mixed, vertically varying PH+THM direction. The scales are
        # physical amplitudes; the direction is normalized once before FD runs.
        state = arrays["source_profile_projected_input_state"].copy()
        state_count = state.size
        off_w = 9792
        off_ph, off_thm = off_w + 4913, off_w + 4913 + 4913
        if state_count != 24531 or off_thm + 4624 + 289 != state_count:
            raise AssertionError("source-profile packed field offsets changed")
        direction = np.zeros(state_count, dtype=np.float64)
        ph = np.zeros((17, 17, 17), dtype=np.float64)
        thm = np.zeros((17, 16, 17), dtype=np.float64)
        zph = np.linspace(0.0, 1.0, 17, dtype=np.float64)
        zth = np.linspace(0.0, 1.0, 16, dtype=np.float64)
        ph_profile = np.sin(np.pi * zph[1:])
        thm_profile = np.cos(np.pi * zth)
        ph_profile /= np.linalg.norm(ph_profile)
        thm_profile /= np.linalg.norm(thm_profile)
        ph[:16, 1:, :16] = (100.0 / np.sqrt(2.0) * ph_profile[None, :, None])
        thm[:16, :, :16] = (1.0 / np.sqrt(2.0) * thm_profile[None, :, None])
        direction[off_ph:off_ph + ph.size] = ph.reshape(-1)
        direction[off_thm:off_thm + thm.size] = thm.reshape(-1)
        profile_norms = {"PH_dimensionless_profile_l2": float(np.linalg.norm(ph_profile)),
                         "THM_dimensionless_profile_l2": float(np.linalg.norm(thm_profile))}
        terminal_ph = arrays["source_profile_terminal_cotangent_ph"]
        terminal_thm = arrays["source_profile_terminal_cotangent_thm"]
        grad_ph = arrays["source_profile_vjp_ph"]
        grad_thm = arrays["source_profile_vjp_thm"]
        endpoint_base = arrays["source_profile_endpoint_2"]
        vjp_pairings = {"PH": float(np.dot(grad_ph, direction)),
                        "THM": float(np.dot(grad_thm, direction))}
        fp_epsilon = np.finfo(np.float64).eps
        epsilons = (0.02, 0.01, 0.005)
        fd_records = {"PH": [], "THM": []}
        taylor_records = {"PH": [], "THM": []}
        fd_assessment = {}
        branch_runs = []
        baseline_omega = [arrays[f"source_profile_stage_omega_{i}"] for i in range(1, 9)]
        baseline_rho = parse_converged_stage_rho(run_dir / "two_step.log", repetitions=5)
        for eps in epsilons:
            endpoints = []
            omegas = []
            for sign in (-1.0, 1.0):
                override = state + sign * eps * direction
                override_path = run_dir / f"initial_{'minus' if sign < 0 else 'plus'}_{eps:g}.txt"
                with override_path.open("w") as stream:
                    stream.write(f"{override.size}\n")
                    np.savetxt(stream, override, fmt="%.17g")
                branch_csv = run_dir / f"branch_{'minus' if sign < 0 else 'plus'}_{eps:g}.csv"
                branch_cmd = [str(exe), "17", "17", "16", str(branch_csv),
                              "--source-profile", str(bundle), "1", "15", str(override_path)]
                branch = subprocess.run(branch_cmd, check=False, capture_output=True, text=True)
                RUN_LOG.append(f"COMMAND: {' '.join(branch_cmd)}\n{branch.stdout}{branch.stderr}\n")
                (run_dir / f"branch_{'minus' if sign < 0 else 'plus'}_{eps:g}.log").write_text(
                    branch.stdout + branch.stderr)
                if branch.returncode:
                    raise RuntimeError(f"source-profile branch run failed at eps={eps:g}, sign={sign:g}; see {run_dir}")
                _, branch_arrays, _, branch_text = read_payload(branch_csv)
                if branch_text.get("source_profile_status") != "accepted_branch_fd_endpoint":
                    raise AssertionError("source-profile branch output has an unexpected status")
                endpoints.append(branch_arrays["source_profile_endpoint_2"])
                omegas.append([branch_arrays[f"source_profile_stage_omega_{i}"] for i in range(1, 9)])
                branch_runs.append({"epsilon": eps, "sign": int(sign), "csv": str(branch_csv),
                                    "converged_stage_rho": parse_converged_stage_rho(
                                        run_dir / f"branch_{'minus' if sign < 0 else 'plus'}_{eps:g}.log",
                                        repetitions=2)})
            safe = True
            flips = 0
            zero_count = 0
            min_margin = float("inf")
            for stage in range(8):
                ref, neg, pos = baseline_omega[stage], omegas[0][stage], omegas[1][stage]
                if ref.size != neg.size or ref.size != pos.size:
                    raise AssertionError("source-profile omega stage shapes differ")
                # The helper emits its established WRF vertical face domain;
                # restrict branch signs to active hard-upwind faces k=2..14.
                ref3, neg3, pos3 = (x.reshape(17, 17, 17) for x in (ref, neg, pos))
                active = ref3[:16, 2:15, :16]
                neg_active = neg3[:16, 2:15, :16]
                pos_active = pos3[:16, 2:15, :16]
                flips += int(np.count_nonzero((active > 0) != (neg_active > 0)))
                flips += int(np.count_nonzero((active > 0) != (pos_active > 0)))
                zero_count += int(np.count_nonzero(active == 0) +
                                   np.count_nonzero(neg_active == 0) +
                                   np.count_nonzero(pos_active == 0))
                min_margin = min(min_margin, float(np.min(np.abs(active))))
            safe = flips == 0 and zero_count == 0
            for run in branch_runs[-2:]:
                run.update({"branch_safe": safe, "omega_sign_flips_for_epsilon": flips,
                            "active_omega_zeros_across_triplet": zero_count,
                            "minimum_active_omega_margin": min_margin})
            for field, cot in (("PH", terminal_ph), ("THM", terminal_thm)):
                objective_minus = float(np.dot(cot, endpoints[0]))
                objective_plus = float(np.dot(cot, endpoints[1]))
                fd = float((objective_plus - objective_minus) / (2.0 * eps))
                fd_floor = float(64.0 * fp_epsilon *
                            float(np.sum(np.abs(cot) *
                                (np.abs(endpoints[1]) + np.abs(endpoints[0])))) / (2.0 * eps))
                fd_records[field].append({"epsilon": eps, "central_fd": fd,
                                          "vjp_directional_pairing": vjp_pairings[field],
                                          "relative_error": abs(fd - vjp_pairings[field]) /
                                              max(abs(fd), abs(vjp_pairings[field]), 1.0e-300),
                                          "fd_roundoff_floor": fd_floor,
                                          "signal_above_floor": bool(abs(fd) > fd_floor and
                                                                     abs(vjp_pairings[field]) > fd_floor)})
                fd_records[field][-1]["branch_safe"] = safe
                fd_records[field][-1]["active_omega_zeros_across_triplet"] = zero_count
                fd_records[field][-1]["minimum_active_omega_margin"] = min_margin
                r_plus_dot = float(np.dot(cot, endpoints[1] - endpoint_base))
                r_minus_dot = float(np.dot(cot, endpoints[0] - endpoint_base))
                linear = eps * vjp_pairings[field]
                remainder_plus = r_plus_dot - linear
                remainder_minus = r_minus_dot + linear
                floor_plus = float(64.0 * fp_epsilon *
                    (float(np.sum(np.abs(cot) * (np.abs(endpoints[1]) + np.abs(endpoint_base)))) +
                     abs(linear)))
                floor_minus = float(64.0 * fp_epsilon *
                    (float(np.sum(np.abs(cot) * (np.abs(endpoints[0]) + np.abs(endpoint_base)))) +
                     abs(linear)))
                taylor_records[field].append({"epsilon": eps,
                    "R_plus": remainder_plus, "R_minus": remainder_minus,
                    "roundoff_floor_plus": floor_plus, "roundoff_floor_minus": floor_minus,
                    "above_roundoff_floor": bool(abs(remainder_plus) > floor_plus and
                                                  abs(remainder_minus) > floor_minus),
                    "branch_safe": safe})
        for field in ("PH", "THM"):
            rows = fd_records[field]
            ratios = []
            taylor = taylor_records[field]
            for i, (a, b) in enumerate(zip(taylor, taylor[1:])):
                for side in ("plus", "minus"):
                    ra, rb = abs(a[f"R_{side}"]), abs(b[f"R_{side}"])
                    floor_ok = a["above_roundoff_floor"] and b["above_roundoff_floor"]
                    ratio = ra / rb if rb > 0.0 else None
                    ratios.append({"epsilon_pair": [a["epsilon"], b["epsilon"]],
                                   "side": side, "ratio": ratio,
                                   "above_roundoff_floor": floor_ok,
                                   "accepted": bool(floor_ok and ratio is not None and 3.5 <= ratio <= 4.5)})
            fd_pass = all(row["branch_safe"] and row["signal_above_floor"] and
                          row["relative_error"] < 5.0e-4 for row in rows)
            taylor_pass = len(ratios) == 4 and all(x["accepted"] for x in ratios)
            for row in rows:
                row["accepted_for_derivative"] = fd_pass
            for idx, item in enumerate(taylor):
                item["adjacent_ratios"] = [r for r in ratios
                    if r["epsilon_pair"][0] == item["epsilon"]]
            fd_assessment[field] = {
                "status": "accepted" if fd_pass and taylor_pass else "inconclusive",
                "fd_central_difference_pass": fd_pass,
                "taylor_remainder_pass": taylor_pass,
                "adjacent_taylor_remainder_ratios": ratios,
                "criteria": {"relative_error_lt": 5.0e-4,
                             "signal_above_roundoff_floor": True,
                             "roundoff_floor": "64*eps64 times absolute cotangent-weighted endpoint magnitudes; conservative numerical screen, not a formal state/gradient error bound",
                             "adjacent_error_ratio": [3.5, 4.5],
                             "adjacent_error_ratio_definition": "|R(epsilon)|/|R(epsilon/2)| for R_plus=dot(lambda,y_plus-y0)-epsilon*AD and R_minus=dot(lambda,y_minus-y0)+epsilon*AD",
                             "requires_branch_safe": True}}

        receipt.update({"status": "diagnostic_source_profile_map_complete" if gate_error else "native_source_profile_map_complete",
                        "baseline_rhs_gate": rhs_gate,
                        "baseline_rhs_gate_failure": gate_error,
                        "exact_gate_relaxed": False,
                        "temporal_result_scope": "diagnostic_map_only" if gate_error else "source_profile_map",
                        "phase_b_full_snapshot": str(full_snapshot),
                        "native_output_meta": meta,
                        "native_text_meta": text_meta,
                        "mixed_direction": {"component_normalization": "each dimensionless vertical profile has per-column L2 norm 1, repeated over physical XY owners, then weighted 1/sqrt(2)",
                                             "profile_norms": profile_norms,
                                             "ph_scale_m2_s2": 100.0,
                                             "thm_scale_k": 1.0, "epsilon_candidates": epsilons},
                        "vjp_directional_pairings": vjp_pairings,
                        "finite_difference": fd_records,
                        "taylor_remainders": taylor_records,
                        "finite_difference_assessment": fd_assessment,
                        "newton_convergence_diagnostics": {
                            "baseline": baseline_rho,
                            "finite_difference_branches": [r["converged_stage_rho"] for r in branch_runs],
                            "configured_newton_tolerance": meta.get("newton_tol_config"),
                            "configured_krylov_tolerance": meta.get("krylov_tol_config"),
                            "scope": "ρ reports converged-stage nonlinear residual only; it is not a state/gradient error bound"},
                        "derivative_semantics": {
                            "jvp": "central finite difference of the two-step endpoint vector along the pinned mixed PH+THM direction",
                            "vjp": "retained and full-replay pullbacks for separate PH-only and THM-only terminal cotangents",
                            "omega": "authoritative compute_wrf_ww_cp evaluated from each of the eight accepted ARK stage states",
                            "map_claim": "source-profile diagnostic map; no claim of exact six-field source Full-RHS parity when that gate failed"},
                        "omega_branch_checks": branch_runs,
                        "replay_endpoint_and_vjp_exact": meta.get("source_profile_replay_exact") == 1.0,
                        "branch_runs": branch_runs,
                        "arrays": {k: {"count": int(v.size), "finite": bool(np.isfinite(v).all()),
                                       "max_abs": float(np.max(np.abs(v))) if v.size else 0.0}
                                   for k, v in arrays.items()},
                        "native_run_log": str(output_path.with_suffix(".log")),
                        "native_raw_dir": str(run_dir),
                        "executable": str(exe), "executable_sha256": sha256_file(exe),
                        "cpp_sha256": sha256_file(ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"),
                        "python_sha256": sha256_file(Path(__file__).resolve())})
        output_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
        output_path.with_suffix(".log").write_text("\n".join(RUN_LOG))
        print(json.dumps({"status": receipt["status"], "output": str(output_path),
                          "bundle_sha256": receipt["bundle_sha256"],
                          "source_profile_steps": meta["source_profile_steps"]}, sort_keys=True))
        return
    coarse = run_grid(exe, 8, 6, 4)
    fine = run_grid(exe, 16, 12, 8)
    damping_probe = {component: run_damping_probe(exe,component) for component in ("U","V")}
    propagation_records = {}
    for label, run in (("coarse", coarse), ("fine", fine)):
        c, arrays = run["c"], run["arrays"]
        print(f"NATIVE_WAVE grid={label} nx={run['nx']} ny={run['ny']} nz={c['nz']} "
              f"kappa={c['kappa']:.12g} mode_omega={run['meta']['mode_frequency']:.12g} "
              f"phb_source={c['phi_base_source']}")
        for name, value in run["equilibrium_scaled"].items():
            print(f"NATIVE_WAVE_BASE_RHS block={name} physical_scaled_max={value:.6e} budget=1.000000e-12")
        for name, value in run["tangent_error"].items():
            print(f"NATIVE_WAVE_GATE check={name} scaled_error={value:.6e} budget=1.000000e-08")
        for name in ("U", "V", "W", "PH", "THETA", "MU"):
            metrics=run["canonical_metrics"]
            violated=sum(v["output_block"]==name for v in run["zero_violations"])
            print(f"NATIVE_WAVE_FULLFIELD block={name} source_error={metrics['source_error'].get(name,0.0):.6e} "
                  f"fd_scatter={metrics['fd_width_scatter'].get(name,0.0):.6e} "
                  f"analytic_zero_narrow={metrics['analytic_zero_response_max_abs'].get(name,0.0):.6e} "
                  f"analytic_zero_budget={metrics['analytic_zero_fd_roundoff_budget_abs'].get(name,0.0):.6e} "
                  f"violations={violated}")
        # A source-selected eigenmode is advanced through 90 accepted native
        # steps, then compared with the same-grid matrix exponential per block.
        q0 = run["project"](arrays["initial"])
        q1 = run["project"](arrays["prop90"])
        propagation_time = run["meta"]["dt"] * run["meta"]["propagation_steps"]
        predicted = expm(propagation_time * c["A"]) @ q0
        blocks = [("U", slice(0, c["nz"]), 1.0),
                  ("W", slice(c["nz"], 2*c["nz"]), 1.0),
                  ("PH", slice(2*c["nz"], 3*c["nz"]), 100.0),
                  ("THETA", slice(3*c["nz"], 4*c["nz"]), 1.0),
                  ("MU", slice(4*c["nz"], 4*c["nz"]+1), 100.0)]
        for name, sl, floor in blocks:
            err = block_error(q1[sl], predicted[sl], f"{label} propagation {name}", floor, 1.0e-8)
            propagation_records.setdefault(label, {})[name] = {
                "scaled_error": err, "budget": 1.0e-8, "unit_floor": floor,
                "native_signal_norm": float(np.linalg.norm(q1[sl])),
                "reference_signal_norm": float(np.linalg.norm(predicted[sl])),
            }
            print(f"NATIVE_WAVE_PROPAGATION block={name} scaled_error={err:.6e} budget=1.000000e-08 "
                  f"signal={np.linalg.norm(q1[sl]):.6e} time_s={propagation_time:.1f}")
        run["evidence"]["propagation"] = {
            "steps": int(run["meta"]["propagation_steps"]), "dt_s": run["meta"]["dt"],
            "duration_s": propagation_time,
            "phase_rad": run["meta"]["mode_frequency"] * propagation_time,
            "blocks": propagation_records[label],
        }
    all_zero_violations = coarse["zero_violations"] + fine["zero_violations"]
    pass_status = not all_zero_violations
    if pass_status:
        print("NATIVE_WAVE_REFINEMENT PASS: all source columns, carry adjoints and 90-second eigenmode propagation pass")
    else:
        print(f"NATIVE_WAVE_REFINEMENT FAIL: {len(all_zero_violations)} analytic-zero fullfield responses exceed the RHS-derived FD roundoff bound")
    print("NATIVE_WAVE_DAMPING_PROBE " + json.dumps(damping_probe,sort_keys=True))
    if args.output:
        output_path = args.output.resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            source_revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             check=True, capture_output=True, text=True).stdout.strip()
        except (subprocess.SubprocessError, FileNotFoundError):
            source_revision = "unavailable"
        evidence = {
            "schema": "native-wave-refinement-v1", "status": "pass" if pass_status else "fail",
            "source_revision": source_revision,
            "runtime": {"python": platform.python_version(), "numpy": np.__version__,
                        "scipy": scipy.__version__,
                        "native_torch": coarse["evidence"]["native_build_info"]["native_torch_version"],
                        "native_compiler": coarse["evidence"]["native_build_info"]["native_compiler_version"]},
            "artifacts": {"executable": str(exe), "executable_sha256": sha256_file(exe),
                          "cpp_sha256": sha256_file(ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"),
                          "python_sha256": sha256_file(Path(__file__).resolve()),
                          "source_reference_sha256": sha256_file(REF_PATH),
                          "execution_log": str(output_path.with_suffix(".log"))},
            "contracts": {"F0_physical_scaled_budget": 1.0e-12,
                          "canonical_source_scaled_budget": 1.0e-8,
                          "carry_vjp_fd_relative_budget": 1.0e-8,
                          "native_propagation_scaled_budget": 1.0e-8,
                          "rhs_fd_widths": [1.0e-3, 5.0e-4],
                          "carry_fd_widths": [0.1, 0.05],
                          "reference_propagation": "source-column matrix exponential"},
            "divergence_damping_term_identification": {
                "formula": "positive kdamp*grad(div) gives negative discrete Laplacian on U/V modes",
                "probes": damping_probe,
                "closure": "signed expected U/V tendency, negative kinetic-energy rate, isolated ON-OFF reverse-mode directional VJP and two-width FD dots",
            },
            "grids": {"coarse": coarse["evidence"], "fine": fine["evidence"]},
        }
        output_path.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
        output_path.with_suffix(".log").write_text("\n".join(RUN_LOG))
    if not pass_status:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
