# KMDF VHF source driver fails in `VhfCreate`

> Resolved on 2026-07-23. The INF did not install the in-box `vhf.sys` driver
> as the source device's lower filter. Adding the `.NT.HW` section shown below
> made both the minimal mouse and Precision Touchpad configurations start:
>
> ```ini
> [MtpVhfTouchpad_Install.NT.HW]
> AddReg=MtpVhfTouchpad_Vhf_AddReg
>
> [MtpVhfTouchpad_Vhf_AddReg]
> HKR,,"LowerFilters",0x00010000,"vhf"
> ```

## Goal

Create a root-enumerated KMDF Virtual HID Framework source driver that exposes a
Windows Precision Touchpad. A user-mode receiver opens
`\\.\MtpVhfTouchpad` and sends fixed-size touch frames through an IOCTL.

## Environment

- Windows 11 x64
- Secure Boot: disabled
- `TESTSIGNING`: enabled
- Visual Studio Community 2026 18.8
- Windows SDK: `10.0.26100.0`
- WDK NuGet: `Microsoft.Windows.WDK.x64 10.0.26100.6584`
- KMDF: 1.33
- Driver hardware ID: `Root\MtpVhfTouchpad`
- Driver class: `HIDClass`
- VHF IDs: VID `0x1209`, PID `0x3987`

## Signing and installation

A local RSA/SHA-256 code-signing certificate is installed in:

- `LocalMachine\My`
- `LocalMachine\Root`
- `LocalMachine\TrustedPublisher`

Both SYS and CAT pass:

```text
signtool verify /pa
Status: Valid
```

`Inf2Cat /os:10_X64` completes with no errors or warnings.

The root device is created with:

```powershell
devcon install MtpVhfTouchpad.inf Root\MtpVhfTouchpad
```

The resulting instance is:

```text
ROOT\HIDCLASS\0001
```

The driver package is selected and installed successfully.

## Failure

PnP reports:

```text
Status: Error
Problem: CM_PROB_FAILED_ADD
Problem code: 31
Initial problem status: 0xC0000010 (STATUS_INVALID_DEVICE_REQUEST)
```

The service exists but is stopped:

```text
SERVICE_NAME: MtpVhfTouchpad
TYPE: KERNEL_DRIVER
STATE: STOPPED
WIN32_EXIT_CODE: 1077
```

`setupapi.dev.log` shows:

```text
Device 'ROOT\HIDCLASS\0001' not started:
Device has problem: 0x1f (CM_PROB_FAILED_ADD),
problem status: 0xc0000010.
```

## Exact location isolated

`EvtDeviceAdd` performs:

1. `WdfDeviceCreate`
2. `WdfDeviceCreateSymbolicLink`
3. `WdfWaitLockCreate`
4. `WdfIoQueueCreate`
5. `VHF_CONFIG_INIT`
6. `VhfCreate`
7. `VhfStart`

For diagnosis, failures were remapped:

```c
status = VhfCreate(&vhfConfig, &context->VhfHandle);
if (!NT_SUCCESS(status)) {
    return STATUS_DEVICE_CONFIGURATION_ERROR; // 0xC0000182
}

status = VhfStart(context->VhfHandle);
if (!NT_SUCCESS(status)) {
    VhfDelete(context->VhfHandle, TRUE);
    context->VhfHandle = NULL;
    return STATUS_DEVICE_NOT_READY; // 0xC00000A3
}
```

After installing this diagnostic build, PnP reports:

```text
DEVPKEY_Device_ProblemStatus = 3221225858 = 0xC0000182
```

Therefore the failure is definitely returned by `VhfCreate`, before
`VhfStart`.

## VHF configuration

