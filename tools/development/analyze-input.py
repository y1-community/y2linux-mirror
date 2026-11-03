#!/usr/bin/env python3
"""Analyze bounded input-capture JSONL; never infer a human detent count."""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return {"n": 0, "median_us": None, "p95_us": None, "worst_us": None, "min_us": None}
    def quantile(p):
        return values[max(0, math.ceil(len(values) * p) - 1)]
    return {"n": len(values), "median_us": quantile(.5), "p95_us": quantile(.95),
            "worst_us": values[-1], "min_us": values[0]}


def analyze(rows):
    devices = {row["event"]: row for row in rows if "device" in row}
    events = [row for row in rows if "type" in row]
    end = next((row for row in reversed(rows) if "capture_end_us" in row), None)
    start = next((row for row in rows if "capture_start_us" in row), None)
    valid = bool(start and end and not end["failed"] and not end["truncated"]
                 and end["records"] == len(events))
    counts, latencies, holds = defaultdict(Counter), defaultdict(list), defaultdict(list)
    pressed = {}
    wheel = []
    issues = Counter()
    for row in events:
        device = devices.get(row["event"], {})
        name = device.get("device", "unknown")
        if name == "unknown":
            valid = False
        if row["type"] == 0 and row["code"] == 3:
            issues["syn_dropped"] += 1
        if row["type"] != 1:
            continue
        key = (row["event"], row["code"])
        label = f"{name}:{row['code']}"
        counts[label][str(row["value"])] += 1
        if device.get("monotonic"):
            latency = row["received_us"] - row["kernel_us"]
            if latency >= 0:
                latencies[label].append(latency)
            else:
                valid = False
                issues["negative_latency"] += 1
        if row["value"] == 1:
            if key in pressed:
                issues["duplicate_down_without_release"] += 1
            pressed[key] = row["kernel_us"]
        elif row["value"] == 0:
            if key not in pressed:
                issues["unmatched_release"] += 1
            else:
                holds[label].append(row["kernel_us"] - pressed.pop(key))
        if name == "APT32F click-wheel" and row["value"] == 1:
            direction = {103: "ccw", 104: "ccw", 108: "cw", 109: "cw"}.get(row["code"])
            if direction:
                wheel.append((row["kernel_us"], direction))
    gaps = [(b[0] - a[0], a[1] == b[1]) for a, b in zip(wheel, wheel[1:])]
    same = [gap for gap, same in gaps if same and gap >= 0]
    reverse = [gap for gap, same in gaps if not same and gap >= 0]
    return {"collection_valid": valid, "duration_us": end["capture_end_us"] - start["capture_start_us"]
            if start and end else None, "records": len(events), "devices": list(devices.values()),
            "key_counts": dict(counts), "press_duration_us": {k: distribution(v) for k, v in holds.items()},
            "kernel_to_observer_us": {k: distribution(v) for k, v in latencies.items()},
            "wheel_presses": dict(Counter(direction for _, direction in wheel)),
            "same_direction_gap_us": distribution(same), "reversal_gap_us": distribution(reverse),
            "same_direction_gaps_below_8ms": sum(gap < 8000 for gap in same),
            "same_direction_gaps_below_8ms_values": [gap for gap in same if gap < 8000],
            "held_at_capture_end": len(pressed), "issues": dict(issues),
            "physical_detent_accuracy": "REQUIRES_OWNER_COUNT",
            "physical_to_kernel_and_kernel_to_ui_frame": "NOT_MEASURED_BY_THIS_OBSERVER"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    if args.capture.stat().st_size > 2 * 1024 * 1024:
        parser.error("capture exceeds 2 MiB limit")
    report = analyze([json.loads(line) for line in args.capture.read_text().splitlines() if line.strip()])
    print(json.dumps(report, indent=2))
    return 0 if report["collection_valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
