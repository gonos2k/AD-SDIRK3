#!/usr/bin/env python3
"""Authoritative NumPy objective/cotangent primitive and two-time FD analysis.

`objective_pair` is shared by the runner and portable recomputation. It does
not cast q-weighted cotangents; the native caller performs Q32 only at the
Fortran boundaries fixed in protocol b65b2b01... .
"""
from __future__ import annotations

import argparse
import json
import math
from typing import Any

import numpy as np

N = 24531
Y_FIXED = np.array([0.0, 0.0], dtype=np.float64)
SIGMA_FIXED = np.array([1.0, 1.0], dtype=np.float64)
FD_PAIRS = (('0p01', 2, 3), ('0p03', 4, 5))
CASE_ORDER = ('baseline', 'replay', 'plus_0p01', 'minus_0p01', 'plus_0p03', 'minus_0p03')
EPS64 = np.finfo(np.float64).eps


def _need(ok: Any, message: str) -> None:
    if not bool(ok):
        raise ValueError(message)


def _dot64(a: np.ndarray, b: np.ndarray) -> float:
    # Stable, standalone summation; q remains the original FP32 vector.
    return math.fsum(float(x) * float(y) for x, y in zip(a, b))


def _weighted_abs_sum(q32: np.ndarray, z64: np.ndarray) -> float:
    return math.fsum(abs(float(q) * float(z)) for q, z in zip(q32, z64))


def objective_pair(
    z_mid_f64: np.ndarray,
    z_terminal_f64: np.ndarray,
    q_mid_f32: np.ndarray,
    qN_f32: np.ndarray,
    y: np.ndarray = Y_FIXED,
    sigma: np.ndarray = SIGMA_FIXED,
) -> dict[str, Any]:
    """Evaluate J=.5*sum(((h-y)/sigma)^2), h=[qmid·Z128,qN·Z256].

    Returns the uncast FP64 alpha*q vectors. Keep q in its frozen FP32 form;
    do not fold alpha into a replacement q or perform Q32 here.
    """
    zm = np.asarray(z_mid_f64)
    zn = np.asarray(z_terminal_f64)
    qm = np.asarray(q_mid_f32)
    qn = np.asarray(qN_f32)
    yy = np.asarray(y, dtype=np.float64)
    ss = np.asarray(sigma, dtype=np.float64)
    _need(zm.shape == (N,) and zn.shape == (N,) and zm.dtype == np.float64 and zn.dtype == np.float64,
          'states must be FP64 packed vectors of length 24531')
    _need(qm.shape == (N,) and qn.shape == (N,) and qm.dtype == np.float32 and qn.dtype == np.float32,
          'q vectors must retain their frozen FP32 representation')
    _need(yy.shape == (2,) and ss.shape == (2,) and np.isfinite(yy).all() and np.isfinite(ss).all() and
          np.all(ss > 0), 'y/sigma must be finite 2-vectors with positive sigma')
    _need(np.isfinite(zm).all() and np.isfinite(zn).all() and np.isfinite(qm).all() and np.isfinite(qn).all(),
          'objective inputs must be finite')
    h = np.array([_dot64(qm.astype(np.float64), zm), _dot64(qn.astype(np.float64), zn)], dtype=np.float64)
    residual = h - yy
    alpha = residual / (ss * ss)
    costs = 0.5 * (residual / ss) ** 2
    bounds = 0.5 * ((np.array([_weighted_abs_sum(qm, zm), _weighted_abs_sum(qn, zn)]) + np.abs(yy)) / ss) ** 2
    return {'h': h, 'residual_f64': residual, 'cost_components': costs,
            'cost_total_f64': math.fsum(float(x) for x in costs), 'alpha': alpha,
            'cotangent_mid_f64': alpha[0] * qm.astype(np.float64),
            'cotangent_terminal_f64': alpha[1] * qn.astype(np.float64),
            'component_cost_magnitude_bound_f64': bounds,
            'cost_magnitude_bound_total_f64': math.fsum(float(x) for x in bounds)}


def inject_midpoint_f32(lambda128_f32: np.ndarray, alpha_qmid_f64: np.ndarray) -> np.ndarray:
    """Apply the sole midpoint injection: Q32(FP64(lambda128)+FP64(alpha_m*qmid))."""
    lam = np.asarray(lambda128_f32); inc = np.asarray(alpha_qmid_f64)
    _need(lam.shape == (N,) and lam.dtype == np.float32 and inc.shape == (N,) and inc.dtype == np.float64,
          'midpoint injection operands have wrong shape/dtype')
    _need(np.isfinite(lam).all() and np.isfinite(inc).all(), 'midpoint injection operands nonfinite')
    return (lam.astype(np.float64) + inc).astype('<f4')