```c
VHF_CONFIG_INIT(
    &vhfConfig,
    WdfDeviceWdmGetDeviceObject(device),
    g_MtpReportDescriptorLength,
    g_MtpReportDescriptor);

vhfConfig.VhfClientContext = context;
vhfConfig.VendorID = 0x1209;
vhfConfig.ProductID = 0x3987;
vhfConfig.VersionNumber = 1;
vhfConfig.EvtVhfAsyncOperationGetFeature = MtpEvtVhfGetFeature;
vhfConfig.EvtVhfAsyncOperationSetFeature = MtpEvtVhfSetFeature;

status = VhfCreate(&vhfConfig, &context->VhfHandle);
```

`VhfCreate` is called from `EvtDeviceAdd`, at PASSIVE_LEVEL, after a successful
`WdfDeviceCreate`, as required by Microsoft documentation.

`OperationContextSize` remains zero. The callbacks do not use their operation
context and complete requests synchronously with
`VhfAsyncOperationComplete`.

## Descriptor experiments

### Precision Touchpad descriptor

The original descriptor contains:

- Digitizer / Touch Pad application collection
- Five Finger logical collections
- Input report ID `0x05`
- Device capabilities feature report ID `0x07`
- 256-byte PTPHQA report ID `0x08`
- Digitizer / Configuration application collection
- Input Mode report ID `0x04`
- Function Switch report ID `0x06`

Two descriptor issues were found and corrected:

1. Digitizer Usage Page is now restored before every Finger collection.
2. Input Mode is wrapped in a Finger/Logical collection and Function Switch in
   a Physical collection, matching `mac-precision-touchpad`.

`VhfCreate` still fails.

### Minimal mouse isolation descriptor

The entire PTP descriptor was then replaced with a conventional minimal
three-button relative mouse descriptor:

```c
UCHAR g_MtpReportDescriptor[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
    0xC0, 0xC0
};
```

The minimal mouse build also fails in `VhfCreate` with the same diagnostic
status. This strongly suggests that the PTP report descriptor is not the root
cause.

## INF

Relevant sections:

```ini
[Version]
Signature="$WINDOWS NT$"
Class=HIDClass
ClassGuid={745A17A0-74D3-11D0-B6FE-00A0C90F57DA}
Provider=%ManufacturerName%
CatalogFile=MtpVhfTouchpad.cat
PnpLockdown=1

[Manufacturer]
%ManufacturerName%=Standard,NTamd64

[Standard.NTamd64]
%DeviceName%=MtpVhfTouchpad_Install,Root\MtpVhfTouchpad

[MtpVhfTouchpad_Install.NT]
CopyFiles=DriverCopyFiles

[MtpVhfTouchpad_Install.NT.Services]
AddService=MtpVhfTouchpad,0x00000002,MtpVhfTouchpad_Service

[MtpVhfTouchpad_Service]
ServiceType=1
StartType=3
ErrorControl=1
ServiceBinary=%12%\MtpVhfTouchpad.sys
```

## Questions for comparison with known working VHF projects

1. Must a root-enumerated VHF source driver use `Class=System` or another setup
   class instead of `Class=HIDClass` for its parent/source device?
2. Does the INF need specific VHF, KMDF, device-interface, security, or
   isolation directives?
3. Is `WdfDeviceInitSetExclusive(deviceInit, TRUE)` incompatible with VHF?
4. Are asynchronous Get/Set Feature callbacks invalid when
   `OperationContextSize == 0`, even if the callback does not need context?
5. Should VHF creation occur in `EvtDevicePrepareHardware` or
   `EvtDeviceSelfManagedIoInit` instead of `EvtDeviceAdd` for a root device?
6. Does VHF reject `WdfDeviceWdmGetDeviceObject(device)` when the parent device
   itself belongs to `HIDClass`?
7. Is a required INF dependency/service entry for `vhf.sys` missing?
8. What are known causes of `VhfCreate` returning
   `STATUS_INVALID_DEVICE_REQUEST (0xC0000010)` with a valid minimal mouse
   descriptor?

## Relevant local files

- `windows/driver/Driver.c`
- `windows/driver/Driver.h`
- `windows/driver/Descriptor.c`
- `windows/driver/MtpVhfTouchpad.inf`
- `windows/driver/MtpVhfTouchpad.vcxproj`
