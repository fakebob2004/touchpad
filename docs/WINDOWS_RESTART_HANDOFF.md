# Windows restart and real-Mac integration handoff

Date: 2026-07-23

## Current user report

The first real Mac-to-Windows session already achieved:

- Windows pointer movement from the MacBook trackpad;
- native two-finger scrolling;
- native pinch zoom.

Two follow-up findings were reported:

1. Vertical pointer motion was reversed.
2. After restarting the Windows receiver and Mac agent, the integration no
   longer worked.

The exact restart failure output is not yet known. Do not assume the kernel
driver is broken until the receiver binary, listening process, firewall, and
driver handle have been checked separately.

## Changes arriving from macOS side

Pull the latest `agent/windows-vhf-touchpad` branch before diagnosing.

The relevant changes are:

- `windows/receiver/TouchSession.cpp` now maps
  `hid_y = clamp(1 - mac_y)`, based on the real-device test;
- `windows/tests/MtpTests.cpp` verifies the inverted and clamped Y mapping;
- `mac/Agent/main.c` sends strictly increasing sequence numbers for
  `HELLO`, `RESET`, and every `FRAME`;
- queue overflow now forces a fresh TCP session rather than creating an
  invalid sequence gap;
- the Mac agent does **not** intercept local macOS events.

The Y-axis fix is user-mode receiver code. Restarting an old
`mtp-receiver-diag2.exe` does not apply it; the receiver must be rebuilt or
replaced. The kernel driver does not need reinstalling for this particular
change.

## Required Windows procedure

Run from an x64 Native Tools/Developer PowerShell.

### 1. Update the branch

```powershell
git fetch origin
git switch agent/windows-vhf-touchpad
git pull --ff-only
git rev-parse --short HEAD
```

Record the resulting commit in the response section below.

### 2. Stop every stale receiver

```powershell
Get-Process mtp-receiver* -ErrorAction SilentlyContinue |
    Stop-Process -Force

Get-NetTCPConnection -LocalPort 39871 -State Listen -ErrorAction SilentlyContinue
```

The second command should return nothing before starting the new receiver. If
another process still owns the port, record:

```powershell
Get-NetTCPConnection -LocalPort 39871 -State Listen |
    Select-Object LocalAddress,LocalPort,OwningProcess
```

### 3. Rebuild receiver and tests

The portable repository build is:

```powershell
cmake -S windows -B out\windows -A x64
cmake --build out\windows --config Release
ctest --test-dir out\windows -C Release --output-on-failure
```

Expected test result:

```text
mtp-tests: passed
```

Use the newly built executable:

```powershell
.\out\windows\Release\mtp-receiver.exe 39871
```

If the diagnostic-only executable
`out\manual\mtp-receiver-diag2.exe` must remain in use, rebuild that target
from the same updated `Mtp1.cpp` and `TouchSession.cpp`; do not restart a stale
copy.

### 4. Verify driver presence before Mac connects

Receiver startup must show:

```text
listening on TCP 39871
```

It must not show:

```text
driver not present; parse/log mode
```

If it does, collect:

```powershell
Get-PnpDevice -PresentOnly |
    Where-Object InstanceId -Like 'ROOT\HIDCLASS\*' |
    Format-List Status,Class,FriendlyName,InstanceId,Problem,ProblemStatus

Get-Service MtpVhfTouchpad -ErrorAction SilentlyContinue |
    Format-List Name,Status,StartType

Get-CimInstance Win32_PnPSignedDriver |
    Where-Object DeviceName -Match 'Mtp|Touchpad' |
    Select-Object DeviceName,DriverVersion,InfName,IsSigned
```

Do not reinstall the driver merely for Y inversion. Reinstall only if the
source device is absent or has a PnP problem.

### 5. Prepare network access

From the repository root in Administrator PowerShell:

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\windows\tools\prepare_receiver.ps1
```

Expected endpoint observed on 2026-07-23:

```text
192.168.31.115:39871
```

Confirm:

```powershell
Get-NetTCPConnection -LocalPort 39871 -State Listen
Get-NetFirewallRule -DisplayName 'MTP Touchpad Receiver (TCP 39871)' |
    Format-List Enabled,Profile,Direction,Action
```

### 6. Ask Mac to connect

On macOS:

```sh
nc -vz 192.168.31.115 39871
./build/mac-touch-agent 192.168.31.115 39871
```

The Mac should print `connected`. During movement, its final summary must have
nonzero `captured` and `sent` counts.

### 7. Acceptance test

Verify in this order:

1. Single finger moves the Windows pointer.
2. Moving toward the top of the Mac trackpad moves the Windows pointer upward.
3. Two-finger scrolling works in the expected direction.
4. Pinch zoom works.
5. Disconnecting the Mac releases all contacts within 200 ms.
6. Reconnecting succeeds without restarting the driver.

After disconnect, the diagnostic receiver should report:

```text
driver status: submits=... last_ntstatus=0x0 report_bytes=50
active=0 input_mode=3 function=0x3 get_feature=... set_feature=...
```

Healthy conditions:

- `submits > 0`;
- `last_ntstatus=0x0`;
- `report_bytes=50`;
- `input_mode=3`;
- `function=0x3`.

## Windows-side response requested

Please update this section or send the values back to the macOS side:

```text
Commit:
Receiver executable path:
Receiver build timestamp:
ctest result:
Windows IPv4:
Port 39871 listening PID:
Driver present (yes/no):
Receiver printed parse/log mode (yes/no):
Mac connection accepted (yes/no):
Driver submits:
last_ntstatus:
report_bytes:
input_mode:
function_switch:
Pointer movement:
Vertical direction:
Two-finger scroll:
Pinch zoom:
Exact restart failure, if still present:
```
