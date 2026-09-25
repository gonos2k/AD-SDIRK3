#!/usr/bin/env python3
"""Source-extracted option-1 U/V momentum diffusion contract.

The test compiles the production ``horizontal_diffusion`` routine and checks
the flat, unit-map U-X periodic and V-Y no-flux eigenmodes.  The expected
coupled tendency contains the hybrid layer mass exactly once:

    tendency = K * (c1(k) * MUT + c2(k)) * lambda_h * field.

For V, the y-face values are a Neumann cosine mode.  Its ghost values are the
even reflection (j=0 copies j=2, j=ny+2 copies j=ny), so the boundary rows are
part of the same discrete eigenmode check.
"""
from __future__ import annotations

import math
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "dyn_em" / "module_big_step_utilities_em.F"
SOURCE = Path(os.environ.get("WRF_HORIZONTAL_DIFFUSION_SOURCE", DEFAULT_SOURCE))


DRIVER = r"""
PROGRAM option1_momentum_diffusion_contract
  USE extracted_horizontal_diffusion
  USE, INTRINSIC :: ieee_arithmetic, ONLY: ieee_is_finite
  IMPLICIT NONE
  INTEGER, PARAMETER :: nx=8, ny=6, nz=4
  INTEGER, PARAMETER :: ids=1, ide=nx+1, jds=1, jde=ny+1, kds=1, kde=nz+1
  INTEGER, PARAMETER :: ims=-2, ime=nx+3, jms=-2, jme=ny+3, kms=1, kme=nz+1
  REAL, PARAMETER :: pi=3.1415926535897932384626433832795
  REAL :: field(ims:ime,kms:kme,jms:jme), tendency(ims:ime,kms:kme,jms:jme)
  REAL :: mut(ims:ime,jms:jme), c1(kms:kme), c2(kms:kme)
  REAL :: msfux(ims:ime,jms:jme), msfuy(ims:ime,jms:jme)
  REAL :: msfvx(ims:ime,jms:jme), msfvx_inv(ims:ime,jms:jme)
  REAL :: msfvy(ims:ime,jms:jme), msftx(ims:ime,jms:jme), msfty(ims:ime,jms:jme)
  REAL :: kh(ims:ime,kms:kme,jms:jme), rdx, rdy, mass, q, expected
  REAL :: maxerr, signal, budget, work, dot_product
  REAL :: mut_values(2)
  INTEGER :: i,j,k,mut_case,component,ip
  TYPE(grid_config_rec_type) :: cfg

  rdx=0.1; rdy=0.07
  mut_values=[80000.,120000.]
  c1=1.; c2=0.
  c1(1:4)=[0.25,0.50,0.75,1.00]
  c2(1:4)=[3000.,4000.,5000.,6000.]
  msfux=1.; msfuy=1.; msfvx=1.; msfvx_inv=1.
  msfvy=1.; msftx=1.; msfty=1.
  kh=2.
  cfg%specified=.FALSE.; cfg%nested=.FALSE.
  cfg%open_xs=.FALSE.; cfg%open_xe=.FALSE.
  cfg%open_ys=.FALSE.; cfg%open_ye=.FALSE.
  cfg%periodic_x=.TRUE.; cfg%polar=.FALSE.

  DO mut_case=1,2
    mut=mut_values(mut_case)
    DO component=1,2
      field=0.; tendency=0.
      IF (component==1) THEN
        ! U is periodic in X.  All halo values are periodic copies.
        DO j=jms,jme
          DO i=ims,ime
            ip=MODULO(i-1,nx)
            DO k=kms,kme
              field(i,k,j)=SIN(2.*pi*REAL(ip)/REAL(nx))
            END DO
          END DO
        END DO
        CALL horizontal_diffusion('u',field,tendency,mut,c1,c2,cfg, &
             msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
             2.,kh,rdx,rdy,ids,ide,jds,jde,kds,kde, &
             ims,ime,jms,jme,kms,kme,ids,ide-1,jds,jde-1,kds,kde)
      ELSE
        ! V is on y faces.  q_j=cos(pi*(j-1)/ny) is Neumann data;
        ! its ghost rows j=0 and j=ny+2 equal the reflected interior rows.
        DO j=jms,jme
          DO i=ims,ime
            DO k=kms,kme
              field(i,k,j)=COS(pi*REAL(j-1)/REAL(ny))
            END DO
          END DO
        END DO
        CALL horizontal_diffusion('v',field,tendency,mut,c1,c2,cfg, &
             msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
             2.,kh,rdx,rdy,ids,ide,jds,jde,kds,kde, &
             ims,ime,jms,jme,kms,kme,ids,ide-1,jds,jde,kds,kde)
      END IF

      maxerr=0.; signal=0.; dot_product=0.
      IF (component==1) THEN
        work=-4.*SIN(pi/REAL(nx))**2*rdx**2
        DO j=1,ny
          DO k=1,nz
            mass=c1(k)*mut_values(mut_case)+c2(k)
            DO i=1,nx
              q=SIN(2.*pi*REAL(i-1)/REAL(nx))
              expected=2.*mass*work*q
              IF (.NOT.ieee_is_finite(tendency(i,k,j))) ERROR STOP 'nonfinite U tendency'
              maxerr=MAX(maxerr,ABS(tendency(i,k,j)-expected))
              signal=MAX(signal,ABS(expected))
              dot_product=dot_product+q*tendency(i,k,j)
            END DO
          END DO
        END DO
      ELSE
        work=-4.*SIN(pi/(2.*REAL(ny)))**2*rdy**2
        DO j=1,ny+1
          DO k=1,nz
            mass=c1(k)*mut_values(mut_case)+c2(k)
            DO i=1,nx
              q=COS(pi*REAL(j-1)/REAL(ny))
              expected=2.*mass*work*q
              IF (.NOT.ieee_is_finite(tendency(i,k,j))) ERROR STOP 'nonfinite V tendency'
              maxerr=MAX(maxerr,ABS(tendency(i,k,j)-expected))
              signal=MAX(signal,ABS(expected))
              dot_product=dot_product+q*tendency(i,k,j)
            END DO
          END DO
        END DO
      END IF
      budget=128.*EPSILON(1.)*MAX(1.,signal)
      IF (maxerr>budget) THEN
        WRITE(*,'(A,1X,A,1X,I0,4(1X,ES14.6))') 'FAIL', &
             MERGE('U-X','V-Y',component==1),mut_case,maxerr,budget,signal,dot_product
        ERROR STOP 'option-1 momentum eigenvalue mismatch'
      END IF
      IF (signal<=100.*budget) ERROR STOP 'eigenmode signal is unresolved'
      IF (.NOT.(dot_product<0.)) ERROR STOP 'positive diffusivity is not dissipative'
      WRITE(*,'(A,1X,A,1X,I0,4(1X,ES14.6))') 'PASS', &
           MERGE('U-X','V-Y',component==1),mut_case,maxerr,budget,signal,dot_product
    END DO
  END DO
END PROGRAM option1_momentum_diffusion_contract
"""


