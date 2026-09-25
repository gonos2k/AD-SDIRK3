#!/usr/bin/env python3
"""Compare option-2 W stress sign against extracted WRF Fortran routines."""
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
KH, RHO, DN, RDZ, G = 2.0, 1.0, -1.0, 1.0, 1.0


def extract_routines(source: str) -> tuple[str, str]:
    names = ("cal_titau_13_31", "cal_titau_23_32", "horizontal_diffusion_w_2")
    bodies = []
    for name in names:
        start = source.index(f"SUBROUTINE {name}")
        end_marker = f"END SUBROUTINE {name}"
        end = source.index(end_marker, start) + len(end_marker)
        bodies.append(source[start:end])
    return "\n".join(bodies), "\n".join(bodies)


def check_source_equations(source: str) -> None:
    normalized = re.sub(r"\s+", " ", source.replace("&", " "))
    required = (
        "defor13(i,k,j) = mm(i,j) * ( rdx * ( hat(i,k,j) - hat(i-1,k,j) ) - tmp1(i,k,j) )",
        "defor23(i,k,j) = mm(i,j) * ( rdy * ( hat(i,k,j) - hat(i,k,j-1) ) - tmp1(i,k,j) )",
        "titau(i,k,j) = - xkxavg(i,k,j) * defor(i,k,j)",
        "tendency(i,k,j)=tendency(i,k,j) + g/(dn(k)*rdz(i,k,j)) *",
        "mrdx*(titau1(i+1,k,j)-titau1(i,k,j))",
        "mrdy*(titau2(i,k,j+1)-titau2(i,k,j))",
    )
    for equation in required:
        if equation not in normalized:
            raise RuntimeError(f"Fortran source contract changed; missing {equation!r}")


