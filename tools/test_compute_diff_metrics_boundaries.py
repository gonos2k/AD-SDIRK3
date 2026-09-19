#!/usr/bin/env python3
"""Regression for compute_diff_metrics top and horizontal boundary ownership.

The routine is extracted from dyn_em/module_diffusion_em.F at test time and
compiled with a minimal config type.  The pre-fix source fails because its
periodic/nonperiodic zx/zy boundary loops stop at ktf rather than kte.
The regression checks all four boundary-mode cases.
"""
from __future__ import annotations

import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "dyn_em" / "module_diffusion_em.F"


def extract_routine() -> str:
    text = SOURCE.read_text(encoding="ascii")
    match = re.search(
        r"(?ms)^\s*SUBROUTINE\s+compute_diff_metrics\b.*?^\s*END\s+SUBROUTINE\s+compute_diff_metrics\s*$",
        text,
    )
    if match is None:
        raise RuntimeError(f"could not extract compute_diff_metrics from {SOURCE}")
    return match.group(0)


DRIVER = r"""
PROGRAM test_compute_diff_metrics_boundaries
  USE metric_mod
  USE, INTRINSIC :: ieee_arithmetic, ONLY: ieee_is_finite
  IMPLICIT NONE
  CALL run_case(.TRUE.,  .TRUE.,  'periodic_xy')
  CALL run_case(.TRUE.,  .FALSE., 'periodic_x')
  CALL run_case(.FALSE., .TRUE.,  'periodic_y')
  CALL run_case(.FALSE., .FALSE., 'nonperiodic_xy')
  WRITE(*,'(A)') 'compute_diff_metrics boundary regression: PASS'
CONTAINS
  SUBROUTINE run_case(periodic_x, periodic_y, label)
    LOGICAL, INTENT(IN) :: periodic_x, periodic_y
    CHARACTER(LEN=*), INTENT(IN) :: label
    INTEGER, PARAMETER :: nx=4, ny=4, nz=4
    INTEGER, PARAMETER :: ids=1, ide=nx+1, jds=1, jde=ny+1
    INTEGER, PARAMETER :: kds=1, kde=nz+1
    INTEGER, PARAMETER :: ims=0, ime=nx+1, jms=0, jme=ny+1
    INTEGER, PARAMETER :: kms=0, kme=nz+2
    INTEGER, PARAMETER :: its=1, ite=ide, jts=1, jte=jde
    INTEGER, PARAMETER :: kts=1, kte=nz+1
    REAL :: ph(ims:ime,kms:kme,jms:jme)
    REAL :: phb(ims:ime,kms:kme,jms:jme)
    REAL :: z(ims:ime,kms:kme,jms:jme)
    REAL :: rdz(ims:ime,kms:kme,jms:jme)
    REAL :: rdzw(ims:ime,kms:kme,jms:jme)
    REAL :: zx(ims:ime,kms:kme,jms:jme)
    REAL :: zy(ims:ime,kms:kme,jms:jme)
    TYPE(grid_config_rec_type) :: cfg
    INTEGER :: i,j,k,ip,jp
    REAL :: rdx, rdy, expected, err, tol
    REAL, PARAMETER :: pi=3.1415926535897932384626433832795

    cfg%periodic_x=periodic_x; cfg%periodic_y=periodic_y
    rdx=0.17; rdy=0.23; tol=2.0E-5
    ph=0.0; phb=0.0
    ! A positive vertical column with nonzero top horizontal slopes.  Physical
    ! i,j are 1..4; both halos are periodic analytic copies for this fixture.
    DO j=jms,jme
      jp=MODULO(j-1,ny)+1
      DO k=kms,kme
        DO i=ims,ime
          ip=MODULO(i-1,nx)+1
          ph(i,k,j)=g*(100.0*k + 2.0*SIN(2.0*pi*REAL(ip-1)/REAL(nx)) &
                       + 3.0*COS(2.0*pi*REAL(jp-1)/REAL(ny)) &
                       + 0.1*REAL(k)*SIN(2.0*pi*REAL(ip-1)/REAL(nx)))
          phb(i,k,j)=g*(0.25*COS(2.0*pi*REAL(ip-1)/REAL(nx)) &
                        + 0.4*SIN(2.0*pi*REAL(jp-1)/REAL(ny)) &
                        + 0.03*REAL(k)*COS(2.0*pi*REAL(ip-1)/REAL(nx)))
        END DO
      END DO
    END DO
    ! Poison every output so an unassigned top or boundary slot is observable.
    z=-777.0; rdz=-777.0; rdzw=-777.0; zx=-777.0; zy=-777.0

    CALL compute_diff_metrics(cfg,ph,phb,z,rdz,rdzw,zx,zy,rdx,rdy, &
         ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
         its,ite,jts,jte,kts,kte)

    err=0.0
    ! Interior faces must be the source backward differences through k=kte.
    DO k=1,kte
      DO j=1,ny
        DO i=2,nx
          expected=rdx*((ph(i,k,j)+phb(i,k,j))-(ph(i-1,k,j)+phb(i-1,k,j)))/g
          CALL update_error(err,zx(i,k,j),expected)
        END DO
      END DO
      DO j=2,ny
        DO i=1,nx
          expected=rdy*((ph(i,k,j)+phb(i,k,j))-(ph(i,k,j-1)+phb(i,k,j-1)))/g
          CALL update_error(err,zy(i,k,j),expected)
        END DO
      END DO
    END DO
    ! Endpoint ownership is tested independently for periodic and open modes.
    DO k=1,kte
      DO j=1,ny
        IF (periodic_x) THEN
          expected=rdx*((ph(ids,k,j)+phb(ids,k,j))-(ph(ids-1,k,j)+phb(ids-1,k,j)))/g
          CALL update_error(err,zx(ids,k,j),expected)
          expected=rdx*((ph(ide,k,j)+phb(ide,k,j))-(ph(ide-1,k,j)+phb(ide-1,k,j)))/g
          CALL update_error(err,zx(ide,k,j),expected)
        ELSE
          CALL update_error(err,zx(ids,k,j),0.0)
          CALL update_error(err,zx(ide,k,j),0.0)
        END IF
      END DO
      DO i=1,nx
        IF (periodic_y) THEN
          expected=rdy*((ph(i,k,jds)+phb(i,k,jds))-(ph(i,k,jds-1)+phb(i,k,jds-1)))/g
          CALL update_error(err,zy(i,k,jds),expected)
          expected=rdy*((ph(i,k,jde)+phb(i,k,jde))-(ph(i,k,jde-1)+phb(i,k,jde-1)))/g
          CALL update_error(err,zy(i,k,jde),expected)
        ELSE
          CALL update_error(err,zy(i,k,jds),0.0)
          CALL update_error(err,zy(i,k,jde),0.0)
        END IF
      END DO
    END DO
    IF (err > tol) THEN
      WRITE(*,'(A,1X,A,1X,ES12.4)') 'compute_diff_metrics boundary regression:',TRIM(label)//' FAIL maxerr=',err
      ERROR STOP 1
    END IF
    WRITE(*,'(A,1X,A,1X,ES12.4)') 'case',TRIM(label)//' PASS maxerr=',err
  END SUBROUTINE run_case

    SUBROUTINE update_error(accum,actual,expected)
      REAL, INTENT(INOUT) :: accum
      REAL, INTENT(IN) :: actual, expected
      IF (.NOT. ieee_is_finite(actual) .OR. .NOT. ieee_is_finite(expected)) THEN
        accum=HUGE(accum)
      ELSE
        accum=MAX(accum,ABS(actual-expected))
      END IF
    END SUBROUTINE update_error
END PROGRAM test_compute_diff_metrics_boundaries
"""


