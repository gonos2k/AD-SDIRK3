#!/usr/bin/env python3
"""Offline verification of the compact native idealized-twin artifact.

Requires only NumPy and the files in this directory. It does not read WRF,
repository source, raw checkpoints, or native executables.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys

import numpy as np


ROOT = Path(__file__).resolve().parent
NPZ = ROOT / 'native_idealized_twin_portable.npz'
MANIFEST = ROOT / 'manifest.json'
PROTOCOL = ROOT / 'protocol.json'
PROOF = ROOT / 'proof_192_equivalence.json'
OUT = ROOT / 'portable_verification.json'
EPS = np.finfo(np.float64).eps
FIELDS = ('U', 'V', 'W', 'PH', 'T', 'MU')
OBS_FIELD = {0: 'U', 1: 'V', 2: 'theta'}


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def require(condition: bool, message: str) -> None:
    if not bool(condition):
        raise ValueError(message)


def close(a, b, *, rtol=5e-12, atol=5e-12) -> bool:
    return bool(np.allclose(a, b, rtol=rtol, atol=atol, equal_nan=False))


def main() -> None:
    manifest = json.loads(MANIFEST.read_text())
    protocol = json.loads(PROTOCOL.read_text())
    proof = json.loads(PROOF.read_text())
    recalc = json.loads((ROOT / 'recomputed.json').read_text())
    require(recalc.get('overall_recomputed_pass') is True and
            all(recalc['acceptance_recomputed'].values()), 'published independent acceptance gates are not all true')
    require(manifest['schema'] == 'native_idealized_twin_portable_manifest_v1', 'bad manifest schema')
    require(manifest['portable_npz']['sha256'] == sha(NPZ), 'portable NPZ hash mismatch')
    require(manifest['portable_protocol']['sha256'] == sha(PROTOCOL), 'protocol hash mismatch')
    require(manifest['profiled_192_case_specific_equivalence']['sha256'] == sha(PROOF) ==
            '405942141aeada1146cecff380b0e4b0a44463a1b8c5a87f9f1202548f439f7d',
            'single profiled-192 proof hash mismatch')
    require(proof['schema'] == 'native_mpi_finalize_observer_equivalence_v1' and
            proof['prior_failure_cause_established'] is False and
            proof['remaining_calculations_use_base_executable'] is True,
            'profiled-192 proof has unexpected scope')
    provenance = manifest['execution_provenance']
    require(provenance['executable']['sha256'] == proof['base_executable']['sha256'] and
            provenance['protocol']['sha256'] == sha(PROTOCOL) and
            provenance['source_files'] and provenance['input_pins'] and
            provenance['artifact_manifest']['sha256'] and provenance['build_receipt']['sha256'],
            'manifest lacks exact executable/source/input/build provenance')
    require(proof['call_identity']['expected_steps'] == 192 and
            proof['call_identity']['protocol_sha256'] == sha(PROTOCOL) and
            proof['failed_case_receipt']['sha256'] and proof['normal_case_receipt']['sha256'],
            'profiled-192 proof does not pin the one replaced call')
    require(protocol['status'] == 'frozen_ready' and
            sha(PROTOCOL) == manifest['protocol_sha256'], 'protocol is not the pinned frozen protocol')
    validation_records = {}
    for source_name, item in manifest['portable_validation_records'].items():
        path = ROOT / item['path']
        require(path.is_file() and sha(path) == item['sha256'] and
                item['source_sha256'] == manifest['validation_result_pins'][source_name]['sha256'],
                f'portable validation record pin mismatch: {source_name}')
        validation_records[source_name] = json.loads(path.read_text())

    with np.load(NPZ, allow_pickle=False) as z:
        arrays = {name: z[name] for name in z.files}
    for name, pin in manifest['arrays'].items():
        arr = arrays[name]
        require(str(arr.dtype) == pin['dtype'] and list(arr.shape) == pin['shape'] and
                hashlib.sha256(np.ascontiguousarray(arr).tobytes()).hexdigest() == pin['sha256'],
                f'NPZ array pin mismatch: {name}')
        require(np.isfinite(arr).all() if arr.dtype.kind in 'fc' else True,
                f'nonfinite value in NPZ array {name}')

    labels = arrays['case_labels'].tolist()
    state_steps = arrays['state_steps_i32']
    states = arrays['states_f64']
    truth = arrays['truth_states_f64']
    controls = arrays['controls_f64']
    P = arrays['P_f64']
    slots = arrays['observation_slots_i32']
    obs_y = arrays['observation_values_f64']
    sigma = arrays['observation_sigma_f64']
    obs_step = arrays['observation_step_i32']
    obs_field = arrays['observation_field_i8']
    owner = arrays['owner_masks_bool']
    observed = arrays['observed_masks_bool']
    epsilon = arrays['epsilon_f64']
    require(tuple(state_steps) == (0, 64, 128, 192, 256), 'unexpected checkpoint times')
    require(states.shape == (len(labels), 5, P.shape[1]) and truth.shape == (5, P.shape[1]),
            'unexpected state/truth dimensions')
    idx = {int(s): i for i, s in enumerate(state_steps)}
    case = {label: i for i, label in enumerate(labels)}
    require(len(case) == len(labels) and P.shape[0] == 2 and len(slots) == len(obs_y) == len(sigma) == 384,
            'unexpected case/control/observation dimensions')

    # H reads the frozen packed observation slots; theta is stored as T+300 K.
    def H(state: np.ndarray, step: int, *, affine: bool = True) -> np.ndarray:
        mask = obs_step == step
        out = state[slots[mask]].copy()
        if affine:
            out[obs_field[mask] == 2] += 300.0
        return out

    # Ht is the exact scatter transpose of H, before sigma weighting.
    def Ht(values: np.ndarray, step: int) -> np.ndarray:
        mask = obs_step == step
        out = np.zeros(P.shape[1], dtype=np.float64)
        np.add.at(out, slots[mask], values)
        return out

    ht_dot_errors = {}
    rng = np.random.default_rng(20261023)
    probe = rng.standard_normal(P.shape[1])
    for step in (128, 256):
        mask = obs_step == step
        y = rng.standard_normal(int(mask.sum()))
        lhs = float(np.dot(H(probe, step, affine=False), y))
        rhs = float(np.dot(probe, Ht(y, step)))
        ht_dot_errors[str(step)] = abs(lhs-rhs) / max(1.0, abs(lhs), abs(rhs))
    require(max(ht_dot_errors.values()) < 2e-14, 'H/Ht dot-product identity failed')

    costs: dict[str, dict[int, float]] = {}
    qs: dict[str, dict[int, np.ndarray]] = {}
    for label in labels:
        costs[label] = {}; qs[label] = {}
        for step in (128, 256):
            mask = obs_step == step
            residual = H(states[case[label], idx[step]], step) - obs_y[mask]
            q = Ht(residual / np.square(sigma[mask]), step)
            costs[label][step] = 0.5 * float(np.dot(residual/sigma[mask], residual/sigma[mask]))
            qs[label][step] = q
    objective_rows = {x['case']: x for x in recalc['J_by_case']}
    for label in labels:
        total = sum(costs[label].values())
        require(close(total, objective_rows[label]['Jtotal']), f'objective mismatch: {label}')
    for label in labels:
        require(close(controls[case[label]], objective_rows[label]['control']), f'control mismatch: {label}')

    lam = arrays['lambda0_f32'].astype(np.float64)
    require(lam.shape == (3, P.shape[1]), 'lambda0 rows do not match E0/background/final')
    require(arrays['lambda0_labels'].tolist() == ['e0_initial','e1_background_initial','e1_final_initial'],
            'lambda0 rows do not identify E0/background/final')
    pdot = P @ lam.T
    e0, bg, final = pdot.T
    terminal = P @ arrays['terminal_component_lambda0_f32'].astype(np.float64)
    mid = bg - terminal
    require(close(e0, recalc['E0']['gradient_control']), 'E0 Pdot(lambda0) mismatch')
    require(close(bg, recalc['E1']['background_gradient_control']), 'background Pdot(lambda0) mismatch')
    require(close(final, recalc['E1']['final_gradient_control']), 'final Pdot(lambda0) mismatch')

    # Build weighted observation Jacobians from the frozen +/- control pairs.
    H_width = {}
    rank = []
    for width, tag in ((.01, '01'), (.03, '03')):
        cols = []
        for axis in (0, 1):
            plus = f'fd_a{axis}_{tag}_plus'; minus = f'fd_a{axis}_{tag}_minus'
            delta = controls[case[plus], axis] - controls[case[minus], axis]
            pieces = []
            for step in (128, 256):
                mask = obs_step == step
                pieces.append((H(states[case[plus], idx[step]], step) -
                               H(states[case[minus], idx[step]], step)) / delta / sigma[mask])
            cols.append(np.concatenate(pieces))
        A = np.stack(cols, axis=1)
        H_width[str(width)] = A
        require(close(A, recalc['H'][str(width)]) and close(A.T, recalc['Ht'][str(width)]),
                f'H/Ht arrays differ from published recomputation at width {width}')
        require(close(A.T@A, recalc['Gauss_Newton_H'][str(width)]),
                f'Gauss-Newton H differs from published recomputation at width {width}')
        sv = np.linalg.svd(A, compute_uv=False)
        rank.append({'width': width, 'singular_values': sv.tolist(), 'ratio': float(sv[-1]/sv[0]),
                     'column_cosine': float(A[:,0]@A[:,1]/(np.linalg.norm(A[:,0])*np.linalg.norm(A[:,1])))})
    rank_pin = recalc['rank']
    for got, pinned in zip(rank, rank_pin, strict=True):
        require(got['width'] == pinned['width'] and close(got['singular_values'], pinned['singular_values']) and
                close(got['ratio'], pinned['ratio']) and close(got['column_cosine'], pinned['column_cosine']),
                'rank/SVD result differs from postprocessor')
    matrix_difference = float(np.linalg.norm(H_width['0.01']-H_width['0.03'], ord=2))
    require(close(matrix_difference, recalc['rank_matrix_difference_2norm']), 'rank-matrix width delta mismatch')
    require(close(np.linalg.inv(H_width['0.01'].T@H_width['0.01']),
                  recalc['H0_inverse_Gauss_Newton_H']), 'inverse Gauss-Newton H mismatch')

    fd_results = []
    g_components = {'full': bg, 'midpoint_composite': mid, 'terminal_reference': terminal}
    for axis in (0, 1):
        for width, tag in ((.01, '01'), (.03, '03')):
            plus=f'fd_a{axis}_{tag}_plus'; minus=f'fd_a{axis}_{tag}_minus'
            dcontrol=controls[case[plus],axis]-controls[case[minus],axis]
            pairs = {'full':sum(costs[plus].values()),
                     'midpoint_composite':costs[plus][128],
                     'terminal_reference':costs[plus][256]}
            minus_pairs = {'full':sum(costs[minus].values()),
                           'midpoint_composite':costs[minus][128],
                           'terminal_reference':costs[minus][256]}
            background_pairs = {'full':sum(costs['e1_cost_000'].values()),
                                'midpoint_composite':costs['e1_cost_000'][128],
                                'terminal_reference':costs['e1_cost_000'][256]}
            for component, jp in pairs.items():
                jm=minus_pairs[component]; j0=background_pairs[component]; ad=float(g_components[component][axis])
                fd=(jp-jm)/dcontrol; den=max(abs(fd),abs(ad))
                rel=abs(fd-ad)/den if den else None
                fd_results.append({'component':component,'axis':axis,'width':width,'FD':fd,'AD':ad,
                                   'relative_error':rel,'signal_pass':bool(abs(jp-jm)>100*EPS*max(abs(jp),abs(jm),abs(j0))),
                                   'one_percent_pass':bool(den>0 and rel<=.01)})
    require(len(fd_results)==12 and all(x['signal_pass'] and x['one_percent_pass'] for x in fd_results),
            'FD signal or one-percent gradient check failed')
    for got, pinned in zip(fd_results, recalc['fd_gradient_checks'], strict=True):
        require(got['component']==pinned['component'] and got['axis']==pinned['axis'] and
                got['width']==pinned['width'] and close(got['FD'],pinned['FD']) and
                close(got['AD'],pinned['AD']) and close(got['relative_error'],pinned['relative_error']),
                'FD recompute differs from portable report')

    # Withheld-time and unobserved-owner field scores from the exported states.
    def score(label: str, step: int, masks: np.ndarray) -> dict:
        delta=states[case[label],idx[step]]-truth[idx[step]]
        rms={}; floor={}
        for fi,field in enumerate(FIELDS):
            d=delta[masks[fi]]; a=states[case[label],idx[step],masks[fi]]; b=truth[idx[step],masks[fi]]
            rms[field]=float(np.sqrt(np.mean(d*d)))
            floor[field]=float(np.sqrt(np.mean((100*EPS*np.maximum(1.0,np.maximum(np.abs(a),np.abs(b))))**2)))
        comp=float(np.sqrt(np.mean([(rms[f]/epsilon[i])**2 for i,f in enumerate(FIELDS)])))
        fcomp=float(np.sqrt(np.mean([(floor[f]/epsilon[i])**2 for i,f in enumerate(FIELDS)])))
        return {'field_rms':rms,'composite':comp,'roundoff_floor':fcomp}
    withheld = {}
    for step in (64,192):
        per={label:score(label,step,owner) for label in ('e1_cost_000','e1_cost_001')}
        ratio=per['e1_cost_001']['composite']/per['e1_cost_000']['composite']
        withheld[str(step)]={'cases':per,'final_over_background':ratio}
        require(per['e1_cost_000']['composite']>per['e1_cost_000']['roundoff_floor'] and ratio<=.5,
                f'withheld-time score failed at step {step}')
    unobserved = {}
    for step, oi in ((128,0),(256,1)):
        masks=owner & ~observed[oi]
        per={label:score(label,step,masks) for label in ('e1_cost_000','e1_cost_001')}
        ratio=per['e1_cost_001']['composite']/per['e1_cost_000']['composite']
        unobserved[str(step)]={'cases':per,'final_over_background':ratio}
        require(per['e1_cost_000']['composite']>per['e1_cost_000']['roundoff_floor'] and ratio<=.5,
                f'unobserved-owner score failed at step {step}')

    e0_j=sum(costs['e0_forward'].values())
    final_j=sum(costs['e1_cost_001'].values())
    initial_j=sum(costs['e1_cost_000'].values())
    final_ginf=float(np.linalg.norm(final, ord=np.inf))
    initial_ginf=float(np.linalg.norm(bg, ord=np.inf))
    e0_record=validation_records['E0.json']; e2_record=validation_records['E2.json']
    optimizer=validation_records['minimizer.json']
    require(e0_record['e0_gate_passed'] is True and e0_record['gradient_calls']==1 and
            e0_record['accepted_updates']==0 and e0_j<=protocol['independent_acceptance']['E0J'] and
            np.linalg.norm(e0,ord=np.inf)<=protocol['independent_acceptance']['E0g_inf'],
            'portable E0 zero-innovation result failed')
    require(e2_record['all_detected'] is True and len(e2_record['mutations'])==10 and
            all(e2_record['mutations'].values()), 'portable E2 mutation validation failed')
    require(optimizer['converged'] is True and optimizer['accepted_updates']==1 and
            optimizer['objective_forwards']==2 and optimizer['gradient_calls']==2 and
            close(optimizer['control'],controls[case['e1_cost_001']]) and
            close(optimizer['terminal_J'],final_j), 'optimizer trace disagrees with portable recomputation')
    attempt_items=manifest['raw_case_receipts']
    completed=[x for x in attempt_items if x['status']=='completed']
    failed=[x for x in attempt_items if x['status']=='preserved_failed_not_accepted']
    require(len(completed)==39 and all(x['return_code']==0 for x in completed) and
            len(failed)==2 and all(x['return_code']==1 for x in failed), 'native attempt accounting mismatch')
    require(sum(x['expected_steps'] for x in attempt_items)==6880, 'native model-step accounting mismatch')
    for x in attempt_items:
        m=x['physical_minima']
        require(x['all_fields_finite'] is True and all(np.isfinite(m[k]) and m[k]>0 for k in
                ('column_mass_Pa','layer_dph','theta_K')), f"physical checks failed for {x['label']}")
    require(manifest['RK3_comparison']['performed'] is False and
            manifest['RK3_comparison']['runtime_or_speed_claim'] is False,
            'package must not assert a new RK3 comparison/performance claim')
    chains = recalc['accepted_FP32_lambda_chains']
    require(any(w['logical_label']=='e1_grad_000_096' and
                w['resolved_label']=='e1_grad_000_096_recovery1'
                for w in chains['E1_background']['windows']) and
            any(w['logical_label']=='e1_grad_001_192' and
                w['resolved_label']=='e1_grad_001_192_mpi_trace'
                for w in chains['E1_final']['windows']), 'accepted lambda chains lack exact recovery aliases')
    relative_control=float(np.linalg.norm(controls[case['e1_cost_001']]-np.array([-.5,.5]))/np.sqrt(.5))
    require(relative_control <= protocol['independent_acceptance']['control_relative_L2_error'] and
            close(relative_control,recalc['E1']['relative_control_error']) and
            close(final_ginf/max(1.0,initial_ginf),optimizer['normalized_gradient_inf']) and
            optimizer['normalized_gradient_inf'] <= 1e-4 and
            final_j/initial_j <= 1e-4, 'control/cost/gradient convergence gate failed')

    result = {
        'schema':'native_idealized_twin_portable_verification_v1',
        'input_sha256':{'npz':sha(NPZ),'manifest':sha(MANIFEST),'protocol':sha(PROTOCOL),
                        'profiled_192_proof':sha(PROOF),'recompute_tool':sha(Path(__file__).resolve())},
        'checks':{'array_pins':True,'H_Ht_adjoint':True,'J_and_weighted_Ht':True,
                  'Pdot_lambda0':True,'rank_and_FD':True,'E0_zero_innovation':True,
                  'E2_all_mutations_detected':bool(recalc['E2']['all_detected']),
                  'physical_attempts':True,'withheld_and_unobserved':True,
                  'native_attempt_accounting':True,'single_profiled_192_proof':True},
        'H_Ht_dot_relative_errors':ht_dot_errors,
        'Pdot_lambda0':{'E0':e0.tolist(),'background':bg.tolist(),'final':final.tolist()},
        'J':{'E0':e0_j,'background':initial_j,'final':final_j,
             'final_over_background':final_j/initial_j},
        'H_singular_values':rank,'H_width_difference_2norm':matrix_difference,
        'Gauss_Newton_H_0_01':(H_width['0.01'].T@H_width['0.01']).tolist(),
        'FD_gradient_checks':fd_results,
        'optimizer':{'control':controls[case['e1_cost_001']].tolist(),
            'relative_control_error':relative_control,
            'normalized_final_gradient_inf':final_ginf/max(1.0,initial_ginf),
            'cost_ratio':final_j/initial_j,'accepted_updates':1},
        'withheld_scores':withheld,'unobserved_owner_scores':unobserved,
        'E0':recalc['E0'],'E2':recalc['E2'],
        'optimizer_trace':optimizer,
        'native_attempt_accounting':{'completed_rc0':len(completed),'failed_rc1_preserved':len(failed),
            'total_attempts':len(attempt_items),'model_steps':6880},
        'claim_scope':'Idealized E0/E1/E2 case only. No new RK3 run, runtime comparison, speed claim, real-observation claim, or general profiled-executable equivalence.'}
    OUT.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'status':'portable_verification_pass','output':str(OUT),
        'final_cost_ratio':result['J']['final_over_background'],
        'final_normalized_gradient_inf':result['optimizer']['normalized_final_gradient_inf'],
        'native_attempts':result['native_attempt_accounting']},indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'portable verification failed: {exc}', file=sys.stderr)
        raise
