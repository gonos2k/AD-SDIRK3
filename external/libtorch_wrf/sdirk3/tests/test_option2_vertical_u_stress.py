#!/usr/bin/env python3
"""Compare the raw option-2 U vertical stress tendency with WRF Fortran."""
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
G, RDNW, DNW = 9.81, 1.0, -1.0
KV = 1.0


def extract_subroutine(source: str, name: str) -> str:
    start = source.index(f"SUBROUTINE {name}(")
    end_marker = f"END SUBROUTINE {name}"
    end = source.index(end_marker, start) + len(end_marker)
    return source[start:end]


def check_source_equations(source: str) -> None:
    normalized = " ".join(source.replace("&", " ").split()).lower()
    required = (
        "titau(i,k,j) = - xkxavg(i,k,j) * defor(i,k,j)",
        "rdzu = -g/(dnw(k))",
        "tendency(i,k,j)=tendency(i,k,j)-rdzu*(titau3(i,k+1,j)-titau3(i,k,j))",
        "rdzv = - g / dnw(k)",
        "tendency(i,k,j)=tendency(i,k,j)-rdzv*(titau3(i,k+1,j)-titau3(i,k,j))",
    )
    for equation in required:
        if equation not in normalized:
            raise RuntimeError(f"Fortran equation changed; missing {equation!r}")


def fortran_oracle(source: str, compiler: str, kv: float) -> tuple[dict, str]:
    stress = extract_subroutine(source, "cal_titau_13_31")
    tendency = extract_subroutine(source, "vertical_diffusion_u_2")
    routines = stress + "\n" + tendency
    routine_sha = hashlib.sha256(routines.encode()).hexdigest()
    driver = f"""module extracted_u_diffusion
  implicit none
  real, parameter :: g={G}
  integer, parameter :: P_m13=1
  type :: grid_config_rec_type
    logical :: open_xs=.false., open_xe=.false., open_ys=.false., open_ye=.false.
    logical :: specified=.false., nested=.false., periodic_x=.true., periodic_y=.false.
    integer :: sfs_opt=0, m_opt=0
  end type
contains
{routines}
end module extracted_u_diffusion

program oracle_driver
  use extracted_u_diffusion
  implicit none
  integer, parameter :: ny={NY}, nz={NZ}, nx={NX}
  integer, parameter :: ids=1, ide=nx+1, jds=1, jde=ny+1, kds=1, kde=nz+1
  integer, parameter :: ims=0, ime=nx+1, jms=0, jme=ny, kms=1, kme=nz+1
  integer, parameter :: its=2, ite=nx, jts=1, jte=ny, kts=1, kte=nz+1
  integer :: i,j,k
  type(grid_config_rec_type) :: cfg
  real :: tendency(ims:ime,kms:kme,jms:jme), defor13(ims:ime,kms:kme,jms:jme)
  real :: xkmv(ims:ime,kms:kme,jms:jme),rho(ims:ime,kms:kme,jms:jme)
  real :: rdzw(ims:ime,kms:kme,jms:jme),mtau(ims:ime,kms:kme,jms:jme)
  real :: nba_mij(ims:ime,kms:kme,jms:jme,2),dnw(kms:kme),fnm(kms:kme),fnp(kms:kme)
  tendency=0.; defor13=0.; rdzw=1.; mtau=0.; nba_mij=0.
  dnw={DNW}; fnm=0.75; fnp=0.25; rho=0.; xkmv=0.
  do j=jms,jme
    do k=kms,nz
      do i=ims,nx
        rho(i,k,j)=1.+real(k-1)/32.+real(i-1)/512.
        xkmv(i,k,j)={kv}*(2.+real(k-1)/64.+real(i-1)/128.)
      enddo
    enddo
  enddo
  do j=jts,jte
    do k=kts+1,kte
      do i=its,ite
        defor13(i,k,j)=0.25+real(k-1)/128.+real(i-1)/256.
      enddo
    enddo
  enddo
  call vertical_diffusion_u_2(tendency,cfg,defor13,xkmv,nba_mij(ims,kms,jms,1),2, &
       dnw,rdzw,fnm,fnp,rho,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
       its,ite,jts,jte,kts,kte)
  do j=jts,jte
    do k=kts,kte-1
      do i=its,ite
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'F_RAW',j-1,k-1,i-1,tendency(i,k,j)
      enddo
    enddo
  enddo
end program oracle_driver
"""
    with tempfile.TemporaryDirectory(prefix="sdirk3-option2-u-oracle-") as directory:
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
            raise RuntimeError("extracted Fortran compile failed:\n" + built.stderr)
        ran = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
    result = {}
    for line in ran.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == "F_RAW":
            _, j, k, i, value = fields
            result[(int(j), int(k), int(i))] = float(value)
    return result, routine_sha


