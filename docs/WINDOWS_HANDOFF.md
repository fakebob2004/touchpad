# Windows receiver handoff: MTP1 to Precision Touchpad

Status: Mac capture and TCP sender are implemented and tested on Apple Silicon macOS. The built-in
trackpad produces five concurrent contacts at a median 8 ms hardware interval (approximately
125 Hz), with stable per-contact identifiers and no observed frame-number gaps.

## Deliverables from the Mac side

- `build/mac-touch-agent`: connects to a Windows IP and streams raw contact frames.
- `protocol/TouchFrame.h` and `protocol/TouchFrame.c`: authoritative wire codec.
- `tools/debug_receiver.py`: executable reference TCP parser.
- `tests/protocol_test.c`: byte-order and round-trip test.
- Default TCP port: `39871`.

Run the Mac sender with:

```text
./build/mac-touch-agent WINDOWS_IP [PORT] [--duration SECONDS]
```

The current milestone intentionally has no discovery, pairing, encryption, service installation,
or GUI. Use a trusted wired LAN during development.

## TCP stream rules

TCP is a byte stream. A single `recv` is not guaranteed to return one complete message. The receiver
must:

1. Read exactly 36 bytes for the header.
2. Validate magic, version, type, payload length, and contact count.
3. Read exactly `payload_length` additional bytes.
4. Parse contacts only after the complete payload arrives.

All integers and IEEE-754 `float32` bit patterns are **big-endian**. There is no implicit C struct
padding on the wire.

Maximum message size in version 1 is:

```text
36 + 10 * 44 = 476 bytes
```

Reject a message or connection when:

- magic is not ASCII `MTP1`;
- version is not 1;
- type is unknown;
- `contact_count > 10`;
- `payload_length != contact_count * 44`;
- FRAME has unknown header flag bits;
- HELLO or RESET has a nonzero contact count or flags;
- any coordinate is NaN or infinite.

## Header: 36 bytes

| Offset | Size | Type | Field | Version 1 meaning |
|---:|---:|---|---|---|
| 0 | 4 | bytes | magic | ASCII `MTP1`, hex `4D 54 50 31` |
| 4 | 2 | u16 | version | `1` |
| 6 | 2 | u16 | message_type | HELLO=1, FRAME=2, RESET=3 |
| 8 | 4 | u32 | payload_length | `contact_count * 44` |
| 12 | 4 | u32 | sequence | Monotonic within the current connection epoch |
| 16 | 2 | u16 | contact_count | Number of contacts in this complete frame |
| 18 | 2 | u16 | flags | FRAME: `BUTTON=bit 0`; HELLO/RESET: zero |
| 20 | 8 | u64 | capture_time_us | Mac monotonic clock, microseconds |
| 28 | 8 | u64 | device_time_us | Trackpad timestamp converted to microseconds |

`capture_time_us` and `device_time_us` are useful for logging and latency analysis. They are not
wall-clock timestamps and must not be compared directly with the Windows wall clock.

## Contact record: 44 bytes

| Offset within contact | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 4 | u32 | identifier | Stable raw Mac contact ID for this touch lifecycle |
| 4 | 1 | u8 | state | Raw MultitouchSupport state, described below |
| 5 | 1 | u8 | flags | IN_RANGE=bit 0, TIP=bit 1, CONFIDENCE=bit 2 |
| 6 | 2 | u16 | reserved | Zero |
| 8 | 4 | f32 | x | Normalized X, normally 0...1 |
| 12 | 4 | f32 | y | Normalized Y, normally 0...1 |
| 16 | 4 | f32 | velocity_x | Raw normalized X velocity |
| 20 | 4 | f32 | velocity_y | Raw normalized Y velocity |
| 24 | 4 | f32 | size | Raw contact-size signal |
| 28 | 4 | f32 | angle | Raw ellipse angle, radians |
| 32 | 4 | f32 | major_axis | Raw contact ellipse major axis |
| 36 | 4 | f32 | minor_axis | Raw contact ellipse minor axis |
| 40 | 4 | f32 | density | Raw density signal; do not assume calibrated pressure |

Version 1 transmits the raw geometry fields for research and later calibration. The Windows HID
implementation uses `identifier`, contact `flags`, `x`, `y`, and the frame-level `BUTTON` flag.
`BUTTON` comes from the private framework's button-state callback and maps to HID Button 1. Do not
substitute `density` for pressure until its behavior has been characterized across Mac models.

### Physical button handoff

The complete click path is:

```text
MTRegisterButtonStateCallback pressed/released
    -> MTP1 FRAME header BUTTON bit
    -> MTP_IOCTL_FRAME_BUTTON
    -> Precision Touchpad input report Button 1
```

Button transitions are sent immediately using the latest complete contact snapshot. RESET,
disconnect, and timeout release paths must clear the button. The MTP1 layout and IOCTL structure
sizes are unchanged, but both the Windows receiver and driver must be rebuilt because their flag
semantics changed.

## Raw Mac states and flags

The private framework state values observed and conventionally named by existing implementations
are:

