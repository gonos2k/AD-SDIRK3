#!/usr/bin/env python3
"""Small source-grounded Fortran oracle for option-2 U-X terrain stress.

This is an explicit numeric transcription of the relevant WRF loops, not a
compiled extraction of module_diffusion_em.F.  The source guards below pin the
equations and call chain from which the transcription was made.
"""
from __future__ import annotations

import argparse
import hashlib
import math
import re
import subprocess
import sys
from pathlib import Path

NX, NY, NZ = 8, 6, 4
DX, DZ, GRAVITY, KH, RHO = 1000.0, 1000.0, 9.81, 2.0, 1.0


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def check_source(repo: Path) -> str:
    source_path = repo / "dyn_em/module_diffusion_em.F"
    source = source_path.read_text()
    metrics = section(source, "SUBROUTINE compute_diff_metrics", "END SUBROUTINE compute_diff_metrics")
    strain = section(source, "SUBROUTINE cal_deform_and_div", "END SUBROUTINE cal_deform_and_div")
    stress = section(source, "SUBROUTINE cal_titau_11_22_33", "END SUBROUTINE cal_titau_11_22_33")
    diffusion = section(source, "SUBROUTINE horizontal_diffusion_u_2", "END SUBROUTINE horizontal_diffusion_u_2")
    required = [
        (metrics, "z_at_w(i,k,j) = ( ph(i,k,j) + phb(i,k,j) ) / g"),
        (metrics, "zx(i,k,j) = zx(i,k,j) + rdx * ( ph(i,k,j) - ph(i-1,k,j) ) / g"),
        (strain, "tmpzx       = 0.25 * ("),
        (strain, "tmp1(i,k,j) = ( hatavg(i,k+1,j) - hatavg(i,k,j) ) *tmpzx * rdzw(i,k,j)"),
        (stress, "titau(i,k,j) = - rho(i,k,j) * xkx(i,k,j) * defor(i,k,j)"),
        (diffusion, "tmpdz = (1./rdzw(i,k,j)+1./rdzw(i-1,k,j))/2."),
        (diffusion, "mrdx*(titau1(i,k,j  ) - titau1(i-1,k,j))"),
        (diffusion, "msfux(i,j)*zx_at_u(i,k,j)*(titau1avg(i,k+1,j)-titau1avg(i,k,j)) / tmpdz"),
    ]
    normalized = [(re.sub(r"\s+", " ", body), re.sub(r"\s+", " ", needle))
                  for body, needle in required]
    for body, needle in normalized:
        if needle not in body:
            raise RuntimeError(f"Fortran source contract changed; missing: {needle}")
    caller = (repo / "dyn_em/module_first_rk_step_part2.f90").read_text()
    for routine in ("compute_diff_metrics", "cal_deform_and_div", "horizontal_diffusion_2"):
        if f"CALL {routine}" not in caller:
            raise RuntimeError(f"Fortran caller contract changed; missing CALL {routine}")
    return hashlib.sha256(source_path.read_bytes()).hexdigest()


