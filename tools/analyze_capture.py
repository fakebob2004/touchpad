#!/usr/bin/env python3
import argparse
import collections
import json
import statistics
import struct
import sys


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]

def signal_summary(values):
    return {
        "min": round(min(values), 7),
        "p10": round(percentile(values, 0.10), 7),
        "median": round(statistics.median(values), 7),
        "p90": round(percentile(values, 0.90), 7),
        "p95": round(percentile(values, 0.95), 7),
        "p99": round(percentile(values, 0.99), 7),
        "max": round(max(values), 7),
    }

def normalize_contact(contact):
    # Probe revisions before 608afdb exposed these inferred ABI fields under
    # temporary names. Preserve analysis compatibility with those captures.
    if "pressure" not in contact and "unknown3" in contact:
        contact["pressure"] = struct.unpack(
            "f", struct.pack("I", contact["unknown3"] & 0xFFFFFFFF)
        )[0]
    if "finger_id" not in contact and "unknown1" in contact:
        contact["finger_id"] = contact["unknown1"]
    if "hand_id" not in contact and "unknown2" in contact:
        contact["hand_id"] = contact["unknown2"]
    return contact


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
    active_contacts = []
    for frame in frames:
        contacts = frame["contacts"]
        ids = [contact["id"] for contact in contacts]
        if len(ids) != len(set(ids)):
            duplicate_id_frames.append(frame["frame"])
        contact_counts[len(contacts)] += 1
        identifiers.update(ids)
        states.update(contact["state"] for contact in contacts)
        active_contacts.extend(
            normalize_contact(contact)
            for contact in contacts
            if contact["state"] in (3, 4)
        )

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
    signal_names = (
        "density",
        "size",
        "major",
        "minor",
        "pressure",
        "finger_id",
        "hand_id",
    )
    available_signals = {
        name: [contact[name] for contact in active_contacts if name in contact]
        for name in signal_names
    }
    report["active_contact_signals"] = {
        name: signal_summary(values)
        for name, values in available_signals.items()
        if values
    }

    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
