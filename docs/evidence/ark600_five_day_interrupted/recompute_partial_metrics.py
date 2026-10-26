#!/usr/bin/env python3
"""Recompute the published owned-point distances; these are not exact-truth errors."""
from pathlib import Path
import argparse
import csv
import json
import sys
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hours', type=int, choices=(24, 36))
    parser.add_argument('--field', choices=('U', 'V', 'W', 'PH', 'T', 'MU', 'P_total'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    writer = csv.writer(sys.stdout, lineterminator='\n')
    writer.writerow(['model_hours', 'field', 'rms', 'max_abs', 'mean_bias',
                     'reference_change_rms', 'relative_rms_to_reference_change',
                     'owned_points'])
    manifest = json.loads((root / 'array_asset_manifest.json').read_text())
    for asset in manifest['assets']:
        if args.hours is not None and asset['model_hours'] != args.hours:
            continue
        if args.field is not None and asset['field'] != args.field:
            continue
        with np.load(root / asset['file'], allow_pickle=False) as data:
            diff = data['diff']
            signal = data['reference_change']
            rms = float(np.sqrt(np.mean(diff * diff)))
            signal_rms = float(np.sqrt(np.mean(signal * signal)))
            writer.writerow([asset['model_hours'], asset['field'], rms,
                             float(np.max(np.abs(diff))), float(np.mean(diff)),
                             signal_rms, rms / signal_rms if signal_rms else '',
                             diff.size])



if __name__ == '__main__':
    main()
