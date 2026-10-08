#!/usr/bin/env python3
"""Run a bounded four-control native inverse with one source-generated dataset.

The observation points, values, and sigma are fixed across coarse/fine runs.
The source truth comes from the independent fine-grid linear operator; native
forward/VJP evaluations use the test-only ``--physical-wave-inverse`` CLI.
This is a fixture-specific inverse check, not a general NWP observation system.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from scipy.linalg import expm


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
import wave_quadratic_reference as reference  # noqa: E402


SIGMA = 3.0e-4
TRUTH = np.array([0.78, -0.36, 0.35, 0.24], dtype=np.float64)
FD_DIRECTION = TRUTH / np.linalg.norm(TRUTH)
GRIDS = {"coarse": (8, 6, 4), "fine": (16, 12, 8)}
LENGTHS = (40_000.0, 30_000.0)


def digest(values: np.ndarray) -> str:
    return hashlib.sha256(np.asarray(values, dtype=np.float64).tobytes()).hexdigest()


def file_digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_payload(path: Path) -> tuple[dict,dict,dict,dict]:
    """Read numeric arrays/scalars and string-valued native M/T metadata."""
    meta: dict[str,float]={}; arrays: dict[str,np.ndarray]={}
    scalars: dict[str,float]={}; text_meta: dict[str,str]={}
    with path.open(newline="") as stream:
        for row in csv.reader(stream):
            if not row: continue
            if row[0]=="M":
                try: meta[row[1]]=float(row[2])
                except ValueError: text_meta[row[1]]=row[2]
            elif row[0]=="T": text_meta[row[1]]=row[2]
            elif row[0]=="S": scalars[row[1]]=float(row[2])
            elif row[0]=="A":
                count=int(row[2]); value=np.asarray([float(x) for x in row[3:]],dtype=np.float64)
                if value.size!=count: raise AssertionError(f"{row[1]} has {value.size}, expected {count}")
                arrays[row[1]]=value
    return meta,arrays,scalars,text_meta


def native_call(exe: Path, nx: int, ny: int, nz: int, out: Path,
                args: list[str]) -> tuple[dict, dict, dict, dict]:
    command = [str(exe), str(nx), str(ny), str(nz), str(out), *args]
    started = time.monotonic()
    print(f"native start {out.name}", flush=True)
    done = subprocess.run(command, capture_output=True, text=True, check=False)
    print(f"native end {out.name} rc={done.returncode} elapsed_s={time.monotonic()-started:.3f}", flush=True)
    if done.returncode:
        tail = "\n".join((done.stdout+done.stderr).splitlines()[-80:])
        raise RuntimeError(f"native wave inverse failed ({done.returncode}): {command}\n{tail}")
    return read_payload(out)


def descriptor(exe: Path, outdir: Path, grid: tuple[int, int, int]) -> dict:
    nx, ny, nz = grid
    path = outdir/f"descriptor_{nx}x{ny}x{nz}.csv"
    meta, arrays, scalars, text_meta = native_call(
        exe,nx,ny,nz,path,["--descriptor-only"])
    required = {"base", "phb", "pbase", "thbase_perturb", "mubase"}
    if required-arrays.keys():
        raise AssertionError(f"{path.name} missing descriptor arrays {sorted(required-arrays.keys())}")
    if (meta.get("nx"),meta.get("ny"),meta.get("nz")) != (float(nx),float(ny),float(nz)):
        raise AssertionError(f"{path.name} grid metadata mismatch")
    config_expected = {"kdamp_config": 0.0, "implicit_divergence": 0.0,
                       "omega_w_blend_config": 1.0, "do_curvature_config": 1.0,
                       "effective_wrf_omega_ww_cp": 1.0,
                       "advection_order_config": 2.0,
                       "non_hydrostatic_config": 1.0, "map_input_max_deviation": 0.0}
    mismatch = {k:(meta.get(k),v) for k,v in config_expected.items()
                if meta.get(k)!=v}
    if mismatch:
        raise AssertionError(f"{path.name} branch configuration mismatch: {mismatch}")
    c = reference.build_case(nx,nz,native_csv=path)
    return {"path":path,"meta":meta,"arrays":arrays,"scalars":scalars,
            "text_meta":text_meta,"column":c,"base":arrays["base"]}


def _complex_interp(z0: np.ndarray, value: np.ndarray, z1: np.ndarray) -> np.ndarray:
    return (np.interp(z1,z0,np.asarray(value).real) +
            1j*np.interp(z1,z0,np.asarray(value).imag))


def physical_profile_remap(c0: dict, q: np.ndarray, c1: dict) -> np.ndarray:
    """Sample U/theta mass and W/PH face profiles at the target physical heights."""
    n0,n1 = int(c0["nz"]),int(c1["nz"])
    g_native=float(c0["g"])
    zface0=np.asarray(c0["phi_total_w"],dtype=np.float64)/g_native
    zface1=np.asarray(c1["phi_total_w"],dtype=np.float64)/g_native
    zmass0=0.5*(zface0[:-1]+zface0[1:])
    zmass1=0.5*(zface1[:-1]+zface1[1:])
    out=np.zeros(4*n1+1,dtype=np.complex128)
    out[:n1]=_complex_interp(zmass0,q[:n0],zmass1)
    out[3*n1:4*n1]=_complex_interp(zmass0,q[3*n0:4*n0],zmass1)
    for begin0,begin1 in ((n0,n1),(2*n0,2*n1)):
        profile=np.r_[0j,q[begin0:begin0+n0]]
        out[begin1:begin1+n1]=_complex_interp(zface0,profile,zface1)[1:]
    out[-1]=q[-1]
    return out


def profile_basis(c0: dict, cgrid: dict) -> tuple[np.ndarray, list[np.ndarray]]:
    """Build the four fixed-physical-height source directions on one grid."""
    modes=[reference.mode_pair(c0,rank) for rank in (0,1)]
    source_basis=[modes[0][1],modes[0][2],modes[1][1],modes[1][2]]
    if int(cgrid["nz"]) != int(c0["nz"]) or not np.array_equal(cgrid["z_mass"],c0["z_mass"]):
        mapped=[physical_profile_remap(c0,q,cgrid) for q in source_basis]
    else:
        mapped=source_basis
    return np.stack(mapped,axis=1),mapped


def pack_source_mode(q: np.ndarray, grid: tuple[int,int,int]) -> np.ndarray:
    """Pack a real m=1 source phasor into native [u,v,w,ph,theta,mu]."""
    nx,ny,nz=grid
    q=np.asarray(q,dtype=np.complex128)
    if q.shape != (4*nz+1,):
        raise ValueError(f"source mode has shape {q.shape}, expected {(4*nz+1,)}")
    sizes=(ny*nz*(nx+1),(ny+1)*nz*nx,ny*(nz+1)*nx,
           ny*(nz+1)*nx,ny*nz*nx,ny*nx)
    offsets=np.r_[0,np.cumsum(sizes[:-1])].astype(int)
    result=np.zeros(int(sum(sizes)),dtype=np.float64)
    ou,ov,ow,oph,ot,omu=offsets
    xu=2.0*np.pi*np.arange(nx,dtype=np.float64)/nx
    xc=2.0*np.pi*(np.arange(nx,dtype=np.float64)+0.5)/nx
    pu=np.exp(1j*xu);pc=np.exp(1j*xc)
    u=result[ou:ov].reshape(ny,nz,nx+1)
    w=result[ow:oph].reshape(ny,nz+1,nx)
    ph=result[oph:ot].reshape(ny,nz+1,nx)
    theta=result[ot:omu].reshape(ny,nz,nx)
    mu=result[omu:].reshape(ny,nx)
    for k in range(nz):
        u[:,k,:nx]=np.real(q[k]*pu)[None,:]
        u[:,k,nx]=u[:,k,0]
        w[:,k+1,:]=np.real(q[nz+k]*pc)[None,:]
        ph[:,k+1,:]=np.real(q[2*nz+k]*pc)[None,:]
        theta[:,k,:]=np.real(q[3*nz+k]*pc)[None,:]
    mu[:,:]=np.real(q[-1]*pc)[None,:]
    return result


def coarse_observation_xyz(c: dict, grid: tuple[int,int,int]) -> np.ndarray:
    """The existing 105 physical-W points: unique interior rows/faces/cells."""
    nx,ny,nz=grid
    dx,dy=LENGTHS[0]/nx,LENGTHS[1]/ny
    zfaces=np.asarray(c["phi_total_w"],dtype=np.float64)/float(c["g"])
    rows=[]
    for j in range(ny-1):
        for k in range(1,nz):
            for i in range(nx-1):
                rows.append(((i+0.5)*dx,(j+0.5)*dy,float(zfaces[k])))
    result=np.asarray(rows,dtype=np.float64)
    if result.shape != (105,3):
        raise ValueError(f"coarse observation design has shape {result.shape}, expected (105,3)")
    return result


def source_physical_w_samples(c: dict, q: np.ndarray, grid: tuple[int,int,int],
                              xyz: np.ndarray) -> np.ndarray:
    """Source A8 W sampled with the native bilinear-corner/PH-height H operator."""
    nx,ny,nz=grid
    dx,dy=LENGTHS[0]/nx,LENGTHS[1]/ny
    k1=2.0*np.pi/LENGTHS[0]
    phi_hat=np.r_[0j,np.asarray(q[2*nz:3*nz],dtype=np.complex128)]
    w_hat=np.r_[0j,np.asarray(q[nz:2*nz],dtype=np.complex128)]
    base_phi=np.asarray(c["phi_total_w"],dtype=np.float64)
    out=np.empty(xyz.shape[0],dtype=np.float64)
    for obs,(x,y,z) in enumerate(xyz):
        xloc=(x%LENGTHS[0])/dx-0.5
        x0=int(np.floor(xloc)); fx=xloc-x0
        yloc=y/dy-0.5
        y0=int(np.floor(yloc)); fy=yloc-y0
        value=0.0
        for jy,jweight in ((y0,1.0-fy),(y0+1,fy)):
            if not 0 <= jy < ny:
                raise ValueError("source observation lies outside fine y centers")
            for ix,xweight in ((x0,1.0-fx),(x0+1,fx)):
                ii=ix%nx
                xcenter=(ii+0.5)*dx
                phase=np.exp(1j*k1*xcenter)
                zfaces=(base_phi+np.real(phi_hat*phase))/float(c["g"])
                wfaces=np.real(w_hat*phase)
                # Native uses the lower layer at a face knot (left insertion).
                lower=int(np.searchsorted(zfaces,z,side="left")-1)
                lower=max(0,lower)
                if lower>=nz: lower=nz-1
                if lower < 0 or lower >= nz:
                    raise ValueError("source fixed-height observation is outside a fine column")
                alpha=(z-zfaces[lower])/(zfaces[lower+1]-zfaces[lower])
                sample=wfaces[lower]+alpha*(wfaces[lower+1]-wfaces[lower])
                value += jweight*xweight*sample
        out[obs]=value
    return out


def source_observation_data(cfine: dict, qfine: np.ndarray,
                            grid_coarse: tuple[int,int,int],
                            ccoarse: dict) -> tuple[np.ndarray,np.ndarray]:
    xyz=coarse_observation_xyz(ccoarse,grid_coarse)
    Af=reference.source_matrix(cfine)
    obs=np.stack([source_physical_w_samples(cfine,expm(t*Af)@qfine,
                                            (int(cfine["nx"]),int(cfine["ny"]),int(cfine["nz"])),xyz)
                  for t in (150.0,300.0)])
    return xyz,obs


def source_linear_observation_matrix(c: dict, basis: list[np.ndarray],
                                     xyz: np.ndarray) -> np.ndarray:
    """Build the independent linear H exp(tA) B matrix for BFGS scaling."""
    A=reference.source_matrix(c)
    columns=[]
    for q in basis:
        columns.append(np.concatenate([
            source_linear_w_samples(c,expm(t*A)@q,xyz) for t in (150.0,300.0)]))
    return np.column_stack(columns)


def source_linear_w_samples(c: dict, q: np.ndarray, xyz: np.ndarray) -> np.ndarray:
    """Linearized fixed-physical-height H at the balanced background."""
    nx,ny,nz=int(c["nx"]),int(c["ny"]),int(c["nz"])
    dx,dy=LENGTHS[0]/nx,LENGTHS[1]/ny
    k1=2.0*np.pi/LENGTHS[0]
    w_hat=np.r_[0j,np.asarray(q[nz:2*nz],dtype=np.complex128)]
    zfaces=np.asarray(c["phi_total_w"],dtype=np.float64)/float(c["g"])
    out=np.empty(xyz.shape[0],dtype=np.float64)
    for obs,(x,y,z) in enumerate(xyz):
        xloc=(x%LENGTHS[0])/dx-0.5; x0=int(np.floor(xloc)); fx=xloc-x0
        yloc=y/dy-0.5; y0=int(np.floor(yloc)); fy=yloc-y0
        value=0.0
        for jy,jweight in ((y0,1.0-fy),(y0+1,fy)):
            for ix,xweight in ((x0,1.0-fx),(x0+1,fx)):
                ii=ix%nx
                phase=np.exp(1j*k1*(ii+0.5)*dx)
                wfaces=np.real(w_hat*phase)
                lower=int(np.searchsorted(zfaces,z,side="left")-1)
                lower=max(0,lower)
                if lower>=nz: lower=nz-1
                alpha=(z-zfaces[lower])/(zfaces[lower+1]-zfaces[lower])
                value+=jweight*xweight*((1.0-alpha)*wfaces[lower]+alpha*wfaces[lower+1])
        out[obs]=value
    return out


def write_vector(path: Path, values: np.ndarray) -> None:
    v=np.asarray(values,dtype=np.float64).ravel()
    with path.open("w") as f:
        f.write(f"{v.size}\n")
        f.write(" ".join(format(float(x),".17g") for x in v)+"\n")


def write_observations(path: Path, xyz: np.ndarray, values: np.ndarray) -> None:
    if xyz.shape != (105,3) or values.shape != (2,105):
        raise ValueError("physical observation file requires 105 xyz rows and two times")
    with path.open("w") as f:
        f.write("PHYSICAL_WAVE_OBSERVATIONS_V1\n105\n")
        for p,y150,y300 in zip(xyz,values[0],values[1]):
            f.write(" ".join(format(float(x),".17g") for x in (*p,y150,y300))+"\n")


def check_initial_admissibility(grid: tuple[int,int,int], state: np.ndarray,
                                descriptor_data: dict) -> None:
    nx,ny,nz=grid
    sizes=(ny*nz*(nx+1),(ny+1)*nz*nx,ny*(nz+1)*nx,ny*(nz+1)*nx,ny*nz*nx,ny*nx)
    offsets=np.r_[0,np.cumsum(sizes[:-1])].astype(int)
    phb=np.asarray(descriptor_data["arrays"]["phb"],dtype=float).reshape(ny,nz+1,nx)
    mubase=np.asarray(descriptor_data["arrays"]["mubase"],dtype=float).reshape(ny,nx)
    theta=300.0+state[int(offsets[4]):int(offsets[5])].reshape(ny,nz,nx)
    mu=mubase+state[int(offsets[5]):].reshape(ny,nx)
    ph=state[int(offsets[3]):int(offsets[3])+sizes[3]].reshape(ny,nz+1,nx)
    z=(phb+ph)/float(descriptor_data["column"]["g"])
    if np.min(theta)<=0.0 or np.min(mu)<=0.0 or not np.all(np.diff(z,axis=1)>0.0):
        raise ValueError("initial controls violate positive theta/mass or monotone PH height")


def evaluate(exe: Path, grid: tuple[int,int,int], outdir: Path,
             descriptor_data: dict, base: np.ndarray, B: np.ndarray,
             controls: np.ndarray, observations: Path,
             steps: int, dt: int, forward_only: bool=False) -> dict:
    nx,ny,nz=grid
    initial=base+B@np.asarray(controls,dtype=np.float64)
    check_initial_admissibility(grid,initial,descriptor_data)
    runid=f"{nx}x{ny}x{nz}_s{steps}_v{digest(controls)[:12]}"+("_fwd" if forward_only else "_adj")
    state_path=outdir/f"initial_{runid}.txt"
    result_path=outdir/f"result_{runid}.csv"
    write_vector(state_path,initial)
    argv=["--physical-wave-inverse",str(state_path),str(observations),str(steps),str(dt)]
    if forward_only: argv.append("--forward-only")
    meta,arrays,scalars,text_meta=native_call(exe,nx,ny,nz,result_path,argv)
    if meta.get("physical_wave_inverse")!=1.0 or text_meta.get("physical_observation_domain_guard") != "passed":
        raise AssertionError(f"native physical-wave guards did not pass: {result_path}")
    if text_meta.get("checkpoint_precision")!="retained_fp64_trajectory":
        raise AssertionError(f"unexpected checkpoint precision in {result_path}")
    if meta.get("retains_tape")!=1.0 or meta.get("pullback_requested")!=(0.0 if forward_only else 1.0):
        raise AssertionError(f"native retained-tape/pullback mode mismatch in {result_path}")
    if not forward_only and "initial_pullback" not in arrays:
        raise AssertionError("retained native evaluation omitted initial_pullback")
    gravity=meta.get("observation_gravity_m_s2")
    if gravity is not None and gravity!=float(descriptor_data["column"]["g"]):
        raise AssertionError(f"native/source observation gravity differs: {gravity} vs {descriptor_data['column']['g']}")
    return {"path":result_path,"meta":meta,"arrays":arrays,"scalars":scalars,
            "text_meta":text_meta,"controls":np.asarray(controls,dtype=float),
            "objective":float(scalars["objective_physical_w"]),"initial":initial}


def project_gradient(evaluation: dict, B: np.ndarray) -> np.ndarray:
    return B.T@evaluation["arrays"]["initial_pullback"]


def bfgs_optimize(exe: Path, grid: tuple[int,int,int], outdir: Path,
                  descriptor_data: dict, base: np.ndarray, B: np.ndarray,
                  observations: Path,
                  preconditioner: np.ndarray, max_iterations: int=12,
                  steps: int=30, dt: int=10,
                  initial_controls: np.ndarray | None=None) -> dict:
    """Four-parameter inverse BFGS with source-observability scaling and Armijo."""
    v=(np.zeros(B.shape[1],dtype=np.float64) if initial_controls is None else
       np.asarray(initial_controls,dtype=np.float64).copy())
    H=np.asarray(preconditioner,dtype=np.float64).copy()
    if H.shape!=(v.size,v.size) or not np.isfinite(H).all():
        raise ValueError("source preconditioner must be a finite square control matrix")
    evaluations=[]
    current=evaluate(exe,grid,outdir,descriptor_data,base,B,v,observations,steps,dt)
    evaluations.append(current)
    g=project_gradient(current,B)
    history=[{"iteration":0,"controls":v.tolist(),"objective":current["objective"],
              "gradient_norm":float(np.linalg.norm(g))}]
    trial_history=[]
    status="iteration_limit"
    stationary_trial=None
    for iteration in range(1,max_iterations+1):
        if np.linalg.norm(g)<1.0e-5:
            status="converged_at_armijo_state"
            break
        direction=-(H@g)
        if not np.isfinite(direction).all() or float(g@direction)>=0.0:
            H=np.asarray(preconditioner,dtype=np.float64).copy()
            direction=-(H@g)
        slope=float(g@direction)
        step=1.0
        accepted=None
        for trial in range(10):
            candidate=v+step*direction
            try:
                check_initial_admissibility(grid,base+B@candidate,descriptor_data)
            except ValueError:
                step*=0.5
                continue
            proposal=evaluate(exe,grid,outdir,descriptor_data,base,B,candidate,observations,steps,dt)
            evaluations.append(proposal)
            proposal_gradient=project_gradient(proposal,B)
            proposal_gradient_norm=float(np.linalg.norm(proposal_gradient))
            armijo=proposal["objective"]<=current["objective"]+1.0e-4*step*slope
            trial_history.append({"iteration":iteration,"line_search_trial":trial+1,
                "controls":candidate.tolist(),"objective":proposal["objective"],
                "gradient_norm":proposal_gradient_norm,"step":step,"armijo_accepted":bool(armijo)})
            if armijo:
                accepted=proposal
                break
            if proposal_gradient_norm<1.0e-5 and proposal["objective"]<history[0]["objective"]:
                stationary_trial={"controls":candidate,"evaluation":proposal,
                                  "gradient":proposal_gradient,"gradient_norm":proposal_gradient_norm,
                                  "iteration":iteration,"line_search_trial":trial+1}
                status="converged_stationary_trial"
                break
            step*=0.5
        if stationary_trial is not None:
            break
        if accepted is None:
            status="line_search_failed"
            break
        vnew=accepted["controls"]
        gnew=project_gradient(accepted,B)
        s=vnew-v; y=gnew-g; ys=float(y@s)
        if ys>1.0e-14*np.linalg.norm(y)*np.linalg.norm(s):
            rho=1.0/ys; ident=np.eye(v.size)
            H=(ident-rho*np.outer(s,y))@H@(ident-rho*np.outer(y,s))+rho*np.outer(s,s)
        v,current,g=vnew,accepted,gnew
        history.append({"iteration":iteration,"controls":v.tolist(),"objective":current["objective"],
                        "gradient_norm":float(np.linalg.norm(g)),"step":step,
                        "line_search_trials":trial+1})
        if np.linalg.norm(g)<1.0e-5:
            status="converged_at_armijo_state"
            break
    if stationary_trial is not None:
        controls=stationary_trial["controls"]
        terminal_evaluation=stationary_trial["evaluation"]
        terminal_gradient=stationary_trial["gradient"]
        terminal_gradient_norm=stationary_trial["gradient_norm"]
    else:
        controls=v
        terminal_evaluation=current
        terminal_gradient=g
        terminal_gradient_norm=float(np.linalg.norm(g))
        if terminal_gradient_norm<1.0e-5 and status=="iteration_limit":
            status="converged_at_armijo_state"
    converged=status in ("converged_at_armijo_state","converged_stationary_trial")
    return {"controls":controls,"evaluation":terminal_evaluation,"gradient":terminal_gradient,
            "gradient_norm":terminal_gradient_norm,"status":status,"converged":converged,
            "terminal_trial":stationary_trial,"last_armijo_controls":v,
            "last_armijo_evaluation":current,"history":history,"trial_history":trial_history,
            "evaluations":len(evaluations),"final_metric":H,"steps":steps,"dt_s":dt}


def directional_checks(exe: Path, grid: tuple[int,int,int], outdir: Path,
                       descriptor_data: dict, base: np.ndarray, B: np.ndarray,
                       observations: Path,
                       controls: np.ndarray, center: dict,
                       xyz: np.ndarray) -> dict:
    """Check objective VJP and state-dependent H^T with paired FP64 forwards."""
    d=FD_DIRECTION/np.linalg.norm(FD_DIRECTION)
    grad=project_gradient(center,B)
    epsilons=(1.0e-2,5.0e-3)
    rows=[]
    for eps in epsilons:
        plus=evaluate(exe,grid,outdir,descriptor_data,base,B,controls+eps*d,observations,30,10,True)
        minus=evaluate(exe,grid,outdir,descriptor_data,base,B,controls-eps*d,observations,30,10,True)
        pa,ma=plus["arrays"],minus["arrays"]
        fd=(plus["objective"]-minus["objective"])/(2.0*eps)
        ad=float(grad@d)
        pred_dot=sum(float(np.dot(center["arrays"][f"observation_cotangent_{t}"],
                                  pa[f"predicted_{t}"]-ma[f"predicted_{t}"]))/(2.0*eps)
                     for t in (150,300))
        initial_dot=float(center["arrays"]["initial_pullback"]@(B@d))
        brackets_unchanged=True
        for t in (150,300):
            brackets_unchanged &= np.array_equal(center["arrays"][f"bracket_index_{t}"],
                                                  pa[f"bracket_index_{t}"])
            brackets_unchanged &= np.array_equal(center["arrays"][f"bracket_index_{t}"],
                                                  ma[f"bracket_index_{t}"])
        minface=min(plus["meta"]["minimum_height_to_any_W_face_150_m"],
                    plus["meta"]["minimum_height_to_any_W_face_300_m"],
                    minus["meta"]["minimum_height_to_any_W_face_150_m"],
                    minus["meta"]["minimum_height_to_any_W_face_300_m"],
                    center["meta"]["minimum_height_to_any_W_face_150_m"],
                    center["meta"]["minimum_height_to_any_W_face_300_m"])
        if not brackets_unchanged or minface<=0.0:
            raise AssertionError("selected nonzero finite-difference point crossed an H vertical bracket knot")
        rows.append({"epsilon":eps,"objective_fd":fd,"initial_vjp_dot":ad,
                     "observation_cotangent_dot":pred_dot,"initial_directional_dot":initial_dot,
                     "objective_relative_error":abs(fd-ad)/max(abs(fd),abs(ad),np.finfo(float).tiny),
                     "H_transpose_relative_error":abs(pred_dot-initial_dot)/max(abs(pred_dot),abs(initial_dot),np.finfo(float).tiny),
                     "brackets_unchanged_center_plus_minus":bool(brackets_unchanged),
                     "minimum_face_distance_m":float(minface)})
        signal_floor=1024.0*np.finfo(float).eps*max(1.0,plus["objective"],minus["objective"])/eps
        if min(abs(fd),abs(ad),abs(pred_dot),abs(initial_dot))<=signal_floor:
            raise AssertionError(f"selected directional signal is unresolved above FP64 roundoff: {rows[-1]}")
        if rows[-1]["objective_relative_error"]>2.0e-2 or rows[-1]["H_transpose_relative_error"]>2.0e-2:
            raise AssertionError(f"selected finite-difference/VJP check failed at epsilon={eps}: {rows[-1]}")
    # Independent local H^T from the documented bilinear/vertical interpolation.
    # Compare W and PH blocks separately so suppressing PH geometry is detectable.
    nx,ny,nz=grid
    sizes=(ny*nz*(nx+1),(ny+1)*nz*nx,ny*(nz+1)*nx,ny*(nz+1)*nx,ny*nz*nx,ny*nx)
    offsets=np.r_[0,np.cumsum(sizes[:-1])].astype(int)
    h_checks={}
    phb=np.asarray(descriptor_data["arrays"]["phb"],dtype=np.float64).reshape(ny,nz+1,nx)
    for t in (150,300):
        state=center["arrays"][f"checkpoint_{t}"]
        cot=center["arrays"][f"observation_cotangent_{t}"]
        independent=numpy_physical_w_pullback(grid,phb,state,xyz,cot,
                                               float(descriptor_data["column"]["g"]))
        native_cot=center["arrays"][f"checkpoint_cotangent_{t}"]
        o_w,o_ph=int(offsets[2]),int(offsets[3]); end_ph=o_ph+sizes[3]
        wsl=slice(o_w,o_ph); phsl=slice(o_ph,end_ph)
        norm_w=float(np.linalg.norm(native_cot[wsl])); norm_ph=float(np.linalg.norm(native_cot[phsl]))
        norm_total=float(np.linalg.norm(native_cot))
        if min(norm_w,norm_ph,norm_total)<=np.finfo(float).tiny:
            raise AssertionError(f"physical H transpose has an unresolved W/PH block at t={t}")
        err_w=np.linalg.norm(independent[wsl]-native_cot[wsl])/norm_w
        err_ph=np.linalg.norm(independent[phsl]-native_cot[phsl])/norm_ph
        gravity=float(descriptor_data["column"]["g"])
        scaled_w=norm_w*SIGMA
        scaled_ph=norm_ph*gravity
        ph_fraction=scaled_ph/max(np.hypot(scaled_w,scaled_ph),np.finfo(float).tiny)
        if err_w>2.0e-10 or err_ph>2.0e-10:
            raise AssertionError(f"independent physical H^T failed at t={t}: W={err_w:g} PH={err_ph:g}")
        ph_direction=np.zeros_like(state)
        active=int(np.argmax(np.abs(independent[phsl])))
        active_index=o_ph+active
        ph_direction[active_index]=gravity  # one metre of PH-face displacement
        center_margin=float(center["meta"][f"minimum_height_to_any_W_face_{t}_m"])
        eps_ph=min(1.0e-3,0.25*center_margin)
        if eps_ph<=0.0:
            raise AssertionError(f"no positive off-knot PH perturbation is available at t={t}")
        pred_plus,bracket_plus,margin_plus=numpy_physical_w_prediction(
            grid,phb,state+eps_ph*ph_direction,xyz,gravity)
        pred_minus,bracket_minus,margin_minus=numpy_physical_w_prediction(
            grid,phb,state-eps_ph*ph_direction,xyz,gravity)
        if (not np.array_equal(bracket_plus,center["arrays"][f"bracket_index_{t}"]) or
                not np.array_equal(bracket_minus,center["arrays"][f"bracket_index_{t}"]) or
                min(margin_plus,margin_minus)<=0.0):
            raise AssertionError(f"PH-only H directional check crossed a vertical bracket at t={t}")
        fd_ph=float(np.dot(cot,pred_plus-pred_minus)/(2.0*eps_ph))
        independent_ph=float(independent[active_index]*gravity)
        native_ph=float(native_cot[active_index]*gravity)
        signal_scale=max(abs(fd_ph),abs(independent_ph),abs(native_ph))
        roundoff_floor=(128.0*np.finfo(float).eps/eps_ph*
                        float(np.dot(np.abs(cot),np.maximum(np.abs(pred_plus),np.abs(pred_minus)))))
        if signal_scale<=roundoff_floor:
            raise AssertionError(f"PH-only H directional signal is below its operand roundoff floor at t={t}")
        fd_error=abs(fd_ph-native_ph)/signal_scale
        independent_error=abs(independent_ph-native_ph)/signal_scale
        if fd_error>2.0e-5 or independent_error>2.0e-10:
            raise AssertionError(f"PH-only H directional transpose failed at t={t}: FD={fd_error:g}, independent={independent_error:g}")
        h_checks[str(t)]={"W_relative_error":float(err_w),"PH_relative_error":float(err_ph),
                          "scaled_PH_direction_fraction":float(ph_fraction),
                          "PH_active_flat_index":active,"PH_direction_height_m":1.0,
                          "PH_direction_epsilon_m":eps_ph,
                          "PH_native_directional_dot":native_ph,
                          "PH_independent_directional_dot":independent_ph,
                          "PH_finite_difference_directional_dot":fd_ph,
                          "PH_fd_relative_error":float(fd_error),
                          "PH_independent_relative_error":float(independent_error),
                          "PH_operand_roundoff_floor":float(roundoff_floor),
                          "PH_bracket_margin_m":float(min(margin_plus,margin_minus))}
    return {"direction":d.tolist(),"checks":rows,"independent_H_transpose":h_checks}


def numpy_physical_w_pullback(grid: tuple[int,int,int], phb: np.ndarray,
                              state: np.ndarray, xyz: np.ndarray,
                              observation_cotangent: np.ndarray,
                              gravity: float) -> np.ndarray:
    """Analytic NumPy transpose of native corner-wise W/PH interpolation."""
    nx,ny,nz=grid; dx,dy=LENGTHS[0]/nx,LENGTHS[1]/ny
    g=float(gravity)
    sizes=(ny*nz*(nx+1),(ny+1)*nz*nx,ny*(nz+1)*nx,ny*(nz+1)*nx,ny*nz*nx,ny*nx)
    offsets=np.r_[0,np.cumsum(sizes[:-1])].astype(int)
    ow,oph=int(offsets[2]),int(offsets[3]); nface=ny*(nz+1)*nx
    w=state[ow:oph].reshape(ny,nz+1,nx)
    ph=state[oph:oph+nface].reshape(ny,nz+1,nx)
    gw=np.zeros_like(w); gph=np.zeros_like(ph)
    for (x,y,z),cot in zip(xyz,observation_cotangent):
        xloc=(x%LENGTHS[0])/dx-0.5; x0=int(np.floor(xloc)); fx=xloc-x0
        yloc=y/dy-0.5; y0=int(np.floor(yloc)); fy=yloc-y0
        for jj,wy in ((y0,1-fy),(y0+1,fy)):
            for ii0,wx in ((x0,1-fx),(x0+1,fx)):
                ii=ii0%nx; weight=float(cot*wy*wx)
                zf=(phb[jj,:,ii]+ph[jj,:,ii])/g
                lower=int(np.searchsorted(zf,z,side="left")-1)
                lower=max(0,min(lower,nz-1)); dz=zf[lower+1]-zf[lower]
                alpha=(z-zf[lower])/dz
                jump=w[jj,lower+1,ii]-w[jj,lower,ii]
                gw[jj,lower,ii]+=weight*(1-alpha)
                gw[jj,lower+1,ii]+=weight*alpha
                gph[jj,lower,ii]+=weight*(-(1-alpha)*jump/dz)/g
                gph[jj,lower+1,ii]+=weight*(-alpha*jump/dz)/g
    out=np.zeros_like(state)
    out[ow:oph]=gw.ravel(); out[oph:oph+nface]=gph.ravel()
    return out


def numpy_physical_w_prediction(grid: tuple[int,int,int], phb: np.ndarray,
                                state: np.ndarray, xyz: np.ndarray,
                                gravity: float) -> tuple[np.ndarray,np.ndarray,float]:
    """Independent NumPy H and branch metadata for the PH-only transpose check."""
    nx,ny,nz=grid; dx,dy=LENGTHS[0]/nx,LENGTHS[1]/ny; gravity=float(gravity)
    sizes=(ny*nz*(nx+1),(ny+1)*nz*nx,ny*(nz+1)*nx,ny*(nz+1)*nx,ny*nz*nx,ny*nx)
    offsets=np.r_[0,np.cumsum(sizes[:-1])].astype(int)
    ow,oph=int(offsets[2]),int(offsets[3]); nface=ny*(nz+1)*nx
    w=state[ow:oph].reshape(ny,nz+1,nx)
    ph=state[oph:oph+nface].reshape(ny,nz+1,nx)
    pred=np.zeros(xyz.shape[0]); brackets=[]; min_margin=float("inf")
    for row,(x,y,z) in enumerate(xyz):
        xloc=(x%LENGTHS[0])/dx-0.5; x0=int(np.floor(xloc)); fx=xloc-x0
        yloc=y/dy-0.5; y0=int(np.floor(yloc)); fy=yloc-y0
        for jj,wy in ((y0,1-fy),(y0+1,fy)):
            for ii0,wx in ((x0,1-fx),(x0+1,fx)):
                ii=ii0%nx; zf=(phb[jj,:,ii]+ph[jj,:,ii])/gravity
                lower=int(np.searchsorted(zf,z,side="left")-1)
                lower=max(0,min(lower,nz-1)); brackets.append(lower)
                min_margin=min(min_margin,float(np.min(np.abs(zf-z))))
                alpha=(z-zf[lower])/(zf[lower+1]-zf[lower])
                pred[row]+=wy*wx*(w[jj,lower,ii]+alpha*(w[jj,lower+1,ii]-w[jj,lower,ii]))
    return pred,np.asarray(brackets,dtype=np.float64),min_margin


def run(exe: Path, outdir: Path, max_iterations: int=12,
        parity_only: bool=False) -> dict:
    outdir.mkdir(parents=True,exist_ok=True)
    desc={name:descriptor(exe,outdir,grid) for name,grid in GRIDS.items()}
    ccoarse,cfine=desc["coarse"]["column"],desc["fine"]["column"]
    grid_c,grid_f=GRIDS["coarse"],GRIDS["fine"]
    _,basis_c=profile_basis(ccoarse,ccoarse)
    _,basis_f=profile_basis(ccoarse,cfine)
    Bc=np.column_stack([pack_source_mode(q,grid_c) for q in basis_c])
    Bf=np.column_stack([pack_source_mode(q,grid_f) for q in basis_f])
    qtruth=sum((TRUTH[j]*basis_f[j] for j in range(4)),np.zeros(4*int(cfine["nz"])+1,dtype=np.complex128))
    xyz,obs=source_observation_data(cfine,qtruth,grid_c,ccoarse)
    obsfile=outdir/"fixed_physical_observations.txt"
    write_observations(obsfile,xyz,obs)
    source_sensitivity=source_linear_observation_matrix(cfine,basis_f,xyz)
    whitened=source_sensitivity/SIGMA
    source_singular=np.linalg.svd(whitened,compute_uv=False)
    gram=whitened.T@whitened
    gram_eigenvalues=np.linalg.eigvalsh(gram)[::-1]
    if source_singular[-1]<=source_singular[0]*1.0e-12:
        raise AssertionError(f"four-control source observation matrix is rank deficient: {source_singular}")
    H=np.linalg.inv(gram)
    probe=0.5*TRUTH
    # Establish retained-tape / no-pullback parity on nonzero physical initial
    # profiles before optimization or finite-difference checks.
    probe_centers={}
    parity={}
    for name,grid,B in (("fine",grid_f,Bf),("coarse",grid_c,Bc)):
        retained=evaluate(exe,grid,outdir,desc[name],desc[name]["base"],B,probe,obsfile,30,10)
        forward=evaluate(exe,grid,outdir,desc[name],desc[name]["base"],B,probe,obsfile,30,10,True)
        fields=("initial_state","checkpoint_150","checkpoint_300","predicted_150","predicted_300")
        parity[name]={key:bool(np.array_equal(retained["arrays"][key],forward["arrays"][key]))
                      for key in fields}
        parity[name]["objective_exact"]=retained["objective"]==forward["objective"]
        if not all(parity[name].values()):
            raise AssertionError(f"retained/carry FP64 {name} h10 parity failed: {parity[name]}")
        probe_centers[name]=retained
        if parity_only:
            return {"schema":"physical-wave-inverse-parity-v1","grid":list(grid),
                    "controls":probe.tolist(),"objective_retained":retained["objective"],
                    "objective_forward_only":forward["objective"],"exact_parity":parity[name],
                    "retained_csv":str(retained["path"]),"forward_only_csv":str(forward["path"]),
                    "checkpoint_precision_retained":retained["text_meta"]["checkpoint_precision"],
                    "checkpoint_precision_forward":forward["text_meta"]["checkpoint_precision"],
                    "minimum_face_distance_150_m":retained["meta"]["minimum_height_to_any_W_face_150_m"],
                    "minimum_face_distance_300_m":retained["meta"]["minimum_height_to_any_W_face_300_m"]}
    checks={name:directional_checks(exe,grid,outdir,desc[name],desc[name]["base"],B,
                                    obsfile,probe,probe_centers[name],xyz)
            for name,grid,B in (("coarse",grid_c,Bc),("fine",grid_f,Bf))}
    coarse=bfgs_optimize(exe,grid_c,outdir,desc["coarse"],desc["coarse"]["base"],Bc,
                         obsfile,H,max_iterations)
    if not coarse["converged"]:
        raise AssertionError(f"coarse h=10 optimization did not reach the required gradient tolerance: {coarse['status']} ||g||={coarse['gradient_norm']}")
    vstar=coarse["controls"]
    fixed={}
    fixed["coarse_h10"]=coarse["evaluation"]
    fixed["fine_h10"]=evaluate(exe,grid_f,outdir,desc["fine"],desc["fine"]["base"],Bf,vstar,obsfile,30,10)
    fine_fwd=evaluate(exe,grid_f,outdir,desc["fine"],desc["fine"]["base"],Bf,vstar,obsfile,30,10,True)
    fixed["fine_h5"]=evaluate(exe,grid_f,outdir,desc["fine"],desc["fine"]["base"],Bf,vstar,obsfile,60,5,True)
    fineopt=bfgs_optimize(exe,grid_f,outdir,desc["fine"],desc["fine"]["base"],Bf,
                          obsfile,coarse["final_metric"],max_iterations,
                          initial_controls=vstar)
    fine_h5_opt=bfgs_optimize(exe,grid_f,outdir,desc["fine"],desc["fine"]["base"],Bf,
                              obsfile,fineopt["final_metric"],max_iterations,
                              steps=60,dt=5,initial_controls=fineopt["controls"])
    if not fine_h5_opt["converged"]:
        raise AssertionError(f"primary fine h=5 optimization did not reach the required gradient tolerance: {fine_h5_opt['status']} ||g||={fine_h5_opt['gradient_norm']}")
    report={"schema":"physical-wave-inverse-v1","sigma_m_s":SIGMA,
        "truth_controls":TRUTH.tolist(),"fixed_observation_xyz_sha256":digest(xyz),
        "fixed_observation_values_sha256":digest(obs),"fixed_observation_file":str(obsfile),
        "grids":{k:{"shape":list(v),"descriptor":str(desc[k]["path"]),
                    "base_sha256":digest(desc[k]["base"])} for k,v in GRIDS.items()},
        "source_observability":{"matrix_shape":list(source_sensitivity.shape),
            "whitened_A_singular_values":source_singular.tolist(),
            "whitened_A_condition_number":float(source_singular[0]/source_singular[-1]),
            "scaled_gram_eigenvalues":gram_eigenvalues.tolist(),
            "scaled_gram_condition_number":float(gram_eigenvalues[0]/gram_eigenvalues[-1]),
            "source_rank":int(np.linalg.matrix_rank(whitened)),
            "preconditioner":H.tolist()},
        "coarse_optimization":{"status":coarse["status"],"converged":coarse["converged"],
            "controls":vstar.tolist(),"objective":coarse["evaluation"]["objective"],
            "initial_objective":coarse["history"][0]["objective"],"gradient":coarse["gradient"].tolist(),
            "gradient_norm":coarse["gradient_norm"],"steps":coarse["steps"],"dt_s":coarse["dt_s"],
            "final_inverse_metric":coarse["final_metric"].tolist(),
            "control_error_norm_to_source_truth":float(np.linalg.norm(vstar-TRUTH)),
            "accepted_armijo_history":coarse["history"],"line_search_trial_history":coarse["trial_history"],
            "terminal_stationary_trial":None if coarse["terminal_trial"] is None else {
                "controls":coarse["terminal_trial"]["controls"].tolist(),
                "objective":coarse["terminal_trial"]["evaluation"]["objective"],
                "gradient_norm":coarse["terminal_trial"]["gradient_norm"],
                "objective_delta_vs_last_armijo":coarse["terminal_trial"]["evaluation"]["objective"]-coarse["last_armijo_evaluation"]["objective"],
                "objective_delta_vs_initial":coarse["terminal_trial"]["evaluation"]["objective"]-coarse["history"][0]["objective"]},
            "native_evaluations":coarse["evaluations"],
            "directional_checks_at_half_truth":checks["coarse"]},
        "same_control_grid_time_sensitivity":{},"fp64_carry_parity_at_half_truth":parity,
        "fine_directional_checks_at_half_truth":checks["fine"],
        "fine_optimization_diagnostic":{"status":fineopt["status"],
            "converged":fineopt["converged"],"diagnostic_only":True,
            "interpretation":"fine h=10 inverse stationarity is not a required gate",
            "returned_controls":fineopt["controls"].tolist(),
            "initial_controls":fineopt["history"][0]["controls"],
            "initial_inverse_metric":coarse["final_metric"].tolist(),
            "returned_objective":fineopt["evaluation"]["objective"],
            "initial_objective":fineopt["history"][0]["objective"],
            "gradient_norm":fineopt["gradient_norm"],"steps":fineopt["steps"],"dt_s":fineopt["dt_s"],
            "control_error_norm_to_source_truth_diagnostic":float(np.linalg.norm(fineopt["controls"]-TRUTH)),
            "gradient":fineopt["gradient"].tolist(),"accepted_armijo_history":fineopt["history"],
            "line_search_trial_history":fineopt["trial_history"],
            "terminal_stationary_trial":None if fineopt["terminal_trial"] is None else {
                "controls":fineopt["terminal_trial"]["controls"].tolist(),
                "objective":fineopt["terminal_trial"]["evaluation"]["objective"],
                "gradient_norm":fineopt["terminal_trial"]["gradient_norm"],
                "objective_delta_vs_last_armijo":fineopt["terminal_trial"]["evaluation"]["objective"]-fineopt["last_armijo_evaluation"]["objective"],
                "objective_delta_vs_initial":fineopt["terminal_trial"]["evaluation"]["objective"]-fineopt["history"][0]["objective"]},
            "final_inverse_metric":fineopt["final_metric"].tolist(),
            "native_evaluations":fineopt["evaluations"]},
        "fine_h5_optimization":{"controls":fine_h5_opt["controls"].tolist(),
            "controls_source":fine_h5_opt["status"],
            "initial_controls":fine_h5_opt["history"][0]["controls"],
            "initial_inverse_metric":fineopt["final_metric"].tolist(),
            "objective":fine_h5_opt["evaluation"]["objective"],
            "initial_objective":fine_h5_opt["history"][0]["objective"],
            "gradient_norm":fine_h5_opt["gradient_norm"],"steps":fine_h5_opt["steps"],"dt_s":fine_h5_opt["dt_s"],
            "control_change_from_fine_h10_diagnostic":float(np.linalg.norm(fine_h5_opt["controls"]-fineopt["controls"])),
            "status":fine_h5_opt["status"],"converged":fine_h5_opt["converged"],
            "gradient":fine_h5_opt["gradient"].tolist(),
            "accepted_armijo_history":fine_h5_opt["history"],
            "line_search_trial_history":fine_h5_opt["trial_history"],
            "terminal_stationary_trial":None if fine_h5_opt["terminal_trial"] is None else {
                "controls":fine_h5_opt["terminal_trial"]["controls"].tolist(),
                "objective":fine_h5_opt["terminal_trial"]["evaluation"]["objective"],
                "gradient_norm":fine_h5_opt["terminal_trial"]["gradient_norm"],
                "objective_delta_vs_last_armijo":fine_h5_opt["terminal_trial"]["evaluation"]["objective"]-fine_h5_opt["last_armijo_evaluation"]["objective"],
                "objective_delta_vs_initial":fine_h5_opt["terminal_trial"]["evaluation"]["objective"]-fine_h5_opt["history"][0]["objective"]},
            "final_inverse_metric":fine_h5_opt["final_metric"].tolist(),
            "native_evaluations":fine_h5_opt["evaluations"]}}
    baseline=fixed["coarse_h10"]["arrays"]
    for name,item in fixed.items():
        arrays=item["arrays"]
        report["same_control_grid_time_sensitivity"][name]={
            "objective":item["objective"],
            "predicted_150_rms_difference_from_coarse_m_s":float(np.sqrt(np.mean((arrays["predicted_150"]-baseline["predicted_150"])**2))),
            "predicted_300_rms_difference_from_coarse_m_s":float(np.sqrt(np.mean((arrays["predicted_300"]-baseline["predicted_300"])**2))),
            "steps":int(item["meta"]["trajectory_steps"]),"dt_s":float(item["meta"]["trajectory_dt_fp32"]),
            "forward_only":item["meta"]["forward_only"]==1.0,
            "minimum_face_distance_m":min(item["meta"]["minimum_height_to_any_W_face_150_m"],item["meta"]["minimum_height_to_any_W_face_300_m"]),
            "bracket_changes":int(item["meta"]["bracket_corner_changes_150_to_300"])}
    fine10=fixed["fine_h10"]["arrays"]
    fine5=fixed["fine_h5"]["arrays"]
    report["same_control_sensitivity"]={
        "space_fine_h10_minus_coarse_h10":{
            "objective_delta":fixed["fine_h10"]["objective"]-fixed["coarse_h10"]["objective"],
            "rms_150_m_s":float(np.sqrt(np.mean((fine10["predicted_150"]-baseline["predicted_150"])**2))),
            "rms_300_m_s":float(np.sqrt(np.mean((fine10["predicted_300"]-baseline["predicted_300"])**2)))},
        "time_fine_h5_minus_fine_h10":{
            "objective_delta":fixed["fine_h5"]["objective"]-fixed["fine_h10"]["objective"],
            "rms_150_m_s":float(np.sqrt(np.mean((fine5["predicted_150"]-fine10["predicted_150"])**2))),
            "rms_300_m_s":float(np.sqrt(np.mean((fine5["predicted_300"]-fine10["predicted_300"])**2)))} }
    report["reoptimized_control_sensitivity"]={
        "coarse_h10_controls":vstar.tolist(),
        "fine_h10_diagnostic_status":fineopt["status"],
        "fine_h10_diagnostic_controls":fineopt["controls"].tolist(),
        "fine_h5_required_controls":fine_h5_opt["controls"].tolist(),
        "fine_h5_minus_fine_h10_diagnostic_control_norm":float(np.linalg.norm(fine_h5_opt["controls"]-fineopt["controls"])),
        "fine_h5_vs_fine_h10_diagnostic_objective_delta":fine_h5_opt["evaluation"]["objective"]-fineopt["evaluation"]["objective"],
        "fine_h10_exact_optimizer_solution_claimed":False,
        "fine_h10_optimality_uncertainty":"open; h=10 optimizer is diagnostic only"}
    qstar=sum((vstar[j]*basis_f[j] for j in range(4)),
              np.zeros(4*int(cfine["nz"])+1,dtype=np.complex128))
    _,source_at_vstar=source_observation_data(cfine,qstar,grid_c,ccoarse)
    fine_native=fixed["fine_h10"]["arrays"]
    source_vs_native={}
    for idx,t in enumerate((150,300)):
        native_prediction=fine_native[f"predicted_{t}"]
        diff=source_at_vstar[idx]-native_prediction
        source_vs_native[str(t)]={"source_rms_m_s":float(np.sqrt(np.mean(source_at_vstar[idx]**2))),
                                  "native_rms_m_s":float(np.sqrt(np.mean(native_prediction**2))),
                                  "source_minus_native_rms_m_s":float(np.sqrt(np.mean(diff**2))),
                                  "source_minus_native_max_abs_m_s":float(np.max(np.abs(diff)))}
    report["source_vs_native_at_coarse_optimum"]={"times":source_vs_native,
        "source_objective_on_fixed_observations":float(0.5*np.sum(((source_at_vstar-obs)/SIGMA)**2))}
    report["native_files"]={"coarse":str(coarse["evaluation"]["path"]),
        "fine":str(fixed["fine_h10"]["path"]),"fine_h10_forward_only":str(fine_fwd["path"]),
        "fine_h5":str(fixed["fine_h5"]["path"]),
        "fine_h10_diagnostic":str(fineopt["evaluation"]["path"])}
    report["native_files"]["fine_h5_optimized"]=str(fine_h5_opt["evaluation"]["path"])
    revision=subprocess.run(["git","rev-parse","HEAD"],cwd=ROOT,capture_output=True,
                            text=True,check=True).stdout.strip()
    report["artifacts"]={"source_revision":revision,
        "executable":str(exe.resolve()),"executable_sha256":file_digest(exe),
        "cpp_sha256":file_digest(ROOT/"external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"),
        "source_reference_sha256":file_digest(TOOLS/"wave_quadratic_reference.py"),
        "transport_reference_sha256":file_digest(TOOLS/"wave_quadratic_transport.py"),
        "runner_sha256":file_digest(Path(__file__).resolve()),
        "python":platform.python_version(),"numpy":np.__version__}
    return report


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True,help="built test_native_wave_refinement executable")
    parser.add_argument("--output",type=Path,required=True,help="JSON result path (also stores native CSV/text inputs)")
    parser.add_argument("--max-iterations",type=int,default=12)
    parser.add_argument("--parity-only",action="store_true",
                        help="run only the fine-grid h=10 retained/pullback-skipped FP64 parity pair")
    args=parser.parse_args()
    report=run(args.exe,args.output.parent/(args.output.stem+"_artifacts"),
               args.max_iterations,args.parity_only)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    if report["schema"]=="physical-wave-inverse-parity-v1":
        summary={"schema":report["schema"],"grid":report["grid"],
                 "exact_parity":report["exact_parity"],
                 "objective_retained":report["objective_retained"],
                 "objective_forward_only":report["objective_forward_only"]}
    else:
        summary={"coarse_objective":report["coarse_optimization"]["objective"],
                 "fine_h10_diagnostic_status":report["fine_optimization_diagnostic"]["status"],
                 "fine_h10_diagnostic_objective":report["fine_optimization_diagnostic"]["returned_objective"],
                 "fine_h5_objective":report["fine_h5_optimization"]["objective"],
                 "coarse_controls":report["coarse_optimization"]["controls"],
                 "fine_h5_controls":report["fine_h5_optimization"]["controls"]}
    print(json.dumps(summary,sort_keys=True))


if __name__=="__main__":
    main()
