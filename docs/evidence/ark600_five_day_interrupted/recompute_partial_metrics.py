#!/usr/bin/env python3
"""Recompute the published owned-point distances; these are not exact-truth errors."""
from pathlib import Path
import argparse
import csv
import hashlib
import io
import json
import sys
import numpy as np


def load_verified_asset(root, asset):
    """Load one array only after checking its manifest pins and numeric contract."""
    path = (root / asset['file']).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as exc:
        raise ValueError(f"asset path escapes evidence directory: {asset['file']}") from exc

    try:
        size = path.stat().st_size
    except OSError as exc:
        raise ValueError(f"cannot stat asset {asset['file']}: {exc}") from exc
    if size != asset['bytes']:
        raise ValueError(f"asset size mismatch for {asset['file']}: {size} != {asset['bytes']}")

    try:
        payload = path.read_bytes()
    except OSError as exc:
        raise ValueError(f"cannot read asset {asset['file']}: {exc}") from exc
    if len(payload) != asset['bytes']:
        raise ValueError(f"asset size changed while reading {asset['file']}")
    digest = hashlib.sha256(payload).hexdigest()
    if digest != asset['sha256']:
        raise ValueError(f"asset SHA-256 mismatch for {asset['file']}")

    try:
        with np.load(io.BytesIO(payload), allow_pickle=False) as data:
            arrays = {name: data[name] for name in ('diff', 'reference_change')}
    except (OSError, ValueError, KeyError) as exc:
        raise ValueError(f"cannot load expected arrays from {asset['file']}: {exc}") from exc

    expected_shape = tuple(asset['shape'])
    for name, values in arrays.items():
        if values.shape != expected_shape:
            raise ValueError(
                f"{asset['file']}:{name} shape mismatch: {values.shape} != {expected_shape}")
        if values.dtype != np.dtype('float64'):
            raise ValueError(f"{asset['file']}:{name} dtype must be float64, got {values.dtype}")
        if not np.isfinite(values).all():
            raise ValueError(f"{asset['file']}:{name} contains NaN or Inf")
    return arrays['diff'], arrays['reference_change']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hours', type=int, choices=(24, 36))
    parser.add_argument('--field', choices=('U', 'V', 'W', 'PH', 'T', 'MU', 'P_total'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    writer = csv.writer(sys.stdout, lineterminator='\n')
    rows = []
    manifest = json.loads((root / 'array_asset_manifest.json').read_text())
    for asset in manifest['assets']:
        if args.hours is not None and asset['model_hours'] != args.hours:
            continue
        if args.field is not None and asset['field'] != args.field:
            continue
        try:
            diff, signal = load_verified_asset(root, asset)
        except ValueError as exc:
            parser.error(str(exc))
        rms = float(np.sqrt(np.mean(diff * diff)))
        signal_rms = float(np.sqrt(np.mean(signal * signal)))
        rows.append([asset['model_hours'], asset['field'], rms,
                     float(np.max(np.abs(diff))), float(np.mean(diff)),
                     signal_rms, rms / signal_rms if signal_rms else '',
                     diff.size])
    writer.writerow(['model_hours', 'field', 'rms', 'max_abs', 'mean_bias',
                     'reference_change_rms', 'relative_rms_to_reference_change',
                     'owned_points'])
    writer.writerows(rows)



if __name__ == '__main__':
    main()
