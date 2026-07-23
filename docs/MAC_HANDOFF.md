# macOS agent handoff

## Current Windows milestone

The Windows side has reached an end-to-end local-input milestone:

```text
synthetic MTP1 contact
    -> TCP receiver
    -> fixed-size IOCTL
    -> KMDF source driver
    -> vhf.sys
    -> Windows Precision Touchpad
    -> pointer movement
```

The original bring-up was validated with SDK/WDK 26100. The current Windows
build baseline is Visual Studio 2026 with SDK/WDK 28000:

- `Root\MtpVhfTouchpad` starts without a PnP problem.
- The source-device stack contains `MtpVhfTouchpad` over the required `vhf`
  lower filter.
- Windows enumerates `HID\VID_1209&PID_3987` as a HID-compliant touchpad.
- Windows selects Precision Touchpad input mode `3`.
- Device capabilities and PTPHQA feature reports are queried successfully.
- A 50-byte, five-contact PTP report is accepted by
  `VhfReadReportSubmit`.
- A synthetic single contact moves the Windows pointer.

The macOS task is now to replace `windows/tools/synthetic_touch.py` with the
existing real `mac-touch-agent` stream.

## Files that define the contract

- `protocol/TouchFrame.h` and `protocol/TouchFrame.c`: authoritative MTP1 wire
  encoder.
- `mac/Agent/main.c`: macOS TCP producer.
- `mac/Probe/MultitouchSupportABI.h`: isolated private Apple ABI declarations.
- `windows/receiver/Mtp1.cpp`: strict Windows decoder.
- `windows/receiver/TouchSession.cpp`: contact-ID to five-slot mapping.
- `docs/WINDOWS_HANDOFF.md`: byte-level protocol specification.

Do not create a second protocol implementation for macOS. Continue using the
shared C encoder in `protocol/TouchFrame.c`.

## Required contact semantics

Every active finger sent by macOS must set all three MTP1 flags:

```c
TP_CONTACT_IN_RANGE | TP_CONTACT_TIP | TP_CONTACT_CONFIDENCE
```

The numeric value is `0x07`.

This is important: Windows discards contacts whose Confidence bit is clear.
The local synthetic test initially used `0x03`, which produced valid IOCTL and
VHF submissions but no pointer movement.

For an active frame:

- `identifier` must remain stable from touch-down through lift.
- `x` and `y` must be finite and normalized to `[0.0, 1.0]`.
- Include the complete set of currently active contacts in every FRAME.
- Do not send duplicate identifiers in one frame.
- Send no more than ten MTP1 contacts; Windows exposes at most five.
- Prefer the five oldest active contacts if macOS observes more than five.

A finger disappears by being omitted from the next complete FRAME. The Windows
receiver emits the required one-frame HID lift report automatically.

## Timing requirements

`capture_time_us` must use a monotonic clock and represent microseconds. The
Windows receiver converts it to the PTP Scan Time unit:

```text
scan_time_100us = (capture_time_us / 100) mod 65536
```

Do not send a constant timestamp. An earlier fallback that advanced Scan Time
by only one unit per frame made a 125 Hz stream appear to Windows as a 10 kHz
stream and prevented normal pointer behavior.

Target active cadence:

- preferred: the native callback cadence, approximately 100–125 Hz;
- acceptable initial floor: 60 Hz;
- do not synthesize duplicate frames merely to reach a nominal rate.

## Session sequence

For every new TCP connection, send:

```text
HELLO(sequence=N)
RESET(sequence=N+1)
FRAME(sequence=N+2)
FRAME(sequence=N+3)
...
```

Sequence numbers must increase by exactly one for every message. On reconnect,
start a fresh session with HELLO and RESET. On capture discontinuity, queue
overflow, device replacement, or timestamp reset, send RESET before sending
new contacts.

The Windows receiver:

