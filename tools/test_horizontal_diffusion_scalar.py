#!/usr/bin/env python3
"""Small source-extracted scalar diffusion contract regression.

The test calls the production metric producer, the production 3-D boundary
routine, and the production scalar horizontal-diffusion routine in one fixed
state. It checks all-cell cancellation and a flat Fourier control, plus
interior-layer mixed terrain/Fourier forcing of the flux-divergence metrics.
"""
from __future__ import annotations

import os
import math
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DIFFUSION = ROOT / "dyn_em" / "module_diffusion_em.F"
BOUNDARY = ROOT / "share" / "module_bc.F"


def extract(source: Path, name: str) -> str:
    text = source.read_text(encoding="ascii")
    match = re.search(
        rf"(?ms)^\s*SUBROUTINE\s+{name}\b.*?^\s*END\s+SUBROUTINE\s+{name}\s*$",
        text,
    )
    if match is None:
        raise RuntimeError(f"could not extract {name} from {source}")
    return match.group(0)


def parity_rows(output: str, prefix: str) -> dict[tuple[int, int, int], float]:
    rows = {}
    for line in output.splitlines():
        if not line.startswith(prefix + " "):
            continue
        tag, j, k, i, value = line.split()
        key = (int(j), int(k), int(i))
        if key in rows:
            raise RuntimeError(f"duplicate {tag} cell {key}")
        rows[key] = float(value)
    return rows


def compare_cpp_fortran(precision: str, fortran_output: str, cpp_binary: Path) -> None:
    mode = "fp32" if precision == "default REAL" else "fp64"
    cpp = subprocess.run(
        [str(cpp_binary), "--flat-x-parity", mode],
        text=True, capture_output=True, check=True,
    )
    left = parity_rows(fortran_output, "F_PARITY")
    right = parity_rows(cpp.stdout, "C_PARITY")
    expected_keys = {(j, k, i) for j in range(1, 7)
                     for k in range(1, 5) for i in range(1, 9)}
    if set(left) != expected_keys or set(right) != expected_keys:
        raise RuntimeError(
            f"{mode} cell inventory mismatch: Fortran={len(left)} C++={len(right)}"
        )
    if not all(math.isfinite(value) for value in (*left.values(), *right.values())):
        raise RuntimeError(f"{mode} nonfinite scalar diffusion tendency")
    signal = max(abs(value) for value in left.values())
    epsilon = 2.0 ** (-23 if mode == "fp32" else -52)
    budget = 64.0 * epsilon * max(1.0, signal)
    worst = max(expected_keys, key=lambda key: abs(left[key] - right[key]))
    error = abs(left[worst] - right[worst])
    seam_error = max(abs(left[key] - right[key]) for key in expected_keys
                     if key[2] in (1, 8))
    print(f"flat X Fortran/C++ {mode}: cells=192 signal={signal:.9g} "
          f"max_error={error:.9g} seam_error={seam_error:.9g} "
          f"budget={budget:.9g} worst={worst}")
    if not (signal > 100.0 * budget and error <= budget):
        raise RuntimeError(f"{mode} scalar diffusion differs from source Fortran")


