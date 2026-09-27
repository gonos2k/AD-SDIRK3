#!/usr/bin/env python3
"""Source-extracted references for option-2 U-X and W terrain momentum.

The full path extracts metrics, deformation, stress, and U-diffusion bodies
verbatim from module_diffusion_em.F. The retained operator-only path injects
source-checked Python D11 and analytic metrics to preserve the original oracle.
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
# The Python equation oracle retains its selected interior level; the compiled
# source/C++ contract below compares every owned mass level.
PARITY_K = 1
DX, DZ, GRAVITY, KH, KV, RHO, MAP = 1000.0, 1000.0, 9.81, 2.0/3.0, 4.0/3.0, 1.0, 1.25


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def check_source(repo: Path) -> str:
    source_path = repo / "dyn_em/module_diffusion_em.F"
    source = source_path.read_text()
    metrics = section(source, "SUBROUTINE compute_diff_metrics", "END SUBROUTINE compute_diff_metrics")
    strain = section(source, "SUBROUTINE cal_deform_and_div", "END SUBROUTINE cal_deform_and_div")
    stress = section(source, "SUBROUTINE cal_titau_11_22_33", "END SUBROUTINE cal_titau_11_22_33")
    vertical_stress = section(source, "SUBROUTINE cal_titau_13_31", "END SUBROUTINE cal_titau_13_31")
    vertical = section(source, "SUBROUTINE vertical_diffusion_u_2", "END SUBROUTINE vertical_diffusion_u_2")
    diffusion = section(source, "SUBROUTINE horizontal_diffusion_u_2", "END SUBROUTINE horizontal_diffusion_u_2")
    required = [
        (metrics, "z_at_w(i,k,j) = ( ph(i,k,j) + phb(i,k,j) ) / g"),
        (metrics, "zx(i,k,j) = zx(i,k,j) + rdx * ( ph(i,k,j) - ph(i-1,k,j) ) / g"),
        (strain, "tmpzx       = 0.25 * ("),
        (strain, "tmp1(i,k,j) = ( hatavg(i,k+1,j) - hatavg(i,k,j) ) *tmpzx * rdzw(i,k,j)"),
        (stress, "titau(i,k,j) = - rho(i,k,j) * xkx(i,k,j) * defor(i,k,j)"),
        (vertical_stress, "titau(i,k,j) = - xkxavg(i,k,j) * defor(i,k,j)"),
        (vertical, "tendency(i,k,j)=tendency(i,k,j)-rdzu*(titau3(i,k+1,j)-titau3(i,k,j))"),
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
    for routine in ("compute_diff_metrics", "cal_deform_and_div", "vertical_diffusion_2", "horizontal_diffusion_2"):
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

    # Same U state in the C++ and Fortran fixtures: a single interior vertical
    # pulse with a weak x mode keeps both horizontal and vertical U diffusion
    # active at selected mass level k=1.
    u = [[(1.0 + 0.1 * math.cos(2.0 * math.pi * i / NX)) if k == 1 else 0.0
         for i in range(NX + 1)] for k in range(NZ)]
    for k in range(NZ):
        u[k][NX] = u[k][0]
    hatavg = [0.0] * (NZ + 1)
    hatavg[0] = [0.5 * (u[0][i] + u[0][i + 1]) / MAP for i in range(NX)]
    for q in range(1, NZ):
        hatavg[q] = [0.25 * (u[q][i] + u[q][i + 1] + u[q - 1][i] + u[q - 1][i + 1]) / MAP
                     for i in range(NX)]
    hatavg[NZ] = [0.5 * (u[NZ - 1][i] + u[NZ - 1][i + 1]) / MAP for i in range(NX)]

    d11 = [[0.0] * NX for _ in range(NZ)]
    for k in range(NZ):
        for i in range(NX):
            tmpzx = 0.25 * (zx[k][i] + zx[k][(i + 1) % NX] +
                            zx[k + 1][i] + zx[k + 1][(i + 1) % NX])
            i_next = (i + 1) % NX
            du_dx = (u[k][i_next] - u[k][i]) / MAP / DX
            dpsi_dx = (hatavg[k + 1][i] - hatavg[k][i]) * tmpzx * rdzw[k][i]
            d11[k][i] = 2.0 * MAP**2 * (du_dx - dpsi_dx)

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
            bracket = (MAP * (tau[k][i] - tau[k][im1]) / DX -
                       MAP * zx_at_u * (tauavg[k + 1][face % NX] -
                                  tauavg[k][face % NX]) / tmpdz)
            # TileCase supplies positive rdnw=NZ=4; WRF's signed dnw is
            # therefore -1/rdnw=-1/4. Keep the full Fortran g*tmpdz/dnw scale.
            expected[(j, k, face)] = GRAVITY * tmpdz / (-1.0 / NZ) * bracket
    return expected


def compiled_fortran_oracle(repo: Path, compiler: str, flags: list[str],
                            work: Path, profile: str="interior-pulse") -> tuple[dict[str, dict[tuple[int, int, int], float]], str]:
    """Run both operator-only and full source-extracted Fortran U oracles."""
    source = (repo / "dyn_em/module_diffusion_em.F").read_text()
    routine_names = ("compute_diff_metrics", "cal_deform_and_div",
                     "cal_titau_11_22_33", "cal_titau_12_21",
                     "cal_titau_13_31", "cal_titau_23_32",
                     "vertical_diffusion_u_2", "horizontal_diffusion_u_2",
                     "vertical_diffusion_w_2", "horizontal_diffusion_w_2")
    routines = []
    for name in routine_names:
        end = f"END SUBROUTINE {name}"
        begin_at = source.index(f"SUBROUTINE {name}")
        end_at = source.index(end, begin_at) + len(end)
        routines.append(source[begin_at:end_at])
    em_source = (repo / "dyn_em/module_em.F").read_text()
    rk_addtend = section(em_source, "SUBROUTINE rk_addtend_dry", "END SUBROUTINE rk_addtend_dry")
    rk_addtend += "END SUBROUTINE rk_addtend_dry"
    routines.append(rk_addtend)
    routine_hash = hashlib.sha256("\n".join(routines).encode()).hexdigest()
    if profile not in {"interior-pulse", "top-pulse", "w-composite"}:
        raise ValueError(f"unknown fixture profile: {profile}")
    top_pulse = 1 if profile == "top-pulse" else 0
    _, _, _, d11_expected, _, _ = source_grounded_fields()
    src = f"""module extracted_wrf_diffusion
  implicit none
  real, parameter :: g=9.81
  integer, parameter :: P_m11=1, P_m12=2, P_m13=3, P_m23=4, P_m33=5
  integer, parameter :: P_r12=1, P_r13=2, P_r23=3
  type :: grid_config_rec_type
    logical :: open_xs=.false., open_xe=.false., open_ys=.false., open_ye=.false.
    logical :: specified=.false., nested=.false., periodic_x=.true., periodic_y=.false.
    logical :: polar=.false., mix_full_fields=.true.
    integer :: sfs_opt=0, m_opt=0
  end type