def extract_routine(source: Path = SOURCE) -> str:
    text = source.read_text(encoding="ascii")
    match = re.search(
        r"(?ms)^\s*SUBROUTINE\s+horizontal_diffusion\s*\(.*?"
        r"^\s*END\s+SUBROUTINE\s+horizontal_diffusion\s*$",
        text,
    )
    if match is None:
        raise RuntimeError(f"source Fortran routine not found in {source}")
    return match.group(0)


def compile_and_run(compiler: list[str], optimization: str,
                    real64: bool) -> subprocess.CompletedProcess[str]:
    routine = extract_routine()
    module = (
        "MODULE extracted_horizontal_diffusion\n"
        "  IMPLICIT NONE\n"
        "  TYPE grid_config_rec_type\n"
        "    LOGICAL :: specified=.FALSE., nested=.FALSE.\n"
        "    LOGICAL :: open_xs=.FALSE., open_xe=.FALSE.\n"
        "    LOGICAL :: open_ys=.FALSE., open_ye=.FALSE.\n"
        "    LOGICAL :: periodic_x=.FALSE., polar=.FALSE.\n"
        "  END TYPE grid_config_rec_type\n"
        "CONTAINS\n" + routine + "\nEND MODULE extracted_horizontal_diffusion\n"
    )
    with tempfile.TemporaryDirectory(prefix="option1-momentum-diffusion-") as tmp:
        work = Path(tmp)
        source = work / "horizontal_diffusion.f90"
        driver = work / "driver.f90"
        executable = work / "contract.exe"
        source.write_text(module, encoding="ascii")
        driver.write_text(DRIVER, encoding="ascii")
        flags = [optimization, "-fcheck=bounds", "-ffree-line-length-none"]
        if real64:
            flags.append("-fdefault-real-8")
        build = subprocess.run(
            [*compiler, *flags, str(source), str(driver), "-o", str(executable)],
            cwd=work, text=True, capture_output=True,
        )
        if build.returncode:
            return build
        return subprocess.run([str(executable)], cwd=work,
                              text=True, capture_output=True)


def main() -> int:
    compiler = shlex.split(os.environ.get("FC", "gfortran"))
    if not compiler or shutil.which(compiler[0]) is None and not Path(compiler[0]).exists():
        print(f"Fortran compiler not found: {compiler!r}", file=sys.stderr)
        return 2
    failed = False
    for precision, real64 in (("REAL32", False), ("REAL64", True)):
        for optimization in ("-O0", "-O2"):
            result = compile_and_run(compiler, optimization, real64)
            print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
            print(f"{precision} {optimization}: exit={result.returncode}")
            failed |= result.returncode != 0
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