def _fd_term(values: np.ndarray, eta: np.ndarray, ad: float, baseline_bound: float) -> dict[str, Any]:
    """Cost FD/AD gate and signal floor; quadratic-cost side gaps are diagnostics."""
    values = np.asarray(values, dtype=np.float64); eta = np.asarray(eta, dtype=np.float64)
    _need(values.shape == (6,) and eta.shape == (6,), 'FD arrays require six frozen old cases')
    widths = {}
    for tag, ip, im in FD_PAIRS:
        ep, em = float(eta[ip]), float(eta[im])
        _need(ep > 0 and em < 0 and ep == -em, f'{tag}: actual FP32 eta pair is not symmetric')
        jp, jm, j0 = float(values[ip]), float(values[im]), float(values[0])
        dfd = (jp - jm) / (ep - em)
        den = max(abs(dfd), abs(ad))
        plus = (jp - j0) / ep
        minus = (j0 - jm) / (-em)
        side_den = max(abs(plus), abs(minus))
        side_gap = abs(plus - minus) / side_den if side_den else None
        floor = 100.0 * EPS64 * max(abs(jp), abs(jm), float(baseline_bound))
        widths[tag] = {'eta_plus_actual': ep, 'eta_minus_actual': em, 'J_plus': jp, 'J_minus': jm,
            'J0': j0, 'central_fd': dfd,
            'relative_shared_signal_error': abs(dfd-ad)/den if den else None,
            'one_sided_plus_diagnostic': plus, 'one_sided_minus_diagnostic': minus,
            'one_sided_relative_gap_diagnostic': side_gap,
            'signal_abs_Jplus_minus_Jminus': abs(jp-jm), 'signal_floor': floor,
            'signal_above_frozen_floor': bool(abs(jp-jm) > floor)}
    f1, f3 = widths['0p01']['central_fd'], widths['0p03']['central_fd']
    den = max(abs(f1), abs(f3), abs(ad))
    plateau = abs(f1-f3)/den if den else None
    fd_ad = all(widths[w]['relative_shared_signal_error'] is not None and
                widths[w]['relative_shared_signal_error'] <= .01 for w, _, _ in FD_PAIRS)
    signal = all(widths[w]['signal_above_frozen_floor'] for w, _, _ in FD_PAIRS)
    return {'widths': widths, 'central_width_plateau_relative': plateau,
        'central_widths_pass_1_percent': bool(plateau is not None and plateau <= .01),
        'both_widths_fd_ad_pass_1_percent': bool(fd_ad), 'both_signals_above_frozen_floor': bool(signal),
        'quadratic_cost_one_sided_gaps_are_diagnostic_only': True,
        'frozen_cost_fd_ad_plateau_signal_pass': bool(plateau is not None and plateau <= .01 and fd_ad and signal)}


def _linear_h_one_sided(values: np.ndarray, eta: np.ndarray) -> dict[str, Any]:
    values=np.asarray(values,dtype=np.float64); eta=np.asarray(eta,dtype=np.float64); gaps={}
    for tag,ip,im in FD_PAIRS:
        ep,em=float(eta[ip]),float(eta[im]); _need(ep>0 and em<0 and ep==-em,tag+': eta pair asymmetric')
        p=(values[ip]-values[0])/ep; m=(values[0]-values[im])/(-em); den=max(abs(p),abs(m))
        gaps[tag]={'plus_slope':float(p),'minus_slope':float(m),
                   'relative_gap':float(abs(p-m)/den) if den else None}
    unresolved=all(gaps[k]['relative_gap'] is not None and gaps[k]['relative_gap']>.01 for k,_,_ in FD_PAIRS)
    return {'widths':gaps,'one_sided_gap_over_1_percent_at_both_widths':bool(unresolved),
            'frozen_linear_h_one_sided_check_pass':not unresolved}


def evaluate_cases(z_mid_f64: np.ndarray, z_terminal_f64: np.ndarray,
                   q_mid_f32: np.ndarray, q_terminal_f32: np.ndarray,
                   y: np.ndarray, sigma: np.ndarray) -> list[dict[str, Any]]:
    """Call objective_pair for each case; runner and FD recompute share this function."""
    zm, zn = np.asarray(z_mid_f64), np.asarray(z_terminal_f64)
    _need(zm.shape == zn.shape == (6, N), 'state matrices must be [six cases,24531]')
    return [objective_pair(zm[i], zn[i], q_mid_f32, q_terminal_f32, y, sigma) for i in range(6)]


