# Mac Precision Touchpad bridge

The first milestone is a macOS feasibility probe that reads raw contact frames from the built-in
trackpad. It uses Apple's private `MultitouchSupport.framework`; this is suitable for research and
direct distribution, not the Mac App Store, and its ABI can change between macOS releases.

## Build and run the probe

Requirements: macOS with Xcode Command Line Tools installed.

```sh
make
./build/mac-capture-probe
```

Run for a fixed interval and save JSON Lines while keeping diagnostics separate:

```sh
./build/mac-capture-probe --duration 10 > touches.jsonl
```

Touch the built-in trackpad while it runs. Frame records are written to stdout; device discovery,
errors, and the final sampling-rate summary are written to stderr. Stop an unbounded run with
Ctrl-C.

Analyze a saved capture with:

```sh
python3 tools/analyze_capture.py touches.jsonl
```

`overall_rate` in the live summary includes periods with no fingers on the pad. The analyzer's
`active_device_interval_ms` and `estimated_hz` describe the hardware callback cadence while active.

## Current acceptance checks

- The built-in device is discovered on Apple Silicon.
- Contact IDs remain stable from touch-down through lift-off.
- Five simultaneous contacts are reported.
- The measured callback rate is approximately 100 Hz or better under movement.
- Capture continues while Terminal is not focused.

The private ABI declarations are isolated in `mac/Probe/MultitouchSupportABI.h`. If a future macOS
release changes the structure layout or required symbols, the probe fails near startup instead of
silently feeding data into the eventual network protocol.

## Stream to a receiver

Start the reference receiver on the destination machine or locally:

```sh
python3 tools/debug_receiver.py
```

Then point the Mac agent at its IP address. TCP port 39871 is the default:

```sh
./build/mac-touch-agent 192.168.1.20
```

For a bounded test or a non-default port:

```sh
./build/mac-touch-agent 192.168.1.20 40000 --duration 15
```

The agent reconnects automatically, uses `TCP_NODELAY`, and keeps network writes off the private
framework callback thread. A reconnect or queue overflow emits a RESET so the receiver can release
all active contacts safely.

The versioned wire format and Windows implementation contract are documented in
`docs/WINDOWS_HANDOFF.md`.