def fortran_oracle(source: str, compiler: str, kh: float) -> tuple[dict[tuple[int, int, int], float], str]:
    extracted, routine_material = extract_routines(source)
    routine_sha = hashlib.sha256(routine_material.encode()).hexdigest()
    driver = f"""module extracted_w_diffusion
  implicit none
  real, parameter :: g={G}, eps=1.e-12
  integer, parameter :: P_m13=1, P_m23=2
  type :: grid_config_rec_type
    logical :: open_xs=.false., open_xe=.false., open_ys=.false., open_ye=.false.
    logical :: specified=.false., nested=.false., periodic_x=.true., periodic_y=.false.
    logical :: polar=.false.
    integer :: sfs_opt=0, m_opt=0
  end type
contains
{extracted}
end module extracted_w_diffusion

program oracle_driver
  use extracted_w_diffusion
  implicit none
  integer, parameter :: nx={NX}, ny={NY}, nz={NZ}, nw=nz+1
  integer, parameter :: ids=1, ide=nx+2, jds=1, jde=ny+2, kds=1, kde=nw
  integer, parameter :: ims=0, ime=nx+2, jms=0, jme=ny+2, kms=1, kme=nw
  integer, parameter :: its=2, ite=nx+1, jts=2, jte=ny+1, kts=1, kte=nw
  integer :: i,j,k,ii
  real, parameter :: pi=3.14159265358979323846
  real :: rdx,rdy
  type(grid_config_rec_type) :: cfg
  real :: tendency(ims:ime,kms:kme,jms:jme),defor13(ims:ime,kms:kme,jms:jme)
  real :: defor23(ims:ime,kms:kme,jms:jme),div(ims:ime,kms:kme,jms:jme)
  real :: tke(ims:ime,kms:kme,jms:jme),xkmv(ims:ime,kms:kme,jms:jme)
  real :: rho(ims:ime,kms:kme,jms:jme),zx(ims:ime,kms:kme,jms:jme)
  real :: zy(ims:ime,kms:kme,jms:jme),rdz(ims:ime,kms:kme,jms:jme)
  real :: msftx(ims:ime,jms:jme),msfty(ims:ime,jms:jme)
  real :: fnm(kms:kme),fnp(kms:kme),dn(kms:kme)
  real :: nba_mij(ims:ime,kms:kme,jms:jme,2)
  tendency=0.; defor13=0.; defor23=0.; div=0.; tke=0.; xkmv={kh}; rho={RHO}
  zx=0.; zy=0.; rdz={RDZ}; msftx=1.; msfty=1.; nba_mij=0.
  fnm=0.5; fnp=0.5; dn={DN}; rdx=1.; rdy=1.
  ! Unit-map flat terrain; W is a cosine mode at one interior W level.
  ! Fill periodic mass halos and form the source D13 horizontal difference.
  do j=jms,jme
    do k=kms,kme
      do i=ims,ime
        ii=modulo(i-2,nx)
        if (i>=1 .and. i<=nx+1) then
          if (k==3) then
            defor13(i,k,j)=cos(2.*pi*real(modulo(i-2,nx))/real(nx)) - &
                           cos(2.*pi*real(modulo(i-3,nx))/real(nx))
          endif
        endif
      enddo
    enddo
  enddo
  ! Only mass-cell rows are consumed. The mode is constant in Y, so D23=0.
  call horizontal_diffusion_w_2(tendency,cfg,defor13,defor23,div, &
       nba_mij(ims,kms,jms,1),2,tke,msftx,msfty,xkmv,rdx,rdy,fnm,fnp, &
       dn,zx,zy,rdz,rho,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
       its,ite,jts,jte,kts,kte)
  do j=jts,jte
    do k=1,nw
      do i=its,ite
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'F_RAW',j-2,k-1,i-2,tendency(i,k,j)
      enddo
    enddo
  enddo
end program oracle_driver
"""
    with tempfile.TemporaryDirectory(prefix="sdirk3-option2-w-oracle-") as directory:
        work = Path(directory)
        src = work / "oracle.f90"
        exe = work / "oracle"
        src.write_text(driver)
        link_flags: list[str] = []
        if sys.platform == "darwin":
            sdk = subprocess.run(["xcrun", "--show-sdk-path"], text=True,
                                 capture_output=True)
            if sdk.returncode == 0 and sdk.stdout.strip():
                link_flags.append(f"-Wl,-syslibroot,{sdk.stdout.strip()}")
        command = [*shlex.split(compiler), "-ffree-form", "-ffree-line-length-none",
                   *link_flags, str(src), "-o", str(exe)]
        built = subprocess.run(command, text=True, capture_output=True)
        if built.returncode:
            raise RuntimeError("extracted Fortran compile failed:\n" + built.stderr)
        ran = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
    result: dict[tuple[int, int, int], float] = {}
    for line in ran.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == "F_RAW":
            _, j, k, i, value = fields
            result[(int(j), int(k), int(i))] = float(value)
    return result, routine_sha


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"))
    parser.add_argument("--expect-counterexample", action="store_true",
                        help="pass when the compiled C++ helper shows the known old sign defect")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    source_path = repo / "dyn_em/module_diffusion_em.F"
    source = source_path.read_text()
    check_source_equations(source)
    source_sha = hashlib.sha256(source_path.read_bytes()).hexdigest()
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo,
                              check=True, text=True, capture_output=True).stdout.strip()
    cpp_source_path = repo / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp"
    cpp_source_sha = hashlib.sha256(cpp_source_path.read_bytes()).hexdigest()
    binary_sha = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    def cpp_oracle(mode: str) -> dict[tuple[int, int, int], float]:
        run = subprocess.run([str(args.binary), mode], check=True, text=True,
                             capture_output=True)
        values: dict[tuple[int, int, int], float] = {}
        for line in run.stdout.splitlines():
            fields = line.split()
            if fields and fields[0] == "W_RAW":
                _, j, k, i, value = fields
                values[(int(j), int(k), int(i))] = float(value)
        return values

    actual = cpp_oracle("--option2-w-fourier-sign")
    zero_actual = cpp_oracle("--option2-w-fourier-zero-k")
    expected, routine_sha = fortran_oracle(source, args.fortran_compiler, KH)
    zero_expected, zero_routine_sha = fortran_oracle(source, args.fortran_compiler, 0.0)
    if not expected or not zero_expected:
        raise RuntimeError("extracted Fortran oracle emitted no values")
    if routine_sha != zero_routine_sha:
        raise RuntimeError("the extracted Fortran routine source changed between K controls")

    # Exclude the periodic seam cells: this isolates stress sign and amplitude
    # from the independent endpoint closure in the current C++ helper.
    keys = [(j, 2, i) for j in range(NY) for i in range(1, NX-1)]
    missing = [key for key in keys if key not in actual or key not in expected]
    if missing:
        print(f"FAIL missing owned W tendency values: {missing[:3]}")
        return 1
    signal = max(abs(expected[key]) for key in keys)
    tolerance = 2e-6 * max(1.0, signal)
    error = max(abs(actual[key] - expected[key]) for key in keys)
    expected_projection = sum(
        math.cos(2.0 * math.pi * i / NX) * expected[(j, 2, i)]
        for j in range(NY) for i in range(1, NX-1))
    actual_projection = sum(
        math.cos(2.0 * math.pi * i / NX) * actual[(j, 2, i)]
        for j in range(NY) for i in range(1, NX-1))
    max_key = max(keys, key=lambda key: abs(expected[key]))
    zero_keys = [(j,k,i) for j in range(NY) for k in range(NZ+1)
                 for i in range(NX)]
    zero_missing = [key for key in zero_keys
                    if key not in zero_actual or key not in zero_expected]
    if zero_missing:
        print(f"FAIL missing K=0 tendency values: {zero_missing[:3]}")
        return 1
    zero_signal = max(max(abs(zero_actual[key]), abs(zero_expected[key]))
                      for key in zero_keys)
    print(f"REVISION {revision}")
    print(f"FORTRAN_SOURCE_SHA256 {source_sha}")
    print(f"CPP_SOURCE_SHA256 {cpp_source_sha}")
    print(f"CPP_BINARY_SHA256 {binary_sha}")
    print(f"EXTRACTED_ROUTINES_SHA256 {routine_sha}")
    print(f"UNITS dx=1 dz=1 dn_fortran={DN} dn_cpp=+1 rdz={RDZ} rho={RHO} Kh={KH} g={G}; W=[ny,nz+1,nx]")
    print(f"FOURIER_MODE kx=2pi/{NX}, one interior W level, zero top/bottom")
    print(f"PROJECTION fortran={expected_projection:.17g} cpp={actual_projection:.17g}")
    print(f"MAX_ERROR {error:.17g} SIGNAL {signal:.17g} TOLERANCE {tolerance:.17g} AT {max_key}")
    print(f"K_ZERO_CONTROL max_abs_fortran={max(abs(zero_expected[key]) for key in zero_keys):.17g} "
          f"max_abs_cpp={max(abs(zero_actual[key]) for key in zero_keys):.17g}")
    if not all(math.isfinite(actual[key]) for key in keys):
        print("FAIL non-finite C++ raw W tendency")
        return 1
    zero_ok = zero_signal == 0.0
    print(("PASS" if zero_ok else "FAIL") + " option-2 W K=0 control")
    matches = (signal > 100.0 * tolerance and error <= tolerance and
               expected_projection < -tolerance and actual_projection < -tolerance)
    counterexample = (signal > 100.0 * tolerance and error > tolerance and
                      expected_projection < -tolerance and actual_projection > tolerance)
    result_ok = counterexample if args.expect_counterexample else matches
    label = "old sign counterexample" if args.expect_counterexample else "stress sign and Fourier amplitude"
    print(("PASS" if result_ok and zero_ok else "FAIL") + f" option-2 W raw {label}")
    return 0 if result_ok and zero_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
