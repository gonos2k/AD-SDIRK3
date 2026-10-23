#!/usr/bin/env python3
"""Execute the frozen native twin; every model call is recorded by NativeRunner."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import copy
import hashlib
import json
from pathlib import Path
from threading import Event, Lock

import numpy as np

from native_twin_problem import BLOCKS, load_problem, validate_observations
from native_twin_runner import NativeRunner
from native_twin_minimize import minimize

N = 24531
ENDS = (256, 224, 192, 160, 128, 96, 64, 32)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def safe(x):
    if isinstance(x, dict):
        return {str(k): safe(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [safe(v) for v in x]
    if isinstance(x, np.ndarray):
        return x.tolist()
    if isinstance(x, np.generic):
        return x.item()
    return x


def write(path, value):
    Path(path).write_text(json.dumps(safe(value), indent=2, allow_nan=False) + '\n')


def need(value, message):
    if not bool(value):
        raise ValueError(message)


def checkpoint(path):
    return np.memmap(path, dtype='<f8', mode='r', shape=(257, N))


def owned(z, field):
    offset, shape = BLOCKS[field]
    v = np.asarray(z)[offset:offset + int(np.prod(shape))].reshape(shape)
    if field == 'MU':
        return v[:16, :16]
    if field == 'V':
        return v[:17, :, :16]
    if field in ('W', 'PH'):
        return v[:16, 1:, :16]
    return v[:16, :, :16]


class Experiment:
    def __init__(self, root):
        self.e = Path(root).resolve()
        self.p = load_problem(self.e / 'artifacts')
        self.r = NativeRunner(self.e)
        self.policy = json.loads((self.e / 'protocol.json').read_text())
        map_path = Path(__file__).resolve().parents[4] / 'docs/evidence/research_forward_setting/research_forward_map.json'
        need(sha(map_path) == self.policy['state_error_scale_source']['sha256'],
             'The frozen state-error scale map changed')
        self.eps = json.loads(map_path.read_text())['forward_acceptance']['candidate_limits']
        self.results = self.e / 'results'
        self.results.mkdir(exist_ok=True)
        self.gradients = {}
        self.lock = Lock()
        self.budget_lock = Lock()
        receipts = list((self.e / 'runs').glob('*/run_receipt.json'))
        self.model_calls = len(receipts)
        self.model_steps = sum(json.loads(p.read_text())['expected_steps'] for p in receipts)

    def progress(self, phase, **extra):
        value = {'phase': phase, **extra}
        with self.lock:
            write(self.e / 'progress.json', value)
            print(json.dumps(safe(value)), flush=True)

    def case_identity(self, label, seed, horizon, reference, incoming):
        mode = 'prefix' if incoming is not None else 'forward'
        need((reference is None) == (incoming is None) and
             (horizon == 256 if mode == 'forward' else horizon in ENDS),
             'Forward/prefix label has an invalid horizon/reference/cotangent combination')
        chain = 'twin' if mode == 'prefix' else 'forward'
        return {'label': label, 'mode': mode, 'chain': chain, 'expected_steps': horizon,
                'initial_seed_f64_sha256': hashlib.sha256(
                    np.asarray(seed, dtype='<f8').tobytes()).hexdigest(),
                'reference_sha256': sha(reference) if reference is not None else None,
                'incoming_cotangent_f32_sha256': hashlib.sha256(
                    np.asarray(incoming, dtype='<f4').tobytes()).hexdigest()
                    if incoming is not None else None}

    def validate_completed(self, label, directory, identity, receipt, receipt_path):
        actual = receipt.get('actual', {})
        need(receipt.get('status') == 'completed' and receipt.get('label') == label and
             receipt.get('mode') == identity['mode'] and receipt.get('chain') == identity['chain'] and
             receipt.get('expected_steps') == identity['expected_steps'] and
             receipt.get('expected_implicit_stages') == 3 * identity['expected_steps'] and
             receipt.get('initial_seed_f64_sha256') == identity['initial_seed_f64_sha256'] and
             receipt.get('reference_sha256') == identity['reference_sha256'] and
             receipt.get('incoming_cotangent_f32_sha256') == identity['incoming_cotangent_f32_sha256'] and
             actual.get('return_code') == 0 and actual.get('validation_error') is None and
             actual.get('step_telemetry_count') == identity['expected_steps'] and
             actual.get('newton_convergence_record_count') == 3 * identity['expected_steps'] and
             actual.get('native_rss_monitor_has_samples') is True and
             actual.get('success_marker') is True and actual.get('stage_csv_count') == 0,
             'Completed native case identity/output gate failed: ' + label)
        if receipt.get('binding_sha256') == self.r.frozen['binding_sha']:
            binding = self.r.frozen['binding']
            need(receipt.get('protocol_sha256') == self.r.frozen['protocol_sha'] and
                 receipt.get('executable') == binding['executable'] and
                 receipt.get('source_files') == binding['source_files'] and
                 receipt.get('code_files') == binding['code_files'] and
                 receipt.get('source_case') == binding['source_case'] and
                 receipt.get('input_pins') == binding['input_pins'],
                 'Current-binding completed receipt has mismatched backend/input metadata: ' + label)
        for name, pin in actual.get('output_hashes', {}).items():
            path = directory / name
            need(path.is_file() and path.stat().st_size == pin['bytes'] and sha(path) == pin['sha256'],
                 'Completed raw output changed: ' + str(path))
        return {'directory': str(directory),
                'checkpoints_path': str(directory / 'terminal.checkpoints_f64.bin'),
                'receipt_path': str(receipt_path),
                'gradient_f32_path': (str(directory / 'terminal.gradient_f32.bin')
                                      if identity['mode'] == 'prefix' else None)}

    def validate_reuse_archive(self, label, pin, receipt_path, receipt, identity):
        need(pin.get('label') == label and pin.get('receipt_path') == str(receipt_path) and
             pin.get('receipt_sha256') == sha(receipt_path) and
             pin.get('mode') == identity['mode'] and pin.get('chain') == identity['chain'] and
             pin.get('expected_steps') == identity['expected_steps'] and
             pin.get('initial_seed_f64_sha256') == identity['initial_seed_f64_sha256'] and
             pin.get('reference_sha256') == identity['reference_sha256'] and
             pin.get('incoming_cotangent_f32_sha256') == identity['incoming_cotangent_f32_sha256'],
             'Completed case is not explicitly bound for this exact call: ' + label)
        previous_binding_pin = pin['previous_binding']
        previous_binding_path = Path(previous_binding_pin['path'])
        need(sha(previous_binding_path) == previous_binding_pin['sha256'] == receipt['binding_sha256'],
             'Archived launch binding changed for ' + label)
        old = json.loads(previous_binding_path.read_text())
        for field in ('executable', 'source_files', 'code_files', 'source_case', 'input_pins'):
            need(receipt.get(field) == old[field],
                 'Archived receipt/backend metadata changed: ' + field)
        need(receipt.get('artifact_manifest_sha256') == old['artifact_manifest']['sha256'],
             'Archived receipt artifact manifest changed')
        previous_protocol_pin = pin['previous_protocol']
        previous_protocol_path = Path(previous_protocol_pin['path'])
        need(sha(previous_protocol_path) == previous_protocol_pin['sha256'] ==
             old['protocol']['sha256'] == receipt['protocol_sha256'],
             'Archived protocol changed for ' + label)
        old_policy = json.loads(previous_protocol_path.read_text())
        current_policy = copy.deepcopy(self.policy)
        old_policy.get('truth_reuse', {}).pop('initial_comparison', None)
        current_policy.get('truth_reuse', {}).pop('initial_comparison', None)
        need(old_policy == current_policy, 'Scientific protocol changed across native reuse: ' + label)
        prior_controller = pin['prior_controller']
        old_controller = next(x for x in old['code_files']
                              if Path(x['path']).name == 'native_twin_execute.py')
        need(sha(prior_controller['path']) == prior_controller['sha256'] == old_controller['sha256'],
             'Archived prior controller changed for ' + label)
        prior_runner = pin.get('prior_runner')
        old_runner = next((x for x in old['code_files']
                           if Path(x['path']).name == 'native_twin_runner.py'), None)
        need(old_runner is not None and prior_runner is not None and
             sha(prior_runner['path']) == prior_runner['sha256'] == old_runner['sha256'],
             'Archived prior runner changed for ' + label)
        new = self.r.frozen['binding']
        equivalence = pin.get('mpi_trace_equivalence')
        if equivalence is not None:
            self.validate_mpi_trace_equivalence(label, pin, old, new, receipt, identity)
        fields = ('source_case', 'input_pins', 'artifact_manifest') if equivalence else (
            'executable', 'source_files', 'source_case', 'input_pins', 'artifact_manifest')
        for field in fields:
            need(old[field] == new[field], 'Reused native backend/input changed: ' + field)
        for row in old['code_files']:
            name = Path(row['path']).name
            if name in ('native_twin_execute.py', 'native_twin_runner.py'):
                continue
            need(sha(row['path']) == row['sha256'], 'Archived supporting code changed: ' + row['path'])

    def validate_mpi_trace_equivalence(self, label, case_pin, old, new, receipt, identity):
        """Permit only the one traced MPI-observer receipt paired to the base executable."""
        pin = case_pin['mpi_trace_equivalence']
        need(label == 'e1_grad_001_192_mpi_trace' and identity['expected_steps'] == 192 and
             pin.get('path') and sha(pin['path']) == pin.get('sha256'),
             'MPI observer equivalence is not pinned to the sole profiled 192-step case')
        observer_rows = [x for x in self.r.frozen['binding'].get('reused_completed_native_cases', [])
                         if x.get('mpi_trace_equivalence')]
        need(len(observer_rows) == 1 and observer_rows[0].get('label') == label and
             observer_rows[0]['mpi_trace_equivalence'] == pin,
             'MPI observer equivalence is allowed for more than the profiled case')
        proof = json.loads(Path(pin['path']).read_text())
        need(proof.get('schema') == 'native_mpi_finalize_observer_equivalence_v1',
             'Unexpected MPI observer equivalence proof schema')
        need(sha(proof['base_binding']['path']) == proof['base_binding']['sha256'] and
             sha(proof['observed_binding']['path']) ==
             proof['observed_binding']['sha256'] == receipt['binding_sha256'] and
             sha(proof['protocol']['path']) == proof['protocol']['sha256'] == self.r.frozen['protocol_sha'],
             'MPI observer proof binding/protocol pins changed')
        call = proof['call_identity']
        need(call.get('protocol_sha256') == self.r.frozen['protocol_sha'] and
             all(call.get(k) == identity[k] for k in
                 ('mode', 'chain', 'expected_steps', 'initial_seed_f64_sha256',
                  'reference_sha256', 'incoming_cotangent_f32_sha256')) and
             proof.get('prior_failure_cause_established') is False and
             proof.get('remaining_calculations_use_base_executable') is True and
             proof.get('unchanged_scientific_protocol') is True,
             'MPI observer proof call identity or scope changed')
        need(old['executable'] == proof['observed_executable'] and
             new['executable'] == proof['base_executable'] and
             old['source_files'] == proof['observed_source_files'] and
             new['source_files'] == proof['base_source_files'],
             'MPI observer proof does not bind archived and current executable/source pins')
        need(len(proof['base_source_files']) == 12 and
             proof['observed_source_files'][:-1] == proof['base_source_files'] and
             proof['observed_source_files'][-1] == proof['trace_source'],
             'MPI observer source delta is not exactly the pinned trace source')
        for key in ('base_link_receipt', 'observed_link_receipt'):
            row = proof[key]
            need(sha(row['path']) == row['sha256'], 'MPI observer link receipt changed: ' + key)
        base_link = json.loads(Path(proof['base_link_receipt']['path']).read_text())
        observed_link = json.loads(Path(proof['observed_link_receipt']['path']).read_text())
        base_argv, observed_argv = base_link['argv'], observed_link['argv']
        need(observed_argv[:2] == base_argv[:2] and
             observed_argv[3] == proof['trace_object']['path'] and
             observed_argv[4:] == base_argv[3:] and
             observed_argv[2] == proof['observed_executable']['path'] and
             base_argv[2] == proof['base_executable']['path'] and
             observed_link['output_sha256'] == proof['observed_executable']['sha256'] and
             base_link['output_sha256'] == proof['base_executable']['sha256'],
             'MPI observer link is not the base argv with one object and output path delta')
        for row in (proof['trace_source'], proof['trace_object']):
            need(sha(row['path']) == row['sha256'], 'MPI trace source/object changed: ' + row['path'])
        for row in proof['base_core_artifacts']:
            need(sha(row['path']) == row['sha256'], 'Pinned common core artifact changed: ' + row['path'])
        failed_pin, normal_pin = proof['failed_case_receipt'], proof['normal_case_receipt']
        need(sha(failed_pin['path']) == failed_pin['sha256'] and
             sha(normal_pin['path']) == normal_pin['sha256'] == case_pin.get('receipt_sha256') and
             normal_pin['path'] == case_pin.get('receipt_path'),
             'MPI observer failed/completed receipts are not exactly pinned')
        failed = json.loads(Path(failed_pin['path']).read_text())
        need(failed.get('status') == 'failed' and failed.get('actual', {}).get('return_code') == 1 and
             failed.get('expected_steps') == 192 and
             failed.get('binding_sha256') == proof['base_binding']['sha256'] and
             failed.get('protocol_sha256') == self.r.frozen['protocol_sha'] and
             failed.get('executable') == proof['base_executable'] and
             failed.get('source_files') == proof['base_source_files'] and
             all(failed.get(k) == identity[k] for k in
                 ('initial_seed_f64_sha256', 'reference_sha256', 'incoming_cotangent_f32_sha256')),
             'MPI observer comparison does not identify the original failed invocation')
        need(receipt.get('status') == 'completed' and receipt.get('actual', {}).get('return_code') == 0 and
             receipt.get('expected_steps') == 192 and
             all(receipt.get(k) == identity[k] for k in
                 ('initial_seed_f64_sha256', 'reference_sha256', 'incoming_cotangent_f32_sha256')),
             'MPI observer paired receipt is not a completed exact-call result')
        payloads = {row['name']: row for row in proof['payload_files']}
        need(set(payloads) == {'terminal.checkpoints_f64.bin', 'terminal.gradient_f64.bin',
                               'terminal.gradient_f32.bin'},
             'MPI observer proof must compare checkpoints and both gradient payloads')
        for row in payloads.values():
            base_payload, observed_payload = Path(row['base_path']), Path(row['observed_path'])
            need(base_payload.stat().st_size == row['bytes'] == observed_payload.stat().st_size and
                 sha(base_payload) == row['sha256'] == sha(observed_payload),
                 'MPI observer payloads differ: ' + row['name'])
            need(receipt.get('actual', {}).get('output_hashes', {}).get(row['name'], {}).get(
                     'sha256') == row['sha256'],
                 'MPI observer payload proof disagrees with the run receipts: ' + row['name'])
        trace_pin = proof['trace_file']
        need(sha(trace_pin['path']) == trace_pin['sha256'], 'MPI observer trace changed')
        need(receipt.get('actual', {}).get('output_hashes', {}).get('mpi_finalize.trace', {}).get(
                 'sha256') == trace_pin['sha256'], 'MPI trace proof disagrees with run receipt')
        trace_lines = [line.partition(' ')[2] for line in Path(trace_pin['path']).read_text().splitlines()]
        expected_trace = [
            'constructor initialized_rc=0 initialized=0 finalized_rc=0 finalized=0',
            'enter initialized_rc=0 initialized=1 finalized_rc=0 finalized=0',
            'forward ierr_ptr=present',
            'return ierr=0 initialized_rc=0 initialized=1 finalized_rc=0 finalized=1',
            'destructor initialized_rc=0 initialized=1 finalized_rc=0 finalized=1',
        ]
        need(len(trace_lines) == len(expected_trace) and
             all(trace_lines.count(line) == 1 for line in expected_trace),
             'MPI observer trace does not report a clean finalize return')

    def resolve_failed_alias(self, label, identity):
        for alias in self.r.frozen['binding'].get('recovery_aliases', []):
            if alias.get('failed_label') != label:
                continue
            failed_path = self.e / 'runs' / label / 'run_receipt.json'
            need(alias.get('failed_receipt_path') == str(failed_path) and
                 failed_path.is_file() and sha(failed_path) == alias.get('failed_receipt_sha256'),
                 'Pinned failed receipt changed before recovery alias: ' + label)
            failed = json.loads(failed_path.read_text())
            need(failed.get('status') == 'failed' and failed.get('label') == label and
                 failed.get('mode') == identity['mode'] and failed.get('chain') == identity['chain'] and
                 failed.get('expected_steps') == identity['expected_steps'] and
                 failed.get('initial_seed_f64_sha256') == identity['initial_seed_f64_sha256'] and
                 failed.get('reference_sha256') == identity['reference_sha256'] and
                 failed.get('incoming_cotangent_f32_sha256') == identity['incoming_cotangent_f32_sha256'] and
                 failed.get('actual', {}).get('return_code') == 1 and
                 failed.get('actual', {}).get('validation_error') is not None,
                 'Pinned case is not the expected failed invocation: ' + label)
            replacement = alias.get('replacement_label')
            replacement_path = self.e / 'runs' / replacement / 'run_receipt.json'
            need(alias.get('replacement_receipt_path') == str(replacement_path) and
                 replacement_path.is_file() and
                 sha(replacement_path) == alias.get('replacement_receipt_sha256'),
                 'Pinned recovery receipt is missing/changed: ' + str(replacement))
            replacement_receipt = json.loads(replacement_path.read_text())
            need(replacement_receipt.get('status') == 'completed' and
                 replacement_receipt.get('label') == replacement and
                 replacement_receipt.get('actual', {}).get('return_code') == 0 and
                 replacement_receipt.get('actual', {}).get('validation_error') is None and
                 replacement_receipt.get('mode') == identity['mode'] and
                 replacement_receipt.get('chain') == identity['chain'] and
                 replacement_receipt.get('expected_steps') == identity['expected_steps'] and
                 replacement_receipt.get('initial_seed_f64_sha256') == identity['initial_seed_f64_sha256'] and
                 replacement_receipt.get('reference_sha256') == identity['reference_sha256'] and
                 replacement_receipt.get('incoming_cotangent_f32_sha256') == identity['incoming_cotangent_f32_sha256'],
                 'Pinned recovery output does not match the failed call identity')
            return replacement
        return label

    def native(self, seed, label, horizon=256, reference=None, incoming=None):
        """Resume only hash-verified completed calls; never rerun a partial case."""
        identity = self.case_identity(label, seed, horizon, reference, incoming)
        requested_label = label
        if any(x.get('failed_label') == label
               for x in self.r.frozen['binding'].get('recovery_aliases', [])):
            label = self.resolve_failed_alias(label, identity)
        directory = self.e / 'runs' / label
        if directory.exists():
            receipt_path = directory / 'run_receipt.json'
            receipt = json.loads(receipt_path.read_text())
            identity = {**identity, 'label': label}
            result = self.validate_completed(label, directory, identity, receipt, receipt_path)
            same_binding = receipt.get('binding_sha256') == self.r.frozen['binding_sha']
            if not same_binding:
                approved = self.r.frozen['binding'].get('reused_completed_native_cases', [])
                pin = next((x for x in approved if x['label'] == label), None)
                need(pin is not None, 'Completed case lacks explicit source-version reuse pin: ' + label)
                self.validate_reuse_archive(label, pin, receipt_path, receipt, identity)
            return result
        need(requested_label == label,
             'Recovery alias target is absent; refusing to create or rerun a replacement')
        with self.budget_lock:
            need(self.model_calls < 165 and self.model_steps + horizon <= 28992,
                 'Global predeclared native call/step cap exhausted')
            self.model_calls += 1
            self.model_steps += horizon
            write(self.e / 'actual_model_budget.json', {'reserved_model_calls': self.model_calls,
                'reserved_model_steps': self.model_steps, 'last_label': label,
                'call_cap': 165, 'model_step_cap': 28992})
        return self.r.run(np.asarray(seed, dtype='<f8'), label, horizon=horizon,
                          reference=reference, lambda_in=incoming)

    def evaluate(self, a, label, truth_origin=False):
        a = np.asarray(a, dtype=np.float64)
        need(a.shape == (2,) and np.isfinite(a).all() and np.max(np.abs(a)) <= 1,
             'Control is outside the frozen box')
        seed = self.p.Z_star + a @ self.p.P if truth_origin else self.p.initial_state(a)
        physical = self.p.physical_metrics(seed)
        need(physical['min_total_column_mass_pa'] > 0 and
             physical['min_potential_temperature_k'] > 0 and
             physical['min_geopotential_height_difference'] > 0, 'Invalid initial state')
        self.progress('cost_forward', label=label, control=a)
        run = self.native(seed, label)
        cp = checkpoint(run['checkpoints_path'])
        minima = {'theta': float('inf'), 'mass': float('inf'), 'layer_dph': float('inf')}
        for z in cp:
            m = self.p.physical_metrics(z)
            minima['theta'] = min(minima['theta'], m['min_potential_temperature_k'])
            minima['mass'] = min(minima['mass'], m['min_total_column_mass_pa'])
            minima['layer_dph'] = min(minima['layer_dph'], m['min_geopotential_height_difference'])
            fields = {f: z[o:o+int(np.prod(s))].reshape(s) for f, (o,s) in BLOCKS.items()}
            for f, v in fields.items():
                if f == 'U':
                    need(np.array_equal(v[...,16], v[...,0]) and np.array_equal(v[...,17], v[...,1]),
                         'U periodic aliases changed')
                else:
                    need(np.array_equal(v[...,16], v[...,0]), 'Periodic alias changed: '+f)
                if f == 'V':
                    need(np.all(v[0] == 0) and np.all(v[16] == 0) and np.array_equal(v[17], -v[15]),
                         'V symmetric-wall boundary changed')
                else:
                    need(np.array_equal(v[16], v[15]), 'Symmetric ghost boundary changed: '+f)
            need(np.all(fields['W'][:,0,:] == 0) and np.all(fields['PH'][:,0,:] == 0),
                 'Fixed W/PH lower boundary changed')
        need(all(v > 0 for v in minima.values()), 'An accepted endpoint is physically inadmissible')
        objective = self.p.objective(cp[128], cp[256])
        rec = {'J': objective['J'], 'control': a, 'label': label,
               'seed': seed, 'run': run, 'objective': objective,
               'all_257_endpoint_physical_minima': minima, 'boundary_checks': True}
        write(self.results / (label + '.json'),
              {k: v for k, v in rec.items() if k not in ('seed', 'objective')})
        return rec

    def reverse_lower(self, rec, incoming, tag):
        lam = np.asarray(incoming, dtype='<f4')
        for n in (128, 96, 64, 32):
            self.progress('prefix_pullback', label=tag, prefix=n)
            run = self.native(rec['seed'], f'{tag}_{n:03d}', horizon=n,
                              reference=rec['run']['checkpoints_path'], incoming=lam)
            lam = np.fromfile(run['gradient_f32_path'], dtype='<f4')
        return lam

    def gradient(self, rec, tag):
        if tag in self.gradients:
            return self.gradients[tag]['g']
        q = rec['objective']['q']
        lam = np.asarray(q[256], dtype='<f4')
        upper_lambda = None
        for n in ENDS:
            if n == 128:
                upper_lambda = lam.copy()
                lam = self.inject(lam, q[128], 128, 1)
                self.verify_injection(lam, upper_lambda, q[128], 128, 1)
            self.progress('prefix_pullback', label=tag, prefix=n)
            run = self.native(rec['seed'], f'{tag}_{n:03d}', horizon=n,
                              reference=rec['run']['checkpoints_path'], incoming=lam)
            lam = np.fromfile(run['gradient_f32_path'], dtype='<f4')
        g = self.p.gradient_control(lam)
        self.gradients[tag] = {'g': g, 'lambda0': lam, 'lambda128_terminal': upper_lambda}
        write(self.results / (tag + '.json'), {'g': g, 'lambda0_sha256':
              hashlib.sha256(lam.tobytes()).hexdigest(), 'source_cost_record': rec['label']})
        return g

    @staticmethod
    def inject(lam, q, step, count):
        need(step == 128 and count == 1, 'Midpoint step/count does not match the observation ledger')
        return (np.asarray(lam, dtype=np.float64) + q).astype('<f4')

    @staticmethod
    def verify_injection(actual, prior, q, step, count):
        expected = Experiment.inject(prior, q, step, count)
        need(np.asarray(actual, dtype='<f4').tobytes() == expected.tobytes(),
             'Actual midpoint cotangent does not match the declared cost injection')

    def e0(self):
        rec = self.evaluate(np.zeros(2), 'e0_forward', truth_origin=True)
        cp = checkpoint(rec['run']['checkpoints_path'])
        signed_zero_indices = np.flatnonzero(cp[0].view('u8') != self.p.truth[0].view('u8'))
        need(np.array_equal(cp[0], self.p.truth[0]) and
             np.all((cp[0,signed_zero_indices] == 0) & (self.p.truth[0,signed_zero_indices] == 0)) and
             cp[1:].tobytes() == self.p.truth[1:].tobytes(),
             'New E0 primal does not reproduce the pinned numerical truth; stop reuse')
        write(self.results/'E0_truth_reuse.json', {'initial_values_exact': True,
              'initial_signed_zero_only_bit_differences': signed_zero_indices,
              'all_256_accepted_states_byte_exact': True,
              'no_truth_or_primal_array_modified': True})
        def cost(b):
            need(np.array_equal(b, np.zeros(2)), 'E0 budget allows only the actual truth-origin point')
            return rec
        result = minimize(cost,
                          lambda record: self.gradient(record, 'e0_gradient'),
                          np.eye(2), e0_zero_innovation=True,
                          max_updates=0, max_cost_forwards=1, max_gradient_calls=1)
        write(self.results / 'E0.json', result)
        need(result.get('e0_gate_passed') and result['accepted_updates'] == 0 and
             result['objective_forwards'] == result['gradient_calls'] == 1,
             'E0 same-path zero innovation/gradient/update failed')
        return rec

    def preflight(self):
        bg = self.evaluate(np.zeros(2), 'e1_cost_000')
        jobs = [(axis, width, sign) for axis in range(2)
                for width in (.01, .03) for sign in (1, -1)]
        failed = Event()
        def one(job):
            need(not failed.is_set(), 'FD batch stopped after another case failed')
            axis, width, sign = job
            a = np.zeros(2); a[axis] = sign * width
            tag = f'fd_a{axis}_{int(width * 100):02d}_{"plus" if sign > 0 else "minus"}'
            return job, self.evaluate(a, tag)
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(one, job) for job in jobs]
            fd = {}
            try:
                for future in as_completed(futures):
                    job, record = future.result()
                    fd[job] = record
            except Exception:
                failed.set()
                for future in futures:
                    future.cancel()
                raise
        rank = {}
        matrices = {}
        for width in (.01, .03):
            columns = []
            for axis in range(2):
                pp = checkpoint(fd[(axis, width, 1)]['run']['checkpoints_path'])
                pm = checkpoint(fd[(axis, width, -1)]['run']['checkpoints_path'])
                delta = np.concatenate([(self.p.observe(n, pp[n]) - self.p.observe(n, pm[n]))
                                        for n in (128, 256)]) / (2 * width * self.p.sigma)
                columns.append(delta)
            A = np.stack(columns, axis=1); matrices[width] = A
            sv = np.linalg.svd(A, compute_uv=False)
            need(sv[-1] > 0 and sv[-1] / sv[0] >= 1e-3, 'Observations do not identify both controls')
            rank[str(width)] = {'singular_values': sv, 'ratio': sv[-1] / sv[0],
                'column_cosine': np.dot(A[:, 0], A[:, 1]) /
                    (np.linalg.norm(A[:, 0]) * np.linalg.norm(A[:, 1]))}
        difference = np.linalg.norm(matrices[.01] - matrices[.03], ord=2)
        need(np.linalg.svd(matrices[.01], compute_uv=False)[-1] > difference,
             'Rank signal is unresolved relative to the two-width matrix difference')
        write(self.results / 'identifiability.json', {'widths': rank, 'matrix_difference': difference})
        g = self.gradient(bg, 'e1_grad_000')
        terminal_lam = self.reverse_lower(bg, self.gradients['e1_grad_000']['lambda128_terminal'],
                                          'preflight_terminal')
        g_terminal = self.p.gradient_control(terminal_lam)
        g_mid = self.p.gradient_control(self.gradients['e1_grad_000']['lambda0'].astype('f8') -
                                        terminal_lam.astype('f8'))
        comparisons = []
        for axis, width in ((a, w) for a in range(2) for w in (.01, .03)):
            plus, minus = fd[(axis, width, 1)], fd[(axis, width, -1)]
            for name, ad in [('full', g[axis]), ('midpoint_composite', g_mid[axis]),
                             ('terminal', g_terminal[axis])]:
                jp = plus['J'] if name == 'full' else plus['objective']['per_time_cost'][128 if name.startswith('mid') else 256]
                jm = minus['J'] if name == 'full' else minus['objective']['per_time_cost'][128 if name.startswith('mid') else 256]
                j0 = bg['J'] if name == 'full' else bg['objective']['per_time_cost'][128 if name.startswith('mid') else 256]
                signal = abs(jp - jm)
                need(signal > 100 * np.finfo(float).eps * max(abs(jp), abs(jm), abs(j0)),
                     'FD signal is unresolved')
                df = (jp - jm) / (2 * width); den = max(abs(df), abs(ad))
                need(den > 0 and abs(df - ad) / den <= .01, 'Control FD/adjoint check failed')
                comparisons.append({'component': name, 'axis': axis, 'width': width,
                                    'FD': df, 'AD': ad, 'relative_error': abs(df-ad)/den})
        write(self.results / 'gradient_checks.json', comparisons)
        self.e2(bg)
        return bg, np.linalg.inv(matrices[.01].T @ matrices[.01])

    def e2(self, bg):
        outcomes = {}
        def rejected(name, fn):
            try:
                fn()
            except (ValueError, AssertionError, IndexError):
                outcomes[name] = True
            else:
                raise ValueError('E2 failed to detect ' + name)
        rows = self.p.rows
        slots, y, sigma = self.p.observation_slots, self.p.observation_values, self.p.sigma
        for name in ('nonfinite', 'sigma_zero', 'sigma_negative', 'duplicate_id', 'wrong_grid', 'wrong_slot'):
            rr = copy.deepcopy(rows); ss = sigma.copy(); yy = y.copy(); ii = slots.copy()
            if name == 'nonfinite': yy[0] = np.nan
            elif name == 'sigma_zero': ss[0] = 0
            elif name == 'sigma_negative': ss[0] = -1
            elif name == 'duplicate_id': rr[1]['unique_id'] = rr[0]['unique_id']
            elif name == 'wrong_grid': rr[0]['grid_layout_id'] = 'wrong'
            else: ii[0] += 1
            rejected(name, lambda rr=rr,ii=ii,yy=yy,ss=ss: validate_observations(rr,ii,yy,ss))
        q = bg['objective']['q'][128]
        prior = self.gradients['e1_grad_000']['lambda128_terminal']
        expected = self.inject(prior, q, 128, 1)
        need(np.any(expected != prior), 'E2 midpoint injection has no resolved signal')
        rejected('omitted_midpoint', lambda: self.verify_injection(prior, prior, q, 128, 1))
        rejected('wrong_step', lambda: self.verify_injection(expected, prior, q, 96, 1))
        rejected('duplicate_injection', lambda: self.verify_injection(expected, prior, q, 128, 2))
        wrong_q = np.zeros(N)
        mask = np.array([r['accepted_step'] == 128 for r in rows])
        np.add.at(wrong_q, slots[mask], bg['objective']['residuals'][128] / sigma[mask])
        wrong_injection = self.inject(prior, wrong_q, 128, 1)
        rejected('sigma_instead_of_sigma_squared',
                 lambda: self.verify_injection(wrong_injection, prior, q, 128, 1))
        write(self.results / 'E2.json', {'all_detected': all(outcomes.values()), 'mutations': outcomes})

    def score(self, rec):
        cp = checkpoint(rec['run']['checkpoints_path'])
        eps = self.eps
        result = {}
        for n in (64, 128, 192, 256):
            fields = {f: float(np.sqrt(np.mean(owned(cp[n]-self.p.truth_state(n), f)**2))) for f in BLOCKS}
            result[str(n)] = {'time_s': n * .9375, 'RMS': fields,
                'composite': float(np.sqrt(np.mean([(fields[f]/eps[f])**2 for f in BLOCKS])))}
            floors = {}
            for f in BLOCKS:
                x, t = owned(cp[n], f), owned(self.p.truth_state(n), f)
                v = 100 * np.finfo(float).eps * np.maximum(1, np.maximum(abs(x), abs(t)))
                floors[f] = float(np.sqrt(np.mean(v*v)))
            result[str(n)]['composite_roundoff_floor'] = float(np.sqrt(np.mean(
                [(floors[f]/eps[f])**2 for f in BLOCKS])))
            if n in (128, 256):
                indices = np.arange(N)
                unobserved = {}
                for f in BLOCKS:
                    owner_ids = owned(indices, f).ravel()
                    observed = [r['packed_slot'] for r in self.p.rows if r['accepted_step'] == n]
                    owner_ids = owner_ids[~np.isin(owner_ids, observed)]
                    d = cp[n, owner_ids] - self.p.truth_state(n)[owner_ids]
                    unobserved[f] = float(np.sqrt(np.mean(d*d)))
                result[str(n)]['unobserved_owner_RMS'] = unobserved
        return result

    def run(self):
        self.e0()
        bg, h0 = self.preflight()
        records = [bg]
        def cost(a):
            if len(records) == 1 and np.array_equal(a, np.zeros(2)):
                return records[0]
            rec = self.evaluate(a, f'e1_cost_{len(records):03d}'); records.append(rec)
            return rec
        gradient_counter = 0
        def gradient(rec):
            nonlocal gradient_counter
            tag = f'e1_grad_{gradient_counter:03d}'; gradient_counter += 1
            return self.gradient(rec, tag)
        self.progress('E1_minimization')
        result = minimize(cost, gradient, h0)
        write(self.results / 'minimizer.json', result)
        need(result['converged'], 'E1 minimizer did not meet both frozen convergence conditions')
        a = np.array(result['control'])
        final = next(r for r in records if np.array_equal(r['control'], a))
        background_score, final_score = self.score(bg), self.score(final)
        relative = float(np.linalg.norm(a-np.array([-.5,.5]))/np.sqrt(.5))
        need(all(background_score[n]['composite'] > background_score[n]['composite_roundoff_floor']
                 for n in ('64','192')),
             'Withheld background composite is unresolved')
        hold = {n: final_score[n]['composite']/background_score[n]['composite'] for n in ('64','192')}
        need(relative <= .01 and all(v <= .5 for v in hold.values()),
             'Cost/gradient convergence did not restore control and withheld states')
        summary = {'status':'complete_E0_E1_E2','control':a,'relative_control_error':relative,
            'withheld_final_over_background':hold,'minimizer':result,
            'background_state_errors':background_score,'final_state_errors':final_score,
            'claim':'Declared dry two-control idealized twin only; not real-observation assimilation.'}
        write(self.results / 'summary.json', summary)
        self.progress('complete_E0_E1_E2', relative_control_error=relative, withheld=hold)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence_root', type=Path)
    args = parser.parse_args()
    exp = Experiment(args.evidence_root)
    try:
        exp.run()
    except Exception as exc:
        exp.progress('stopped_first_failure', reason=f'{type(exc).__name__}: {exc}')
        raise


if __name__ == '__main__':
    main()
