#!/usr/bin/env python3
"""Compare option-1 theta diffusion with source-extracted WRF Fortran.

The fixture has variable hybrid layer mass and physical diffusivity. It uses
unit maps and Y-invariant fields to isolate the mass-point/face ownership and
the periodic X seam; terrain-metric option 2 is a separate operator.
"""
from __future__ import annotations

import math
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "dyn_em" / "module_big_step_utilities_em.F"
DRIVER = r"""
PROGRAM hybrid_layer_mass_contract
  USE extracted_hdiff
  USE, INTRINSIC :: ieee_arithmetic, ONLY: ieee_is_finite
  IMPLICIT NONE
  INTEGER, PARAMETER :: nx=8,ny=6,nz=4
  INTEGER, PARAMETER :: ims=-4,ime=nx+5,jms=-4,jme=ny+5,kms=0,kme=nz+2
  INTEGER, PARAMETER :: ids=1,ide=nx+1,jds=1,jde=ny+1,kds=1,kde=nz+1
  REAL :: field(ims:ime,kms:kme,jms:jme),base(ims:ime,kms:kme,jms:jme)
  REAL :: kh(ims:ime,kms:kme,jms:jme),tend(ims:ime,kms:kme,jms:jme)
  REAL :: mut(ims:ime,jms:jme),msftx(ims:ime,jms:jme)
  REAL :: msfty(ims:ime,jms:jme),msfux(ims:ime,jms:jme)
  REAL :: msfuy(ims:ime,jms:jme),msfvx(ims:ime,jms:jme)
  REAL :: msfvx_inv(ims:ime,jms:jme),msfvy(ims:ime,jms:jme)
  REAL :: c1(kms:kme),c2(kms:kme),rdx,rdy
  INTEGER :: i,j,k,x,y,yv,mode
  TYPE(grid_config_rec_type) :: cfg
  rdx=REAL(REAL(.1,KIND=4),KIND=KIND(rdx))
  rdy=REAL(REAL(.13,KIND=4),KIND=KIND(rdy))
  msftx=1.;msfty=1.;msfux=1.;msfuy=1.
  msfvx=1.;msfvx_inv=1.;msfvy=1.
  base=0.;field=0.;kh=0.;tend=0.;mut=0.
  c1=1.;c2=0.
  c1(1:4)=[1.1953125,1.5703125,1.4921875,.296875]
  c2(1:4)=(1.-c1(1:4))*80000.
  cfg%periodic_x=.TRUE.
  DO j=jms,jme
    DO i=ims,ime
      x=MODULO(i-1,nx)
      mut(i,j)=90000.+128.*REAL(x)
      DO k=kms,kme
        field(i,k,j)=1.+.03125*REAL(x)+.0078125*REAL(MOD(x*x,3)) &
                      +.015625*REAL(k-1)
        kh(i,k,j)=2.+.125*REAL(x)+.0625*REAL(k-1)
      END DO
    END DO
  END DO
  DO mode=1,4
    tend=0.
    IF (mode==2) THEN
      c1=1.;c2=0.
    ELSE IF (mode==3) THEN
      c1(1:4)=[1.1953125,1.5703125,1.4921875,.296875]
      c2(1:4)=(1.-c1(1:4))*80000.
      DO j=jms,jme
        y=MAX(0,MIN(ny-1,j-1))
        yv=MAX(0,MIN(ny,j-1))
        DO i=ims,ime
          x=MODULO(i-1,nx)
          msftx(i,j)=1.+REAL(x)/32.+REAL(y)/64.
          msfty(i,j)=1.+REAL(x)/64.+REAL(y)/32.
          msfux(i,j)=1.+REAL(x)/16.+REAL(y)/128.
          msfuy(i,j)=1.+REAL(x)/64.+REAL(y)/32.
          msfvx(i,j)=1.+REAL(x)/32.+REAL(yv)/128.
          msfvx_inv(i,j)=1./msfvx(i,j)
          msfvy(i,j)=1.+REAL(x)/128.+REAL(yv)/16.
          mut(i,j)=90000.+128.*REAL(x)+256.*REAL(y)
          DO k=kms,kme
            field(i,k,j)=1.+.03125*REAL(x)+.0078125*REAL(MOD(x*x,3)) &
              +.015625*REAL(k-1)+.0625*REAL(y)+.015625*REAL(MOD(y*y,2)) &
              +.0078125*REAL(x*y)
            kh(i,k,j)=2.+.125*REAL(x)+.0625*REAL(k-1)+.03125*REAL(y)
          END DO
        END DO
      END DO
    ELSE IF (mode==4) THEN
      DO j=jms,jme
        y=MAX(0,MIN(ny-1,j-1))
        DO i=ims,ime
          x=MODULO(i-1,nx)
          DO k=kms,kme
            base(i,k,j)=.5*REAL(x)+.125*REAL(y)+ &
              .0625*REAL(MOD(x*x,3))+.03125*REAL(k-1)
            field(i,k,j)=field(i,k,j)+base(i,k,j)
          END DO
        END DO
      END DO
    END IF
    CALL horizontal_diffusion_3dmp('m',field,tend,mut,c1,c2,cfg,base, &
        msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty,2.,kh,rdx,rdy, &
        ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
        ids,ide,jds,jde,kds,kde)
    DO j=1,ny
      DO k=1,nz
        DO i=1,nx
          IF (.NOT.ieee_is_finite(tend(i,k,j))) ERROR STOP 'nonfinite Fortran tendency'
          IF (mode==1) THEN
            WRITE(*,'(A,3(1X,I0),1X,ES25.16E3)') 'F_HYBRID',j,k,i,tend(i,k,j)
          ELSE IF (mode==2) THEN
            WRITE(*,'(A,3(1X,I0),1X,ES25.16E3)') 'F_SIGMA',j,k,i,tend(i,k,j)
          ELSE IF (mode==3) THEN
            WRITE(*,'(A,3(1X,I0),1X,ES25.16E3)') 'F_MAP',j,k,i,tend(i,k,j)
          ELSE
            WRITE(*,'(A,3(1X,I0),1X,ES25.16E3)') 'F_BASE',j,k,i,tend(i,k,j)
          END IF
        END DO
      END DO
    END DO
  END DO
END PROGRAM hybrid_layer_mass_contract
"""


