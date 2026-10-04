#!/usr/bin/env python3
"""Spot-check stable-wave reference rows against source-extracted WRF Fortran.

This is a compiled row oracle, not a full wave propagator or an independent
forecast reference.  It compiles the authoritative WRF calc_p_rho_phi,
pg_buoy_w, and rhs_ph routines with double default reals, then finite-differences
their pressure/EOS, interior/top W, geopotential-gravity, and omega rows.
"""
from __future__ import annotations

import argparse
import hashlib
import math
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import tempfile


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin:source.index(end, begin) + len(end)]


def normalized(source: str) -> str:
    return re.sub(r"\s+", " ", source.replace("&", " ")).lower()


def require_contract(body: str, formulas: tuple[str, ...], name: str) -> None:
    text = normalized(body)
    for formula in formulas:
        if normalized(formula) not in text:
            raise RuntimeError(f"{name}: source contract changed; missing {formula!r}")


def extracted_source(repo: Path) -> tuple[str, dict[str, str]]:
    big_path = repo / "dyn_em/module_big_step_utilities_em.F"
    solve_path = repo / "dyn_em/solve_em.F"
    big = big_path.read_text()
    solve = solve_path.read_text()
    calc = section(big, "SUBROUTINE calc_p_rho_phi", "END SUBROUTINE calc_p_rho_phi")
    pgw = section(big, "SUBROUTINE pg_buoy_w", "END SUBROUTINE pg_buoy_w")
    rhs = section(big, "SUBROUTINE rhs_ph(", "END SUBROUTINE rhs_ph")
    hpg = section(big, "SUBROUTINE horizontal_pressure_gradient(", "END SUBROUTINE horizontal_pressure_gradient")

    require_contract(calc, (
        "al(i,k,j)=-1./(c1(k)*muts(i,j)+c2(k))*(alb(i,k,j)*(c1(k)*mu(i,j)) + rdnw(k)*(ph(i,k+1,j)-ph(i,k,j)))",
        "p(i,k,j)=p0*(eos_arg**cpovcv)-pb(i,k,j)",
        "eos_arg = (r_d*(t0+t(i,k,j)))/(p0*(al(i,k,j)+alb(i,k,j)))",
    ), "calc_p_rho_phi")
    require_contract(pgw, (
        "cq1*rdn(k)*(p(i,k,j)-p(i,k-1,j))",
        "cq1*2.*rdnw(k-1)*(  -p(i,k-1,j))",
        "-(c1f(k)*muf(i,j))",
        "DO k = 2, kde-1",
    ), "pg_buoy_w")
    require_contract(rhs, (
        "wdwn(i,k) = rdnw(k-1)*(ph(i,k,j)-ph(i,k-1,j)+phb(i,k,j)-phb(i,k-1,j))",
        "ph_tend(i,k,j) = ph_tend(i,k,j) - ww(i,k,j)*(fnm(k)*wdwn(i,k+1)+fnp(k)*wdwn(i,k))",
        "ph_tend(i,kde,j) = 0.",
        "ph_tend(i,k,j) = ph_tend(i,k,j) + (c1f(k)*mut(i,j)+c2f(k))*g*w(i,k,j)/msfty(i,j)",
        "DO k = 2, kte",
    ), "rhs_ph")
    # The direct Fortran span is the W-staggered 4-mass/5-W convention used by
    # this fixture.  solve_em forwards the physical-domain kps/kpe bounds.
    solve_norm = normalized(solve)
    for text in ("k_start = kps", "k_end = kpe"):
        if normalized(text) not in solve_norm:
            raise RuntimeError(f"solve_em source contract changed; missing {text!r}")

    require_contract(hpg, (
        "itf=ite",
        "if ( config_flags%periodic_x ) itf=ite",
        "dpx = (msfux(i,j)/msfuy(i,j))*.5*rdx*(c1h(k)*muu(i,j)+c2h(k))",
        "(p (i,k,j)-p (i-1,k,j))",
        "ru_tend(i,k,j) = ru_tend(i,k,j)-dpx",
        "DO K=1,ktf",
    ), "horizontal_pressure_gradient")

    # Imported module state is reduced to a typed configuration/constants stub;
    # the routine bodies below remain verbatim source slices.
    routines = []
    for body in (calc, pgw, rhs, hpg):
        body = body.replace("\r\n", "\n")
        routines.append(body)
    module = """module row_config
  implicit none
  type :: grid_config_rec_type
    logical :: specified=.false., nested=.false.
    logical :: open_xs=.false., open_xe=.false.
    logical :: open_ys=.false., open_ye=.false.
    logical :: periodic_x=.false., polar=.false.
    integer :: h_sca_adv_order=2, phi_adv_z=2
  end type grid_config_rec_type
end module row_config

module row_constants
  implicit none
  integer, parameter :: PARAM_FIRST_SCALAR=1, P_QV=1
  real(kind=8), parameter :: r_d=287.0d0, rvovrd=1.608d0
  real(kind=8), parameter :: cp=1004.5d0, cv=717.5d0
  real(kind=8), parameter :: cpovcv=cp/cv, cvpm=cv/cp, p1000mb=100000.0d0
  ! WRF's canonical single-precision gravity promoted to the row-oracle real.
  real(kind=8), parameter :: g=9.81000041961669921875d0
end module row_constants

module extracted_wrf_rows
  use row_config, only: grid_config_rec_type
  use row_constants
  implicit none
contains
""" + "\n\n".join(routines) + "\nend module extracted_wrf_rows\n"
    stubs = """subroutine vpow(out, base, exponent, n)
  implicit none
  integer, intent(in) :: n
  real, intent(out) :: out(*)
  real, intent(in) :: base(*), exponent(*)
  integer :: i
  do i=1,n
    out(i)=base(i)**exponent(i)
  end do
end subroutine vpow

subroutine vpowx(n, base, exponent, out)
  implicit none
  integer, intent(in) :: n
  real, intent(in) :: base(*), exponent
  real, intent(out) :: out(*)
  integer :: i
  do i=1,n
    out(i)=base(i)**exponent
  end do
end subroutine vpowx

subroutine wrf_error_fatal(message)
  implicit none
  character(*), intent(in) :: message
  write(*,*) trim(message)
  stop 9
end subroutine wrf_error_fatal
"""
    hashes = {
        big_path.name: hashlib.sha256(big_path.read_bytes()).hexdigest(),
        solve_path.name: hashlib.sha256(solve_path.read_bytes()).hexdigest(),
        "calc_p_rho_phi": hashlib.sha256(calc.encode()).hexdigest(),
        "pg_buoy_w": hashlib.sha256(pgw.encode()).hexdigest(),
        "rhs_ph": hashlib.sha256(rhs.encode()).hexdigest(),
        "horizontal_pressure_gradient": hashlib.sha256(hpg.encode()).hexdigest(),
    }
    return module + stubs, hashes


