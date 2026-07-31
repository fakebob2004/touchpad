# macOS development notes

[简体中文](MAC_DEVELOPMENT.zh-CN.md)

The macOS implementation is intentionally small. Windows owns gesture recognition and the native
Precision Touchpad contract; the Mac has three responsibilities: capture raw contacts faithfully,
encode complete MTP1 frames, and deliver current state with safe reconnect behavior.

## 1. The original feasibility risk

The project depended on whether macOS could expose stable raw contacts rather than only high-level
scroll and gesture events. Public `NSTouch` can report normalized positions in an application event
path, but it was not assumed to provide the required global, background, and geometry behavior.

The feasibility probe therefore dynamically loads Apple's private:

```text
/System/Library/PrivateFrameworks/MultitouchSupport.framework
```

Required symbols are resolved with `dlopen`/`dlsym`, so the binary does not directly link against an
undocumented SDK interface. All inferred structures and function types are isolated in
`mac/Probe/MultitouchSupportABI.h`, including a compile-time size assertion for `MTTouch`.

## 2. What the probe established

Real capture testing established:

- one built-in device is discoverable on the tested Apple Silicon MacBook;
- five simultaneous contacts are available;
- contact IDs remain stable through a touch lifecycle;
- active hardware cadence has an 8 ms median interval, approximately 125 Hz;
- position, velocity, size, ellipse axes, angle, and density fields contain usable values;
- the private `MTRegisterButtonStateCallback` symbol is available and reports physical
  pressed/released transitions without estimating force from contact density;
- no invalid frames or duplicate IDs were observed in the recorded test sessions.

The probe writes JSON Lines to stdout and diagnostics to stderr. A private-framework hardware banner
initially polluted stdout; startup output is now redirected so capture files remain valid JSONL.

## 3. Why the Mac sends contacts, not gestures

Recognizing scroll, pinch, and multi-finger gestures on macOS would turn the project into remote
mouse emulation and lose native Windows settings. MTP1 therefore carries the complete current
contact set:

```text
identifier + state + flags + x/y + geometry + button state + monotonic timestamp
```

Windows maps those contacts to HID slots and lets its Precision Touchpad stack recognize gestures.
This is why native two-finger scrolling and pinch zoom work without Mac-side gesture code.

Physical clicking is also not recognized as a gesture on the Mac. The agent forwards the private
framework's button transition as the MTP1 frame-level `BUTTON` bit. It emits an immediate frame
with the latest complete contact snapshot on both press and release, preventing a late release from
leaving Windows in a stuck-button state.

On the tested Force Touch MacBook, `MTRegisterButtonStateCallback` registers but emits no events.
The inferred `MTTouch.pressure` field is therefore used as a calibrated fallback. Separate captures
measured a light-touch maximum of 68 and a force-hold median of 189, so the default press threshold
is 90 for this hardware. Three consecutive frames must cross the threshold. Once pressed, Button 1
stays latched until every tip contact lifts; pressure dips while dragging cannot release it.

Override the threshold with `--pressure-threshold VALUE`, or use `0` to disable pressure clicking.

## 4. Real-time and reconnect design

The private callback must not block on TCP. `mac-touch-agent` encodes on the callback path, places
bounded messages into a 256-frame queue, and performs socket writes on a sender thread with
`TCP_NODELAY`.

Every connection starts:

```text
HELLO(sequence=N)
RESET(sequence=N+1)
FRAME(sequence=N+2)
```

An early prototype reused sequence numbers for control messages. The strict Windows receiver
correctly rejected that stream after restart. Sequence allocation was fixed to advance for every
message. If the bounded queue overflows, the agent reconnects and starts a new `HELLO`/`RESET` epoch
instead of continuing with an invalid gap.

## 5. Coordinate orientation

The wire protocol preserves raw normalized Mac coordinates. Real end-to-end testing showed that the
Mac Y axis and Windows Precision Touchpad surface grow in opposite directions. The correction belongs
in the Windows mapping layer:

```text
hid_y = 1 - mac_y
```

Keeping this out of the wire format preserves raw sensor meaning and allows other receivers to make
their own coordinate choices.

## 6. Local macOS input policy

A CGEventTap-based suppression experiment was considered, but rejected:

- it requires Accessibility/Input Monitoring privileges;
- it cannot reliably distinguish the built-in trackpad from an external mouse;
- global interception creates unnecessary safety and usability risk.

The final design does not suppress macOS input. Users who dedicate the built-in trackpad to Windows
use macOS's own “Ignore built-in trackpad when mouse or wireless trackpad is present” setting.

## 7. Why there is relatively little Mac code

The implementation is small because responsibility is deliberately narrow:

- no gesture recognizer;
- no virtual device or kernel extension on macOS;
- no UI, pairing, installer, or background daemon yet;
- no direct USB device-mode path;
- shared protocol codec instead of a second serialization implementation.

The difficult work was validating the private sensor path and defining failure-safe boundaries, not
writing a large application. Keeping this side small reduces breakage when macOS private ABI changes.

## Remaining macOS work

- test additional Apple Silicon generations and macOS releases;
- package a signed menu-bar/background agent;
- add discovery, pairing, authentication, and encrypted transport;
- handle sleep/wake and interface changes;
- investigate a reduced-function public-API backend;
- add calibrated device metadata only after cross-model measurement.