contains
{routines[0]}
{routines[1]}
{routines[2]}
{routines[3]}
{routines[4]}
{routines[5]}
{routines[6]}
{routines[7]}
{routines[8]}
{routines[9]}
{routines[10]}
end module extracted_wrf_diffusion

program oracle_driver
  use extracted_wrf_diffusion
  implicit none
  integer, parameter :: nx={NX}, ny={NY}, nz={NZ}
  integer, parameter :: ids=1, ide=nx+1, jds=1, jde=ny+1, kds=1, kde=nz+1
  integer, parameter :: ims=0, ime=nx+2, jms=0, jme=ny+2, kms=1, kme=nz+1
  integer, parameter :: itsm=1, item=nx+1, itsd=1, ited=nx, itsu=2, iteu=nx
  integer, parameter :: jts=2, jte=ny, kts=1, kte=nz+1
  integer :: i,j,k,ii
  type(grid_config_rec_type) :: cfg
  real :: rdx,rdy,terrain,pi,cf1,cf2,cf3
  real :: ph(ims:ime,kms:kme,jms:jme), phb(ims:ime,kms:kme,jms:jme)
  real :: z(ims:ime,kms:kme,jms:jme), rdz(ims:ime,kms:kme,jms:jme)
  real :: tendency(ims:ime,kms:kme,jms:jme), defor11(ims:ime,kms:kme,jms:jme)
  real :: defor22(ims:ime,kms:kme,jms:jme), defor33(ims:ime,kms:kme,jms:jme)
  real :: defor12(ims:ime,kms:kme,jms:jme), defor13(ims:ime,kms:kme,jms:jme)
  real :: defor23(ims:ime,kms:kme,jms:jme), div(ims:ime,kms:kme,jms:jme)
  real :: u(ims:ime,kms:kme,jms:jme), v(ims:ime,kms:kme,jms:jme)
  real :: w(ims:ime,kms:kme,jms:jme), tke(ims:ime,kms:kme,jms:jme)
  real :: xkmh(ims:ime,kms:kme,jms:jme), xkmv(ims:ime,kms:kme,jms:jme)
  real :: rho(ims:ime,kms:kme,jms:jme)
  real :: tendency_v(ims:ime,kms:kme,jms:jme), ru_tend(ims:ime,kms:kme,jms:jme)
  real :: rv_tend(ims:ime,kms:kme,jms:jme), rw_tend(ims:ime,kms:kme,jms:jme)
  real :: ph_tend(ims:ime,kms:kme,jms:jme), t_tend(ims:ime,kms:kme,jms:jme)
  real :: ru_tendf(ims:ime,kms:kme,jms:jme), rv_tendf(ims:ime,kms:kme,jms:jme)
  real :: rw_tendf(ims:ime,kms:kme,jms:jme), ph_tendf(ims:ime,kms:kme,jms:jme)
  real :: t_tendf(ims:ime,kms:kme,jms:jme), u_save(ims:ime,kms:kme,jms:jme)
  real :: v_save(ims:ime,kms:kme,jms:jme), w_save(ims:ime,kms:kme,jms:jme)
  real :: ph_save(ims:ime,kms:kme,jms:jme), t_save(ims:ime,kms:kme,jms:jme)
  real :: mu_tend(ims:ime,jms:jme), mu_tendf(ims:ime,jms:jme)
  real :: h_diabatic(ims:ime,kms:kme,jms:jme), mut(ims:ime,jms:jme)
  real :: msfvx_inv(ims:ime,jms:jme)
  real :: zx(ims:ime,kms:kme,jms:jme), zy(ims:ime,kms:kme,jms:jme)
  real :: rdzw(ims:ime,kms:kme,jms:jme)
  real :: msfux(ims:ime,jms:jme), msfuy(ims:ime,jms:jme)
  real :: msfvx(ims:ime,jms:jme), msfvy(ims:ime,jms:jme)
  real :: msftx(ims:ime,jms:jme), msfty(ims:ime,jms:jme)
  real :: fnm(kms:kme), fnp(kms:kme), dn(kms:kme), dnw(kms:kme)
  real :: u_base(kms:kme), v_base(kms:kme)
  real :: nba_rij(ims:ime,kms:kme,jms:jme,3)
  real :: nba_mij(ims:ime,kms:kme,jms:jme,3)
  tendency=0.; tendency_v=0.; defor11=0.; defor22=0.; defor33=0.; defor12=0.
  defor13=0.; defor23=0.; div=0.; u=0.; v=0.; w=0.; tke=0.
  xkmh={KH:.17g}; xkmv={KV:.17g}
  ru_tend=0.; rv_tend=0.; rw_tend=0.; ph_tend=0.; t_tend=0.
  ru_tendf=0.; rv_tendf=0.; rw_tendf=0.; ph_tendf=0.; t_tendf=0.
  u_save=0.; v_save=0.; w_save=0.; ph_save=0.; t_save=0.
  mu_tend=0.; mu_tendf=0.; h_diabatic=0.; mut=1.; msfvx_inv=1./{MAP:.17g}
  ph=0.; phb=0.; z=0.; rdz=0.; rho=1.; zx=0.; zy=0.; rdzw=0.
  nba_rij=0.; nba_mij=0.
  msfux={MAP:.17g}; msfuy={MAP:.17g}; msfvx={MAP:.17g}; msfvy={MAP:.17g}
  msftx={MAP:.17g}; msfty={MAP:.17g}
  fnm=0.5; fnp=0.5; dn=-0.25; dnw=-0.25; u_base=0.; v_base=0.
  cf1=1.; cf2=0.; cf3=0.; pi=acos(-1.)
  rdx=1./{DX:.17g}; rdy=rdx
  do j=jms,jme
    do k=kms,kme
      do i=ims,ime
        ii=modulo(i-1,nx)
        terrain=100.*cos(2.*pi*real(ii)/real(nx))
        ph(i,k,j)=g*(terrain+{DZ:.17g}*real(k-1))
      enddo
    enddo
  enddo
  ! Stage PH is Y-constant; i=0/9 are the periodic copies of i=8/1.
  ! Exact metrics run at the east endpoint so they write zx(ide).
  call compute_diff_metrics(cfg,ph,phb,z,rdz,rdzw,zx,zy,rdx,rdy, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
       itsm,item,jts,jte,kts,kte)
  ! Prepare periodic X and symmetric-Y metric halos analytically. This is not
  ! an execution or validation of WRF set_physical_bc3d.
  do j=jms,jts-2
    rdzw(:,:,j)=rdzw(:,:,jts-1); rdz(:,:,j)=rdz(:,:,jts-1)
    zx(:,:,j)=zx(:,:,jts); zy(:,:,j)=zy(:,:,jts)
  enddo
  do j=jte+1,jme
    rdzw(:,:,j)=rdzw(:,:,jte); rdz(:,:,j)=rdz(:,:,jte)
    zx(:,:,j)=zx(:,:,jte); zy(:,:,j)=zy(:,:,jte)
  enddo
  do j=jms,jme
    do k=kms,kme
      rdzw(0,k,j)=rdzw(nx,k,j); rdzw(nx+1,k,j)=rdzw(1,k,j)
      rdzw(nx+2,k,j)=rdzw(2,k,j)
      rdz(0,k,j)=rdz(nx,k,j); rdz(nx+1,k,j)=rdz(1,k,j)
      rdz(nx+2,k,j)=rdz(2,k,j)
      zx(0,k,j)=zx(nx,k,j); zx(nx+1,k,j)=zx(1,k,j)
      zx(nx+2,k,j)=zx(2,k,j)
    enddo
  enddo
  do j=jts,jte
    do k=kts,kte
      do i=1,nx+1
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'M_ZX',j-1,k-1,i-1,zx(i,k,j)
      enddo
      if (k<=nz) then
        do i=1,nx
          write(*,'(A,3(1X,I0),1X,ES25.16)') 'M_RDZW',j-1,k-1,i-1,rdzw(i,k,j)
        enddo
      endif
    enddo
  enddo
  if ({top_pulse} == 1) then
    u(:,nz,:)=1.
  else
    do j=jms,jme
      do i=1,nx+1
        u(i,2,j)=1.0+0.1*cos(2.*pi*real(modulo(i-1,nx))/real(nx))
      enddo
    enddo
  endif
  if ({1 if profile == "w-composite" else 0} == 1) then
    do j=jms,jme
      do i=ims,ime
        w(i,3,j)=0.2*cos(2.*pi*real(modulo(i-1,nx))/real(nx))
      enddo
    enddo
  endif
  ! Run actual deformation over j=2..6 so the consumer's north cross-stress
  ! row j=6 is produced by the same source routine, not forcibly overwritten.
  call cal_deform_and_div(cfg,u,v,w,div,defor11,defor22,defor33, &
       defor12,defor13,defor23,nba_rij,3,u_base,v_base, &
       msfux,msfuy,msfvx,msfvy,msftx,msfty,rdx,rdy,dn,dnw,rdz,rdzw, &
       fnm,fnp,cf1,cf2,cf3,zx,zy,ids,ide,jds,jde,kds,kde, &
       ims,ime,jms,jme,kms,kme,itsd,ited,jts,jte,kts,kte)
  do j=jts,jte
    do k=1,nz-1
      do i=1,nx
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'D12_RAW',j-1,k-1,i-1,defor12(i,k,j)
      enddo
    enddo
  enddo
  do j=jts,jte
    do k=1,nz
      do i=1,nx
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'D11_RAW',j-1,k-1,i-1,defor11(i,k,j)
      enddo
    enddo
  enddo
  do j=jts,jte
    do k=1,nz+1
      do i=1,nx+1
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'D13_RAW',j-1,k-1,i-1,defor13(i,k,j)
      enddo
    enddo
  enddo
  do j=jts,jte
    do k=1,nz+1
      do i=1,nx
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'D23_RAW',j-1,k-1,i-1,defor23(i,k,j)
      enddo
    enddo
  enddo
  do j=jts,jte
    do k=1,nz
      do i=1,nx
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'D33_RAW',j-1,k-1,i-1,defor33(i,k,j)
      enddo
    enddo
  enddo
  call vertical_diffusion_u_2(tendency_v,cfg,defor13,xkmv, &
       nba_mij(ims,kms,jms,1),2,dnw,rdzw,fnm,fnp,rho, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,itsu,iteu,jts,jte,kts,kte)
  do j=jts,jte-1
    do k=kts,kte-1
      do i=itsu,iteu
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'V_RAW',j-1,k-1,i-1,tendency_v(i,k,j)
      enddo
    enddo
  enddo
  call horizontal_diffusion_u_2(tendency,cfg,defor11,defor12,div, &
       nba_mij(ims,kms,jms,1),2,tke,msfux,msfuy,xkmh,rdx,rdy,fnm,fnp, &
       dnw,zx,zy,rdzw,rho,ids,ide,jds,jde,kds,kde, &
       ims,ime,jms,jme,kms,kme,itsu,iteu,jts,jte,kts,kte)
  do j=jts,ny-1
    do k=kts,kte-1
      do i=itsu-1,iteu+1
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'F_RAW',j-1,k-1,i-1,tendency(i,k,j)
      enddo
    enddo
  enddo
  ru_tendf=tendency+tendency_v
  ! W composite uses horizontal xkmv and vertical xkmh, then dry-RK scales
  ! the raw sum by msfty at the final W momentum location.
  tendency=0.; tendency_v=0.
  call horizontal_diffusion_w_2(tendency,cfg,defor13,defor23,div, &
       nba_mij(ims,kms,jms,1),2,tke,msftx,msfty,xkmv,rdx,rdy,fnm,fnp, &
       dn,zx,zy,rdz,rho,ids,ide,jds,jde,kds,kde, &
       ims,ime,jms,jme,kms,kme,itsd,ited,jts,jte,kts,kte)
  do j=jts,jte
    do k=kts+1,kte-1
      do i=itsm,iteu-1
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'HW_RAW',j-1,k-1,i-1,tendency(i,k,j)
      enddo
    enddo
  enddo
  call vertical_diffusion_w_2(tendency_v,cfg,defor33,tke, &
       nba_mij(ims,kms,jms,1),2,div,xkmh,dn,rdz,fnm,fnp,rho, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
       itsm,iteu-1,jts,jte,kts,kte)
  do j=jts,jte
    do k=kts+1,kte-1
      do i=itsm,iteu-1
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'VW_RAW',j-1,k-1,i-1,tendency_v(i,k,j)
      enddo
    enddo
  enddo
  rw_tendf=tendency+tendency_v
  call rk_addtend_dry(ru_tend,rv_tend,rw_tend,ph_tend,t_tend, &
       ru_tendf,rv_tendf,rw_tendf,ph_tendf,t_tendf, &
       u_save,v_save,w_save,ph_save,t_save,mu_tend,mu_tendf,2,fnm,fnm, &
       h_diabatic,mut,msftx,msfty,msfux,msfuy,msfvx,msfvx_inv,msfvy, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme, &
       ids,ide,jds,jde,kds,kde,itsu,iteu,jts,jte,kts,kte)
  do j=jts,jte-1
    do k=kts,kte-1
      do i=itsu,iteu
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'RHS_U',j-1,k-1,i-1,ru_tend(i,k,j)
      enddo
    enddo
  enddo
  do j=jts,jte
    do k=kts+1,kte-1
      do i=itsm,iteu-1
        write(*,'(A,3(1X,I0),1X,ES25.16)') 'RHS_W',j-1,k-1,i-1,rw_tend(i,k,j)
      enddo
    enddo
  enddo
  ! Operator-only control: the old source-grounded Python D11 and ideal metric
  ! inputs feed exact stress/U2 routines independently of the producer chain.
  tendency=0.; defor11=0.; defor12=0.; div=0.; zx=0.
  rdzw=1./{DZ:.17g}
  do j=jms,jme
    do k=kms,kme
      do i=1,nx+1
        zx(i,k,j)=({100.0:.17g})*cos(2.*pi*real(modulo(i-1,nx))/real(nx)) - &
                   ({100.0:.17g})*cos(2.*pi*real(modulo(i-2,nx))/real(nx))
        zx(i,k,j)=zx(i,k,j)*rdx
      enddo
    enddo
  enddo
  zx(0,:,:)=zx(nx,:,:); zx(nx+2,:,:)=zx(2,:,:)