| State | Conventional meaning | IN_RANGE | TIP |
|---:|---|---:|---:|
| 0 | Not tracking | 0 | 0 |
| 1 | Start in range | 1 | 0 |
| 2 | Hover in range | 1 | 0 |
| 3 | Make touch | 1 | 1 |
| 4 | Touching | 1 | 1 |
| 5 | Break touch | 1 | 0 |
| 6 | Linger in range | 1 | 0 |
| 7 | Out of range | 0 | 0 |

These names are inferred from a private ABI. The transmitted flags are the protocol contract; the
Windows implementation should not independently reinterpret raw `state` for HID Tip Switch.

## Message lifecycle

Every successful TCP connection begins with:

```text
HELLO
RESET
FRAME...
```

- HELLO declares the MTP1 stream version through its header. It has no payload in version 1.
- RESET means release every Windows contact slot immediately and clear all ID mappings.
- FRAME is a complete hardware frame, not a delta. A FRAME with zero contacts releases all slots.

The Windows service must also perform RESET behavior when:

- TCP disconnects;
- no complete FRAME arrives for 200 ms while contacts are active;
- sequence moves backwards or unexpectedly skips;
- parsing or validation fails;
- the service or driver is stopping.

The Mac sender uses a 256-frame nonblocking queue. On queue overflow it discards stale queued frames
and forces a reconnect. The new connection starts with HELLO followed by RESET before accepting new
complete frames. This avoids both stale motion and an invalid sequence gap, while the old connection
close causes Windows to release all active contacts.

## Contact ID to HID slot mapping

Do not use the raw Mac ID as a HID array index. Maintain a map:

```text
Mac identifier -> Windows contact slot 0...9
```

Recommended algorithm for each complete FRAME:

1. Validate that all Mac identifiers in the frame are unique.
2. Reuse an existing slot for every known identifier.
3. Assign the lowest free slot to every new identifier.
4. Submit contacts whose TIP flag is set as active.
5. Submit one Tip=0 report where required for contacts that left the previous frame.
6. Release mappings only after the corresponding lift has been delivered to VHF.

A RESET releases every active slot before clearing the map.

## Windows process boundary

Keep networking out of the kernel driver:

```text
Mac Agent
    -> TCP 39871
Windows Receiver Service
    -> validated fixed-size IOCTL or shared ring
KMDF HID source driver using VHF
    -> Windows HIDClass / Precision Touchpad stack
```

The user-mode receiver owns TCP parsing, reconnect policy, validation, contact mapping, diagnostics,
and timeout handling. The KMDF driver owns only VHF device creation, HID Feature Reports, converting
validated frames to HID Input Reports, and fail-safe contact release.

## Precision Touchpad HID requirements

Use Microsoft's Precision Touchpad sample report descriptor as the baseline. The top-level collection
must be Digitizers page `0x0D`, Touch Pad usage `0x05`. Implement at minimum:

- per contact: Contact Identifier, Tip Switch, Confidence, X, Y;
- per frame: Contact Count, Scan Time, and integrated Button 1 state;
- Device Capabilities Feature Report, including maximum contact count and button type;
- Device Certification Status Feature Report with the required 256-byte vendor-defined field;
- optional Latency Mode Feature Report;
- `VhfReadReportSubmit` for input delivery.

Relevant Microsoft documentation:

- https://learn.microsoft.com/windows-hardware/design/component-guidelines/touchpad-protocol-implementation
- https://learn.microsoft.com/windows-hardware/design/component-guidelines/touchpad-windows-precision-touchpad-collection
- https://learn.microsoft.com/windows-hardware/drivers/hid/virtual-hid-framework--vhf-

For the first integration, use five HID slots even though MTP1 permits ten. Five concurrent contacts
have been verified on the current Mac hardware.

## Coordinate conversion

Clamp normalized coordinates before converting to the HID logical range:

```text
hid_x = round(clamp(x, 0, 1) * logical_max_x)
hid_y = round(clamp(1 - y, 0, 1) * logical_max_y)
```

Real Mac-to-Windows testing confirmed that MultitouchSupport Y grows in the opposite direction from
the Windows PTP surface, so the receiver inverts Y. Keep physical size and logical maximum consistent
in the HID descriptor; Windows gesture thresholds can depend on the declared physical dimensions.

## Reference parser

`tools/debug_receiver.py` is the normative parsing example. Its Python contact format is:

```python
HEADER = struct.Struct(">4sHHIIHHQQ")
CONTACT = struct.Struct(">IBBHfffffffff")
```

This is directly translatable to explicit big-endian reads in C# or C++. Do not marshal the network
bytes directly into a native Windows struct because Windows is little-endian and native padding can
differ.

## Integration acceptance criteria

Before connecting the receiver to VHF:

- HELLO and RESET parse correctly after arbitrary TCP fragmentation;
- a five-contact FRAME parses with the same IDs and float values as the debug receiver;
- disconnect and 200 ms timeout both release all contacts;
- malformed length, excessive contact count, NaN, and duplicate IDs are rejected;
- sustained 125 Hz five-contact input produces no unbounded queue growth;
- sequence discontinuity produces a reset, never a stuck contact.

End-to-end acceptance:

- Windows Device Manager shows the HID touchpad;
- Windows Settings exposes the Touchpad page;
- two-finger scrolling and pinch are generated by Windows, not synthesized mouse events;
- three- and four-finger actions follow Windows Touchpad settings;
- reconnect, service restart, sleep, and Mac Agent termination leave no ghost contacts.