DRIVER = r"""program row_driver
  use extracted_wrf_rows
  implicit none
  integer, parameter :: nz=4, nw=5
  integer :: i,j,k, nmoist
  real :: moist(1:1,1:nw,1:1,0:0)
  real :: al(1:1,1:nw,1:1), alb(1:1,1:nw,1:1), p(1:1,1:nw,1:1)
  real :: ph(1:1,1:nw,1:1), phb(1:1,1:nw,1:1)
  real :: t(1:1,1:nw,1:1), pb(1:1,1:nw,1:1)
  real :: mu(1:1,1:1), muts(1:1,1:1)
  real :: c1(1:nw), c2(1:nw), c3h(1:nw), c4h(1:nw), c3f(1:nw), c4f(1:nw)
  real :: znu(1:nw), znw(1:nw), dnw(1:nw), rdnw(1:nw), rdn(1:nw)
  real :: rw(1:1,1:nw,1:1), cqw(1:1,1:nw,1:1)
  real :: muf(0:2,0:2), mubf(0:2,0:2), msfty(0:2,0:2)
  real :: ph_tend(0:2,1:nw,0:2), u(0:2,1:nw,0:2), v(0:2,1:nw,0:2)
  real :: ww(0:2,1:nw,0:2), w(0:2,1:nw,0:2), ph_big(0:2,1:nw,0:2)
  real :: ph_old(0:2,1:nw,0:2), phb_big(0:2,1:nw,0:2)
  real :: mut(0:2,0:2), muuf(0:2,0:2), muvf(0:2,0:2)
  real :: msfux(0:2,0:2), msfuy(0:2,0:2), msfvx(0:2,0:2)
  real :: msfvx_inv(0:2,0:2), msfvy(0:2,0:2), msftx(0:2,0:2)
  real :: fnm(1:nw), fnp(1:nw), c1f(1:nw), c2f(1:nw)
  real :: theta_base(1:nz), theta_bar(1:nz), p_base(1:nz), p_bar(1:nz)
  real :: alpha_base(1:nz), alpha_bar(1:nz), phi_base(1:nw), phi_pert(1:nw)
  real :: ru_hpg(0:10,1:nw,0:2), rv_hpg(0:10,1:nw,0:2)
  real :: ph_hpg(0:10,1:nw,0:2), alt_hpg(0:10,1:nw,0:2)
  real :: p_hpg(0:10,1:nw,0:2), pb_hpg(0:10,1:nw,0:2)
  real :: al_hpg(0:10,1:nw,0:2), php_hpg(0:10,1:nw,0:2)
  real :: cqu_hpg(0:10,1:nw,0:2), cqv_hpg(0:10,1:nw,0:2)
  real :: muu_hpg(0:10,0:2), muv_hpg(0:10,0:2), mu_hpg(0:10,0:2)
  real :: msfux_hpg(0:10,0:2), msfuy_hpg(0:10,0:2)
  real :: msfvx_hpg(0:10,0:2), msfvy_hpg(0:10,0:2)
  real :: msftx_hpg(0:10,0:2), msfty_hpg(0:10,0:2)
  real :: c1h_hpg(1:nw), c2h_hpg(1:nw), fnm_hpg(1:nw), fnp_hpg(1:nw), rdnw_hpg(1:nw)
  real :: hp_phys_base, hp_phys_plus, hp_phys_minus
  real :: hp_pack_left, hp_pack_right, hp_pack_left_plus, hp_pack_left_minus
  real :: hp_pack_right_plus, hp_pack_right_minus, pi
  real :: h, p0, ptop, t0, mu_bar, mub
  real :: al_plus, al_minus, p_plus, p_minus, coeff
  type(grid_config_rec_type) :: cfg
  integer :: ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte
  integer :: h_ids,h_ide,h_jds,h_jde,h_kds,h_kde,h_ims,h_ime,h_jms,h_jme,h_kms,h_kme
  integer :: h_its,h_ite,h_jts,h_jte,h_kts,h_kte

  ids=1; ide=2; jds=1; jde=2; kds=1; kde=5
  ims=1; ime=1; jms=1; jme=1; kms=1; kme=5
  its=1; ite=1; jts=1; jte=1; kts=1; kte=5
  nmoist=0; p0=100000.; ptop=20000.; t0=300.
  mu_bar=4000.; mub=80000.
  theta_base=(/300.,300.,300.,300./)
  theta_bar=(/310.,312.,314.,316./)
  p_base=(/90000.,70000.,50000.,30000./)
  p_bar=(/93500.,72500.,51500.,30500./)
  h=0.25
  do k=1,nz
    alpha_base(k)=r_d*theta_base(k)/p_base(k)*(p_base(k)/p0)**(r_d/cp)
    alpha_bar(k)=r_d*theta_bar(k)/p_bar(k)*(p_bar(k)/p0)**(r_d/cp)
  end do
  phi_base=0.; phi_pert=0.
  do k=1,nz
    phi_base(k+1)=phi_base(k)+alpha_base(k)*mub/4.
    phi_pert(k+1)=phi_pert(k)+(alpha_bar(k)*(mub+mu_bar)-alpha_base(k)*mub)/4.
  end do
  do k=1,nw
    c1(k)=1.; c2(k)=0.; c3h(k)=0.; c4h(k)=0.; c3f(k)=0.; c4f(k)=0.
    znu(k)=0.; znw(k)=0.; dnw(k)=-0.25; rdnw(k)=-4.; rdn(k)=-4.
    fnm(k)=0.5; fnp(k)=0.5; c1f(k)=1.; c2f(k)=0.
  end do
  do k=1,nw
    al(1,k,1)=0.; alb(1,k,1)=0.; p(1,k,1)=0.; t(1,k,1)=0.; pb(1,k,1)=0.
    ph(1,k,1)=phi_pert(k); phb(1,k,1)=phi_base(k)
  end do
  do k=1,nz
    alb(1,k,1)=alpha_base(k); t(1,k,1)=theta_bar(k)-t0; pb(1,k,1)=p_base(k)
  end do
  mu(1,1)=mu_bar; muts(1,1)=mub+mu_bar
  moist=0.
  call calc_p_rho_phi(moist,nmoist,1,al,alb,mu,muts,c1,c2,c3h,c4h,c3f,c4f, &
       ph,phb,p,pb,t,p0,t0,ptop,znu,znw,dnw,rdnw,rdn,.true.,1, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)
  write(*,'(A,1X,ES25.17,1X,ES25.17)') 'CALC_BASE',al(1,2,1)+alb(1,2,1),p(1,2,1)
  ph(1,3,1)=phi_pert(3)+h
  call calc_p_rho_phi(moist,nmoist,1,al,alb,mu,muts,c1,c2,c3h,c4h,c3f,c4f, &
       ph,phb,p,pb,t,p0,t0,ptop,znu,znw,dnw,rdnw,rdn,.true.,1, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)
  al_plus=al(1,2,1)+alb(1,2,1); p_plus=p(1,2,1)
  ph(1,3,1)=phi_pert(3)-h
  call calc_p_rho_phi(moist,nmoist,1,al,alb,mu,muts,c1,c2,c3h,c4h,c3f,c4f, &
       ph,phb,p,pb,t,p0,t0,ptop,znu,znw,dnw,rdnw,rdn,.true.,1, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)
  al_minus=al(1,2,1)+alb(1,2,1); p_minus=p(1,2,1)
  write(*,'(A,1X,4(ES25.17,1X))') 'CALC_FD',al_plus,al_minus,p_plus,p_minus

  ! The compiled pg_buoy_w receives source-calculated p' and the balanced
  ! MUB=80 kPa / mu=4 kPa row profile.  Perturb p'(k=4) to expose both rows.
  do k=1,nw
    rw(:,k,:)=0.; cqw(:,k,:)=0.
  end do
  do k=1,nz
    p(1,k,1)=p_bar(k)-p_base(k)
  end do
  muf=mu_bar; mubf=mub; msfty=1.; msftx=1.
  call pg_buoy_w(rw,p,cqw,muf,mubf,c1f,c2f,rdnw,rdn,g,msftx,msfty, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PG_BASE',rw(1,4,1),rw(1,5,1)
  p(1,4,1)=p_bar(4)-p_base(4)+h
  rw=0.; cqw=0.
  call pg_buoy_w(rw,p,cqw,muf,mubf,c1f,c2f,rdnw,rdn,g,msftx,msfty, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PG_PLUS',rw(1,4,1),rw(1,5,1)
  p(1,4,1)=p_bar(4)-p_base(4)-h
  rw=0.; cqw=0.
  call pg_buoy_w(rw,p,cqw,muf,mubf,c1f,c2f,rdnw,rdn,g,msftx,msfty, &
       ids,ide,jds,jde,kds,kde,ims,ime,jms,jme,kms,kme,its,ite,jts,jte,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PG_MINUS',rw(1,4,1),rw(1,5,1)

  ! Use 0:2 horizontal halos so the full rhs_ph body is safe to execute.  All
  ! horizontal velocities are zero; the measured rows are vertical omega and
  ! the full-span gravity term including k=kde=5.
  u=0.; v=0.; ww=0.; w=0.; ph_big=0.; ph_old=0.; phb_big=0.; ph_tend=0.
  mut=mub+mu_bar; muuf=1.; muvf=1.
  msfux=1.; msfuy=1.; msfvx=1.; msfvx_inv=1.; msfvy=1.; msftx=1.; msfty=1.
  muf=mu_bar; mubf=mub
  do k=1,nw
    ph_big(1,k,1)=phi_pert(k); phb_big(1,k,1)=phi_base(k)
    w(1,k,1)=0.2*k; ww(1,k,1)=0.01*k
  end do
  cfg%specified=.false.; cfg%nested=.false.; cfg%open_xs=.false.; cfg%open_xe=.false.
  cfg%open_ys=.false.; cfg%open_ye=.false.; cfg%h_sca_adv_order=2; cfg%phi_adv_z=2
  call rhs_ph(ph_tend,u,v,ww,ph_big,ph_old,phb_big,w,mut,muuf,muvf,c1f,c2f, &
       fnm,fnp,rdnw,1.,1.,1.,1.,msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
       .true.,cfg,ids,ide,jds,jde,kds,kde,0,2,0,2,kms,kme,0,2,0,2,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PH_BASE',ph_tend(1,3,1),ph_tend(1,5,1)
  w(1,3,1)=0.6+h; ph_tend=0.
  call rhs_ph(ph_tend,u,v,ww,ph_big,ph_old,phb_big,w,mut,muuf,muvf,c1f,c2f, &
       fnm,fnp,rdnw,1.,1.,1.,1.,msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
       .true.,cfg,ids,ide,jds,jde,kds,kde,0,2,0,2,kms,kme,0,2,0,2,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PH_WPLUS',ph_tend(1,3,1),ph_tend(1,5,1)
  w(1,3,1)=0.6-h; ph_tend=0.
  call rhs_ph(ph_tend,u,v,ww,ph_big,ph_old,phb_big,w,mut,muuf,muvf,c1f,c2f, &
       fnm,fnp,rdnw,1.,1.,1.,1.,msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
       .true.,cfg,ids,ide,jds,jde,kds,kde,0,2,0,2,kms,kme,0,2,0,2,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PH_WMINUS',ph_tend(1,3,1),ph_tend(1,5,1)
  w(1,3,1)=0.6; w(1,5,1)=0.8+h; ph_tend=0.
  call rhs_ph(ph_tend,u,v,ww,ph_big,ph_old,phb_big,w,mut,muuf,muvf,c1f,c2f, &
       fnm,fnp,rdnw,1.,1.,1.,1.,msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
       .true.,cfg,ids,ide,jds,jde,kds,kde,0,2,0,2,kms,kme,0,2,0,2,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PH_TOPPLUS',ph_tend(1,3,1),ph_tend(1,5,1)
  w(1,5,1)=0.8-h; ph_tend=0.
  call rhs_ph(ph_tend,u,v,ww,ph_big,ph_old,phb_big,w,mut,muuf,muvf,c1f,c2f, &
       fnm,fnp,rdnw,1.,1.,1.,1.,msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
       .true.,cfg,ids,ide,jds,jde,kds,kde,0,2,0,2,kms,kme,0,2,0,2,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PH_TOPMINUS',ph_tend(1,3,1),ph_tend(1,5,1)
  w(1,5,1)=0.8; ww(1,3,1)=0.03+h; ph_tend=0.
  call rhs_ph(ph_tend,u,v,ww,ph_big,ph_old,phb_big,w,mut,muuf,muvf,c1f,c2f, &
       fnm,fnp,rdnw,1.,1.,1.,1.,msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
       .true.,cfg,ids,ide,jds,jde,kds,kde,0,2,0,2,kms,kme,0,2,0,2,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PH_OMPLUS',ph_tend(1,3,1),ph_tend(1,5,1)
  ww(1,3,1)=0.03-h; ph_tend=0.
  call rhs_ph(ph_tend,u,v,ww,ph_big,ph_old,phb_big,w,mut,muuf,muvf,c1f,c2f, &
       fnm,fnp,rdnw,1.,1.,1.,1.,msfux,msfuy,msfvx,msfvx_inv,msfvy,msftx,msfty, &
       .true.,cfg,ids,ide,jds,jde,kds,kde,0,2,0,2,kms,kme,0,2,0,2,kts,kte)
  write(*,'(A,1X,2(ES25.17,1X))') 'PH_OMMINUS',ph_tend(1,3,1),ph_tend(1,5,1)

  ! Source-extracted horizontal_pressure_gradient rows. The physical layout
  ! has eight mass points/nine U points; the packed layout has seven unique
  ! mass points, one mass alias, and periodic U seam rows at i=1 and i=8.
  pi=4.*atan(1.)
  alt_hpg=alpha_bar(2); ph_hpg=0.; pb_hpg=0.; al_hpg=0.; php_hpg=0.
  cqu_hpg=1.; cqv_hpg=1.; muu_hpg=mub+mu_bar; muv_hpg=mub+mu_bar; mu_hpg=mu_bar
  msfux_hpg=1.; msfuy_hpg=1.; msfvx_hpg=1.; msfvy_hpg=1.; msftx_hpg=1.; msfty_hpg=1.
  c1h_hpg=1.; c2h_hpg=0.; fnm_hpg=0.5; fnp_hpg=0.5; rdnw_hpg=-4.
  p_hpg=0.;
  do j=0,2
    do k=1,nz
      do i=1,8
        p_hpg(i,k,j)=cos(2.*pi*real(i-1)/8.)
      end do
    end do
  end do
  h_ids=1; h_ide=9; h_jds=1; h_jde=2; h_kds=1; h_kde=5
  h_ims=0; h_ime=10; h_jms=0; h_jme=2; h_kms=1; h_kme=5
  h_its=1; h_ite=9; h_jts=1; h_jte=1; h_kts=1; h_kte=5
  cfg%periodic_x=.false.; ru_hpg=0.; rv_hpg=0.; call run_hpg()
  hp_phys_base=ru_hpg(8,2,1)
  p_hpg(8,2,:)=p_hpg(8,2,:)+h; ru_hpg=0.; rv_hpg=0.; call run_hpg()
  hp_phys_plus=ru_hpg(8,2,1)
  p_hpg(8,2,:)=p_hpg(8,2,:)-2.*h; ru_hpg=0.; rv_hpg=0.; call run_hpg()
  hp_phys_minus=ru_hpg(8,2,1)
  write(*,'(A,1X,4(ES25.17,1X))') 'HPG_PHYS',hp_phys_base,hp_phys_plus,hp_phys_minus, &
       (hp_phys_plus-hp_phys_minus)/(2.*h)

  p_hpg=0.
  do j=0,2
    do k=1,nz
      do i=1,7
        p_hpg(i,k,j)=cos(2.*pi*real(i-1)/7.)
      end do
      p_hpg(0,k,j)=p_hpg(7,k,j); p_hpg(8,k,j)=p_hpg(1,k,j)
    end do
  end do
  h_ide=8; h_ite=8; cfg%periodic_x=.true.; ru_hpg=0.; rv_hpg=0.; call run_hpg()
  hp_pack_left=ru_hpg(1,2,1); hp_pack_right=ru_hpg(8,2,1)
  p_hpg(7,2,:)=p_hpg(7,2,:)+h; p_hpg(0,2,:)=p_hpg(0,2,:)+h
  ru_hpg=0.; rv_hpg=0.; call run_hpg()
  hp_pack_left_plus=ru_hpg(1,2,1); hp_pack_right_plus=ru_hpg(8,2,1)
  p_hpg(7,2,:)=p_hpg(7,2,:)-2.*h; p_hpg(0,2,:)=p_hpg(0,2,:)-2.*h
  ru_hpg=0.; rv_hpg=0.; call run_hpg()
  hp_pack_left_minus=ru_hpg(1,2,1); hp_pack_right_minus=ru_hpg(8,2,1)
  write(*,'(A,1X,6(ES25.17,1X))') 'HPG_PACKED',hp_pack_left,hp_pack_right, &
       hp_pack_left_plus,hp_pack_left_minus,hp_pack_right_plus,hp_pack_right_minus
contains
  subroutine run_hpg()
    implicit none
    call horizontal_pressure_gradient(ru_hpg,rv_hpg,ph_hpg,alt_hpg,p_hpg,pb_hpg,al_hpg, &
         php_hpg,cqu_hpg,cqv_hpg,muu_hpg,muv_hpg,mu_hpg,c1h_hpg,c2h_hpg,fnm_hpg, &
         fnp_hpg,rdnw_hpg,1./3.,1./3.,1./3.,1./3.,1./3.,1.,1.,msfux_hpg,msfuy_hpg, &
         msfvx_hpg,msfvy_hpg,msftx_hpg,msfty_hpg,cfg,.false.,.false., &
         h_ids,h_ide,h_jds,h_jde,h_kds,h_kde,h_ims,h_ime,h_jms,h_jme,h_kms,h_kme, &
         h_its,h_ite,h_jts,h_jte,h_kts,h_kte)
  end subroutine run_hpg
end program row_driver
"""