def fortran_oracle() -> dict[tuple[int, int, int], float]:
    # PHB is flat and stage PH adds H_i at every W level. Thus the Fortran
    # compute_diff_metrics input PH+PHB has z_w = 1000*k + H_i.
    terrain = [100.0 * math.cos(2.0 * math.pi * i / NX) for i in range(NX)]
    zx = [[0.0] * NX for _ in range(NZ + 1)]
    rdzw = [[0.0] * NX for _ in range(NZ)]
    for k in range(NZ + 1):
        for face in range(NX):
            # periodic_x branch: zx(face) = rdx * (z(face)-z(face-1))
            zx[k][face] = (terrain[face] - terrain[(face - 1) % NX]) / DX
    for k in range(NZ):
        for i in range(NX):
            rdzw[k][i] = 1.0 / DZ

    # Fortran cal_deform_and_div: u=[0,0,0,1], unit maps, fnm=fnp=1/2.
    # The selected mass level is zero-based k=1 (Fortran level 2): its stress
    # averages use interior W levels. The lower mass stresses vanish because
    # the first three U levels are zero, independent of cf1/cf2/cf3. The top
    # extrapolation is outside the selected tendency stencil.
    u = [0.0, 0.0, 0.0, 1.0]
    hatavg = [0.0] * (NZ + 1)
    hatavg[0] = 0.5 * u[0]
    for q in range(1, NZ):
        hatavg[q] = 0.5 * (u[q] + u[q - 1])
    hatavg[NZ] = hatavg[NZ - 1]  # top value is not used in selected mass level

    d11 = [[0.0] * NX for _ in range(NZ)]
    for k in range(NZ):
        for i in range(NX):
            tmpzx = 0.25 * (zx[k][i] + zx[k][(i + 1) % NX] +
                            zx[k + 1][i] + zx[k + 1][(i + 1) % NX])
            d11[k][i] = -2.0 * (hatavg[k + 1] - hatavg[k]) * tmpzx * rdzw[k][i]

    tau = [[-RHO * KH * d11[k][i] for i in range(NX)] for k in range(NZ)]
    # horizontal_diffusion_u_2 sets tau11avg to zero at the bottom/top W
    # boundaries and applies the fnm/fnp four-point average at interior W levels.
    tauavg = [[0.0] * NX for _ in range(NZ + 1)]
    for q in range(1, NZ):
        for face in range(NX):
            im1, i = (face - 1) % NX, face
            tauavg[q][face] = 0.25 * (tau[q][im1] + tau[q][i] +
                                      tau[q - 1][im1] + tau[q - 1][i])

    expected: dict[tuple[int, int, int], float] = {}
    # C++ raw helper's owned non-halo U cells are j=1..ny-2 and i=1..nx-1;
    # use mass level k=1 so both vertical interpolants are interior levels.
    k = 1
    for j in range(1, NY - 1):
        for face in range(1, NX):
            im1, i = (face - 1) % NX, face % NX
            tmpdz = 0.5 * (1.0 / rdzw[k][i] + 1.0 / rdzw[k][im1])
            zx_at_u = 0.5 * (zx[k][face % NX] + zx[k + 1][face % NX])
            bracket = ((tau[k][i] - tau[k][im1]) / DX -
                       zx_at_u * (tauavg[k + 1][face % NX] -
                                  tauavg[k][face % NX]) / tmpdz)
            # TileCase supplies positive rdnw=NZ=4; WRF's signed dnw is
            # therefore -1/rdnw=-1/4. Keep the full Fortran g*tmpdz/dnw scale.
            expected[(j, k, face)] = GRAVITY * tmpdz / (-1.0 / NZ) * bracket
    return expected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path,
                        help="built test_scalar_diffusion_contract executable")
    parser.add_argument("--expect-counterexample", action="store_true",
                        help="pass only when cached-geometry C++ output disagrees")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    sha = check_source(repo)
    cpp_path = repo / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp"
    cpp_sha = hashlib.sha256(cpp_path.read_bytes()).hexdigest()
    cpp_test_path = repo / "external/libtorch_wrf/sdirk3/tests/test_scalar_diffusion_contract.cpp"
    cpp_test_sha = hashlib.sha256(cpp_test_path.read_bytes()).hexdigest()
    python_test_sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    binary_sha = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo,
                              check=True, text=True, capture_output=True).stdout.strip()
    run = subprocess.run([str(args.binary), "--option2-momentum-stage-geometry"],
                         check=True, text=True, capture_output=True)
    actual: dict[tuple[int, int, int], float] = {}
    explicit_geometry: bool | None = None
    for line in run.stdout.splitlines():
        if line.startswith("GEOMETRY_INPUT "):
            explicit_geometry = line.endswith("stage_explicit=1")
        if line.startswith("U_RAW "):
            _, j, k, i, value = line.split()
            actual[(int(j), int(k), int(i))] = float(value)
    expected = fortran_oracle()
    absent = sorted(set(expected) - set(actual))
    if absent:
        print(f"FAIL missing owned C++ outputs: {absent[:3]}", file=sys.stderr)
        return 1
    errors = {key: abs(actual[key] - value) for key, value in expected.items()}
    max_error_key = max(errors, key=errors.get)
    max_error = errors[max_error_key]
    signal = max(abs(v) for v in expected.values())
    # U face 0 and terminal face nx represent periodic seam slots in the C++
    # raw helper output; this fixture expects its unowned seam aliases to stay 0.
    seam_error = max((abs(v) for (j, k, i), v in actual.items()
                      if 0 <= j < NY and 0 <= k < NZ and i in (0, NX)), default=0.0)
    tolerance = 2e-6 * max(1.0, signal)
    mismatch = max_error > tolerance
    print("ORACLE explicit numeric transcription of source equations; Fortran was not compiled")
    print(f"PROVENANCE revision={revision} module_diffusion_em.F.sha256={sha} "
          f"wrf_sdirk3_tile_unified_impl.cpp.sha256={cpp_sha}")
    print(f"TEST cpp.sha256={cpp_test_sha} python.sha256={python_test_sha} "
          f"binary={args.binary.resolve()} binary.sha256={binary_sha}")
    print(f"FIXTURE Nx={NX} periodic_x H=100*cos(2*pi*i/8)m dx={DX:g}m "
          f"dz={DZ:g}m dnw=-1/{NZ} U=[0,0,0,1] K={KH:g} rho={RHO:g} maps=1")
    print(f"ORACLE owned raw tendency max_abs={signal:.9g}")
    print(f"CPP vs oracle max_error={max_error:.9g} at {max_error_key} "
          f"cpp={actual[max_error_key]:.9g} oracle={expected[max_error_key]:.9g} "
          f"tolerance={tolerance:.3g}; seam_error={seam_error:.9g}")
    if args.expect_counterexample:
        ok = mismatch and signal > 1e-8 and seam_error == 0.0 and explicit_geometry is False
        print(("PASS" if ok else "FAIL") + " baseline stage-geometry counterexample")
        return 0 if ok else 1
    ok = not mismatch and seam_error == 0.0 and explicit_geometry is True
    print(("PASS" if ok else "FAIL") + " option-2 U-X Fortran geometry parity")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
