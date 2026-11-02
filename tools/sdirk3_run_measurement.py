"""Independent clock and Newton-stage measurements for a proposed runner.

Stage logs lack a timestep identifier. We merge duplicate streams by taking the
maximum multiplicity of each (stage, iters) tuple, preserving repeats in one
stream. Distinct identical tuples split across streams cannot be distinguished.
"""
from collections import Counter
from pathlib import Path
import re
import time

STAGE = re.compile(r'\[Newton\] CONVERGED\s+stage=(\d+)\s+iters=(\d+)')
STREAMS = ('rsl.out.0000', 'rsl.error.0000',
           'launcher.stdout.log', 'launcher.stderr.log')


def clock_endpoint(monotonic_ns=time.monotonic_ns, realtime_ns=time.time_ns):
    """Capture the realtime reading within monotonic before/after bounds."""
    before = monotonic_ns()
    realtime = realtime_ns()
    after = monotonic_ns()
    return {'monotonic_before_ns': before, 'realtime_ns': realtime,
            'monotonic_after_ns': after}


def clock_measurement(start, end, *, max_endpoint_span_ns=10_000_000,
                      gap_limit_ns=1_000_000_000,
                      monotonic_resolution_ns=None, realtime_resolution_ns=None):
    """Classify wall minus monotonic elapsed time using conservative bounds.

    The default span and gap budgets are policy defaults fixed before the experiment. A wide bracket or an
    interval straddling the threshold is unresolved, never a definite jump.
    Resolution expands each side to account for quantization of four readings.
    """
    if monotonic_resolution_ns is None:
        monotonic_resolution_ns = max(1, round(time.get_clock_info('monotonic').resolution * 1e9))
    if realtime_resolution_ns is None:
        realtime_resolution_ns = max(1, round(time.get_clock_info('time').resolution * 1e9))
    if min(max_endpoint_span_ns, gap_limit_ns, monotonic_resolution_ns,
           realtime_resolution_ns) <= 0:
        raise ValueError('clock budgets and resolutions must be positive')
    a, b = start['monotonic_before_ns'], start['monotonic_after_ns']
    c, d = end['monotonic_before_ns'], end['monotonic_after_ns']
    if a > b or c > d or b > c:
        raise ValueError('invalid or overlapping monotonic brackets')
    wall = end['realtime_ns'] - start['realtime_ns']
    uncertainty = 2 * (monotonic_resolution_ns + realtime_resolution_ns)
    lower = wall - (d - a) - uncertainty
    upper = wall - (c - b) + uncertainty
    spans = (b - a, d - c)
    if max(spans) > max_endpoint_span_ns:
        classification = 'unresolved'
    elif lower >= gap_limit_ns or upper <= -gap_limit_ns:
        classification = 'definite_gap'
    elif lower > -gap_limit_ns and upper < gap_limit_ns:
        classification = 'consistent'
    else:
        classification = 'unresolved'
    return {'start': dict(start), 'end': dict(end), 'wall_elapsed_ns': wall,
            'gap_interval_ns': [lower, upper], 'endpoint_spans_ns': list(spans),
            'max_endpoint_span_ns': max_endpoint_span_ns,
            'gap_limit_ns': gap_limit_ns,
            'monotonic_resolution_ns': monotonic_resolution_ns,
            'realtime_resolution_ns': realtime_resolution_ns,
            'classification': classification}


def collect_stages(directory):
    """Read all four streams and merge duplicate copies conservatively."""
    merged = Counter()
    counts = {}
    for name in STREAMS:
        path = Path(directory) / name
        rows = Counter((int(stage), int(iters)) for stage, iters in
                       STAGE.findall(path.read_text(errors='replace') if path.exists() else ''))
        counts[name] = sum(rows.values())
        merged |= rows
    return {'stage_iterations': [{'stage': s, 'iters': i, 'count': n}
                                 for (s, i), n in sorted(merged.items())],
            'stage_count': sum(merged.values()), 'stream_counts': counts,
            'deduplication': 'maximum multiplicity per (stage, iters) across streams'}
