"""Independent design checks; NOT WRF, its boundaries, or a WRF speed benchmark.
Requires numpy/scipy. Run: python design_checks.py --output calculations.json
The pressure example uses periodic x, reflecting y, and a rigid z boundary.
The real WRF upper-boundary operator MUST replace that example boundary.
"""
from __future__ import annotations
import argparse,json,math
from pathlib import Path
import numpy as np
from scipy.fft import rfft,irfft,dct,idct
from scipy.integrate import solve_ivp
from scipy.linalg import solve
from scipy.optimize import brentq
from scipy.sparse import csr_matrix,eye,bmat
from scipy.sparse.linalg import spsolve

AE=np.array([[0,0,0,0],[1767732205903/2027836641118,0,0,0],
 [5535828885825/10492691773637,788022342437/10882634858940,0,0],
 [6485989280629/16251701735622,-4246266847089/9704473918619,10755448449292/10357097424841,0]],float)
AI=np.array([[0,0,0,0],[1767732205903/4055673282236,1767732205903/4055673282236,0,0],
 [2746238789719/10658868560708,-640167445237/6845629431997,1767732205903/4055673282236,0],
 [1471266399579/7840856788654,-4482444167858/7529755066697,11266239266428/11593286722821,1767732205903/4055673282236]],float)
b=AI[-1].copy(); gamma=AI[1,1]

def apply_s(p,alpha,dx,dy,zedge):
    # positive flux Laplacian, independent of the transform implementation
    kx=(2*p-np.roll(p,1,axis=2)-np.roll(p,-1,axis=2))/dx**2
    ky=np.zeros_like(p); dif=(p[:,1:,:]-p[:,:-1,:])/dy**2
    ky[:,:-1,:]-=dif; ky[:,1:,:]+=dif
    kz=np.zeros_like(p); dif=(p[1:]-p[:-1])*zedge[:,None,None]
    kz[:-1]-=dif; kz[1:]+=dif
    return p+alpha*(kx+ky+kz)

