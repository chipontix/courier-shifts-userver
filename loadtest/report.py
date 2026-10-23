"""Summarises a Pandora phout log of a step load profile.

    python3 report.py phout.log --from 1000 --step 1000 --duration 30 [--json out.json]

phout columns: ts, tag, rtt_us, connect, send, latency, receive,
interval_event, req_bytes, resp_bytes, net_errno, http_code.

Per step: offered RPS, answered RPS (HTTP 2xx and 409; 409 is the correct
answer to a duplicate order or an overlapping shift), latency percentiles of
those answers and the error share split into 429 (congestion control shed
the request), 5xx, and network errors/timeouts (net_errno != 0, which
includes Pandora's 777 "could not keep the schedule").
"""

import argparse
import json
import math


def percentile(sorted_values, q):
    if not sorted_values:
        return float('nan')
    k = max(0, math.ceil(q * len(sorted_values)) - 1)
    return sorted_values[k]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phout')
    parser.add_argument('--from', dest='start', type=int, required=True)
    parser.add_argument('--step', type=int, required=True)
    parser.add_argument('--duration', type=int, required=True)
    parser.add_argument('--warmup', type=int, default=5, help='seconds skipped at the start of each step')
    parser.add_argument('--tail', type=int, default=2, help='seconds skipped at the end of each step')
    parser.add_argument('--json')
    args = parser.parse_args()

    rows = []
    with open(args.phout) as f:
        for line in f:
            parts = line.rstrip('\n').split('\t')
            if len(parts) < 12:
                continue
            rows.append((float(parts[0]), int(parts[2]), int(parts[10]), int(parts[11])))
    if not rows:
        raise SystemExit('empty phout')
    t0 = min(r[0] for r in rows)

    steps = {}
    for ts, rtt_us, errno, code in rows:
        offset = ts - t0
        idx = int(offset // args.duration)
        in_step = offset - idx * args.duration
        if in_step < args.warmup or in_step >= args.duration - args.tail:
            continue
        s = steps.setdefault(idx, {'ok': [], 'r429': 0, 'r5xx': 0, 'net': 0, 'other': 0, 't': [ts, ts]})
        s['t'] = [min(s['t'][0], ts), max(s['t'][1], ts)]
        if errno != 0:
            s['net'] += 1
        elif 200 <= code < 300 or code == 409:
            s['ok'].append(rtt_us)
        elif code == 429:
            s['r429'] += 1
        elif code >= 500:
            s['r5xx'] += 1
        else:
            s['other'] += 1

    result = []
    print('| offered RPS | answered RPS | p50, ms | p99, ms | errors | 429 | 5xx | net/timeout |')
    print('|---:|---:|---:|---:|---:|---:|---:|---:|')
    for idx in sorted(steps):
        s = steps[idx]
        ok = sorted(s['ok'])
        total = len(ok) + s['r429'] + s['r5xx'] + s['net'] + s['other']
        errors = total - len(ok)
        row = {
            'offered_rps': args.start + idx * args.step,
            'answered_rps': round(len(ok) / (s['t'][1] - s['t'][0])),
            'p50_ms': round(percentile(ok, 0.50) / 1000, 2),
            'p99_ms': round(percentile(ok, 0.99) / 1000, 2),
            'error_pct': round(100 * errors / total, 2) if total else 0.0,
            'r429_pct': round(100 * s['r429'] / total, 2) if total else 0.0,
            'r5xx_pct': round(100 * s['r5xx'] / total, 2) if total else 0.0,
            'net_pct': round(100 * s['net'] / total, 2) if total else 0.0,
            'other': s['other'],
        }
        result.append(row)
        print(
            f"| {row['offered_rps']} | {row['answered_rps']} | {row['p50_ms']} | {row['p99_ms']} "
            f"| {row['error_pct']}% | {row['r429_pct']}% | {row['r5xx_pct']}% | {row['net_pct']}% |",
        )
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(result, f, indent=2)


if __name__ == '__main__':
    main()