def fortran_oracle_v(source: str, compiler: str, kv: float) -> tuple[dict, str]:
    stress = extract_subroutine(source, "cal_titau_23_32")
    tendency = extract_subroutine(source, "vertical_diffusion_v_2")
    routines = stress + "\n" + tendency
    routine_sha = hashlib.sha256(routines.encode()).hexdigest()
    driver = f"""module extracted_v_diffusion
  implicit none
  real, parameter :: g={G}
  integer, parameter :: P_m23=2
  type :: grid_config_rec_type
    logical :: open_xs=.false., open_xe=.false., open_ys=.false., open_ye=.false.
    logical :: specified=.false., nested=.false., periodic_x=.true., periodic_y=.false.
    integer :: sfs_opt=0, m_opt=0
  end type
contains
{routines}
end module extracted_v_diffusion

program oracle_driver
  use extracted_v_diffusion
  implicit none
  integer, parameter :: ny={NY}, nz={NZ}, nx={NX}
  integer, parameter :: ids=1, ide=nx+1, jds=1, jde=ny+1, kds=1, kde=nz+1
  integer, parameter :: ims=0, ime=nx+1, jms=0, jme=ny, kms=1, kme=nz+1
  integer, parameter :: its=1, ite=nx, jts=2, jte=ny, kts=1, kte=nz+1
  integer :: i,j,k
  type(grid_config_rec_type) :: cfg
  real :: tendency(ims:ime,kms:kme,jms:jme), defor23(ims:ime,kms:kme,jms:jme)
  real :: xkmv(ims:ime,kms:kme,jms:jme),rho(ims:ime,kms:kme,jms:jme)
  real :: rdzw(ims:ime,kms:kme,jms:jme),mtau(ims:ime,kms:kme,jms:jme)
  real :: nba_mij(ims:ime,kms:kme,jms:jme,2),dnw(kms:kme),fnm(kms:kme),fnp(kms:kme)
  tendency=0.; defor23=0.; rdzw=1.; mtau=0.; nba_mij=0.
  dnw={DNW}; fnm=0.75; fnp=0.25; rho=0.; xkmv=0.
  do j=jms,jme
    do k=kms,nz
      do i=ims,nx
        rho(i,k,j)=1.+real(k-1)/32.+real(i-1)/512.
        xkmv(i,k,j)={kv}*(2.+real(k-1)/64.+real(i-1)/128.)
      enddo
    enddo
  enddo
  do j=jts,jte
    do k=kts+1,kte
      do i=its,ite
        defor23(i,k,j)=0.25+real(k-1)/128.+real(j-1)/256.+real(i-1)/512.
      enddo
    enddo
  enddo
  call vertical_diffusion_v_2(tendency,cfg,defor23,xkmv,nba_mij(ims,kms,jms,1),2, &
       dnw,rdzw,fnm,fnp,rho,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
       its,ite,jts,jte,kts,kte)
  do j=jts,jte
    do k=kts,kte-1
      do i=its,ite
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'F_RAW',j-1,k-1,i-1,tendency(i,k,j)
      enddo
    enddo
  enddo
end program oracle_driver
"""
    with tempfile.TemporaryDirectory(prefix="sdirk3-option2-v-oracle-") as directory:
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
            raise RuntimeError("extracted Fortran V compile failed:\n" + built.stderr)
        ran = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
    result = {}
    for line in ran.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == "F_RAW":
            _, j, k, i, value = fields
            result[(int(j), int(k), int(i))] = float(value)
    return result, routine_sha


def cpp_raw(binary: Path, zero_k: bool, axis: str = "u") -> dict:
    mode = f"--vertical-{axis}-stress-zero-k" if zero_k else f"--vertical-{axis}-stress-raw"
    ran = subprocess.run([str(binary), mode], text=True, capture_output=True)
    if ran.returncode:
        detail = ran.stderr.strip().splitlines()
        diagnostic = next((line for line in detail if "size of tensor" in line),
                          detail[0] if detail else "no diagnostic")
        raise RuntimeError(
            f"C++ helper {mode} failed ({ran.returncode}): "
            + diagnostic)
    result = {}
    for line in ran.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == f"{axis.upper()}_RAW":
            _, j, k, i, value = fields
            result[(int(j), int(k), int(i))] = float(value)
    return result


