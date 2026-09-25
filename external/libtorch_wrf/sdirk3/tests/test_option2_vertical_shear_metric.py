#!/usr/bin/env python3
"""Diagnose vertical shear metric fallback against WRF's D13/D23 equations."""
from __future__ import annotations

import argparse
import hashlib
import math
import subprocess
from pathlib import Path

DZ_M = 1024.0
RDNW = 10.0
DELTA_VELOCITY = 0.5
EXPECTED_RDZ = 1.0 / DZ_M
EXPECTED_SHEAR = DELTA_VELOCITY * EXPECTED_RDZ
LEGACY_SHEAR = DELTA_VELOCITY * 2.0 * RDNW


def extract_routine(source: str, name: str) -> str:
    start = source.index(f"SUBROUTINE {name}(")
    marker = f"END SUBROUTINE {name}"
    end = source.index(marker, start) + len(marker)
    return source[start:end]


def check_fortran_equations(source: str) -> str:
    routine = extract_routine(source, "cal_deform_and_div")
    normalized = " ".join(routine.replace("&", " ").split()).lower()
    equations = (
        "tmp1(i,k,j) = ( u(i,k,j) - u(i,k-1,j) ) * 0.5 * ( rdz(i,k,j) + rdz(i-1,k,j) )",
        "defor13(i,k,j) = defor13(i,k,j) + tmp1(i,k,j)",
        "tmp1(i,k,j) = ( v(i,k,j) - v(i,k-1,j) ) * 0.5 * ( rdz(i,k,j) + rdz(i,k,j-1) )",
        "defor23(i,k,j) = defor23(i,k,j) + tmp1(i,k,j)",
    )
    for equation in equations:
        if equation not in normalized:
            raise RuntimeError(f"WRF cal_deform_and_div equation changed; missing {equation!r}")
    return routine


def parse_diag(stdout: str, axis: str) -> dict[str, float]:
    prefix = f"RDZ_{axis}"
    for line in stdout.splitlines():
        fields = line.split()
        if prefix in fields:
            result = {}
            for item in fields[fields.index(prefix) + 1:]:
                if "=" in item:
                    key, value = item.split("=", 1)
                    try:
                        result[key] = float(value)
                    except ValueError:
                        continue
            return result
    raise RuntimeError(f"C++ diagnostic did not emit {prefix}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--cpp-source", type=Path,
                        help="implementation source corresponding to --binary")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    source_path = repo / "dyn_em/module_diffusion_em.F"
    test_path = Path(__file__).resolve().with_name("test_scalar_diffusion_contract.cpp")
    cpp_path = (args.cpp_source or
                repo / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp").resolve()
    source = source_path.read_text()
    routine = check_fortran_equations(source)
    run = subprocess.run([str(args.binary), "--vertical-shear-rdz-metric"],
                         check=True, text=True, capture_output=True)
    routine_sha = hashlib.sha256(routine.encode()).hexdigest()
    print(f"FIXTURE_REVISION {subprocess.run(['git','rev-parse','HEAD'],cwd=repo,check=True,text=True,capture_output=True).stdout.strip()}")
    print(f"CPP_REVISION {subprocess.run(['git','rev-parse','HEAD'],cwd=cpp_path.parents[3],check=True,text=True,capture_output=True).stdout.strip()}")
    print(f"FORTRAN_SOURCE_SHA256 {hashlib.sha256(source_path.read_bytes()).hexdigest()}")
    print(f"FORTRAN_CAL_DEFORM_AND_DIV_SHA256 {routine_sha}")
    print(f"CPP_SOURCE_SHA256 {hashlib.sha256(cpp_path.read_bytes()).hexdigest()}")
    print(f"CPP_TEST_FIXTURE_SHA256 {hashlib.sha256(test_path.read_bytes()).hexdigest()}")
    print(f"CPP_BINARY_SHA256 {hashlib.sha256(args.binary.read_bytes()).hexdigest()}")
    print(f"INPUT dz_m={DZ_M:g} rdnw={RDNW:g} cache_rdz={EXPECTED_RDZ:.17g} delta_u=delta_v={DELTA_VELOCITY:g}")
    print(f"SOURCE_EXPECTED shear=delta_velocity*rdz={EXPECTED_SHEAR:.17g}; legacy_2rdnw={LEGACY_SHEAR:.17g}")
    for axis in ("U", "V"):
        values = parse_diag(run.stdout, axis)
        print(f"{axis} " + " ".join(f"{key}={value:.17g}" for key,value in values.items()))
        fallback_reproduces = (abs(values.get("fallback", math.inf)-LEGACY_SHEAR) <= 1.0e-6 and
                               values.get("fallback_max_error", 0.0) > 9.99)
        stage_matches = (abs(values.get("expected", math.inf)-EXPECTED_SHEAR) <= 1.0e-9 and
                         abs(values.get("stage", math.inf)-EXPECTED_SHEAR) <= 1.0e-7 and
                         values.get("stage_max_error", math.inf) <= 1.0e-7)
        print(("PASS" if fallback_reproduces else "FAIL") + f" {axis} cached-metric shape fallback is 2*rdnw")
        print(("PASS" if stage_matches else "FAIL") + f" {axis} explicit stage rdz matches WRF flat shear")
        if not (fallback_reproduces and stage_matches):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