DRIVER = r"""
PROGRAM test_horizontal_diffusion_scalar
  USE scalar_mod
  USE, INTRINSIC :: ieee_arithmetic, ONLY: ieee_is_finite
  IMPLICIT NONE
  INTEGER, PARAMETER :: NX0=8, NY0=6
  REAL, PARAMETER :: LAYER_DEPTH=20.
  REAL, PARAMETER :: PI0=3.1415926535897932384626433832795
  CALL run_case(1, 'flat_cancel')
  CALL run_case(2, 'flat_fourier')
  CALL run_case(3, 'terrain_cancel')
  CALL run_case(4, 'terrain_mixed_interior')
  CALL run_case(5, 'flat_periodic_x_parity')
  WRITE(*,'(A)') 'horizontal scalar producer/BC/consumer: PASS'
CONTAINS
  SUBROUTINE run_case(case_id, label)
    INTEGER, INTENT(IN) :: case_id
    CHARACTER(LEN=*), INTENT(IN) :: label
    INTEGER, PARAMETER :: nx=8, ny=6, nw=5
    INTEGER, PARAMETER :: ims=-4, ime=nx+5, jms=-4, jme=ny+5
    INTEGER, PARAMETER :: kms=0, kme=nw+1
    INTEGER, PARAMETER :: ids=1, ide=nx+1, jds=1, jde=ny+1
    INTEGER, PARAMETER :: kds=1, kde=nw
    INTEGER, PARAMETER :: ips=ids, ipe=ide, jps=jds, jpe=jde
    INTEGER, PARAMETER :: its=ids, ite=ide, jts=jds, jte=jde
    INTEGER, PARAMETER :: kts=1, kte=nw
    REAL :: ph(ims:ime,kms:kme,jms:jme), phb(ims:ime,kms:kme,jms:jme)
    REAL :: z(ims:ime,kms:kme,jms:jme), rdz(ims:ime,kms:kme,jms:jme)
    REAL :: rdzw(ims:ime,kms:kme,jms:jme), zx(ims:ime,kms:kme,jms:jme)
    REAL :: zy(ims:ime,kms:kme,jms:jme), rho(ims:ime,kms:kme,jms:jme)
    REAL :: var(ims:ime,kms:kme,jms:jme), tend(ims:ime,kms:kme,jms:jme)
    REAL :: xkhh(ims:ime,kms:kme,jms:jme)
    REAL :: msftx(ims:ime,jms:jme), msfty(ims:ime,jms:jme)
    REAL :: msfux(ims:ime,jms:jme), msfuy(ims:ime,jms:jme)
    REAL :: msfvx(ims:ime,jms:jme), msfvy(ims:ime,jms:jme)
    REAL :: fnm(kms:kme), fnp(kms:kme), dn(kms:kme), dnw(kms:kme)
    TYPE(grid_config_rec_type) :: cfg
    INTEGER :: i,j,k,ip,jp
    REAL :: rdx,rdy,cf1,cf2,cf3,aa,zz,expected,err,max_expected
    REAL :: max_slope,amp,kh,b,tol,derivative_scale
    CHARACTER(LEN=1) :: parity_dump
    INTEGER :: parity_status
    REAL, PARAMETER :: pi=3.1415926535897932384626433832795

    rdx=.1; rdy=.13; cf1=2.; cf2=-1.5; cf3=.5
    ! The C++ helper ABI takes float rdx/rdy even for REAL64 tensors.
    IF (case_id == 5) rdx=REAL(REAL(.1,KIND=4),KIND=KIND(rdx))
    fnm=.5; fnp=.5; dn=-.25; dnw=-.25
    msftx=1.; msfty=1.; msfux=1.; msfuy=1.; msfvx=1.; msfvy=1.
    rho=1.; xkhh=2.; ph=0.; phb=0.; z=0.; rdz=0.; rdzw=0.; zx=0.; zy=0.
    kh=2.; b=.1
    IF (case_id == 5) b=0.
    cfg%periodic_x=.TRUE.; cfg%periodic_y=.TRUE.
    cfg%specified=.FALSE.; cfg%nested=.FALSE.; cfg%polar=.FALSE.
    cfg%open_xs=.FALSE.; cfg%open_xe=.FALSE.; cfg%open_ys=.FALSE.; cfg%open_ye=.FALSE.
    cfg%symmetric_xs=.FALSE.; cfg%symmetric_xe=.FALSE.
    cfg%symmetric_ys=.FALSE.; cfg%symmetric_ye=.FALSE.

    DO j=jms,jme
      jp=MODULO(j-1,ny)+1
      DO i=ims,ime
        ip=MODULO(i-1,nx)+1
        DO k=kms,kme
          ph(i,k,j)=g*zw(ip,jp,k,case_id)
          phb(i,k,j)=0.
        END DO
      END DO
    END DO

    CALL compute_diff_metrics(cfg,ph,phb,z,rdz,rdzw,zx,zy,rdx,rdy, &
         ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
         its,ite,jts,jte,kts,kte)
    CALL set_physical_bc3d(rdzw,'w',cfg,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,ips,ipe,jps,jpe,kts,kte,its,ite,jts,jte,kts,kte)
    CALL set_physical_bc3d(rdz ,'w',cfg,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,ips,ipe,jps,jpe,kts,kte,its,ite,jts,jte,kts,kte)
    CALL set_physical_bc3d(z   ,'w',cfg,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,ips,ipe,jps,jpe,kts,kte,its,ite,jts,jte,kts,kte)
    CALL set_physical_bc3d(zx  ,'e',cfg,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,ips,ipe,jps,jpe,kts,kte,its,ite,jts,jte,kts,kte)
    CALL set_physical_bc3d(zy  ,'f',cfg,ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,ips,ipe,jps,jpe,kts,kte,its,ite,jts,jte,kts,kte)

    DO j=jms,jme; DO i=ims,ime; DO k=kms,kme
      ! Analytic mass-point height; do not derive the test input from the
      ! metric producer's output, which could hide a shared geometry error.
      zz=100.+LAYER_DEPTH*(REAL(k)+.5)
      IF (case_id == 3 .OR. case_id == 4) zz=zz+terrain(i,j)
      IF (case_id == 2) THEN
        aa=1.+.03*SIN(2.*pi*REAL(MODULO(i-1,nx))/REAL(nx))+ &
             .02*COS(2.*pi*REAL(MODULO(j-1,ny))/REAL(ny))
      ELSE IF (case_id == 5) THEN
        aa=1.+.03*SIN(2.*pi*REAL(MODULO(i-1,nx))/REAL(nx))
      ELSE
        aa=1.
      END IF
      var(i,k,j)=aa+b*zz
      IF (case_id == 4) var(i,k,j)=var(i,k,j)+zz*( &
           .03*SIN(2.*pi*REAL(MODULO(i-1,nx))/REAL(nx))+ &
           .02*COS(2.*pi*REAL(MODULO(j-1,ny))/REAL(ny)))
    END DO; END DO; END DO
    tend=0.
    CALL horizontal_diffusion_s(tend,cfg,var,msftx,msfty,msfux,msfuy,msfvx,msfvy,xkhh,rdx,rdy, &
         fnm,fnp,cf1,cf2,cf3,zx,zy,rdz,rdzw,dnw,dn,rho,.FALSE., &
         ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)

    IF (case_id == 5) THEN
      parity_dump=' '
      CALL GET_ENVIRONMENT_VARIABLE('SDIRK3_SCALAR_PARITY_DUMP',parity_dump,STATUS=parity_status)
      IF (parity_status == 0 .AND. parity_dump == '1') THEN
        DO j=1,ny; DO k=1,kte-1; DO i=1,nx
          WRITE(*,'(A,3(1X,I0),1X,ES25.16E3)') 'F_PARITY',j,k,i,tend(i,k,j)
        END DO; END DO; END DO
      END IF
    END IF

    err=0.; max_expected=0.
    DO j=1,ny; DO k=1,kte-1; DO i=1,nx
      IF (.NOT. ieee_is_finite(tend(i,k,j))) ERROR STOP 'nonfinite diffusion tendency'
      ! The mixed oracle uses the interior vertical stencil. The existing
      ! three cases still check every physical layer; all outputs stay finite.
      IF (case_id == 4 .AND. (k == 1 .OR. k == kte-1)) CYCLE
      expected=0.
      IF (case_id == 2) expected=fourier_rhs(i,j,rdx,rdy,dnw(k),kh)
      IF (case_id == 4) expected=mixed_rhs(i,j,k,rdx,rdy,dnw(k),kh)
      IF (case_id == 5) expected=-g*kh*LAYER_DEPTH/dnw(k)* &
           (-4.*rdx**2*SIN(PI0/NX0)**2*.03*SIN(2.*PI0*REAL(i-1)/NX0))
      err=MAX(err,ABS(tend(i,k,j)-expected)); max_expected=MAX(max_expected,ABS(expected))
    END DO; END DO; END DO

    max_slope=0.
    IF (case_id == 3 .OR. case_id == 4) THEN
      DO j=1,ny; DO i=1,nx
        max_slope=MAX(max_slope,ABS(rdx*(terrain(i,j)-terrain(i-1,j))))
        max_slope=MAX(max_slope,ABS(rdy*(terrain(i,j)-terrain(i,j-1))))
      END DO; END DO
    END IF
    amp=g*LAYER_DEPTH/ABS(dnw(1))
    derivative_scale=ABS(rdx)+ABS(rdy)+2.*max_slope/LAYER_DEPTH
    ! Fixed engineering roundoff budget, not a rigorous error bound:
    ! input magnitude * two derivative scales * diffusivity * tendency units.
    ! The factor 64 allows accumulation through interpolation and flux loops;
    ! it is independent of the observed mismatch and scales with precision.
    tol=64.*EPSILON(1.)*MAXVAL(ABS(var(1:nx,1:kte-1,1:ny)))*kh*amp*derivative_scale**2
    IF (.NOT. ieee_is_finite(tol)) ERROR STOP 'invalid error budget'
    IF ((case_id == 2 .OR. case_id == 4 .OR. case_id == 5) .AND. max_expected <= tol) ERROR STOP 'unresolved positive control'
    IF (err > tol) THEN
      WRITE(*,'(A,1X,A,2(1X,ES13.5))') 'scalar regression FAIL',TRIM(label),err,tol
      ERROR STOP 1
    END IF
    WRITE(*,'(A,1X,A,3(1X,ES13.5))') 'case PASS error/budget/signal',TRIM(label),err,tol,max_expected
  END SUBROUTINE run_case

  REAL FUNCTION terrain(i,j) RESULT(q)
    INTEGER, INTENT(IN) :: i,j
    q=3.*SIN(2.*PI0*REAL(MODULO(i-1,NX0))/REAL(NX0))+2.*COS(2.*PI0*REAL(MODULO(j-1,NY0))/REAL(NY0))
  END FUNCTION terrain
  REAL FUNCTION zw(i,j,k,case_id) RESULT(q)
    INTEGER, INTENT(IN) :: i,j,k,case_id
    q=100.+LAYER_DEPTH*REAL(k)
    IF (case_id == 3 .OR. case_id == 4) q=q+terrain(i,j)
  END FUNCTION zw
  REAL FUNCTION fourier_rhs(i,j,rdx,rdy,deta,kh) RESULT(q)
    INTEGER, INTENT(IN) :: i,j
    REAL, INTENT(IN) :: rdx,rdy,deta,kh
    REAL :: laplacian
    ! Fourier eigenvalue of the periodic second difference, independent of
    ! the production flux loops. Flat terrain makes vertical flux terms zero.
    laplacian=-4.*rdx**2*SIN(PI0/NX0)**2*.03*SIN(2.*PI0*REAL(i-1)/NX0) &
              -4.*rdy**2*SIN(PI0/NY0)**2*.02*COS(2.*PI0*REAL(j-1)/NY0)
    q=-g*kh*LAYER_DEPTH/deta*laplacian
  END FUNCTION fourier_rhs
  REAL FUNCTION mixed_rhs(i,j,k,rdx,rdy,deta,kh) RESULT(q)
    INTEGER, INTENT(IN) :: i,j,k
    REAL, INTENT(IN) :: rdx,rdy,deta,kh
    REAL :: sx,cy,lx,ly,lap_f,product_correction,height
    sx=SIN(2.*PI0*REAL(i-1)/NX0); cy=COS(2.*PI0*REAL(j-1)/NY0)
    lx=-4.*rdx**2*SIN(PI0/NX0)**2; ly=-4.*rdy**2*SIN(PI0/NY0)**2
    lap_f=lx*.03*sx+ly*.02*cy
    height=100.+LAYER_DEPTH*(REAL(k)+.5)+terrain(i,j)
    ! Exact centered-difference product identity, applied in each direction:
    ! D(z_face D F) - D0(h) D0(F) = z L(F) + dx^2/4 L(h) L(F).
    ! A continuous z*L(F) oracle alone would omit this discrete correction.
    product_correction=.25*((lx*3.*sx)*(lx*.03*sx)/rdx**2 &
                          +(ly*2.*cy)*(ly*.02*cy)/rdy**2)
    q=-g*kh*LAYER_DEPTH/deta*(height*lap_f+product_correction)
  END FUNCTION mixed_rhs
END PROGRAM test_horizontal_diffusion_scalar
"""


