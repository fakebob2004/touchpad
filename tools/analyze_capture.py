#!/usr/bin/env python3
import argparse
import collections
import json
import statistics
import sys


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def main():
    parser = argparse.ArgumentParser(description="Analyze mac-capture-probe JSONL")
    parser.add_argument("capture")
    args = parser.parse_args()

    frames = []
    rejected_lines = []
    with open(args.capture, encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                rejected_lines.append(line_number)
                continue
            if record.get("type") == "frame":
                frames.append(record)

    if not frames:
        print("error: capture contains no frame records", file=sys.stderr)
        return 1

    capture_intervals = [
        (current["capture_time_us"] - previous["capture_time_us"]) / 1000
        for previous, current in zip(frames, frames[1:])
    ]
    device_intervals = [
        (current["device_timestamp"] - previous["device_timestamp"]) * 1000
        for previous, current in zip(frames, frames[1:])
    ]
    frame_gaps = collections.Counter(
        current["frame"] - previous["frame"]
        for previous, current in zip(frames, frames[1:])
    )

    duplicate_id_frames = []
    contact_counts = collections.Counter()
    states = collections.Counter()
    identifiers = set()
    for frame in frames:
        contacts = frame["contacts"]
        ids = [contact["id"] for contact in contacts]
        if len(ids) != len(set(ids)):
            duplicate_id_frames.append(frame["frame"])
        contact_counts[len(contacts)] += 1
        identifiers.update(ids)
        states.update(contact["state"] for contact in contacts)

    regular_device_intervals = [value for value in device_intervals if value <= 20]
    report = {
        "frames": len(frames),
        "frame_range": [frames[0]["frame"], frames[-1]["frame"]],
        "max_contacts": max(contact_counts),
        "contact_count_frames": dict(sorted(contact_counts.items())),
        "contact_ids": sorted(identifiers),
        "states": dict(sorted(states.items())),
        "duplicate_id_frames": duplicate_id_frames,
        "invalid_json_lines": rejected_lines,
        "frame_number_gaps": dict(sorted(frame_gaps.items())),
    }

    if capture_intervals:
        report["capture_interval_ms"] = {
            "median": round(statistics.median(capture_intervals), 3),
            "p95": round(percentile(capture_intervals, 0.95), 3),
            "p99": round(percentile(capture_intervals, 0.99), 3),
        }
    if regular_device_intervals:
        median = statistics.median(regular_device_intervals)
        report["active_device_interval_ms"] = {
            "median": round(median, 3),
            "p95": round(percentile(regular_device_intervals, 0.95), 3),
            "estimated_hz": round(1000 / median, 2),
        }

    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