- listens on TCP port `39871` by default;
- uses a 200 ms contact-release timeout;
- rejects malformed, oversized, non-finite, duplicate-ID, or discontinuous
  streams;
- releases all contacts on disconnect.

## Windows test endpoint

For the current integration machine, the observed LAN endpoint on 2026-07-23
is:

```text
Windows IPv4: 192.168.31.115
TCP port:     39871
Subnet:       192.168.31.0/24
```

The address is assigned by the router and can change. Before a Mac integration
session, prepare Windows from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\tools\prepare_receiver.ps1
```

The script requests UAC, detects the current active IPv4 address, marks that
network Private, installs a firewall rule restricted to Private/LocalSubnet,
starts the receiver if necessary, and prints the exact Mac command.

To start the receiver manually:

```powershell
E:\formal_PHD\meney\touchpad\out\windows\Release\mtp-receiver.exe 39871
```

For traffic from the Mac, allow this executable through Windows Firewall on
**Private networks only**. Public-network access is unnecessary.

From macOS, test reachability before starting the agent:

```sh
nc -vz 192.168.31.115 39871
```

Then run:

```sh
./build/mac-touch-agent 192.168.31.115 39871
```

The agent does not intercept or suppress local macOS events. If the Mac should
ignore its built-in trackpad while an external mouse is present, use the macOS
system setting for that behavior rather than granting the agent global event
interception privileges.

The receiver should begin with:

```text
listening on TCP 39871
```

It must not print:

```text
driver not present; parse/log mode
```

After a client disconnect, the diagnostic build prints:

```text
driver status: submits=... last_ntstatus=0x0 report_bytes=50
active=0 input_mode=3 function=0x3 get_feature=2 set_feature=4
```

Healthy values are:

- `submits > 0`;
- `last_ntstatus=0x0`;
- `report_bytes=50`;
- `input_mode=3`;
- `function=0x3`.

## Build and run on macOS

Requirements:

- Apple Silicon Mac used for the existing feasibility work;
- Xcode Command Line Tools;
- access to the built-in trackpad;
- permission to run code using the private
  `MultitouchSupport.framework`.

Build:

```sh
make clean
make
```

First revalidate local capture:

```sh
./build/mac-capture-probe --duration 10 > touches.jsonl
python3 tools/analyze_capture.py touches.jsonl
```

Then stream to the Windows private IPv4 address:

```sh
./build/mac-touch-agent 192.168.1.20 39871 --duration 30
```

Replace `192.168.1.20` with the Windows host's LAN address.

## Integration acceptance test

Use this order:

1. Confirm both machines are on a trusted private network.
2. Start `out/windows/Release/mtp-receiver.exe` on Windows.
3. Start `mac-touch-agent` on macOS.
4. Place one finger on the Mac trackpad and move it slowly.
5. Confirm the Windows pointer follows without jumps and vertical direction is
   correct. The Windows receiver intentionally maps `hid_y = 1 - mac_y`.
6. Lift the finger and confirm the pointer stops immediately.
7. Test two-finger scrolling.
8. Test two-finger pinch.
9. Test three- and four-finger Windows gestures.
10. Stop the Mac agent while fingers are down and confirm Windows releases all
    contacts within 200 ms.
11. Restart the Mac agent and confirm HELLO/RESET reconnects cleanly.

Record the receiver's final diagnostic line and the macOS agent's callback
rate for each run.

## Known limitations and next work

- The macOS capture API is private and may change across macOS releases.
- The current TCP transport is intended for a trusted LAN; pairing and TLS are
  not implemented.
- Driver installation currently uses Windows test-signing mode.
- The receiver is a foreground executable, not yet a Windows service.
- Sleep/wake, network changes, and long-duration stability still need testing.
- Five-contact mapping is implemented, but real Mac multi-finger gesture
  behavior remains to be validated end to end.

The next required user action is on the Mac: build the current agent, point it
at the Windows host, and report the first real-contact diagnostic line.