def compile_and_run() -> int:
    fc = shlex.split(os.environ.get("FC", "gfortran"))
    if not fc:
        print("FC is empty", file=sys.stderr)
        return 2
    compiler = fc[0]
    if shutil.which(compiler) is None and not Path(compiler).exists():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2
    fflags = shlex.split(os.environ.get("FFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="compute-diff-metrics-regression-") as tmp:
        work = Path(tmp)
        routine = extract_routine()
        module = (
            "MODULE metric_mod\n"
            "  IMPLICIT NONE\n"
            "  REAL, PARAMETER :: g=9.81\n"
            "  TYPE :: grid_config_rec_type\n"
            "    LOGICAL :: periodic_x=.FALSE., periodic_y=.FALSE.\n"
            "  END TYPE grid_config_rec_type\n"
            "CONTAINS\n" + routine + "\nEND MODULE metric_mod\n"
        )
        source = work / "metric_regression.f90"
        driver = work / "driver.f90"
        exe = work / "metric_regression.exe"
        source.write_text(module, encoding="ascii")
        driver.write_text(DRIVER, encoding="ascii")
        command = [
            *fc,
            *fflags,
            "-O0",
            "-fcheck=bounds",
            "-ffree-line-length-none",
            str(source),
            str(driver),
            "-o",
            str(exe),
        ]
        build = subprocess.run(command, cwd=work, text=True, capture_output=True)
        if build.returncode:
            print(build.stdout, end="")
            print(build.stderr, end="", file=sys.stderr)
            return build.returncode
        run = subprocess.run([str(exe)], cwd=work, text=True, capture_output=True)
        print(run.stdout, end="")
        print(run.stderr, end="", file=sys.stderr)
        return run.returncode


def main() -> int:
    return compile_and_run()


if __name__ == "__main__":
    raise SystemExit(main())
