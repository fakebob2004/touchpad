# macOS setup and operation

[简体中文](MAC_SETUP.zh-CN.md)

The macOS side is a foreground command-line agent. It reads raw contacts from the built-in trackpad
and sends complete MTP1 frames to the Windows receiver. It does not install a system service, inject
events, or suppress local macOS input.

## 1. Optional: make macOS ignore the built-in trackpad

When an external mouse is connected, macOS can ignore the built-in trackpad without any event
interception by this project:

```text
System Settings
→ Accessibility
→ Pointer Control
→ Mouse & Trackpad
→ Ignore built-in trackpad when mouse or wireless trackpad is present
```

Enable this setting if the Mac should remain controlled by an external mouse while its built-in
trackpad is dedicated to Windows. The option only takes effect while macOS detects a mouse or
wireless trackpad.

## 2. Build

Install Xcode Command Line Tools, then:

```sh
cd /path/to/touchpad
make clean
make
```

Produced tools:

- `build/mac-capture-probe`: local raw-contact diagnostics;
- `build/mac-touch-agent`: MTP1 TCP sender.

## 3. Verify raw capture

```sh
./build/mac-capture-probe --duration 10 > touches.jsonl
python3 tools/analyze_capture.py touches.jsonl
```

Move one, two, and five fingers during the capture. A healthy tested machine reports:

```text
max_contacts: 5
estimated_hz: approximately 125
duplicate_id_frames: []
invalid_json_lines: []
```

The live `overall_rate` includes idle time with no fingers and can be lower than the active hardware
rate.

## 4. Prepare and connect to Windows

Start the Windows receiver first. Use its current private IPv4 address:

```sh
nc -vz WINDOWS_IP 39871
./build/mac-touch-agent WINDOWS_IP 39871
```

Example:

```sh
./build/mac-touch-agent 192.168.31.115 39871
```

Expected startup:

```text
connected to WINDOWS_IP:39871
streaming 1 built-in trackpad(s) to WINDOWS_IP:39871
```

Stop with `Ctrl-C`. The agent reconnects automatically while it remains running.

## 5. Acceptance test

1. Move one finger and confirm the Windows pointer follows in both axes.
2. Lift the finger and confirm movement stops immediately.
3. Verify native two-finger scrolling.
4. Verify native pinch zoom.
5. Stop the agent with fingers down and confirm Windows releases contacts within 200 ms.
6. Restart the receiver and agent and confirm a fresh `HELLO`/`RESET` session succeeds.

## Troubleshooting

### `captured=0`

No raw callbacks occurred during the run. Touch and move the built-in trackpad while the agent is
active. Re-run `mac-capture-probe` to separate capture problems from networking.

### Agent never prints `connected`

- Start the Windows receiver first.
- Confirm the Windows network profile is Private.
- Run `nc -vz WINDOWS_IP 39871`.
- Check the Private/LocalSubnet firewall rule on Windows.

### Mac still reacts to the built-in trackpad

The agent intentionally does not suppress local events. Connect an external mouse and enable the
macOS setting in step 1. If macOS does not detect an external pointing device, that setting does not
disable the built-in trackpad.

### Capture stops after a macOS update

The project relies on private `MultitouchSupport.framework` symbols and an inferred touch structure.
Run the probe first and report the macOS version, Mac model, startup diagnostics, and capture file
analysis. Do not silently change the ABI layout.

## Security

MTP1 currently uses unauthenticated, unencrypted TCP. Use it only on a trusted private LAN and do
not expose port 39871 to the internet.