def compare(actual: dict, expected: dict, label: str) -> tuple[bool, float, float]:
    keys = sorted(expected)
    missing = [key for key in keys if key not in actual]
    if missing:
        print(f"FAIL {label}: missing C++ output keys {missing[:3]}")
        return False, math.inf, 0.0
    signal = max(abs(expected[key]) for key in keys)
    tolerance = 3.0e-6 * max(1.0, signal)
    error = max(abs(actual[key] - expected[key]) for key in keys)
    print(f"{label} max_error={error:.17g} signal={signal:.17g} tolerance={tolerance:.17g}")
    return error <= tolerance and all(math.isfinite(actual[key]) for key in keys), error, signal


def report_vertical_error_breakdown(actual: dict, expected: dict, label: str) -> None:
    errors = {key: abs(actual[key] - expected[key]) for key in expected if key in actual}
    for region, predicate in (
        ("bottom", lambda k: k == 0),
        ("interior", lambda k: 0 < k < NZ - 1),
        ("top", lambda k: k == NZ - 1),
    ):
        keys = [key for key in errors if predicate(key[1])]
        error = max((errors[key] for key in keys), default=0.0)
        expected_signal = max((abs(expected[key]) for key in keys), default=0.0)
        actual_signal = max((abs(actual[key]) for key in keys), default=0.0)
        print(f"{label}_{region.upper()} max_error={error:.17g} fortran_max_abs={expected_signal:.17g} cpp_max_abs={actual_signal:.17g} count={len(keys)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"))
    parser.add_argument("--cpp-source", type=Path,
                        help="implementation source corresponding to --binary")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    fortran_path = repo / "dyn_em/module_diffusion_em.F"
    cpp_path = args.cpp_source or repo / "external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp"
    source = fortran_path.read_text()
    check_source_equations(source)
    source_sha = hashlib.sha256(fortran_path.read_bytes()).hexdigest()
    cpp_path = cpp_path.resolve()
    cpp_sha = hashlib.sha256(cpp_path.read_bytes()).hexdigest()
    binary_sha = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    print(f"REVISION {subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=repo, check=True, text=True, capture_output=True).stdout.strip()}")
    cpp_repo = cpp_path.parents[3]
    cpp_revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=cpp_repo,
                                  check=True, text=True, capture_output=True).stdout.strip()
    print(f"CPP_REVISION {cpp_revision}")
    print(f"FORTRAN_SOURCE_SHA256 {source_sha}")
    print(f"CPP_SOURCE_SHA256 {cpp_sha}")
    print(f"CPP_BINARY_SHA256 {binary_sha}")
    u_routines = extract_subroutine(source, 'cal_titau_13_31') + "\n" + extract_subroutine(source, 'vertical_diffusion_u_2')
    v_routines = extract_subroutine(source, 'cal_titau_23_32') + "\n" + extract_subroutine(source, 'vertical_diffusion_v_2')
    u_routine_sha = hashlib.sha256(u_routines.encode()).hexdigest()
    v_routine_sha = hashlib.sha256(v_routines.encode()).hexdigest()
    print(f"EXTRACTED_U_ROUTINES_SHA256 {u_routine_sha}")
    print(f"EXTRACTED_V_ROUTINES_SHA256 {v_routine_sha}")
    print(f"BOUNDARY_CONTRACT specified=false open_xs/open_xe/open_ys/open_ye=false nested=false periodic_x=true periodic_y=false")
    print("VERTICAL_INTERPOLATION C++ solver setter and Fortran driver both receive fnm=3/4 fnp=1/4 (sum=1 exactly)")
    print(f"VERTICAL_BOUNDARY_INDEX source kte=kde={NZ+1}, ktf=kde-1={NZ}; compare C++ mass k=0..{NZ-1} to Fortran k=1..{NZ}")
    print("INPUT_PATTERN_U rho=1+k/32+i/512 Kv=2+k/64+i/128 defor13=1/4+k/128+i/256; dyadic FP32")
    print("INPUT_PATTERN_V rho=1+k/32+i/512 Kv=2+k/64+i/128 defor23=1/4+k/128+j/256+i/512; dyadic FP32")
    print(f"RAW_CONTRACT tendency(i,k,j) = -(-g/dnw) * delta(titau); dnw={DNW}, rdnw={RDNW}, g={G}; output is an unscaled WRF tendency value (no dt); U=[{NY},{NZ},{NX+1}], rho/Kv=[{NY},{NZ},{NX}]")
    print(f"V_RAW_CONTRACT tendency(i,k,j) = -(-g/dnw) * delta(titau); dnw={DNW}, rdnw={RDNW}, g={G}; V=[{NY+1},{NZ},{NX}], rho/Kv=[{NY},{NZ},{NX}]")

    expected, routine_sha = fortran_oracle(source, args.fortran_compiler, KV)
    if not expected:
        raise RuntimeError("extracted Fortran U oracle emitted no interior values")
    print(f"FORTRAN_RAW_RANGE min={min(expected.values()):.17g} max={max(expected.values()):.17g} count={len(expected)}")
    zero_expected, zero_sha = fortran_oracle(source, args.fortran_compiler, 0.0)
    if routine_sha != zero_sha:
        raise RuntimeError("Fortran U oracle routines changed between K controls")
    zero_signal = max((abs(value) for value in zero_expected.values()), default=0.0)
    print(f"FORTRAN_K_ZERO max_abs={zero_signal:.17g} count={len(zero_expected)}")
    passed = False
    zero_pass = False
    actual = {}
    try:
        actual = cpp_raw(args.binary, False, "u")
        passed, _, _ = compare(actual, expected, "U vertical stress source parity")
        report_vertical_error_breakdown(actual, expected, "U")
        keys = sorted(expected)
        proj = sum(expected[key] * (key[1] + 1) for key in keys)
        got_proj = sum(actual[key] * (key[1] + 1) for key in keys)
        print(f"U_VERTICAL_SHEAR_PROJECTION fortran={proj:.17g} cpp={got_proj:.17g}")
    except RuntimeError as exc:
        print(f"FAIL raw U source parity: {exc}")
    try:
        zero_actual = cpp_raw(args.binary, True, "u")
        zero_pass, _, zero_signal = compare(zero_actual, zero_expected, "U K=0 control")
        zero_pass = zero_pass and zero_signal == 0.0
    except RuntimeError as exc:
        print(f"FAIL raw U K=0 control: {exc}")
    print(("PASS" if passed else "FAIL") + " raw U source parity")
    print(("PASS" if zero_pass else "FAIL") + " raw U K=0 control")

    v_expected, v_routine_sha = fortran_oracle_v(source, args.fortran_compiler, KV)
    if not v_expected:
        raise RuntimeError("extracted Fortran V oracle emitted no interior values")
    print(f"V_FORTRAN_RAW_RANGE min={min(v_expected.values()):.17g} max={max(v_expected.values()):.17g} count={len(v_expected)}")
    v_zero_expected, v_zero_sha = fortran_oracle_v(source, args.fortran_compiler, 0.0)
    if v_routine_sha != v_zero_sha:
        raise RuntimeError("Fortran V oracle routines changed between K controls")
    v_zero_signal = max((abs(value) for value in v_zero_expected.values()), default=0.0)
    print(f"V_FORTRAN_K_ZERO max_abs={v_zero_signal:.17g} count={len(v_zero_expected)}")
    v_pass = False
    v_zero_pass = False
    try:
        v_actual = cpp_raw(args.binary, False, "v")
        v_pass, _, _ = compare(v_actual, v_expected, "V vertical stress source parity")
        report_vertical_error_breakdown(v_actual, v_expected, "V")
        v_keys = sorted(v_expected)
        v_proj = sum(v_expected[key] * (key[1] + 1) for key in v_keys)
        v_got_proj = sum(v_actual[key] * (key[1] + 1) for key in v_keys)
        print(f"V_VERTICAL_SHEAR_PROJECTION fortran={v_proj:.17g} cpp={v_got_proj:.17g}")
    except RuntimeError as exc:
        print(f"FAIL raw V source parity: {exc}")
    try:
        v_zero_actual = cpp_raw(args.binary, True, "v")
        v_zero_pass, _, v_zero_actual_signal = compare(
            v_zero_actual, v_zero_expected, "V K=0 control")
        v_zero_pass = v_zero_pass and v_zero_actual_signal == 0.0 and v_zero_signal == 0.0
    except RuntimeError as exc:
        print(f"FAIL raw V K=0 control: {exc}")
    print(("PASS" if v_pass else "FAIL") + " raw V source parity")
    print(("PASS" if v_zero_pass else "FAIL") + " raw V K=0 control")
    return 0 if passed and zero_pass and v_pass and v_zero_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
