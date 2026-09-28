#!/usr/bin/env python3
"""Actual option-2 V RHS against source-extracted WRF H/Z operators."""
from __future__ import annotations

import argparse
import importlib.util
import math
import os
from pathlib import Path
import struct
import subprocess


def load_geometry_module(path: Path):
    spec = importlib.util.spec_from_file_location("option2_momentum_geometry", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load source-extracted WRF oracle")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_cpp(binary: Path, mode: str):
    result = subprocess.run([str(binary), mode], check=True, text=True,
                            capture_output=True)
    fields_by_name: dict[str, dict[tuple[int, int, int], float]] = {}
    meta: dict[str, str] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "VACT_META":
            meta.update(item.split("=", 1) for item in fields[1:])
        elif fields[0] in {"VACT_RAW_H", "VACT_RAW_Z", "VACT_H_RATE",
                           "VACT_Z_RATE", "VACT_BOTH_RATE", "VACT_M_V",
                           "VACT_MSFVX", "VACT_ALPHA_V", "VACT_OFF_V",
                           "VACT_ON_H", "VACT_ON_Z", "VACT_ON_BOTH",
                           "VACT_RHO", "VACT_Z_PROFILE_RAW",
                           "VACT_Z_PRODUCT_MUTANT"}:
            label, j, k, i, value = fields
            fields_by_name.setdefault(label, {})[(int(j), int(k), int(i))] = float(value)
    return fields_by_name, meta, result.stdout


def f32(value: float) -> float:
    """Round one operation result to the C++ fixture's float32 storage."""
    return struct.unpack("f", struct.pack("f", value))[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    oracle = load_geometry_module(repo / "external/libtorch_wrf/sdirk3/tests/test_option2_momentum_geometry.py")
    cases = (
        ("physical", "--option2-momentum-v-actual-rhs-physical", "actual-v-rhs", False, oracle.MAP, False),
        ("packed", "--option2-momentum-v-actual-rhs-packed", "actual-v-rhs-packed", True, oracle.MAP, False),
        ("map1", "--option2-momentum-v-actual-rhs-map1", "actual-v-rhs-map1", False, 1.0, False),
        ("yvar", "--option2-momentum-v-actual-rhs-yvary-physical", "actual-v-rhs-yvary", False, oracle.MAP, True),
        ("yvar-packed", "--option2-momentum-v-actual-rhs-yvary-packed", "actual-v-rhs-yvary-packed", True, oracle.MAP, True),
    )
    all_ok = True
    receipts = repo / ".validation/v-step10"
    receipts.mkdir(parents=True, exist_ok=True)
    eps32 = 2.0**-23
    for name, mode, profile, packed, map_value, nonuniform in cases:
        work = receipts / f"fortran-{name}"
        fortran, routine_hash = oracle.compiled_fortran_oracle(
            repo, args.fortran_compiler, ["-O0"], work, profile=profile)
        cpp, meta, stdout = parse_cpp(args.binary, mode)
        (receipts / f"cpp-{name}-source-test.stdout").write_text(stdout)
        core_ny = oracle.NY - 1 if packed else oracle.NY
        owned_i = range(oracle.NX - 1) if packed else range(oracle.NX)
        owned_j = range(1, oracle.NY - 1) if packed else range(1, oracle.NY)
        owned = {(j, k, i) for j in owned_j for k in range(oracle.NZ) for i in owned_i}
        labels = ("VACT_RAW_H", "VACT_RAW_Z", "VACT_H_RATE", "VACT_Z_RATE",
                  "VACT_BOTH_RATE", "VACT_OFF_V", "VACT_ON_H", "VACT_ON_Z",
                  "VACT_ON_BOTH", "VACT_M_V", "VACT_ALPHA_V")
        for label in labels:
            if not owned.issubset(cpp.get(label, {})):
                raise RuntimeError(f"{name}: missing C++ {label} owned cells")
        for label in ("V_H_RAW", "V_Z_RAW", "RHS_V"):
            if not owned.issubset(fortran.get(label, {})):
                raise RuntimeError(f"{name}: missing Fortran {label} owned cells")

        # Mirror the positive symmetric y mass perturbation in the C++ fixture.
        mass_cell = {}
        for j in range(core_ny):
            angle = f32(2.0 * math.pi * (j + 0.5) / core_ny)
            sy = f32(math.cos(angle))
            perturbation = f32(8000.0 * sy) if nonuniform else 0.0
            mass_cell[j] = f32(80000.0 + perturbation)
        mass_face = {}
        for j in range(oracle.NY + 1):
            if j == 0:
                mass_face[j] = mass_cell[0]
            elif j >= core_ny:
                mass_face[j] = mass_cell[core_ny - 1]
            else:
                mass_face[j] = f32(0.5 * f32(mass_cell[j - 1] + mass_cell[j]))
        c1_profile = (0.90, 1.00, 1.10, 1.20) if nonuniform else (1.0,) * oracle.NZ
        c2_profile = (14000.0, 13000.0, 11000.0, 9000.0) if nonuniform else (12000.0,) * oracle.NZ
        c1_profile = tuple(f32(value) for value in c1_profile)
        c2_profile = tuple(f32(value) for value in c2_profile)
        layer_mass_at = {q: f32(f32(c1_profile[q[1]] * mass_face[q[0]]) + c2_profile[q[1]])
                         for q in owned}
        alpha_at = {q: f32(layer_mass_at[q] / map_value) for q in owned}
        source_rate = {q: fortran["RHS_V"][q] * map_value / layer_mass_at[q] for q in owned}
        source_h = {q: fortran["V_H_RAW"][q] / layer_mass_at[q] for q in owned}
        source_z = {q: fortran["V_Z_RAW"][q] / layer_mass_at[q] for q in owned}
        source_signal = max(abs(value) for value in source_rate.values())
        layer_mass_min = min(layer_mass_at.values())

        h_error = max(abs(cpp["VACT_RAW_H"][q] - fortran["V_H_RAW"][q]) for q in owned)
        z_error = max(abs(cpp["VACT_RAW_Z"][q] - fortran["V_Z_RAW"][q]) for q in owned)
        h_signal = max(abs(fortran["V_H_RAW"][q]) for q in owned)
        z_signal = max(abs(fortran["V_Z_RAW"][q]) for q in owned)
        h_budget = 2e-6 * max(1e-12, h_signal)
        z_budget = 2e-6 * max(1e-12, z_signal)
        source_normalization_error = max(
            abs((fortran["V_H_RAW"][q] + fortran["V_Z_RAW"][q]) / map_value -
                fortran["RHS_V"][q]) for q in owned)
        source_budget = 2e-6 * max(1e-12, source_signal)
        h_rate_error = max(abs(cpp["VACT_H_RATE"][q] - source_h[q]) for q in owned)
        z_rate_error = max(abs(cpp["VACT_Z_RATE"][q] - source_z[q]) for q in owned)
        both_rate_error = max(abs(cpp["VACT_BOTH_RATE"][q] - source_rate[q]) for q in owned)
        linearity_error = max(abs(cpp["VACT_BOTH_RATE"][q] - cpp["VACT_H_RATE"][q] -
                                  cpp["VACT_Z_RATE"][q]) for q in owned)
        off_signal = max(abs(cpp["VACT_OFF_V"][q]) for q in owned)
        on_signal = max(abs(cpp["VACT_ON_BOTH"][q]) for q in owned)
        raw_source_error = {q: abs(cpp["VACT_RAW_H"][q] - fortran["V_H_RAW"][q]) +
                               abs(cpp["VACT_RAW_Z"][q] - fortran["V_Z_RAW"][q]) for q in owned}
        onoff_floor = max(
            4.0 * eps32 * max(abs(cpp["VACT_ON_BOTH"][q]), abs(cpp["VACT_OFF_V"][q])) +
            source_budget + raw_source_error[q] / layer_mass_min for q in owned)
        mass_error = max(abs(cpp["VACT_M_V"][q] - mass_face[q[0]]) for q in owned)
        layer_mass_error = max(abs(cpp["VACT_ALPHA_V"][q] - alpha_at[q]) for q in owned)
        one_sided_layer = {q: c1_profile[q[1]] * mass_cell[q[0]] + c2_profile[q[1]]
                           for q in owned}
        one_sided_rate = {q: (fortran["V_H_RAW"][q] + fortran["V_Z_RAW"][q]) /
                             one_sided_layer[q] for q in owned}
        one_sided_error = max(abs(one_sided_rate[q] - source_rate[q]) for q in owned)
        old_mutant = {q: fortran["V_H_RAW"][q] / alpha_at[q] +
                         fortran["V_Z_RAW"][q] / cpp["VACT_M_V"][q] for q in owned}
        mutant_error = max(abs(old_mutant[q] - source_rate[q]) for q in owned)
        order_error = float(meta.get("same_solver_order_error", "inf"))
        one_sided_mutant_ok = (not nonuniform or one_sided_error > 10.0 * onoff_floor)
        base_state_ok = (meta.get("packed") == ("1" if packed else "0") and
                         abs(float(meta.get("M_v", "nan")) - 80000.0) <= 1e-4 and
                         (meta.get("nonuniform") == ("1" if nonuniform else "0")))
        rho_error = 0.0
        rho_spread = 0.0
        rho_ok = True
        if nonuniform:
            rho_keys = {(j,k,i) for j in range(core_ny) for k in range(oracle.NZ)
                        for i in range(oracle.NX)}
            if not rho_keys.issubset(cpp.get("VACT_RHO",{})) or not rho_keys.issubset(fortran.get("V_RHO_RAW",{})):
                raise RuntimeError(f"{name}: nonuniform EOS rho receipt is incomplete")
            rho_error = max(abs(cpp["VACT_RHO"][q] - fortran["V_RHO_RAW"][q]) for q in rho_keys)
            rho_signal = max(abs(fortran["V_RHO_RAW"][q]) for q in rho_keys)
            rho_spread = max(fortran["V_RHO_RAW"][q] for q in rho_keys) - min(fortran["V_RHO_RAW"][q] for q in rho_keys)
            rho_ok = rho_error <= 2e-6 * max(1e-12, rho_signal) and rho_spread > 1e-4
            if not owned.issubset(cpp.get("VACT_Z_PROFILE_RAW", {})) or not owned.issubset(fortran.get("V_Z_PROFILE_RAW", {})):
                raise RuntimeError(f"{name}: variable-K direct stress receipt is incomplete")
            profile_signal = max(abs(fortran["V_Z_PROFILE_RAW"][q]) for q in owned)
            profile_error = max(abs(cpp["VACT_Z_PROFILE_RAW"][q] -
                                    fortran["V_Z_PROFILE_RAW"][q]) for q in owned)
            profile_budget = 2e-6 * max(1e-12, profile_signal)
            product_mutant_signal = max(abs(cpp["VACT_Z_PRODUCT_MUTANT"][q] -
                                            cpp["VACT_Z_PROFILE_RAW"][q]) for q in owned)
            profile_ok = (profile_error <= profile_budget and
                          product_mutant_signal > 10.0 * profile_budget)
        else:
            rho_ok = float(meta.get("rho_error", "inf")) <= 2e-6
            profile_error = profile_budget = product_mutant_signal = 0.0
            profile_ok = True
        mode_ok = (h_error <= h_budget and z_error <= z_budget and
                   source_normalization_error <= 2e-6 * max(1e-12, max(abs(v) for v in fortran["RHS_V"].values())) and
                   h_rate_error <= onoff_floor and z_rate_error <= onoff_floor and
                   both_rate_error <= onoff_floor and linearity_error <= onoff_floor and
                   order_error <= onoff_floor and one_sided_mutant_ok and
                   mutant_error > 10.0 * onoff_floor and
                   mass_error <= 3.0 * eps32 * max(abs(v) for v in mass_face.values()) and
                   layer_mass_error <= 3.0 * eps32 * max(abs(v) for v in alpha_at.values()) and
                   base_state_ok and rho_ok and profile_ok)
        print(f"VACT_SOURCE {name} owned={len(owned)} raw_H/Z={h_error:.9g}/{z_error:.9g} "
              f"raw_budget={h_budget:.3g}/{z_budget:.3g} source_H/Z/total={h_rate_error:.9g}/"
              f"{z_rate_error:.9g}/{both_rate_error:.9g} ONOFF_floor={onoff_floor:.9g} "
              f"mass/L_error={mass_error:.9g}/{layer_mass_error:.9g} rho_error/spread="
              f"{rho_error:.9g}/{rho_spread:.9g} one-sided-mutant={one_sided_error:.9g} "
              f"variable-K profile error/budget={profile_error:.9g}/{profile_budget:.3g} "
              f"I(rho*K) mutant separation={product_mutant_signal:.9g} "
              f"old-scale-mutant={mutant_error:.9g} same-order={order_error:.9g} "
              f"state_ok={base_state_ok and rho_ok} routine_sha={routine_hash}")
        (receipts / f"fortran-{name}-source-test.txt").write_text(
            f"profile={profile}\nroutine_sha={routine_hash}\nowned={len(owned)}\n"
            f"raw_H_error={h_error:.17g}\nraw_Z_error={z_error:.17g}\n"
            f"source_H_rate_error={h_rate_error:.17g}\nsource_Z_rate_error={z_rate_error:.17g}\n"
            f"source_total_rate_error={both_rate_error:.17g}\nONOFF_floor={onoff_floor:.17g}\n"
            f"rho_error={rho_error:.17g}\nrho_spread={rho_spread:.17g}\n"
            f"variable_K_profile_error={profile_error:.17g}\n"
            f"variable_K_profile_budget={profile_budget:.17g}\n"
            f"product_interpolation_mutant_separation={product_mutant_signal:.17g}\n"
            f"mass_error={mass_error:.17g}\nlayer_mass_error={layer_mass_error:.17g}\n"
            f"one_sided_error={one_sided_error:.17g}\nold_mutant_error={mutant_error:.17g}\n"
            f"same_solver_order_error={order_error:.17g}\nmode_ok={mode_ok}\n")
        all_ok = all_ok and mode_ok
    if not all_ok:
        return 1
    print("PASS option-2 V actual RHS physical/packed/map1/y-varying source contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
