# Agent integration handoff

Runtime coordination between the Windows and macOS agents is exchanged through
versioned files in this directory instead of manual copy/paste.

## Windows to macOS

The Windows agent owns `windows-runtime.json`. The macOS agent should pull the
latest integration branch, read that file, and use its exact address, port,
minimum commit, and startup marker. Values such as `WINDOWS_IP` in general
documentation are placeholders and must never be passed literally.

## macOS to Windows

After a real-device run, the macOS agent should create or update
`mac-validation.json` with this shape:

```json
{
  "schema_version": 1,
  "tested_windows_runtime_updated_at": "copy from windows-runtime.json",
  "mac_source_commit": "full Git commit",
  "connected": true,
  "physical_button_enabled": true,
  "button_events": 2,
  "physical_drag": "failed",
  "raw_button_callbacks": [
    {"pressed": 1, "released": 0},
    {"pressed": 0, "released": 1}
  ],
  "emitted_button_transitions": [
    {"sequence": 100, "button": true, "contact_count": 1},
    {"sequence": 350, "button": false, "contact_count": 1}
  ],
  "notes": ["Add exact symptoms here."]
}
```

`button_events` must increase for physical press and release. Use
`"physical_drag": "failed"` and place exact symptoms in `notes` if Windows
does not drag despite nonzero button events. For a two-second hold, the
frame-level button value must remain true for every intervening contact frame;
a short true/false pulse is only a click and cannot drag.

Do not put credentials, certificate private keys, tokens, or public-network
addresses in handoff files.

### Mac capture commands

Build the current agent, then perform one deliberate two-second physical
press-and-hold while moving one finger:

```sh
make build/mac-touch-agent
./build/mac-touch-agent 192.168.31.115 39871 \
  --duration 15 \
  --button-trace /tmp/mac-button-trace.jsonl
```

Record whether dragging worked, then generate the response:

```sh
python3 tools/analyze_button_trace.py /tmp/mac-button-trace.jsonl \
  --physical-drag failed
```

Use `--physical-drag passed` if text selection or window dragging actually
worked. The analyzer checks every emitted contact frame between the first
physical press and release and writes `handoff/mac-validation.json`.
