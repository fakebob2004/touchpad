# Open-source reuse assessment

This note records the reference revisions inspected before implementing the Windows receiver and
virtual Precision Touchpad. The repositories are local research inputs under `third_party/` and are
not vendored into this project.

## Pinned reference revisions

| Project | Revision inspected | License relevant to reuse | Intended use |
|---|---|---|---|
| `imbushuo/mac-precision-touchpad` | `29d277880f167a3cead6c1fe8b4eba58bc168a05` | SPI KMDF project: MIT; USB projects: GPLv2 | Precision Touchpad descriptor, feature reports, packed input report layout, coordinate/report behavior |
| `microsoft/Windows-driver-samples` | `2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89` | Microsoft Public License | General KMDF/UMDF HID minidriver and user/driver communication patterns |
| `input-leap/input-leap` | `34a34fb20b93113a6b26052cb5a54f9be2327775` | GPLv2 with OpenSSL exception | Architectural reference for reconnect, TLS, certificate fingerprints, sockets, and stream framing |

## Project scope and repository license

The repository is publicly distributed under Apache License 2.0. The license was selected to permit
commercial and non-commercial use while providing an explicit contributor patent grant. The current
prototype still uses Windows test signing and omits an installer, discovery, pairing UI, production
certificates, authentication, and encryption.

Third-party policy:

- MIT-licensed SPI descriptor/report material may be adapted when its copyright and permission
  notice are retained in `THIRD_PARTY_NOTICES.md`;
- GPL USB and Input Leap source code may be studied as a behavioral or architectural reference but
  must not be copied into this Apache-2.0 codebase;
- Microsoft documentation defines the platform contract; Microsoft sample source requires a
  separate license review before any substantial copying;
- every new third-party code or asset addition must record its source, revision, license, and files
  affected before merge.

## Corrections to the initial reuse hypothesis

### mac-precision-touchpad is not a VHF implementation

The project implements HID minidrivers that service HID IOCTLs and pending read requests directly.
There are no `VhfCreate`, `VhfStart`, or `VhfReadReportSubmit` calls. Its transport and device
lifecycle code therefore should not be forked as the base of the network device.

The reusable part is narrower and more valuable:

- `src/AmtPtpDeviceSpiKm/Hid.h` defines five-contact packed reports, capabilities, mode selection,
  and the 256-byte certification report;
- `src/AmtPtpDeviceSpiKm/Hid.c` contains the report descriptor and feature-report behavior;
- the SPI project is MIT-licensed, while both USB projects are GPLv2 and must not be copied into a
  closed-source product;
- input conversion routines are useful for behavioral comparison, but Apple packet parsing and
  physical-device queues are out of scope.

The MIT SPI code may be adapted with attribution. GPL USB code remains a behavioral reference only.
The descriptor and report path must still be revalidated against current Microsoft Precision
Touchpad documentation and submitted with VHF. This is reuse of the HID contract and conversion
logic, not an 80% transport-layer fork.

### vhidmini2 is not the VHF sample

`hid/vhidmini2` is a HID minidriver sample. It demonstrates HID descriptors, feature reports, and a
test application, but it does not demonstrate VHF. The authoritative VHF implementation pattern is
the Microsoft documentation for `VhfCreate`, `VhfStart`, asynchronous feature callbacks, and
`VhfReadReportSubmit`.

The new driver should be a small KMDF HID source driver using VHF rather than a modified copy of
`vhidmini2`.

### Input Leap is an architectural reference, not a networking library dependency

Input Leap supplies mature reconnect, certificate-fingerprint, TLS socket, event-loop, and framing
ideas. Its code is GPLv2 and is not incorporated into this Apache-2.0 repository. Adopting the
implementation would also pull in a much larger runtime than a 125 Hz stream of at most 476-byte
MTP1 messages needs.

For the first trusted-LAN milestone, retain the existing bounded MTP1 TCP protocol and implement the
receiver with native Windows sockets. Add authentication and TLS only if the local network setup
actually requires them.

## Selected implementation architecture

```text
macOS MultitouchSupport
    -> existing MTP1 sender
    -> TCP receiver service (native implementation)
    -> validated fixed-size IOCTL
    -> new KMDF VHF source driver
       - VHF lifecycle from Microsoft documentation
       - PTP descriptor/report semantics adapted from MIT SPI code
    -> HIDClass / Windows Precision Touchpad
```

Networking stays in user mode. The kernel boundary accepts only a fixed-size, versioned structure
containing at most five already-validated contacts. Both service and driver independently clamp
counts and coordinates, and either side releases all contacts on shutdown or timeout.

For the current milestone, the "service" is a foreground console application. Turning it into a
Windows service is deferred until the data path and gestures are stable. TCP must not be moved into
the kernel driver: doing so would add kernel networking, reconnect, cancellation, and teardown risk
without removing the need for a user-facing diagnostic process.

## Why not add `AmtPtpDeviceNetworkKm`

The inspected mac-precision-touchpad projects are physical-device HID minidrivers, not a driver with
a replaceable transport interface. USB, SPI, interrupt handling, pending HID reads, device startup,
and report production are coupled inside their respective projects. A nominal network variant would
still require removing the physical-device lifecycle and adding a safe user/kernel ingress path.

A new VHF source driver therefore has less inherited code but a smaller state space. We will copy or
adapt the proven descriptor, feature reports, packed report definitions, and useful conversion
behavior while implementing only the VHF lifecycle and one fixed IOCTL ingress queue.

## Implementation order

1. Build a dependency-free foreground MTP1 receiver and stream state machine with fragmentation, malformed-input,
   duplicate-ID, sequence-gap, disconnect, timeout, and sustained-rate tests.
2. Define the fixed-size service-to-driver IOCTL ABI and slot/lift semantics.
3. Create the minimal KMDF VHF driver and validate enumeration plus feature reports before accepting
   live network input.
4. Port the five-contact descriptor and report structures from the MIT SPI project, checking every
   field against current Microsoft documentation.
5. Connect the service to the driver and run synthetic end-to-end reports.
6. Only then request a Mac sender run for coordinate orientation and real gesture acceptance tests.
7. Optionally add service installation, pairing, authentication, and encryption after the personal
   trusted-LAN data path is stable.

## Build-environment implication

The old mac-precision-touchpad project targets `WindowsKernelModeDriver10.0` and KMDF 1.23. It is a
reference, not the build baseline. The new project should target the locally installed current WDK
and explicitly choose a supported minimum Windows version. No dependency on the legacy solution or
its signing configuration is required.