def parse_output(text: str) -> dict[str, list[float]]:
    values: dict[str, list[float]] = {}
    for line in text.splitlines():
        fields = line.split()
        if fields and fields[0] in {
            "CALC_BASE", "CALC_FD", "PG_BASE", "PG_PLUS", "PG_MINUS",
            "PH_BASE", "PH_WPLUS", "PH_WMINUS", "PH_TOPPLUS", "PH_TOPMINUS",
            "PH_OMPLUS", "PH_OMMINUS", "HPG_PHYS", "HPG_PACKED",
        }:
            values[fields[0]] = [float(value.replace("D", "E")) for value in fields[1:]]
    return values


def close(actual: float, expected: float, *, atol: float, rtol: float, label: str) -> float:
    error = abs(actual - expected)
    budget = atol + rtol * abs(expected)
    if not math.isfinite(actual) or error > budget:
        raise AssertionError(f"{label}: actual={actual:.12g} expected={expected:.12g} error={error:.3g} budget={budget:.3g}")
    return error


def run(repo: Path, compiler: str) -> None:
    source, hashes = extracted_source(repo)
    version = subprocess.run([compiler, "--version"], check=True, capture_output=True, text=True).stdout.splitlines()[0]
    flags = ["-O0", "-cpp", "-fdefault-real-8", "-ffree-line-length-none"]
    sysroot = os.environ.get("SDKROOT")
    if platform.system() == "Darwin" and not sysroot:
        sdk = Path("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
        if sdk.exists():
            sysroot = str(sdk)
    if sysroot:
        flags.insert(0, f"--sysroot={sysroot}")
    with tempfile.TemporaryDirectory(prefix="stable-wave-row-oracle-") as temp:
        work = Path(temp)
        f90 = work / "rows.f90"
        binary = work / "rows.exe"
        f90.write_text(source + "\n" + DRIVER)
        build = subprocess.run([compiler, *flags, str(f90), "-o", str(binary)], capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError("source-extracted Fortran compile failed:\n" + build.stderr)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    values = parse_output(result.stdout)
    required = {
        "CALC_BASE", "CALC_FD", "PG_BASE", "PG_PLUS", "PG_MINUS",
        "PH_BASE", "PH_WPLUS", "PH_WMINUS", "PH_TOPPLUS", "PH_TOPMINUS",
        "PH_OMPLUS", "PH_OMMINUS", "HPG_PHYS", "HPG_PACKED",
    }
    if values.keys() < required:
        raise RuntimeError(f"compiled oracle omitted rows: {sorted(required - values.keys())}; stdout={result.stdout}")

    # Pressure/alpha row: layer 2, upper interface k=3.  The analytic alpha
    # and EOS derivatives use the same M/theta/p profile as the compiled call.
    theta_bar, p_bar = 312.0, 72500.0
    alpha_bar = 287.0 * theta_bar / p_bar * (p_bar / 100000.0) ** (287.0 / 1004.5)
    alpha_fd = (values["CALC_FD"][0] - values["CALC_FD"][1]) / 0.5
    p_fd = (values["CALC_FD"][2] - values["CALC_FD"][3]) / 0.5
    alpha_analytic = 4.0 / 84000.0
    p_analytic = -(1004.5 / 717.5) * p_bar / alpha_bar * alpha_analytic
    err_alpha = close(alpha_fd, alpha_analytic, atol=2e-11, rtol=2e-8, label="EOS alpha d/dPH")
    err_pressure = close(p_fd, p_analytic, atol=2e-8, rtol=2e-8, label="EOS pressure d/dPH")
    close(values["CALC_BASE"][0], alpha_bar, atol=3e-13, rtol=2e-12, label="balanced alpha")
    close(values["CALC_BASE"][1], 2500.0, atol=3e-8, rtol=2e-12, label="balanced pressure perturbation")

    # Interior and special top PG/buoyancy rows for p'(k=4).  The signed
    # inverse eta metrics are rdn=rdnw=-4 in this source-faithful profile.
    pg_fd = [
        (values["PG_PLUS"][0] - values["PG_MINUS"][0]) / 0.5,
        (values["PG_PLUS"][1] - values["PG_MINUS"][1]) / 0.5,
    ]
    pg_expected = [float("9.81000041961669921875") * -4.0,
                   float("9.81000041961669921875") * 8.0]
    err_pg_i = close(pg_fd[0], pg_expected[0], atol=2e-9, rtol=2e-12, label="PG interior d/dp4")
    err_pg_t = close(pg_fd[1], pg_expected[1], atol=2e-9, rtol=2e-12, label="PG top d/dp4")
    close(values["PG_BASE"][0], 0.0, atol=2e-8, rtol=0.0, label="PG balanced interior")
    close(values["PG_BASE"][1], 0.0, atol=2e-8, rtol=0.0, label="PG balanced top")

    # rhs_ph gravity rows include both k=3 and the special upper W level k=5.
    g_m = 9.81000041961669921875 * 84000.0
    gravity_i = (values["PH_WPLUS"][0] - values["PH_WMINUS"][0]) / 0.5
    gravity_top = (values["PH_TOPPLUS"][1] - values["PH_TOPMINUS"][1]) / 0.5
    err_gi = close(gravity_i, g_m, atol=2e-7, rtol=2e-12, label="PH gravity d/dW3")
    err_gt = close(gravity_top, g_m, atol=2e-7, rtol=2e-12, label="PH top gravity d/dW5")

    # Omega row uses phi_adv_z=2, full (PH+PHB) vertical differences, signed
    # rdnw, and the original WRF fnm/fnp interpolation.
    layer_alpha_bar = [
        287.0 * theta / pressure * (pressure / 100000.0) ** (287.0 / 1004.5)
        for theta, pressure in zip((310.0, 312.0, 314.0, 316.0),
                                   (93500.0, 72500.0, 51500.0, 30500.0))
    ]
    omega_coeff = -0.5 * (
        -4.0 * layer_alpha_bar[2] * 84000.0 / 4.0 +
        -4.0 * layer_alpha_bar[1] * 84000.0 / 4.0
    )
    omega_fd = (values["PH_OMPLUS"][0] - values["PH_OMMINUS"][0]) / 0.5
    err_omega = close(omega_fd, omega_coeff, atol=3e-8, rtol=2e-12, label="PH omega d/dWW3")
    if abs(values["PH_BASE"][1]) <= 1.0:
        raise AssertionError("top PH gravity signal is unexpectedly absent")

    # Horizontal pressure-gradient rows at C-grid U points.  The hydrostatic
    # row reduces to -M*alpha*(p_i-p_{i-1}) for the constant map/metric profile.
    hpg_alpha = layer_alpha_bar[1]
    hpg_scale = 84000.0 * hpg_alpha
    hpg_physical = values["HPG_PHYS"]
    hpg_physical_fd = hpg_physical[3]
    hpg_physical_expected = -hpg_scale
    err_hpg_physical = close(hpg_physical_fd, hpg_physical_expected,
                             atol=2e-7, rtol=2e-12,
                             label="HPG physical U7 d/dp8")
    if abs(hpg_physical[0]) <= 1e-6 or abs(hpg_physical[0] - hpg_physical[1]) <= 1e-6:
        raise AssertionError("physical HPG U7 row has no finite pressure-gradient signal")

    hpg_packed = values["HPG_PACKED"]
    pack_left_fd = (hpg_packed[2] - hpg_packed[3]) / 0.5
    pack_right_fd = (hpg_packed[4] - hpg_packed[5]) / 0.5
    hpg_packed_expected = hpg_scale
    err_hpg_pack_left = close(pack_left_fd, hpg_packed_expected,
                              atol=2e-7, rtol=2e-12,
                              label="HPG packed west seam d/dpLast")
    err_hpg_pack_right = close(pack_right_fd, hpg_packed_expected,
                               atol=2e-7, rtol=2e-12,
                               label="HPG packed east seam d/dpLast")
    close(hpg_packed[0], hpg_packed[1], atol=1e-9, rtol=2e-12,
          label="HPG packed periodic seam equality")

    print(f"COMPILER {version}")
    print(f"FLAGS {shlex.join(flags)}")
    for name, value in hashes.items():
        print(f"SOURCE_SHA256 {name}={value}")
    print("PROFILE nz=4 nw=5 kds=kts=1 kde=kte=5 MUB=80000 mu=4000 M=84000 ptop=20000")
    print("PROFILE pBase=90000,70000,50000,30000 pPert=3500,2500,1500,500 theta=310,312,314,316")
    print(f"COEFFICIENT eos_alpha={alpha_fd:.12g}/{alpha_analytic:.12g} eos_pressure={p_fd:.12g}/{p_analytic:.12g}")
    print(f"COEFFICIENT pg_interior={pg_fd[0]:.12g}/{pg_expected[0]:.12g} pg_top={pg_fd[1]:.12g}/{pg_expected[1]:.12g}")
    print(f"COEFFICIENT ph_gravity={gravity_i:.12g}/{g_m:.12g} ph_top={gravity_top:.12g}/{g_m:.12g} omega={omega_fd:.12g}/{omega_coeff:.12g}")
    print(f"ROWS pressure_alpha={err_alpha:.3g} pressure={err_pressure:.3g} pg_i={err_pg_i:.3g} pg_top={err_pg_t:.3g} ph_g={err_gi:.3g} ph_top={err_gt:.3g} omega={err_omega:.3g}")
    print("HPG_COUNTS physical=9FortranU(7interior+2boundaries) packed=8FortranU(7unique+1duplicate-seam)")
    print(f"HPG_COEFFICIENT physical_U7={hpg_physical_fd:.12g}/{hpg_physical_expected:.12g} packed_seams={pack_left_fd:.12g},{pack_right_fd:.12g}/{hpg_packed_expected:.12g}")
    print(f"HPG_ROWS physical={err_hpg_physical:.3g} packed_left={err_hpg_pack_left:.3g} packed_right={err_hpg_pack_right:.3g}")
    print("SCOPE compiled source rows only; no full coupled-wave, advance_w, top_lid, or forecast oracle")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"))
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    run(args.repo.resolve(), args.fortran_compiler)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