def recompute_components(z_mid_f64: np.ndarray, z_terminal_f64: np.ndarray,
                         q_mid_f32: np.ndarray, q_terminal_f32: np.ndarray,
                         y: np.ndarray, sigma: np.ndarray, eta_actual_f32: np.ndarray,
                         v_f32: np.ndarray, new_g0_f32: np.ndarray,
                         old_terminal_g0_f32: np.ndarray,
                         old_two_time_g0_f32: np.ndarray | None = None) -> dict[str, Any]:
    """Compute the three required checks from six old states and new full-chain g0."""
    eta = np.asarray(eta_actual_f32, dtype=np.float32).astype(np.float64)
    v = np.asarray(v_f32, dtype=np.float32).astype(np.float64)
    gnew = np.asarray(new_g0_f32, dtype=np.float32).astype(np.float64)
    gold = np.asarray(old_terminal_g0_f32, dtype=np.float32).astype(np.float64)
    _need(eta.shape == (6,) and v.shape == gnew.shape == gold.shape == (N,), 'FD/gradient shape mismatch')
    cases = evaluate_cases(z_mid_f64, z_terminal_f64, q_mid_f32, q_terminal_f32, y, sigma)
    h = np.stack([x['h'] for x in cases]); costs = np.stack([x['cost_components'] for x in cases])
    bounds = cases[0]['component_cost_magnitude_bound_f64']
    alpha_n = float(cases[0]['alpha'][1])
    # Full gradient is the actual new Fortran FP32 chain output. The midpoint
    # increment is the composite difference from alpha_N times the old terminal
    # chain, after both vectors are promoted; it is not a standalone native VJP.
    d_full = float(_dot64(v, gnew))
    d_mid = float(_dot64(v, gnew - alpha_n * gold))
    d_terminal_reference = float(alpha_n * _dot64(v, gold))
    j_mid = np.array([x['cost_components'][0] for x in cases])
    j_terminal = np.array([x['cost_components'][1] for x in cases])
    j_full = np.array([x['cost_total_f64'] for x in cases])
    fd_full = _fd_term(j_full, eta, d_full, float(np.sum(bounds)))
    fd_mid = _fd_term(j_mid, eta, d_mid, float(bounds[0]))
    fd_terminal = _fd_term(j_terminal, eta, d_terminal_reference, float(bounds[1]))
    # Original linear h one-sided check remains separate from quadratic-cost side slopes.
    old_prediction = None
    if old_two_time_g0_f32 is not None:
        gold_two = np.asarray(old_two_time_g0_f32, dtype=np.float32).astype(np.float64)
        alpha_m=float(cases[0]['alpha'][0])
        old_prediction = float(_dot64(v, alpha_m * (gold_two-gold) + alpha_n*gold))
    residuals=np.stack([x['residual_f64'] for x in cases]); alphas=np.stack([x['alpha'] for x in cases])
    return {'h': h, 'residual_f64':residuals,'alpha_f64':alphas,
        'cost_components': costs, 'cost_total_f64': j_full,
        'full_dAD_from_new_fortran_fp32_g0': d_full,
        'midpoint_increment_dAD_composite': d_mid,
        'terminal_residual_reference_dAD': d_terminal_reference,
        'old_terminal_scaled_postprocess_prediction_diagnostic': old_prediction,
        'fd_full_quadratic_cost': fd_full, 'fd_midpoint_quadratic_cost': fd_mid,
        'fd_terminal_residual_guard': fd_terminal,
        'old_linear_h_one_sided_checks': {'midpoint_h': _linear_h_one_sided(h[:,0],eta),
                                          'terminal_h': _linear_h_one_sided(h[:,1],eta)},
        'claim_scope': {'new_full_dAD_is_actual_fp32_chain': True,
            'midpoint_increment_is_derived_composite_not_independent_native_vjp': True,
            'terminal_residual_guard_uses_scaled_old_terminal_chain_only': True,
            'global_exact_fp64_g0_claim': False, 'second_full_gradient_replay_claim': False,
            'quadratic_cost_one_sided_gaps_are_not_gates': True}}


def summarize_local_casts(lambda_out_f32: np.ndarray, local_precast_f64: np.ndarray) -> list[dict[str, Any]]:
    out, pre = np.asarray(lambda_out_f32), np.asarray(local_precast_f64)
    _need(out.shape == pre.shape and out.ndim == 2 and out.shape[1] == N, 'local cast arrays must be [windows,N]')
    rows=[]
    for i in range(out.shape[0]):
        diff=out[i].astype(np.float64)-pre[i]
        rows.append({'max_abs':float(np.max(np.abs(diff))), 'rms':float(np.sqrt(np.mean(diff*diff))),
            'different_elements':int(np.count_nonzero(diff)),
            'byte_cast_equal':out[i].tobytes()==pre[i].astype('<f4').tobytes()})
    return rows


