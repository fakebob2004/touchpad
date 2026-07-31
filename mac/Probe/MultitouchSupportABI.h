#ifndef MULTITOUCH_SUPPORT_ABI_H
#define MULTITOUCH_SUPPORT_ABI_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// MultitouchSupport is a private framework and ships without public headers.
// Keep every inferred ABI declaration in this file so breakage is easy to audit.
typedef void *MTDeviceRef;

typedef struct {
    float x;
    float y;
} MTPoint;

typedef struct {
    MTPoint position;
    MTPoint velocity;
} MTReadout;

typedef struct {
    int32_t frame;
    double timestamp;
    int32_t identifier;
    int32_t state;
    int32_t finger_id;
    int32_t hand_id;
    MTReadout normalized;
    float size;
    float pressure;
    float angle;
    float major_axis;
    float minor_axis;
    MTReadout millimeters;
    int32_t reserved[2];
    float density;
} MTTouch;

typedef void (*MTContactFrameCallback)(MTDeviceRef device,
                                       MTTouch *touches,
                                       size_t touch_count,
                                       double timestamp,
                                       size_t frame);
typedef void (*MTButtonStateCallback)(MTDeviceRef device,
                                      int32_t pressed,
                                      int32_t released,
                                      void *context);

typedef CFArrayRef (*MTDeviceCreateListFn)(void);
typedef void (*MTRegisterContactFrameCallbackFn)(MTDeviceRef, MTContactFrameCallback);
typedef void (*MTDeviceStartFn)(MTDeviceRef, int32_t);
typedef bool (*MTDeviceIsBuiltInFn)(MTDeviceRef);

_Static_assert(sizeof(MTPoint) == 8, "Unexpected MTPoint layout");
_Static_assert(sizeof(MTReadout) == 16, "Unexpected MTReadout layout");
_Static_assert(sizeof(MTTouch) == 96, "Unexpected MTTouch layout");

#endif