"""
    for k in range(NZ):
        for i in range(NX):
            for j in range(NY):
                src += (f"  defor11({i + 1},{k + 1},{j + 1})="
                        f"{d11_expected[k][i]:.17g}\n")
    src += f"""
  call horizontal_diffusion_u_2(tendency,cfg,defor11,defor12,div, &
       nba_mij(ims,kms,jms,1),2,tke,msfux,msfuy,xkmh,rdx,rdy,fnm,fnp, &
       dnw,zx,zy,rdzw,rho,ids,ide,jds,jde,kds,kde, &
       ims,ime,jms,jme,kms,kme,itsu,iteu,jts,jte,kts,kte)
  do j=jts,ny-1
    do i=itsu-1,iteu+1
      write(*,'(A,3(1X,I0),1X,ES25.16)') 'O_RAW',j-1,2-1,i-1,tendency(i,2,j)
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
                   *link_flags, str(f90), "-o", str(exe)]
    built = subprocess.run(compile_cmd, text=True, capture_output=True)
    if built.returncode:
        raise RuntimeError("Fortran oracle compile failed:\n" + built.stderr)
    run = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
    values: dict[str, dict[tuple[int, int, int], float]] = {}
    labels = {"M_ZX", "M_RDZW", "D11_RAW", "D12_RAW", "D13_RAW", "D23_RAW", "D33_RAW",
              "V_RAW", "RHS_U", "F_RAW", "O_RAW", "HW_RAW", "VW_RAW", "RHS_W"}
    for line in run.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] in labels:
            label, j, k, i, value = fields
            values.setdefault(label, {})[(int(j), int(k), int(i))] = float(value)
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
    actual: dict[tuple[int, int, int], float] = {}
    vertical_actual: dict[tuple[int, int, int], float] = {}
    coeff_wrong_vertical: dict[tuple[int, int, int], float] = {}
    explicit_geometry: bool | None = None
    cpp_metadata: dict[str, dict[str,float]] = {}
    def parse_cpp(binary: Path, mode: str):
        output = subprocess.run([str(binary), mode], check=True, text=True,
                                capture_output=True).stdout
        parsed: dict[str, dict[tuple[int, int, int], float]] = {}
        for line in output.splitlines():
            fields = line.split()
            if fields[0] == "GEOMETRY_INPUT":
                nonlocal explicit_geometry
                explicit_geometry = fields[1].endswith("stage_explicit=1")
            elif fields[0] == "OPTION2_K":
                meta = dict(field.split("=",1) for field in fields[1:])
                if "top-pulse" in mode:
                    cpp_metadata.setdefault(mode,{}).update(
                        {key:float(value) for key,value in meta.items()
                         if value.replace(".","",1).replace("-","",1).isdigit()})
                    print("CPP " + mode.rsplit("--",1)[-1] + " coefficients " +
                          " ".join(fields[1:]))
            elif fields[0] in {"U_RAW", "UV_RAW", "D11_CPP", "D13_CPP", "D23_CPP",
                               "D33_CPP", "WH_RAW", "WV_RAW", "WH_KH_WRONG",
                               "WV_KV_WRONG", "D13_OLDZX", "WH_OLDZX",
                               "M_ZX_CPP", "M_RDZW_CPP"}:
                label, j, k, i, value = fields
                parsed.setdefault(label, {})[(int(j), int(k), int(i))] = float(value)
        return parsed
    cpp_outputs = parse_cpp(args.binary, "--option2-momentum-stage-geometry")
    actual = cpp_outputs["U_RAW"]
    vertical_actual = cpp_outputs["UV_RAW"]
    wrong_cpp_outputs = parse_cpp(args.binary, "--option2-momentum-stage-geometry-wrong-k")
    coeff_wrong_vertical = wrong_cpp_outputs["UV_RAW"]
    wrong_metric_outputs = parse_cpp(args.binary, "--option2-momentum-stage-geometry-wrong-metric")
    metric_wrong_horizontal = wrong_metric_outputs["U_RAW"]
    top_pulse_cpp = parse_cpp(args.binary, "--option2-momentum-stage-geometry-top-pulse")
    expected = fortran_oracle()
    _, zx_expected, rdzw_expected, d11_expected, _, _ = source_grounded_fields()
    fortran_results: list[tuple[str, dict[str, dict[tuple[int, int, int], float]], str]] = []
    top_pulse_results: list[tuple[str, dict[str, dict[tuple[int, int, int], float]], str]] = []
    w_composite_results: list[tuple[str, dict[str, dict[tuple[int, int, int], float]], str]] = []
    with tempfile.TemporaryDirectory(prefix="sdirk3-option2-fortran-") as temp_dir:
        temp = Path(temp_dir)
    for label, flags in (("fp32-O0", ["-O0"]), ("fp32-O2", ["-O2"]),
                         ("real64-O0", ["-O0", "-fdefault-real-8"]),
                         ("real64-O2", ["-O2", "-fdefault-real-8"])):
        result, extracted_sha = compiled_fortran_oracle(
            repo, args.fortran_compiler, flags, temp / label)
        fortran_results.append((label, result, extracted_sha))
        top_result, top_routine_sha = compiled_fortran_oracle(
            repo, args.fortran_compiler, flags, temp / f"{label}-top-pulse",
            profile="top-pulse")
        w_result, w_routine_sha = compiled_fortran_oracle(
            repo, args.fortran_compiler, flags, temp / f"{label}-w-composite",
            profile="w-composite")
        if top_routine_sha != extracted_sha:
            raise RuntimeError("top-pulse diagnostic used different extracted routines")
        if w_routine_sha != extracted_sha:
            raise RuntimeError("W composite used different extracted routines")
        top_pulse_results.append((label,top_result,top_routine_sha))
        w_composite_results.append((label,w_result,w_routine_sha))
    cpp_seams = {(j, k, i) for j in range(NY) for k in range(NZ)
                 for i in (0, NX)}
    fortran_seams = {(j, 1, i) for j in range(1, NY - 1)
                     for i in (0, NX)}
    metric_zx_keys = {(j, k, i) for j in range(1, NY)
                      for k in range(NZ + 1) for i in range(NX + 1)}
    metric_rdzw_keys = {(j, k, i) for j in range(1, NY)
                        for k in range(NZ) for i in range(NX)}
    d11_keys = {(j, k, i) for j in range(1, NY)
                for k in range(NZ) for i in range(NX)}
    missing_cpp_seams = sorted(cpp_seams - actual.keys())
    if missing_cpp_seams:
        print(f"FAIL missing C++ seam outputs: {missing_cpp_seams[:3]}", file=sys.stderr)
        return 1
    for label, result, _ in fortran_results:
        for output in ("F_RAW", "O_RAW"):
            values = result.get(output, {})
            missing = sorted(set(expected) - values.keys())
            if missing:
                print(f"FAIL missing {label} {output} owned outputs: {missing[:3]}",
                      file=sys.stderr)
                return 1
            missing_seams = sorted(fortran_seams - values.keys())
            if missing_seams:
                print(f"FAIL missing {label} {output} seam outputs: {missing_seams[:3]}",
                      file=sys.stderr)
                return 1
        for output, required in (("M_ZX", metric_zx_keys),
                                 ("M_RDZW", metric_rdzw_keys),
                                 ("D11_RAW", d11_keys),
                                 ("D12_RAW", {(j,k,i) for j,k,i in d11_keys if k < NZ-1})):
            missing = sorted(required - result.get(output, {}).keys())
            if missing:
                print(f"FAIL missing {label} {output} producer outputs: {missing[:3]}",
                      file=sys.stderr)
                return 1
        u_keys = {(j, k, i) for j in range(1, NY - 1)
                  for k in range(NZ) for i in range(1, NX)}
        for output in ("V_RAW", "RHS_U"):
            missing = sorted(u_keys - result.get(output, {}).keys())
            if missing:
                print(f"FAIL missing {label} {output} owned outputs: {missing[:3]}",
                      file=sys.stderr)
                return 1
    outputs = [("C++", actual), ("equation oracle", expected)]
    for label, result, _ in fortran_results:
        outputs.extend((f"{label}/{name}", values) for name, values in result.items())
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
    extracted_sha = fortran_results[0][2]
    if any(routine_sha != extracted_sha for _, _, routine_sha in fortran_results):
        raise RuntimeError("the extracted Fortran routine source changed between builds")
    print(f"EXTRACTED_ROUTINES sha256={extracted_sha}")
    zx_scale = max(abs(value) for row in zx_expected for value in row)
    rdzw_scale = 1.0 / DZ
    d11_scale = 2.0 * 0.5 * zx_scale / DZ * MAP
    for label, result, _ in fortran_results:
        eps = (2.0 ** -23) if label.startswith("fp32") else (2.0 ** -52)
        # Fixed before observing producer errors: 32 eps for FP32 and 256 eps
        # for REAL64, scaled by the fixture's terrain/metric/deformation units.
        factor = 32.0 if label.startswith("fp32") else 256.0
        budgets = {"M_ZX": factor * eps * max(zx_scale, 1e-12),
                   "M_RDZW": factor * eps * rdzw_scale,
                   "D11_RAW": factor * eps * d11_scale,
                   "D12_RAW": factor * eps * d11_scale}
        metric_errors = {
            "M_ZX": max(abs(value - zx_expected[k][i % NX])
                        for (j, k, i), value in result["M_ZX"].items()),
            "M_RDZW": max(abs(value - rdzw_expected[k][i])
                          for (j, k, i), value in result["M_RDZW"].items()),
            "D11_RAW": max(abs(value - d11_expected[k][i])
                           for (j, k, i), value in result["D11_RAW"].items()),
            "D12_RAW": max(abs(value) for value in result["D12_RAW"].values()),
        }
        for name, error in metric_errors.items():
            print(f"FORTRAN {label} {name} max_abs_error={error:.9g} "
                  f"predeclared_budget={budgets[name]:.3g}")
            if error > budgets[name]:
                compiled_mismatches.append((label, name, error, budgets[name]))
        for name in ("O_RAW", "F_RAW"):
            error = max(abs(result[name][key] - expected[key]) for key in expected)
            print(f"FORTRAN {label} {name} vs Python operator oracle "
                  f"max_error={error:.9g} tolerance={tolerance:.3g}")
            if error > tolerance:
                key = max(expected, key=lambda k: abs(result[name][k] - expected[k]))
                compiled_mismatches.append((label, name, key, error))
        u_keys = {(j, k, i) for j in range(1, NY - 1)
                  for k in range(NZ) for i in range(1, NX)}
        v_error = max(abs(result["V_RAW"][key] - vertical_actual[key]) for key in u_keys)
        v_signal = max(abs(result["V_RAW"][key]) for key in u_keys)
        v_tolerance = 2e-6 * max(1e-12, v_signal)
        rhs_error = max(abs(result["RHS_U"][key] -
                            (result["F_RAW"].get(key, 0.0) + result["V_RAW"][key]) / MAP)
                        for key in u_keys)
        rhs_signal = max(abs(result["RHS_U"][key]) for key in u_keys)
        rhs_tolerance = 2e-6 * max(1e-12, rhs_signal)
        cpp_raw_error = max(abs((actual[key] + vertical_actual[key]) -
                                (result["F_RAW"][key] + result["V_RAW"][key]))
                            for key in u_keys)
        cpp_raw_signal = max(abs(result["F_RAW"][key] + result["V_RAW"][key])
                             for key in u_keys)
        cpp_raw_tolerance = 2e-6 * max(1e-12, cpp_raw_signal)
        cpp_rhs_error = max(abs((actual[key] + vertical_actual[key]) / MAP -
                                result["RHS_U"][key]) for key in u_keys)
        cpp_rhs_signal = rhs_signal
        cpp_rhs_tolerance = 2e-6 * max(1e-12, cpp_rhs_signal)
        wrong_k_error = max(abs(coeff_wrong_vertical[key] - result["V_RAW"][key])
                            for key in u_keys)
        metric_wrong_error = max(abs(metric_wrong_horizontal[key] - result["F_RAW"][key])
                                 for key in u_keys)
        horizontal_signal = max(abs(result["F_RAW"][key]) for key in u_keys)
        horizontal_tolerance = 2e-6 * max(1e-12, horizontal_signal)
        print(f"CPP vs {label} owned U vertical max_error={v_error:.9g} "
              f"tolerance={v_tolerance:.3g}; composite raw U signal={cpp_raw_signal:.9g} "
              f"max_error={cpp_raw_error:.9g} tolerance={cpp_raw_tolerance:.3g}; "
              f"post-rk_addtend U signal={cpp_rhs_signal:.9g} max_error={cpp_rhs_error:.9g} "
              f"tolerance={cpp_rhs_tolerance:.3g}; Fortran normalization self-check="
              f"{rhs_error:.9g}; wrong-k signal={wrong_k_error:.9g}; "
              f"wrong-stage-metric signal={metric_wrong_error:.9g} "
              f"tolerance={horizontal_tolerance:.3g}")
        if (v_error > v_tolerance or cpp_raw_error > cpp_raw_tolerance or
                cpp_rhs_error > cpp_rhs_tolerance or rhs_error > rhs_tolerance or
                wrong_k_error <= v_tolerance or metric_wrong_error <= horizontal_tolerance):
            compiled_mismatches.append((label, "vertical/normalized/wrong-k",
                                        v_error, cpp_raw_error, cpp_rhs_error,
                                        rhs_error, wrong_k_error, metric_wrong_error))
        raw_cpp_error = max(abs(result["F_RAW"][key] - actual[key])
                            for key in expected)
        op_cpp_error = max(abs(result["O_RAW"][key] - actual[key])
                           for key in expected)
        raw_seam_error = max(abs(result["F_RAW"][key]) for key in fortran_seams)
        op_seam_error = max(abs(result["O_RAW"][key]) for key in fortran_seams)
        print(f"CPP vs {label} full U max_error={raw_cpp_error:.9g}; "
              f"operator-only max_error={op_cpp_error:.9g}; "
              f"full/operator seam={raw_seam_error:.9g}/{op_seam_error:.9g}")
        if raw_cpp_error > tolerance or op_cpp_error > tolerance:
            compiled_mismatches.append((label, "cpp-raw", raw_cpp_error, tolerance))
        if raw_seam_error != 0.0 or op_seam_error != 0.0:
            compiled_mismatches.append((label, "seam", raw_seam_error, 0.0))
    w_cpp = parse_cpp(args.binary, "--option2-w-momentum-stage-geometry")
    # W has mass-grid X length `nx`; compared cells exclude physical tile edges.
    # Periodic source/cpp seam aliases are checked separately below.
    w_keys = {(j, k, i) for j in range(1, NY - 1)
              for k in range(1, NZ) for i in range(1, NX - 1)}
    for label, result, _ in w_composite_results:
        for name in ("M_ZX", "M_RDZW", "D13_RAW", "D23_RAW", "D33_RAW",
                     "HW_RAW", "VW_RAW", "RHS_W"):
            if not all(math.isfinite(value) for value in result[name].values()):
                compiled_mismatches.append((label,name,"non-finite"))
        for cpp_label, fort_label, keys, name in (
            ("M_ZX_CPP", "M_ZX", {(j,k,i) for j in range(1,NY-1)
                                   for k in range(NZ+1) for i in range(1,NX+1)}, "M_ZX"),
            ("M_RDZW_CPP", "M_RDZW", {(j,k,i) for j in range(1,NY-1)
                                       for k in range(NZ) for i in range(1,NX-1)}, "M_RDZW"),
            ("D13_CPP", "D13_RAW", {(j,k,i) for j in range(1,NY-1)
                                     for k in range(1,NZ) for i in range(1,NX-1)}, "D13"),
            ("D23_CPP", "D23_RAW", {(j,k,i) for j in range(1,NY-1)
                                     for k in range(1,NZ) for i in range(1,NX-1)}, "D23"),
            ("D33_CPP", "D33_RAW", w_keys, "D33")):
            common = keys & w_cpp[cpp_label].keys() & result[fort_label].keys()
            if common != keys:
                compiled_mismatches.append((label,name,"missing",sorted(keys-common)[:3]))
                continue
            error = max(abs(w_cpp[cpp_label][key]-result[fort_label][key]) for key in common)
            scale = max(abs(result[fort_label][key]) for key in common)
            tol = 2e-6*max(1e-12,scale)
            print(f"CPP vs {label} W {name} max_error={error:.9g} tolerance={tol:.3g}")
            if error > tol:
                compiled_mismatches.append((label,name,error,tol))
        seam_keys = {(j,k) for j in range(1,NY-1) for k in range(NZ+1)}
        for seam in ("M_ZX_CPP", "M_ZX"):
            values = w_cpp[seam] if seam.endswith("CPP") else result[seam]
            seam_error = max(abs(values[(j,k,0)]-values[(j,k,NX)]) for j,k in seam_keys)
            seam_scale = max(abs(values[(j,k,0)]) for j,k in seam_keys)
            seam_tol = 2e-6*max(1e-12,seam_scale)
            print(f"{label} periodic zx endpoint {seam} max_error={seam_error:.9g} "
                  f"tolerance={seam_tol:.3g}")
            if seam_error > seam_tol:
                compiled_mismatches.append((label,seam,"seam",seam_error,seam_tol))
        w_h_error = max(abs(w_cpp["WH_RAW"][key]-result["HW_RAW"][key]) for key in w_keys)
        w_v_error = max(abs(w_cpp["WV_RAW"][key]-result["VW_RAW"][key]) for key in w_keys)
        w_raw_error = max(abs(w_cpp["WH_RAW"][key]+w_cpp["WV_RAW"][key]-
                              result["HW_RAW"][key]-result["VW_RAW"][key]) for key in w_keys)
        w_rhs_error = max(abs((w_cpp["WH_RAW"][key]+w_cpp["WV_RAW"][key])/MAP-
                              result["RHS_W"][key]) for key in w_keys)
        w_signal = max(abs(result["HW_RAW"][key]+result["VW_RAW"][key]) for key in w_keys)
        w_rhs_signal = max(abs(result["RHS_W"][key]) for key in w_keys)
        w_tol = 2e-6*max(1e-12,w_signal)
        w_rhs_tol = 2e-6*max(1e-12,w_rhs_signal)
        wrong_h = max(abs(w_cpp["WH_KH_WRONG"][key]-w_cpp["WH_RAW"][key]) for key in w_keys)
        wrong_v = max(abs(w_cpp["WV_KV_WRONG"][key]-w_cpp["WV_RAW"][key]) for key in w_keys)
        old_d13_error = max(abs(w_cpp["D13_OLDZX"][key]-result["D13_RAW"][key])
                            for key in w_keys)
        old_h_error = max(abs(w_cpp["WH_OLDZX"][key]-result["HW_RAW"][key])
                          for key in w_keys)
        print(f"CPP vs {label} W H/V errors={w_h_error:.9g}/{w_v_error:.9g}; "
              f"raw signal={w_signal:.9g} error={w_raw_error:.9g} tol={w_tol:.3g}; "
              f"post-rk_addtend signal={w_rhs_signal:.9g} error={w_rhs_error:.9g} "
              f"tol={w_rhs_tol:.3g}; wrong-k controls={wrong_h:.9g}/{wrong_v:.9g}; "
              f"old adjacent-zx D13/H errors={old_d13_error:.9g}/{old_h_error:.9g}")
        if (w_h_error>w_tol or w_v_error>w_tol or w_raw_error>w_tol or
                w_rhs_error>w_rhs_tol or wrong_h<=w_tol or wrong_v<=w_tol or
                old_d13_error<=2e-6*max(1e-12,max(abs(result["D13_RAW"][key]) for key in w_keys)) or
                old_h_error<=w_tol):
            compiled_mismatches.append((label,"W-composite",w_h_error,w_v_error,
                                        w_raw_error,w_rhs_error,wrong_h,wrong_v))
    top_pulse_result=top_pulse_results[0][1]
    top_keys = {(j, k, i) for j in range(1, NY - 1)
                for k in range(NZ) for i in range(1, NX)}
    top_h_errors = {key: abs(top_pulse_cpp["U_RAW"][key] - top_pulse_result["F_RAW"][key])
                    for key in top_keys}
    top_v_errors = {key: abs(top_pulse_cpp["UV_RAW"][key] - top_pulse_result["V_RAW"][key])
                    for key in top_keys}
    top_raw_errors = {key: abs((top_pulse_cpp["U_RAW"][key] + top_pulse_cpp["UV_RAW"][key]) -
                               (top_pulse_result["F_RAW"][key] + top_pulse_result["V_RAW"][key]))
                      for key in top_keys}
    top_rhs_errors = {key: abs((top_pulse_cpp["U_RAW"][key] + top_pulse_cpp["UV_RAW"][key]) / MAP -
                               top_pulse_result["RHS_U"][key]) for key in top_keys}
    top_d11_keys = {(j, k, i) for j in range(1, NY - 1)
                    for k in range(NZ) for i in range(1, NX)}
    top_d11_errors = {key: abs(top_pulse_cpp["D11_CPP"][key] -
                               top_pulse_result["D11_RAW"][key]) for key in top_d11_keys}
    h_key = max(top_h_errors, key=top_h_errors.get)
    v_key = max(top_v_errors, key=top_v_errors.get)
    raw_key = max(top_raw_errors, key=top_raw_errors.get)
    d11_key = max(top_d11_errors, key=top_d11_errors.get)
    h_by_k = [max(top_h_errors[key] for key in top_keys if key[1] == k)
              for k in range(NZ)]
    v_by_k = [max(top_v_errors[key] for key in top_keys if key[1] == k)
              for k in range(NZ)]
    print("TOP-PULSE SOURCE-CHAIN DIAGNOSTIC: "
          f"max H={top_h_errors[h_key]:.9g} at {h_key} "
          f"CPP={top_pulse_cpp['U_RAW'][h_key]:.9g} Fortran={top_pulse_result['F_RAW'][h_key]:.9g}; "
          f"max V={top_v_errors[v_key]:.9g} at {v_key}; "
          f"max raw composite={top_raw_errors[raw_key]:.9g} at {raw_key}; "
          f"max post-rk={max(top_rhs_errors.values()):.9g}; "
          f"levelwise H={h_by_k} V={v_by_k}; "
          f"D11 max={top_d11_errors[d11_key]:.9g} at {d11_key} "
          f"CPP={top_pulse_cpp['D11_CPP'][d11_key]:.9g} Fortran={top_pulse_result['D11_RAW'][d11_key]:.9g}; "
          f"D11@top={max(error for key,error in top_d11_errors.items() if key[1]==NZ-1):.9g}")
    top_signal = max(abs(top_pulse_result["F_RAW"][key] +
                         top_pulse_result["V_RAW"][key]) for key in top_keys)
    top_tolerance = 2e-6 * max(1e-12,top_signal)
    top_d11_signal = max(abs(top_pulse_result["D11_RAW"][key]) for key in top_d11_keys)
    top_d11_tolerance = 2e-6 * max(1e-12,top_d11_signal)
    print(f"PINNED TOP-PULSE parity: raw max_error={max(top_raw_errors.values()):.9g} "
          f"tol={top_tolerance:.3g}; D11 max_error={max(top_d11_errors.values()):.9g} "
          f"tol={top_d11_tolerance:.3g}; expected C++ cfn/cfn1=1.5/-0.5")
    if (max(top_raw_errors.values()) > top_tolerance or
            max(top_d11_errors.values()) > top_d11_tolerance or
            abs(cpp_metadata.get("--option2-momentum-stage-geometry-top-pulse",{}).get("cfn",0.0)-1.5) > 1e-6 or
            abs(cpp_metadata.get("--option2-momentum-stage-geometry-top-pulse",{}).get("cfn1",0.0)+0.5) > 1e-6):
        compiled_mismatches.append(("top-pulse-metric-pinning",
                                    max(top_raw_errors.values()),top_tolerance,
                                    max(top_d11_errors.values()),top_d11_tolerance,
                                    cpp_metadata))
    for label,top_result,_ in top_pulse_results:
        top_raw_error=max(abs((top_pulse_cpp["U_RAW"][key]+top_pulse_cpp["UV_RAW"][key])-
                              (top_result["F_RAW"][key]+top_result["V_RAW"][key]))
                          for key in top_keys)
        top_raw_sig=max(abs(top_result["F_RAW"][key]+top_result["V_RAW"][key])
                        for key in top_keys)
        top_raw_tol=2e-6*max(1e-12,top_raw_sig)
        top_rhs_error=max(abs((top_pulse_cpp["U_RAW"][key]+top_pulse_cpp["UV_RAW"][key])/MAP-
                              top_result["RHS_U"][key]) for key in top_keys)
        top_rhs_sig=max(abs(top_result["RHS_U"][key]) for key in top_keys)
        top_rhs_tol=2e-6*max(1e-12,top_rhs_sig)
        print(f"TOP-PULSE {label} raw signal={top_raw_sig:.9g} error={top_raw_error:.9g} "
              f"tol={top_raw_tol:.3g}; post-rk signal={top_rhs_sig:.9g} "
              f"error={top_rhs_error:.9g} tol={top_rhs_tol:.3g}")
        if top_raw_error>top_raw_tol or top_rhs_error>top_rhs_tol:
            compiled_mismatches.append((label,"top-pulse-U",top_raw_error,top_raw_tol,
                                        top_rhs_error,top_rhs_tol))
    print("ORACLE full chain extracts compute_diff_metrics, cal_deform_and_div, "
          "U/W horizontal and vertical diffusion, and rk_addtend_dry; the retained "
          "operator-only U reference injects Python D11/analytic metrics")
    print(f"PROVENANCE revision={revision} module_diffusion_em.F.sha256={sha} "
          f"wrf_sdirk3_tile_unified_impl.cpp.sha256={cpp_sha}")
    print(f"TEST cpp.sha256={cpp_test_sha} python.sha256={python_test_sha} "
          f"binary={args.binary.resolve()} binary.sha256={binary_sha}")
    print(f"FIXTURE Nx={NX} periodic_x H=100*cos(2*pi*i/8)m dx={DX:g}m "
          f"dz={DZ:g}m dnw=-1/{NZ} U=[0,1,0,0]*(1+0.1*cos(x)) khdif={KH:g} kvdif={KV:g} "
          f"xkhh={3*KH:g} xkhv={3*KV:g} rho={RHO:g} maps={MAP:g}; "
          f"km_opt=1 dry one tile interior_oracle_k={PARITY_K}; composite compared all k")
    print(f"PYTHON operator-only reference owned raw tendency max_abs={signal:.9g}")
    print(f"CPP vs oracle max_error={max_error:.9g} at {max_error_key} "
          f"cpp={actual[max_error_key]:.9g} oracle={expected[max_error_key]:.9g} "
          f"tolerance={tolerance:.3g}; seam_error={seam_error:.9g}")
    if args.expect_counterexample:
        ok = mismatch and signal > 1e-8 and seam_error == 0.0 and explicit_geometry is False
        print(("PASS" if ok else "FAIL") + " baseline stage-geometry counterexample")
        return 0 if ok else 1
    ok = (not mismatch and seam_error == 0.0 and explicit_geometry is True and
          not compiled_mismatches)
    if compiled_mismatches:
        print(f"FAIL compiled Fortran mismatches: {compiled_mismatches[:2]}")
    print(("PASS" if ok else "FAIL") +
          " option-2 U-X and W terrain composite Fortran geometry parity")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
