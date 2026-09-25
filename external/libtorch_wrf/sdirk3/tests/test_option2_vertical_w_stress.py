#!/usr/bin/env python3
"""Compare W vertical stress against extracted WRF Fortran and controls."""
from __future__ import annotations

import argparse
import hashlib
import math
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

NY, NZ, NX = 17, 16, 17
G, DN = 9.81, -0.5
RDZW_PHYSICAL, RDNW = 1.0 / 1024.0, 10.0


def extract_subroutine(source: str, name: str) -> str:
    start = source.index(f"SUBROUTINE {name}(")
    marker = f"END SUBROUTINE {name}"
    end = source.index(marker, start) + len(marker)
    return source[start:end]


def check_fortran_equations(source: str) -> None:
    normalized = " ".join(source.replace("&", " ").split()).lower()
    required = (
        "titau(i,k,j) = - rho(i,k,j) * xkx(i,k,j) * defor(i,k,j)",
        "tendency(i,k,j)=tendency(i,k,j)+ g*(titau3(i,k,j)-titau3(i,k-1,j))/dn(k)",
    )
    for equation in required:
        if equation not in normalized:
            raise RuntimeError(f"Fortran W stress equation changed; missing {equation!r}")


def check_step10_old_call(source: str) -> None:
    normalized = " ".join(source.split()).lower()
    expected_call = "compute_vertical_mixing_w(w, kv_mom, rdnw_tensor, rho)"
    if expected_call not in normalized:
        raise RuntimeError(f"Step10 legacy W call changed; missing {expected_call!r}")


def fortran_oracle(source: str, compiler: str, zero_k: bool = False) -> tuple[dict, str]:
    tau_routine = extract_subroutine(source, "cal_titau_11_22_33")
    w_routine = extract_subroutine(source, "vertical_diffusion_w_2")
    routines = tau_routine + "\n" + w_routine
    routine_sha = hashlib.sha256(routines.encode()).hexdigest()
    xkmh_scale = 0.0 if zero_k else 1.0
    driver = f"""module extracted_w_vertical_diffusion
  implicit none
  real, parameter :: g={G}
  integer, parameter :: P_m33=1
  type :: grid_config_rec_type
    logical :: open_xs=.false., open_xe=.false., open_ys=.false., open_ye=.false.
    logical :: specified=.false., nested=.false., periodic_x=.true., periodic_y=.false.
    integer :: sfs_opt=0, m_opt=0
  end type
contains
{routines}
end module extracted_w_vertical_diffusion

program w_oracle_driver
  use extracted_w_vertical_diffusion
  implicit none
  integer, parameter :: ny={NY}, nz={NZ}, nx={NX}
  integer, parameter :: ids=1, ide=nx+1, jds=1, jde=ny+1, kds=1, kde=nz+1
  integer, parameter :: ims=0, ime=nx+1, jms=0, jme=ny, kms=1, kme=nz+1
  integer, parameter :: its=1, ite=nx, jts=1, jte=ny, kts=1, kte=nz+1
  integer :: i,j,k
  type(grid_config_rec_type) :: cfg
  real :: tendency(ims:ime,kms:kme,jms:jme), defor33(ims:ime,kms:kme,jms:jme)
  real :: tke(ims:ime,kms:kme,jms:jme), div(ims:ime,kms:kme,jms:jme)
  real :: xkmh(ims:ime,kms:kme,jms:jme), rho(ims:ime,kms:kme,jms:jme)
  real :: rdz(ims:ime,kms:kme,jms:jme), nba_mij(ims:ime,kms:kme,jms:jme,1)
  real :: dn(kms:kme), fnm(kms:kme), fnp(kms:kme)
  tendency=0.; defor33=0.; tke=0.; div=0.; rdz={RDZW_PHYSICAL}
  nba_mij=0.; dn={DN}; fnm=0.5; fnp=0.5; rho=0.; xkmh=0.
  do j=jms,jme
    do k=kms,nz
      do i=ims,nx
        rho(i,k,j)=1.+real(k-1)/32.+real(i-1)/512.
        xkmh(i,k,j)={xkmh_scale}*(4.+real(k-1)/8.+real(i-1)/128.)
        defor33(i,k,j)=0.25+real(k-1)/16.+real(i-1)/256.
      enddo
    enddo
  enddo
  call vertical_diffusion_w_2(tendency,cfg,defor33,tke,nba_mij(ims,kms,jms,1),1, &
       div,xkmh,dn,rdz,fnm,fnp,rho,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
       its,ite,jts,jte,kts,kte)
  do j=jts,jte
    do k=kts+1,nz
      do i=its,ite
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'F_RAW',j-1,k-1,i-1,tendency(i,k,j)
      enddo
    enddo
  enddo
end program w_oracle_driver
"""
    with tempfile.TemporaryDirectory(prefix="sdirk3-option2-w-vertical-oracle-") as directory:
        work = Path(directory).resolve()
        src, exe = work / "oracle.f90", work / "oracle"
        src.write_text(driver)
        link_flags = []
        if sys.platform == "darwin":
            sdk = subprocess.run(["xcrun", "--show-sdk-path"], text=True,
                                 capture_output=True)
            if sdk.returncode == 0 and sdk.stdout.strip():
                link_flags.append(f"-Wl,-syslibroot,{sdk.stdout.strip()}")
        command = [*shlex.split(compiler), "-ffree-form", "-ffree-line-length-none",
                   *link_flags, str(src), "-o", str(exe)]
        built = subprocess.run(command, cwd=work, text=True, capture_output=True)
        if built.returncode:
            raise RuntimeError("extracted Fortran W compile failed:\n" + built.stderr)
        ran = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
    result = {}
    for line in ran.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == "F_RAW":
            _, j, k, i, value = fields
            result[(int(j), int(k), int(i))] = float(value)
    return result, routine_sha


