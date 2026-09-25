#!/usr/bin/env python3
"""Source-grounded option-2 U-X oracle with compiled WRF diffusion routines.

The stress and U-diffusion bodies are extracted verbatim from
module_diffusion_em.F at runtime. D11 remains an explicit transcription of the
source-checked deformation equation; source guards pin both the equations and
the production call chain.
"""
from __future__ import annotations

import argparse
import hashlib
import math
import os
import re
import shlex
import subprocess
import sys
import tempfile
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


def source_grounded_fields():
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

    return terrain, zx, rdzw, d11, tau, tauavg


def fortran_oracle() -> dict[tuple[int, int, int], float]:
    terrain, zx, rdzw, d11, tau, tauavg = source_grounded_fields()
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


def compiled_fortran_oracle(repo: Path, compiler: str, flags: list[str],
                            work: Path) -> tuple[dict[tuple[int, int, int], float], str]:
    """Compile exact U stress/diffusion routine bodies, with Python D11 inputs."""
    source = (repo / "dyn_em/module_diffusion_em.F").read_text()
    routine_names = ("cal_titau_11_22_33", "cal_titau_12_21",
                     "horizontal_diffusion_u_2")
    routines = []
    for name in routine_names:
        end = f"END SUBROUTINE {name}"
        begin_at = source.index(f"SUBROUTINE {name}")
        end_at = source.index(end, begin_at) + len(end)
        routines.append(source[begin_at:end_at])
    routine_hash = hashlib.sha256("\n".join(routines).encode()).hexdigest()
    terrain, zx, rdzw, d11, _, _ = source_grounded_fields()
    # Fortran's owned U faces 2..NX map to C++/Python faces 1..NX-1.
    # The first and last periodic aliases remain outside the owned comparison.
    src = f"""module extracted_wrf_diffusion
  implicit none
  real, parameter :: g=9.81
  integer, parameter :: P_m11=1, P_m12=2
  type :: grid_config_rec_type
    logical :: open_xs=.false., open_xe=.false., open_ys=.false., open_ye=.false.
    logical :: specified=.false., nested=.false., periodic_x=.true., periodic_y=.false.
    integer :: sfs_opt=0, m_opt=0
  end type
contains
{routines[0]}
{routines[1]}
{routines[2]}
end module extracted_wrf_diffusion

program oracle_driver
  use extracted_wrf_diffusion
  implicit none
  integer, parameter :: nx={NX}, ny={NY}, nz={NZ}
  integer, parameter :: ids=1, ide=nx+1, jds=1, jde=ny+1, kds=1, kde=nz+1
  integer, parameter :: ims=0, ime=nx+2, jms=0, jme=ny+2, kms=1, kme=nz+1
  integer, parameter :: its=2, ite=nx, jts=2, jte=ny-1, kts=1, kte=nz
  integer :: i,j,k
  type(grid_config_rec_type) :: cfg
  real :: rdx,rdy
  real :: tendency(ims:ime,kms:kme,jms:jme), defor11(ims:ime,kms:kme,jms:jme)
  real :: defor12(ims:ime,kms:kme,jms:jme), div(ims:ime,kms:kme,jms:jme)
  real :: tke(ims:ime,kms:kme,jms:jme), xkmh(ims:ime,kms:kme,jms:jme)
  real :: rho(ims:ime,kms:kme,jms:jme), zx(ims:ime,kms:kme,jms:jme)
  real :: zy(ims:ime,kms:kme,jms:jme), rdzw(ims:ime,kms:kme,jms:jme)
  real :: msfux(ims:ime,jms:jme), msfuy(ims:ime,jms:jme)
  real :: fnm(kms:kme), fnp(kms:kme), dnw(kms:kme)
  real :: nba_mij(ims:ime,kms:kme,jms:jme,2)
  tendency=0.; defor11=0.; defor12=0.; div=0.; tke=0.; xkmh=2.
  rho=1.; zx=0.; zy=0.; rdzw=1./{DZ:.17g}; nba_mij=0.
  msfux=1.; msfuy=1.; fnm=0.5; fnp=0.5; dnw=-0.25
  rdx=1./{DX:.17g}; rdy=rdx
  do j=jms,jme
    do k=kms,kme
      do i=ims,ime
        if (i>=1 .and. i<=nx+1) then
          zx(i,k,j)=real(({100.0:.17g})*cos(2.*acos(-1.)*real(modulo(i-1,nx))/real(nx)) - &
                           ({100.0:.17g})*cos(2.*acos(-1.)*real(modulo(i-2,nx))/real(nx))) * rdx
        endif
      enddo
    enddo
  enddo
  ! D11 is the separately evaluated, source-checked cal_deform_and_div equation.
"""
    for k in range(NZ):
        for i in range(NX):
            for j in range(NY):
                src += (f"  defor11({i + 1},{k + 1},{j + 1})="
                        f"{d11[k][i]:.17g}\n")
    src += f"""
  ! Exact source-extracted stress and horizontal diffusion routines follow.
  call horizontal_diffusion_u_2(tendency,cfg,defor11,defor12,div, &
       nba_mij(ims,kms,jms,1),2,tke,msfux,msfuy,xkmh,rdx,rdy,fnm,fnp, &
       dnw,zx,zy,rdzw,rho,ids,ide,jds,jde,kds,kde, &
       ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)
  do j=jts,jte
    do i=its-1,ite+1
      write(*,'(A,3(1X,I0),1X,ES25.16)') 'F_RAW',j-1,2-1,i-1,tendency(i,2,j)
    enddo
  enddo
end program oracle_driver
"""
    f90 = work / "extracted_wrf_diffusion_oracle.f90"
    exe = work / "fortran_oracle"
    work.mkdir(parents=True, exist_ok=True)
    f90.write_text(src)
    compiler_cmd = shlex.split(compiler)
    link_flags: list[str] = []
    if sys.platform == "darwin":
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], text=True,
                             capture_output=True)
        if sdk.returncode == 0 and sdk.stdout.strip():
            link_flags.append(f"-Wl,-syslibroot,{sdk.stdout.strip()}")
    compile_cmd = [*compiler_cmd, "-ffree-form", "-ffree-line-length-none", *flags,
                   *link_flags,
                   str(f90), "-o", str(exe)]
    built = subprocess.run(compile_cmd, text=True, capture_output=True)
    if built.returncode:
        raise RuntimeError("Fortran oracle compile failed:\n" + built.stderr)
    run = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
    values: dict[tuple[int, int, int], float] = {}
    for line in run.stdout.splitlines():
        if line.startswith("F_RAW "):
            _, j, k, i, value = line.split()
            values[(int(j), int(k), int(i))] = float(value)
    return values, routine_hash


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path,
                        help="built test_scalar_diffusion_contract executable")
    parser.add_argument("--expect-counterexample", action="store_true",
                        help="pass only when cached-geometry C++ output disagrees")
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"),
                        help="Fortran compiler used for exact extracted WRF routines")
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
    fortran_results: list[tuple[str, dict[tuple[int, int, int], float], str]] = []
    with tempfile.TemporaryDirectory(prefix="sdirk3-option2-fortran-") as temp_dir:
        temp = Path(temp_dir)
        for label, flags in (("fp32-O0", ["-O0"]), ("fp32-O2", ["-O2"]),
                             ("real64-O0", ["-O0", "-fdefault-real-8"]),
                             ("real64-O2", ["-O2", "-fdefault-real-8"])):
            result, extracted_sha = compiled_fortran_oracle(
                repo, args.fortran_compiler, flags, temp / label)
            fortran_results.append((label, result, extracted_sha))
    outputs = [("C++", actual), ("equation oracle", expected)]
    outputs.extend((name, result) for name, result, _ in fortran_results)
    for label, values in outputs:
        bad = next((key for key, value in values.items()
                    if not math.isfinite(value)), None)
        if bad is not None:
            print(f"FAIL non-finite {label} output at {bad}", file=sys.stderr)
            return 1
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
    tolerance = 2e-6 * max(1e-12, signal)
    mismatch = max_error > tolerance
    compiled_mismatches = []
    fp32_values: dict[tuple[int, int, int], float] | None = None
    for label, result, _ in fortran_results:
        missing = sorted(set(expected) - set(result))
        if missing:
            compiled_mismatches.append((label, "missing", missing[0], math.inf))
            continue
        err = max(abs(result[key] - expected[key]) for key in expected)
        limit = ((2e-6 if label.startswith("fp32") else 1e-11) *
                 max(1e-12, signal))
        print(f"FORTRAN {label} max_error={err:.9g} tolerance={limit:.3g} "
              f"routine_bodies.sha256={fortran_results[0][2]}")
        if err > limit:
            key = max(expected, key=lambda k: abs(result[k] - expected[k]))
            compiled_mismatches.append((label, key, result[key], err))
        if label == "fp32-O0":
            fp32_values = result
    cpp_fortran_error = math.inf
    fortran_seam_error = math.inf
    if fp32_values is not None:
        cpp_fortran_error = max(abs(actual[key] - fp32_values[key]) for key in expected)
        fortran_seam_error = max((abs(value) for (j, k, i), value in fp32_values.items()
                                  if 0 <= j < NY and 0 <= k < NZ and i in (0, NX)),
                                 default=0.0)
        print(f"CPP vs compiled Fortran fp32-O0 max_error={cpp_fortran_error:.9g}; "
              f"compiled Fortran seam_error={fortran_seam_error:.9g}")
        if cpp_fortran_error > tolerance or fortran_seam_error != 0.0:
            compiled_mismatches.append(("cpp-fp32-O0", "max_error",
                                        cpp_fortran_error, tolerance))
    print("ORACLE exact source-extracted cal_titau_11_22_33, cal_titau_12_21, "
          "and horizontal_diffusion_u_2; D11 is the existing source-checked "
          "Python equation transcription")
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
    ok = (not mismatch and seam_error == 0.0 and explicit_geometry is True and
          fortran_seam_error == 0.0 and not compiled_mismatches)
    if compiled_mismatches:
        print(f"FAIL compiled Fortran mismatches: {compiled_mismatches[:2]}")
    print(("PASS" if ok else "FAIL") + " option-2 U-X compiled Fortran geometry parity")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