def main() -> int:
    if len(sys.argv) > 2:
        print("usage: test_horizontal_diffusion_scalar.py [compiled_cpp_contract]", file=sys.stderr)
        return 2
    cpp_binary = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else None
    if cpp_binary and not cpp_binary.is_file():
        print(f"C++ contract not found: {cpp_binary}", file=sys.stderr)
        return 2
    fc = shlex.split(os.environ.get("FC", "gfortran"))
    if not fc or (shutil.which(fc[0]) is None and not Path(fc[0]).exists()):
        print(f"compiler not found: {fc[0] if fc else '<empty>'}", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="horizontal-scalar-regression-") as tmp:
        work = Path(tmp)
        module = """MODULE scalar_mod
  IMPLICIT NONE
  REAL, PARAMETER :: g=9.81
  INTEGER, PARAMETER :: bdyzone=4
  TYPE :: grid_config_rec_type
    LOGICAL :: specified=.FALSE., nested=.FALSE., open_xs=.FALSE., open_xe=.FALSE.
    LOGICAL :: open_ys=.FALSE., open_ye=.FALSE., periodic_x=.FALSE., periodic_y=.FALSE.
    LOGICAL :: symmetric_xs=.FALSE., symmetric_xe=.FALSE., symmetric_ys=.FALSE., symmetric_ye=.FALSE.
    LOGICAL :: polar=.FALSE.
  END TYPE grid_config_rec_type
CONTAINS
""" + extract(DIFFUSION, "compute_diff_metrics") + "\n" + extract(BOUNDARY, "set_physical_bc3d") + "\n" + extract(DIFFUSION, "horizontal_diffusion_s") + "\nEND MODULE scalar_mod\n"
        (work / "scalar_mod.F90").write_text(module, encoding="ascii")
        (work / "driver.f90").write_text(DRIVER, encoding="ascii")
        exe = work / "scalar_regression.exe"
        # Two arithmetic precisions on the same source, not a Python matrix.
        for precision, flags in (("default REAL", []), ("REAL64", ["-fdefault-real-8"])):
            command = [*fc, *flags, "-O0", "-cpp", "-fcheck=bounds",
                       "-ffree-line-length-none", str(work / "scalar_mod.F90"),
                       str(work / "driver.f90"), "-o", str(exe)]
            build = subprocess.run(command, cwd=work, text=True, capture_output=True)
            if build.returncode:
                print(build.stdout, end="")
                print(build.stderr, end="", file=sys.stderr)
                return build.returncode
            print(precision, flush=True)
            env = os.environ.copy()
            if cpp_binary:
                env["SDIRK3_SCALAR_PARITY_DUMP"] = "1"
            run = subprocess.run([str(exe)], cwd=work, text=True,
                                 capture_output=True, env=env)
            if cpp_binary:
                print("\n".join(line for line in run.stdout.splitlines()
                                if not line.startswith("F_PARITY ")))
            else:
                print(run.stdout, end="")
            print(run.stderr, end="", file=sys.stderr)
            if run.returncode:
                return run.returncode
            if cpp_binary:
                compare_cpp_fortran(precision, run.stdout, cpp_binary)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