def cpp_raw(binary: Path, mode: str) -> dict:
    ran = subprocess.run([str(binary), mode], check=True, text=True,
                         capture_output=True)
    result = {}
    for line in ran.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == "W_RAW":
            _, j, k, i, value = fields
            result[(int(j), int(k), int(i))] = float(value)
    return result


def max_error(actual: dict, expected: dict) -> float:
    missing = [key for key in expected if key not in actual]
    if missing:
        raise RuntimeError(f"C++ W helper omitted source-owned points: {missing[:3]}")
    return max(abs(actual[key] - expected[key]) for key in expected)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"))
    parser.add_argument("--cpp-source", type=Path,
                        help="implementation source corresponding to --binary")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    fortran_path = repo / "dyn_em/module_diffusion_em.F"
    fixture_path = repo / "external/libtorch_wrf/sdirk3/tests/test_scalar_diffusion_contract.cpp"
    cpp_path = (args.cpp_source or repo / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp").resolve()
    source = fortran_path.read_text()
    check_fortran_equations(source)
    expected, routine_sha = fortran_oracle(source, args.fortran_compiler)
    zero_expected, zero_sha = fortran_oracle(source, args.fortran_compiler, True)
    if routine_sha != zero_sha:
        raise RuntimeError("extracted Fortran W routines changed in K=0 control")
    cpp_source = cpp_path.read_text()
    check_step10_old_call(cpp_source)
    xkmv_old_actual = cpp_raw(args.binary, "--vertical-w-mixing-old-step10")
    xkmh_old_actual = cpp_raw(args.binary, "--vertical-w-mixing-old-xkmh")
    zero_actual = cpp_raw(args.binary, "--vertical-w-mixing-xkmh-zero-k")
    source_signal = max(abs(value) for value in expected.values())
    tolerance = 3.0e-6 * max(1.0, source_signal)
    projection = lambda values: sum(values[key] * (key[1] + 1) for key in expected)
    xkmv_error = max_error(xkmv_old_actual, expected)
    xkmh_error = max_error(xkmh_old_actual, expected)
    xkmv_signal = max(abs(xkmv_old_actual[key]) for key in expected)
    xkmh_signal = max(abs(xkmh_old_actual[key]) for key in expected)
    zero_error = max_error(zero_actual, zero_expected)
    zero_signal = max(abs(zero_expected[key]) for key in zero_expected)
    print(f"FIXTURE_REVISION {subprocess.run(['git','rev-parse','HEAD'],cwd=repo,check=True,text=True,capture_output=True).stdout.strip()}")
    print(f"CPP_REVISION {subprocess.run(['git','rev-parse','HEAD'],cwd=cpp_path.parents[3],check=True,text=True,capture_output=True).stdout.strip()}")
    print(f"FORTRAN_SOURCE_SHA256 {hashlib.sha256(fortran_path.read_bytes()).hexdigest()}")
    print(f"EXTRACTED_W_ROUTINES_SHA256 {routine_sha}")
    print(f"CPP_SOURCE_SHA256 {hashlib.sha256(cpp_path.read_bytes()).hexdigest()}")
    print(f"CPP_TEST_FIXTURE_SHA256 {hashlib.sha256(fixture_path.read_bytes()).hexdigest()}")
    print(f"CPP_BINARY_SHA256 {hashlib.sha256(args.binary.read_bytes()).hexdigest()}")
    print(f"INPUT dn_fortran={DN} rdn_cpp=2 rdnw_cpp={RDNW} rdzw_physical={RDZW_PHYSICAL:.17g} Fortran_xkmh=4+k/8+i/128 Step10_Kv_mom=xkmv=12+k/16+i/64 rho=1+k/32+i/512 defor33=1/4+k/16+i/256 W integrated with deltaW=D33/(2*rdzw)")
    print(f"OWNED_W_POINTS {len(expected)} W=[{NY},{NZ+1},{NX}] mass=[{NY},{NZ},{NX}]")
    print(f"FORTRAN_XKMH_RAW_RANGE min={min(expected.values()):.17g} max={max(expected.values()):.17g} max_abs={source_signal:.17g}")
    print(f"OLD_STEP10_XKMV max_error={xkmv_error:.17g} fortran_signal={source_signal:.17g} cpp_signal={xkmv_signal:.17g} tolerance={tolerance:.17g} projection_fortran={projection(expected):.17g} projection_cpp={projection(xkmv_old_actual):.17g}")
    print(f"OLD_CALL_XKMH_METRIC_SIGN_CONTROL max_error={xkmh_error:.17g} cpp_signal={xkmh_signal:.17g} projection_cpp={projection(xkmh_old_actual):.17g}")
    print(f"OLD_STEP10_K_ZERO max_error={zero_error:.17g} fortran_max_abs={zero_signal:.17g}")
    # Old Step10 recomputes D33 from W but uses physical rdzw for divergence;
    # Fortran uses xkmh and signed eta dn in vertical_diffusion_w_2.
    old_call_counterexample = source_signal > 100*tolerance and xkmh_error > 100*tolerance
    zero_ok = zero_signal == 0.0 and zero_error <= tolerance
    print(("PASS" if old_call_counterexample else "FAIL") + " old Step10 W call is a negative control")
    print(("PASS" if zero_ok else "FAIL") + " old Step10 W K=0 control")
    return 0 if old_call_counterexample and zero_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
