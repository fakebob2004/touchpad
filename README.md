# MacBook Precision Touchpad Bridge

[简体中文](README.zh-CN.md) · [Windows handoff](docs/WINDOWS_HANDOFF.md) · [License](LICENSE)

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![macOS](https://img.shields.io/badge/macOS-Apple%20Silicon-black.svg)](#requirements)
[![Windows](https://img.shields.io/badge/Windows-11%20x64-0078D4.svg)](#requirements)

## Why?

Many programmers already work with both hands:

- the right hand uses a mouse for precise pointing, selection, drawing, or CAD;
- the left hand scrolls documents, papers, browsers, and code, and performs pinch zoom.

Great standalone Windows touchpads are uncommon, but many desks already have a MacBook sitting next
to the Windows PC. Instead of buying another touchpad, this project turns the MacBook's built-in
trackpad into a native Windows Precision Touchpad.

```text
┌─────────────────────────────┐
│          Windows PC         │
└─────────────────────────────┘

         ⌨ Keyboard

MacBook trackpad          Mouse
   left hand            right hand
 scroll / zoom       precise operation
```

> **Don't buy another touchpad. Reuse the best one you already have.**

macOS captures raw contacts over a trusted wired LAN; Windows exposes them through KMDF and Virtual
HID Framework (VHF), so scrolling, pinch zoom, and system gestures are handled by the native Windows
touchpad stack rather than mouse-event emulation.

> **Experimental:** the end-to-end prototype moves the Windows pointer and supports native
> two-finger scrolling and pinch zoom. It currently requires a test-signed Windows driver and a
> private macOS framework. It is not ready for unattended or security-sensitive deployment.

## What works

| Capability | Status |
|---|---|
| Raw built-in MacBook trackpad capture | Verified, 5 contacts at about 125 Hz |
| Versioned MTP1 protocol over TCP | Implemented |
| Reconnect, reset, timeout, and contact release | Implemented |
| Windows VHF Precision Touchpad enumeration | Verified on Windows 11 x64 |
| Pointer movement | Verified with real Mac input |
| Native two-finger scrolling | Verified |
| Native pinch zoom | Verified |
| Physical press / click (Precision Touchpad Button 1) | Implemented; awaiting real Windows validation |
| Three- and four-finger gestures | Pending wider real-device validation |
| Pairing, authentication, and encryption | Not implemented |
| Production driver signing and installer | Not implemented |

## Architecture

```text
MacBook built-in trackpad
    -> MultitouchSupport.framework
    -> mac-touch-agent
    -> MTP1 / TCP 39871
    -> Windows receiver
    -> fixed-size IOCTL
    -> KMDF + VHF
    -> Windows Precision Touchpad stack
```

The Mac sends complete raw contact frames, not gestures. Networking stays in user mode. The Windows
driver accepts only a bounded, validated structure and submits a five-contact Precision Touchpad
report through VHF.

## Requirements

### macOS

- Apple Silicon MacBook with a built-in trackpad
- Xcode Command Line Tools
- Current prototype validated on modern macOS

The capture path dynamically loads Apple's private `MultitouchSupport.framework`. Its ABI can
change between macOS releases, and software using it is not suitable for the Mac App Store.

### Windows

- Windows 11 x64
- Visual Studio 2026 with C++ desktop tools
- Windows SDK/WDK 10.0.28000
- Administrator access
- Test-signing mode for the current driver package

## Quick start

### 1. Build and verify macOS capture

```sh
make clean
make
./build/mac-capture-probe --duration 10 > touches.jsonl
python3 tools/analyze_capture.py touches.jsonl
```

See the [complete macOS operation guide](docs/MAC_SETUP.md) and
[macOS development notes](docs/MAC_DEVELOPMENT.md).

### 2. Prepare Windows

Build and install the KMDF/VHF driver and receiver first. The current bring-up and restart procedure
is documented in:

- [Complete Windows setup guide (Chinese)](docs/WINDOWS_SETUP.zh-CN.md)
- [Windows restart and integration handoff](docs/WINDOWS_RESTART_HANDOFF.md)
- [VHF startup issue and resolution](docs/VHF_STARTUP_ISSUE.md)

From an Administrator PowerShell at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\tools\prepare_receiver.ps1
```

The script configures a Private/LocalSubnet firewall rule, starts the receiver, and prints the exact
Mac command.

### 3. Stream the trackpad

```sh
nc -vz WINDOWS_IP 39871
./build/mac-touch-agent WINDOWS_IP 39871
```

Stop with `Ctrl-C`. On disconnect or a 200 ms active-contact timeout, Windows releases all contacts.

The agent does not intercept local macOS input. To dedicate the built-in trackpad to Windows while
an external mouse controls the Mac, enable:

```text
System Settings → Accessibility → Pointer Control → Mouse & Trackpad
→ Ignore built-in trackpad when mouse or wireless trackpad is present
```

## Repository layout

```text
mac/            raw contact probe and TCP agent
protocol/       authoritative MTP1 wire codec
windows/driver/ KMDF VHF source driver
windows/receiver/
                TCP parser, contact mapping, and driver IOCTL client
windows/tests/  parser and session tests
windows/tools/  Windows preparation and synthetic-input tools
tools/          macOS capture analyzer and reference receiver
docs/           protocol, integration, and bring-up notes
```

## Protocol and safety properties

MTP1 uses a fixed 36-byte big-endian header and up to ten 44-byte contact records. Every TCP session
starts with `HELLO`, then `RESET`, followed by strictly increasing `FRAME` messages. The Windows
receiver rejects malformed lengths, duplicate IDs, non-finite values, sequence discontinuities, and
oversized contact sets.

See [Windows receiver handoff](docs/WINDOWS_HANDOFF.md) for the byte-level specification.

This prototype listens on a trusted LAN without authentication or encryption. Do not expose TCP
39871 to a public network.

## Development

Run the portable protocol test on macOS:

```sh
make test
```

Run Windows receiver tests from a Developer PowerShell:

```powershell
cmake -S windows -B out\windows -A x64
cmake --build out\windows --config Release
ctest --test-dir out\windows -C Release --output-on-failure
```

Contributions should keep networking out of kernel mode, preserve complete-frame semantics, and add
tests for protocol or contact-lifecycle changes. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Development process

This prototype was developed end to end through **vibe coding with GPT-5.6 Sol (Light)**, from the
macOS feasibility probe and MTP1 protocol to the Windows VHF driver and documentation. It used
approximately 50% of one weekly model allowance.

The work was completed in two main development sessions of roughly three hours each. When the
Windows driver showed the Device Manager yellow warning icon, a web high-reasoning mode briefly
joined the investigation to isolate the `VhfCreate` startup failure and its missing `vhf` lower
filter.

Physical-device actions, installation, risk decisions, and end-to-end acceptance tests were
performed by the project owner. AI-generated code was compiled and tested on the actual Mac and
Windows machines before being merged.

## Roadmap

- Validate three- and four-finger gestures with multiple MacBook generations
- Package the Windows receiver as a service
- Add device pairing, authentication, and encrypted transport
- Add production signing and an installer
- Test sleep/wake, network transitions, and long-duration recovery
- Add a stable public-API fallback for reduced-function macOS capture

## License

The project is licensed under the [Apache License 2.0](LICENSE). This permissive license supports
commercial and non-commercial use and includes an explicit contributor patent grant.

Some Windows Precision Touchpad report semantics were adapted from the MIT-licensed SPI portion of
`imbushuo/mac-precision-touchpad`. GPL-licensed implementations were studied only as architectural
references and are not included. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[the reuse assessment](docs/OPEN_SOURCE_REUSE.md).

Apple, MacBook, macOS, Microsoft, Windows, and Precision Touchpad are trademarks of their respective
owners. This project is independent and is not endorsed by Apple or Microsoft.