def pressure_solve(rhs,alpha,dx,dy,zedge):
    """Separable horizontal reference + independent tridiagonal vertical solves."""
    nz,ny,nx=rhs.shape
    lamx=4*np.sin(np.pi*np.arange(nx//2+1)/nx)**2/dx**2
    lamy=4*np.sin(np.pi*np.arange(ny)/(2*ny))**2/dy**2
    r=dct(rfft(rhs,axis=2,norm='ortho'),type=2,axis=1,norm='ortho')
    zdiag=np.r_[zedge,0]+np.r_[0,zedge]
    diag=1+alpha*(zdiag[:,None,None]+lamy[None,:,None]+lamx[None,None,:])
    up=-alpha*zedge
    cp=np.empty_like(diag[:-1]); dp=np.empty_like(r)
    cp[0]=up[0]/diag[0]; dp[0]=r[0]/diag[0]
    for k in range(1,nz):
        pivot=diag[k]-up[k-1]*cp[k-1]
        if not np.all(pivot>0): raise ArithmeticError('nonpositive SPD pivot')
        if k<nz-1: cp[k]=up[k]/pivot
        dp[k]=(r[k]-up[k-1]*dp[k-1])/pivot
    x=np.empty_like(r);x[-1]=dp[-1]
    for k in range(nz-2,-1,-1): x[k]=dp[k]-cp[k]*x[k+1]
    return irfft(idct(x,type=2,axis=1,norm='ortho'),n=nx,axis=2,norm='ortho')

def small_divergence(nx,ny,nz,dx,dy,dz):
    # incidence matrix from periodic x faces, interior y/z faces into cells
    rows=[];cols=[];vals=[]; face=0
    def idx(k,j,i):return (k*ny+j)*nx+i
    for k in range(nz):
      for j in range(ny):
       for i in range(nx):
        rows.extend([idx(k,j,i),idx(k,j,(i+1)%nx)]);cols.extend([face,face]);vals.extend([1/dx,-1/dx]);face+=1
    for k in range(nz):
      for j in range(ny-1):
       for i in range(nx):
        rows.extend([idx(k,j,i),idx(k,j+1,i)]);cols.extend([face,face]);vals.extend([1/dy,-1/dy]);face+=1
    for k in range(nz-1):
      for j in range(ny):
       for i in range(nx):
        rows.extend([idx(k,j,i),idx(k+1,j,i)]);cols.extend([face,face]);vals.extend([1/dz,-1/dz]);face+=1
    return csr_matrix((vals,(rows,cols)),shape=(nx*ny*nz,face))

def fixed_reference_ark(y,h):
    # Reference and L are frozen for ALL four stages of this macrostep.
    ref=y.copy(); L=np.array([[-6.,6*np.cos(ref[1])],[0.,0.]])
    def f(v):return np.array([-6*(v[0]-np.sin(v[1])),-.4*v[1]+.1*v[0]**2])
    ksI=[];ksE=[]
    for i in range(4):
        base=y.copy()
        for j in range(i):base+=h*(AE[i,j]*ksE[j]+AI[i,j]*ksI[j])
        delta=solve(np.eye(2)-h*AI[i,i]*L,base-ref)
        Yi=ref+delta; fi=L@delta;fe=f(Yi)-fi
        ksI.append(fi);ksE.append(fe)
    return y+h*sum(b[j]*(ksI[j]+ksE[j]) for j in range(4))

def main():
    rng=np.random.default_rng(3417); out={}
    c=340.; h=600.; a=gamma*h;alpha=(a*c)**2
    nx,ny,nz=40,80,64;dx=dy=1e5;dz=250.
    zedge=(1+0.3*np.sin(np.pi*(np.arange(nz-1)+.5)/(nz-1)))/dz**2
    p=rng.normal(size=(nz,ny,nx));rhs=apply_s(p,alpha,dx,dy,zedge)
    ph=pressure_solve(rhs,alpha,dx,dy,zedge)
    out['pressure_reference']={'shape':list(p.shape),'unknowns':p.size,'gamma':gamma,
      'dt_seconds':h,'sound_speed_assumed_m_s':c,'dz_example_m':dz,
      'relative_solution_error':float(np.linalg.norm(ph-p)/np.linalg.norm(p)),
      'relative_true_residual':float(np.linalg.norm(apply_s(ph,alpha,dx,dy,zedge)-rhs)/np.linalg.norm(rhs)),
      'boundary_scope':'synthetic periodic x / reflecting y / rigid z; NOT native WRF top',
      'runtime_claim':'No timing or native speedup claim'}
    # Independent coupled solve vs pressure elimination on a smaller geometry
    D=small_divergence(6,5,4,1e5,1e5,250.);G=-D.T;nc,nv=D.shape
    K=bmat([[eye(nv),a*c*G],[a*c*D,eye(nc)]],format='csr')
    bu=rng.normal(size=nv);bp=rng.normal(size=nc);bb=np.r_[bu,bp]
    full=spsolve(K,bb);S=eye(nc)+alpha*(D@D.T)
    ps=spsolve(S,bp-a*c*(D@bu));us=bu-a*c*(G@ps);z=np.r_[us,ps]
    eig=np.linalg.eigvalsh(S.toarray())
    out['block_schur_equivalence']={'full_unknowns':int(nc+nv),'pressure_unknowns':int(nc),
      'relative_solution_difference':float(np.linalg.norm(z-full)/np.linalg.norm(full)),
      'relative_block_residual':float(np.linalg.norm(K@z-bb)/np.linalg.norm(bb)),
      'schur_min_eigenvalue':float(eig.min()),'schur_max_eigenvalue':float(eig.max())}
    # Pure horizontal acoustics remaining explicit under a HEVI alternative.
    def amp(beta):
        q=solve(np.eye(4,dtype=complex)-1j*beta*AE,np.ones(4,dtype=complex))
        return abs(1+1j*beta*b@q)
    grid=np.linspace(.01,10,10000);start=next(i for i,x in enumerate(grid) if amp(x)>1+1e-10)
    beta=brentq(lambda x:amp(x)-1,grid[start-1],grid[start])
    theta=2*np.pi*np.arange(ny)/ny
    out['horizontal_acoustic_example']={'explicit_imaginary_axis_limit':beta,
      'C_at_600s':c*h/dy,'Cgrid_max_h_omega':2*c*h/dy,
      'explicit_ARK_amplification_at_Cgrid_Nyquist':amp(2*c*h/dy),
      'one_dimensional_HEVI_dt_limit_example_s':beta*dy/(2*c),
      'staggered_nyquist_gradient_magnitude':2/dy,
      'collocated_centered_nyquist_gradient_magnitude':abs(np.sin(np.pi))/dy,
      'qualification':'constant coefficients, no filters, 1D horizontal wave; not full WRF stability'}
    f=lambda t,v:np.array([-6*(v[0]-np.sin(v[1])),-.4*v[1]+.1*v[0]**2])
    y0=np.array([.2,.7]);T=1.;exact=solve_ivp(f,(0,T),y0,method='DOP853',rtol=2.3e-14,atol=2.3e-14).y[:,-1]
    levels=[10,20,40,80,160];errs=[]
    for n in levels:
        y=y0.copy()
        for k in range(n):y=fixed_reference_ark(y,T/n)
        errs.append(float(np.linalg.norm(y-exact)))
    out['macrostep_fixed_reference_order']={'steps':levels,'errors':errs,
      'observed_orders':list(np.log2(np.array(errs[:-1])/errs[1:])),
      'scope':'smooth nonlinear manufactured ODE; macrostep-fixed affine IMEX split; NOT WRF order proof'}
    nstate=ny*nz*(nx+1)+(ny+1)*nz*nx+2*ny*(nz+1)*nx+ny*nz*nx+ny*nx
    out['cost_arithmetic']={'native_shape_illustration_state_scalars':nstate,
      'pressure_scalars':nx*ny*nz,'V_Z_bytes_restart30':nstate*8*61,
      'old_paired_same_dt_cost_ratio':2024.4392/4.5668,
      'required_timestep_for_ratio_alone_s':600*2024.4392/4.5668,
      'note':'historic paired timing, not a current benchmark; step-count ratios do not predict speedups'}
    for key in ['pressure_reference','block_schur_equivalence']:print(key,out[key])
    print('explicit',out['horizontal_acoustic_example']);print('order',out['macrostep_fixed_reference_order'])
    assert out['pressure_reference']['relative_true_residual']<1e-10
    assert out['block_schur_equivalence']['relative_block_residual']<1e-8
    assert min(out['macrostep_fixed_reference_order']['observed_orders'][-2:])>2.8
    p=argparse.ArgumentParser();p.add_argument('--output',default=str(Path(__file__).with_name('calculations.json')));args=p.parse_args()
    Path(args.output).write_text(json.dumps(out,indent=2,ensure_ascii=False)+'\n')
if __name__=='__main__':main()
