#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

#include "..\shared\TouchpadIoctl.h"

#define MTP_REPORT_ID_INPUT 0x05
#define MTP_REPORT_ID_MODE 0x04
#define MTP_REPORT_ID_FUNCTION 0x06
#define MTP_REPORT_ID_CAPS 0x07
#define MTP_REPORT_ID_CERTIFICATION 0x08

#pragma pack(push, 1)
typedef struct MTP_PTP_CONTACT {
    UCHAR Flags;
    ULONG ContactId;
    USHORT X;
    USHORT Y;
} MTP_PTP_CONTACT;

typedef struct MTP_PTP_REPORT {
    UCHAR ReportId;
    MTP_PTP_CONTACT Contacts[MTP_HID_MAX_CONTACTS];
    USHORT ScanTime;
    UCHAR ContactCount;
    UCHAR Button;
} MTP_PTP_REPORT;
#pragma pack(pop)

typedef struct DEVICE_CONTEXT {
    VHFHANDLE VhfHandle;
    WDFWAITLOCK SubmitLock;
    USHORT LastScanTime;
    volatile LONG SubmitCount;
    NTSTATUS LastSubmitStatus;
    USHORT LastReportSize;
    UCHAR LastActiveContactCount;
    UCHAR InputMode;
    UCHAR FunctionSwitch;
    volatile LONG GetFeatureCount;
    volatile LONG SetFeatureCount;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD MtpEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP MtpEvtDeviceCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL MtpEvtIoDeviceControl;
EVT_VHF_ASYNC_OPERATION MtpEvtVhfGetFeature;
EVT_VHF_ASYNC_OPERATION MtpEvtVhfSetFeature;

extern UCHAR g_MtpReportDescriptor[];
extern const USHORT g_MtpReportDescriptorLength;
extern const UCHAR g_MtpCertificationBlob[256];
