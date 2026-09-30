"""Summarize binaries/ai_driver_trace.csv (written by a verbose AiDriver): where a lap loses time and why.

usage: python tools/ai_driver/analyze_trace.py [trace.csv] [--lap N] [--bin 50]
"""
import csv
import sys
from collections import defaultdict


def load(path):
    with open(path, newline="") as f:
        # the engine may be mid-write, a partial last row is skipped
        return [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f) if None not in row.values()]


def main():
    args = sys.argv[1:]
    path = "binaries/ai_driver_trace.csv"
    lap = None
    bin_size = 50.0
    i = 0
    while i < len(args):
        if args[i] == "--lap":
            lap = int(args[i + 1])
            i += 2
        elif args[i] == "--bin":
            bin_size = float(args[i + 1])
            i += 2
        else:
            path = args[i]
            i += 1

    rows = load(path)
    laps = sorted({int(r["lap"]) for r in rows})
    if lap is None:
        lap = laps[-2] if len(laps) > 1 else laps[-1]
    rows = [r for r in rows if int(r["lap"]) == lap]
    print(f"lap {lap}: {len(rows)} samples, {rows[-1]['time']:.1f} s" if rows else "no samples")

    bins = defaultdict(list)
    for r in rows:
        bins[int(r["distance"] // bin_size)].append(r)

    print(f"{'from':>6} {'speed':>6} {'target':>6} {'plan':>6} {'deficit':>7} {'|err|':>5} {'maxerr':>6} {'curv':>7} {'thr':>5} {'brk':>5} {'fuse':>5} {'ruse':>5} {'slip':>6} {'over%':>5} {'lat g':>5}")
    for key in sorted(bins):
        b = bins[key]
        n = len(b)
        avg = lambda name: sum(r[name] for r in b) / n
        speed = avg("speed") * 3.6
        target = avg("target") * 3.6
        plan = avg("plan") * 3.6
        err = sum(abs(r["line_error"]) for r in b) / n
        maxerr = max(abs(r["line_error"]) for r in b)
        over = 100.0 * sum(r["oversteer"] for r in b) / n
        print(f"{key * bin_size:6.0f} {speed:6.0f} {target:6.0f} {plan:6.0f} {plan - speed:7.0f} {err:5.2f} {maxerr:6.2f} {avg('curvature'):7.4f} {avg('throttle'):5.2f} {avg('brake'):5.2f} {avg('front_use'):5.2f} {avg('rear_use'):5.2f} {avg('body_slip'):6.3f} {over:5.0f} {abs(avg('lateral_accel')) / 9.81:5.2f}")


if __name__ == "__main__":
    main()
