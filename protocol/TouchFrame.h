#ifndef TOUCH_FRAME_H
#define TOUCH_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TP_WIRE_MAGIC 0x4d545031u /* "MTP1" */
#define TP_WIRE_VERSION 1u
#define TP_MAX_CONTACTS 10u
#define TP_HEADER_SIZE 36u
#define TP_CONTACT_SIZE 44u
#define TP_MAX_MESSAGE_SIZE (TP_HEADER_SIZE + TP_MAX_CONTACTS * TP_CONTACT_SIZE)

enum TPMessageType {
    TP_MESSAGE_HELLO = 1,
    TP_MESSAGE_FRAME = 2,
    TP_MESSAGE_RESET = 3,
};

enum TPContactFlags {
    TP_CONTACT_IN_RANGE = 1u << 0,
    TP_CONTACT_TIP = 1u << 1,
    TP_CONTACT_CONFIDENCE = 1u << 2,
};

typedef struct {
    uint32_t identifier;
    uint8_t state;
    uint8_t flags;
    float x;
    float y;
    float velocity_x;
    float velocity_y;
    float size;
    float angle;
    float major_axis;
    float minor_axis;
    float density;
} TPContact;

typedef struct {
    uint32_t sequence;
    uint64_t capture_time_us;
    uint64_t device_time_us;
    uint16_t contact_count;
    TPContact contacts[TP_MAX_CONTACTS];
} TPFrame;

// All multi-byte fields, including IEEE-754 float bit patterns, use big-endian byte order.
size_t tp_encode_message(enum TPMessageType type,
                         const TPFrame *frame,
                         uint8_t *destination,
                         size_t capacity);

bool tp_decode_message(const uint8_t *source,
                       size_t length,
                       enum TPMessageType *type,
                       TPFrame *frame);

#endif
