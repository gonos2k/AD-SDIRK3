#!/usr/bin/env python3
"""Source-extracted option-1 U/V/W momentum diffusion contract.

The test compiles the production ``horizontal_diffusion`` routine and checks
the flat, unit-map U-X/W-X periodic and V-Y zero-wall eigenmodes.  An optional
libtorch contract binary can be supplied to compare raw owned-cell tendencies
against the same source-extracted Fortran routine.  The expected coupled
tendency contains the hybrid layer mass exactly once:

    tendency = K * (c1(k) * MUT + c2(k)) * lambda_h * field.

For V, the y-face values are a Dirichlet sine mode with zero normal velocity
at both walls.  Its ghost values use odd reflection (q_0=-q_2 and
q_(ny+2)=-q_ny), so the boundary rows are part of the same discrete
eigenmode check.
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
  INTEGER, PARAMETER :: packed_nx=7, packed_ny=5
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
  INTEGER :: i,j,k,mut_case,component,ip,component_lo,component_hi
  INTEGER :: requested_case,requested_component,ios,x,y,yf,k0
  CHARACTER(LEN=16) :: arg1,arg2,arg3,component_name
  LOGICAL :: dump_mode,packed_mode
  TYPE(grid_config_rec_type) :: cfg

  ! Match the C++ ABI: spacing inputs are float even in the REAL64 contract.
  rdx=REAL(REAL(.1,KIND=4),KIND=KIND(rdx))
  rdy=REAL(REAL(.07,KIND=4),KIND=KIND(rdy))
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

  dump_mode=.FALSE.
  packed_mode=.FALSE.
  requested_component=0
  requested_case=0
  IF (COMMAND_ARGUMENT_COUNT()>0) THEN
    CALL GET_COMMAND_ARGUMENT(1,arg1)
    IF (TRIM(arg1)=='--dump-packed') THEN
      IF (COMMAND_ARGUMENT_COUNT()/=2) ERROR STOP 'usage: --dump-packed U|V|W'
      CALL GET_COMMAND_ARGUMENT(2,arg2)
      packed_mode=.TRUE.
    ELSE IF (TRIM(arg1)=='--dump') THEN
      IF (COMMAND_ARGUMENT_COUNT()/=3) ERROR STOP 'usage: --dump U|V|W CASE'
      CALL GET_COMMAND_ARGUMENT(2,arg2)
      CALL GET_COMMAND_ARGUMENT(3,arg3)
      READ(arg3,*,IOSTAT=ios) requested_case
      IF (ios/=0 .OR. requested_case<1 .OR. requested_case>2) &
        ERROR STOP 'dump CASE must be 1 or 2'
      dump_mode=.TRUE.
    ELSE
      ERROR STOP 'usage: --dump U|V|W CASE or --dump-packed U|V|W'
    END IF
    SELECT CASE(TRIM(arg2))
    CASE('U'); requested_component=1
    CASE('V'); requested_component=2
    CASE('W'); requested_component=3
    CASE DEFAULT; ERROR STOP 'dump component must be U, V, or W'
    END SELECT
  END IF

  DO mut_case=1,2
    IF (packed_mode .AND. mut_case/=1) CYCLE
    IF (dump_mode .AND. mut_case/=requested_case) CYCLE
    IF (.NOT.packed_mode) mut=mut_values(mut_case)
    IF (packed_mode) THEN
      c1(1:5)=[0.25,0.50,0.75,1.00,1.00]
      c2(1:5)=[3000.,4000.,5000.,6000.,6000.]
      DO j=jms,jme
        y=MIN(MAX(j-1,0),packed_ny-1)
        DO i=ims,ime
          x=MODULO(i-1,packed_nx)
          mut(i,j)=80000.+128.*REAL(x)+256.*REAL(y)
          msftx(i,j)=1.+REAL(x)/32.+REAL(y)/64.
          msfty(i,j)=1.+REAL(x)/64.+REAL(y)/32.
          msfux(i,j)=1.+REAL(x)/16.+REAL(y)/128.
          msfuy(i,j)=1.+REAL(x)/64.+REAL(y)/32.
          yf=MIN(MAX(j-1,0),packed_ny)
          msfvx(i,j)=1.+REAL(x)/32.+REAL(yf)/128.
          msfvy(i,j)=1.+REAL(x)/128.+REAL(yf)/16.
          msfvx_inv(i,j)=1./msfvx(i,j)
          DO k=kms,kme
            k0=k-1
            kh(i,k,j)=2.+REAL(x)/8.+REAL(y)/16.+REAL(k0)/32.
          END DO
        END DO
      END DO
    END IF
    DO component=1,3
      IF ((dump_mode .OR. packed_mode) .AND. component/=requested_component) CYCLE
      field=0.; tendency=0.
      IF (component==1) THEN
        IF (packed_mode) THEN
          ! U uses periodic x-face aliases and replicated y halos.
          DO j=jms,jme
            y=MIN(MAX(j-1,0),packed_ny-1)
            DO i=ims,ime
              x=MODULO(i-1,packed_nx)
              DO k=kms,kme
                k0=k-1
                field(i,k,j)=SIN(2.*pi*REAL(x)/REAL(packed_nx))* &
                    (1.+REAL(y)/16.+REAL(k0)/32.)
              END DO
            END DO
          END DO
        ELSE
          ! U is periodic in X.  All halo values are periodic copies.
          DO j=jms,jme
            DO i=ims,ime
              ip=MODULO(i-1,nx)
              DO k=kms,kme
                field(i,k,j)=SIN(2.*pi*REAL(ip)/REAL(nx))
              END DO
            END DO
          END DO
        END IF
        CALL horizontal_diffusion('u',field,tendency,mut,c1,c2,cfg, &
             msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
             2.,kh,rdx,rdy,ids,ide,jds,jde,kds,kde, &
             ims,ime,jms,jme,kms,kme,ids,ide-1,jds, &
             MERGE(packed_ny,ny,packed_mode),kds,kde)
      ELSE IF (component==2) THEN
        IF (packed_mode) THEN
          ! V has zero physical wall values and odd-reflected y ghosts.
          DO j=jms,jme
            yf=j-1
            DO i=ims,ime
              x=MODULO(i-1,packed_nx)
              DO k=kms,kme
                k0=k-1
                IF (yf==0 .OR. yf==packed_ny) THEN
                  field(i,k,j)=0.
                ELSE
                  field(i,k,j)=SIN(pi*REAL(yf)/REAL(packed_ny))* &
                      (1.+REAL(x)/16.+REAL(k0)/32.)
                END IF
              END DO
            END DO
          END DO
        ELSE
          ! V is on y faces with zero normal velocity at both walls.
          ! q_j=sin(pi*(j-1)/ny); the all-j formula gives odd ghosts
          ! q_0=-q_2 and q_(ny+2)=-q_ny.
          DO j=jms,jme
            DO i=ims,ime
              DO k=kms,kme
                field(i,k,j)=SIN(pi*REAL(j-1)/REAL(ny))
              END DO
            END DO
          END DO
        END IF
        CALL horizontal_diffusion('v',field,tendency,mut,c1,c2,cfg, &
             msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
             2.,kh,rdx,rdy,ids,ide,jds,jde,kds,kde, &
             ims,ime,jms,jme,kms,kme,ids,MERGE(packed_nx,nx,packed_mode), &
             jds,MERGE(packed_ny+1,ny+1,packed_mode),kds,kde)
      ELSE
        IF (packed_mode) THEN
          ! W has periodic X and replicated Y; only internal levels are owned.
          DO j=jms,jme
            y=MIN(MAX(j-1,0),packed_ny-1)
            DO i=ims,ime
              x=MODULO(i-1,packed_nx)
              DO k=kms,kme
                k0=k-1
                field(i,k,j)=SIN(2.*pi*REAL(x)/REAL(packed_nx))* &
                    (1.+REAL(y)/16.+REAL(k0)/32.)
              END DO
            END DO
          END DO
        ELSE
          ! W is periodic in X and constant in Y.  The routine owns only
          ! interior W levels k=2..nz; all other stencil inputs are halos.
          DO j=jms,jme
            DO i=ims,ime
              ip=MODULO(i-1,nx)
              DO k=kms,kme
                field(i,k,j)=SIN(2.*pi*REAL(ip)/REAL(nx))
              END DO
            END DO
          END DO
        END IF
        CALL horizontal_diffusion('w',field,tendency,mut,c1,c2,cfg, &
             msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
             2.,kh,rdx,rdy,ids,ide,jds,jde,kds,kde, &
             ims,ime,jms,jme,kms,kme,ids,MERGE(packed_nx,nx,packed_mode), &
             jds,jde-1,kds,kde)
      END IF

      IF (packed_mode) THEN
        IF (component==1) THEN
          component_lo=1; component_hi=packed_ny; component_name='U'
          DO j=component_lo,component_hi
            DO k=1,nz
              DO i=1,packed_nx+1
                IF (.NOT.ieee_is_finite(tendency(i,k,j))) &
                  ERROR STOP 'nonfinite packed U tendency'
                WRITE(*,'(A,1X,A,3(1X,I0),1X,ES24.16E3)') &
                    'F_PACK',TRIM(component_name),j,k,i,tendency(i,k,j)
              END DO
            END DO
          END DO
        ELSE IF (component==2) THEN
          component_lo=1; component_hi=packed_ny+1; component_name='V'
          DO j=component_lo,component_hi
            DO k=1,nz
              DO i=1,packed_nx
                IF (.NOT.ieee_is_finite(tendency(i,k,j))) &
                  ERROR STOP 'nonfinite packed V tendency'
                WRITE(*,'(A,1X,A,3(1X,I0),1X,ES24.16E3)') &
                    'F_PACK',TRIM(component_name),j,k,i,tendency(i,k,j)
              END DO
            END DO
          END DO
        ELSE
          component_lo=1; component_hi=packed_ny; component_name='W'
          DO j=component_lo,component_hi
            DO k=2,nz
              DO i=1,packed_nx
                IF (.NOT.ieee_is_finite(tendency(i,k,j))) &
                  ERROR STOP 'nonfinite packed W tendency'
                WRITE(*,'(A,1X,A,3(1X,I0),1X,ES24.16E3)') &
                    'F_PACK',TRIM(component_name),j,k,i,tendency(i,k,j)
              END DO
            END DO
          END DO
        END IF
        CYCLE
      END IF

      maxerr=0.; signal=0.; dot_product=0.
      IF (component==1 .OR. component==3) THEN
        work=-4.*SIN(pi/REAL(nx))**2*rdx**2
        DO j=1,ny
          IF (component==1) THEN
            component_lo=1
          ELSE
            component_lo=2
          END IF
          DO k=component_lo,nz
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
              q=SIN(pi*REAL(j-1)/REAL(ny))
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
        component_name='U-X'
        IF(component==2) component_name='V-Y'
        IF(component==3) component_name='W-X'
        WRITE(*,'(A,1X,A,1X,I0,4(1X,ES14.6))') 'FAIL', &
             TRIM(component_name),mut_case,maxerr,budget,signal,dot_product
        ERROR STOP 'option-1 momentum eigenvalue mismatch'
      END IF
      IF (signal<=100.*budget) ERROR STOP 'eigenmode signal is unresolved'
      IF (.NOT.(dot_product<0.)) ERROR STOP 'positive diffusivity is not dissipative'
      IF (dump_mode) THEN
        SELECT CASE(component)
        CASE(1)
          component_name='U'; component_lo=1; component_hi=ny
          DO j=component_lo,component_hi
            DO k=1,nz
              DO i=1,nx
                WRITE(*,'(A,1X,A,3(1X,I0),1X,ES24.16E3)') &
                    'F_MOM',TRIM(component_name),j,k,i,tendency(i,k,j)
              END DO
            END DO
          END DO
        CASE(2)
          component_name='V'; component_lo=1; component_hi=ny+1
          DO j=component_lo,component_hi
            DO k=1,nz
              DO i=1,nx
                WRITE(*,'(A,1X,A,3(1X,I0),1X,ES24.16E3)') &
                    'F_MOM',TRIM(component_name),j,k,i,tendency(i,k,j)
              END DO
            END DO
          END DO
        CASE(3)
          component_name='W'; component_lo=1; component_hi=ny
          DO j=component_lo,component_hi
            DO k=2,nz
              DO i=1,nx
                WRITE(*,'(A,1X,A,3(1X,I0),1X,ES24.16E3)') &
                    'F_MOM',TRIM(component_name),j,k,i,tendency(i,k,j)
              END DO
            END DO
          END DO
        END SELECT
      ELSE
        component_name='U-X'
        IF(component==2) component_name='V-Y'
        IF(component==3) component_name='W-X'
        WRITE(*,'(A,1X,A,1X,I0,4(1X,ES14.6))') 'PASS', &
             TRIM(component_name),mut_case,maxerr,budget,signal,dot_product
      END IF
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


def parse_rows(output: str, marker: str, component: str) -> dict[tuple[str, int, int, int], float]:
    rows: dict[tuple[str, int, int, int], float] = {}
    for line in output.splitlines():
        fields = line.split()
        if not fields or fields[0] != marker:
            continue
        if len(fields) != 6 or fields[1] != component:
            raise RuntimeError(f"malformed {marker} row: {line!r}")
        _, name, j, k, i, value = fields
        key = (name, int(j), int(k), int(i))
        if key in rows:
            raise RuntimeError(f"duplicate {marker} row: {key}")
        parsed = float(value.replace("D", "E").replace("d", "e"))
        if not math.isfinite(parsed):
            raise RuntimeError(f"nonfinite {marker} row: {line!r}")
        rows[key] = parsed
    if not rows:
        raise RuntimeError(f"no {marker} {component} rows found")
    return rows


def owned_keys(component: str) -> set[tuple[str, int, int, int]]:
    ny, nz, nx = 6, 4, 8
    if component == "U":
        k_range, j_range = range(1, nz + 1), range(1, ny + 1)
    elif component == "V":
        k_range, j_range = range(1, nz + 1), range(1, ny + 2)
    else:
        k_range, j_range = range(2, nz + 1), range(1, ny + 1)
    return {
        (component, j, k, i)
        for j in j_range for k in k_range for i in range(1, nx + 1)
    }


def packed_owned_keys(component: str) -> set[tuple[str, int, int, int]]:
    nx, ny, nz = 7, 5, 4
    if component == "U":
        i_range, j_range, k_range = range(1, nx + 2), range(1, ny + 1), range(1, nz + 1)
    elif component == "V":
        i_range, j_range, k_range = range(1, nx + 1), range(1, ny + 2), range(1, nz + 1)
    else:
        i_range, j_range, k_range = range(1, nx + 1), range(1, ny + 1), range(2, nz + 1)
    return {
        (component, j, k, i)
        for j in j_range for k in k_range for i in i_range
    }


def run_fortran_packed(fortran_exe: Path) -> tuple[list[str], bool]:
    messages: list[str] = []
    failed = False
    for component in ("U", "V", "W"):
        result = subprocess.run(
            [str(fortran_exe), "--dump-packed", component],
            text=True, capture_output=True,
        )
        if result.returncode:
            messages.append(
                f"Fortran packed {component}: exit={result.returncode}\n{result.stderr}"
            )
            failed = True
            continue
        try:
            rows = parse_rows(result.stdout, "F_PACK", component)
        except (RuntimeError, ValueError) as exc:
            messages.append(f"Fortran packed {component}: {exc}")
            failed = True
            continue
        expected = packed_owned_keys(component)
        if set(rows) != expected:
            messages.append(
                f"Fortran packed {component}: owned keys {len(rows)}/{len(expected)}"
            )
            failed = True
            continue
        messages.append(f"PASS Fortran packed {component}: cells={len(expected)} finite")
    return messages, failed


def compare_packed_cpp(fortran_exe: Path, cpp_binary: Path,
                       precision: str) -> tuple[list[str], bool]:
    messages: list[str] = []
    failed = False
    eps = 2.0**-23 if precision == "fp32" else 2.0**-52
    for component in ("U", "V", "W"):
        f_run = subprocess.run(
            [str(fortran_exe), "--dump-packed", component],
            text=True, capture_output=True,
        )
        c_run = subprocess.run(
            [str(cpp_binary), "--option1-momentum-packed", precision, component],
            text=True, capture_output=True,
        )
        if f_run.returncode or c_run.returncode:
            messages.append(
                f"packed parity {precision} {component}: "
                f"Fortran exit={f_run.returncode}, C++ exit={c_run.returncode}\n"
                f"Fortran stderr:\n{f_run.stderr}\nC++ stderr:\n{c_run.stderr}"
            )
            failed = True
            continue
        try:
            f_rows = parse_rows(f_run.stdout, "F_PACK", component)
            c_rows = parse_rows(c_run.stdout, "C_PACK", component)
        except (RuntimeError, ValueError) as exc:
            messages.append(f"packed parity {precision} {component}: {exc}")
            failed = True
            continue
        expected = packed_owned_keys(component)
        if set(f_rows) != expected or set(c_rows) != expected:
            messages.append(
                f"packed parity {precision} {component}: owned key mismatch "
                f"Fortran={len(f_rows)}/{len(expected)}, C++={len(c_rows)}/{len(expected)}"
            )
            failed = True
            continue
        signal = max(max(abs(value) for value in f_rows.values()), 1.0)
        error = max(abs(f_rows[key] - c_rows[key]) for key in expected)
        budget = 512.0 * eps * signal
        verdict = "PASS" if error <= budget else "FAIL"
        messages.append(
            f"{verdict} packed parity {precision} {component}: "
            f"cells={len(expected)} max_error={error:.9e} "
            f"budget={budget:.9e} signal={signal:.9e}"
        )
        failed |= error > budget
    return messages, failed


def compare_cpp(fortran_exe: Path, cpp_binary: Path, precision: str) -> tuple[list[str], bool]:
    messages: list[str] = []
    failed = False
    eps = 2.0**-23 if precision == "fp32" else 2.0**-52
    for component in ("U", "V", "W"):
        for case in (1, 2):
            f_run = subprocess.run(
                [str(fortran_exe), "--dump", component, str(case)],
                text=True, capture_output=True,
            )
            c_run = subprocess.run(
                [str(cpp_binary), "--option1-momentum-parity", precision,
                 component, str(case)],
                text=True, capture_output=True,
            )
            if f_run.returncode or c_run.returncode:
                messages.append(
                    f"parity {precision} {component} case={case}: "
                    f"Fortran exit={f_run.returncode}, C++ exit={c_run.returncode}\n"
                    f"Fortran stderr:\n{f_run.stderr}\nC++ stderr:\n{c_run.stderr}"
                )
                failed = True
                continue
            try:
                f_rows = parse_rows(f_run.stdout, "F_MOM", component)
                c_rows = parse_rows(c_run.stdout, "C_MOM", component)
            except (RuntimeError, ValueError) as exc:
                messages.append(f"parity {precision} {component} case={case}: {exc}")
                failed = True
                continue
            expected_keys = owned_keys(component)
            if set(f_rows) != expected_keys or set(c_rows) != expected_keys:
                messages.append(
                    f"parity {precision} {component} case={case}: owned-cell key mismatch "
                    f"Fortran={len(f_rows)}/{len(expected_keys)}, "
                    f"C++={len(c_rows)}/{len(expected_keys)}"
                )
                failed = True
                continue
            signal = max(max(abs(value) for value in f_rows.values()), 1.0)
            error = max(abs(f_rows[key] - c_rows[key]) for key in expected_keys)
            budget = 512.0 * eps * signal
            verdict = "PASS" if error <= budget else "FAIL"
            messages.append(
                f"{verdict} parity {precision} {component} case={case}: "
                f"cells={len(expected_keys)} max_error={error:.9e} "
                f"budget={budget:.9e} signal={signal:.9e}"
            )
            failed |= error > budget
    packed_messages, packed_failed = compare_packed_cpp(fortran_exe, cpp_binary, precision)
    messages.extend(packed_messages)
    failed |= packed_failed
    return messages, failed


def compile_and_run(compiler: list[str], optimization: str,
                    real64: bool, cpp_binary: Path | None = None
                    ) -> subprocess.CompletedProcess[str]:
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
        result = subprocess.run([str(executable)], cwd=work,
                                text=True, capture_output=True)
        if result.returncode:
            return result
        packed_messages, packed_failed = run_fortran_packed(executable)
        parity_messages: list[str] = []
        parity_failed = False
        if cpp_binary is not None and optimization == "-O2":
            parity_messages, parity_failed = compare_cpp(
                executable, cpp_binary, "fp64" if real64 else "fp32"
            )
        all_messages = packed_messages + parity_messages
        output = result.stdout + "\n" + "\n".join(all_messages) + "\n"
        return subprocess.CompletedProcess(
            result.args, 1 if packed_failed or parity_failed else 0, output, result.stderr
        )


def main() -> int:
    if len(sys.argv) > 2:
        print(f"usage: {Path(sys.argv[0]).name} [CPP_BINARY]", file=sys.stderr)
        return 2
    cpp_binary: Path | None = None
    if len(sys.argv) == 2:
        cpp_binary = Path(sys.argv[1]).expanduser().resolve()
        if not cpp_binary.is_file() or not os.access(cpp_binary, os.X_OK):
            print(f"C++ contract binary is not executable: {cpp_binary}", file=sys.stderr)
            return 2
    compiler = shlex.split(os.environ.get("FC", "gfortran"))
    if not compiler or shutil.which(compiler[0]) is None and not Path(compiler[0]).exists():
        print(f"Fortran compiler not found: {compiler!r}", file=sys.stderr)
        return 2
    failed = False
    for precision, real64 in (("REAL32", False), ("REAL64", True)):
        for optimization in ("-O0", "-O2"):
            result = compile_and_run(compiler, optimization, real64, cpp_binary)
            print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
            print(f"{precision} {optimization}: exit={result.returncode}")
            failed |= result.returncode != 0
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
