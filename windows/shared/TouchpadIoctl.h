#pragma once

#if defined(_KERNEL_MODE)
#define MTP_UINT8 UINT8
#define MTP_UINT16 UINT16
#define MTP_UINT32 UINT32
#define MTP_INT32 INT32
#else
#include <stdint.h>
#define MTP_UINT8 uint8_t
#define MTP_UINT16 uint16_t
#define MTP_UINT32 uint32_t
#define MTP_INT32 int32_t
#endif

#define MTP_IOCTL_ABI_VERSION 1u
#define MTP_HID_MAX_CONTACTS 5u
#define MTP_COORDINATE_LOGICAL_MAX 32767u

#define MTP_IOCTL_CONTACT_IN_RANGE 0x01u
#define MTP_IOCTL_CONTACT_TIP 0x02u
#define MTP_IOCTL_CONTACT_CONFIDENCE 0x04u
#define MTP_IOCTL_FRAME_RESET 0x00000001u
#define MTP_IOCTL_FRAME_BUTTON 0x00000002u

#pragma pack(push, 1)
typedef struct MTP_IOCTL_CONTACT {
    MTP_UINT8 slot;
    MTP_UINT8 flags;
    MTP_UINT16 x;
    MTP_UINT16 y;
    MTP_UINT16 reserved;
} MTP_IOCTL_CONTACT;

typedef struct MTP_IOCTL_FRAME {
    MTP_UINT32 abi_version;
    MTP_UINT32 sequence;
    MTP_UINT32 flags;
    MTP_UINT16 scan_time_100us;
    MTP_UINT8 active_contact_count;
    MTP_UINT8 report_contact_count;
    MTP_IOCTL_CONTACT contacts[MTP_HID_MAX_CONTACTS];
} MTP_IOCTL_FRAME;

typedef struct MTP_IOCTL_STATUS {
    MTP_UINT32 abi_version;
    MTP_UINT32 submit_count;
    MTP_INT32 last_submit_status;
    MTP_UINT16 last_report_size;
    MTP_UINT8 last_active_contact_count;
    MTP_UINT8 input_mode;
    MTP_UINT8 function_switch;
    MTP_UINT8 reserved[3];
    MTP_UINT32 get_feature_count;
    MTP_UINT32 set_feature_count;
} MTP_IOCTL_STATUS;
#pragma pack(pop)

#undef MTP_UINT8
#undef MTP_UINT16
#undef MTP_UINT32
#undef MTP_INT32

#if defined(_WIN32)
#define MTP_CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#define FILE_DEVICE_MTP_TOUCHPAD 0x8000u
#define IOCTL_MTP_SUBMIT_FRAME MTP_CTL_CODE(FILE_DEVICE_MTP_TOUCHPAD, 0x800u, 0u, 0x0002u)
#define IOCTL_MTP_QUERY_STATUS MTP_CTL_CODE(FILE_DEVICE_MTP_TOUCHPAD, 0x801u, 0u, 0x0001u)
#endif

#if defined(__cplusplus)
static_assert(sizeof(MTP_IOCTL_CONTACT) == 8);
static_assert(sizeof(MTP_IOCTL_FRAME) == 56);
static_assert(sizeof(MTP_IOCTL_STATUS) == 28);
#endif