def rows(output: str, tag: str) -> dict[tuple[int, int, int], float]:
    result: dict[tuple[int, int, int], float] = {}
    for line in output.splitlines():
        if not line.startswith(tag + " "):
            continue
        _, j, k, i, value = line.split()
        key = int(j), int(k), int(i)
        if key in result:
            raise RuntimeError(f"duplicate {tag} cell {key}")
        result[key] = float(value)
    expected = {(j, k, i) for j in range(1, 7) for k in range(1, 5)
                for i in range(1, 9)}
    if set(result) != expected or not all(map(math.isfinite, result.values())):
        raise RuntimeError(f"{tag}: incomplete or nonfinite output ({len(result)} cells)")
    return result


def error(left: dict[tuple[int, int, int], float],
          right: dict[tuple[int, int, int], float]) -> float:
    return max(abs(left[key] - right[key]) for key in left)


def run(compiler: list[str], cpp_binary: Path, precision: str,
        extra_flags: list[str], optimization: str) -> None:
    text = SOURCE.read_text(encoding="ascii")
    match = re.search(
        r"(?ms)^\s*SUBROUTINE\s+horizontal_diffusion_3dmp\b.*?"
        r"^\s*END\s+SUBROUTINE\s+horizontal_diffusion_3dmp\s*$", text)
    if not match:
        raise RuntimeError("source Fortran routine not found")
    module = ("MODULE extracted_hdiff\n"
              "TYPE grid_config_rec_type\n"
              " LOGICAL :: specified=.FALSE.,nested=.FALSE.\n"
              " LOGICAL :: open_xs=.FALSE.,open_xe=.FALSE.\n"
              " LOGICAL :: open_ys=.FALSE.,open_ye=.FALSE.\n"
              " LOGICAL :: periodic_x=.FALSE.\n"
              "END TYPE\nCONTAINS\n" + match.group(0)
              + "\nEND MODULE extracted_hdiff\n")
    with tempfile.TemporaryDirectory(prefix="hybrid_scalar_") as tmp:
        path = Path(tmp)
        (path / "source.f90").write_text(module)
        (path / "driver.f90").write_text(DRIVER)
        subprocess.run([*compiler, optimization, "-fcheck=bounds", *extra_flags,
                        "-ffree-line-length-none", "source.f90", "driver.f90",
                        "-o", "contract"], cwd=path, check=True)
        actual = subprocess.run([str(path / "contract")], cwd=path,
                                text=True, capture_output=True, check=True).stdout
    def cpp(mode: str) -> dict[tuple[int, int, int], float]:
        output = subprocess.run([str(cpp_binary), "--hybrid-layer-parity",
                                 precision, mode], text=True, capture_output=True,
                                check=True).stdout
        return rows(output, "C_HYBRID")

    hybrid = rows(actual, "F_HYBRID")
    sigma = rows(actual, "F_SIGMA")
    match_hybrid = cpp("hybrid")
    match_sigma2d = cpp("sigma2d")
    match_sigma3d = cpp("sigma3d")
    legacy = cpp("legacy")
    no_c2 = cpp("no-c2")
    signal = max(abs(value) for value in hybrid.values())
    eps = 2.0 ** (-23 if precision == "fp32" else -52)
    budget = 64.0 * eps * max(1.0, signal)
    hybrid_error = error(hybrid, match_hybrid)
    sigma_error = error(sigma, match_sigma2d)
    sigma_rank_error = error(match_sigma2d, match_sigma3d)
    old_error = error(hybrid, legacy)
    no_c2_error = error(hybrid, no_c2)
    seam_error = max(abs(hybrid[key] - match_hybrid[key]) for key in hybrid
                     if key[2] in (1, 8))
    print(f"option1 hybrid {precision} {optimization}: cells=192 signal={signal:.9g} "
          f"error={hybrid_error:.9g} seam={seam_error:.9g} budget={budget:.9g} "
          f"sigma={sigma_error:.9g} rank={sigma_rank_error:.9g} "
          f"old={old_error:.9g} no_c2={no_c2_error:.9g}")
    if not (signal > 100 * budget and hybrid_error <= budget and
            seam_error <= budget and sigma_error <= budget and
            sigma_rank_error <= budget and old_error > 10 * budget and
            no_c2_error > 10 * budget):
        raise RuntimeError(f"option-1 hybrid layer-mass contract failed: {precision}")

    maps = rows(actual, "F_MAP")
    def cpp_maps(mode: str) -> dict[tuple[int, int, int], float]:
        output = subprocess.run([str(cpp_binary), "--hybrid-map-parity",
                                 precision, mode], text=True, capture_output=True,
                                check=True).stdout
        return rows(output, "C_MAP")
    exact = cpp_maps("exact")
    legacy_x = cpp_maps("old-x")
    legacy_y = cpp_maps("old-y")
    legacy_both = cpp_maps("old-both")
    map_signal = max(abs(value) for value in maps.values())
    map_budget = 64.0 * eps * max(1.0, map_signal)
    map_error = error(maps, exact)
    x_error = error(maps, legacy_x)
    y_error = error(maps, legacy_y)
    both_error = error(maps, legacy_both)
    map_seam_error = max(abs(maps[key] - exact[key]) for key in maps
                         if key[2] in (1, 8))
    print(f"option1 nonunit maps {precision} {optimization}: cells=192 "
          f"signal={map_signal:.9g} error={map_error:.9g} "
          f"seam={map_seam_error:.9g} budget={map_budget:.9g} "
          f"old_x={x_error:.9g} old_y={y_error:.9g} "
          f"old_both={both_error:.9g}")
    if not (map_signal > 100 * map_budget and map_error <= map_budget and
            map_seam_error <= map_budget and x_error > 10 * map_budget and
            y_error > 10 * map_budget and both_error > 10 * map_budget):
        raise RuntimeError(f"option-1 stagger-map contract failed: {precision}")

    base = rows(actual, "F_BASE")
    base_error = error(base, cpp_maps("base"))
    omitted_base_error = error(base, cpp_maps("old-base"))
    print(f"option1 nonuniform t_init {precision} {optimization}: "
          f"cells=192 error={base_error:.9g} budget={map_budget:.9g} "
          f"omitted_base={omitted_base_error:.9g}")
    if not (base_error <= map_budget and omitted_base_error > 10 * map_budget):
        raise RuntimeError(f"option-1 t_init subtraction contract failed: {precision}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_hybrid_scalar_layer_mass.py CPP_TEST_BINARY")
    fc = shlex.split(os.environ.get("FC", "gfortran"))
    target = Path(sys.argv[1]).resolve()
    for level in ("-O0", "-O2"):
        run(fc, target, "fp32", [], level)
        run(fc, target, "fp64", ["-fdefault-real-8"], level)
