#!/usr/bin/env python3
"""Convert a mac-touch-agent button JSONL trace into the Windows handoff."""

import argparse
import json
import subprocess
from pathlib import Path


def load_jsonl(path: Path) -> list[dict]:
    events = []
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
    return events


def git_commit(repo: Path) -> str:
    return subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Analyze physical-button continuity and write mac-validation.json"
    )
    parser.add_argument("trace", type=Path)
    parser.add_argument(
        "--physical-drag",
        choices=("passed", "failed", "not-tested"),
        default="not-tested",
    )
    parser.add_argument(
        "--output", type=Path, default=Path("handoff/mac-validation.json")
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    runtime = json.loads((repo / "handoff/windows-runtime.json").read_text())
    events = load_jsonl(args.trace)
    callbacks = [event for event in events if event.get("event") == "button_callback"]
    frames = [event for event in events if event.get("event") == "frame"]
    connections = [event for event in events if event.get("event") == "connection"]
    capabilities = [
        event for event in events if event.get("event") == "button_capability"
    ]
    callback_registered = any(
        event.get("callback_registered") for event in capabilities
    )

    pressed_callbacks = [event for event in callbacks if event.get("pressed", 0) != 0]
    released_callbacks = [event for event in callbacks if event.get("released", 0) != 0]
    press_time = pressed_callbacks[0]["time_us"] if pressed_callbacks else None
    release_time = (
        next(
            (
                event["time_us"]
                for event in released_callbacks
                if press_time is None or event["time_us"] >= press_time
            ),
            None,
        )
        if released_callbacks
        else None
    )
    held_frames = (
        [
            frame
            for frame in frames
            if press_time <= frame["time_us"] < release_time
        ]
        if press_time is not None and release_time is not None
        else []
    )
    false_while_held = [frame for frame in held_frames if not frame.get("button")]
    hold_duration_ms = (
        round((release_time - press_time) / 1000, 3)
        if press_time is not None and release_time is not None
        else None
    )

    transitions = []
    previous = None
    for frame in frames:
        button = bool(frame.get("button"))
        if previous is None or button != previous:
            transitions.append(
                {
                    "sequence": frame["sequence"],
                    "button": button,
                    "contact_count": frame["contact_count"],
                }
            )
        previous = button

    continuous = bool(held_frames) and not false_while_held
    notes = []
    if held_frames:
        notes.extend(
            [
                f"Observed {len(held_frames)} frames during a {hold_duration_ms} ms physical hold.",
                (
                    "TP_FRAME_BUTTON stayed set throughout the measured hold."
                    if continuous
                    else f"{len(false_while_held)} frame(s) cleared TP_FRAME_BUTTON during the hold."
                ),
            ]
        )
    else:
        notes.append("No connected MTP1 frames were available for hold analysis.")
    if press_time is None or release_time is None:
        notes.append("A complete physical press/release callback pair was not captured.")
    if callback_registered and not callbacks:
        notes.append(
            "MTRegisterButtonStateCallback was registered, but it emitted no events."
        )

    output = {
        "schema_version": 1,
        "tested_windows_runtime_updated_at": runtime["updated_at"],
        "mac_source_commit": git_commit(repo),
        "connected": bool(connections),
        "physical_button_enabled": callback_registered,
        "button_callback_functional": bool(callbacks),
        "button_events": len(callbacks),
        "physical_drag": args.physical_drag,
        "raw_button_callbacks": [
            {"pressed": event["pressed"], "released": event["released"]}
            for event in callbacks
        ],
        "emitted_button_transitions": transitions,
        "held_button_frames": len(held_frames),
        "hold_duration_ms": hold_duration_ms,
        "button_held_continuously": continuous,
        "notes": notes,
    }
    output_path = args.output if args.output.is_absolute() else repo / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
