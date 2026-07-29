#include "TouchFrame.h"

#include <string.h>

static void put_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void put_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void put_u64(uint8_t *output, uint64_t value) {
    put_u32(output, (uint32_t)(value >> 32));
    put_u32(output + 4, (uint32_t)value);
}

static void put_f32(uint8_t *output, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u32(output, bits);
}

static uint16_t get_u16(const uint8_t *input) {
    return (uint16_t)((uint16_t)input[0] << 8 | input[1]);
}

static uint32_t get_u32(const uint8_t *input) {
    return (uint32_t)input[0] << 24 | (uint32_t)input[1] << 16 |
           (uint32_t)input[2] << 8 | input[3];
}

static uint64_t get_u64(const uint8_t *input) {
    return (uint64_t)get_u32(input) << 32 | get_u32(input + 4);
}

static float get_f32(const uint8_t *input) {
    const uint32_t bits = get_u32(input);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

size_t tp_encode_message(enum TPMessageType type,
                         const TPFrame *frame,
                         uint8_t *destination,
                         size_t capacity) {
    if (destination == NULL || frame == NULL || frame->contact_count > TP_MAX_CONTACTS) {
        return 0;
    }
    if (type != TP_MESSAGE_HELLO && type != TP_MESSAGE_FRAME && type != TP_MESSAGE_RESET) {
        return 0;
    }

    const uint16_t count = type == TP_MESSAGE_FRAME ? frame->contact_count : 0;
    const uint16_t flags = type == TP_MESSAGE_FRAME ? frame->flags : 0;
    if ((flags & ~TP_FRAME_BUTTON) != 0) {
        return 0;
    }
    const uint32_t payload_size = (uint32_t)count * TP_CONTACT_SIZE;
    const size_t message_size = TP_HEADER_SIZE + payload_size;
    if (capacity < message_size) {
        return 0;
    }

    put_u32(destination, TP_WIRE_MAGIC);
    put_u16(destination + 4, TP_WIRE_VERSION);
    put_u16(destination + 6, (uint16_t)type);
    put_u32(destination + 8, payload_size);
    put_u32(destination + 12, frame->sequence);
    put_u16(destination + 16, count);
    put_u16(destination + 18, flags);
    put_u64(destination + 20, frame->capture_time_us);
    put_u64(destination + 28, frame->device_time_us);

    for (uint16_t index = 0; index < count; ++index) {
        const TPContact *contact = &frame->contacts[index];
        uint8_t *output = destination + TP_HEADER_SIZE + index * TP_CONTACT_SIZE;
        put_u32(output, contact->identifier);
        output[4] = contact->state;
        output[5] = contact->flags;
        put_u16(output + 6, 0);
        put_f32(output + 8, contact->x);
        put_f32(output + 12, contact->y);
        put_f32(output + 16, contact->velocity_x);
        put_f32(output + 20, contact->velocity_y);
        put_f32(output + 24, contact->size);
        put_f32(output + 28, contact->angle);
        put_f32(output + 32, contact->major_axis);
        put_f32(output + 36, contact->minor_axis);
        put_f32(output + 40, contact->density);
    }
    return message_size;
}

bool tp_decode_message(const uint8_t *source,
                       size_t length,
                       enum TPMessageType *type,
                       TPFrame *frame) {
    if (source == NULL || type == NULL || frame == NULL || length < TP_HEADER_SIZE ||
        get_u32(source) != TP_WIRE_MAGIC || get_u16(source + 4) != TP_WIRE_VERSION) {
        return false;
    }
    const uint16_t raw_type = get_u16(source + 6);
    if (raw_type < TP_MESSAGE_HELLO || raw_type > TP_MESSAGE_RESET) {
        return false;
    }
    const uint32_t payload_size = get_u32(source + 8);
    const uint16_t count = get_u16(source + 16);
    const uint16_t flags = get_u16(source + 18);
    if (count > TP_MAX_CONTACTS || payload_size != (uint32_t)count * TP_CONTACT_SIZE ||
        length != TP_HEADER_SIZE + payload_size ||
        (flags & ~TP_FRAME_BUTTON) != 0 ||
        (raw_type != TP_MESSAGE_FRAME && (count != 0 || flags != 0))) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    *type = (enum TPMessageType)raw_type;
    frame->sequence = get_u32(source + 12);
    frame->flags = flags;
    frame->contact_count = count;
    frame->capture_time_us = get_u64(source + 20);
    frame->device_time_us = get_u64(source + 28);
    for (uint16_t index = 0; index < count; ++index) {
        TPContact *contact = &frame->contacts[index];
        const uint8_t *input = source + TP_HEADER_SIZE + index * TP_CONTACT_SIZE;
        contact->identifier = get_u32(input);
        contact->state = input[4];
        contact->flags = input[5];
        contact->x = get_f32(input + 8);
        contact->y = get_f32(input + 12);
        contact->velocity_x = get_f32(input + 16);
        contact->velocity_y = get_f32(input + 20);
        contact->size = get_f32(input + 24);
        contact->angle = get_f32(input + 28);
        contact->major_axis = get_f32(input + 32);
        contact->minor_axis = get_f32(input + 36);
        contact->density = get_f32(input + 40);
    }
    return true;
}
