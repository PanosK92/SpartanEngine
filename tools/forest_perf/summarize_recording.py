"""Summarize only GPU scopes backed by fresh timestamp readbacks."""
import argparse
import csv
import json
import statistics
from collections import defaultdict

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("recording")
args = parser.parse_args()
frames = defaultdict(lambda: defaultdict(float))
with open(args.recording, newline="", encoding="utf-8-sig") as file:
    for row in csv.DictReader(file):
        if (row["row_type"] == "block" and row["block_type"] == "gpu"
                and row["gpu_timing_valid"] == "1"
                and row["capture_mode"] == "cpu_per_frame_gpu_sample"):
            frames[row["capture_frame"]][row["name"]] += float(row["duration_ms"])
assert len(frames) >= 5, "Need at least five fresh GPU samples"
scopes = defaultdict(list)
for frame in frames.values():
    for name, duration in frame.items():
        scopes[name].append(duration)
medians = {name: round(statistics.median(values), 4) for name, values in scopes.items()}
print(json.dumps({"fresh_gpu_samples": len(frames), "median_scope_ms":
    dict(sorted(medians.items(), key=lambda item: -item[1]))}, indent=2))