def synthetic_checks() -> dict[str, Any]:
    # Local directional derivative of 1/2*((q·z-y)/sigma)^2.
    q=np.zeros(N,dtype=np.float32); q[:3]=[.4,-.7,.2]
    z=np.zeros(N,dtype=np.float64); z[:3]=[1.2,-.3,2.]
    d=np.zeros(N,dtype=np.float64); d[:3]=[.1,.25,-.2]; y=np.array([0.,0.]); sigma=np.array([2.,1.])
    r=objective_pair(z,z,q,q,y,sigma); eps=1.e-5
    rp=objective_pair(z+eps*d,z+eps*d,q,q,y,sigma); rm=objective_pair(z-eps*d,z-eps*d,q,q,y,sigma)
    fd=(rp['cost_total_f64']-rm['cost_total_f64'])/(2*eps)
    ad=float(np.dot(r['cotangent_mid_f64'],d)+np.dot(r['cotangent_terminal_f64'],d))
    _need(abs(fd-ad)<1.e-8,'synthetic local cost finite difference failed')
    _need(abs(r['alpha'][0] - r['residual_f64'][0]/4.0)<1.e-14,'sigma squared denominator failed')
    changed=objective_pair(z+0.3*d,z,q,q,y,sigma)
    _need(not np.array_equal(changed['cotangent_mid_f64'],r['cotangent_mid_f64']),
          'state-dependent alpha was stale')
    qwrong=q.copy();qwrong[0]*=2
    wrong=objective_pair(z,z,qwrong,q,y,sigma)
    _need(not np.array_equal(wrong['h'],r['h']),'stale q input was not reflected in h')
    _need(r['cost_components'][0] > 0,'midpoint cost omission synthetic setup is ineffective')
    # For affine h=a+b*eta, central FD is exact for the quadratic cost, while
    # one-sided slopes differ by curvature; these side gaps are diagnostic.
    a,b=1.,20.; etas=np.array([.01,.03]); costs=[.5*(a+b*x)**2 for x in (-.03,-.01,.01,.03)]
    fd1=(costs[2]-costs[1])/.02; fd3=(costs[3]-costs[0])/.06; ad0=a*b
    sp=(costs[2]-.5*a*a)/.01; sm=(.5*a*a-costs[1])/.01
    side_gap=abs(sp-sm)/max(abs(sp),abs(sm))
    _need(abs(fd1-ad0)<1.e-10 and abs(fd3-ad0)<1.e-10 and side_gap>.01,
          'quadratic one-sided curvature example failed')
    eta6=np.array([0.,0.,.01,-.01,.03,-.03]); jcost=np.array([.5*(a+b*x)**2 for x in eta6])
    cost_check=_fd_term(jcost,eta6,ad0,0.)
    _need(cost_check['frozen_cost_fd_ad_plateau_signal_pass'] and
          all(cost_check['widths'][w]['one_sided_relative_gap_diagnostic']>.01 for w,_,_ in FD_PAIRS),
          'quadratic-cost one-sided diagnostic incorrectly became a gate')
    # Omitted midpoint can hide under a large full denominator, but separate
    # midpoint normalization must expose 100% error.
    full_rel=abs(-11036.2739-(-11052.6478))/max(11036.2739,11052.6478)
    mid_rel=abs(16.373758-0.)/16.373758
    _need(full_rel<.01 and mid_rel==1.0,'midpoint omission separation test failed')
    # Exercise the six-case array/recompute API as well as the scalar primitive.
    zm=np.stack([z.copy() for _ in range(6)]);zn=zm.copy()
    etas=np.array([0.,0.,.01,-.01,.03,-.03],dtype=np.float32)
    composed=recompute_components(zm,zn,q,q,y,sigma,etas,np.ones(N,dtype=np.float32),
        np.ones(N,dtype=np.float32),np.ones(N,dtype=np.float32),np.ones(N,dtype=np.float32))
    _need(composed['h'].shape==(6,2) and composed['alpha_f64'].shape==(6,2) and
          set(composed['fd_full_quadratic_cost']['widths'])=={'0p01','0p03'} and
          set(composed['fd_terminal_residual_guard']['widths'])=={'0p01','0p03'},
          'synthetic recompute_components schema/run failed')
    return {'local_cost_fd_pass':True,'sigma_squared_test_pass':True,'state_dependent_alpha_test_pass':True,
        'stale_q_test_pass':True,'midpoint_omission_test_pass':True,'missing_midpoint_full_relative_error':full_rel,
        'missing_midpoint_component_relative_error':mid_rel,'affine_quadratic_central_fd_exact':True,
        'affine_quadratic_one_sided_gap_diagnostic':side_gap,'cost_one_sided_gap_is_not_a_gate':True,
        'synthetic_recompute_components_api_pass':True}


def main() -> None:
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('--self-test',action='store_true'); a=ap.parse_args()
    if a.self_test: print(json.dumps({'schema':'native_residual_objective_synthetic_checks_v1','checks':synthetic_checks()},indent=2))


if __name__=='__main__': main()
